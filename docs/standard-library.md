# Standard input and integer conversions (SPEC-030)

For the proposed expansion before generics, see the
[standard-library roadmap](standard-library-roadmap.md). This guide describes
the currently implemented SPEC-030 API.

The public API lives in [stdlib/std.gloin](../stdlib/std.gloin). These are ordinary
Gloin functions and result structs, compiled from source. Native primitives handle
byte input and decimal conversion. Output remains string-only and never changes
`main`'s return value.

```gloin
import "@std";
import "@arena";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    std.print("number: ");
    def line: std.InputResult = std.input(&memory, 128);
    if line.status == std.END { return 0; }
    if line.status != std.OK { return 1; }
    def number: std.IntResult = std.to_int(line.value);
    if number.status != std.OK { return 2; }
    std.println(std.to_string(&memory, number.value));
    return 0;
}
```

## API and ownership

| Function | Result and allocation |
| --- | --- |
| `std.input(memory: &arena.GeneralArena, max_bytes: u64)` | `std.InputResult { status: i32, value: string }`; attempts `max_bytes + 1` zeroed bytes in the caller's arena |
| `std.to_int(value: string)` | `std.IntResult { status: i32, value: i32 }`; no allocation |
| `std.to_string(memory: &arena.GeneralArena, value: i32)` | `string`; allocates 12 zeroed bytes in the caller's arena; traps on allocation failure |

Successful strings are non-owning descriptors over arena bytes. Copying, returning,
or deferring a string does not copy its bytes or extend the arena lifetime.
Keep the arena live until all uses finish; reset/free invalidates its strings.
There is no per-string free, hidden persistent allocator, or automatic lifetime
management. Register `defer memory.free()` before deferred uses of its strings.
The arena type is explicit; other allocator types can gain their own APIs later.

Every `input` call attempts its bounded storage before reading, even if it reaches
EOF or encounters an error. Consumed arena space remains until reset/free.
A loop should consume each result, then reset its arena, or intentionally keep
it alive for retained lines. Error results contain the static empty string.
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

| Status constant | Value | Meaning |
| --- | ---: | --- |
| `std.OK` | 0 | A complete line or valid integer, including an empty input line |
| `std.END` | 1 | EOF before any byte of the next line; repeated EOF remains END |
| `std.INVALID` | 2 | Integer text does not satisfy the complete decimal grammar |
| `std.OVERFLOW` | 3 | Valid decimal text is outside the i32 range |
| `std.IO_ERROR` | 4 | Reading stdin failed; no partial line is returned |
| `std.TOO_LONG` | 5 | Payload exceeded its byte limit; the whole line is consumed and no partial text returned |
| `std.NO_MEMORY` | 6 | Input buffer size is unrepresentable or allocation failed; stdin is untouched |

An unterminated final nonempty line succeeds once; the next call returns END.
An oversized line is drained through LF/EOF so the next call starts on the next
line. An I/O error while draining takes precedence over TOO_LONG. Already-read
bytes cannot be restored. Input itself prints nothing. Printing a prompt through
`std.print` flushes it before the read. There are no exceptions or implicit exits
for these result statuses; callers decide their policy. Arithmetic in caller code
still follows the language's overflow-trap rules.

## Decimal conversions

`to_int` accepts exactly `[+-]?[0-9]+`, with ASCII digits and optional leading
zeroes, within `[-2147483648, 2147483647]`. It does not trim whitespace, accept
base prefixes/separators/exponents, or stop at NUL. The complete counted byte
sequence must match. Invalid syntax takes precedence over overflow if both
occur. Every failure returns value zero, distinguished from valid zero by status.

`to_string` produces locale-independent base-10 ASCII, with a minus only for
negative values and no leading zeroes, plus sign, whitespace, or newline. Minimum
i32 is supported without signed overflow. `to_int(to_string(...))` must round-trip.
Both functions are specifically i32 APIs. Wider integers, floats, booleans,
implicit conversions, interpolation, and printf-style arguments remain unsupported.
Use an explicit `std.to_string(&memory, value)` before printing an i32.

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

The LLVM-independent runtime is included in both `libgloin_runtime.a` and
`libgloin_runtime.dylib`. Installed C headers cover arenas and standard routines.
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
numeric conversions. The legacy functions on this page retain their contracts.
See [the numeric guide](numbers.md) for full grammar, ownership, costs, and examples.
