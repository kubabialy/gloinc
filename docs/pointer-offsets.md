# Pointer offsets (development branch)

A nullable pointer `*T` can be advanced by a signed `i64` number of elements:

```gloin
def main() -> i32 {
    def mut values: [i32; 3] = {10, 20, 12};
    def first: *i32 = &values[0];
    def second: *i32 = first + 1;
    return *second + *(second + 1);
}
```

This returns 32. Run the [complete example](../examples/pointer_offsets.gloin)
with `gloinc --jit examples/pointer_offsets.gloin`, or build it with
`gloinc -O2 -o pointer-offsets examples/pointer_offsets.gloin`.

`p + offset` preserves the pointer's pointee type and read-only capability.
It accepts only a `*T` data pointer on the left and an `i64` element count on
the right; integer literals are inferred as `i64`. A negative offset moves
backward. `&T`, `void`, and function pointers cannot be offset. Convert an
addressable element to `*T` explicitly through a typed binding, as above.
There is no pointer subtraction, pointer difference, ordering, or pointer
indexing.

The compiler traps if `p` is null, including when the offset is zero. It does
not know an allocation's length and does not check the resulting address.
The caller must keep both `p` and its result within the same live allocation
or at its one-past-the-end address. Address arithmetic overflow, an offset
outside that range, and dereferencing a one-past or misaligned address are
invalid program behavior; the compiler does not promise a trap for these cases.
Offsetting or dereferencing after an arena reset/free is also invalid. As with
existing references, the compiler does not track lifetimes or aliases.
Fixed-array `values[index]` retains its own
runtime bounds check; use that form when the array and index are available.

LLVM lowering uses a regular typed GEP without `inbounds`, because the
compiler cannot prove the allocation rule for an arbitrary `*T`. This feature
does not change the fixed-array bounds check or grant an aliasing guarantee.
