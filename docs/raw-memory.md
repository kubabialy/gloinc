# Raw memory and user-written arenas

This API is available in the current development tree after 0.0.4. The
versioned 0.0.4 guide describes the released language and does not include it.
Import `@memory` to own individual native blocks and build allocation policies
in Gloin. The complete [bump arena example](../examples/custom_arena.gloin)
allocates one block, places values of different types, handles exhaustion,
resets its cursor, and frees the block.

```gloin
import "@memory";

def main() -> i32 {
    def block: *u8 = memory.alloc(64, 16);
    if block == null { return 1; }
    defer memory.free(block);
    def value: *i32 = memory.place<i32>(block, 64, 42);
    if value == null { return 2; }
    return *value;
}
```

`alloc(bytes: i64, alignment: i64) -> *u8` returns a nullable pointer to an
uninitialized block. Alignment must be a positive power of two; negative sizes,
invalid alignment, overflow, and allocation failure return null. A successful
zero-byte allocation has a distinct address but grants no readable byte.
`free(pointer: *u8)` releases an allocation; passing null is harmless. Pass
only the **original** pointer returned by `memory.alloc`, exactly once. Never
free an interior pointer, a pointer from `GeneralArena`, or a pointer into an
array. `free` does not run destructors for placed values.

`size_of<T>() -> i64` and `align_of<T>() -> i64` report the native layout of
`T`, including array elements and struct padding. The compiler computes these
from the target layout. `padding<T>(address: *u8) -> i64` returns the number of
bytes to advance to align that address for `T`; null returns `-1`. For example,
a custom arena can compute `cursor = base + used`, ask for padding, verify that
padding and `size_of<T>()` fit in its remaining capacity, and advance its
cursor after successful placement.

`place<T>(storage: *u8, available: i64, value: T) -> *T` checks for null,
alignment, and `available >= size_of<T>()`. On failure it returns null and
does not write; on success it copies `value` into the block and returns a
nullable typed pointer. The value expression is evaluated before the checks.
The caller must truthfully report how many writable bytes remain at `storage`
and ensure the block stays live. The compiler cannot verify that a raw pointer
belongs to an allocation, detect overlapping placements, or prevent reading
bytes before they are initialized. A placed value follows ordinary Gloin copy
rules, so pointers and strings inside it are copied shallowly.

After `free`, every raw or typed pointer into that block is invalid. A custom
arena's `reset` can invalidate earlier values even while it retains its block;
the programmer defines and enforces that policy. Gloin has no lifetime or
alias tracking for raw blocks. `defer memory.free(block)` runs on normal
function exit, including an early return, but runtime traps do not run defers.

The runtime exports `gloin_memory_alloc` and `gloin_memory_free` with checked
C ABIs for the JIT and native executables. Source code reaches them through
the declarations in [memory.gloin](../stdlib/memory.gloin); this does not add
general FFI. Alignment padding and typed placement remain distinct
`gloin.raw_padding` and `gloin.raw_place` operations in checked GloinIR.
Lowering checks size and alignment, conditionally stores the initializer, and
converts the operations to LLVM. The native ABI is declared in
[memory_runtime.h](../src/memory_runtime.h).
