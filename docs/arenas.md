# Arena allocation (SPEC-028)

`import "@arena";` loads [arena.gloin](../stdlib/arena.gloin). Its first allocator
is `arena.GeneralArena`; future allocator types can live in the same module.

```gloin
import "@arena";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def value: &i32 = memory.alloc(40);
    *value = *value + 2;
    return *value;
}
```

This returns 42. `alloc(value)` copies an initialized value into properly aligned
storage and returns a writable reference. Its type comes from the value, so use
an explicitly typed local when a literal's default `i32`/`f32` type is unsuitable.
The receiver and value are evaluated once, in that order. Struct and string
copies are shallow; referenced memory does not become owned by the arena.

`try_alloc(value)` returns a nullable pointer instead. It returns null if storage
allocation fails; initializer side effects have already occurred. `alloc` and
`create` trap on allocation failure. Fatal traps do not run deferred cleanup.

Growth never moves existing objects. `reset()` invalidates all objects and keeps
all blocks for reuse, including large blocks. Never use old references after a
reset, even if the same addresses are reused. `free()` releases blocks and the
control object, and clears the receiving handle. Repeating free on that cleared
handle is harmless; allocation/reset through it trap.

Copying an arena handle creates an alias, not an independent owner. Designate one
owner and pass `&arena.GeneralArena` to helpers. Freeing through that owner makes
all other handle copies dangling; there is no automatic dangling-handle check.
Reset/free do not destroy objects or release external resources referenced by
their fields. Register their cleanup after `defer memory.free()` so LIFO cleanup
runs it while the arena is still live. Arenas have no internal synchronization.

Run the larger example with:

```sh
./build/gloinc examples/arena_lab.gloin
```

It checks 10,000 linked particles per frame over four reset cycles, stable
addresses, padded fields, methods, fallible allocation, and an independent arena.
It prints `arena lab: ok` and exits zero. Adjust `PARTICLES` for larger workloads.

## Library and compiler boundary

Lifecycle methods and allocation policy are ordinary source in `arena.gloin`.
Until generic functions are specified, the checked declarations
`GeneralArena.alloc` and `GeneralArena.try_alloc` form a narrow typed bridge.
Their library implementation signature is
`(self: &GeneralArena, size: u64, alignment: u64) -> *u8`; source callers must pass
one initialized value, never size/alignment. Sema validates that exact public
instance-method signature and binds the resolved method identity. An unrelated
type or module's `alloc` method keeps ordinary call semantics.

The compiler evaluates the receiver and initializer, measures the initializer's
native layout, calls the library method with size/alignment, and stores the value
only on success. It guards `alloc` against a null result even if an overridden
library returns one. Deferred allocation captures the receiver and value at
registration, then performs allocation/initialization during cleanup.

Only `@arena` source can call `__arena_general_create`, `__arena_general_alloc`,
`__arena_general_reset`, `__arena_general_destroy`, and `__arena_require`.
The require primitive emits a non-null trap guard. The other primitives use the
C ABI declared in [arena_runtime.h](../src/arena_runtime.h). The LLVM-independent
[native runtime](../src/arena_runtime.cpp) uses aligned bump allocation with
checked arithmetic, initially 64 KiB blocks, doubling the preferred block size
up to 1 MiB. Larger requests receive larger blocks. These sizes are tuning
choices. Each arena has its own allocation state and backing allocator.

The compiler links the runtime statically and registers its native symbols with
the JIT after exact ABI validation. Installation also includes
`lib/libgloin_runtime.a`, `lib/libgloin_runtime.dylib` on macOS, and
`include/gloin/arena_runtime.h`. External `mlir-runner` executions load the shared
runtime using `--shared-libs=/path/to/libgloin_runtime.dylib`.

The complete language contract is in [SPEC.md](../SPEC.md#arena-allocation-spec-028).
