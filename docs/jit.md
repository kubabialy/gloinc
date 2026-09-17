# In-process JIT execution

`JitRunner::run(module)` executes a defined `main() -> i32` using the shared
SPEC-018 lowering pipeline. The caller retains its module and MLIR context;
the JIT lowers an owned clone and releases the clone and execution engine before
returning. Both high-level and already-lowered compiler output are accepted.

```cpp
auto compiled = compile_source(text, filename, context, CompilationMode::Executable);
if (!compiled.success()) {
    compiled.diagnostics->render(std::cerr);
    return 1;
}
auto executed = JitRunner::run(*compiled.module);
if (!executed.success()) {
    executed.diagnostics->render(std::cerr);
    return 1;
}
int32_t program_result = *executed.value;
```

`ExecutionResult` separates an optional signed i32 `value` from owned diagnostics
and the optional `failed_stage`. Every i32, including -1 and both limits, is a
valid successful result. Failure has no value and is never represented by a
sentinel integer. Verification/lowering failures retain their stage; entry,
translation, engine, and invocation errors use `Execution`. The JIT does not
print routine messages or implicitly render collected diagnostics. Errors retain
available IR file/line/column locations.

## Entry validation and invocation

Before invoking code, the JIT requires a defined, non-variadic `main` with no
parameters and a signless i32 result. It must use C calling convention and
external, internal, or private emitted linkage. Source `priv` does not prevent
execution. Helper functions also require C calling convention and emitted
external/internal/private linkage. External function/global declarations are
rejected before engine creation; the core does not resolve user symbols from
ambient process libraries or provide an FFI.

Native target/assembly-printer registration runs once. Builtin and LLVM dialect
translation interfaces are registered in the caller's context. The translated
LLVM module passes LLVM verification before native code generation. The existing shared LLVM/MLIR linkage remains unchanged.

A compiler-generated adapter calls `main`; MLIR's `invokePacked` interface writes
its result into an actual `int32_t` slot. There is no unchecked cast of a native
function address. Both adapter and future packed-wrapper names are checked for
collisions. User functions such as `_mlir_main` remain valid, and imported IR
cannot hijack invocation by defining the adapter's preferred name.

Runs use fresh engines. Tests repeat the same borrowed module and use test-only
LLVM globals to prove no engine state survives into the next run. Separate
contexts can run concurrently; the tests check independent results. The caller
must keep its context alive throughout each synchronous invocation. These tests
establish repeatability on the supported Apple Silicon macOS/LLVM 21.1.6 platform,
not cross-platform or native-file reproducibility.

## Runtime failure

Arithmetic guards retain SPEC-015's explicit LLVM trap semantics. A trap
terminates the calling process, so `run` does not return an `ExecutionResult` or
integer value in that case. The JIT installs no signal recovery handlers and
provides no rollback, timeout, or cleanup guarantee after a trap. Intentional
integer/float trap regressions run the in-process JIT inside test subprocesses
and require SIGTRAP or SIGILL; ordinary exits for compile/setup failure do not
satisfy them. This does not change successful in-process execution into an
external-runner implementation.

The floating-point environment must retain the specified default rounding and
gradual underflow. There is no source-language I/O, concurrency, or global-variable
feature added by JIT support. File loading, result/exit conventions, and
check/IR modes are documented in [the CLI reference](cli.md). Full source-file acceptance and release packaging
remain SPEC-021/SPEC-046.
