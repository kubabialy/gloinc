# Compiler diagnostics and stage boundaries

`compile_source(text, filename, context)` in [compiler.h](../src/compiler.h)
runs parsing, semantic checking, and high-level MLIR generation in order. Each
stage must succeed before the next starts. Its `CompilationResult` contains an
owned module on success, structured diagnostics, and the failed stage on error.
A failed result never exposes a partial module. The caller's MLIR context must
outlive the result. This is the API for the eventual file-reading CLI; lowering,
JIT execution, and the CLI remain SPEC-018 through SPEC-020.

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
code generation join the same diagnostic collection. Complete IR verification
and the lowering/JIT diagnostic paths remain SPEC-018/SPEC-019.

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
scope checking; return analysis, constant evaluation, and other semantic rules
remain in their subsequent tasks.
Newly parsed constants fail explicitly in Sema/codegen pending SPEC-012.

The external E2E harness now uses `compile_source` before verification and tool
execution. Stage-isolated codegen tests intentionally bypass semantic checking,
but must check parser and codegen status. Such tests establish only the behavior
of those stages, not end-to-end language support.
