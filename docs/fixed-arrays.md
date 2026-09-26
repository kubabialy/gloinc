# Fixed arrays (0.0.3)

Fixed arrays have a known element type and length. They are available in 0.0.3;
the 0.0.2 release does not contain them. See the
[0.0.3 HTML guide](site/0.0.3/index.html) for a language overview.

Run [the example](../examples/fixed_arrays.gloin) with
`./build/gloinc --jit examples/fixed_arrays.gloin`; it prints `sum = 42`.
The [acceptance program](../tests/fixtures/core/run/fixed_arrays.gloin) also
exercises literal arguments and returns, nested arrays, struct elements,
references to elements and whole arrays, and once-only index evaluation in
both JIT and native execution.

```gloin
def sum(values: [i32; 2]) -> i32 {
    return values[0] + values[1];
}

def main() -> i32 {
    def mut values: [i32; 2] = {1, 2};
    values[0] = 40;
    return sum(values);
}
```

`[T; N]` is the type of exactly `N` values of `T`. `N` is a decimal whole-number
literal. The current compiler accepts 0 through 1,048,576 elements per array
type; this is an implementation limit. `T` may be a supported scalar, pointer,
ordinary struct, or another fixed array; `void` and function types are not
elements. `[i32; 2]` and `[i32; 3]` are different types. No implicit conversion
or array-to-pointer decay occurs.

`{...}` constructs an array in a context that supplies its type, such as a
typed binding, function return, argument, or struct field. It must contain
exactly `N` elements compatible with `T`. An empty array uses `[T; 0] = {}`.
Nested arrays use nested braces, for example
`def grid: [[i32; 2]; 2] = {{1, 2}, {3, 4}};`. Bracket literals such as
`[1, 2]` are not supported. Ordinary struct literals keep their named form,
such as `Cell { value: 1 }`.

Indexing is zero based. Any signed or unsigned integer type may be an index;
an untyped integer literal uses `usize` (`u64`). Every indexed read or write
checks `0 <= index < N` at runtime and traps before accessing memory when the
index is invalid. The base is evaluated once, then the index once. Indexed
assignment checks the destination before evaluating its right-hand side. A
zero-length array has no valid index. Indexed assignment requires writable
storage, such as a `def mut` binding or a writable struct field. An array must
be initialized as a whole before any element is read or written;
element-by-element initialization is not supported.

Arrays are values: assigning, passing, and returning an array copies its
elements. `&values[0]` creates a reference to a live element and follows the
same manual lifetime rules as other references. Pointers to arrays are not
pointers to their first element. A pointer to an element can use the explicit
offset rules in [pointer offsets](pointer-offsets.md).
Array layout follows the target's ordinary contiguous array layout, including
the element type's alignment and padding.

Slices, vectors, maps, and variable-length arrays are separate future work
after generics. Fixed arrays have no `.length` member or slice operator yet;
the length is part of their type.

## Development addition after 0.0.3: `zeroed`

The current development compiler accepts `zeroed` wherever an explicit
fixed-array type supplies the context:

```gloin
def mut grid: [[i32; 100]; 100] = zeroed;
grid[99][99] = 42;
```

It initializes every element to zero (`false` for `bool`, `null` for a nullable
pointer, and an empty string for `string`). The string zero value has a null
data pointer and zero length; string operations must accept that as empty text.
It works recursively for nested arrays and for an empty array of any valid
element type. Arrays of non-null references and structs are currently rejected.
`zeroed` has no type on its own: `def x: i32 = zeroed;` is invalid. The array
still occupies its full storage, and initializing a large local array uses
memory proportional to its size. `zeroed` adds no heap allocation or lazy
capacity reservation.

`def mut grid: [[i32; 100]; 100];` already reserves local storage without
initializing it, but the definite-initialization rule prevents element writes
until the whole array has been assigned. A separate `init` keyword is not
defined for fixed arrays. Vector and slice capacity/length rules remain for
their later design.
