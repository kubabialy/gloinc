# Test inventory and harness

The maintained suite is the `TEST`/`TEST_F` definitions in `tests/*_test.cpp`,
registered through `gloinc_test` and CTest. CMake checks that every file matching
that pattern is in the explicit target source list and fails configuration if a
suite is omitted. Support programs under `tests/support` are harness fixtures,
not additional test cases.

At SPEC-018, maintained source definitions and CTest discovery both contain **314 tests**:

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
| ArenaTest | 1 |
| AsyncTest | 2 |
| ArrayStringTest | 2 |
| UnlessTest | 1 |
| JitRunnerTest | 1 |
| E2ETest | 29 |
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
their output is not build or language-coverage evidence. Maintained loop tests now cover SPEC-017; imports
remain tracked in SPEC-023/SPEC-029/SPEC-030, and the status documents were corrected in SPEC-005. Example `.gloin` files are also not automated tests.

## Running and inspecting tests

After a tests-enabled build (see [toolchain.md](../docs/toolchain.md)):

```sh
ctest --test-dir build --show-only=json-v1
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
ctest --test-dir build -R '^(E2ETest|ExternalRunnerTest)' -j 4 --output-on-failure
```

All CTest cases have a 30-second timeout. No known failures are disabled or marked
as expected successes. Serial and parallel runs at SPEC-018 both produce
**306 passes, 8 failures, no crashes or skipped tests**, with identical failing test names.

## Lexical contract checks

All 34 lexer tests pass, including the seven failures from the original audit.
They cover reserved words versus identifier prefixes, operators adjacent to operands,
LF/CRLF and comment positions, repeated EOF, malformed numbers and quoted literals,
escape spelling, Unicode scalars, invalid UTF-8 in tokens and comments, BOM/NUL,
unsupported trivia, and progress with bounded spans for every byte value.
The literal grammar and reserved-token policy are defined in
[SPEC.md](../SPEC.md#lexical-literal-forms-spec-008). Token recognition does not
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
| `ArenaTest.ArenaAllocation` | SPEC-028: `Arena::new` is unresolved; codegen previously emitted an unchecked call. |
| `ArrayStringTest.HandlesStringLiterals` | SPEC-022 |
| `ArrayStringTest.HandlesArrayLiterals` | SPEC-035: `[i32; 3]` type resolution is unsupported; a null type was previously tolerated. |
| `JitRunnerTest.SmokeTest` | SPEC-019: missing builtin LLVM translation interface |

SPEC-007's newline handling made
`LexerTest.HandlesComments`, `LexerTest.TrackLineNumbers`,
`ParserTest.ParseStructDefinition`, and `ParserTest.ParsePackedStruct` pass.
Three formerly passing cases now expose failures: arena construction, the invalid
async fixture, and array type resolution. None were disabled or converted into
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
exact trace **123423422** proves initializer-once, condition-before-body,
body-before-update, final false-condition checking, and single evaluation of a
skipped `unless` body condition. Eight other programs return 42.

Parser tests now accept omitted header components and keep rejecting missing
separators. The diagnostic test that previously rejected `unless`/`for` now
checks still-deferred imports, `defer`, and `break`; the new suite verifies the
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
