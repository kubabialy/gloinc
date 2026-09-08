# Gloin specification and compiler checklist

This is the implementation backlog for [SPEC.md](SPEC.md), based on the architecture audit of `mlir` at `8e25383` on 2026-09-07. Work through the numbered items in order. Each item has a stable ID so we can discuss, implement, and verify it separately.

**Next item: SPEC-005.** Completed items have verification evidence in the completion log. Existing partial implementations and results from temporary audit repairs do not count as completed work.

The first milestone is a reproducible build. The first working compiler milestone is SPEC-021: real source files passing through the CLI with reliable error handling. The proposed first release boundary and supported platforms are decided in SPEC-006; later features remain tracked even if they are outside that release.

## How to use this checklist

- Mark an item complete only when its acceptance criteria are met in the repository, relevant checks pass, and its evidence is recorded in the completion log.
- For a language feature, check syntax, semantic validation, generated IR, and observable execution where applicable. Include invalid-source cases. A token, AST node, non-null module, or printed operation name alone is insufficient.
- Unsupported syntax, types, and operations must produce diagnostics. Never silently omit them or substitute an unrelated type.
- Resolve language decisions in SPEC.md before implementing the affected behavior. This checklist does not silently change the language specification.
- Keep known failures visible and associate them with task IDs. Do not disable tests or weaken assertions just to report a green suite. Obsolete tests should be corrected against an explicit spec decision.
- If an item proves too large, split it into suffix IDs such as `SPEC-027a`; retain the parent ID and its acceptance criteria.

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

- [ ] **SPEC-005 — Add clean-build CI and accurate onboarding/status documents.**
  Document dependencies and commands in a root README, add CI for the selected development platform(s), and correct [example status claims](examples/README.md), [phase notes](examples/PHASE2_PROGRESS.md), and [OpenCode.md](OpenCode.md).
  **Done when:** CI builds from an empty directory and publishes the complete test results; documentation distinguishes working, partial, and unsupported features and contains no unsupported production-readiness or coverage claims. A failing full suite remains visibly failing.

## 2. Define the core contract and repair the frontend

- [ ] **SPEC-006 — Resolve core syntax ambiguities and choose the first release boundary.**
  Reconcile mandatory `def` with `const`, explicit type requirements, `int`/`usize` aliases, `string` versus `String`, visibility placement, statement terminators, and UTF-8 identifier rules. State the supported initial types/features/platforms and whether that release requires JIT, native compilation, or both. Record later decisions under their feature tasks.
  **Done when:** SPEC.md provides canonical examples and an explicit first release feature list. Implementation-only syntax is either documented as an extension or scheduled for rejection; deferred work stays on this checklist.

- [ ] **SPEC-007 — Introduce source-aware diagnostics and stop on errors.**
  Preserve source file/span information through tokens and AST nodes. Give parsing, semantic checking, and code generation explicit success/failure results. Route every error through the same diagnostic mechanism; unsupported AST nodes must fail explicitly.
  **Done when:** malformed input, unknown constructs, and non-boolean conditions report a useful location, set failure status, and prevent later compilation stages. No error is only printed to stderr while compilation reports success.

- [ ] **SPEC-008 — Repair lexer behavior against the agreed vocabulary.**
  Fix newline/comment handling, multi-character operators, and keyword recognition. Resolve existing `in`, range, `=>`, `spawn`, and `await` test expectations against the spec/extension decisions. Validate malformed numeric/string/character tokens and preserve accurate source positions.
  **Done when:** supported vocabulary has correct tokens and positions; unsupported vocabulary has deliberate behavior; the seven audited lexer failures are resolved without silently accepting obsolete syntax.

- [ ] **SPEC-009 — Make parsing complete and deterministic.**
  Standardize token consumption, require closing delimiters, and make error recovery always advance. Fix identifier conditions such as `if b { ... }`, operator precedence, and multiline struct/function parsing. Cover [parser.cpp](src/parser.cpp) with complete-program cases.
  **Done when:** ordinary multiline programs parse without diagnostics; truncated blocks/calls and malformed expressions fail; `if b` is not parsed as a struct literal; parser errors cannot leave a supposedly successful partial program.

- [ ] **SPEC-010 — Establish one resolved type/symbol contract for codegen.**
  Choose a typed AST, semantic side tables, or another explicit checked-program representation. Resolve types and declarations once and make codegen consume that information. Define ownership of AST, semantic data, MLIR context, and modules.
  **Done when:** semantic checking and codegen agree on primitive types and symbol identities, unknown types never default to i32, and codegen cannot accidentally process an unchecked program through the normal compiler path.

## 3. Make core language semantics and code generation reliable

- [ ] **SPEC-011 — Resolve declarations and scopes before checking bodies.**
  Collect function/type declarations before use, define duplicate/shadowing rules, and check references with lexical scope. Initialize all symbol properties, including parameter mutability.
  **Done when:** forward calls and recursion work under the chosen rules; duplicate declarations and unresolved names fail consistently; bindings do not leak across scopes or compiler invocations.

- [ ] **SPEC-012 — Enforce variables, constants, mutability, and initialization.**
  Require declared types, require constant initializers, validate assignment targets, and enforce mutability. Add definite-initialization checks and reject invalid initializer/store types before lowering. Apply aggregate and pointer rules as those features arrive.
  **Done when:** `1 = 2`, immutable assignment, uninitialized reads, and `def mut n: i8 = 3.14` fail before codegen; valid mutable assignments execute correctly. Constants behave according to the agreed compile-time rules.

- [ ] **SPEC-013 — Implement typed numeric literals and conversions.**
  Parse decimal/hexadecimal/binary integers with full-consumption and range checks. Support the chosen signed/unsigned widths and floating types without forcing every literal to i32/f32. Define overflow, narrowing, explicit casts, and literal compatibility in SPEC.md.
  **Done when:** `0x2A` and `0b101010` mean 42; oversized literals fail rather than becoming zero; integer and float boundary tests agree across Sema and codegen; allocation and store widths match.

- [ ] **SPEC-014 — Validate functions, calls, returns, and entry points.**
  Check argument count/types, parameter rules, return values, all reachable return paths, and `main`'s supported signature. Define implicit void return and unreachable-source behavior.
  **Done when:** incorrect/missing returns, top-level returns, invalid calls, and invalid entry points fail; direct/forward/recursive calls return correct values; no unchecked return signature reaches execution.

- [ ] **SPEC-015 — Implement the supported expression operators.**
  Align lexer/parser/Sema/codegen operator tables. Implement unary minus/not, arithmetic, comparisons, and boolean short-circuiting; choose signed, unsigned, and floating operations from resolved types. Specify division-by-zero, overflow, shifts, and evaluation order for the supported operator set.
  **Done when:** negation and inequality do not crash; float addition uses floating arithmetic; unsigned comparisons/division are correct; short-circuit tests prove that skipped operands have no side effects. Unsupported operators fail explicitly.

- [ ] **SPEC-016 — Correct nested if/while control flow.**
  Require boolean conditions and track the current insertion block after recursive generation. Stop emitting into terminated blocks and correctly terminate merge blocks and loop backedges.
  **Done when:** nested branches, loops containing branches, early returns, empty bodies, and unreachable statements produce valid IR and correct execution without missing terminators or accidental fallthrough.

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

- [ ] **SPEC-046 — Verify specification coverage and prepare the release.**
  Turn normative spec examples into complete executable or expected-error fixtures. Clearly identify conceptual examples and external dependencies. Finish installation/packaging, version/help information, platform setup instructions, and a feature matrix linked to acceptance tests.
  **Done when:** a fresh checkout can build/test/install by following the README; all tests required by the chosen release scope pass; every advertised feature has end-to-end evidence; remaining unsupported features are documented and rejected. Record serial/parallel results and applicable runtime sanitizer checks. No temporary audit patch, cached old binary, or machine-specific path is required.

## Specification coverage map

| Specification area | Checklist items |
| --- | --- |
| UTF-8, declarations, explicit typing, entry point | SPEC-006 through SPEC-014, SPEC-020 |
| Variables and constants | SPEC-012, SPEC-013 |
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
