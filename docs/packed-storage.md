# Packed storage and explicit allocation (0.3.0)

**Status: accepted design; implementation in progress.** This extends the
[existing packed-record contract](packed-layout.md). Syntax and APIs in this
document are requirements, not evidence that the compiler supports them yet.

## Types and representation

`packed(msb_first) [T; N]` and `packed(lsb_first) [T; N]` are fixed arrays.
Removing `; N` denotes a borrowed slice; `[const T]` removes element write
access. Order, element type and fixed length participate in type identity.
Ordinary arrays and slices retain their existing representation and stride.

Native integer elements occupy their declared width in bits, using the existing
packed-field bit mapping and width-bit two's-complement encoding. Qualified
integer elements occupy their declared bytes in their own byte order and begin
on byte boundaries. Packed-record elements retain their whole-byte extent,
including tail bits. Other element categories require a separate contract.
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
addressable. No implicit ordinary/packed slice or bit-order conversion exists.

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

Ordinary subranges provide no exclusivity guarantee. A checked partition helper
must reject a split between two nonempty views unless the split is byte-aligned.
For integer stride `W` and initial bit offset `s`, the condition is
`(s + k * W) % 8 == 0`. Zero-based `u3` and `u13` views can split every eight
elements. Success proves disjoint footprints of the returned pair only; other
aliases remain possible. It does not authorize `noalias` metadata.

Whole-object copies preserve every representation byte. Packed subrange copies
preserve bits outside their destination and have overlapping-copy semantics.
Schema/encoding changes require explicit value conversion. Copying does not
grant synchronization or make concurrent overlapping access valid.

## Allocation and placement APIs

New APIs return checked results. Existing allocation APIs keep their signatures.
The proposed calls are:

```gloin
a.alloc_aligned<T>(value, alignment)
a.alloc_slice<S>(count, fill, alignment)
memory.place_at<T>(bytes, byte_offset, value, alignment)
memory.view_packed<S>(bytes, bit_offset, count)
memory.storage_bytes<S>(count)
```

`alloc_aligned<T>` returns `result<&T>`. `alloc_slice<S>` takes a complete writable
ordinary or packed slice type and returns `result<S>` initialized with one
captured fill. The arena owns the resulting storage. `place_at<T>` receives a
writable byte slice and returns `result<&T>`; it constructs a value, unlike
`view_packed<S>`, which borrows initialized bytes without changing them.
Read-only bytes cannot produce a writable view. `storage_bytes<S>` returns
`result<u64>` for the minimal zero-offset byte extent of `count` elements.

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

## GloinIR and optimization requirements

Keep checked view construction, subranges, element selection, packed loads and
stores, fill, and copy explicit through GloinIR. Element selection must not
fabricate an ordinary scalar pointer. Verifiers preserve type identity,
capabilities, encoding, bounds and effect contracts. A masked store has both
read and write effects; trapping checks remain effectful.

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
