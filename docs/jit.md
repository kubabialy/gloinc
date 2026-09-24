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


SPEC-030 also registers and validates the exact parse-i32, format-i32, bounded
stdin, and zero-byte-initialization ABIs. Source declarations with native symbol
spellings are mangled separately. Standard input statuses stay in program values;
they do not turn into JIT errors or replace `main`'s result. The shared runtime
supplies byte output for external runners, while the JIT retains its existing
flush/error-reporting callback. See [standard-library APIs](standard-library.md).

SPEC-030a additionally validates and binds the void native byte-copy ABI
`gloin_strings_copy(pointer, i64, pointer)`. Length queries and guarded byte/slice
access lower directly; string library result statuses do not alter JIT entry
results. The shared runtime exports the same copy routine for external execution.
See [byte-string usage, ownership, and costs](strings.md).

SPEC-030c adds 21 numeric native routines for wide integer/float/bool parsing,
formatting, and checked conversion. `stdlib_abi.h` shares their primitive
signatures; `stdlib_lowering.h` derives exact LLVM function types for JIT validation.
All return i32 status with explicit output pointers, avoiding native aggregate
return ABI differences; parsed booleans use a u8 output. Float arguments retain
their f32/f64 width, and conversion modes are i32. Wrong widths, varargs, or
user-supplied bodies for reserved runtime symbols are rejected before binding.
The static JIT runtime and shared external runtime use the same implementation.
See [numeric API semantics and costs](numbers.md).

SPEC-030d extends the same signature validation and explicit symbol binding to
seven I/O routines. Opaque FILE pointers never become public language-level raw
handles. `io.gloin` owns shared close-state records in a caller arena; native
operations return statuses/errno/progress through scalars and output references.
Descriptor construction is guarded in lowering. Standard streams remain process
resources shared with existing `std` calls; no JIT-wide registry or cleanup list
silently closes application files. See [I/O ownership and errors](io.md).

SPEC-030e adds `JitRunner::run(module, arguments)` with a default empty vector.
An invocation-owned copy supplies the thread-local argument context only for entry
execution; previous bindings restore on return. Embedded NUL is rejected before
invocation. The CLI inserts the exact supplied source filename at index zero and
forwards only values after FILE --. External hosts can bind explicit scopes via
installed `context_runtime.h`; unconfigured external execution sees zero arguments.
The runtime never imports host/compiler argv implicitly. Environment and cwd are
host process resources; argument scopes do not isolate them. See
[ownership and native scope APIs](filesystem-process.md).

SPEC-030g extends the entry API to `JitRunner::run(module, arguments, clock)`.
The optional `const GloinClockSource*` defaults to null, explicitly selecting the
OS monotonic clock for that invocation. A non-null descriptor is copied, while
its userdata remains borrowed until the synchronous call returns. The callback
must not throw. A thread-local scope restores the previous host binding on
normal return and host exception unwinding; invalid providers fail before entry
execution. Binding allocates one small native record per invocation; reads do
not allocate in the runtime. Fatal traps retain the process-termination behavior
above. Installed `time_runtime.h` exposes the same LIFO scope API to native hosts.
The JIT validates and binds both the monotonic-read and SplitMix64 native ABIs;
random state stays entirely caller-owned. See [clock contracts and injection](time-random.md).
