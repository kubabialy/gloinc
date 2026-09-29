# Borrowed slices and vectors (0.0.4)

These features first ship in 0.0.4. Build `gloinc` and run the
[slice](../examples/slices.gloin), [inline vector](../examples/fixed_vector.gloin),
and [arena vector](../examples/vector.gloin) examples.

## Borrowed slices

`[T]` is a writable borrowed view of contiguous `T` elements. `[const T]` is a
read-only view. A slice stores a pointer and a `u64` length. It does not own or
copy the elements. Copying or passing a slice copies this small descriptor and
continues to refer to the same storage.

```gloin
def sum(values: [const i32]) -> i32 {
    def mut total: i32 = 0;
    def mut index: u64 = 0;
    while index < values.len {
        total = total + values[index];
        index = index + 1;
    }
    return total;
}

def main() -> i32 {
    def mut values: [i32; 4] = {1, 2, 3, 4};
    def middle: [i32] = values[1..3]; // 2, 3
    middle[0] = 20;                   // values[1] is now 20
    return sum(values[..]);           // 28
}
```

`array[start..end]` borrows an exclusive-end range. Either bound may be omitted:
`array[..]`, `array[1..]`, and `array[..3]`. The same forms work on a slice and
produce another borrowed slice. Bounds must be integers; at runtime the compiler
checks `0 <= start <= end <= source length` and traps on failure. `slice[index]`
checks the index against its current length and traps if out of bounds. Empty
slices and empty ranges are valid; they have no accessible element. `.len` is
read-only and has type `u64`.

A fixed array must be addressable to borrow it. An immutable array gives a
`[const T]` view; it cannot be converted to `[T]`. A writable slice can be
weakened to `[const T]`. Making the **slice binding** immutable only prevents
replacing its descriptor; it does not remove write access to its elements.
`[const T]` removes that access. The compiler checks these capabilities when
assigning through an index.

Slice storage follows the existing manual pointer lifetime rules. Do not keep
a slice after its array goes out of scope or after its arena is reset or freed.
The compiler does not track lifetimes or aliases and cannot diagnose every
dangling slice. Slices are currently constructed from fixed arrays and other
slices; raw pointers and vectors do not implicitly convert to slices.

## Inline fixed-capacity vectors

`vector.Vector<T; N>` has an inline `[T; N]` buffer and an `i64` logical length.
`N` is a compile-time `usize` argument and part of the type. It can be zero and
must be at most 1,048,576. `create(fill)` copies `fill` into all `N` slots,
including unused slots, then sets the logical length to zero. No arena or heap
allocation is involved. The capacity is known during compilation, so the
compiler can specialize its layout and operations for that capacity.

```gloin
import "@vector";

def main() -> i32 {
    def mut values: vector.Vector<i32; 2> = vector.Vector<i32; 2>.create(0);
    if !values.push(20) || !values.push(22) { return 1; }
    if values.push(99) { return 2; } // full: length stays 2
    def first: *const i32 = values.get(0);
    def second: *const i32 = values.get(1);
    if first == null || second == null { return 3; }
    return *first + *second;
}
```

`len()` and `cap()` return `i64`. `push(value)` returns `false` at capacity and
does not allocate. `get` and `get_mut` return nullable pointers; `clear()` sets
length to zero without overwriting slots. Indexes are checked by the methods.
Copying a fixed vector copies **all N slots and its length**. Pass `&Vector<T;
N>` to avoid large copies. Pointers returned by `get` or `get_mut` point into
that particular vector value and become invalid when it goes out of scope.
Assignment to a copied vector does not affect the original. The `fill` value
and stored elements follow Gloin's normal value-copy semantics; referenced
objects are not deeply copied. There is no automatic destructor for elements.

## Growable arena vectors

`import "@vector";` also provides `vector.Vector<T>`. `create` takes an explicit
`&arena.GeneralArena`, a signed `i64` initial capacity, and a `T` fill value.
It traps for a negative capacity, size overflow, or allocation failure. It
allocates aligned storage for the initial capacity and copies `fill` into
**every** slot before returning, including slots outside the logical length.
A zero initial capacity is valid; the first push allocates one slot.

```gloin
import "@arena";
import "@vector";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def mut values: vector.Vector<i32> = vector.Vector<i32>.create(&memory, 2, 0);
    if !values.push(20) || !values.push(22) { return 1; }
    if !values.push(99) { return 2; } // capacity grows from 2 to 4
    def first: *const i32 = values.get(0);
    def second: *i32 = values.get_mut(1);
    if first == null || second == null { return 3; }
    return *first + *second;
}
```

`len()` and `cap()` return `i64`. `push(value)` doubles capacity when full,
then writes one element; it returns `false` only if doubling would exceed the
signed length limit. Allocation failure traps. Its argument is evaluated before
growth. `reserve(requested)` allocates enough capacity when needed, returns
`false` for a negative request, and otherwise returns `true` (or traps on
allocation failure). `get(index)` returns a
nullable read-only pointer; `get_mut(index)` returns a nullable writable
pointer. Both return null for negative or out-of-range indexes. Check for null
before dereferencing. `clear()` sets the logical length to zero; it does not
overwrite elements or free capacity. Creation and reserve take O(capacity)
time because all slots are initialized. Push is amortized O(1); access is O(1).

The vector's arena owns its storage. Growth allocates a new buffer and copies
the live elements; the old buffer remains in the arena until reset or free.
Treat pointers from `get` or `get_mut` as invalid after growth or reserve, and
all vector storage as invalid after arena reset/free. Copying a vector aliases
its current element storage **but copies the length and capacity counters**, so
mutation through one copy does not update the other. Keep one vector handle and
pass a pointer to it to helpers. Like other Gloin values, a `T` containing a
pointer or string copies that reference shallowly; the vector does not own
referenced memory or run destructors. There is no per-vector free or direct
vector-to-slice conversion yet.

The underlying `GeneralArena.alloc_many(fill, capacity)` bridge is also
available directly. It returns `&T`, initializes the whole allocation, and
uses the same checked signed capacity and arena lifetime rules.

`Vector<T; N>` and `Vector<T>` are distinct types with different layouts and
ownership. There is no implicit conversion between them. Choose the fixed
form when a useful maximum is known and copying its inline buffer is
acceptable; choose the arena form when the sequence must grow. A runtime
capacity passed to `Vector<T>.create` cannot specialize the type or eliminate
its arena allocation.

## Repeated fixed-array initializer

`{value; N}` fills a declared `[T; N]` array with `N` copies of one evaluated
value. `N` must be a decimal literal or a compile-time size parameter and must
match the array's declared length. This form is also used by the fixed vector
constructor. It works for any copyable `T`; unlike `zeroed`, it does not
require `T` to have a language-defined zero value.
