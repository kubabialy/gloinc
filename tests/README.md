# Test inventory and harness

The maintained suite is the `TEST`/`TEST_F` definitions in `tests/*_test.cpp`,
registered through `gloinc_test` and CTest. CMake checks that every file matching
that pattern is in the explicit target source list and fails configuration if a
suite is omitted. Support programs under `tests/support` are harness fixtures,
not additional test cases.

At SPEC-012, maintained source definitions and CTest discovery both contain **222 tests**:

| Suite | Tests |
| --- | ---: |
| LexerTest | 34 |
| DiagnosticsTest | 20 |
| CheckedProgramTest | 13 |
| ParserTest | 49 |
| ScopeTest | 11 |
| VariablesTest | 17 |
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
| E2ETest | 19 |
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
their output is not build or language-coverage evidence. Import and loop behavior
remain tracked in SPEC-017/SPEC-023/SPEC-029/SPEC-030, and the status documents were corrected in SPEC-005. Example `.gloin` files are also not automated tests.

## Running and inspecting tests

After a tests-enabled build (see [toolchain.md](../docs/toolchain.md)):

```sh
ctest --test-dir build --show-only=json-v1
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
ctest --test-dir build -R '^(E2ETest|ExternalRunnerTest)' -j 4 --output-on-failure
```

All CTest cases have a 30-second timeout. No known failures are disabled or marked
as expected successes. Serial and parallel runs at SPEC-012 both produce
**214 passes, 8 failures, no crashes or skipped tests**, with identical failing test names.

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
statement-only assignment, complete loop headers, malformed/truncated input,
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
and guards for unfinished operators and return checking. Compile-time assertions
prove that raw ASTs cannot call `CodeGen::generate` and clients cannot construct a
`CheckedProgram`. [The contract](../docs/checked-program.md) documents the limits.

Existing stage-isolated backend tests now use the explicit
`generate_unchecked_for_testing` entry; their feature assertions and failures are
preserved. All nineteen E2E tests use checked generation. The seventh executes an
`int` alias and nested shadowing, confirming the outer binding still returns 42.

## External execution

CMake discovers `mlir-opt` and `mlir-runner` in the selected LLVM installation's
tools directory. `GLOIN_MLIR_OPT` and `GLOIN_MLIR_RUNNER` cache paths permit explicit
overrides. Missing tools fail configuration when tests are enabled; runtime
launch failures are test errors. Tests-disabled builds do not discover these
programs or create the fixture executable.

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
