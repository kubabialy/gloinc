# Packed storage and explicit allocation (0.3.0)

**Status: implemented in development; unreleased.** This extends the
[existing packed-record contract](packed-layout.md). See the
[0.3.0 draft](release-0.3.0-draft.md) for validation and release status.
Implemented: `alloc_aligned<T>`, `place_at<T>`, the shared `check_alignment<T>`
validator, and fixed packed integer arrays with literal, repeated and zeroed
construction, exact byte layouts, checked scalar indexing, embedding, and
representation-preserving copies/calls. Packed integer slices now borrow fixed
arrays with `array[start..end]`, support nested subranges, `.len`, indexing, and
read-only weakening. `storage_bytes<S>` and `check_slice_alignment<S>` provide
checked extent and element-storage alignment queries for ordinary and packed
slices. `view_packed<S>` now constructs bounded integer views over initialized
byte slices. `alloc_slice<S>` allocates and initializes ordinary or packed integer
slices with explicit base alignment. `split_packed<S>` checks byte-disjoint
partitions. Packed-record elements now support the same array/slice operations,
retaining complete record bytes and ordinary element references.
`copy_packed<S>` now provides checked overlap-safe subrange copies. Further
GloinIR bounds proofs and bounded dynamic reads are implemented and measured
on macOS ARM64. See the [development validation report](release-0.3.0-draft.md#packed-storage-development-validation-2026-11-10)
for macOS, Linux, sanitizer and relocated-package results and limitations.

## Types and representation

`packed(msb_first) [T; N]` and `packed(lsb_first) [T; N]` are fixed arrays.
Removing `; N` denotes a borrowed slice; `[const T]` removes element write
access. Order, element type and fixed length participate in type identity.
Ordinary arrays and slices retain their existing representation and stride.

Native integer elements occupy their declared width in bits, using the existing
packed-field bit mapping and width-bit two's-complement encoding. Qualified
integer elements occupy their declared bytes in their own byte order and begin
on byte boundaries. Packed-record elements retain their whole-byte extent,
including tail bits; their own field/bit order is independent of the array order.
Other element categories require a separate contract.
For example, eight `u13` elements occupy 13 bytes; eight `u3` elements occupy
three. Fixed-array storage has alignment one and rounds its total extent up to
bytes. Embedded packed arrays begin on byte boundaries and retain that extent.

Construction evaluates initializers in source order, encodes scalar values,
preserves existing aggregate representations, and zeroes unused trailing bits.
Repeated initialization evaluates its fill once, even for zero elements.
Arithmetic and integer constructors keep their existing exact-width checks.
Increasing allocation alignment does not change element stride or encoding.

## Borrowing, indexing and lifetimes

A packed slice conceptually contains a pointer to its first containing byte,
a normalized starting bit offset in 0..7, and an element count. Encoding and
stride are static type information. Construction validates the complete extent
and representability of address calculations without overflowing intermediate
products. Empty views perform no memory access. Their internal descriptor need
not preserve a dereferenceable address.

Slicing checks `0 <= start <= end <= len`, evaluates its operands once, and
adjusts the descriptor without allocation or element reads. Slicing preserves
bit position and encoding. `.len` counts elements. Indexing checks bounds before
access. Indexed assignment evaluates the destination and index, checks bounds,
then evaluates its right-hand side and performs the write, as ordinary arrays do.

Reading an integer yields its declared value type. Writing changes only the
element's bits and preserves all neighboring bits. Ordinary scalar pointers and
references cannot represent packed elements: taking their address is rejected,
including implicit generic borrows. Whole packed arrays and records remain
addressable. Packed-record elements also admit ordinary references with alignment
one and the view's write capability; scalar fields within them still cannot be
borrowed as ordinary scalar references. No implicit ordinary/packed slice or
bit-order conversion exists.

Views borrow storage. Descriptor copies alias the same owner; they neither
allocate nor extend its lifetime. A read-only view prohibits writes through that
view but does not freeze other aliases. Owner reset/free invalidates all views.

## Physical access and concurrency

For an element at bit `b` with extent `w`, its access footprint is the half-open
byte interval `[floor(b/8), ceil((b+w)/8))`. Reads and writes may not touch bytes
outside this footprint, even if those bytes belong to the same allocation.
An implementation may use bounded word accesses inside the footprint with no
stronger alignment assumption than it can prove. Boundary-byte writes are
read-modify-write operations. Ordinary packed operations are not atomic.

Unsynchronized accesses conflict if their byte footprints overlap and either
access writes. Distinct logical elements can therefore conflict. Concurrent
reads are permitted; disjoint-byte accesses may proceed independently, subject
to valid lifetimes and publication/synchronization. This defines the storage
boundary for future concurrency support, not a thread runtime or race detector.

Ordinary subranges provide no exclusivity guarantee.
`memory.split_packed<S>(view, index)` returns `result<memory.PackedSplit<S>>` with
`left` and `right` views. It rejects a split between two nonempty views unless
the split is byte-aligned.
For integer stride `W` and initial bit offset `s`, the condition is
`(s + k * W) % 8 == 0`. Zero-based `u3` and `u13` views can split every eight
elements. Success proves disjoint footprints of the returned pair only; other
aliases remain possible. It does not authorize `noalias` metadata.
`PackedSplit<S>` is an ordinary result carrier, not an exclusivity capability;
manually constructing one does not establish disjointness.
The helper preserves encoding, read-only capability and borrowed ownership; it
reads or writes no backing bytes. Out-of-range positions return `OUT_OF_RANGE`
before boundary checks; a shared boundary byte returns `INVALID`, both with OS
error zero. Splits at either end succeed, including for an empty view at an
interior bit offset. The boundary calculation uses reduced offsets and widths
so it never computes a potentially overflowing full bit product.

Whole-object copies preserve every representation byte.
`memory.copy_packed<S>(destination, source)` returns `result<void>` and requires
a writable packed destination type `S` and a matching read-only source encoding
(writable source views may weaken). Both views must have equal element counts.
A mismatch returns `OUT_OF_RANGE` with OS error zero before touching bytes.
Success behaves as if all source element values were captured before any write:
copying preserves destination boundary bits and works for overlapping views in
either direction, including different bit offsets. Packed-record elements copy
every representation byte. Empty and zero-width copies access no backing bytes.
Schema/encoding changes require explicit value conversion. Copying does not
grant synchronization or make concurrent overlapping access valid.

## Allocation and placement APIs

New APIs return checked results. Existing allocation APIs keep their signatures.
The calls are:

```gloin
a.alloc_aligned<T>(value, alignment)
a.alloc_slice<S>(count, fill, alignment)
memory.place_at<T>(bytes, byte_offset, value, alignment)
memory.view_packed<S>(bytes, bit_offset, count)
memory.storage_bytes<S>(count)
memory.split_packed<S>(view, index)
memory.copy_packed<S>(destination, source)
```

`alloc_aligned<T>` returns `result<&T>`. `alloc_slice<S>` takes a complete writable
ordinary or packed slice type and returns `result<S>` initialized with one
captured fill. The arena owns the resulting storage. `place_at<T>` receives a
writable byte slice and returns `result<&T>`; it constructs a value, unlike
`view_packed<S>`, which borrows initialized bytes without changing them.
Read-only bytes cannot produce a writable view. `storage_bytes<S>` returns
`result<u64>` for the minimal zero-offset byte extent of `count` elements.
Its result is bounded by `INT64_MAX` bytes on the supported 64-bit targets.
Calculation divides the packed count before multiplying, so a full bit count
larger than `u64` does not spuriously overflow when the byte count fits.
Zero-sized ordinary and packed-record elements require zero bytes for every
count; this query does not itself construct a view or allocate storage. Allocation
of zero-sized elements accepts the full `u64` count range and performs no fill loop;
the fill argument still evaluates once. Signed indices and range bounds retain
explicit nonnegative checks even for these large slices. `check_slice_alignment<S>`
checks the stored elements' minimum alignment (one for packed storage), rather
than the borrowed descriptor's native alignment.

`view_packed<S>` accepts `[u8]` for writable `S` and `[const u8]` for read-only
`S` (writable bytes may weaken to read-only). Endian-qualified and packed-record
elements require a byte-aligned start, including for empty views. Empty native-integer views may
start at an interior bit or exactly one past the byte buffer, but never beyond
it. Construction reads/writes no backing bytes. Its error precedence is invalid
element alignment, then unrepresentable extent, then insufficient capacity or
out-of-range position. An empty byte buffer admits only offset zero/count zero.
A nonempty view of zero-sized packed records requires a nonempty backing byte
slice; a byte-aligned one-past start is allowed. This retains non-null backed
element references without reading or writing bytes.

Alignment arguments are `u64` byte counts, positive powers of two, at least the
stored type's minimum alignment, and within the target addressable range.
They apply to the allocation base, not every element. Placement checks the
actual destination address against the requested alignment. Placement offsets,
view bit offsets, and element counts are `u64` and retain their stated units.

Invalid alignment/type-independent parameters return `status.INVALID`;
unrepresentable size calculations return `status.OVERFLOW`; insufficient byte
capacity or out-of-range offsets return `status.OUT_OF_RANGE`; allocation
failure returns `status.NO_MEMORY`. These errors use OS code zero. Static type
and capability violations remain compilation errors. Result inspection follows
the ordinary checked-result rules.

Invalid allocation requests perform no allocation; allocation failure publishes
no storage. Failed placement writes nothing. Initializer arguments have already
been evaluated according to ordinary call evaluation order. Empty fixed-array
placement must obey the existing non-null reference contract; zero-length views
grant access to no elements.
`place_at` requires a nonempty backing byte slice even for an empty object;
zero-sized placement at its one-past address is allowed when aligned. See the
[implemented placement contract](raw-memory.md#checked-explicit-placement-030-development).

## GloinIR and optimization requirements

Keep checked view construction, subranges, element selection, packed loads and
stores, fill, and copy explicit through GloinIR. Element selection must not
fabricate an ordinary scalar pointer. Verifiers preserve type identity,
capabilities, encoding, bounds and effect contracts. A masked store has both
read and write effects; trapping checks remain effectful.

Fixed-array indexing uses `gloin.packed_array_select` to check bounds and produce
an internal `!gloin.packed_slot` with the element's encoding and write capability.
`gloin.packed_element_load` and `gloin.packed_element_store` consume that location;
it cannot become an ordinary scalar pointer. `gloin.packed_slot_address` accepts
only packed-record slots, preserves their access capability, and checks the
non-null reference contract, including for zero-sized records. Repeated
construction uses
`gloin.packed_array_repeat` with one captured fill. Packed slice construction,
subranges and indexing retain explicit `gloin.packed_slice_from_array`,
`gloin.packed_slice_subrange` and `gloin.packed_slice_select` operations. Slice
index/range checks retain the source signedness of their bounds; byte/bit
advancement divides before multiplying to avoid overflowing a full bit position.
`gloin.checked_packed_view` validates byte-backed construction and returns an
ordinary checked result; error payloads come from the Gloin wrapper.
`gloin.initialize_slice` validates writable raw storage and extent before any
write, fills the element representation, zeroes packed trailing bits, and only
then publishes the descriptor. Arena policy and allocation errors remain in Gloin.
`gloin.packed_slice_copy` retains matching encodings and read/write effects,
checks equal counts, then selects traversal direction using byte addresses and
normalized bit offsets. Each iteration captures the whole source element before
storing it through the bounded packed access operations.
Split validation is Gloin code using existing checked subranges and the private
`gloin.packed_slice_bit_offset` descriptor query.
Native integer reads up to 56 bits use a machine-sized window: the guaranteed
prefix is read in bounded chunks and one final byte is read only if the element
crosses into it. Wider reads and masked stores retain specialization over the
eight possible bit positions. Byte-aligned qualified integers and records use
their direct bounded accesses. Whole-byte strides use byte multiplication;
nonbyte strides retain division before multiplication to avoid bit-count overflow.

The existing GloinIR promotion handles packed descriptors only when their stack
slots have exact loads/stores and no escaping address. Bounds proofs apply to
that exact SSA descriptor and index, or to a fixed array's static extent. They
remain internal transient proofs invalidated on rewritten or erased operations;
textual IR cannot request unchecked accesses. Signed nonnegative guards remain
independent of unsigned upper-bound proofs. See the
[packed indexing benchmark](packed-storage-benchmark.md) for the saved-compiler
comparison and independent output checks.

Constant indices and static widths/orders allow address folding. Sequential
loops can advance byte/bit cursors and specialize repeating bit positions.
Bounds elimination requires proof for the exact SSA view and index. A view's
read-only capability is not proof of immutable memory. Reassignment, alias
mutation and effectful calls must not retain invalid proofs or cached loads.

Combining accesses or vectorizing requires proof of valid initialized footprints,
alias behavior, and preservation of evaluation, writes, and required traps.
Allocation capacity alone never justifies widening into neighboring bytes.
Failed loop-versioning predicates select the original checked loop; they must
not introduce an earlier trap. Code size and sequential/random-access costs
must be measured independently before claiming performance improvements.

## Implementation and verification sequence

1. Checked explicit alignment and bounded placement, with failure/no-write tests.
2. Fixed packed arrays, exact bytes, scalar indexing, and embedded endian arrays.
3. Bounded packed views, subranges, allocation, and checked byte-boundary splits.
4. Verified GIR optimizations with alias and failure-order regressions.
5. Core, platform, sanitizer, installed-package and final release validation.

Use independent byte/bit oracles, all bit offsets, empty ranges, exact capacity,
overflow, signed boundaries, neighboring-bit canaries, overlapping copies and
guard pages. Exercise JIT, native `-O0` and native `-O2`. Inspect memory footprints
and GIR rejection of forged capabilities/layouts. Race-free native peer probes
may test disjoint footprints; deliberately racing programs cannot establish
correctness. Preserve baseline/raw performance evidence and failed runs.
