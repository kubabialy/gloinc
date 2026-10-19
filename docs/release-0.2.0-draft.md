# Gloin 0.2.0 development record

0.2.0 covers switch and match, migration of the remaining standard-library
error returns to built-in `result<T>`, and compiler fixes found while checking
larger and more complex programs. The [0.2.0 language guide](site/0.2.0/index.html)
documents the release. See the [release notes](release-notes-0.2.0.md) and
[final validation record](release-0.2.0.md#verification-boundary) for current
status; the dated entries below preserve their original measurement boundaries.

The [0.1.0 release notes](release-notes-0.1.0.md) and
[validation record](next-release-draft.md) describe the released baseline.

## Scope and status

| Area | Status |
| --- | --- |
| Compiler correctness and performance fixes | PERF-001–009 implemented and verified in the development tree; measurements and limits are in the [compiler validation record](compiler-validation.md). |
| `switch` and `match` | Implemented in the development tree: statement `switch`, value-producing exhaustive `match` with at least two arms, result patterns, GloinIR lowering and formatter support. See [the guide](switch-match.md). |
| Standard-library `result<T>` migration | `@math`, bounded `@random` sampling, and recoverable `@fs`, `@io`, `@process`, `@strings`, `@std` and `@time` APIs migrated. The planned public status-wrapper migration is complete. Partial I/O progress stays in explicit I/O state; successful end/readiness outcomes remain ordinary values. Local package validation passes on macOS and Ubuntu ARM64; hosted evidence accompanies the release. |
| Endianness, packed structs and bit fields | Outside 0.2.0. |
| Threads and language-level async/spawn | Outside 0.2.0. Existing nonblocking I/O and explicit readiness polling remain available. |

macOS and Linux remain the supported platforms. Windows support is not planned.

## Switch and match

Implemented on 2026-10-16. Both keywords are reserved. Subjects may be integers,
booleans, strings or payloadless enums; match also accepts built-in results.
See [the language guide](switch-match.md), [enums](enums.md), and
[the parser contract](parser.md).

The core contract was agreed on 2026-10-16:

- `switch` is a statement with scoped `case pattern { ... }` blocks and an
  optional `default { ... }`. It can leave values unhandled.
- `match` is an expression with `case pattern => expression;` arms and
  `default => expression;` where a fallback is needed. It produces one typed
  value and requires at least two arms, counting a default arm.
- A non-exhaustive match is a compile error. Boolean matches cover both values;
  enum matches cover every variant; result matches cover success and failure.
  A default may cover remaining outcomes. Integer and string matches initially
  require a default.
- The subject is evaluated once. Only the selected arm executes, with no
  fallthrough. All arm values must be compatible with the match's result type
  under the existing literal and conversion rules.
- Result patterns use `value(name)`, `error(name)`, and `value()` for
  `result<void>` success. Bindings are immutable and local to their arm, and
  retain the payload's existing ownership and lifetime rules. Matching handles
  the result without exposing an inactive payload.

The two-arm minimum also rejects an otherwise exhaustive one-arm match over a
single-variant enum. `return` continues to exit the enclosing function.

The accepted contract is recorded under SPEC-017a in the
[language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec#switch-and-match-spec-017a-development-compiler).
Block-valued match arms still need a way to produce their value; they are outside
this implementation. Grouped cases, ranges, guards, enum payloads, `break`, and
`continue` are also excluded. Default must be last. Labels are typed compile-time
constants or qualified enum variants. Duplicate labels and unreachable defaults
are errors. Initialization and result proofs merge across continuing paths;
reassigning a result invalidates its proof.

The compiler retains dispatch and match values in verified `gloin.switch`,
`gloin.match`, and `gloin.match_yield` operations. Lowering creates branches and
value joins. String cases compare counted bytes. There is no jump-table or
specialized string-dispatch performance guarantee in this implementation.

### Validation, 2026-10-16

- macOS/LLVM 21.1.6: the full `check-core` gate passed **1,038 tests**.
- A final invalid-input review found an unchecked optional type when nested
  match arms referred to unsupported function values. After fixing it and adding
  a regression, **168 focused macOS tests** passed on the final source.
- Linux ARM64/LLVM 21.1.8: **169 focused tests** passed on the final source,
  including parser, control flow, enum/result behavior, GloinIR lowering,
  selection execution and formatter checks.
- macOS ASan/UBSan: **168 focused compiler tests** passed on the final source,
  with no sanitizer errors. JIT-generated machine code is not instrumented.
- Selection programs passed checking, GloinIR/LLVM emission, JIT and native
  `-O0`/`-O2`. The maintained example prints `PONG` on macOS and Linux.
  Repository formatting and local documentation links passed; the new HTML
  examples compile and run.

## Standard-library result migration

The released 0.1.0 library mixed built-in `result<T>` with older structs carrying
`status`, `value`, `os_error` or progress fields. Those payloads could be read
without proving success. The development library now replaces the planned public
fallible status/value wrappers with the language's checked result mechanism.

Migration status by module:

| Module | Migration status |
| --- | --- |
| `@fs` | Migrated: all five status-result structs removed; successful metadata and directory reads have ordinary payload records |
| `@io` | Migrated: six result structs removed; transfer progress lives in explicit caller-owned state |
| `@process` | Migrated: argument/environment/cwd return `result<string>`; `TextResult` removed |
| `@strings` | Migrated: four result structs removed; cursor advancement returns ordinary `TextItem` |
| `@std` | Migrated: eight wrappers removed; input has a successful `InputLine` payload; infallible conversions return scalars |
| `@math` | Migrated: all 37 fallible functions use `result<T>`; min/max remain scalar |
| `@random` | Migrated: `SplitMix64.below` uses `result<u64>` |
| `@time` | Migrated: six fallible calls use `result<Instant>`/`result<Duration>`; both wrappers removed; native clock errno preserved |

Operations with no success payload use `result<void>`; operations returning a
value use `result<T>`. Records that describe successful outcomes remain ordinary
payload types. A status field is not automatically an error: EOF, iteration end,
would-block states and child exit status have their own meaning.

The migration preserves these contracts:

- **Error details.** Implemented: read-only `code: i32` and `os_error: i32`
  accompany the static message. `error("message")` defaults both to zero;
  `error("message", code, os_error)` preserves explicit diagnostics. No
  source-location or debug payload is included yet.
- **Partial progress.** Progress stays in explicit I/O state, not on `error`.
  Reads/writes must retain bytes transferred before failure without permitting
  access to an invalid success payload. File/stream transfers now take explicit
  `ReadState`, `ByteReadState` or `WriteState` and return `result<void>`.
- **Expected end states.** Directory exhaustion now succeeds with `end = true`;
  a missing metadata path fails with `NOT_FOUND`. File/stream line, chunk and
  direct reads expose empty EOF as successful `end = true`; read-all completes
  normally at EOF. `std.input` now succeeds with `InputLine.end = true` at EOF. Existing socket
  would-block states remain ordinary successful outcomes.
- **Infallible operations.** Exact i32-to-i64 widening returns plain i64. The
  explicitly rounded i64/u64-to-f64 conversions return plain f64; their full
  integer ranges are finite in f64 and precision loss is permitted by the API.
- **Ownership.** Existing arena, borrowed-buffer, handle-alias and cleanup rules
  continue to apply to successful values and failure paths.

This changes source APIs. The linked module guides record old/new signatures
and behavior; repository examples, tools, compiler/runtime tests and the
development guide are updated with them. The versioned 0.1.0 documentation retains
its original API descriptions. Native runtime status codes may remain internal;
the public Gloin boundary must construct results through the checked compiler
and GloinIR path.

### Numeric APIs, 2026-10-17

The first migration replaces `math.I32Result`, `I64Result`, `U64Result`,
`F32Result`, `F64Result`, and `random.U64Result` with built-in results. Function
names and arguments are unchanged. The [math guide](math.md#migration-from-010)
records each old/new return type and the exact error messages. The
[random guide](time-random.md) records the new bounded-sampling signature.
All math failure categories retain distinct messages. These functions use the
one-argument error constructor, so diagnostic codes are currently zero. They
had no public OS error or partial progress fields.

Successful math values, signed zeros, rounding and subnormal policies are
unchanged. Bound zero still leaves random state untouched; seeded sequences and
draw counts remain the version-1 contract. Native runtime ABIs are unchanged.
Source-level result construction, forwarding and matching use GloinIR.

Migrating the geometry example exposed a missing type hint for result payloads:
comparison literals beside `result<f64>.value` fell back to f32. The compiler
now uses the payload type as numeric context, while retaining success proofs
and rejecting conversions between differently typed operands.

Validation on macOS/LLVM 21.1.6 passed the full **1,042-test core gate**,
including the million-record integrated checks. Linux ARM64/LLVM 21.1.8 passed
**75 focused tests** for numeric/result/selection behavior and integrated
examples. The same **75 tests passed under macOS ASan/UBSan**, with no sanitizer
errors; JIT-generated code itself is not instrumented. Coverage includes every
public math function, exact error messages,
signed zeros and subnormals, seeded vectors, invalid bounds without advancement,
rejected unchecked access, GloinIR types, and native `-O0`/`-O2` execution.
Both Markdown guide programs and the new HTML example run; repository formatting
and edited guides' local links pass.

Current signatures are listed in the [filesystem/process](filesystem-process.md), [I/O](io.md),
[strings](strings.md), [numbers](numbers.md) and [result](results.md) guides.

## Compiler validation, 2026-10-08–12

Larger program checks exposed unnecessary fixed-array copies, stalled repeated
array builds, excessive lowering/optimization work and valid capability
conversions rejected by the compiler. The resulting fixes retain the language's
arithmetic, bounds, null and result-access checks.

Completed work includes compilation-stage reporting, reuse of array storage,
compact repeated-array fills, executable/JIT reachability, checked arithmetic
lowering, GIR range proofs, counted and strided slice loop versioning, exact integer widening
and corrected source-type conversions. The
[compiler validation record](compiler-validation.md) documents the failures,
changes, measurements, unsuccessful attempts and remaining limits. These are
investigations of specific programs, not general performance guarantees.

At the end of this work the macOS core suite passed 1,028 tests. The final
widening change also passed 83 focused Linux tests and 83 ASan/UBSan tests.
Earlier entries record their own validation batches. This is development
validation; it is not a completed 0.2.0 package or release audit.

## Final release validation

Switch/match and the standard-library migration now have error-detail,
end-state, partial-progress and cleanup regressions, documented in the dated
entries below. The [release guide](release-0.2.0.md#verification-boundary)
records final core, sanitizer, installed/relocated package and platform checks.
The numbered 0.2.0 guide and release notes describe the implemented scope.
The four unsupported async/spawn tests remain documented exclusions from the
supported core and expected failures in complete-suite audits.

The 2026-10-19 candidate audit passes 1,078 core tests on each ARM64 platform,
552 installed and 552 relocated package tests on each, and 1,078 macOS
ASan/UBSan core tests. All four complete-suite reports contain 1,121 passes
and exactly the four expected failures. Formatting, package integrations,
the 0.2.0 HTML examples and offline version navigation also pass. Earlier
entries below retain their validation status at the time of implementation.
Hosted macOS and Ubuntu x86_64 checks on the tagged revision are required for
publication; their reports accompany the release.

### Error diagnostics and filesystem mutations, 2026-10-17

Built-in errors now carry read-only `message: string`, `code: i32`, and
`os_error: i32`. One-argument construction remains valid; the three-argument
form supplies both codes. Message storage remains static, copying allocates
nothing, and forwarding/matching preserves all fields. Zero codes mean
unspecified diagnostics; they never determine the result variant. Partial I/O
progress stays in explicit I/O state. See [the contract](results.md).

`fs.mkdir`, `fs.remove_file`, `fs.rename_replace`, and `fs.Directory.close`
now return `result<void>`; `FsResult` is removed. Failure categories use
`@status` constants in `.error.code`, with native errno retained in
`.error.os_error`. Validation precedes host mutation, and directory close
still consumes its handle and invalidates aliases even on native failure.
The [filesystem guide](filesystem-process.md#mutation-migration-from-010)
lists each signature and message. Repository tools, cleanup helpers, examples
and test drivers now check these results explicitly.

Construction and member access stay in typed GloinIR operations until the
source-type boundary. The error storage layout changes in this development
compiler; rebuild generated objects and executables together. Previously
migrated message-only APIs keep zero codes until individually updated.
At this stage, filesystem metadata/open/iteration and I/O wrappers remained to be migrated; the next entry records the filesystem follow-up.

Validation on macOS/LLVM 21.1.6 passed the full **1,047-test core gate**.
Regressions cover constructor arity/types and evaluation order, immutable
members, proof-checked access, code preservation through copies/forwarding/match,
old/malformed storage rejection in GloinIR, exact native errno, validation
without mutation, alias close state, and JIT/native `-O0`/`-O2` execution.
Repository formatting and all seven HTML guides' local links pass. The new
HTML mutation program passes both creation and already-existing-path branches.
Linux ARM64/LLVM 21.1.8 passed **119 focused tests**, covering results, source
lowering, selection, filesystem/context behavior, and rebuilt Gloin formatter
and documentation tooling in native and JIT modes.
macOS ASan/UBSan passed **16 focused checks** covering all result tests,
error-layout/boundary rejection, filesystem mutation diagnostics and directory
alias close behavior. No sanitizer errors were reported. Instrumentation
covers the compiler and native runtime, not generated Gloin machine code.

### Filesystem queries, paths and iteration, 2026-10-17

All fallible `@fs` APIs now return built-in results. `basename`, `dirname` and
`join` return `result<string>`. `metadata` returns `result<Metadata>` with
`kind`/`size` on success; missing paths are failures with code `NOT_FOUND`,
so callers distinguish absence from permission and other failures explicitly.
`Directory.open` returns `result<Directory>` and `next` returns
`result<DirectoryRead>`. The latter carries `end: bool` and `name: string`:
normal exhaustion is successful with `end = true` and a static empty name.
The compiler checks result success, not the end flag. A failed read exposes
no name. All five former filesystem result structs are removed.

Path borrowing, join allocation counts, arena lifetimes, native error mapping,
unsorted directory order and close alias invalidation retain their existing
contracts. An entry copy failure can consume a native entry; retrying cannot
recover its name. End performs no allocation and is not permanently latched.
The existing symlink, workspace and checked replacement helpers now retain
library categories and native errno alongside their existing static messages.
No progress field is added to `error`.

The migration uses the existing checked result/struct operations and native
`gloin.abi_call` boundary; no new compiler bypass or native runtime ABI is needed.
The [filesystem guide](filesystem-process.md#mutation-migration-from-010) has
old/new signatures, diagnostic categories, ownership and failure effects.
The development HTML includes a complete directory walker with checked cleanup.
Repository examples, formatter, documentation tools and stress drivers use the
new APIs. At this stage, process-context and I/O wrappers remained next; the
following entry records their migration.

Validation on macOS/LLVM 21.1.6 covered **1,050 core tests**. The full run
passed 1,048; it caught an incorrect ENOTDIR expectation in the new workspace
assertions and a changed formatter diagnostic. ENOTDIR retains its existing
NOT_FOUND mapping, the formatter keeps its previous message while forwarding
codes, and both tests passed after those corrections on a targeted rerun.
Linux ARM64/LLVM 21.1.8 passed **43 focused tests**, including all filesystem/
process-context and result tests plus rebuilt formatter/documentation tooling.
macOS ASan/UBSan passed **11 focused filesystem tests** with no reports;
generated Gloin machine code is not instrumented. The new cases cover result
handling/proofs, exact missing-path errno, empty/repeated directory end, retained
names, state/name allocation failures and consumed cursor state. JIT and native
`-O0`/`-O2` execution agree. The HTML directory example also passes in JIT and
native `-O2` for empty and nonempty directories. Repository Gloin formatting,
all seven HTML guides' local links and whitespace checks pass. CTest discovery
now contains **1,097 tests** overall. This is development validation, not a
release audit.

### I/O and process-context results, 2026-10-17

Every fallible `@io` API now returns a built-in result. File opening returns
`result<io.File>`; OS diagnostic text and process argument/environment/cwd
queries return `result<string>`. File/stream transfers, flush and close return
`result<void>`. The six old I/O wrappers and `process.TextResult` are removed.
Failed opens no longer expose a dummy closed File. Deferred cleanup helpers
must handle the result; bare deferred close is rejected.

Transfers take a final caller-owned `ReadState`, `ByteReadState` or `WriteState`.
Every call resets state before validation. Chunk/all and direct-read failures
retain received bytes; writes retain their accepted count, including both stages
of write-line. Line failures still expose empty text after any required draining.
Empty EOF succeeds with `end = true` for line/chunk/direct reads. Read-all treats
EOF as completion and leaves `end = false`, including for an empty file.

The compiler requires checking the result; progress fields remain independently
readable because their partial data is valid on failure. They do not establish
success. No progress field is added to `error`. Categories and native errno are
preserved. Process environment absence remains NOT_FOUND, distinct from an
existing empty value; missing arguments remain OUT_OF_RANGE. OS-message lookup
errors describe that lookup and do not reuse its input errno.

Handle aliases, allocation-before-open effects, arena borrowing, read bounds,
blocking behavior, native buffering and SIGPIPE policy retain their contracts.
The source implementation uses existing GloinIR result/struct operations and
the native ABI boundary. No compiler bypass or native ABI change was added.
Repository callers, examples, tools and fixtures use the new signatures. The
[I/O guide](io.md), [process-context guide](filesystem-process.md), development
HTML and wiki drafts document migration, ownership, progress and failure effects.
At this stage, strings, legacy `@std` input/numeric text/conversion and time wrappers remained; the following entry records strings.

Validation: macOS/LLVM 21.1.6 passed **1,053 core tests**, including the
million-record stress case. An initial run alongside Linux and sanitizer work
hit the stress child's deadline; the complete macOS rerun after those jobs
finished passed with unchanged limits. Linux ARM64/LLVM 21.1.8 passed **72
selected tests**, covering I/O, process/filesystem context, integrated examples,
HTTP/TLS/JSON and rebuilt formatter/checker tools. macOS ASan/UBSan passed **25
focused tests** with no reports; generated Gloin machine code is not instrumented.
Injected failures follow real transfers and verify exact partial bytes/counts,
codes, state reset and close aliases in JIT/native `-O0`/`-O2`. All repository
Gloin formatting and seven HTML guides' local-link checks pass. The HTML I/O
example also runs in JIT/native `-O2` for a line, an empty line and empty EOF.
CTest discovery contains **1,100 tests** overall. These are development checks,
not a release audit or a performance comparison.

### Strings, cursors and builders, 2026-10-17

All fallible `@strings` calls now return built-in results. Byte access returns
`result<u8>`; slicing, copies, transformations and snapshots return
`result<string>`. Split and builder creation return their handles in a result;
appends return `result<void>`. `ByteResult`, `StringResult`, `SplitCursorResult`
and `BuilderResult` are removed. The checked byte/slice aliases remain, with
the same messages and newly preserved `OUT_OF_RANGE` codes. Failure has no
readable fallback payload. Errors carry library categories and OS code zero.

Advancing a valid cursor cannot fail and returns ordinary
`TextItem { end: bool, value: string }`. Empty items have `end = false`;
exhaustion repeatedly returns `end = true` and empty text. An empty split
delimiter fails at construction without exposing an invalid cursor. Line
creation and search remain infallible. The compiler checks results, not the
ordinary `end` or `found` flags.

Borrowing, cursor copies, builder aliasing, byte algorithms, allocation counts
and lifetimes retain their contracts. A failed append changes neither bytes
nor length; a sequence of appends does not roll back earlier successful work.
Internal parser/lookahead helpers that deliberately used zero/empty sentinels
now choose them explicitly after checking the result. Public APIs do not
supply an implicit fallback.

Source result construction and payload access use the existing typed GloinIR
operations; native runtime ABIs are unchanged. Repository callers, tools,
examples and tests use the new APIs. The [strings](strings.md) and
[text construction](text-construction.md) guides include old/new signatures,
exact diagnostics, ownership and failure effects. Development HTML has a
complete bounded report example; numbered 0.1.0 guides retain their API.
At this stage, `@std` input/numeric text/conversion and `@time` wrappers remained; the next entry records std.

Validation: macOS/LLVM 21.1.6 passed the full **1,057-test core gate**, including
the million-record stress case with unchanged limits. Linux ARM64/LLVM 21.1.8
passed **132 selected tests** covering strings/text, results, dependent I/O and
process/filesystem APIs, integrated examples, HTTP/TLS/JSON and rebuilt
formatter/checker tools. macOS ASan/UBSan passed **41 focused tests** covering
all string/text cases and both native byte-copy checks, with no reports.
Instrumentation covers project compiler/runtime code, not generated Gloin
machine code; leak detection was disabled.

Regressions verify required handling and inactive payload rejection, exact
diagnostic codes, alias behavior, private cursor invariants, empty/repeated end,
state/buffer/snapshot allocation failures and atomic failed appends. Source
results and cursor items execute through GloinIR, JIT and native `-O0`/`-O2`.
All 20 changed executable entry points pass source checking. Repository Gloin
formatting, seven HTML guides' local links and whitespace checks pass. Both
Markdown guides' programs run verbatim; the new HTML report runs in JIT and
native `-O2`. CTest discovery contains **1,104 tests** overall. These are
development checks; the final 0.2.0 package/release audit remains pending.

### Standard input and numeric results, 2026-10-17

`@std` parsers, numeric formatters and fallible conversions now return built-in
results. All eight former status structs are removed. `to_int` and
`parse_i32_checked` share the canonical i32 parser's messages and codes;
`to_string` and integer-width aliases share their canonical formatter. In
particular, `to_string` allocation failure now returns `NO_MEMORY`, replacing
its earlier trap. `format_bool` still returns static text. Output convenience
functions retain their existing failure policy; `@io` provides recoverable writes.

Input returns `result<InputLine>`. Success carries `{ end: bool, value: string }`:
empty lines have `end = false`, and EOF repeatedly succeeds with `end = true`
and empty text. Buffer allocation still precedes reading, including at EOF.
Allocation failure consumes no input; oversized lines are drained, and read
errors expose no partial line. Errors preserve category codes and use OS code
zero because the existing native stdin ABI does not capture errno.

`i64_from_i32` returns plain i64 through the existing `gloin.exact_int_widen`
operation. `f64_from_i64_rounded` and `f64_from_u64_rounded` return plain f64:
their entire input ranges are finite in f64, with rounding explicitly allowed.
Float widening retains a result to reject native nonfinite input. All remaining
range, finiteness and precision checks are preserved. Grammar, rounding, signed
zero, subnormals, allocation sizes and lifetimes keep their documented behavior.
Seeded random unit-float generation now uses the infallible rounded conversion;
its 53-bit mantissa is exactly representable and its draw count is unchanged.

Repository callers and tools check each result or extract an ordinary successful
value before crossing loops. The [input](standard-library.md), [numbers](numbers.md),
development HTML and local wiki drafts document migration, exact diagnostics,
costs and limits. Result construction and payload access stay in typed GloinIR;
native runtime ABIs are unchanged. At this stage, `@time` remained; the following
entry records that migration.

Validation: **1,061/1,061 macOS core tests**, **124/124 Ubuntu ARM64 focused
tests**, and **76/76 macOS ASan/UBSan tests** pass. The Linux batch covers std,
numeric, I/O, math, time/random, integrated examples, networking, JSON, TLS,
HTTP and formatter/tooling callers. Sanitizer coverage includes the std/numeric
source APIs and native runtimes, plus mixed std/I/O reads. Leak detection is
disabled; compiler and native runtime code are instrumented, while generated
Gloin machine code is not. macOS uses LLVM 21.1.6; Linux uses LLVM 21.1.8.

The HTML input program passes six cases in both JIT and native `-O2`: a valid
integer, EOF, an empty line, invalid syntax, overflow and an oversized line.
The matching Markdown/wiki input program and the wiki's function/control-flow
examples are updated; those two additional examples also run in both modes.
Formatting and all seven HTML pages' local links pass. CTest discovery contains
**1,108 tests** overall. The final 0.2.0 package/release audit remains pending.

### Timing results, 2026-10-18

`monotonic_now` now returns `result<Instant>`. `elapsed`, both checked duration
constructors, and duration addition/subtraction return `result<Duration>`.
`InstantResult` and `DurationResult` are removed. The three unit accessors
retain scalar signatures. This completes the planned public standard-library
status-wrapper migration; search/iteration/readiness/exit outcomes remain
ordinary records, and native status codes remain internal to their ABI boundary.

Successful zero ticks and zero durations remain valid. Equal readings succeed;
backwards elapsed inputs fail with INVALID. Construction/addition overflow and
negative duration subtraction retain OVERFLOW checks before arithmetic. Clock
IO_ERROR retains native errno with EIO fallback; INVALID/OVERFLOW have OS code
zero. Static messages distinguish failure causes. Copying and forwarding keep
the message and both codes. There is no readable dummy payload on failure.
Clock domains remain a caller responsibility; neither results nor raw ticks
establish a shared epoch or promise clock resolution.

Source policy and result operations continue through GloinIR. The clock runtime,
native ABI, scoped host/JIT providers and error normalization are unchanged.
Timing calls require no arena or per-operation allocation. Duration flooring
and rounded f64 conversion retain their numerical contracts. Process/HTTP
deadlines and example/test servers now check readings explicitly and extract
ordinary instants before loops; their existing messages, budgets and error
policies are preserved. The seeded simulation retains its exact injected-clock
timing report and random sequence.

The [timing guide](time-random.md) records all changed signatures, exact messages,
category/OS codes, lifetimes and costs. It also documents the current restriction
on method receiver references through `result.value`: extract a successful
duration before invoking a method. Development HTML includes a complete live
timing program. The local wiki drafts are updated; the numbered 0.1.0 guide
retains the released API.

Validation: **1,063/1,063 macOS core tests**, **96/96 Ubuntu ARM64 focused
tests**, and **32/32 macOS ASan/UBSan tests** pass. The Linux batch includes
time/random, process context, networking, integrated examples, HTTP/TLS/JSON
and formatter/tooling callers. Tests cover exact constructor boundaries,
zero/MAX ticks, equal/backwards readings, callback normalization, copied and
forwarded diagnostics, rejected unhandled results and inactive payload access,
typed GloinIR, JIT and native `-O0`/`-O2` execution. An obsolete status check in
the TLS fixture failed the initial core run; it was migrated and the complete
core run then passed.

Sanitizer coverage includes time/random source APIs, native runtimes and the
compilation-timing freeze check. Leak detection is disabled; compiler and native
runtime code are instrumented, while generated Gloin machine code is not.
macOS uses LLVM 21.1.6; Linux uses LLVM 21.1.8. The HTML timing program runs in
JIT and native `-O2` without assuming a minimum elapsed time. Formatting and
all seven HTML pages' local links pass. CTest discovers **1,110 tests** overall.
The final 0.2.0 package/release audit remains pending.

## 2026-10-18 — Strided encoder bounds proofs

GIR loop versioning now covers unsigned complete-group loops with fixed positive
strides, including three input bytes to four output bytes. An overflow-safe
entry predicate selects a proven fast copy or the original checked loop. Both
paths preserve counters used by the tail. Short buffers still perform the
original successful writes before trapping, and unrelated arithmetic keeps
its checks.

In the unchanged scalar Base64 encoder, this removes four output-bounds branches
per group. The fast loop goes from 36 instructions/five conditional branches to
24/one; the checked fallback remains. On Apple M2, encoding takes 26–28% less
time, with updated Gloin and matched C medians within about 0.5% of one another
and overlapping sample ranges. The complete encoder grows from 512 to 672 bytes.
The [validation record](compiler-validation.md#strided-encoder-loop-versioning--2026-10-18)
contains the method, measurements and limits.

Validation passed 1,067 macOS core tests, 107 focused Linux ARM64 tests and 79
macOS ASan/UBSan tests. New cases cover tail values, aliasing, exact/short buffers,
observable trap order and unsigned arithmetic boundaries in JIT, O0 and O2.
The final package/release audit remains pending.

## 2026-10-18 — Native call generation

Native objects and executables now use LLVM's ordinary AoT target defaults.
The emitter previously created a JIT target machine, which selected the large
code model and emitted register-indirect calls to known functions. Host CPU
features are retained, Linux keeps PIC relocation, and JIT execution retains
its own target setup. The shared GloinIR and LLVM-dialect output is unchanged.

In the recursive interpreter, three calls become direct calls, the evaluator
shrinks from 480 to 440 bytes, and its stack frame falls from 96 to 64 bytes.
The unchanged program takes about **0.8% less CPU time** in the measured M2
batch. The remaining gap to C is about 20.5%; this change does not account for
most of it. The [validation journal](compiler-validation.md#native-call-generation--2026-10-18)
records timings, background activity, comparison limits, and target behavior.

Validation passed 1,068 macOS core tests, 124 focused Linux ARM64 tests and 50
macOS ASan/UBSan tests. A new regression checks direct runtime-call relocations
at O0/O2 and recursive execution in JIT and native modes. Standalone object
linking also passes on macOS and Linux, including default Linux PIE links.
Linux x86-64 execution and the final release/package audit remain pending.

## 2026-10-19 — Adjacent floating checks

GIR combines the finite-result checks of an immediately adjacent multiplication
and addition for `f32` and `f64`, when the product has exactly one use in that
addition. The sum's check detects both failures and remains mandatory even when
the result is discarded. Separate rounding is preserved. Intervening operations,
extra uses and block boundaries prevent the rewrite; the check stays within its
original loop iteration. Whole-loop check deferral and SIMD are outside this
change. The [lowering guide](lowering.md#adjacent-floating-checks-2026-10-19)
documents the proof, undefined-value handling and diagnostic constraints.

The unchanged matrix kernel uses **3.5–5.0% less CPU time** on Apple M2 across
three sizes, with every paired comparison improving. The dot function shrinks
from 176 to 160 bytes. All 120 timed executions pass bitwise output verification.
The [validation journal](compiler-validation.md#adjacent-floating-checks--2026-10-19)
records measurements, C controls and limits; these are kernel measurements,
not end-to-end application speedups.

Validation passes **1,072 supported macOS core tests**, **128 focused Linux
ARM64 tests** and **80 macOS ASan/UBSan tests**, plus independent numerical and
failure checks across both widths and O0/O2. Regressions cover unused results,
observable failure order, separate rounding and mixed GIR/LLVM poison or undef
addends. Linux x86-64 execution and the final release/package audit remain
pending.

## 2026-10-19 — Local floating reductions

GIR can move the finite check of an ordered f32/f64 dot product to its exit when
it proves the complete input ranges remain safe to read. The first scope covers
initialized local fixed arrays, nested rows and their borrowed views. A guarded
fast loop retains separate rounding and source addition order; the original
checked loop remains available when the range predicate fails. The final check
is mandatory even when its result is discarded.

Escaped or unknown storage, missing initialization, observable effects, other
failures and unsupported control flow prevent deferral. Arbitrary slice
parameters and arena/vector buffers still retain their checks. The existing
generic-slice dot object's O2 bytes are unchanged. The
[lowering guide](lowering.md#local-floating-reductions-2026-10-19) documents the
storage, lifetime, layout, arithmetic and control-flow proofs.

With matrices owned locally for the full multiplication, measured M2 CPU time
falls **19–37%** across three sizes. Updated medians are within **0.1–0.5%** of
C using the same final-check approach, with overlapping sample ranges. Every
paired before/after comparison improves. A separate fixture that copies inputs
for each dot product improves only 1–3%; its copying and initialization are timed.
Both comparisons use unchanged source between compiler versions. The
[validation journal](compiler-validation.md#local-floating-reductions--2026-10-19)
records both batches, code-size growth, background noise and limits.

Validation passes 1,078 macOS core tests, followed by 89 focused tests after final
proof hardening; 89 focused Linux ARM64 tests plus three final rejection checks;
and 89 macOS ASan/UBSan tests. Independent oracles cover both widths and O0/O2,
with 162,392 bitwise cases and 160 expected traps. All 360 timed executions and
125 additional matrix checks pass bitwise verification. Linux x86-64 execution
and the final release/package audit remain pending.
