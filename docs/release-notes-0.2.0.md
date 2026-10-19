# Gloin 0.2.0

Version 0.2.0 adds switch and match, completes the planned
standard-library migration to built-in results, and fixes compiler issues found
while testing larger programs. See the [release guide](release-0.2.0.md) for
builds, packages and verification status, and the [HTML guide](site/0.2.0/index.html)
for the language and examples.

## Language and library

- `switch` selects statements without fallthrough. `match` produces a value,
  requires at least two arms, and rejects non-exhaustive coverage. Subjects
  include integers, booleans, strings and payloadless enums. Match also handles
  `result<T>` with `value(name)`, `error(name)` and `value()` patterns.
  The [selection guide](switch-match.md) documents coverage and scope rules.
- Errors have read-only `message: string`, `code: i32` and `os_error: i32`
  fields. `error("message")` remains valid and sets both codes to zero;
  `error("message", code, os_error)` supplies explicit diagnostics. Messages
  remain string literals. Source locations and stack traces are not included.
- Fallible public APIs in `@fs`, `@io`, `@process`, `@strings`, `@std`,
  `@math`, `@random` and `@time` use built-in `result<T>`. The compiler requires
  handling and proof of success before payload access. Partial I/O progress
  lives in caller-owned state; EOF, iteration end, readiness and child exit
  status remain successful outcomes where documented.
- `gloinfmt` handles switch blocks and match arms, including nested expressions.
- The numbered HTML guide covers the new language and APIs. Shared version
  navigation now opens the correct guide when read offline from an archive.

## Migrating from 0.1.0

This release changes source APIs. Rebuild programs and update callers; do not
mix the new library with a 0.1.0 compiler. In particular:

| Previous API | 0.2.0 |
| --- | --- |
| Status/value wrappers such as `FsResult`, `I32Result` and `InstantResult` | `result<void>` or `result<T>`; check `.erroneous`, forward `.error`, then read `.value` on the success path |
| Error `.status` / `.os_error` on wrappers | `.error.code` / `.error.os_error`; numeric math errors currently use a distinct message with unspecified zero codes |
| File and stream transfer result structs | A `result<void>` plus explicit `ReadState`, `ByteReadState` or `WriteState` argument holding progress |
| Input and directory exhaustion represented as failure | Successful `InputLine` or directory payload with `end == true` |
| String cursor result wrapper | Ordinary `TextItem` from a valid cursor; inspect its end state |
| Exact i32-to-i64 and explicitly rounded integer-to-f64 conversion wrappers | Plain scalar return values |

The [result contract](results.md) explains checking, forwarding and matching.
The [filesystem/process](filesystem-process.md), [I/O](io.md),
[strings](strings.md), [text construction](text-construction.md),
[input](standard-library.md), [numbers](numbers.md), [math](math.md) and
[time/random](time-random.md) guides give signatures, diagnostics and migration
examples. Ownership, arena lifetimes, aliasing and explicit cleanup still apply.
The numbered [0.1.0 HTML guide](site/0.1.0/index.html) retains its original API.

## Compiler

- `--time-compilation` reports compiler stages, IR counts and peak compiler
  memory as a JSON line on stderr. It does not measure application execution.
- Fixed-array indexing uses existing storage. Stored repeated byte arrays use
  direct fills, avoiding the aggregate copy path that made large initializers
  extremely slow to optimize.
- GloinIR removes unreachable source functions for executable/JIT builds after
  checking all source. Object and inspection output retain their definitions.
  Optional inlining into large callers is bounded to control compilation cost.
- GloinIR proofs simplify checked integers, exact widening and array/slice
  bounds. Eligible counted and constant-stride loops use a runtime entry
  predicate to select a proven fast loop or their original checked fallback.
- Adjacent floating multiply/add checks can share a finite check. Eligible dot
  reductions over initialized, confined local arrays can check once at exit.
  Separate rounding, addition order and mandatory failures are retained; no
  fast math or reassociation is enabled. Unknown borrowed, arena and vector
  storage do not qualify for the whole-loop reduction proof.
- Native object/executable generation uses ordinary AoT target defaults,
  preserving host CPU selection and Linux PIC output.
- Source-type conversion fixes retain pointer/slice capabilities and nominal
  types through GloinIR, including result payloads and nested array rows.

The [compiler validation journal](compiler-validation.md) records controlled
measurements, regressions, code-size costs and remaining limits. Its kernel
results are observations for those programs and machines, not a general speed
guarantee. The [lowering guide](lowering.md) documents the proof boundaries.

## Platforms and limits

Targets remain Apple Silicon macOS with LLVM/MLIR 21.1.6 and Ubuntu 24.04 Linux
on ARM64 and x86_64 with LLVM/MLIR 21.1.8. The compiler requires the matching
shared toolchain and OpenSSL 3. Generated executables do not require LLVM;
TLS programs require OpenSSL. The release guide records the verification
boundary; hosted reports and archive checksums accompany the GitHub release.

Endianness APIs, packed structs, bit fields, enum payloads, maps, threads and
language-level async/spawn are outside 0.2.0. Switch/match does not add guards,
ranges, grouped labels, block-valued arms, `break` or `continue`. Windows support
is not planned; community contributions are welcome.
