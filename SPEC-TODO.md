# Gloin specification and compiler checklist

This is the implementation backlog for [SPEC.md](SPEC.md), based on the architecture audit of `mlir` at `8e25383` on 2026-09-07. Work through the numbered items in order. Each item has a stable ID so we can discuss, implement, and verify it separately.

**Next item: SPEC-017.** Completed items have verification evidence in the completion log. Existing partial implementations and results from temporary audit repairs do not count as completed work.

The first milestone is a reproducible build. SPEC-006 selects the first release as the scalar core with an in-process JIT on Apple Silicon macOS. SPEC-021 is its executable acceptance milestone; SPEC-046 remains the packaging/release gate. SPEC-022 through SPEC-045 and SPEC-013b are deferred from that release, with explicit unsupported-feature diagnostics required in the core. Their implementation work remains open.

## How to use this checklist

- Mark an item complete only when its acceptance criteria are met in the repository, relevant checks pass, and its evidence is recorded in the completion log.
- For a language feature, check syntax, semantic validation, generated IR, and observable execution where applicable. Include invalid-source cases. A token, AST node, non-null module, or printed operation name alone is insufficient.
- Unsupported syntax, types, and operations must produce diagnostics. Never silently omit them or substitute an unrelated type.
- Resolve language decisions in SPEC.md before implementing the affected behavior. This checklist does not silently change the language specification.
- Keep known failures visible and associate them with task IDs. Do not disable tests or weaken assertions just to report a green suite. Obsolete tests should be corrected against an explicit spec decision.
- If an item proves too large, split it into suffix IDs such as `SPEC-027a`; retain the parent ID and its acceptance criteria.
- For the selected first release, work through SPEC-021 and then the SPEC-046 release gate. Explicitly deferred items remain unchecked and resume afterward in checklist order; deferral never counts as implementation.

## Audited baseline

| Check | Observed result |
| --- | --- |
| Fresh CMake configuration with cached GoogleTest source | Succeeds |
| Build of the checked-in source | Fails on declaration/AST drift and MLIR API incompatibilities |
| Unmodified lexer and semantic tests built independently | 21 of 28 pass; seven lexer tests fail |
| Temporary declaration/API repairs with original linkage | 63 of 99 configured tests pass; 26 tests crash |
| Temporary repairs plus consistent LLVM/MLIR linkage | 86 of 99 pass, including five external E2E tests; JIT smoke still fails |
| Compiler CLI | Tokenizes a hardcoded string; ignores input files |
| Test inventory | 103 test definitions; four generic tests are omitted from CMake |

These measurements used Apple Silicon, AppleClang 16, LLVM/MLIR 21.1.6, and CMake 4.2.1. They do not establish another platform's status. Test pass counts are not specification coverage percentages. The temporary repairs were not applied to the repository.

## 1. Restore a reproducible build

- [x] **SPEC-001 — Synchronize declarations and implementations.**
  Reconcile `parse_generic_params`, function/struct generic parameters, codegen symbol/function/template/defer state, and the `declare` signature. Remove duplicate import-handler declarations. Start in [parser.h](src/parser.h), [AST.h](src/AST.h), and [codegen.h](src/codegen.h).
  **Done when:** parser/AST interfaces compile together and no missing member, mismatched declaration, or duplicate declaration errors remain. Record any remaining toolchain errors under SPEC-002.

- [x] **SPEC-002 — Establish a supported LLVM/MLIR toolchain and consistent linkage.**
  Select and document a tested LLVM/MLIR version, repair API incompatibilities, and use a consistent component or shared-library strategy. Investigate imported transitive dependencies rather than mixing overlapping archives and shared implementations.
  **Done when:** a fresh build produces both executables and loading all used dialects no longer crashes. The version requirement is enforced or unsupported versions receive an actionable configure error. JIT execution is completed in SPEC-019.
  **Resolved:** the two removed `mlir::Type::isa` calls and the `LLVM::IntToPtrOp` builder mismatch identified after SPEC-001. Both executables now build against shared LLVM/MLIR 21.1.6 without component archives or duplicate-library warnings. All four dialect setup tests pass. See [toolchain requirements](docs/toolchain.md) and the completion log; the full suite still has 13 language/JIT failures.

- [x] **SPEC-003 — Correct the CMake target and dependency structure.**
  Share frontend/backend implementation through reusable targets; make the CLI link the compiler implementation. Pin GoogleTest to an immutable revision or checked archive, honor `BUILD_TESTING`, use target-scoped settings, require the selected C++ standard, and remove platform-specific standard-library assumptions. Keep [Makefile](Makefile) consistent with [CMakeLists.txt](CMakeLists.txt).
  **Done when:** clean builds work with tests enabled and disabled; disabling tests avoids fetching GoogleTest; TableGen outputs are generated in the build tree; explicit LLVM/MLIR package paths work.
  **Verified:** reusable frontend/backend targets serve both executables; C++23 and LLVM usage requirements are target-scoped. Fresh builds with `BUILD_TESTING=ON/OFF` pass using explicit package paths. GoogleTest 1.16.0 is pinned by archive checksum and only fetched with tests enabled. The Makefile delegates to CMake/CTest; full-suite failures remain visible.

- [x] **SPEC-004 — Establish the complete test inventory and trustworthy harnesses.**
  Include the four [generic tests](tests/codegen_generics_test.cpp). Integrate the lit test or replace it with an equivalent maintained test. Give subprocess tests isolated temporary files, discovered tool paths, timeouts, and checked exit status. Classify failures by task ID.
  **Done when:** test discovery matches the maintained inventory, serial and parallel runs do not collide on `temp.mlir`, and tool failures cannot be interpreted as successful program output. Known language failures remain explicitly reported until their tasks are completed.
  **Verified:** all 112 maintained cases are discovered, including the four generic tests and eight harness regressions. The orphaned lit checks are now assertions in `CodeGenTest.GenerateSpawn`, exposing a missing spawn operation tracked under SPEC-040/SPEC-041. Serial and parallel runs both produce 98 passes and the same 14 failures; see [tests/README.md](tests/README.md) for the inventory, harness contract, and failure mapping.

- [x] **SPEC-005 — Add clean-build CI and accurate onboarding/status documents.**
  Document dependencies and commands in a root README, add CI for the selected development platform(s), and correct [example status claims](examples/README.md), [phase notes](examples/PHASE2_PROGRESS.md), and [OpenCode.md](OpenCode.md).
  **Done when:** CI builds from an empty directory and publishes the complete test results; documentation distinguishes working, partial, and unsupported features and contains no unsupported production-readiness or coverage claims. A failing full suite remains visibly failing.
  **Verified:** [hosted CI](https://github.com/kubabialy/gloinc/actions/runs/34242653935) installs LLVM/MLIR 21.1.6 with its matching Z3 dependency and builds from empty directories with tests enabled and disabled. Published serial/parallel reports both contain all 112 tests, with 98 passes, the same 14 known failures, and no crashes or skipped tests. The workflow remains red for those failures. The root README, toolchain guide, example notes, and OpenCode.md now state the verified capabilities and remaining limitations.

## 2. Define the core contract and repair the frontend

- [x] **SPEC-006 — Resolve core syntax ambiguities and choose the first release boundary.**
  Reconcile mandatory `def` with `const`, explicit type requirements, `int`/`usize` aliases, `string` versus `String`, visibility placement, statement terminators, and UTF-8 identifier rules. State the supported initial types/features/platforms and whether that release requires JIT, native compilation, or both. Record later decisions under their feature tasks.
  **Done when:** SPEC.md provides canonical examples and an explicit first release feature list. Implementation-only syntax is either documented as an extension or scheduled for rejection; deferred work stays on this checklist.
  **Decisions:** [the core contract](SPEC.md#first-release-contract-spec-006) selects a JIT-only scalar release on Apple Silicon macOS. Declarations use `def`, constants use `def const`, visibility immediately follows `def`, annotations and statement semicolons are mandatory, identifiers are ASCII in valid UTF-8 source, `int` aliases `i32`, `usize` aliases the target pointer-width unsigned type, and only `string` is the built-in text spelling. Implementation and negative acceptance cases remain in the tasks below; the decision itself does not repair their tests.

- [x] **SPEC-007 — Introduce source-aware diagnostics and stop on errors.**
  Preserve source file/span information through tokens and AST nodes. Give parsing, semantic checking, and code generation explicit success/failure results. Route every error through the same diagnostic mechanism; unsupported AST nodes must fail explicitly.
  **Done when:** malformed input, unknown constructs, and non-boolean conditions report a useful location, set failure status, and prevent later compilation stages. No error is only printed to stderr while compilation reports success.
  **Verified:** tokens and AST nodes own source spans; shared diagnostics render file/line/byte-column locations. Checked parsing discards partial programs, semantic checking returns failure for every reported error, and codegen discards failed modules. `compile_source` gates the three stages; E2E tests use it. All 15 diagnostic regressions pass. Grammar/type completeness and lowering/JIT/CLI integration remain their subsequent tasks; see [the API contract](docs/diagnostics.md).

- [x] **SPEC-008 — Repair lexer behavior against the agreed vocabulary.**
  Fix newline/comment handling, multi-character operators, and keyword recognition. Resolve existing `in`, range, `=>`, `spawn`, and `await` test expectations against the spec/extension decisions. Validate malformed numeric/string/character tokens and preserve accurate source positions.
  **Done when:** supported vocabulary has correct tokens and positions; unsupported vocabulary has deliberate behavior; the seven audited lexer failures are resolved without silently accepting obsolete syntax.
  **SPEC-006 contract:** validate UTF-8 without locale-dependent identifier classification; reject BOM/NUL/non-ASCII identifiers, reserve the documented later/legacy vocabulary, and treat LF/CRLF as trivia with correct line accounting. Newline tokens in lexer tests may remain internal trivia; neither comments nor newlines imply semicolons. Reserved `in`, `..`, and `=>` must not swallow adjacent tokens, even though their syntax is unsupported in the core.
  **Verified:** all 34 lexer tests pass, including all seven originally audited failures. The scanner validates the entire UTF-8 source, malformed literal spellings, operator boundaries, and byte positions. Quoted literals have separate tokens from type keywords; reserved `spawn`/`await` expressions fail explicitly in parsing. All 18 diagnostic tests pass. Full serial/parallel suites agree on 137/145 passes and eight remaining failures; sanitizer probes find no memory/undefined-behavior errors. Literal spelling rules are recorded in [SPEC.md](SPEC.md#lexical-literal-forms-spec-008); grammar, numeric conversion, and string execution remain their later tasks.

- [x] **SPEC-009 — Make parsing complete and deterministic.**
  Standardize token consumption, require closing delimiters, and make error recovery always advance. Fix identifier conditions such as `if b { ... }`, operator precedence, and multiline struct/function parsing. Cover [parser.cpp](src/parser.cpp) with complete-program cases.
  **Done when:** ordinary multiline programs parse without diagnostics; truncated blocks/calls and malformed expressions fail; `if b` is not parsed as a struct literal; parser errors cannot leave a supposedly successful partial program.
  **SPEC-006 contract:** enforce canonical modifier order, explicit annotations, required statement semicolons, comma-separated lists, and the statement-only assignment rule. Reject bare `const`, `pub def`, `fn`, omitted return types, and other unsupported syntax listed in SPEC.md; preserve later feature implementation tasks rather than counting a rejection as implementation.
  **Verified:** every parser consumes its complete construct, including multiline expressions and calls. Identifier conditions cannot consume body braces; precedence no longer depends on capitalization. Core mode enforces annotations, modifiers, scopes, delimiters, and statement-only assignment. Parsing stops on its first error and discards the whole program; obsolete recovery loops are removed and excessive nesting fails explicitly. All 49 parser, 20 diagnostic, and six E2E tests pass. Fresh Debug build succeeds; serial/parallel suites agree on 160/168 passes with the same eight remaining failures. Constants/visibility retain AST metadata; constant evaluation remains explicitly unsupported pending SPEC-012. [The parser contract](docs/parser.md) documents core versus syntax-only stage tests and the first-error policy.

- [x] **SPEC-010 — Establish one resolved type/symbol contract for codegen.**
  Choose a typed AST, semantic side tables, or another explicit checked-program representation. Resolve types and declarations once and make codegen consume that information. Define ownership of AST, semantic data, MLIR context, and modules.
  **Done when:** semantic checking and codegen agree on primitive types and symbol identities, unknown types never default to i32, and codegen cannot accidentally process an unchecked program through the normal compiler path.
  **Verified:** Sema exclusively constructs an owned `CheckedProgram` containing the AST, canonical core types, declaration IDs, and reference bindings. Normal `CodeGen::generate` requires that object and returns an owned module; raw AST experiments use `generate_unchecked_for_testing`. Signatures, storage, references, and calls consume semantic data, with guards against legacy type/name resolution. Aliases resolve in Sema, unknown/deferred annotations fail, and signedness remains distinct from MLIR storage. Ownership and remaining semantic limits are documented in [docs/checked-program.md](docs/checked-program.md). All 13 contract tests and seven E2E tests pass; serial/parallel suites both report 174/182 passes and the same eight known failures. Declaration collection, constants, contextual literals, and full return/operator semantics remain SPEC-011 through SPEC-015.

## 3. Make core language semantics and code generation reliable

- [x] **SPEC-011 — Resolve declarations and scopes before checking bodies.**
  Collect function/type declarations before use, define duplicate/shadowing rules, and check references with lexical scope. Initialize all symbol properties, including parameter mutability.
  **Done when:** forward calls and recursion work under the chosen rules; duplicate declarations and unresolved names fail consistently; bindings do not leak across scopes or compiler invocations.
  **SPEC-006 scope:** core types come from the predefined registry; user-defined type collection stays deferred with SPEC-024/SPEC-031 through SPEC-034. Constants remain SPEC-012 and `for` initializer scope remains SPEC-017.
  **Verified:** Sema collects core function signatures before bodies and codegen declares MLIR functions before emitting bodies. [The scope contract](SPEC.md#declaration-visibility-and-lexical-scopes-spec-011) defines forward/mutual recursion, duplicate rejection, nearest lexical lookup, initializer visibility, and immutable parameters. All 11 scope tests, 13 checked-program tests, and 12 E2E tests pass. Serial/parallel suites agree on 190/198 passes with the unchanged eight known failures. A small nested-branch continuation repair supports the recursion regression; full control-flow work remains SPEC-016.

- [x] **SPEC-012 — Enforce variables, constants, mutability, and initialization.**
  Require declared types, require constant initializers, validate assignment targets, and enforce mutability. Add definite-initialization checks and reject invalid initializer/store types before lowering. Apply aggregate and pointer rules as those features arrive.
  **Done when:** `1 = 2`, immutable assignment, uninitialized reads, and `def mut n: i8 = 3.14` fail before codegen; valid mutable assignments execute correctly. Constants behave according to the agreed compile-time rules.
  **Verified:** [SPEC.md](SPEC.md#variables-constants-and-initialization-spec-012) defines typed stores, delayed immutable initialization, branch/loop rules, and pure constant expressions. Sema checks initialization by declaration ID and evaluates constants before codegen, with overflow/zero-divisor checks and short-circuiting. Constants have no runtime storage; delayed locals use resolved storage types. All 17 variable tests and 19 E2E tests pass. Serial/parallel suites agree on 214/222 passes and the same eight known failures. Contextual constant/literal types remain SPEC-013; the current constant types are i32/f32/bool. A loop-backedge prerequisite repair supports initialization inside nested branches; full control flow remains SPEC-016.

- [x] **SPEC-013 — Implement typed numeric literals and conversions.**
  Parse decimal/hexadecimal/binary integers with full-consumption and range checks. Support the chosen signed/unsigned widths and floating types without forcing every literal to i32/f32. Define overflow, narrowing, explicit casts, and literal compatibility in SPEC.md.
  **Done when:** `0x2A` and `0b101010` mean 42; oversized literals fail rather than becoming zero; integer and float boundary tests agree across Sema and codegen; allocation and store widths match.
  **SPEC-006 scope:** signed/unsigned 8/16/32/64-bit integers and f32/f64, with exact `int = i32` and target-width `usize` aliases. Define contextual literal typing without inferring declaration types. The 128-bit extension is explicitly deferred under SPEC-013b; unsupported types cannot fall back to i32.
  **Verified:** [The numeric contract](SPEC.md#numeric-literals-and-conversions-spec-013) defines contextual typing, exact-width ranges, direct IEEE rounding, defaults, and rejection of typed conversions/casts. Parsing retains numeric spellings; Sema resolves literals and constants to APInt/APFloat values consumed directly by codegen. All 12 numeric tests, 24 E2E cases, and eight float-bit execution probes pass. Serial/parallel suites agree on 231/239 passes with the same eight known failures. Runtime arithmetic semantics and complete return checking remain SPEC-014/SPEC-015; the first-release feature list is unchanged.

- [x] **SPEC-014 — Validate functions, calls, returns, and entry points.**
  Check argument count/types, parameter rules, return values, all reachable return paths, and `main`'s supported signature. Define implicit void return and unreachable-source behavior.
  **Done when:** incorrect/missing returns, top-level returns, invalid calls, and invalid entry points fail; direct/forward/recursive calls return correct values; no unchecked return signature reaches execution.
  **Verified:** [The function contract](SPEC.md#functions-calls-returns-and-entry-points-spec-014) defines exact call/return types, conservative return paths, implicit void returns, unreachable-source rejection, and module/executable entry rules. Sema rejects invalid returns and entry signatures before codegen; executable mode requires `main() -> i32`. All 12 function tests and 29 E2E cases pass. Serial/parallel suites agree on 248/256 passes with the same eight known failures. General operators, control-flow lowering, and execution/CLI integration remain SPEC-015 through SPEC-020.

- [x] **SPEC-015 — Implement the supported expression operators.**
  Align lexer/parser/Sema/codegen operator tables. Implement unary minus/not, arithmetic, comparisons, and boolean short-circuiting; choose signed, unsigned, and floating operations from resolved types. Specify division-by-zero, overflow, shifts, and evaluation order for the supported operator set.
  **Done when:** negation and inequality do not crash; float addition uses floating arithmetic; unsigned comparisons/division are correct; short-circuit tests prove that skipped operands have no side effects. Unsupported operators fail explicitly.
  **SPEC-006 scope:** implement the operator list in SPEC.md, including integer `%`; reject compound assignment, bitwise/shift operators, unary `+`, floating remainder, and assignment expressions for this release. Define precedence, evaluation order, and numeric failure behavior before adding acceptance cases.
  **Verified:** [The operator contract](SPEC.md#expression-operators-and-arithmetic-failure-spec-015) defines exact operand types, left-to-right evaluation, short-circuiting, checked arithmetic, and runtime traps. Shared semantic rules cover constants/runtime expressions; codegen selects signed/unsigned/float operations and guards failures. All 16 operator tests and 130 focused tests pass, including 103 operator executions (37 results and 66 expected traps). Serial/parallel suites agree on 264/272 passes with the same eight known failures. General control flow and lowering/JIT/CLI remain SPEC-016 through SPEC-020.

- [x] **SPEC-016 — Correct nested if/while control flow.**
  Require boolean conditions and track the current insertion block after recursive generation. Stop emitting into terminated blocks and correctly terminate merge blocks and loop backedges.
  **Done when:** nested branches, loops containing branches, early returns, and empty bodies produce valid IR and correct execution without missing terminators or accidental fallthrough. Unreachable source remains rejected according to SPEC-014.
  **Verified:** [The branch/loop contract](SPEC.md#branches-and-while-loops-spec-016) defines boolean conditions, selected-arm execution, condition reevaluation, and early returns. Codegen explicitly tracks live continuations, creates merge/backedges only from continuing paths, and rejects statements after termination in raw stage tests. All 12 control-flow tests and 142 focused tests pass, including nine verified external executions. Serial/parallel suites agree on 276/284 passes with the same eight known failures. `unless`/`for` and lowering/JIT/CLI remain SPEC-017 through SPEC-020.

- [ ] **SPEC-017 — Implement unless and C-style for loops.**
  Add semantic checking and lowering for both existing AST nodes. Define loop-variable scope and supported omitted components. Preserve the documented function-exit meaning of defer when defer support is completed.
  **Done when:** a three-iteration counter returns 3; `unless false` executes its body; invalid body references/conditions fail; nested loops and early returns work. Neither construct can silently disappear.

## 4. Connect the actual compiler pipeline

- [ ] **SPEC-018 — Verify IR and share one lowering pipeline.**
  Define the supported intermediate dialects and final legal operations/types. Provide a shared pipeline for all execution/output modes, including control-flow conversion. Reject custom operations without an implemented lowering and use source locations in diagnostics.
  **Done when:** generated modules are verified before execution and after appropriate conversions; no unsupported Gloin operation or unrealized conversion remains at LLVM export; failures propagate cleanly and module ownership is explicit.

- [ ] **SPEC-019 — Repair the in-process JIT.**
  Register required translation interfaces, including the builtin dialect for the selected MLIR version, use SPEC-018's pipeline, and invoke a validated entry-point ABI. Separate compiler/runtime failure from a program's returned value.
  **Done when:** return-42, function-call, branch, and loop programs execute through JitRunner; a legitimate return of -1 is distinguishable internally from execution failure; missing symbols or invalid signatures produce diagnostics instead of crashes.

- [ ] **SPEC-020 — Replace the lexer demo with a real CLI.**
  Load source files and route them through parsing, checking, codegen, lowering, and execution. Document the command interface and provide checking and IR inspection modes useful for development. Remove hardcoded input and unconditional debug output.
  **Done when:** a file passed to `gloinc` controls the result; missing/unreadable files and invalid source fail with useful diagnostics; help/version and exit behavior are documented; CLI and tests use the same compiler stages.

- [ ] **SPEC-021 — Establish the executable core acceptance suite.**
  Convert the core audit reproductions into repository fixtures driven through the CLI. Cover decimal/hex/binary values, calls, mutation, boolean operators, nested control flow, for/unless, and invalid-source diagnostics.
  **Done when:** the supported core examples execute with asserted values/output, negative cases fail before execution, and both targeted tests and the corresponding CI checks pass from a fresh build. This is the first working compiler milestone, not full specification completion.
  **SPEC-006 scope:** turn the complete core examples and invalid fragments in SPEC.md into source-file fixtures, cover every advertised scalar type/operator, and verify that unsupported later syntax fails before execution. The core has no standard output API; assert returned values and diagnostics. Keep later-feature failures visible in the full suite.

## 5. Finish data, memory, and basic modules

- [ ] **SPEC-022 — Implement the specified string representation and literals.**
  Define length units, UTF-8/escape handling, embedded NUL behavior, and storage ownership/lifetime. Implement pointer-plus-length strings, correctly sized global initializers, and repeated-literal reuse or unique symbols. Define slicing syntax and bounds/lifetime behavior if included in the selected scope.
  **Done when:** empty, escaped, non-ASCII, embedded-NUL, and repeated strings produce valid IR and preserve bytes/lengths; string use does not depend on an accidental terminator or dangling storage.

- [ ] **SPEC-023 — Implement minimal standard-module loading and output.**
  Resolve `import "@std"` to actual symbols and implement `std.print`/`std.println` with the documented string ABI. Reject missing standard members and unsupported module paths until their implementations are available.
  **Done when:** the specification's basic hello-world runs through the CLI and prints exactly `Hello World!` plus a newline. A missing module or member never succeeds through an empty stub.

- [ ] **SPEC-024 — Complete ordinary structs and target-correct layout.**
  Check field declarations, initializers, visibility, duplicate/missing/unknown fields, and field assignments. Use target data layout for size/alignment rather than adding field sizes or hardcoding pointer size.
  **Done when:** mixed-size structs have correct layout, valid literals/field access execute correctly, and invalid fields cannot become index zero or leave unintended undefined values. Packed bitfields are handled separately in SPEC-038/039.

- [ ] **SPEC-025 — Implement pointer/reference types and addressability.**
  Specify nullability, pointee mutability, conversions, lifetime responsibilities, and the boundary between current guarantees and future unsafe-block checks. Preserve pointee types through address-of, dereference, assignment, and calls.
  **Done when:** the spec's pointer example works; non-i32 pointees load/store with correct types; invalid references and writes through immutable storage are rejected according to the documented rules; allocation sizes match accesses.

- [ ] **SPEC-026 — Implement instance/static methods and access rules.**
  Lower instance methods with exactly one self pointer, support the chosen static-call syntax, and check method bodies and visibility. Define addressability and lifetime rules for temporary receivers.
  **Done when:** `person.greet()`, self-field access, and static construction work; signatures do not duplicate self; invalid methods and private access fail in Sema rather than crashing codegen.

- [ ] **SPEC-027 — Define and implement defer across runtime paths.**
  Keep the spec's function-exit, LIFO contract. Decide when call arguments are evaluated, how captured bindings remain valid, repeated registration in loops, return-value evaluation order, and cleanup on runtime failure. Implement registration only when execution reaches the defer.
  **Done when:** untaken branches register nothing; repeated registrations execute in reverse order; explicit/implicit/early returns run cleanup; exited lexical scopes retain the required captured values; nested control flow never duplicates or drops cleanup.

- [ ] **SPEC-028 — Implement the arena runtime and ABI.**
  Specify allocation, alignment, growth, reset/free behavior, failure behavior, and pointer invalidation. Implement and link the declared arena functions and expose a coherent language API.
  **Done when:** real arena programs allocate correctly aligned mixed-size objects, read/write them, and release/reset storage correctly. Runtime tests cover allocation failure and invalidation boundaries; meaningful sanitizer checks cover the native runtime.

- [ ] **SPEC-029 — Implement local modules and exported symbols.**
  Define module-relative paths, exported/private declarations, duplicate imports, cycles, and initialization rules. Load and check dependencies once and resolve qualified calls across files.
  **Done when:** the `utils.calculate` example works from a different working directory; missing files/symbols, illegal private access, and unsupported import cycles produce deterministic diagnostics.

- [ ] **SPEC-030 — Complete the documented basic standard-library functions.**
  Implement `std.input`, `std.to_int`, and `std.to_string`; define EOF, invalid input, overflow, formatting, allocation, and returned-string ownership. Reconcile examples that pass numeric values directly to printing functions.
  **Done when:** the standard-library examples run with asserted output and input fixtures; conversion/EOF failures follow the specified API; allocations have a defined cleanup path.

## 6. Resolve and implement the remaining type/syntax features

- [ ] **SPEC-031 — Specify generic type syntax and resolution.**
  Document the parameterized types required by `Deferred<T>`, `Spawn<T>`, and `Result<T, E>`, and decide whether arbitrary user-defined generics are in scope. Replace capitalization heuristics with a grammar that handles comparisons and nested type arguments.
  **Done when:** nested type applications resolve consistently; generic arity and unknown arguments are diagnosed; ordinary comparisons are not misparsed as type applications. Supported and deferred generic forms are explicit.

- [ ] **SPEC-032 — Complete supported generic struct specialization.**
  Implement type substitution through fields, pointers, nested applications, recursive references, and applicable methods. Cache specializations with stable identities. If user-defined generic structs are deferred by SPEC-031, reject them explicitly and retain their implementation work as deferred.
  **Done when:** each supported generic-struct fixture parses without diagnostics, verifies, and executes with correct field values/layout; all four previously omitted tests have a meaningful, spec-aligned disposition.

- [ ] **SPEC-033 — Resolve the generic-function extension.**
  Generic functions exist as implementation intent but are not fully specified. Either specify explicit type arguments, inference policy, specialization, and recursive calls and implement them, or reject this syntax for the selected release.
  **Done when:** `def identity<T>(...)` is handled deliberately rather than parsed as a variable; supported cases have execution tests. A deferral is recorded as deferred implementation, not as implemented generics.

- [ ] **SPEC-034 — Specify and implement enums and Result.**
  Define enum declarations, payloads, construction, inspection, layout, and exhaustiveness/access rules. Define `Result<T, E>` and the `is_ok`, `ok`, `err`, and value-extraction behavior used by the concurrency examples, including whether extraction involves an optional value type.
  **Done when:** success/error variants retain their payloads and can be handled safely; invalid extraction and type combinations follow documented behavior; no success/error representation is assumed implicitly by codegen.

- [ ] **SPEC-035 — Resolve arrays, indexing, and slicing.**
  Reconcile `[i32; 3]`, `u8[1024]`, and the intended slice behavior; arrays currently appear in tests/examples without a complete spec contract. Define element typing, bounds, mutability, layout, and lifetimes, then implement supported forms or explicitly defer them.
  **Done when:** supported array/index operations return and update correct elements; invalid sizes/types/indices are handled deliberately; indexing cannot return a null codegen value and crash.

- [ ] **SPEC-036 — Resolve range loops and loop-control statements.**
  Reconcile the C-style loop section with `for i: i32 in 0..iterations` in the concurrency example. Decide whether range loops, `break`, and `continue` are supported, including endpoint/overflow rules and interaction with defer.
  **Done when:** every retained loop form has semantic and execution tests; zero-iteration, nested-loop, and transfer-of-control cases behave correctly. Removed/deferred forms produce explicit diagnostics and examples use supported syntax.

- [ ] **SPEC-037 — Implement explicit endianness semantics.**
  Choose canonical prefix/suffix spelling (`be_u16` versus `u16_be`) and distinguish arithmetic values, memory representation, and explicit conversions. Define native-endian behavior and interaction with pointer access.
  **Done when:** byte-buffer fixtures prove big/little-endian load/store/conversion behavior independently of host endianness; arithmetic preserves numeric values; unsupported combinations fail rather than becoming null or native i32 types.

- [ ] **SPEC-038 — Resolve the packed-struct layout contract.**
  Reconcile mandatory backing storage with examples that omit it, explicit versus implicit offsets, fields spanning backing words, gaps/overlap, signed fields, and the multiword IPv4 example. Define exact bit numbering together with SPEC-037.
  **Done when:** SPEC.md gives unambiguous byte/bit layouts and invalid-layout examples, including word-boundary cases, with no contradictory backing-type requirements.

- [ ] **SPEC-039 — Implement packed bitfield checking and lowering.**
  Validate custom widths, offsets, backing capacity, and overlap against SPEC-038. Generate correct masks/shifts, reads, writes, and layout using resolved field widths and endianness.
  **Done when:** byte-exact tests cover little/big-endian fields, boundaries, and supported multiword layouts; writing one field preserves neighboring bits; u4 fields do not silently become separate i32 fields.

## 7. Implement concurrency and external packages

- [ ] **SPEC-040 — Resolve the concurrency type and lifetime contract.**
  Specify the relationship between a deferred function's declared return, its produced handle, and `join`/`force_join` results without double wrapping. Reconcile `run`/`spawnable` with implementation-only `spawn`/`await`. Define handle ownership, repeated joins, errors/panics, task cleanup, and captured-data lifetimes.
  **Done when:** Deferred and Spawn have distinct, consistent types and behavior; examples and API signatures agree; semantic checks reject misuse of deferred/spawnable functions and unsafe argument lifetimes within the documented guarantees.

- [ ] **SPEC-041 — Implement the Spawn thread runtime.**
  Provide real thread creation, typed argument/result transport, function/method invocation, synchronization, failure handling, and resource ownership. Replace the current zero/one-argument pointer/integer casting assumptions.
  **Done when:** synchronization-based tests prove work runs on another thread; multiple arguments and non-i32 results round-trip correctly; joins and failure paths release resources. The runtime is linked into supported execution modes.

- [ ] **SPEC-042 — Implement genuine deferred asynchronous execution.**
  Choose and document the event-loop/executor and an initial asynchronous I/O operation. Implement suspension/resumption, typed results, runtime linkage, and lowering of all supported async operations/types.
  **Done when:** a pending I/O operation allows other work to progress; completion/failure and lifetimes match SPEC-040; no unresolved Gloin async operations remain at LLVM export. A synchronous wrapper is insufficient.

- [ ] **SPEC-043 — Integrate join, force_join, and Result handling.**
  Implement the public APIs across Deferred and Spawn using the representation and ownership rules from SPEC-034/040. Replace debug assumptions with deliberate result/error propagation.
  **Done when:** the canonical async/thread examples run with expected success and error outcomes; `force_join` failure has specified behavior; repeated joins and cleanup follow the chosen contract without double-free or leaked handles.

- [ ] **SPEC-044 — Implement external package resolution.**
  Define how `#package` maps to a package manifest/source/cache, dependency versions, exported modules, and reproducible resolution. A public registry is not required unless chosen as part of the package design.
  **Done when:** a versioned fixture package imports and executes correctly; missing/conflicting dependencies fail clearly; locked dependencies can be resolved reproducibly, including the documented offline/cache behavior. External example packages are provided or identified explicitly.

## 8. Validate and package the chosen release

- [ ] **SPEC-045 — Implement native output if included in the release scope.**
  Add LLVM export, target selection/data layout, object emission, runtime linkage, and executable generation through the shared compiler pipeline. Define the supported host/target matrix and diagnostic behavior for unsupported combinations.
  **Done when:** the selected native targets compile and run fixture programs without the JIT, with results matching the reference execution mode. If SPEC-006 selects a JIT-only first release, mark native output explicitly deferred and keep its implementation work open.
  **SPEC-006 disposition:** native object/executable output and additional host/target platforms are explicitly deferred from the first release. This implementation item stays open; the in-process JIT is the required execution mode.

- [ ] **SPEC-046 — Verify specification coverage and prepare the release.**
  Turn normative spec examples into complete executable or expected-error fixtures. Clearly identify conceptual examples and external dependencies. Finish installation/packaging, version/help information, platform setup instructions, and a feature matrix linked to acceptance tests.
  **Done when:** a fresh checkout can build/test/install by following the README; all tests required by the chosen release scope pass; every advertised feature has end-to-end evidence; remaining unsupported features are documented and rejected. Record serial/parallel results and applicable runtime sanitizer checks. No temporary audit patch, cached old binary, or machine-specific path is required.
  **SPEC-006 scope:** publish only after SPEC-001 through SPEC-021 and this release gate are verified for the scalar JIT contract. Deferred tasks need not be complete, but their unsupported behavior must be diagnosed. Report targeted core acceptance separately from the unfiltered suite, retaining every unresolved failure and its task ID.

### Deferred numeric extension

- [ ] **SPEC-013b — Specify and implement 128-bit numeric types (deferred).**
  Preserve the existing i128/u128/f128 intent outside the initial scalar release. Define literal ranges, arithmetic/conversions, ABI/runtime requirements, and target support before exposing these types. Schedule this extension after the core release work; its ID links it to SPEC-013 without enlarging that task.
  **Done when:** boundary, arithmetic, conversion, call, and execution tests prove the selected 128-bit representations and behavior on documented targets; unsupported variants produce diagnostics rather than smaller fallback types.

## Specification coverage map

| Specification area | Checklist items |
| --- | --- |
| UTF-8, declarations, explicit typing, entry point | SPEC-006 through SPEC-014, SPEC-020 |
| Variables, constants, numeric types | SPEC-012, SPEC-013, SPEC-013b |
| Functions and operators | SPEC-011, SPEC-013 through SPEC-015 |
| If, unless, while, for | SPEC-016, SPEC-017, SPEC-036 |
| Strings | SPEC-022, SPEC-030, SPEC-035 |
| Standard/local/external imports | SPEC-023, SPEC-029, SPEC-030, SPEC-044 |
| Pointers, references, arenas, defer | SPEC-024 through SPEC-028 |
| Structs, methods, enums | SPEC-024, SPEC-026, SPEC-031 through SPEC-034 |
| Endianness and packed bitfields | SPEC-037 through SPEC-039 |
| Deferred, Spawn, Result, joins | SPEC-031, SPEC-034, SPEC-040 through SPEC-043 |
| Implementation-only extensions | SPEC-006, SPEC-031 through SPEC-033, SPEC-035, SPEC-036 |
| Build, CLI, lowering, execution, release | SPEC-001 through SPEC-005, SPEC-007, SPEC-010, SPEC-018 through SPEC-021, SPEC-045, SPEC-046 |

## Completion log

For each completed item, add its date, a short outcome, relevant repository paths, and exact verification commands/results. Record decisions and deferrals explicitly; a deferred implementation does not satisfy the full-spec completion criteria.

| Item | Date | Outcome and evidence |
| --- | --- | --- |
| SPEC-001 | 2026-09-08 | Synchronized [parser.h](src/parser.h), [AST.h](src/AST.h), and [codegen.h](src/codegen.h): declared the generic parser helper, retained function/struct generic parameters, restored codegen symbol/function/template/defer state, aligned `declare`, and removed duplicate import declarations. Fresh CMake configuration succeeds; parser/AST and all other configured translation units except codegen compile, and `gloinc` links. Codegen reports only the MLIR API failures recorded under SPEC-002. Independently built existing parser/spec suites: **35/35 pass**. Commands below. |
| SPEC-002 | 2026-09-08 | [CMakeLists.txt](CMakeLists.txt) requires MLIR 21.1.6 and shared MLIR/LLVM/ExecutionEngine targets; [codegen.cpp](src/codegen.cpp) uses the installed APIs. A fresh Debug build produces both executables. All **4/4 MLIRSetup tests pass**, including the new compiler-dialect regression in [mlir_test.cpp](tests/mlir_test.cpp). Full serial CTest: **87/100 pass, 13 fail, no crashes**; the extra test accounts for the increase from the audited 99 configured tests. Link commands contain no LLVM/MLIR component archives, and `otool -L` confirms shared dependencies. Unsupported-version, missing-target, and static-target configuration probes all reject their fixtures. Requirements are documented in [docs/toolchain.md](docs/toolchain.md); verification and remaining failures follow. |
| SPEC-003 | 2026-09-08 | [CMakeLists.txt](CMakeLists.txt) shares `gloin_frontend` and `gloin_backend` between the CLI and tests, requires standard C++23, scopes LLVM settings to backend consumers, and gates a checksum-pinned GoogleTest 1.16.0 download on `BUILD_TESTING`. Fresh ON/OFF builds pass with explicit LLVM/MLIR paths; OFF compiles the complete compiler implementation without test dependencies. [Makefile](Makefile) supports build options, delegates tests to CTest, and preserves failure reporting. **4/4 dialect tests pass; 87/100 full-suite tests pass, with exactly the same 13 failures as SPEC-002 and no crashes.** [docs/toolchain.md](docs/toolchain.md) documents target boundaries and build modes; verification follows. |
| SPEC-004 | 2026-09-08 | [CMakeLists.txt](CMakeLists.txt) includes all generic tests, discovers external tools, rejects omitted suite files, and assigns 30-second CTest timeouts. [external_runner.cpp](tests/support/external_runner.cpp) runs tools without a shell in per-invocation temporary directories, enforces child timeouts, checks both statuses, preserves stderr, and separates errors from strict i32 results. Eight harness regressions and five E2E tests pass in parallel. The former lit checks are maintained in [codegen_test.cpp](tests/codegen_test.cpp). Source definitions and CTest discovery match at **112 tests**; serial/parallel full suites both report **98 passes, 14 failures, no crashes**, including the newly exposed missing spawn operation. Full inventory and failure task IDs are in [tests/README.md](tests/README.md). |
| SPEC-005 | 2026-09-08 | [Compiler CI](.github/workflows/ci.yml) builds both test modes on hosted macOS 15 arm64 and publishes full logs, environment, inventory, and JUnit reports even when tests fail. [install-llvm.sh](scripts/install-llvm.sh) pins LLVM/MLIR 21.1.6 and the required Z3 4.15.4 ABI using checked historical formulas and bottles. [Run 34242653935](https://github.com/kubabialy/gloinc/actions/runs/34242653935), at `ddcc705de5910e30ee0ac4dd949251d5915cdaf5`, passed both clean builds; downloaded serial/parallel reports each confirm **98/112 pass, 14 fail, no crashes or skipped tests**. [README.md](README.md), [toolchain requirements](docs/toolchain.md), [example status](examples/README.md), [phase notes](examples/PHASE2_PROGRESS.md), and [OpenCode.md](OpenCode.md) replace unsupported readiness/coverage claims with measured status. Verification follows. |
| SPEC-006 | 2026-09-08 | [SPEC.md](SPEC.md) defines the scalar JIT release on Apple Silicon macOS, required types/features, declaration/modifier/type/terminator/UTF-8 rules, three complete core programs, and eleven expected-error fragments. Later designs are separated from the core, spelling contradictions in examples are corrected, and unsupported implementation syntax maps to explicit rejection tasks. Native output and later features remain open; SPEC-013b retains the deferred 128-bit extension. [README.md](README.md) and this checklist agree on SPEC-021 acceptance followed by SPEC-046 release validation. Documentation consistency checks pass; compiler code and test expectations are unchanged. |
| SPEC-007 | 2026-09-09 | [diagnostics.h](src/diagnostics.h) preserves owned source spans and structured errors. [compiler.cpp](src/compiler.cpp) stops parse/check/generate at the first failed stage and owns successful modules; parser/Sema/codegen no longer print errors while returning success. [diagnostics_test.cpp](tests/diagnostics_test.cpp) adds **15 passing regressions**; the five external E2E tests use the guarded API. Fresh Debug build succeeds. Full serial/parallel suites both report **114/127 passes, 13 failures, no crashes or skipped tests**. Four old newline-related failures pass; three previously hidden failures are now visible and mapped in [tests/README.md](tests/README.md). The branch fixture now correctly uses `mut`, with a negative regression for immutable assignment. |
| SPEC-008 | 2026-09-09 | [lexer.cpp](src/lexer.cpp) uses bounded byte scanning with whole-source UTF-8 validation, exact operator boundaries, reserved keywords, distinct type/literal tokens, and malformed-literal errors. [SPEC.md](SPEC.md#lexical-literal-forms-spec-008) fixes the lexical grammar. Minimal parser changes reject legacy expressions and prevent literals/other keywords from substituting for types. All **34 lexer and 18 diagnostic tests pass**, resolving all seven audited lexer failures without enabling obsolete syntax. Debug rebuild succeeds; full serial/parallel runs both report **137/145 passes, eight failures, no crashes or skipped tests**. Source definitions match discovery and all timeouts remain 30 seconds. ASan/UBSan probes pass for 75,536 inputs. |
| SPEC-009 | 2026-09-14 | [parser.cpp](src/parser.cpp) consistently consumes constructs, separates core compilation from deferred syntax tests, fixes precedence and identifier conditions, and requires explicit annotations, canonical modifiers, delimiters, and statement-only assignments. [AST.h](src/AST.h) retains constant/visibility/member metadata; Sema/codegen explicitly reject unevaluated constants pending SPEC-012. First-error parsing discards complete programs and bounds recursive nesting. Fresh Debug build succeeds. All **49 parser, 20 diagnostic, and six E2E tests pass**; serial/parallel suites both report **160/168 passes, the same eight failures as SPEC-008, and no crashes/skips**. Source definitions match discovery and every timeout is 30 seconds. ASan/UBSan checks pass for 20,980 parser probes. The canonical method/field fixture corrections preserve their feature assertions; see [docs/parser.md](docs/parser.md). |
| SPEC-010 | 2026-09-14 | [checked_program.h](src/checked_program.h) owns the AST and semantic tables with canonical scalar types and stable declaration IDs. Sema is the only constructor; normal codegen accepts only checked programs and produces owned modules. Compile-time API restrictions and **13 passing contract tests** cover primitive signatures/storage, aliases, signedness, annotation errors, ID-based references/calls, ownership, independent runs, targets, and explicit guards for unfinished semantics. Existing backend experiments use the explicit unchecked test entry without weakening assertions. Incremental Debug build succeeds; full serial/parallel runs both report **174/182 passes, the same eight failures as SPEC-009, and no crashes/skips**. All seven E2E tests pass, including aliases and nested shadowing returning 42. [docs/checked-program.md](docs/checked-program.md) defines ownership, API use, and remaining scope. |
| SPEC-011 | 2026-09-15 | [sema.cpp](src/sema.cpp) collects core function signatures before bodies and [codegen.cpp](src/codegen.cpp) declares MLIR functions before body emission. The normative scope rules cover duplicates, shadowing, initializer visibility, and immutable parameters. All **11 scope tests and 12 E2E tests pass**, including forward calls, direct/mutual recursion, and nested scope restoration. Incremental Debug build succeeds; serial/parallel suites both report **190/198 passes, the same eight known failures, and no crashes/skips**. User-defined types, constants, and for-loop scope remain their deferred/subsequent tasks. |
| SPEC-012 | 2026-09-15 | [sema.cpp](src/sema.cpp) validates targets, exact initializer/store types, and definite initialization with branch/loop state keyed by declaration ID. [sema_constants.cpp](src/sema_constants.cpp) evaluates pure, lexically ordered constants with checked arithmetic and boolean short-circuiting; codegen materializes folded values and supports delayed immutable storage. All **17 variable tests and 19 E2E tests pass**. Incremental Debug build succeeds; full serial/parallel runs both report **214/222 passes, the same eight known failures, and no crashes/skips**. The normative contract and checked-program documentation record the current i32/f32/bool constant boundary and later numeric/control-flow work. |
| SPEC-013 | 2026-09-15 | [numeric.cpp](src/numeric.cpp) validates complete literal spellings and exact-width ranges; [sema_numeric.cpp](src/sema_numeric.cpp) supplies contextual types without converting typed operands. Numeric AST nodes preserve spelling; checked literals/constants own APInt/APFloat values emitted directly by codegen. All **12 numeric tests, 24 E2E cases, and eight exact float-bit probes pass**. Incremental Debug build succeeds; full serial/parallel suites report **231/239 passes, the same eight known failures, and no crashes/skips**. Frontend APInt/APFloat uses the existing shared LLVM library with consistent linkage. The spec defines cast rejection and preserves later runtime operator/return work. |
| SPEC-014 | 2026-09-16 | [sema.cpp](src/sema.cpp) validates call/return types, conservative return paths, unreachable source, and entry signatures before constructing a checked program. Module/executable modes preserve helper-only compilation while requiring a validated `main() -> i32` for execution. Codegen defensively rejects checked non-void fallthrough. All **12 function tests, 29 E2E cases, and 114 focused tests pass**. Incremental Debug build succeeds; serial/parallel suites report **248/256 passes, the same eight known failures, and no crashes/skips**. Five new execution cases cover nested branches, void returns, loop fallbacks, typed calls, and a valid -1 result. |
| SPEC-015 | 2026-09-16 | [operators.h](src/operators.h) shares scalar operator rules between constant and runtime checking. [codegen_operators.cpp](src/codegen_operators.cpp) emits checked integer arithmetic, signed/unsigned division/remainder/comparisons, native float operations, logical negation, and short-circuit branches. All **16 operator tests and 130 focused tests pass**. The operator suite executes **37 successful results and 66 expected runtime traps**, including exact float bits and instrumented call-order/side-effect checks. Incremental Debug build succeeds; serial/parallel suites report **264/272 passes, the same eight known failures, and no test-process crashes/skips**. Runtime traps remain distinct from valid i32 results; in-process recovery/CLI integration remains SPEC-019/020. |
| SPEC-016 | 2026-09-17 | [codegen_control_flow.cpp](src/codegen_control_flow.cpp) replaces empty-block reachability heuristics with explicit live continuations. Returns clear the insertion point; branches/loops connect only live paths, and raw generation rejects statements after termination. All **12 control-flow tests and 142 focused tests pass**, including nine external executions and structural CFG checks. The pre-change regression reproduced unchecked emission after returns. Incremental Debug build succeeds; serial/parallel suites report **276/284 passes, the same eight known failures, and no test-process crashes/skips**. Condition counters verify one-time branch evaluation, skipped else-if conditions, repeated loop conditions, and empty-loop execution. |

### SPEC-001 verification

Run from the repository root on Apple Silicon with AppleClang 16, LLVM/MLIR 21.1.6, CMake 4.2.1, and Ninja. This check uses the existing GoogleTest source cache at `build/_deps/googletest-src`; dependency pinning and fetch behavior remain SPEC-003 work.

```sh
spec001_build_dir=$(mktemp -d /private/tmp/gloinc-spec001.XXXXXX)
cmake -S . -B "$spec001_build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
cmake --build "$spec001_build_dir" -j 2 -- -k 0 > "$spec001_build_dir/build.log" 2>&1
# Observed at SPEC-001: exit 1, seven diagnostics; resolved by SPEC-002 below.

/usr/bin/c++ -std=c++23 -pthread -Isrc \
  -Ibuild/_deps/googletest-src/googletest/include \
  -Ibuild/_deps/googletest-src/googletest \
  src/lexer.cpp src/parser.cpp tests/parser_test.cpp tests/spec_test.cpp \
  build/_deps/googletest-src/googletest/src/gtest-all.cc \
  build/_deps/googletest-src/googletest/src/gtest_main.cc \
  -o "$spec001_build_dir/parser-tests"
"$spec001_build_dir/parser-tests"
# Result: exit 0; 27 ParserTest and 8 SpecTest cases pass.
git diff --check
```

The independent test executable uses the repository sources without temporary repairs and avoids the blocked backend dependency. This verifies the declaration repair; it does not establish generic execution, a working compiler CLI, or a passing full test suite.

### SPEC-002 verification

Run from the repository root in the same environment as SPEC-001. The GoogleTest source cache is reused, but the build directory starts empty:

```sh
spec002_build_dir=$(mktemp -d /private/tmp/gloinc-spec002.XXXXXX)
cmake -S . -B "$spec002_build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
cmake --build "$spec002_build_dir" -j 2
"$spec002_build_dir/gloinc"
ctest --test-dir "$spec002_build_dir" -R '^MLIRSetup\.' --output-on-failure
ctest --test-dir "$spec002_build_dir" -j 1 --output-on-failure
otool -L "$spec002_build_dir/gloinc" "$spec002_build_dir/gloinc_test"
ninja -C "$spec002_build_dir" -t commands gloinc gloinc_test
git diff --check
```

Configuration, build, CLI lexer-demo startup, and dialect checks exit 0. Full CTest exits 8 with the failures below; no tests are disabled. The only build warnings are deprecations in MLIR's generated operation accessors. The five external E2E tests pass, subject to the existing harness limitations in SPEC-004. JIT smoke reaches execution-engine creation and reports missing builtin-dialect LLVM translation registration instead of crashing while loading dialects (SPEC-019).

| Remaining tests | Follow-up |
| --- | --- |
| `LexerTest.HandlesKeywords`, `HandlesComments`, `HandlesOperatorsAndPunctuation`, `HandlesInKeyword`, `HandlesRangeOperator`, `HandlesRangeInContext`, `TrackLineNumbers` | SPEC-008 |
| `ParserTest.ParseStructDefinition`, `ParsePackedStruct` | SPEC-009 |
| `AsyncTest.SpawnGeneration`, `SemaAsyncTest.AsyncTypes` | SPEC-009, SPEC-040, SPEC-041 |
| `ArrayStringTest.HandlesStringLiterals` | SPEC-022 |
| `JitRunnerTest.SmokeTest` | SPEC-019 |

Configuration rejection was checked with isolated package fixtures, without changing the installed toolchain. Each fixture lives under `<fixture>/lib/cmake/mlir`, and the actual root CMake project is configured with:

```sh
cmake -S . -B <empty-build-directory> -G Ninja \
  -DCMAKE_FIND_ROOT_PATH=<fixture> -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DMLIR_DIR=<fixture>/lib/cmake/mlir
```

The unsupported fixture's `MLIRConfigVersion.cmake` advertises 20.1.8 and sets `PACKAGE_VERSION_COMPATIBLE` and `PACKAGE_VERSION_EXACT` to false: CMake rejects it and names the required 21.1.6 version. The other fixtures advertise 21.1.6 with both flags true: one config declares only imported shared `MLIR`/`LLVM` targets, and the other declares static `MLIR` plus shared `LLVM`/`MLIRExecutionEngineShared`. Both exit 1 with the project's actionable missing/shared-target diagnostics before dependency fetching or compilation.

### SPEC-003 verification

Run from the repository root on the SPEC-002 toolchain. Both build directories start empty. Unlike the earlier audits, the tests-enabled configuration downloads the pinned archive through CMake, verifies its SHA-256, and uses no local source override:

```sh
spec003_build_dir=$(mktemp -d /private/tmp/gloinc-spec003.XXXXXX)
cmake -S . -B "$spec003_build_dir/no-tests" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build "$spec003_build_dir/no-tests" -j 2
test ! -e "$spec003_build_dir/no-tests/_deps"
test ! -e "$spec003_build_dir/no-tests/gloinc_test"
ctest --test-dir "$spec003_build_dir/no-tests" -N
# All exit 0; CTest discovers zero tests.

cmake -S . -B "$spec003_build_dir/with-tests" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build "$spec003_build_dir/with-tests" --target gloinc_test -j 2
cmake --build "$spec003_build_dir/with-tests" --target gloinc -j 2
make run BUILD_DIR="$spec003_build_dir/no-tests" BUILD_TESTING=OFF BUILD_ARGS='-j 2'
make test BUILD_DIR="$spec003_build_dir/with-tests" BUILD_TESTING=OFF \
  BUILD_ARGS='-j 2' CTEST_ARGS='-R ^MLIRSetup'
# All exit 0; make test explicitly enables tests, and 4/4 dialect tests pass.
make test BUILD_DIR="$spec003_build_dir/with-tests" BUILD_ARGS='-j 2'
# CTest exits 8 with the 13 failures listed under SPEC-002; make exits 2.
make clean BUILD_DIR="$spec003_build_dir/no-tests"
test -e "$spec003_build_dir/no-tests/CMakeCache.txt"
test ! -e "$spec003_build_dir/no-tests/gloinc"
test ! -e "$spec003_build_dir/no-tests/src/dialect/GloinOps.h.inc"
git diff --check
```

The sandbox's DNS restriction initially blocked the tests-enabled fetch; retrying configuration with network access succeeded. CMake's downloaded archive independently hashes to `78c676fc63881529bf97bf9d45948d905a66833fbfa5318ea2cd7478cb98f399`. The tests-disabled build succeeded inside the sandbox without any fetch.

Inspection of each `compile_commands.json` confirms one compile entry per project source (7 without tests, 24 with tests), CMake's AppleClang C++23 flag `-std=c++2b`, and no LLVM include paths/definitions on frontend or GoogleTest compilation. Each build produces eight generated `.inc` files under its own `src/dialect`; none appear in the source tree. Ninja link commands include both compiler libraries and the shared LLVM/MLIR targets for the CLI. Building `gloinc_test` first verifies that consumers receive the generated-header dependencies. A subsequent Makefile test invocation reports `ninja: no work to do` before running CTest. The full-suite failure list matches SPEC-002 exactly; no tests were added, removed, or disabled by this task.

### SPEC-004 verification

Run from the repository root on the established toolchain. Verification used a fresh build directory and a source cache of the pinned GoogleTest 1.16.0 archive; omitting the cache argument below fetches that same archive:

```sh
spec004_build_dir=$(mktemp -d /private/tmp/gloinc-spec004.XXXXXX)
cmake -S . -B "$spec004_build_dir/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="<pinned-googletest-source-cache>"
cmake --build "$spec004_build_dir/build" -j 2
ctest --test-dir "$spec004_build_dir/build" --show-only=json-v1 > "$spec004_build_dir/inventory.json"
ctest --test-dir "$spec004_build_dir/build" -R '^(ExternalRunnerTest|E2ETest)' \
  -j 4 --output-on-failure
# Exit 0: all 13 subprocess and E2E tests pass.
ctest --test-dir "$spec004_build_dir/build" -j 1 --output-on-failure
cp "$spec004_build_dir/build/Testing/Temporary/LastTestsFailed.log" "$spec004_build_dir/serial-failures.log"
ctest --test-dir "$spec004_build_dir/build" -j 4 --output-on-failure
# Both full-suite commands exit 8: 98/112 pass, 14 fail, no crashes.
diff -u "$spec004_build_dir/serial-failures.log" \
  "$spec004_build_dir/build/Testing/Temporary/LastTestsFailed.log"
test ! -e "$spec004_build_dir/build/temp.mlir"
git diff --check
```

`TEST`/`TEST_F` names extracted from all `tests/*_test.cpp` files match the CTest JSON names exactly, with no duplicates; every discovered case has `TIMEOUT=30`. The four generic tests pass with their original assertions. Migrating the lit checks exposes `CodeGenTest.GenerateSpawn`'s missing operation, increasing the known failure count from 13 to 14 without changing compiler code. The previous 13 failures remain assigned to their existing tasks, and the additional failure is assigned to SPEC-040/SPEC-041.

The harness regression suite verifies optimizer and runner failures despite numeric stdout, malformed/empty/multiple/out-of-range results, a valid negative return value, a missing executable, timeouts in both stages, and concurrent invocations with different results. The child fixture runs from a path containing a space. An unlisted temporary `tests/spec004_unlisted_test.cpp` caused configuration to fail with the expected missing-suite diagnostic; removing the probe restored successful configuration. A fresh `BUILD_TESTING=OFF` configuration still creates no test dependencies and discovers zero tests. Formatting checks passed for the new helper/fixture/tests and rewritten E2E source. No failing cases are disabled or marked as expected successes.

### SPEC-005 verification

[Hosted run 34242653935](https://github.com/kubabialy/gloinc/actions/runs/34242653935)
verified implementation commit `ddcc705de5910e30ee0ac4dd949251d5915cdaf5` on
2026-09-08. Its environment report records macOS 15.7.9 arm64, AppleClang 17.0.0,
CMake 4.4.3, Ninja 1.13.2, and LLVM/MLIR 21.1.6. The installer downloaded the
historical LLVM and Z3 bottles; pinning Z3 4.15.4 resolves the LLVM bottle's
requirement for `libz3.4.15.dylib`.

The exact configure/build commands and report handling are maintained in
[the workflow](.github/workflows/ci.yml). Both build directories were absent at
startup. Debug configurations with `BUILD_TESTING=OFF` and `ON` both built
successfully using explicit LLVM/MLIR package paths. The OFF step also verified
that no `_deps` directory or test executable existed. The ON configuration
fetched the pinned GoogleTest archive without a local source override.

```sh
ctest --test-dir build-ci --show-only=json-v1 > ci-reports/inventory.json
ctest --test-dir build-ci -j 1 --output-on-failure --output-junit "$PWD/ci-reports/serial.xml"
ctest --test-dir build-ci -j 4 --output-on-failure --output-junit "$PWD/ci-reports/parallel.xml"
# Both full-suite commands exit 8; the workflow runs both before reporting failure.
gh run download 34242653935 -n compiler-ci-reports -D "<report-directory>"
bash -n scripts/install-llvm.sh
git diff --check
```

The published artifact was downloaded and inspected: JSON inventory and both
JUnit reports contain all **112 tests**, with **98 passes, 14 failures, zero
skipped/disabled cases, and no crashes** in each run. Failure names match the
SPEC-004 baseline in [tests/README.md](tests/README.md). All four dialect checks,
five external E2E cases, and eight subprocess-harness regressions pass. The run's
build steps and artifact publication succeeded; its full-suite step and overall
conclusion remain failed. Reports include both complete test logs, configure and
build logs, environment details, CMake caches, and compiler commands. Artifact
retention is 14 days; the baseline is recorded here for longer-term reference.

Local checks also verified the installer's existing-toolchain reuse path,
workflow YAML parsing, shell syntax for every workflow command block, relative
documentation links, and the report step's execution of both suites before
returning failure. No compiler behavior or test expectations changed for this task.


### SPEC-006 verification

Reviewed the specification against `src/lexer.h`, `src/lexer.cpp`,
`src/parser.cpp`, `src/sema.cpp`, `src/codegen.cpp`, and the lexer/parser/spec
unit tests. In particular, lexer token declarations do not imply accepted
syntax; parser defaults for omitted types and the backend's internal `String`
entry are implementation gaps rather than language rules.

| Acceptance area | Recorded decision/evidence |
| --- | --- |
| Declaration and visibility ambiguity | `def const`, visibility immediately after `def`, explicit receiver parameters; later examples use the same order. |
| Types and aliases | Explicit binding/parameter/return annotations; `int = i32`, target-width unsigned `usize`, lowercase built-in `string`. |
| Source and delimiters | Valid UTF-8 with ASCII identifiers, reserved vocabulary, LF/CRLF trivia, mandatory statement/import semicolons, comma-separated lists. |
| Release boundary | Listed scalar types/operators and JIT execution on Apple Silicon macOS; SPEC-021 acceptance and SPEC-046 release gate. |
| Canonical source | Three complete core programs specify results 42, 42, and 3; eleven invalid fragments document required diagnostics. |
| Unsupported/deferred work | Disposition table links syntax to rejection/implementation tasks; all deferred tasks stay unchecked, including new SPEC-013b and native-output SPEC-045. |

A Python standard-library audit passed for local Markdown links and anchors,
balanced code fences, core table columns, all 47 unique checklist task IDs and
actual task references, the three core examples' entry points/braces, and
canonical declaration/import/receiver spellings in the later examples. The
existing illustrative split ID `SPEC-027a` is an example, not a missing task.

```sh
git diff --check
git diff --name-only 6f5ce20 --
# Documentation changes only: README.md, SPEC-TODO.md, SPEC.md.
```

This is a specification decision, not a compiler implementation change. No
compiler tests were rerun or reclassified. The last measured full-suite baseline
remains SPEC-005's 98 passes and 14 failures; the new canonical programs become
execution/diagnostic acceptance fixtures under SPEC-021 as their implementation
tasks are completed. Packed-layout and concurrency API ambiguities remain
explicitly conceptual until SPEC-037/SPEC-038 and SPEC-040 resolve them.


### SPEC-007 verification

A fresh build used the established Apple Silicon toolchain (AppleClang 16,
LLVM/MLIR 21.1.6, CMake 4.2.1) and the pinned GoogleTest source cache:

```sh
cmake -S . -B /private/tmp/gloinc-spec007-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
cmake --build /private/tmp/gloinc-spec007-build -j 2
ctest --test-dir /private/tmp/gloinc-spec007-build -R '^DiagnosticsTest\.' --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec007-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec007-serial.xml
ctest --test-dir /private/tmp/gloinc-spec007-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec007-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec007-build --show-only=json-v1
# Build and diagnostic regressions exit 0. Each full suite exits 8.
git diff --check
```

Both JUnit reports contain **127 tests, 114 passes, 13 failures, zero skipped or
disabled tests, and no crashes**. Failure names match between serial and parallel
runs. All 15 diagnostic, four dialect, five E2E, eight harness, and four generic
IR-string tests pass. The complete inventory and changed failure classifications
are recorded in [tests/README.md](tests/README.md).

The diagnostic cases cover token/AST ownership after lexer/parser destruction,
exact byte spans, CRLF filename/line/column rendering, unknown/unterminated/NUL
tokens, malformed and truncated declarations/blocks/calls, missing terminators,
out-of-range numeric conversion failures, and partial-AST disposal. They also
verify that parser errors never reach Sema, semantic errors never reach codegen,
unsupported statements/expressions cannot disappear, codegen failures return no
module, void calls are distinguished from missing expression values, and generated
operations retain source locations. Errors are collected without implicit stderr
output; client rendering is explicit.

Newline token handling was necessary to support located multiline diagnostics;
it resolves two lexer and two struct-parser failures without claiming SPEC-008
or SPEC-009 complete. The now-visible arena, obsolete async-fixture, and array
failures remain open under SPEC-028, SPEC-009/SPEC-040, and SPEC-035. No language
failure was disabled or marked as expected success. Full typed-program enforcement,
complete grammar/semantic validation, IR verification/lowering, JIT execution, and
the file-reading CLI remain SPEC-008 through SPEC-020 as applicable.

### SPEC-008 verification

Rebuilt the SPEC-007 Debug directory with the same AppleClang 16 and LLVM/MLIR
21.1.6 toolchain. This was an incremental build, not another clean-build claim.

```sh
cmake --build /private/tmp/gloinc-spec007-build -j 2
ctest --test-dir /private/tmp/gloinc-spec007-build \
  -R '^(LexerTest|DiagnosticsTest)\.' --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec007-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec008-serial.xml
ctest --test-dir /private/tmp/gloinc-spec007-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec008-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec007-build --show-only=json-v1
# Build and all 52 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **145 tests, 137 passes, eight failures, and no
crashes or skips**. The failure names are identical and listed with task IDs in
[tests/README.md](tests/README.md). All source test definitions match discovery,
without duplicates; every test retains its 30-second timeout. All five E2E,
eight external harness, four dialect, and four generic IR-string tests pass.

The 15 new lexer cases cover token/position boundaries, repeated EOF, malformed
numbers/quotes/escapes, Unicode scalar and encoding rules, reserved vocabulary,
type/literal separation, and progress for every byte value. Three new pipeline
cases verify that legacy syntax cannot become accepted through token recognition,
invalid encoding in trailing comments blocks compilation, and literals or
non-type keywords cannot parse as type annotations.

An additional temporary standalone scanner probe was built with `/usr/bin/clang++`
and `-std=c++23 -fsanitize=address,undefined -fno-omit-frame-pointer`. It checked
all 65,536 two-byte inputs plus 10,000 deterministic random byte strings (seed
8008, lengths 0–256). All 75,536 inputs reached EOF with monotone bounded token
spans and valid diagnostic spans; neither sanitizer reported an error. This
probe checks scanner safety, not complete language acceptance.

SPEC-007 had already repaired two audited lexer failures through newline handling;
SPEC-008 resolves the other five. Existing feature failures stay visible. Numeric
value/range conversion remains SPEC-013, complete parsing remains SPEC-009,
and string decoding/storage/execution remains SPEC-022.


### SPEC-009 verification

After resuming on 2026-09-14, the old temporary build outputs were incomplete.
Verification therefore used a fresh Debug build with AppleClang 16, LLVM/MLIR
21.1.6, and the existing pinned GoogleTest source cache:

```sh
cmake -S . -B /private/tmp/gloinc-spec009-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec009-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec009-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build exits 0; both full suites exit 8 for the same known feature failures.
git diff --check
```

Both JUnit reports contain **168 tests, 160 passes, eight failures, and no crashes
or skipped tests**. Failure names match SPEC-008 exactly and are mapped in
[tests/README.md](tests/README.md). All 49 parser, 20 diagnostic, six E2E, four
dialect, eight external harness, and four generic IR-string tests pass. Source
`TEST`/`TEST_F` names match CTest discovery without duplicates; all timeouts remain
30 seconds.

Twenty new parser tests verify complete canonical programs, identifier conditions,
precedence and associativity, newlines/comments at token boundaries, strict lists,
annotations and modifiers, file/local scope syntax, required semicolons, assignment
placement, complete loop headers, truncated input, exact source spans, bounded
nesting, and deferred syntax composition. Two diagnostic regressions verify that
core compilation rejects deferred syntax and that unevaluated constants cannot
be emitted as ordinary runtime bindings. The new E2E regression executes a
multiline call inside `if enabled` and returns 42 through external MLIR tools.

A temporary standalone probe, compiled using `/usr/bin/clang++` with
`-std=c++23 -fsanitize=address,undefined -fno-omit-frame-pointer`, tested 10,000
deterministic random token sequences (seed 9009) in both parsing modes, plus 980
truncation/single-character-deletion cases derived from complete programs. All
**20,980 probes pass**: successful programs reach EOF with bounded spans and
printable child nodes; errors discard every statement and remain terminal.
Neither sanitizer reports an error.

The implementation uses a deliberate first-error policy instead of multi-error
recovery: failed tokens cannot be retried in an endless recovery loop. This meets
the progress/failure contract without claiming error recovery was implemented.
The 512-entry recursion limit produces a diagnostic on excessive nesting.

The default core parser rejects deferred grammar. Existing stage tests opt into
`ParseMode::SyntaxOnly`; this is not a language extension available through
`compile_source`. Obsolete `pub def`/untyped `self` and a missing generic-field
comma were corrected against SPEC-006, preserving feature assertions and adding
negative coverage. The standalone `unless` fixture uses the statement API and
now checks its AST and EOF. Concurrency fixtures remain failing until SPEC-040
settles their canonical contract and runtime behavior.

Constant evaluation, resolved types/symbols, complete numeric/operator semantics,
return checking, loop scope/omitted headers, lowering, and JIT remain their listed
subsequent tasks. Parsing the canonical examples is not a claim that they all
execute yet.


### SPEC-010 verification

Rebuilt the SPEC-009 Debug directory using the established AppleClang 16 and
LLVM/MLIR 21.1.6 toolchain. CMake reconfigured to include the new maintained
`checked_program_test.cpp` suite. This was an incremental build.

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec010-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec010-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build exits 0; full suites exit 8 for the unchanged known failures.
git diff --check
```

Both JUnit reports contain **182 tests, 174 passes, eight failures, and no crashes
or skipped tests**. Their failure names match SPEC-009 exactly. Source definitions
match discovery without duplicates; every test has a 30-second timeout. All 13
checked-program, 20 diagnostic, 49 parser, seven E2E, four dialect, eight harness,
and four generic IR-string tests pass. Formatter checks pass for the changed
implementation and new test files.

The contract tests verify scalar storage widths and function signatures with MLIR
verification, canonical aliases, signedness/bool identity, unsupported types in
every annotation position, void restrictions, distinct IDs for shadowed names,
resolved calls, and AST/semantic/module ownership after their producer objects
are destroyed. They also check duplicate/unresolved failures, unsupported target
widths, independence across runs, and failure instead of incorrectly typed
float/unsigned operations or return signatures. Compile-time assertions reject
raw AST use of normal `generate`, direct construction, and copying of a checked
program. The new E2E test returns 42 through aliases and a shadowed local.

The scalar registry belongs to the frontend; codegen lowers canonical IDs rather
than resolving source type strings. The checked backend uses declaration IDs and
rejects legacy string/name lookup. Its normal module no longer receives unused
experimental runtime declarations. Old stage tests retain their feature assertions
through `generate_unchecked_for_testing`; this bypass is absent from the compiler
pipeline. Unknown expression types no longer receive the old i32 fallback.

This task establishes the contract, not complete language validation. The checker
still processes functions in declaration order; SPEC-011 collects declarations and
settles complete scope rules. Constants, definite initialization, contextual
numeric literals, return-path analysis, and supported operator semantics remain
SPEC-012 through SPEC-015. Explicit codegen guards prevent unfinished float,
unsigned-ordering/division, and return-type behavior from producing the wrong
operations; they do not mark those later tasks complete. Full IR verification,
lowering, and JIT remain SPEC-018/SPEC-019.


### SPEC-011 verification

Rebuilt the existing SPEC-009 Debug directory with AppleClang 16 and LLVM/MLIR
21.1.6. CMake reconfigured to include `tests/scope_test.cpp`. This was an
incremental build, not a fresh-build or hosted-CI claim.

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build \
  -R '^(ScopeTest|CheckedProgramTest|E2ETest)\.' --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec011-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec011-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build and 36 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **198 tests, 190 passes, eight failures, and no crashes
or skipped tests**. The failing names match SPEC-010 exactly and remain listed
in [tests/README.md](tests/README.md). Maintained source definitions (excluding
commented-out code) match CTest discovery without duplicates; all timeouts remain
30 seconds. Formatter checks pass for the changed C++ implementation/tests.

Eleven new scope tests check forward signatures with MLIR verification, forward
argument errors, duplicate and shadowing rules, initializer lookup, isolation of
block/branch/while/function locals, parameter mutability, collection errors before
body checking, built-in name protection, and independent compiler invocations.
Semantic-error cases return no module and retain source locations. Existing
alias tests now use the function-first ID order without changing their type
assertions; symbol IDs remain opaque and program-local.

Five new E2E programs verify IR, lower through the external MLIR tools, and assert
execution results. A forward call chain with a void call returns 42, direct
recursion returns 21, mutual recursion returns 42, and initializer/shadowing and
sibling/loop-scope programs return 42. The mutual-recursion test exposed a nested
`if` continuation defect: codegen inspected branch-entry blocks instead of their
final blocks. Branch completion now uses the final block and removes empty
unreachable continuations, covering both nested then/else paths. This prerequisite
repair does not complete SPEC-016's full control-flow contract.

The selected core has no user-defined types to collect; its type registry is
available before signatures. Aggregate declaration collection remains deferred
with SPEC-024/SPEC-031 through SPEC-034. Constants, definite initialization,
contextual literals, return analysis, and `for` initializer lifetime remain
SPEC-012 through SPEC-017 as applicable. The legacy stage-only checker retains
its experimental aggregate path; it does not certify core programs.


### SPEC-012 verification

Rebuilt the existing SPEC-009 Debug directory with AppleClang 16 and LLVM/MLIR
21.1.6. CMake added the frontend constant evaluator and maintained variable test
suite. Verification used an incremental build.

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build \
  -R '^(VariablesTest|DiagnosticsTest|ScopeTest|CheckedProgramTest|E2ETest)\.' \
  --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec012-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec012-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build and all 80 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **222 tests, 214 passes, eight failures, and no crashes
or skipped tests**. Failure names match SPEC-011 exactly. Maintained source
names match CTest discovery without duplicates; all timeouts remain 30 seconds.
Formatter checks pass for the changed C++ implementation/tests.

The 17 new variable tests check malformed targets, explicit annotations,
initializer/store compatibility (including signedness), uninitialized reads,
delayed immutable assignment, returning and nested branch joins, conservative
loop behavior, shadowed declarations, and verified storage for all core scalar
types. Assignment targets do not read their old values; the right-hand value
is checked before the target becomes initialized. Negative cases fail before
codegen and retain source locations.

Constant tests cover lexical dependencies and duplicate names, runtime
binding/call rejection, operator type errors, integer overflow and zero
divisors, short-circuiting, f32 rounding, negative zero, no runtime
storage/arithmetic, and ownership/state isolation between compiler invocations.
The existing diagnostic regression now uses a runtime call in a constant
initializer; unchecked codegen continues to reject unevaluated constants.

Seven new E2E programs verify generated IR, lower with external MLIR tools, and
assert execution results: delayed initialization, initialization in both branch
arms, a returning branch, loop-local initialization, global/local constants,
short-circuited constant arithmetic, and folded numeric/boolean expressions.
Six return 42; the constant-shadowing program returns 44. The loop-local case
exposed a missing backedge after a nested branch. Codegen now inspects the
final loop-body block and removes empty unreachable continuations; SPEC-016's
full control-flow acceptance remains open.

The constant subset and dependency/initialization rules were recorded in
SPEC.md before implementation. Current literal-based constants use i32/f32/bool;
SPEC-013 retains contextual numeric typing and conversions. Runtime arithmetic
failure behavior, full return/unreachable-source analysis, aggregates, and
for-loop scope remain their subsequent tasks. No later feature is counted as
complete by this prerequisite work.


### SPEC-013 verification

Rebuilt the existing SPEC-009 Debug directory with AppleClang 16 and LLVM/MLIR
21.1.6. CMake added the numeric conversion/context implementation and maintained
numeric test suite. This was an incremental build.

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build \
  -R '^(NumericTest|VariablesTest|DiagnosticsTest|ScopeTest|CheckedProgramTest|E2ETest)\.' \
  --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec013-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec013-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
ninja -C /private/tmp/gloinc-spec009-build -t commands gloinc
otool -L /private/tmp/gloinc-spec009-build/gloinc
# Build and 97 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **239 tests, 231 passes, eight failures, and no crashes
or skipped tests**. Failure names match SPEC-012 exactly. Maintained source names
match discovery without duplicates; all test timeouts remain 30 seconds. Formatter
checks pass for the new numeric files and formatted semantic, parser, backend,
and test changes. Existing AST and stage-test formatting was preserved.

Twelve new numeric tests verify every integer width's boundaries, signed minima,
full u64, decimal/hex/binary equivalence, full consumption, preserved oversized
parser spellings, contextual types across every supported position, typed operand
anchors, default types, and explicit rejection of unsupported conversions/casts.
Variable and constant boundary fixtures inspect exact IR values and verify
storage/signature widths. Constant arithmetic now checks overflow/underflow at
all integer widths and uses binary32/binary64 floating arithmetic.

Eight external execution probes compare exact returned float bits using a
test-only IR adapter. They cover ties-to-even, just-above-tie decimal values that
would fail with intermediate host-double rounding, smallest subnormals, and
negative zero in both formats. Each compiled source function passes through the
checked pipeline; the adapter supplies observation operations without adding
Gloin floating comparisons or cast syntax. Five new E2E cases execute numeric
base forms, narrow/wide contexts, signed minima, full u64, resolved-width integer
constants, and f64 precision/range. Every new execution returns 42.

Numeric AST values no longer carry host integer/double approximations. The
checked program owns resolved APInt/APFloat literals and constants; codegen
consumes those values without reparsing. Frontend numerical support links the
same shared LLVM library already used by the backend. The compiler link command
contains one shared LLVM, shared MLIR/ExecutionEngine, and no overlapping LLVM
component archives; dynamic dependencies agree.

The spec decisions precede the implementation: literal-only expressions receive
context; typed operands retain their types; context-free defaults remain i32/f32;
integer and float categories stay separate. Floating literals permit finite
rounding/subnormals but reject nonzero values rounding to zero. Cast syntax and
typed widening/narrowing remain unsupported under the fixed first-release
feature list. 128-bit types remain deferred under SPEC-013b. Runtime arithmetic
failure behavior, general operators, full returns/entry points, and remaining
control flow stay SPEC-014 through SPEC-017.


### SPEC-014 verification

Run on Apple Silicon macOS with LLVM/MLIR 21.1.6 using the existing Debug build:

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build \
  -R '^(FunctionsTest|NumericTest|VariablesTest|DiagnosticsTest|ScopeTest|CheckedProgramTest|E2ETest)\.' \
  --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec014-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec014-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build and 114 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **256 tests, 248 passes, eight failures, and no crashes
or skipped tests**. The failure names match SPEC-013 exactly. Maintained source
names match discovery with no duplicates and all timeouts remain 30 seconds.
Formatter checks pass for the changed implementation and formatted test files;
the diagnostic fixture received only a source-string correction.

Twelve new function tests verify all scalar result types, exact signedness and
widths, missing values/fallthrough, nested returning blocks/branches, conservative
loop reachability, void return rules, forbidden void values, unreachable source,
call arity/types/direct-name rules, parameter scope/immutability, and entry-point
signatures. IR assertions verify scalar call/return signatures and once-only,
left-to-right argument emission. Source failures retain locations and expose no
module; empty executable input has no AST source span. Top-level returns fail
through both the source parser and the AST checking API. Reused Sema instances
cannot retain a prior entry point.

Executable mode requires `main() -> i32` (including `int`); module mode permits
helper-only input and validates any `main` that is present. All E2E invocations
now request executable mode before generation/execution. Five new E2E programs
exercise all nested returning arms, explicit/implicit void returns, discarded
call results, loop returns with fallback paths, typed value parameters and nested
calls. Four return 42; the entry-alias fixture returns -1 as a valid result.
Existing direct/forward/mutual recursion and eight float-bit execution probes
continue to pass.

Return-signature fixtures now expect semantic rejection instead of codegen
rejection. The codegen-disposal fixture uses a boolean-returning helper for its
unsupported runtime `!=` expression, preserving its original stage-failure
assertion. No later-feature tests were disabled or weakened. The spec fixes
return/entry behavior before implementation and preserves SPEC-015 operators,
SPEC-016/017 control flow, and SPEC-018 through SPEC-020 lowering/JIT/CLI work.


### SPEC-015 verification

Run on Apple Silicon macOS with LLVM/MLIR 21.1.6 using the existing Debug build:

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 \
  -R '^(OperatorsTest|FunctionsTest|NumericTest|VariablesTest|DiagnosticsTest|ScopeTest|CheckedProgramTest|E2ETest)\.' \
  --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec015-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec015-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build and 130 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **272 tests, 264 passes, eight failures, and no
skipped tests or test-process crashes**. The failure names match SPEC-014 exactly.
Maintained source names match discovery with no duplicates and all timeouts
remain 30 seconds. Formatter checks pass for the changed implementation and
formatted test files. The build has only the existing generated MLIR deprecation
warnings and uses the same shared LLVM/MLIR targets.

Sixteen operator tests cover the valid scalar matrix, invalid operand categories,
unsupported syntax/operators through source and AST APIs, statement-only
assignments, and valid integer minima/maxima. Constant and runtime checking share
one operator-type table. Runtime operators use resolved signedness and widths;
integer arithmetic checks representability, division/remainder guards dangerous
operands before executing, and floats retain native-width rounding without
fast-math flags. Boolean right operands live on conditional control-flow paths.

The operator suite makes **103 external invocations: 37 return 42, and 66 trap as
expected**. Successful cases exercise every integer width, signed quotient and
remainder signs, high unsigned magnitudes, all comparisons, both float widths,
boolean truth tables, nested conditions/loops/stores/call arguments, and eight
exact runtime float-bit probes. The probes observe per-operation rounding,
subnormals, and signed zero via a test-only bitcast adapter. SPEC-013's eight
literal bit probes and all 29 E2E cases also continue to pass.

Trap cases cover overflow/underflow, signed-minimum negation/division/remainder,
zero integer divisors, both signs of floating zero divisors, and non-finite float
results. They require the runner's trap signal, excluding compiler/optimizer
errors and timeouts. Short-circuiting skips trapping calls and executes selected
right operands. Test-only LLVM instrumentation records helper calls as decimal
digits, verifying that skipped calls have no side effects and evaluated operands
and arguments run once, left to right. Source call/control-flow operations remain
unchanged; no language globals, references, or bitcasts are introduced.

The previous checked-operator rejection fixture now verifies successful typed
modules; invalid returns still fail in Sema. Codegen partial-module disposal is
exercised through the explicit legacy backend, where runtime `!=` remains
unsupported. No deferred feature assertions were disabled or weakened. The
normative operator decisions precede implementation; general control-flow work,
`unless`/`for`, and lowering/JIT/CLI remain SPEC-016 through SPEC-020.


### SPEC-016 verification

Run on Apple Silicon macOS with LLVM/MLIR 21.1.6 using the existing Debug build:

```sh
cmake --build /private/tmp/gloinc-spec009-build -j 2
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 \
  -R '^(ControlFlowTest|OperatorsTest|FunctionsTest|NumericTest|VariablesTest|DiagnosticsTest|ScopeTest|CheckedProgramTest|E2ETest)\.' \
  --output-on-failure
ctest --test-dir /private/tmp/gloinc-spec009-build -j 1 --output-on-failure \
  --output-junit /private/tmp/spec016-serial.xml
ctest --test-dir /private/tmp/gloinc-spec009-build -j 4 --output-on-failure \
  --output-junit /private/tmp/spec016-parallel.xml
ctest --test-dir /private/tmp/gloinc-spec009-build --show-only=json-v1
# Build and 142 focused tests exit 0; full suites exit 8 for known failures.
git diff --check
```

Both JUnit reports contain **284 tests, 276 passes, eight failures, and no skipped
tests or test-process crashes**. Failure names match SPEC-015 exactly. Maintained
source definitions match discovery without duplicates; all timeouts remain
30 seconds. Formatting and diff whitespace checks pass. The incremental Debug
build retains the existing generated MLIR deprecation warnings.

The initial ten-test control-flow regression run passed nine cases and failed
`BackendRejectsStatementsAfterTerminatedPaths`: raw codegen accepted operations
after returns. The new continuation contract fixes that regression. A return
clears the builder insertion point; statement/expression visitors reject missing
or terminated continuations. `if` lowering keeps only live arm-to-merge edges;
`while` lowering sends a live body continuation back to the original condition
header and omits backedges after returns. Function finalization adds implicit
void returns only to live continuations. Boolean conditions and SPEC-014's
semantic unreachable-source rejection are preserved.

All twelve control-flow tests pass. Nine external executions exercise every leaf
of nested branches/else-if chains, partial returns, nested loops with branches,
early returns, both-returning loop arms, empty bodies/arms/blocks, implicit void
returns, and conditions containing arithmetic guards and short-circuit branches.
Each module passes MLIR verification and structural CFG checks for exactly one
final terminator per block, successors in the same function, and no orphan
blocks. Structural reachability includes both edges of constant conditions.

Two executions instrument helper bodies with test-only LLVM counters, preserving
source branch/loop/call operations. They prove if conditions evaluate once,
unselected else-if conditions do not execute, while conditions run before the
first iteration and after each continuing iteration, and empty loop bodies keep
rechecking until false. Six programs return 42; the others assert 34, the exact
condition trace 1234343435, and four empty-loop condition evaluations. No language
global-variable or reference support is introduced by the instrumentation.

Existing test assertions and the eight deferred-feature failures are unchanged.
The shared lowering/verifier pipeline remains SPEC-018; this task verifies its
control-flow output with the existing external harness. `unless`/`for`, in-process
JIT recovery, and CLI integration remain SPEC-017/SPEC-019/SPEC-020.
