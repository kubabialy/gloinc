# Test inventory and harness

The maintained suite is the `TEST`/`TEST_F` definitions in `tests/*_test.cpp`,
registered through `gloinc_test` and CTest. CMake checks that every file matching
that pattern is in the explicit target source list and fails configuration if a
suite is omitted. Support programs under `tests/support` are harness fixtures,
not additional test cases.

At SPEC-008, source definitions and CTest discovery both contain **145 tests**:

| Suite | Tests |
| --- | ---: |
| LexerTest | 34 |
| DiagnosticsTest | 18 |
| ParserTest | 29 |
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
| E2ETest | 5 |
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
as expected successes. Serial and parallel runs at SPEC-008 both produce
**137 passes, 8 failures, no crashes or skipped tests**, with identical failing test names.

## Lexical contract checks

All 34 lexer tests pass, including the seven failures from the original audit.
They cover reserved words versus identifier prefixes, operators adjacent to operands,
LF/CRLF and comment positions, repeated EOF, malformed numbers and quoted literals,
escape spelling, Unicode scalars, invalid UTF-8 in tokens and comments, BOM/NUL,
unsupported trivia, and progress with bounded spans for every byte value.
The literal grammar and reserved-token policy are defined in
[SPEC.md](../SPEC.md#lexical-literal-forms-spec-008). Token recognition does not
establish parsing, type support, numeric conversion, or string execution.

## Source diagnostics and pipeline checks

The 18 diagnostic regressions cover owned token/AST source spans, CRLF locations,
malformed/unterminated input, partial-AST rejection, semantic failure propagation,
non-boolean conditions, unsupported nodes, codegen failure/module disposal,
void-call value checking, explicit rendering, generated MLIR locations, reserved
syntax rejection, encoding errors in trailing comments, and type/literal separation.
[The diagnostic API](../docs/diagnostics.md) documents the stage contracts.
E2E tests use `compile_source` so a failed stage cannot reach external execution.
Stage-isolated codegen tests check parser/module status before inspecting IR;
they continue to expose incomplete features rather than hiding errors.

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
| `AsyncTest.SpawnGeneration`, `SemaAsyncTest.AsyncTypes` | SPEC-009, SPEC-040, SPEC-041 |
| `AsyncTest.DeferredFunctionGeneration` | SPEC-009/SPEC-040: fixture uses obsolete `let` and an omitted return annotation; parser errors were previously ignored. |
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
