# Test inventory and harness

The maintained suite is the `TEST`/`TEST_F` definitions in `tests/*_test.cpp`,
registered through `gloinc_test` and CTest. CMake checks that every file matching
that pattern is in the explicit target source list and fails configuration if a
suite is omitted. Support programs under `tests/support` are harness fixtures,
not additional test cases. `tests/runtime/arena_test.cpp` is a separate native
`gloin_arena_test` target, registered with CTest and independent of LLVM. `tests/runtime/standard_test.cpp`
`tests/runtime/numeric_test.cpp`, `tests/runtime/io_test.cpp`, `tests/runtime/context_test.cpp`, `tests/runtime/math_test.cpp`, and `tests/runtime/time_random_test.cpp` comprise the independent `gloin_standard_test` target.

The current source definitions and CTest discovery contain **880 tests**:

| Suite | Tests |
| --- | ---: |
| LexerTest | 34 |
| DiagnosticsTest | 20 |
| CheckedProgramTest | 13 |
| ParserTest | 49 |
| ScopeTest | 11 |
| VariablesTest | 17 |
| NumericTest | 12 |
| FunctionsTest | 12 |
| OperatorsTest | 16 |
| ControlFlowTest | 12 |
| ForUnlessTest | 15 |
| LoweringTest | 15 |
| SemaTest | 9 |
| SemaAsyncTest | 4 |
| MLIRSetup | 4 |
| CodeGenTest | 5 |
| CodeGenStructTest | 2 |
| CodeGenSpecTest | 2 |
| CodeGenGenericsTest | 4 |
| BasicCodeGenTest | 6 |
| SpecTest | 8 |
| ArenaTest | 21 |
| ArenaRuntimeTest | 11 |
| AsyncTest | 2 |
| ArrayStringTest | 4 |
| UnlessTest | 1 |
| JitRunnerTest | 16 |
| CliTest | 20 |
| StandardModuleTest | 17 |
| StandardLibraryTest | 24 |
| StringLibraryTest | 18 |
| TextLibraryTest | 17 |
| NumericLibraryTest | 12 |
| NumericRuntimeTest | 16 |
| IoLibraryTest | 15 |
| IoRuntimeTest | 14 |
| ContextLibraryTest | 14 |
| ContextRuntimeTest | 9 |
| MathLibraryTest | 12 |
| MathRuntimeTest | 12 |
| TimeRandomLibraryTest | 15 |
| IntegratedExamplesTest | 17 |
| TimeRandomRuntimeTest | 13 |
| StandardRuntimeTest | 17 |
| ModuleTest | 25 |
| OrdinaryStructTest | 14 |
| PointerTest | 20 |
| MethodTest | 19 |
| DeferTest | 24 |
| CoreAcceptanceTest | 152 |
| E2ETest | 31 |
| ExternalRunnerTest | 8 |

The four generic tests are now included without changing their assertions. Their
IR-string checks pass but do not establish generic execution. The orphaned
`tests/lit/spawn.mlir` has been replaced by structured assertions in
`CodeGenTest.GenerateSpawn`: worker/main functions and returns, the constant i32
argument, spawn callee, operand, and handle type. No lit/FileCheck installation
is required. This test now exposes the missing spawn operation instead of
passing on a non-null module.

Maintained tests cover loops (SPEC-017), standard modules (SPEC-023), and local
modules (SPEC-029). The CLI also exercises `core_counter.gloin`, `hello_world.gloin`,
`arena_lab.gloin`, `module_lab.gloin`, `standard_library.gloin`, and
`strings_lab.gloin`, `text_lab.gloin`, `numbers_lab.gloin`, `io_copy.gloin`, `io_filter.gloin`, `file_tool.gloin`, `math_lab.gloin`, `simulation_lab.gloin`, `config_reader.gloin`, and `statistics_tool.gloin`. Every remaining example passes `gloinc --check`.

## Running and inspecting tests

After a tests-enabled build (see [toolchain.md](../docs/toolchain.md)):

```sh
ctest --test-dir build --show-only=json-v1
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
ctest --test-dir build -R '^(E2ETest|ExternalRunnerTest)' -j 4 --output-on-failure
```

All CTest cases have a 30-second timeout. No known failures are disabled or marked
as expected successes. The current local parallel run reports **876 passes and
4 known async/spawn failures**, with no unexpected test-process crashes or skips.

## Integrated examples (SPEC-030h)

Seventeen `IntegratedExamplesTest` cases run the configuration reader, selected-column
statistics tool, and simulation. They cover file/CLI success, exact grammar/size/numeric
bounds, independent integer-sum and seeded simulation references, retained string
copies, module composition, existing destination preservation, and external LLVM
execution. Real examples/data are installed and relocated with the runtime.

In-process runs use a scoped backing allocator to count actual native arena blocks,
enforce a 512 KiB budget, inject block-allocation failures, and require no live
allocations on return. 100,000-line/record streams use two blocks each; repeated
failures preserve descriptor counts. Source-library substitutions inject write,
flush, and close failures; controlled clocks test simulation failures. Two additional
`ArenaRuntimeTest` cases verify nested allocator scopes, retained allocator contexts,
failure restoration, and thread isolation. These checks participate in required
and sanitizer gates. The larger [stress script](../scripts/check-integrated-examples.py)
checks all three programs at one million records/samples with a 2 MiB stack,
including rejection of one-over-limit inputs. See [the guide](../docs/integrated-examples.md).

## Timing and seeded randomness (SPEC-030g)

Fifteen `TimeRandomLibraryTest` cases cover duration overflow/units, backwards
ticks, exact PRNG vectors, independent copies, invalid bounds without advancement,
forced rejection, both unit-float endpoints, and the one-word float mapping.
They verify JIT clock binding/restoration, failure normalization, module privacy,
replaceable source modules, malformed native ABIs, and external LLVM execution.
Both guide programs and the packaged simulation execute verbatim; an injected
clock checks the complete simulation's elapsed report exactly.

Thirteen independent `TimeRandomRuntimeTest` cases cover four reference seeds,
counter wrap, both word endpoints, timespec overflow, errno preservation,
callback status normalization, copied descriptors and borrowed userdata,
nested scopes, invalid/out-of-order/foreign-thread pops, exception restoration,
and concurrent thread isolation. Both suites are required and run under native
sanitizers; compiler cases also run against installed and relocated packages.

## Numerical utilities (SPEC-030f)

Twelve `MathLibraryTest` cases execute all 47 public functions, typed constants,
signed-minimum absolute overflow, signed-zero selections, clamp bounds, floating
rounding, domains, finite subnormals, independent numerical references, and the
explicit `def foo: int = -some_val;` initializer. Canonical module privacy, wrong
signatures, invalid private selectors, replaceable source policy, native symbol
collisions, malformed MLIR ABIs, and all four external LLVM math ABIs are covered.
Both guide programs and the streaming geometry/statistics example run verbatim,
including invalid/empty input and installed/relocated package execution.

Twelve independent `MathRuntimeTest` cases verify both widths against exact cases,
decimal constants (4 scaled machine epsilons), trigonometric/logarithmic identities
(16 scaled epsilons), halfway neighbors, large integral rounding, domains and
nonfinite native inputs, zero signs, representable subnormals versus underflow,
overflow, stable hypot, and errno/floating-environment restoration on success and
failure. Concurrent host threads retain independent rounding environments. These
suites participate in required and sanitizer validation; compiler cases also
participate in installed/relocated validation.

## Filesystem and process context (SPEC-030e)

Fourteen `ContextLibraryTest` cases cover the lexical path matrix, exact/overflow
bounds, both join allocation failures, metadata/mutations, CLI forwarding and
filename escaping, missing/empty/copied environment values, process allocation
failures, host cwd, invocation argument restoration and rejected NUL, type/privacy
checks, native collisions/ABIs, external execution, and the packaged tool/guide.
The tool runs from another directory with spaced paths, missing/empty/nonempty
labels, and malformed options that leave the filesystem untouched.

Nine native `ContextRuntimeTest` cases cover regular files/directories/broken
symlinks/FIFOs, counted paths and invalid-input preservation, mkdir/unlink/rename
semantics, permission errors, deep-copied C argument scopes, invalid/LIFO cleanup,
thread isolation, raw environment bytes, exact cwd bounds, and deleted cwd.
Both suites belong to required and sanitizer validation; compiler cases also run
against installed and relocated packages.

## Streams and files (SPEC-030d)

Fifteen `IoLibraryTest` cases exercise borrowed stdin/stdout/stderr, compatible
stdio buffering, each file mode and I/O method, alias close state, bounded binary
reads and EOF, partial read_all prefixes, wrong directions, malformed paths,
OS errors/messages, metadata allocation before destructive opens, read allocation
failure without consumption, scratch reuse, failed-close invalidation, write_line
suffix failure, receiver mutability/privacy, native signatures and collisions,
external LLVM execution, and packaged copier/filter/guide programs.

Fourteen independent `IoRuntimeTest` cases cover real binary/empty files,
exclusive create/append/truncate, actual and injected permission errors, chunk/line/
read_all boundaries, partial read errors, deterministic short/failing/zero-progress
writes, injected flush/close failures, broken pipes with preserved signal state,
bounded diagnostic messages, and 2,000 open/close cycles with descriptor checks.
Injection is per-call or per-stream; no process-global test hooks change runtime
behavior. Both suites are required by `check-core` and ASan/UBSan; compiler tests
also run against installed and relocated packages.

## Numeric text and conversion (SPEC-030c)

Twelve `NumericLibraryTest` cases execute all 29 public APIs, recoverable failures,
exact/rounded/truncated conversion choices, signed zero, arena allocation failure,
invalid-precision preflight, source type/privacy checks, reserved runtime-name
collisions, malformed native ABIs, and external LLVM execution. The statistics
example and both complete [guide](../docs/numbers.md) programs run verbatim against
normal, installed, and relocated compilers.

Sixteen LLVM-independent `NumericRuntimeTest` cases check integer limits, counted
strict grammar, long inputs, direct-width midpoint rounding, finite float extremes,
subnormals, signed zero, boolean spelling, 20,000 sampled float bit patterns,
locale independence, formatting buffer guards, fixed rounding and precision,
safe conversion boundaries, and cleared payloads on every native failure class.
Both suites are required by `check-core` and ASan/UBSan validation. A separate
million-iteration composition of parsing, formatting, conversion, cursor traversal,
and arena reset passes with a 2 MiB stack.

## Text traversal and construction (SPEC-030b)

Seventeen `TextLibraryTest` cases cover split/line boundary semantics, independent
cursor positions and borrowed views, invalid delimiters, bounded concat/repeat/
replacement/case conversion, overflow and empty-output paths, independent result
lifetimes, all 256 byte values, shared builder state, atomic failed appends,
zero capacity, read-only queries, snapshots, and source reset/free.

Per-handle allocation injection exercises state allocation failure, buffer
allocation failure, and snapshot failure through the actual typed-arena bridge.
10,000 append/clear iterations followed by an injected third-allocation failure
verify that mutation and cursor traversal allocate nothing. Private primitive
range/null/type guards, public mutability/privacy, and external LLVM execution
are checked. The packaged report and both guide programs run verbatim against
normal, installed, and relocated compilers. The suite is part of `check-core`
and sanitizer validation. A separate million-iteration composition of splitting,
case conversion, building, snapshotting, replacement, and scratch reset passes
with a 2 MiB stack.

## Byte strings (SPEC-030a)

Eighteen `StringLibraryTest` cases cover all 14 public functions, shared status
compatibility, unsigned ordering, NUL/arbitrary bytes, UTF-8 byte slicing,
extreme/empty bounds, first/absent/empty search, exact ASCII trimming, and a
64-case independent search/order oracle. They exercise copy lifetime independence,
source reset/free and overwrite, injected allocation failure, allocation-free
queries, cleared handles, type/arity/privacy diagnostics, native-name collisions,
reserved struct names, primitive bounds traps, null empty descriptors, JIT ABI
validation, external LLVM execution, and the packaged configuration example.
Both Gloin code blocks in [the API guide](../docs/strings.md) compile and execute
verbatim in the maintained suite. The suite is required by `check-core` and
installed/relocated package validation.

Two additional `StandardRuntimeTest` cases verify all 256 byte values, exact-sized
copy destinations and guard bytes, no implicit terminator, and null-pointer
zero-length copies. The native runtime is instrumented by the sanitizer build.
A manual stress run of `strings_lab.gloin` expanded to one million iterations
also passes with a 2 MiB stack; its scratch arena is reset each iteration.

## Standard input and conversions (SPEC-030)

Twenty-four `StandardLibraryTest` cases cover the source API, prompts/stdin,
explicit statuses, integer boundaries, exact output bytes, wrong types/arity,
EOF/empty/CRLF/final lines, NUL/non-UTF-8 bytes, zero/exact/oversized limits,
allocation and I/O errors, retained strings across growth, arena reset/free,
deferred views, zeroed bytes, private primitives, symbol collisions, JIT ABI
validation, replaceable library source, external LLVM execution, and the packaged
interactive example. The fixture can redirect stdin to a real file.

Fifteen independent `StandardRuntimeTest` cases cover decimal grammar and bounds,
syntax-vs-overflow precedence, counted non-NUL-terminated input, 10,000 deterministic
round trips, bounded terminated formatting, input/EOF/line endings/raw bytes,
oversized-line draining, I/O errors, and zero-initialized arena reuse. The native
runtime and compiler are instrumented by the sanitizer build. CLI/library tests
also run against installed/relocated packages.

## Local modules (SPEC-029)

Twenty-five `ModuleTest` cases cover source-relative and parent paths, optional
extensions, spaces, shared dependencies and nominal types, unique emitted symbols,
exported constants, private declarations/fields/methods, isolated file scopes,
imported entry names, unused invalid code, forbidden globals, constant errors,
duplicate imports, shadowing, symlinks, cycles, depth limits, located diagnostics,
fresh reads, standard dependencies, native primitive identity, in-memory roots,
qualified deferred calls, invalid function values/addresses, and the module lab.
Successful cases execute through the CLI; selected cases inspect LLVM definitions
and execute externally. Negative cases exercise every CLI compilation mode.
The suite participates in required, sanitizer, and installed/relocated package checks.

## Arena allocation (SPEC-028)

Twenty-one `ArenaTest` cases exercise the source API, all scalar types, nested
struct/string/pointer values, native layout, shallow copies, initialized references,
fallible allocation, mutable receivers, stable linked objects, reset/handle
aliasing, cleared-handle traps, evaluation order, deferred allocation, module
privacy, other allocator types, exact runtime ABIs, forced failure, and external
LLVM execution. They participate in required and installed/relocated package checks.
The previous unchecked `Arena::new` test has been replaced by these executable
source tests. Core fixtures cover success, invalid type-valued arguments, the
required import, and use of a cleared handle.

Nine independent `ArenaRuntimeTest` cases exercise the actual native allocator,
including alignment through 64 KiB, mixed-size object contents across growth,
zero-sized allocations, large blocks, reset retention/reuse, independent arenas,
overflow, deterministic allocation failure, unchanged state on failure, and
balanced releases. This target builds without the compiler/backend and runs under
ASan/UBSan. On macOS, the external LLVM test preloads the test executable's ASan
library before loading an instrumented arena runtime into `mlir-runner`.

The maintained example is `examples/arena_lab.gloin`. A manual stress run with
one million particles over two frames passes under a 2 MiB stack. Retired arena
references are never dereferenced by valid tests: reuse of retained storage does
not provide general stale-reference detection.

## Function-exit defer (SPEC-027)

Twenty-four `DeferTest` cases cover registration-time capture of all scalar
widths, strings, structs, pointers, and method receivers; conditional/zero-loop
registration; repeated mixed loop sites; reverse ordering; explicit, implicit,
and nested early returns; return-value preservation; recursive and nested logs;
module/native-output calls; invalid operands/captures; no unwinding on traps;
nullable receivers; and native allocator-name isolation. A stress test retains
100,000 registrations and checks every reverse-order value. External LLVM
execution verifies the same result independently. Instrumented native calls
count 220 allocations and zero pending records at completion; a forced null
allocation traps. Malformed allocator ABIs are rejected before JIT invocation.

The suite and source fixtures participate in required and installed/relocated
package checks. The former blanket defer rejection now tests a non-call operand.
`run/defer.gloin` verifies conditional and loop cleanup; `trap/defer_cleanup.gloin`
verifies a failure inside cleanup. The old loop-header rejection now targets
`defer` in an initializer, since defer in the body is valid.

## Methods (SPEC-026)

Nineteen `MethodTest` cases cover static construction, explicit instance receivers,
mutation, readonly local/parameter storage, forward/recursive calls, receiver and
argument evaluation order, aggregate/pointer results, numeric literal context,
null handling, isolated member namespaces, module exports and private helpers,
nominal type mismatches, malformed signatures/bodies/calls, rejected temporary
receivers, loop storage, and external LLVM execution. IR assertions require one
self pointer for instance methods and none for static methods. The suite and
three source fixtures (successful methods, invalid receiver, null access in a
method) participate in both required and package checks.

## Pointers and references (SPEC-025)

Eighteen `PointerTest` cases cover all scalar pointees, whole structs/strings,
stable local/parameter addresses, read-only views, nested pointer qualifiers,
reference/raw-pointer conversions, recursive struct links, privacy, null traps,
evaluation order, invalid addresses, live aliases, caller-reference returns,
entry-block allocation in loops, native field layout, and external LLVM execution.
`run/pointers.gloin` and `trap/null_dereference.gloin` add maintained source
acceptance. Former blanket pointer rejections now assert pointee mismatches and
null-reference errors. All these cases are in the required gate and package checks.
Lifetimes remain manual; these tests do not claim borrow checking or dangling
pointer detection.

## Ordinary structs (SPEC-024)

Fourteen `OrdinaryStructTest` cases exercise nested value copies, forward types,
parameters/returns, field writes, strings and empty records, literal evaluation
order, signedness and floating fields, exact initialization, and loop-local
storage. Negative cases cover unknown/duplicate/missing/private fields,
recursive layouts, wrong nominal types, uninitialized reads/writes, immutable
roots and nested fields, temporary receivers, invalid method receivers, and deferred packed/generic
forms. Context reuse checks prevent backend type names leaking between programs.
Layout tests assert padding, field offsets, and ABI alignment using both the
native layout and explicit 32/64-bit pointer layouts. An external LLVM runner
independently executes an aggregate call and field mutation.

The required gate and package checks include this suite and the maintained
`run/ordinary_struct.gloin` source fixture. Prior ordinary-struct rejection
fixtures now cover packed structs; invalid member access on a function remains
an error. These updates preserve deferred-feature rejection coverage.

## Standard modules and output (SPEC-023)

Seventeen `StandardModuleTest` cases cover exact UTF-8/NUL/escape/percent bytes,
empty strings, newlines, constants and delayed locals, call evaluation order,
branches and loops, file-wide imports, namespace shadowing/conflicts, wrong
arguments, unsupported modules/members, void-value rejection, silent checking
and IR modes, repeated compilation/JIT execution, invalid runtime ABI rejection,
and output write errors. File-loading tests replace `std.gloin` to change behavior
and signatures, add `math.gloin` without compiler changes, reject missing/empty/
unreadable files, preserve library diagnostic locations, enforce private exports
and scope isolation, and verify the byte-output primitive's visibility.
`CoreAcceptanceTest.StandardHelloWorld` drives a maintained
source fixture through the CLI. The required `check-core` target and installed/
extracted package checks include this suite.

As requested for SPEC-023, CLI tests now assert main's result modulo 256 as the
host exit status, independently of imports and explicit stdout. Full i32 results
remain checked by the JIT and external execution suites. Prior milestone evidence
below describes the old CLI contract at the time those checks ran.

## Lexical contract checks

All 34 lexer tests pass, including the seven failures from the original audit.
They cover reserved words versus identifier prefixes, operators adjacent to operands,
LF/CRLF and comment positions, repeated EOF, malformed numbers and quoted literals,
escape spelling, Unicode scalars, invalid UTF-8 in tokens and comments, BOM/NUL,
unsupported trivia, and progress with bounded spans for every byte value.
The literal grammar and reserved-token policy are defined in
[language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec#lexical-literal-forms-spec-008). Token recognition does not
establish parsing, type support, numeric conversion, or string execution.

## Parser contract checks

All 49 parser tests pass. Twenty new cases cover complete canonical programs,
identifier conditions, precedence/associativity, newline trivia at every token
boundary, strict lists, mandatory annotations, modifier order, declaration scopes,
statement-only assignment, required loop separators, malformed/truncated input,
source spans, excessive nesting, and deferred syntax composition.

`ParseMode::Core` is the default used by `compile_source`. Stage-isolated tests
of later features explicitly select `ParseMode::SyntaxOnly`; this does not grant
those features language support. Both modes reject obsolete modifiers, untyped
receivers, malformed lists, and legacy `spawn`/`await`. The method fixture now
uses `def pub greet(self: *Person)`, and the generic pair fixture includes its
missing field comma. Existing feature assertions remain intact. The standalone
`unless` fixture now uses the statement API and checks its condition/body and EOF.
[The parser contract](../docs/parser.md) documents consumption and failure rules.

## Source diagnostics and pipeline checks

The 20 diagnostic regressions cover owned token/AST source spans, CRLF locations,
malformed/unterminated input, partial-AST rejection, semantic failure propagation,
non-boolean conditions, unsupported nodes, codegen failure/module disposal,
void-call value checking, explicit rendering, generated MLIR locations, reserved
syntax rejection, encoding errors in trailing comments, type/literal separation,
core rejection of deferred syntax, and rejection of runtime calls in constant initializers.
[The diagnostic API](../docs/diagnostics.md) documents the stage contracts.
E2E tests use `compile_source` so a failed stage cannot reach external execution.
The sixth E2E case executes a multiline call inside `if enabled` and returns 42.
Stage-isolated codegen tests check parser/module status before inspecting IR;
they continue to expose incomplete features rather than hiding errors.

## Checked program contract

All 13 checked-program tests pass. They verify every core scalar's semantic
identity and MLIR signature/storage type, aliases, signedness, unknown/deferred
type rejection in all annotation positions, `void` restrictions, declaration IDs,
call resolution, ownership after frontend/codegen destruction, duplicate and
unresolved declaration failures, target rejection, independent compiler runs,
return checking, and verified scalar operators. Compile-time assertions
prove that raw ASTs cannot call `CodeGen::generate` and clients cannot construct a
`CheckedProgram`. [The contract](../docs/checked-program.md) documents the limits.

Existing stage-isolated backend tests now use the explicit
`generate_unchecked_for_testing` entry; their feature assertions and failures are
preserved. All 29 E2E cases use executable-mode checked generation. The seventh executes an
`int` alias and nested shadowing, confirming the outer binding still returns 42.

## External execution

CMake discovers `mlir-opt` and `mlir-runner` in the selected LLVM installation's
tools directory. `GLOIN_MLIR_OPT` and `GLOIN_MLIR_RUNNER` cache paths permit explicit
overrides. Missing tools fail configuration when tests are enabled; runtime
launch failures are test errors. Tests-disabled builds do not discover these
programs or create the fixture executable.

Language execution tests call `run_external_module`, which clones the verified
high-level module and uses the production `lower_to_llvm` pipeline. The external
optimizer only parses/verifies the serialized LLVM-dialect IR; it no longer has
its own conversion pass list. The string-level `run_external_mlir` API accepts
already-lowered IR and remains available for controlled subprocess fixtures.

Each E2E invocation owns a unique temporary directory containing its input,
lowered IR, and separate stdout/stderr files for each child process. Files are
removed on success and failure. There is no shared `temp.mlir`, shell pipeline,
or process-wide working-directory change. LLVM's process API passes arguments
directly and kills a child exceeding its 10-second limit. The optimizer must
exit successfully before the runner starts. Both exit statuses are checked;
diagnostics include the failed tool, status, and captured stderr.

Only successful runner output containing exactly one signed i32 (surrounded by
optional whitespace) becomes a program result. Empty, trailing, multi-result,
and out-of-range output is rejected. Tool errors use `llvm::Expected` and cannot
be confused with a legitimate program return of `-1`.

The fixture executable exercises numeric stdout paired with failure status from
either tool, missing executables, malformed results, timeouts in both stages,
and concurrent calls returning different values. Its executable path contains a
space to exercise argument handling. Fixture timeouts are one second; the
parent test remains subject to CTest's 30-second limit.

## Remaining failures

| Tests | Follow-up |
| --- | --- |
| `AsyncTest.SpawnGeneration`, `SemaAsyncTest.AsyncTypes` | SPEC-040/SPEC-041: legacy syntax is rejected; concurrency contract, canonical fixtures, and runtime remain open. |
| `AsyncTest.DeferredFunctionGeneration` | SPEC-040: fixture uses obsolete `let` and an omitted return annotation; replace against the deferred-call contract when defined. |
| `CodeGenTest.GenerateSpawn` | SPEC-040, SPEC-041: restored lit assertion finds no spawn operation |
