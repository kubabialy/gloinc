# Verified IR and shared LLVM lowering

`compile_source` returns verified high-level IR by default. Its optional fifth
argument, `CompilationOutput::LLVM`, returns a verified LLVM-dialect module:

```cpp
auto result = compile_source(text, filename, context,
                             CompilationMode::Executable,
                             CompilationOutput::LLVM);
if (!result.success()) {
    result.diagnostics->render(std::cerr);
    return 1;
}
// result.module owns verified LLVM-dialect IR; context must outlive it.
```

Executable mode checks the source `main() -> i32` contract; choosing LLVM output
does not itself execute a program. Module mode still permits helper-only input.
The default high-level output preserves `func`, `arith`, `cf`, and LLVM operations
for inspection and tests of compiler stages.

## One conversion pipeline

[lowering.cpp](../src/lowering.cpp) owns the only conversion pass list:

1. SCF to control flow.
2. Control flow to LLVM.
3. Arithmetic to LLVM.
4. Functions to LLVM.
5. Final memref conversion to LLVM.
6. Reconcile unrealized conversion casts.

`lower_to_llvm` verifies the input before conversion, enables verification after
each pass, checks final legality, and verifies the final module. Input operations
must be registered `func`, `arith`, `cf`, `scf`, `memref`, or `llvm` operations,
plus the root builtin module and temporary unrealized conversion casts. Nested
modules and all Gloin/custom operations fail explicitly. Types nested in
signatures, operands/results, block arguments, and attributes are checked too.
An accepted input dialect is not a promise that every operation can be lowered;
unsupported conversions still fail.

Successful output contains only the root builtin module and registered LLVM
operations with LLVM-compatible types. High-level operations, custom types,
and unrealized casts cannot cross this boundary. The LLVM constant operation's
index-typed integer payload is allowed because LLVM's converter/exporter uses
it with a concrete integer result; runtime index types remain forbidden. Raw
SCF/memref fixtures exercise this internal compiler capability without adding
source-language arrays or structured IR syntax.

The legacy JIT and `run_external_module` test helper both lower owned clones
through this function. The external helper then uses `mlir-opt` only to
parse/verify the serialized LLVM module and `mlir-runner` to execute it. It no
longer maintains a separate conversion pipeline. Low-level subprocess harness
tests retain `run_external_mlir` for already-lowered IR and controlled fake tools.

## Ownership and errors

`verify_module` borrows its module. `lower_to_llvm` consumes an
`mlir::OwningOpRef<mlir::ModuleOp>` and returns ownership only after success.
Failure destroys partial IR and returns an empty owner. Consumers retaining
high-level IR explicitly clone it before lowering. The caller owns the MLIR
context and keeps it alive through every module's lifetime.

Malformed input IR receives a `Verification` diagnostic. Unsupported operations,
conversion errors, and final legality failures receive `Lowering` diagnostics.
MLIR error handlers capture errors even if a pass reports success. Earlier
source errors keep their lexical/parsing/semantic/codegen stage and stop before
verification. Existing errors prevent a new successful lowering result.

File/line/column locations survive lowering. Available owned source text is
mapped back to byte spans. Imported IR without source text uses the diagnostic's
explicit position; it does not manufacture source text. Unknown locations render
as `<unknown>:1:1`. Consumers render collected errors at their boundary.

## Verification and remaining work

`LoweringTest` checks real LLVM IR export and LLVM verification, every scalar
signature, internal wide-integer overflow checks, nested source control flow,
SCF/memref conversion, ownership, source positions, unsupported IR, unresolved
casts, and failed conversion. A source program is compiled in three independent
contexts, producing identical printed LLVM-dialect IR and returning -1 on all
three external executions. This establishes that fixture's repeatability on the
supported toolchain, not cross-platform or byte-identical native binaries.

All existing external language tests now use production lowering, including
arithmetic trap tests. JIT translation registration, validated invocation, and
separate execution failure/results remain SPEC-019. The file CLI remains
SPEC-020; source-file acceptance and release packaging remain SPEC-021/SPEC-046.
