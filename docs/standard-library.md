# Standard input and integer conversions (SPEC-030, 0.2.0)

This guide describes the implemented SPEC-030 input and integer conversion API.
The later modules have dedicated guides for [strings](strings.md),
[text construction](text-construction.md), [numbers](numbers.md), [I/O](io.md),
[filesystem and processes](filesystem-process.md), [math](math.md), and
[time and randomness](time-random.md).

The public API lives in [stdlib/std.gloin](../stdlib/std.gloin). These are ordinary
Gloin functions compiled from source. Input, parsing and formatting return
built-in results. Native primitives handle byte input and decimal conversion.
Output remains string-only and never changes
`main`'s return value.

```gloin
import "@std";
import "@arena";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    std.print("number: ");
    def input: result<std.InputLine> = std.input(&memory, 128);
    if input.erroneous {
        std.println(input.error.message);
        return 1;
    }
    def line: std.InputLine = input.value;
    if line.end { return 0; }
    def number: result<i32> = std.parse_i32(line.value);
    if number.erroneous {
        std.println(number.error.message);
        return 2;
    }
    def text: result<string> = std.to_string(&memory, number.value);
    if text.erroneous {
        std.println(text.error.message);
        return 3;
    }
    std.println(text.value);
    return 0;
}
```

## API and ownership

| Function | Result and allocation |
| --- | --- |
| `std.input(memory: &arena.GeneralArena, max_bytes: u64)` | `result<std.InputLine>`; successful `InputLine { end: bool, value: string }`; attempts `max_bytes + 1` zeroed bytes in the caller's arena |
| `std.to_int(value: string)` | `result<i32>`; no allocation |
| `std.parse_i32_checked(value: string)` | `result<i32>`; compatibility alias of `to_int`/`parse_i32`, including diagnostic codes |
| `std.to_string(memory: &arena.GeneralArena, value: i32)` | `result<string>`; requests 12 zeroed bytes; allocation failure returns `NO_MEMORY` |

Successful strings are non-owning descriptors over arena bytes. Copying, returning,
or deferring a string does not copy its bytes or extend the arena lifetime.
Keep the arena live until all uses finish; reset/free invalidates its strings.
There is no per-string free, hidden persistent allocator, or automatic lifetime
management. Register `defer memory.free()` before deferred uses of its strings.
The arena type is explicit; other allocator types can gain their own APIs later.

Every `input` call attempts its bounded storage before reading, even if it reaches
EOF or encounters an error. Consumed arena space remains until reset/free.
A loop should consume each result, then reset its arena, or intentionally keep
it alive for retained lines. Failure has no readable line or string payload.
EOF succeeds with `end = true` and static empty text; an empty line has `end = false`.
`InputLine.end: bool` and `InputLine.value: string` are public immutable fields.
The 12-byte formatting allocation accommodates the longest decimal i32 plus
one trailing NUL; the string's length excludes that NUL.

`GeneralArena.alloc_bytes(size: u64) -> *u8` and
`try_alloc_bytes(size: u64) -> *u8` provide variable-length, alignment-1 storage
initialized entirely to zero. The first traps on failure; the second returns null.
A zero-size allocation can return a distinct non-null address but grants no bytes
to read or write. Reset/reuse still zeroes every requested byte. These methods
supplement initialized-value `alloc`/`try_alloc`; they do not add pointer arithmetic,
array indexing, or bounds checking on their own. Fixed-array indexing is a
separate language feature. Helpers use borrowed arena references.
Allocation through a cleared handle traps, as with existing arena operations.

## Input rules

`input` reads stdin through one LF or EOF. It removes LF and one CR immediately
before LF. A standalone CR, including a final CR at EOF, remains payload.
`max_bytes` counts payload bytes after this line-ending removal, not characters.
A limit of zero accepts an empty line and rejects any nonempty payload.
Runtime strings can hold arbitrary input bytes, including NUL and invalid UTF-8;
input neither validates nor replaces them. Source files/literals still require UTF-8.

The largest accepted `max_bytes` is 9,223,372,036,854,775,806. Larger values fail
before allocation or input consumption. A call performs O(consumed bytes +
`max_bytes` + 1) byte work, including zeroing the requested buffer, plus arena
allocation. It blocks until a line, EOF or read failure; the byte limit bounds
retained text, while draining an oversized line may consume arbitrarily more.

| Outcome/code | Meaning |
| --- | --- |
| Success, `end = false` | A complete line, including an empty line |
| Success, `end = true` | EOF before any byte of the next line; repeated EOF succeeds again |
| `IO_ERROR` (4) | Reading stdin failed; no partial line is exposed |
| `TOO_LONG` (5) | Payload exceeded its byte limit; the whole line is drained |
| `NO_MEMORY` (6) | Buffer size is unrepresentable or allocation failed; stdin is untouched |

Every result must be handled, but `InputLine.end` is an ordinary flag: the compiler
does not require an end check to read its valid empty text. Errors have static
messages, `@status` codes and `.os_error = 0`. The existing native stdin API does
not capture errno; zero means unavailable, not that no OS failure occurred.
Use [the I/O module](io.md) when native diagnostics or explicit progress are needed.

An unterminated final nonempty line succeeds once; the next call succeeds with `end = true`.
An oversized line is drained through LF/EOF so the next call starts on the next
line. An I/O error while draining takes precedence over TOO_LONG. Already-read
bytes cannot be restored. Input itself prints nothing. Printing a prompt through
`std.print` flushes it before the read. There are no exceptions or implicit exits
for these results; callers decide their policy. Arithmetic in caller code
still follows the language's overflow-trap rules.

## Decimal conversions

`to_int` accepts exactly `[+-]?[0-9]+`, with ASCII digits and optional leading
zeroes, within `[-2147483648, 2147483647]`. It does not trim whitespace, accept
base prefixes/separators/exponents, or stop at NUL. The complete counted byte
sequence must match. Invalid syntax takes precedence over overflow if both
occur. Failure exposes only its error; successful zero remains a normal value.
`parse_i32` and `parse_i32_checked` have identical parsing, messages and codes.

`to_string` produces locale-independent base-10 ASCII, with a minus only for
negative values and no leading zeroes, plus sign, whitespace, or newline. Minimum
i32 is supported without signed overflow. After checking the formatting result,
parsing its string payload recovers the original integer exactly.
Both functions are specifically i32 APIs. The [numeric module guide](numbers.md)
covers other integer widths, floats and booleans. Check a formatting result
before printing its value. Implicit conversions and interpolation are unavailable.

## Runtime and verification

[stdlib_runtime.h](../src/stdlib_runtime.h) declares stable C ABIs using byte
pointers, explicit u64 lengths, output pointers, and i32 statuses. No native C
aggregate-return ABI is assumed. [sema_standard.cpp](../src/sema_standard.cpp)
restricts conversion/input/view primitives to the canonical `@std` source;
[codegen_standard.cpp](../src/codegen_standard.cpp) lowers those checked calls.
Native symbol signatures are validated before JIT execution, and source function
names cannot replace them. The public function names have no compiler special case.
`@std` imports `@arena` and `@status`, so an explicit replacement library directory
must provide both dependencies when using the shipped `std.gloin`. SPEC-030a
moves the existing status values into dependency-free `status.gloin`; the public
`std` names remain compatible aliases. See [byte-string APIs](strings.md) for the
new checked string operations and the additional OUT_OF_RANGE status.

The LLVM-independent runtime is included in `libgloin_runtime.a` and the
platform shared library (`libgloin_runtime.dylib` or `.so`). Installed C headers
cover arenas and standard routines.
External LLVM runners can load the shared library for conversion, input, arena,
and `gloin.runtime.output` symbols. The external output implementation aborts on
write failure; the compiler JIT retains its source execution-error reporting.

Native tests cover decimal boundaries/invalid bytes/overflow, 10,000 round trips,
terminators and buffer bounds, EOF/CRLF/NUL, zero/exact/oversized limits, large-line
draining, I/O failure, and zeroed arena reuse. Compiler/CLI tests cover public
wrappers, exact stdout/statuses, private primitives, wrong types, native ABI
validation, manual lifetimes, deferred views, allocation failures, and external
LLVM execution. The complete suite also runs under ASan/UBSan and against installed
and relocated packages.

Run the complete example:

```sh
printf '10\n-3\n+35\n' | ./build/gloinc --jit examples/standard_library.gloin
```

It prints each accepted number, then `count: 3` and `sum: 42`, and exits zero.
Its [source](../examples/standard_library.gloin) shows where reset is safe and
maps input/conversion errors to explicit nonzero exit codes.

## Further numeric APIs

SPEC-030c adds named wide-integer/float/bool parsing and formatting, plus checked
numeric conversions. Their development signatures use the same checked-result policy.
See [the numeric guide](numbers.md) for full grammar, ownership, costs, and examples.

## Migration from 0.1.0

| API | 0.1.0 | Development |
| --- | --- | --- |
| `input` | `InputResult { status, value }` | `result<InputLine>`; successful `end` distinguishes EOF |
| `to_int`, `parse_i32` | `IntResult` | `result<i32>` |
| `parse_i32_checked` | `result<i32>`, message-only errors | Same result with `INVALID`/`OVERFLOW` codes |
| `to_string` | Plain string; allocation failure trapped | `result<string>`; allocation failure is recoverable |

Names and arguments are unchanged. Replace status checks with direct `.erroneous`
checks or exhaustive matches. Read `.error.code` only on failure, and `.value`
only on success. Extract the successful `InputLine`, then check `end` before
processing a line. There is no failure fallback payload. The compatibility status
constants retain their numbers; `END` is no longer an input error code.

| Failure | Exact message | Code |
| --- | --- | --- |
| Malformed i32 text | `invalid signed decimal i32` | `INVALID` |
| Out-of-range i32 text | `signed decimal i32 is out of range` | `OVERFLOW` |
| Unrepresentable input buffer size | `input buffer size is not representable` | `NO_MEMORY` |
| Input allocation | `cannot allocate input buffer` | `NO_MEMORY` |
| Oversized line | `input line exceeds byte limit` | `TOO_LONG` |
| Stdin read failure | `cannot read standard input` | `IO_ERROR` |
| i32 formatting allocation | `i32 formatting failed` | `NO_MEMORY` |

All these errors have OS code zero. Error construction requires no arena or heap
allocation. Input still allocates before reading, drains oversized lines, and
retains attempted storage until reset/free. `to_string`, `format_i32` and
`to_string_i32` now share their recoverable allocation policy and diagnostics.
Cleared/dangling arenas remain invalid; built-in results do not make lifetime
misuse recoverable. `print`/`println` retain their existing output failure policy;
use `@io` for recoverable writes.

Results and `InputLine` pass through the existing typed GloinIR result/struct
operations. Native input, parse and format ABIs are unchanged. Numbered 0.1.0
HTML keeps the released signatures; this guide describes 0.2.0.
