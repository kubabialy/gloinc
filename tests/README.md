# Test inventory and harness

The maintained suite is the `TEST`/`TEST_F` definitions in `tests/*_test.cpp`,
registered through `gloinc_test` and CTest. CMake checks that every file matching
that pattern is in the explicit target source list and fails configuration if a
suite is omitted. Support programs under `tests/support` are harness fixtures,
not additional test cases. `tests/runtime/arena_test.cpp` is a separate native
`gloin_arena_test` target, registered with CTest and independent of LLVM. `tests/runtime/standard_test.cpp`
`tests/runtime/numeric_test.cpp`, `tests/runtime/io_test.cpp`, `tests/runtime/context_test.cpp`, `tests/runtime/math_test.cpp`, and `tests/runtime/time_random_test.cpp` comprise the independent `gloin_standard_test` target.

At 0.0.2, maintained source definitions and CTest discovery contain **867 tests**:

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
| CliTest | 19 |
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
| PointerTest | 18 |
| MethodTest | 19 |
| DeferTest | 24 |
| CoreAcceptanceTest | 142 |
| E2ETest | 31 |
| ExternalRunnerTest | 8 |

The four generic tests are now included without changing their assertions. Their
IR-string checks pass but do not establish generic execution. The orphaned
`tests/lit/spawn.mlir` has been replaced by structured assertions in
`CodeGenTest.GenerateSpawn`: worker/main functions and returns, the constant i32
argument, spawn callee, operand, and handle type. No lit/FileCheck installation
is required. This test now exposes the missing spawn operation instead of
passing on a non-null module.

The root-level `simple_test_runner.cpp`, `test_imports.cpp`, and
`test_for_loop_parsing.cpp` are legacy manual probes, excluded from the maintained
inventory. They have stale relative includes and weak or print-only checks;
their output is not build or language-coverage evidence. Maintained tests cover
loops (SPEC-017), standard modules (SPEC-023), and local modules (SPEC-029). The CLI also exercises `core_counter.gloin`, `hello_world.gloin`,
`arena_lab.gloin`, `module_lab.gloin`, `standard_library.gloin`, and
`strings_lab.gloin`, `text_lab.gloin`, `numbers_lab.gloin`, `io_copy.gloin`, `io_filter.gloin`, `file_tool.gloin`, `math_lab.gloin`, `simulation_lab.gloin`, `config_reader.gloin`, and `statistics_tool.gloin`; other examples remain manual inputs.

## Running and inspecting tests

After a tests-enabled build (see [toolchain.md](../docs/toolchain.md)):

```sh
ctest --test-dir build --show-only=json-v1
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
ctest --test-dir build -R '^(E2ETest|ExternalRunnerTest)' -j 4 --output-on-failure
```

All CTest cases have a 30-second timeout. No known failures are disabled or marked
as expected successes. The local parallel SPEC-030h run reports
**863 passes, 4 failures, no unexpected test-process crashes or skipped tests**.

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

SPEC-007's newline handling made
`LexerTest.HandlesComments`, `LexerTest.TrackLineNumbers`,
`ParserTest.ParseStructDefinition`, and `ParserTest.ParsePackedStruct` pass.
At SPEC-007, three formerly passing cases exposed failures: arena construction,
the invalid async fixture, and array type resolution. SPEC-028 now implements
arenas with source-based coverage; arrays remain an explicit rejection under SPEC-035. None were disabled or converted into
expected successes. `BasicCodeGenTest.HandlesControlFlow` now explicitly declares
its assigned variable `mut`, as required by SPEC-006; a new diagnostic regression
checks that the original immutable assignment fails. SPEC-007 added 15 passing
regressions. SPEC-008 adds another 18 and resolves the five remaining lexer
failures, moving the full suite from 114/127 to 137/145 passes. Legacy `spawn`
and `await` expressions now fail explicitly in parsing; their feature tests
remain failing until the corresponding language tasks are completed.

SPEC-009 adds 23 passing regressions (20 parser, two diagnostic, one E2E), moving
the suite to 160/168 passes with exactly the same eight failures as SPEC-008.
Constants now parse but are deliberately rejected in Sema/codegen until SPEC-012
implements their evaluation; the new metadata cannot silently imply execution.

SPEC-010 adds 14 passing regressions (13 checked-program tests and one E2E),
moving the suite to 174/182 passes with the same eight failures. Primitive storage
agreement does not claim complete numeric conversions, operators, or return-path
checking; those remain SPEC-013 through SPEC-015.


## Declaration and scope checks (SPEC-011)

All 11 `ScopeTest` cases pass. They cover verified forward-call signatures,
argument validation, duplicate functions/parameters/locals, parameter/body scope,
local declaration order, initializer visibility, block/branch/while/function
isolation, nearest-binding call lookup, parameter mutability, signature errors
before body checking, built-in name protection, and independent compiler runs.
Negative pipeline cases assert semantic failure, no module, and located errors;
duplicate and unresolved-name diagnostics identify the offending identifier.

Five new E2E tests execute forward call chains (including a void call), direct
recursion, mutual recursion, initializer lookup with function/local shadowing,
and sibling/branch/loop scope restoration. Recursion tests return 21 and 42;
the other new programs return 42. Nested branches in the mutual recursion case
also exercise removal of empty unreachable continuations in both arms.

SPEC-011 adds 16 passing tests, moving the suite to **190/198 passes** with the
same eight failures. Complete control flow, return checking, and `for` scope
remain in SPEC-014/SPEC-016/SPEC-017. Aggregate type collection remains deferred
with aggregate support. Constants were still explicitly unsupported at this milestone;
SPEC-012 below supplies their evaluation.


## Variables, constants, and initialization (SPEC-012)

All 17 `VariablesTest` cases pass. They check assignment targets through parsing
and the AST API, explicit annotations, initializer/store type mismatches,
uninitialized reads in expressions/conditions/calls, delayed immutable
initialization, branch joins with returning paths, nested branches, loop and
shadowing state, and storage verification for every core scalar width.

Constant cases cover required metadata, immutability, runtime binding/call
rejection, lexical dependency order, namespace conflicts, invalid operators,
integer overflow, zero divisors, floating range/rounding, negative zero,
short-circuit evaluation, and state/ownership across invocations. IR assertions
show folded constants have no runtime storage or initializer arithmetic.
Located negative cases fail before codegen and return no module; EOF parser
errors can have zero-length insertion spans.

Seven new E2E cases execute delayed mutable/immutable initialization, both sides
of branch initialization, a returning branch, fresh loop-local initialization,
global/local constant shadowing, constant short-circuiting, and folded numeric
and boolean operators. Six return 42; the shadowing case returns 44. The loop
case also verifies the backedge after a nested branch: codegen now checks the
final body block and removes an empty unreachable continuation.

SPEC-012 adds 24 passing tests, bringing both full runs to **214/222 passes**
with the same eight known failures. The existing diagnostic regression now
rejects a runtime call inside a constant initializer, preserving its protection
against silently treating constants as runtime bindings. The unchecked backend
still rejects constants. Contextual numeric typing, full return/control-flow
validation, and runtime operator behavior remain their subsequent tasks.


## Numeric literals and conversions (SPEC-013)

All 12 `NumericTest` cases pass. They cover preserved parser spellings, exact
integer minima/maxima and adjacent failures at every width, all bases, full u64,
full-consumption validation through the AST API, declaration/store/call/return
context, typed operands on either side, nested literal arithmetic, default
literal types, unsupported casts and typed conversions, float range/subnormals,
and checked constant arithmetic for every integer width and both float widths.
Integer boundary cases inspect emitted values and verify modules for both runtime
locals and constants. Negative cases fail before codegen and retain source spans.

The float-bit test executes eight fixtures, including binary32 and binary64 ties,
just-above-tie decimals, smallest subnormals, and negative zero. Each source
function is compiled through `compile_source`. A test-only MLIR adapter compares
the returned bit pattern and returns 42 through the existing external runner.
This observes exact floating results without claiming Gloin runtime floating
comparisons or cast syntax, which remain unsupported.

Five new E2E cases execute all integer base forms, contextual narrow/wide values,
signed minima, full u64 values, width-correct constant arithmetic, and f64
constant range/precision. Every new invocation returns 42. Together these add
17 passing tests, bringing both full suites to **231/239 passes** with the same
eight known failures.

Existing synthetic AST fixtures now construct numeric nodes from spellings.
The i64 constant fixture is accepted under contextual typing; the invalid i8
floating initializer still fails, with a category diagnostic. The floating
remainder fixture now uses f32 so its assertion still tests the unsupported
operator rather than an incompatible annotation. Assertions for deferred
features remain intact. Runtime operator semantics, return-path checking, and
full control-flow validation remain SPEC-014 through SPEC-017 as applicable.


## Functions, calls, returns, and entry points (SPEC-014)

All 12 `FunctionsTest` cases pass. They cover canonical return types at every
scalar width, missing values/fallthrough, nested branches, conservative loop
analysis, bare/implicit void returns, void-call value rejection, unreachable
source, direct call arity/types, parameter immutability and scope, and entry
validation in module/executable modes. Tests inspect every scalar call ABI and
verify that nested arguments are emitted once in left-to-right order. Invalid
source fails before codegen and returns no module; top-level return is also
rejected through the AST API. Reusing Sema does not retain the prior entry point.

Five new E2E cases execute every arm of nested returning branches, implicit/early
void returns and discarded results, loop returns with zero-iteration fallbacks,
and nested calls with typed value parameters. Four return 42; a private `main`
using the `int` alias returns -1. All 29 E2E cases now explicitly request
`CompilationMode::Executable`, and existing forward/direct/mutual recursion
continues to execute correctly. Helper-only and float-bit adapter compilation
uses module mode; it does not bypass function checking.

The existing return-signature guard fixtures now expect semantic errors.
The codegen-failure diagnostic fixture declares a boolean helper result for its
unsupported `!=` expression, so it still verifies codegen module disposal after
passing Sema. Deferred feature assertions remain intact. Both full suites report
**248/256 passes**, with the same eight failures and no crashes/skips. All 114
focused function/numeric/variable/scope/diagnostic/checked-program/E2E tests pass.
General runtime operators, control-flow lowering, `unless`/`for`, and JIT/CLI
integration remain SPEC-015 through SPEC-020.


## Expression operators and arithmetic failure (SPEC-015)

All 16 `OperatorsTest` cases pass. The scalar operator matrix verifies every
accepted integer/float/bool combination and rejects invalid categories, mixed
types, unsupported syntax, unsupported AST operators, and nested assignments.
Constant and runtime checking use one operator-type table. Existing parser
precedence and constant-evaluation tests continue to pass.

The suite performs **103 external executions: 37 return 42 and 66 intentionally
trap**. Successful programs cover signed/unsigned arithmetic and all comparisons
at every integer width, negative division/remainder signs, high unsigned values,
valid minimum/maximum results, both floating widths, boolean truth tables, and
nested operators in conditions, loops, stores, and call arguments. Eight exact
float-bit probes cover per-operation rounding, subnormals, and signed zero using
a test-only bitcast adapter. These are in addition to SPEC-013's eight literal
bit probes and the 29 existing E2E cases.

Overflow, unsigned underflow, signed-minimum negation/division/remainder, zero
divisors (including negative floating zero), and non-finite floating results
must fail in the runner with a trap signal. Compiler/optimizer errors and
timeouts do not satisfy those assertions. Expected child traps are distinct
from test-process crashes and from a valid program return of -1.

Short-circuit cases skip trapping calls and execute required right operands.
A test-only LLVM global records calls as decimal digits, proving skipped calls
have no side effects and evaluated operands/arguments run once, left to right.
The test instruments helper bodies after checked compilation; it does not add
language globals or change generated source call/control-flow operations.

The old checked-operator rejection fixture now verifies successful typed modules;
invalid return signatures still fail in Sema. The partial-module disposal test
uses the explicit legacy backend, which still rejects runtime `!=`, preserving
its codegen-stage failure assertion now that checked codegen supports it. The
legacy stage path does not establish the new operator semantics.

All **130 focused tests pass**. Full serial/parallel runs report **264/272 passes**
with exactly the same eight known failures, no test-process crashes or skips,
and 30-second timeouts. General control-flow completion, `unless`/`for`, lowering,
and JIT/CLI integration remain SPEC-016 through SPEC-020.


## Nested branch and loop control flow (SPEC-016)

All 12 `ControlFlowTest` cases pass. The initial ten-case regression run exposed
one backend failure: raw generation accepted statements after terminated paths.
The other nine cases passed before the change. Backend generation now rejects
such source defensively, while checked source retains SPEC-014's semantic
rejection. Numeric conditions fail in both Sema and codegen; source diagnostics
identify the condition span.

Nine external executions cover every leaf of nested branches and else-if chains,
partially returning arms, nested loops with branches and both backedges, early
returns from loops, loop bodies whose two arms return, empty blocks/arms/bodies,
implicit void returns, and conditions containing arithmetic guards and boolean
short-circuiting. Every generated function is verified with MLIR and a structural
walk: blocks have one final terminator, successors stay within the function, and
all blocks are reachable in the control-flow graph (both edges of constant
conditions are included).

Two executions instrument helper bodies with test-only counters. They verify
that if conditions run once, unselected else-if conditions do not run, while
conditions run before the first iteration and after every continuing iteration,
and an empty loop keeps rechecking until its condition becomes false. The source
branch/loop/call operations are untouched; this instrumentation adds no language
global-variable feature. Six programs return 42; the remaining programs assert
34, the exact condition trace 1234343435, and four condition evaluations.

All **142 focused tests pass**. Full serial/parallel suites report **276/284
passes**, the same eight known failures, no test-process crashes/skips, and
unchanged 30-second timeouts. Existing tests and deferred feature assertions are
unchanged. `unless`/`for`, shared lowering, and in-process JIT/CLI integration
remain SPEC-017 through SPEC-020.


## Unless and C-style for loops (SPEC-017)

All 15 `ForUnlessTest` cases pass. They cover all eight combinations of omitted
header components, required separators/braces, boolean conditions, header/body
scope and shadowing, initializer name lookup, definite initialization through
continuing paths, immutable repeated writes, conservative returns, and invalid
references in unexecuted bodies/updates. Numeric condition errors preserve their
source spans. Hand-built invalid initializer statements fail in both checking
and raw generation.

Ten verified external executions cover false/true `unless`, the three-iteration
counter returning 3, zero-iteration loops, header constants, void-call updates,
discarded expressions, nested loops, early returns that skip trapping updates,
and arithmetic guards/short-circuit expressions in loop headers. A test-only
LLVM global records helper calls without changing the source control flow. The
exact trace **123440422** proves initializer-once, condition-before-body,
body-before-update, final false-condition checking, and single evaluation of a
skipped `unless` body condition. Eight other programs return 42.

Parser tests now accept omitted header components and keep rejecting missing
separators. The diagnostic test that previously rejected `unless`/`for` now
checks still-deferred imports, `continue`, and `break`; the new suite verifies the
supported constructs execute instead of disappearing. No deferred-feature
assertions are disabled or weakened.

All **206 focused tests pass**. Serial/parallel suites both report **291/299
passes**, the same eight failures, no test-process crashes/skips, and 30-second
timeouts. Shared verification/lowering is next under SPEC-018; the in-process
JIT and CLI remain SPEC-019/SPEC-020.


## Verified IR and shared lowering (SPEC-018)

All 15 `LoweringTest` cases pass. They verify high-level versus LLVM output,
real LLVM export and LLVM verification, all scalar signatures and internal i128
overflow calculations, nested calls/branches/loops and mutable storage, raw
SCF/memref conversion, module ownership, preserved locations, invalid IR,
unsupported custom operations/types, unknown operations, nested modules,
unresolved casts, illegal types in metadata, and conversion errors. Null input
and existing diagnostics cannot become successful output. Earlier source errors
keep their original stage when LLVM output is requested.

Six external executions run the new lowering fixtures: return-42, nested source
control flow (42), internal SCF/memref operations (42), and three independent
compilations/executions of a loop returning -1. The repeated compilations produce
identical printed LLVM-dialect IR on the supported toolchain. This is fixture
repeatability evidence, not a cross-platform or native-binary reproducibility
claim. The memref fixture verifies LLVM's legitimate index-typed constant
attributes while runtime index types remain rejected at the final boundary.

Every existing language execution test now uses the same production pipeline,
including integer/float arithmetic traps, numeric bit probes, and the full E2E,
control-flow, and unless/for suites. Existing assertions and all eight known
failures remain visible. The legacy JIT also lowers a clone through the shared
pipeline; translation registration and invocation still need SPEC-019.

All **229 focused tests pass**. Full serial/parallel suites report **306/314
passes**, the same eight known failures, no test-process crashes/skips, and
unchanged 30-second timeouts. [The pipeline contract](../docs/lowering.md)
describes diagnostics and ownership boundaries.


## In-process JIT execution (SPEC-019)

All 16 `JitRunnerTest` cases pass. The original return-42 smoke assertion is
preserved and now passes. Fifteen added cases cover calls, branches, while/for/
unless, recursion, short-circuiting, delayed initialization, all scalar call
signatures, private/already-lowered entry points, every significant i32 boundary,
repeated execution, independent engine state, wrapper-name collisions, missing
entries, invalid signatures/calling conventions/linkage, external dependencies,
verification/lowering failures, located quiet diagnostics, and concurrent contexts.

There are **25 successful in-process JIT invocations and five intentional trap
executions** in this suite. Zero, -1, i32 minimum, and i32 maximum remain valid
results. Three runs of the same module preserve its printed IR; three runs of a
test-only LLVM counter global each return 1, proving engines do not retain prior
state. Separate contexts return 17 and 93 concurrently. Test IR globals and
adapter-collision fixtures do not add source-language globals or an FFI.

The integer trap fixtures cover zero division, signed addition overflow, and
signed-minimum remainder by -1. Floating fixtures cover division by negative zero
and finite overflow. These run the real in-process JIT in thread-safe death-test
subprocesses and require SIGTRAP or SIGILL. Compiler/setup failures exit normally
with distinct fixture statuses and cannot satisfy the trap predicate. The parent
test processes do not crash. In-process signal recovery remains outside the
release contract; runtime traps never return a program value.

All **245 focused tests pass**. Full serial/parallel runs report **322/329 passes**,
with the JIT failure resolved and the same seven deferred-language failures still
visible. There are no skipped tests or unexpected test-process crashes; timeouts
remain 30 seconds. No deferred-feature assertion was removed or weakened.
[The JIT contract](../docs/jit.md) documents result/error separation, ownership,
ABI checks, and process-terminating arithmetic failures. The next task is SPEC-020.

## File-reading CLI (SPEC-020)

All 16 `CliTest` cases pass. They launch the built `gloinc` executable directly
through LLVM's process API, with literal argument vectors, captured stdout/stderr,
isolated temporary files, and a 10-second child timeout. CTest retains its
30-second timeout. Building `gloinc_test` also builds the CLI.

Coverage includes default/explicit runs with different input files, the repository
`core_counter.gloin` example returning 42, i32 limits and negative/zero results,
standalone help/version, rejected usage, missing/unreadable/non-regular files,
symlinks, quoted/option-like paths, complete-byte reads, invalid UTF-8, source-located
compiler errors, invalid entry signatures, helper-only modules, repeated runs,
and independent concurrent processes. Check mode succeeds silently for trapping
and non-terminating programs, establishing that it does not execute them.

Both inspection outputs are reparsed and verified; LLVM inspection contains only
LLVM operations beneath the module. Inspection of trapping source succeeds
without executing it. Two runtime trap cases require SIGTRAP/SIGILL-style process
termination with no result, so ordinary compilation errors and timeouts cannot
pass as arithmetic traps. All successful results use exit 0; reported errors and
usage use 1 and 2 respectively. No existing assertion was removed or weakened.

All **261 focused tests pass**. Full serial/parallel runs report **338/345 passes**,
with the same seven deferred-language failures, no skipped tests or unexpected
test-process crashes, and matching maintained/discovered inventories.
[The CLI contract](../docs/cli.md) documents the interface and version `0.0.1-dev`.
The broader source-file acceptance suite remains SPEC-021.

## Executable core acceptance (SPEC-021)

`CoreAcceptanceTest` adds 125 maintained cases backed by files under
[`fixtures/core`](fixtures/core/README.md). The feature matrix there maps every
advertised scalar type and supported operator, canonical core examples, invalid
fragments, and deferred feature families to source files.

The 28 successful programs pass checking and execute three times each, asserting
exact stdout, empty stderr, and exit 0. The 83 expected-error files are rejected
in all four CLI modes with exit 1, empty stdout, an expected diagnostic message,
and filename/line/column. Diagnostic text is matched separately from filenames.
Many negative programs contain an earlier runtime trap to detect execution before
validation. Fourteen runtime-error files pass checking and require signal
termination without output when executed. Each child has a 10-second timeout;
CTest retains 30-second case timeouts and no disabled/expected-failure tests.

The shared [`CliFixture`](support/cli_fixture.h) preserves the existing CLI suite's
process isolation, argument vectors, captured output, and launch-failure checks.
CMake checks that every `.gloin` acceptance fixture is named in the maintained
test source. The inventory remains explicit `TEST`/`TEST_F` definitions.

From a fresh Debug build, all **161 focused cases pass** (125 acceptance, 16 CLI,
16 JIT, four dialect setup). Full serial and parallel runs report **463/470
passes**, with the same seven deferred-feature failures and no unexpected
test-process crashes or skips. Both README source examples also print 42 with
exit 0. The full suite remains separate from the focused acceptance result.

CI adds a named **Verify executable core acceptance** step after its fresh build,
uses `--no-tests=error`, and uploads `core.xml`/`core.log` alongside the unfiltered
serial/parallel reports. Its tests-disabled build also runs the repository counter
example. The full suite runs even if the core step fails, preserving both results.
Core acceptance is the working-compiler milestone; SPEC-046 remains the release gate.

[Hosted run 35736970025](https://github.com/kubabialy/gloinc/actions/runs/35736970025)
passes both fresh builds and all 161 focused checks. Downloaded JUnit reports
confirm the same 463/470 full-suite results and exact seven failures in both
serial and parallel runs. No tests are skipped; no unexpected test-process
crashes occur. The full-suite step retains its failing status.

## Scalar release validation (SPEC-046)

`cmake --build build --target check-core` builds the tests and runs all **424
required scalar-core cases**, emitting `build/core.xml`. The suites are Lexer,
Diagnostics, CheckedProgram, Parser, Scope, Variables, Numeric, Functions,
Operators, ControlFlow, ForUnless, Lowering, JitRunner, Cli, CoreAcceptance, E2E,
ExternalRunner, and MLIRSetup. Legacy later-feature tests remain in the unfiltered
470-case suite. This explicit release gate neither disables them nor marks their
failures expected successes.

Fresh local Release and ASan/UBSan Debug builds both pass **424/424 core tests**.
The full Release serial and parallel suites each report **463/470 passes** with
the unchanged seven deferred-feature failures. Instrumentation covers project
C++ compiler/harness code; prebuilt LLVM/MLIR and JIT-generated machine code are
not instrumented. LeakSanitizer is not claimed on macOS. See the exact build
commands and runtime options in [the release guide](../docs/release.md).

`scripts/check-package.sh` runs all **141 CLI/source tests** twice: once against
an installed compiler, then against the extracted archive in another prefix with
spaces. `GLOIN_TEST_CLI` selects the executable; `GLOIN_TEST_FIXTURES` selects its
installed fixture directory. Normal tests use generated build/source paths.
Each package check verifies all source fixtures, exact version/help/results,
diagnostics, paths, inspection, and runtime traps through the same assertions.
The script also checks the archive checksum, packaged counter example, and shared
dependencies; reports are retained for review. No new test definitions or weakened
assertions are needed to validate the distribution copies.
