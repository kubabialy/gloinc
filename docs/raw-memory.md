# Raw memory and user-written arenas

This API has been available since 0.1.0. The
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

## Checked explicit placement (0.3.0 development)

`check_alignment<T>(alignment: u64) -> result<void>` validates a positive
power-of-two byte alignment at least `align_of<T>()` and no greater than the
target's signed addressable range (currently 64-bit). Invalid requests return
`status.INVALID` with OS error zero, without accessing storage.

`storage_bytes<S>(count: u64) -> result<u64>` computes the zero-offset storage
extent for an ordinary or packed slice type. Ordinary elements use native slot
sizes; packed integers use their bit widths, rounding only the final extent to
bytes. Sizes above `INT64_MAX` bytes return `OVERFLOW`, without an intermediate
arithmetic trap. This query does not allocate or borrow. Empty ordinary element
types have zero byte extent for every count.

`check_slice_alignment<S>(alignment: u64) -> result<void>` validates a positive
power of two, at least the stored elements' minimum alignment and no greater
than `INT64_MAX`. Packed storage has minimum alignment one, independently of
the slice descriptor's alignment. Invalid requests return `INVALID`.

`view_packed<S>(bytes, bit_offset: u64, count: u64) -> result<S>` borrows initialized
bytes as a packed integer slice, without reading or writing the backing memory.
Writable `S` requires `[u8]`; read-only `S` accepts `[const u8]`. Offset is in
bits and count in elements. Endian-qualified elements require a byte-aligned
offset, even for an empty view. The complete extent is checked before a view is
returned. Error precedence is `INVALID` for element alignment, `OVERFLOW` for
unrepresentable extent, then `OUT_OF_RANGE` for position/capacity. Empty views
may start at an interior bit or exactly one past the buffer; empty buffers allow
only zero offset/count. Copying the descriptor retains the same borrowed owner.

`place_at<T>(bytes: [u8], byte_offset: u64, value: T, alignment: u64) -> result<&T>`
constructs a value in bounded writable storage. It first validates alignment,
then the remaining byte capacity, then address-offset representability, then
the destination address's requested alignment. Insufficient capacity returns
`OUT_OF_RANGE`; an unrepresentable offset returns `OVERFLOW`; a misaligned
address returns `INVALID`. All errors use OS error zero and leave storage
unchanged. Arguments, including the initializer, have already been evaluated.

Even a zero-sized value needs a nonempty backing slice so a live non-null address
can be obtained. Such a value may be placed at the slice's one-past address if
that address meets its requested alignment. An empty backing slice returns
`OUT_OF_RANGE`. The returned reference borrows the byte storage and does not
extend its lifetime. Placement retains representation-preserving copy semantics.
The requested alignment affects placement only, not the type's layout or stride.

Validation and error policy are written in Gloin. The verified
`gloin.raw_is_aligned` operation checks an address without reading memory or
exposing pointer-to-integer conversions to source code. Typed initialization
continues through `gloin.raw_place`.
