# Numbers: parsing, formatting, and explicit conversion (SPEC-030c)

Import `@std` for these functions and `@status` for status constants. The public
implementation is [stdlib/std.gloin](../stdlib/std.gloin); results are ordinary
structs. There are no implicit casts or implicit number-to-string conversions.
The existing `to_int`, `to_string`, and `input` contracts remain unchanged.

## Parse, inspect, format

```gloin
import "@std";
import "@status";
import "@arena";

def main() -> i32 {
    def parsed: std.F64Result = std.parse_f64("+1.25e0");
    if parsed.status != status.OK { return 1; }
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def text: std.FormatResult = std.format_f64_fixed(&memory, parsed.value, 2);
    if text.status != status.OK { return 2; }
    std.println(text.value);
    return 0;
}
```

This prints `1.25` and returns zero. Check `status` before using `value`: failure
payloads are integer zero, positive floating zero, `false`, or static `""`.
`OK` is 0, `INVALID` 2, `OVERFLOW` 3, `NO_MEMORY` 6, `INEXACT` 8, and `UNDERFLOW` 9.
Existing statuses keep their values. Both new statuses are also aliases in `std`.
Successful parse/conversion results own no storage and retain no input reference.

## Parsers

All parsers take a counted `string`. They inspect the entire input; whitespace,
embedded NUL, non-ASCII digits, underscores, and numeric prefixes are invalid.
Call `strings.trim_ascii` explicitly if surrounding whitespace is acceptable.
Malformed input returns `INVALID` even if a numeric prefix would overflow.

| Call example | Return type | Successful value / grammar | Failure statuses | Cost |
| --- | --- | --- | --- | --- |
| `std.parse_i32("-42")` | `std.IntResult` | `-42`; `[+-]?[0-9]+` | `INVALID`, `OVERFLOW` | O(n), no allocation |
| `std.parse_i64("-9223372036854775808")` | `std.I64Result` | i64 minimum; same signed grammar | `INVALID`, `OVERFLOW` | O(n), no allocation |
| `std.parse_u64("+18446744073709551615")` | `std.U64Result` | u64 maximum; `[+]?[0-9]+` | `INVALID`, `OVERFLOW` | O(n), no allocation |
| `std.parse_f32(".125e1")` | `std.F32Result` | `1.25`; decimal float grammar below | `INVALID`, `OVERFLOW`, `UNDERFLOW` | O(n), no allocation |
| `std.parse_f64("+1.25E+0")` | `std.F64Result` | `1.25`; decimal float grammar below | `INVALID`, `OVERFLOW`, `UNDERFLOW` | O(n), no allocation |
| `std.parse_bool("true")` | `std.BoolResult` | `true`; exactly `true` or `false` | `INVALID` | O(1), no allocation |

Here n is input bytes; numeric parsers use bounded auxiliary storage independent
of n and impose no arbitrary text-length limit. `parse_i32` is the named equivalent
of `to_int`. A minus sign is always invalid for `parse_u64`, including `-0`.
Integer ranges are inclusive: i32 −2³¹…2³¹−1, i64 −2⁶³…2⁶³−1, u64 0…2⁶⁴−1.
Leading zeroes and a leading plus are accepted.

Float grammar is `[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`.
Examples `42`, `1.`, `.5`, and `1e-3` are valid. `NaN`, infinity, and hexadecimal
floats are invalid. Parsing rounds directly to the requested width, nearest with
ties to even: `parse_f32` does not parse to f64 and then narrow. Finite subnormals
are accepted. Rounding a nonzero value to zero reports `UNDERFLOW`; rounding to
infinity reports `OVERFLOW`. A zero mantissa with any valid exponent succeeds;
negative zero retains its sign. Decimal separators are always `.`, independent
of the process locale.

## Formatters

All numeric formatters return `std.FormatResult { status, value }`. They take
`&arena.GeneralArena` first, followed by a value of the width named in the function.
The fixed forms take a final `precision: u32` in 0…18. No implicit widening occurs.
In the examples below, `memory` is a mutable `arena.GeneralArena`.

| Call example | Output | Bytes requested from arena, including NUL |
| --- | --- | --- |
| `std.format_i32(&memory, -42)` | `"-42"` | 12 |
| `std.format_i64(&memory, -9223372036854775808)` | `"-9223372036854775808"` | 21 |
| `std.format_u64(&memory, 18446744073709551615)` | `"18446744073709551615"` | 21 |
| `std.format_f32(&memory, 1.25)` | `"1.25"` | 32 |
| `std.format_f64(&memory, 1.25)` | `"1.25"` | 32 |
| `std.format_f32_fixed(&memory, 2.5, 0)` | `"2"` | 64 |
| `std.format_f64_fixed(&memory, 1.25, 3)` | `"1.250"` | 352 |
| `std.format_bool(true)` | `"true"`, directly as `string` | None; static literal |

The successful numeric result borrows its destination arena until reset/free.
It is an independent snapshot and cannot be individually freed. The string length
excludes the NUL terminator. Each numeric formatter makes one byte-allocation request;
`NO_MEMORY` is recoverable. A failed allocated buffer stays in the arena until reset/free.
The request sizes above exclude allocator metadata, alignment, and slab capacity;
see [arena costs](arenas.md). Numeric byte work and auxiliary storage are bounded
O(1) for these fixed widths and precision limits, plus the arena allocation cost.
`format_bool` is O(1), cannot fail, returns `"false"` for false, and needs no arena.

Short float formatting uses the shortest decimal spelling that round-trips through
the same-width parser; whole values need no `.0`. It chooses fixed or scientific
notation by shortest length (fixed on a tie). Scientific notation uses lowercase
`e`, an explicit exponent sign, and at least two exponent digits. Negative zero
formats as `-0`. Fixed formatting uses exactly `precision` digits after the dot,
or no dot for precision zero, with nearest/ties-even rounding of the actual binary
value. Thus `2.5` at precision zero is `2`, `3.5` is `4`, and `-0.125` at precision
two is `-0.12`. A negative sign survives rounding to zero. Decimal text is locale
independent. Precision above 18 returns `INVALID` **before allocation**.

Native nonfinite values are rejected with `INVALID`; ordinary Gloin arithmetic
already traps on nonfinite results. Internal buffer overflow returns `OVERFLOW`,
although the documented buffer sizes suffice for every supported value/precision.
The older `to_string(&memory, i32)` still returns a plain string and traps on
allocation failure. Choose `format_i32` when allocation failure must be recoverable.

## Explicit conversion

Every conversion below is O(1), performs no allocation, and returns a concrete
status/value struct. An integer failure payload is zero, a floating failure payload
is positive zero. Floating input must be finite; native nonfinite input gives
`INVALID`. Successful floating zeros preserve their sign when the destination is
floating. The rounding environment is the core runtime's default nearest/ties-even.

| Function and example argument | Return type | Rule / example result |
| --- | --- | --- |
| `std.i64_from_i32(42)` | `I64Result` | Exact widening, always `OK/42` |
| `std.i32_from_i64(42)` | `IntResult` | `OK/42`; outside i32 range gives `OVERFLOW` |
| `std.u64_from_i64(42)` | `U64Result` | `OK/42`; negative values give `OVERFLOW` |
| `std.i64_from_u64(42)` | `I64Result` | `OK/42`; above i64 maximum gives `OVERFLOW` |
| `std.f64_from_i64_exact(42)` | `F64Result` | `OK/42.0`; discarded integer bits give `INEXACT` |
| `std.f64_from_i64_rounded(42)` | `F64Result` | `OK/42.0`; nearest/ties-even, may lose precision |
| `std.f64_from_u64_exact(42)` | `F64Result` | `OK/42.0`; discarded integer bits give `INEXACT` |
| `std.f64_from_u64_rounded(42)` | `F64Result` | `OK/42.0`; nearest/ties-even, may lose precision |
| `std.i64_from_f64_exact(42.0)` | `I64Result` | `OK/42`; fraction gives `INEXACT`; range failure gives `OVERFLOW` first |
| `std.i64_from_f64_trunc(-42.9)` | `I64Result` | `OK/-42`; truncate toward zero, then check range |
| `std.u64_from_f64_exact(42.0)` | `U64Result` | `OK/42`; fraction gives `INEXACT`; range failure gives `OVERFLOW` first |
| `std.u64_from_f64_trunc(42.9)` | `U64Result` | `OK/42`; truncate toward zero, then check range |
| `std.f64_from_f32(1.25)` | `F64Result` | Exact widening, including subnormals; `OK/1.25` |
| `std.f32_from_f64_exact(1.25)` | `F32Result` | `OK/1.25`; precision loss gives `INEXACT`, with range rules below |
| `std.f32_from_f64_rounded(1.25)` | `F32Result` | `OK/1.25`; nearest/ties-even, with range rules below |

Result type names in the table are in `std`. For float-to-integer conversion, range
checking uses the truncated candidate before testing fractional loss. For example,
`u64_from_f64_trunc(-0.5)` succeeds with zero, `u64_from_f64_exact(-0.5)` gives
`INEXACT`, and either conversion of `-1.5` gives `OVERFLOW`. Nothing wraps or saturates.
For f64-to-f32, magnitude above the largest finite f32 gives `OVERFLOW` before
rounding, and nonzero rounding to zero gives `UNDERFLOW` in both modes. Other
finite precision loss is permitted only by the rounded form. These choices are
explicit in the function names, not hidden behind a general cast.

```gloin
import "@std";
import "@status";
import "@arena";

def main() -> i32 {
    def exact: std.F64Result = std.f64_from_i64_exact(9007199254740993);
    if exact.status != status.INEXACT { return 1; }
    def rounded: std.F64Result = std.f64_from_i64_rounded(9007199254740993);
    if rounded.status != status.OK || rounded.value != 9007199254740992.0 { return 2; }
    def whole: std.I64Result = std.i64_from_f64_trunc(42.9);
    if whole.status != status.OK { return 3; }
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def text: std.FormatResult = std.format_i64(&memory, whole.value);
    if text.status != status.OK { return 4; }
    std.println(text.value);
    return 0;
}
```

This prints `42`. [numbers_lab.gloin](../examples/numbers_lab.gloin) combines bounded
stdin, trimming, parsing, checked conversion, an incremental mean, and formatting.
Run `printf '1.25\n2.75\n3.5\n' | build/gloinc examples/numbers_lab.gloin` to get
`count=3` and `mean=2.50`. Input and formatting storage is reused explicitly via arena reset.

## Implementation and verification

Public wrappers live in `std.gloin`. Narrow private primitives provide native
parsing, formatting, and numeric conversion in
[src/numeric_runtime.cpp](../src/numeric_runtime.cpp). Float parsing uses pinned
[fast_float v8.3.0](../third_party/fast_float/README.md), vendored under MIT with
checksums; building needs no dependency download. Float formatting uses C++
`to_chars`. The dependency license/provenance ship in installed and relocated packages.

`NumericRuntimeTest` checks boundaries, direct-width midpoint rounding, subnormals,
signed zero, deterministic bit-pattern round trips, locale independence, guard bytes,
and conversion range checks before native casts. `NumericLibraryTest` exercises
every public API, failure payloads, allocation failure, source/privacy checks,
external LLVM execution, this guide's full programs, and the packaged example.
