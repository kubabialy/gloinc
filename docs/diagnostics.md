# Compiler diagnostics and stage boundaries

`compile_source(text, filename, context)` in [compiler.h](../src/compiler.h)
runs parsing, semantic checking, MLIR generation, and verification in order. Each
stage must succeed before the next starts. Its `CompilationResult` contains an
owned module on success, structured diagnostics, and the failed stage on error.
A failed result never exposes a partial module. The caller's MLIR context must
outlive the result. Optional LLVM output uses the shared lowering pipeline.
JIT execution and the file-reading CLI remain SPEC-019/SPEC-020.

The optional fourth argument is `CompilationMode::Module` by default; pass
`CompilationMode::Executable` to require `main() -> i32` before generating IR.
Module mode permits helper-only source but still rejects an invalid `main`.
The fifth argument, `CompilationOutput::LLVM`, selects verified LLVM-dialect
output; the default `HighLevel` retains verified high-level IR.

[diagnostics.h](../src/diagnostics.h) defines the shared error representation.
Each diagnostic records its stage, message, and source span. Tokens and AST
nodes retain shared ownership of an immutable source file; their spans use
half-open byte offsets. Rendering computes one-based lines and byte columns
and writes `filename:line:column: error: message` to a caller-selected stream.
LF and CRLF each advance one line. A hand-built AST with no source metadata is
reported as `<unknown>:1:1`; the compiler does not invent a filename.

Errors are collected without implicit stderr output. Clients render them once
at their boundary. For example:

```cpp
mlir::MLIRContext context;
auto result = compile_source(source_text, source_filename, context);
if (!result.success()) {
    result.diagnostics->render(std::cerr);
    return 1;
}
// result.module owns the generated high-level module.
```

The existing stage APIs remain available for focused tests:

- `GloinParser::parse_checked_program()` returns an explicit success flag and
  the AST. On error, the AST is empty. Node-parsing methods return null on error;
  `has_error()` and `diagnostics()` expose the cause. The older `parse_program()`
  convenience method also discards partial output, but callers must inspect its
  diagnostic status rather than interpreting an empty vector as success.
- `Sema::check_for_codegen(std::move(ast))` consumes the AST and returns an owned
  `CheckedProgram` only on success. It retains resolved core types and declaration
  IDs. The legacy `check_program()` boolean and visitor APIs remain for stage tests;
  they cannot be passed as evidence of checking to normal codegen.
- `CodeGen::generate(checked_program)` returns an owned module on success and
  destroys partial modules on failure. The explicit `generate_unchecked_for_testing`
  entry retains the caller-owned raw-module API for deferred stage experiments.
  A CodeGen instance generates one module. Unsupported nodes/operators, unknown
  functions/types, and value uses of void calls fail explicitly.

[The checked-program contract](checked-program.md) defines AST, semantic-data,
module, and context ownership and the limits of the implemented checks.

Generated source operations use MLIR file/line/column locations. Synthetic
runtime declarations use unknown locations. MLIR errors emitted during
code generation join the same diagnostic collection. SPEC-018 adds `Verification`
and `Lowering` stages through [the shared IR pipeline](lowering.md). Errors from
MLIR retain real file/line/column positions even when imported IR has no source
text; owned source text provides byte spans when available. No partial module
is exposed on failure. JIT diagnostics remain SPEC-019.

SPEC-008 validates UTF-8 across the entire source, including comments and text
after a parsing error. Invalid encoding, NUL, BOM, non-ASCII identifiers, and
malformed literals report lexical errors with byte spans. Reserved words have
dedicated tokens; legacy `spawn`/`await` expressions fail explicitly in parsing.
Type keywords and quoted literals cannot substitute for each other.

SPEC-009 establishes consistent token consumption, core grammar/precedence, and
strict delimiters, annotations, and modifier order. [The parser contract](parser.md)
describes the default core mode and the explicit syntax-only API for deferred
stage tests. Parsing uses a terminal first-error policy: it discards the complete
program and never retries a failed token. Multi-error recovery is not implemented;
excessive recursive nesting reports a diagnostic.

SPEC-010 resolves core type and declaration identities once for codegen. This does
not complete the language checker. SPEC-011 adds function collection and lexical
scope checking. SPEC-012 adds definite initialization, typed stores, and
compile-time constant evaluation. Invalid constant expressions, arithmetic
failures, forbidden assignments, and uninitialized reads stop before codegen.
SPEC-013 moves numeric conversion entirely into semantic checking. Literal range,
underflow, and category errors retain the literal's source span, including unary
minus for a signed literal. Well-formed oversized spellings parse successfully
but cannot produce a checked program.

SPEC-014 rejects missing/mismatched returns, invalid calls, void-call values,
unreachable statements, and invalid entry signatures during semantic checking.
The source parser rejects file-scope returns; hand-built ASTs are rejected by
Sema. A missing executable entry is also a semantic error (empty ASTs have no
source span). These errors never reach codegen or execution.

SPEC-015 diagnoses invalid operator/type combinations and nested assignment
expressions in Sema. Constant arithmetic failures remain semantic errors;
runtime integer overflow, zero division/remainder, and non-finite floating
results follow explicit trap paths. The external harness reports runner failure
separately from returned i32 values. Intentional trap regressions check the
runner's trap signal, excluding compiler/optimizer failures and timeouts.

SPEC-016 preserves semantic rejection of unreachable source and adds a defensive
backend rejection for statements after terminated paths in raw stage tests.
Failed generation discards the partial module. Boolean condition diagnostics
retain the condition's source span; nested branch/loop lowering preserves the
current continuation through expression guards and short-circuit blocks.

SPEC-017 checks `unless` and `for` before generation. Non-boolean conditions keep
the condition span; unknown header/body names, invalid stores, uninitialized
reads, and missing returns remain semantic errors even on statically unexecuted
paths. Malformed headers, unsupported `unless ... else`, and deferred loop control
remain parsing errors. Raw AST initializers other than bindings or expression
statements fail explicitly in both Sema and the backend.

The external E2E harness uses executable-mode `compile_source`, then the shared
production lowering pipeline before tool execution. Stage-isolated codegen tests intentionally bypass semantic checking,
but must check parser and codegen status. Such tests establish only the behavior
of those stages, not end-to-end language support.
