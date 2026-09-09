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
- `Sema::check_program()` returns `bool`. All detected semantic errors contribute
  to that result, including non-boolean conditions, invalid struct declarations,
  and unsupported nodes. `get_errors()` remains a compatibility message view.
- `CodeGen::generate()` returns a null module on failure and destroys any partial
  module. A successful raw module is owned by the caller; `compile_source`
  immediately wraps it in `mlir::OwningOpRef`. A CodeGen instance generates one
  module. Unsupported nodes/operators, unknown functions/types, and value uses
  of void calls fail explicitly. Resolved void calls remain valid statements.

Generated source operations use MLIR file/line/column locations. Synthetic
runtime declarations use unknown locations. MLIR errors emitted during
code generation join the same diagnostic collection. Complete IR verification
and the lowering/JIT diagnostic paths remain SPEC-018/SPEC-019.

This establishes failure propagation for the checks currently implemented; it
does not complete the language checker. Reserved vocabulary, UTF-8 validation,
parser grammar/precedence, resolved type identities, return checking, and other
semantic rules remain in their subsequent tasks. Newline token handling and
missing-delimiter/terminator checks were repaired here because reliable source
locations and rejection of partial parses depend on them. Parser failures stop
at the first diagnostic; multi-error recovery remains SPEC-009.

The external E2E harness now uses `compile_source` before verification and tool
execution. Stage-isolated codegen tests intentionally bypass semantic checking,
but must check parser and codegen status. Such tests establish only the behavior
of those stages, not end-to-end language support.
