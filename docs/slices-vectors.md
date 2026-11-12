# Borrowed slices and vectors

Slices and vectors first shipped in 0.0.4. Gloin 0.1.0 includes the vector slice
views, reserved storage, collection methods, and `@slices` helpers below.
Subsequent compiler fixes are identified explicitly.
Build `gloinc` and run the
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

Weakening applies to the outer slice only. A `[const [i32]]` view prevents
replacing its row descriptors, while each loaded `[i32]` row still permits
writes to its own elements. You can explicitly restrict a loaded row:

```gloin
def main() -> i32 {
    def mut values: [i32; 2] = {20, 22};
    def rows: [[i32]; 1]     = {values[..]};
    def view: [const [i32]]  = rows[..];
    def row: [const i32]     = view[0];

    return row[0] + row[1];
}
```

The `row` descriptor still borrows `values`; it does not copy or freeze the
buffer. Nested element types do not convert implicitly: `[[i32]]` cannot become
`[[const i32]]`. The direct row load above is fixed after 0.1.0
([PERF-009](compiler-validation.md)); published 0.1.0 needs an intermediate
`[i32]` binding before assigning to `[const i32]`.

Slice storage follows the existing manual pointer lifetime rules. Do not keep
a slice after its array goes out of scope or after its arena is reset or freed.
The compiler does not track lifetimes or aliases and cannot diagnose every
dangling slice. Slices are constructed from fixed arrays, other slices, and
explicit vector view methods. Raw pointers cannot be sliced from user code;
vectors do not implicitly convert to slices.

`import "@slices";` supplies generic `fill<T>([T], T)`, `reverse<T>([T])`, and
`copy_nonoverlapping<T>([T], [const T])`. Call them with explicit type arguments,
for example `slices.reverse<i32>(values[..])`. All are allocation free and
linear in the number of affected elements. Copy returns `false` without writing
if the destination is shorter than the source; the caller must ensure the two
ranges do not overlap. Values are copied shallowly, as in Gloin assignment.

Development 0.3.0 adds `slices.copy<T>(destination: [T], source: [const T]) -> bool`
for ordinary borrowed slices that may overlap. It copies exactly `source.len`
elements into the destination prefix, with the values the source held before
the copy, and leaves the destination tail unchanged. An undersized destination
returns `false` before any write; otherwise it returns `true`, including for an
empty source. Identical views, adjacent ranges and overlap in either direction
are supported. For example, `slices.copy<i32>(values[1..], values[..3])` shifts
the first three elements right in a four-element array.

This is an allocation-free O(source length) operation. A checked internal GloinIR
query chooses the copy direction from byte addresses in constant time, including
when typed placements overlap between element boundaries. It does not allocate a
temporary buffer or expose general pointer ordering. Element types must match,
including byte order.
Typed aggregate copies preserve their representation bytes, including padding;
pointer, string and slice elements retain their shallow-copy semantics. This API
does not accept packed slice descriptors; use their storage-specific copy API.
The copy is not atomic and provides no synchronization. Overlap handling assumes
live storage with no concurrent mutation of the source or access conflicting with
destination writes; it does not create a snapshot against other threads.

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
`append_from([const T])` checks capacity before copying, so failure leaves the
vector unchanged. Development 0.3.0 also preserves the original source values
for overlapping views, including placements with different element boundaries,
using `slices.copy`. Empty append succeeds even at capacity; it allocates nothing.
`insert(index, value)` shifts elements right and returns
false for an invalid index or full vector; `remove(index)` shifts left and
returns false for an invalid index. `truncate(length)` returns false for a
negative or growing length. `pop()` returns a nullable pointer to the removed
slot, or null when empty. Copy the value before mutating the vector again.
`as_slice()` borrows the live elements as `[T]`; `as_const_slice()` borrows them
as `[const T]`. The returned length is a snapshot, so a later push does not
extend an existing view. Even with unused capacity, the view never exposes
slots beyond `len()`.
Copying a fixed vector copies **all N slots and its length**. Pass `&Vector<T;
N>` to avoid large copies. Pointers returned by `get` or `get_mut` point into
that particular vector value and become invalid when it goes out of scope.
Assignment to a copied vector does not affect the original. A slice view points
into the particular vector value from which it was borrowed. Do not use the
view after that value goes out of scope or after its storage is replaced. After
`clear()`, make a new view before reading elements. The `fill` value
and stored elements follow Gloin's normal value-copy semantics; referenced
objects are not deeply copied. There is no automatic destructor for elements.

## Growable arena vectors

The 0.3.0 development library adds recoverable construction, growth and cloning. Prefer
these methods when allocation failure should be handled by the caller:

| Method | Result | Work, excluding allocation |
| --- | --- | --- |
| `Vector<T>.try_empty(memory, capacity)` | `result<Vector<T>>` | O(1); reserves uninitialized slots without requiring a fill value |
| `try_clone(memory)` | `result<Vector<T>>` | O(length); one allocation request in the chosen arena, capacity equals length |
| `try_reserve(requested)` | `result<void>` | No allocation within capacity; O(length) copying on growth |
| `try_push(value)` | `result<void>` | Amortized O(1), with O(length) copying on growth |
| `try_insert(index, value)` | `result<void>` | O(length − index), plus growth copying if needed |
| `try_append_from(source)` | `result<void>` | O(source length), plus growth copying if needed; at most one allocation request |

```gloin
import "@arena";
import "@vector";

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def made: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 0);
    if made.erroneous { return 1; }
    def mut values: vector.Vector<i32> = made.value;
    def pushed: result<void> = values.try_push(42);
    if pushed.erroneous { return 2; }
    return *values.get(0);
}
```

Capacity and insertion indices remain `i64`. Negative capacities return `INVALID`;
unrepresentable lengths or byte extents return `OVERFLOW`; insertion outside
the inclusive range zero through length returns `OUT_OF_RANGE`; allocation
failure returns `NO_MEMORY`.
All use OS error zero. Insertion checks the index before growth limits. Requests
are validated before allocation. A zero initial capacity still requests backed
storage and can return `NO_MEMORY`; it initializes no elements. Zero-sized element
types accept capacities through `INT64_MAX` without a capacity-sized fill loop.

Every failed new operation leaves the vector's pointer, length, capacity and
elements unchanged, so its existing views and aliases retain their prior state.
Argument evaluation happens before the call; its side effects are not rolled
back. Push/insert capture the value before growth or shifting. Empty append and
reserve within capacity allocate nothing. Growth for push/insert/append normally
doubles capacity, capped at `INT64_MAX / sizeof(T)` (or `INT64_MAX` for zero-sized
elements); a larger append can request its required capacity directly.

`try_append_from` checks the complete length and reserves once before writing.
It preserves source values for self-appends and overlapping views, including
aliases with different logical lengths and typed placements whose element
boundaries differ. It delegates copying to `slices.copy`, using the checked
byte-address direction query without allocating a temporary snapshot.
Element copies remain shallow and preserve aggregate
representation bytes. The existing arena ownership, view invalidation on growth
and independent counters in copied handles still apply. Invalid lifetimes and
programming errors are not converted into allocation errors. The older boolean
methods and inline fixed-capacity vector API remain available below.

`values.try_clone(&destination)` explicitly creates an independent backing buffer
and length/capacity counters in the chosen arena. It copies only the live elements,
preserving aggregate representation bytes, and sets capacity equal to length.
It accepts a read-only vector and leaves the source unchanged on success or
failure. Even an empty clone requests storage and can return `NO_MEMORY`.

Element copies are shallow: pointers, strings, slices and nested arena vectors
still refer to their original storage. Cloning does not extend those referents'
lifetimes or make them safe for concurrent mutation. A clone in a different arena
keeps its own buffer after the source arena is reset, but its referenced objects
must remain live independently. Cloning into the same arena is also supported;
both buffers then share that arena's lifetime. Ordinary assignment continues to
alias the existing buffer.

`import "@vector";` also provides `vector.Vector<T>`. Prefer
`Vector<T>.empty(&memory, capacity)` with an explicit arena and signed `i64`
capacity. It works for struct and enum elements that have no zero value.
`create(&memory, capacity, fill)` remains for compatibility with 0.0.4 source;
its `fill` expression is evaluated but no longer retained or copied into spare
slots.
It traps for a negative capacity, size overflow, or allocation failure. It
allocates aligned storage for the initial capacity without initializing spare
slots. Only `push` initializes an element, and accessors expose only elements
below the logical length. Spare slots must never be read.
A zero initial capacity is valid; the first push allocates one slot.

```gloin
import "@arena";
import "@vector";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def mut values: vector.Vector<i32> = vector.Vector<i32>.empty(&memory, 2);
    if !values.push(20) || !values.push(22) { return 1; }
    if !values.push(99) { return 2; } // capacity grows from 2 to 4
    def first: *const i32 = values.get(0);
    def second: *i32 = values.get_mut(1);
    if first == null || second == null { return 3; }
    def live: [const i32] = values.as_const_slice();
    return live[0] + live[1];
}
```

`len()` and `cap()` return `i64`. `push(value)` normally doubles capacity when
full, but adds one slot near the signed length limit; it returns `false` when
the length cannot grow further. Allocation failure traps. Its argument is evaluated before
growth. `reserve(requested)` allocates enough capacity when needed, returns
`false` for a negative request, and otherwise returns `true` (or traps on
allocation failure). `get(index)` returns a
nullable read-only pointer; `get_mut(index)` returns a nullable writable
pointer. Both return null for negative or out-of-range indexes. Check for null
before dereferencing. `clear()` sets the logical length to zero; it does not
overwrite elements or free capacity. `as_slice()` returns a writable `[T]` over
the live elements and `as_const_slice()` returns a read-only `[const T]`. Both
return empty views when `len()` is zero. Creation has constant initialization
cost aside from the allocator's work; `reserve` copies only the live elements
and takes O(length), and push is amortized O(1). Access is O(1).

The vector's arena owns its storage. Growth allocates a new buffer and copies
the live elements; the old buffer remains in the arena until reset or free.
Treat pointers from `get` or `get_mut` and slices from `as_slice` or
`as_const_slice` as invalid after growth or reserve that moves storage, and
all vector storage as invalid after arena reset/free. Copying a vector aliases
its current element storage **but copies the length and capacity counters**, so
mutation through one copy does not update the other. Keep one vector handle and
pass a pointer to it to helpers. Like other Gloin values, a `T` containing a
pointer or string copies that reference shallowly; the vector does not own
referenced memory or run destructors. There is no per-vector free. A slice view
captures the length at the time of the call; after `clear()`, discard old views
and create a new one. The compiler does not track borrow invalidation.

`append_from([const T])` copies a source slice and permits a view of the same
vector, including when growth moves its storage; the arena retains the old
block until reset/free. It returns false before writing if the final length
would exceed the signed limit. Development 0.3.0 prechecks that length in constant
time, reserves the complete destination before copying, and uses `slices.copy`
to preserve original source values for all supported overlaps. This includes
aliases with different lengths and placements with different element boundaries.
Growth makes at most one allocation request and produces the same final capacity
as repeated `push` calls, without allocating intermediate buffers. Empty append
and append within capacity allocate nothing. Storage byte-extent overflow and
allocation failure still trap; use `try_append_from` for recoverable errors.
Copies remain shallow and provide no synchronization, as described for slices.

`insert` and `remove` shift elements in O(length)
time and return false for invalid indexes; insert can grow and invalidate old
views. `truncate` changes only the live length and rejects a negative or
growing length. `pop()` returns a nullable pointer to the removed slot; copy
its value before the next vector mutation, which may overwrite or move it.
None of these operations destroys elements or frees arena blocks.

The underlying `GeneralArena.alloc_many(fill, capacity)` bridge is also
available directly. It returns `&T`, initializes the whole allocation, and
uses the same checked signed capacity and arena lifetime rules.
The raw reserved-capacity bridge used by `Vector<T>` is restricted to the
standard vector module; user code cannot obtain uninitialized elements through
either reserved allocation bridge.

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
