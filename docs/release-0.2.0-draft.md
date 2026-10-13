# Gloin 0.2.0 development record

0.2.0 covers switch and match, migration of the remaining standard-library
error returns to built-in `result<T>`, and compiler fixes found while checking
larger and more complex programs. The release is in development. The published
compiler and versioned language guide remain at 0.1.0.

The [0.1.0 release notes](release-notes-0.1.0.md) and
[validation record](next-release-draft.md) describe the released baseline.

## Scope and status

| Area | Status |
| --- | --- |
| Compiler correctness and performance fixes | PERF-001–009 implemented and verified in the development tree; measurements and limits are in the [compiler validation record](compiler-validation.md). |
| `switch` and `match` | In scope. Syntax and semantic rules are not yet agreed; the checked compiler still rejects both constructs. |
| Standard-library `result<T>` migration | In scope. Remaining public status/value error wrappers, including `fs.FsResult`, are to be replaced. API signatures and preservation of error details need to be specified. |
| Endianness, packed structs and bit fields | Outside 0.2.0. |
| Threads and language-level async/spawn | Outside 0.2.0. Existing nonblocking I/O and explicit readiness polling remain available. |

macOS and Linux remain the supported platforms. Windows support is not planned.

## Switch and match

Both keywords are reserved. Enums currently contain named variants without
payloads; pattern matching and exhaustiveness checking are not implemented.
See [enums](enums.md) and [the parser contract](parser.md).

The language design still needs to define:

- Statement and expression forms, arm syntax and supported subject types.
- Literal, enum and result patterns; grouped cases, ranges and guards.
- Exhaustiveness, defaults, duplicate/overlapping cases and unreachable arms.
- Fallthrough, arm scopes, return paths and definite initialization.
- Payload bindings, copying/borrowing and mutation of the matched value.
- Whether `break`, `continue` or enum payloads belong to this implementation.

The subject must be evaluated once. Result matching must preserve the existing
success/error distinction: a value is accessible only on a proven success path,
and mutation cannot leave an obsolete proof valid. `result<void>` also needs a
matching rule. These requirements do not establish syntax for either construct.

The agreed rules belong in the
[language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec).
Implementation includes semantic diagnostics, verified GloinIR operations and
lowering, formatter support, runnable examples and language documentation.

## Standard-library result migration

The library currently mixes built-in `result<T>` with older structs carrying
`status`, `value`, `os_error` or progress fields. The latter can be read without
the compiler proving that the operation succeeded. 0.2.0 replaces public
fallible status/value returns with the language's checked result mechanism.

The migration covers the remaining wrappers in these modules:

| Module | Current examples |
| --- | --- |
| `@fs` | `FsResult`, `PathResult`, `MetadataResult`, `DirectoryResult`, `DirectoryEntryResult` |
| `@io` | `IoResult`, `FileResult`, `ReadResult`, `ByteReadResult`, `WriteResult`, `MessageResult` |
| `@process` | `TextResult` |
| `@strings` | `ByteResult`, `StringResult`, `SplitCursorResult`, `BuilderResult` |
| `@std` | Parsing, formatting, input and numeric-conversion result structs |
| `@math` | Checked integer and floating-point result structs |
| `@time`, `@random` | `InstantResult`, `DurationResult`, random `U64Result` |

Operations with no success payload use `result<void>`; operations returning a
value use `result<T>`. Records that describe successful outcomes remain ordinary
payload types. A status field is not automatically an error: EOF, iteration end,
would-block states and child exit status have their own meaning.

Several existing contracts need explicit treatment before changing signatures:

- **Error details.** Filesystem and I/O callers can inspect native `os_error`
  codes and library status categories. The current built-in error interface
  exposes static messages, with no source-location or debug payload. The
  migration must define how callers retain access to the details they need;
  replacing everything with a generic message would lose information.
- **Partial progress.** Reads and writes can report bytes transferred before a
  failure. The replacement API must represent that progress without allowing
  access to an invalid success payload.
- **Expected end states.** EOF, missing entries and iteration completion need
  documented outcome rules, distinct from transport or filesystem failure.
- **Infallible operations.** Exact i32-to-i64 widening always succeeds today.
  Its final return signature needs a decision rather than mechanically retaining
  an always-successful wrapper.
- **Ownership.** Existing arena, borrowed-buffer, handle-alias and cleanup rules
  continue to apply to successful values and failure paths.

This changes source APIs. Each migrated function needs an old/new signature and
behavior entry, with repository examples, tools, compiler/runtime tests and the
development guide updated together. The versioned 0.1.0 documentation retains
its original API descriptions. Native runtime status codes may remain internal;
the public Gloin boundary must construct results through the checked compiler
and GloinIR path.

No migration is implemented by this scope entry. Current signatures are listed
in the [filesystem/process](filesystem-process.md), [I/O](io.md),
[strings](strings.md), [numbers](numbers.md) and [result](results.md) guides.

## Compiler validation, 2026-10-08–12

Larger program checks exposed unnecessary fixed-array copies, stalled repeated
array builds, excessive lowering/optimization work and valid capability
conversions rejected by the compiler. The resulting fixes retain the language's
arithmetic, bounds, null and result-access checks.

Completed work includes compilation-stage reporting, reuse of array storage,
compact repeated-array fills, executable/JIT reachability, checked arithmetic
lowering, GIR range proofs, counted slice loop versioning, exact integer widening
and corrected source-type conversions. The
[compiler validation record](compiler-validation.md) documents the failures,
changes, measurements, unsuccessful attempts and remaining limits. These are
investigations of specific programs, not general performance guarantees.

At the end of this work the macOS core suite passed 1,028 tests. The final
widening change also passed 83 focused Linux tests and 83 ASan/UBSan tests.
Earlier entries record their own validation batches. This is development
validation; it is not a completed 0.2.0 package or release audit.

## Release validation still required

The feature work needs positive and negative source cases, GIR verification,
JIT and native `-O0`/`-O2` checks on macOS and Linux, and formatter/documentation
coverage. Result migration also needs error-detail, end-state, partial-progress
and cleanup tests for each affected API.

The final release audit includes the full core suite, sanitizer checks, installed
and relocated packages, runnable examples and documentation links. A numbered
0.2.0 guide and release notes will describe the implemented scope. The four
unsupported async/spawn tests remain documented exclusions from this release.
