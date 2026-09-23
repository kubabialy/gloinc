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
rejected before engine creation except for the exact runtime ABIs:
`gloin.runtime.output(ptr, i64) -> void`, `malloc(i64) -> ptr`, `free(ptr) -> void`,
and the four [arena runtime operations](arenas.md#library-and-compiler-boundary),
all with external linkage, C calling convention, and no variadic arguments.
The JIT registers output and native allocation callbacks explicitly. Allocation
supports SPEC-027's pending-call records and SPEC-028's explicit arena API. This
does not introduce general FFI. Source functions named `malloc`/`free` or with
arena runtime symbol names receive separate internal linkage
names, preserving ordinary source lookup without a runtime symbol collision.

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
gradual underflow. SPEC-023 adds byte-length-aware standard output; write errors
return an execution diagnostic without an i32 result. Output already written
remains visible if a later error or trap occurs. Concurrency and global variables
remain deferred. File loading, result/exit conventions, and
check/IR modes are documented in [the CLI reference](cli.md). Source-file acceptance
is covered by [SPEC-021's fixtures](../tests/fixtures/core/README.md); installation
and packaging are documented in [the release guide](release.md).

Codegen and the JIT share one thread-safe native-target initialization and layout
query. Source-generated modules carry that native triple and data layout, so
ordinary struct storage and JIT execution use the same target rules (SPEC-024).

Nullable pointer loads, stores, field access, and checked reference creation
trap on null using the existing runtime-trap mechanism. The JIT does not track
resource lifetimes or diagnose dangling non-null pointers (manual memory, SPEC-025).

SPEC-027 adds normal-return defer cleanup, including cleanup on functions that
return a nonzero result. Fatal traps do not unwind. Captures are recorded in
per-invocation heap records and freed while draining; the JIT explicitly binds
native allocation functions with the validated ABIs above. No global defer
state survives an invocation. Output errors still surface after invocation,
following normal function cleanup.
