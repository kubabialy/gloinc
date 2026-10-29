# Packed memory layouts for 0.3.0

**Status: implemented in development; unreleased.** This is the checked-language
contract for packed records, exact-width integers, and checked integer
constructors. See the [0.3.0 draft](release-0.3.0-draft.md) for validation and release
status and the [endianness contract](endianness.md) for byte-order types.

## Representation model

- A packed struct describes a region of memory with exactly defined bytes on
  every supported architecture. It has nominal struct identity and named fields.
- The packed type explicitly selects its bitfield order, independently of the
  byte order of fields such as `be_u32` and `le_u32`.
- `u13` and `u3` are real integer value types. Arithmetic checks their actual
  widths, including when a value is outside a packed struct.
- Integer constructors such as `u16(value)` perform explicit value conversion.
  Every constructor returns `result<T>`; out-of-range inputs return `OVERFLOW`.
  They do not silently truncate or reinterpret signedness.
- The existing separation between construction, typed copy, typed conversion,
  and raw copy continues to apply. Copies preserve the complete representation,
  including through Gloin by-value arguments and returns.

## Syntax and exact layout

```gloin
def packed struct(msb_first) Header {
    def length: u13,
    def flags: u3,
    def sequence: be_u32,
}
```

`msb_first` and `lsb_first` are required choices. The parentheses specify bit
order, replacing the experimental parser's integer backing-type syntax.
Ordinary field visibility and mutability rules continue to apply.

Fields occupy increasing offsets in declaration order. There is no implicit
alignment padding. The record has alignment one, and its size is its total bit
extent rounded up to whole bytes. Arrays of these records use that byte size as
their element stride. A native struct containing a packed record can still have
its own ordinary alignment and padding around that field.

For `length = 0x1234`, `flags = 5`, and `sequence = 0x01020304`:

| Record bit order | Bytes at increasing addresses | Size / alignment |
| --- | --- | --- |
| `msb_first` | `91 A5 01 02 03 04` | 6 / 1 |
| `lsb_first` | `34 B2 01 02 03 04` | 6 / 1 |

The sequence starts at byte offset two in both cases. Its bytes are big-endian
in either record. In the MSB-first record the first two bytes are:

```text
byte 0, bits 7 .. 0: length[12 .. 5]
byte 1, bits 7 .. 0: length[4 .. 0] flags[2 .. 0]
```

Bit order specifies both placement within bytes and the order of a field's
numeric bits. Merely calling a convention “bit zero is the MSB” would leave the
second part ambiguous.

For an integer field starting at stream bit offset `s`, of width `w`, let `j`
range from zero through `w - 1`. Stream bit `s + j` is in byte
`(s + j) / 8`. Within that byte:

| Order | Physical bit, with bit 0 having weight 1 | Numeric bit of the field |
| --- | --- | --- |
| `msb_first` | `7 - ((s + j) % 8)` | `w - 1 - j` |
| `lsb_first` | `(s + j) % 8` | `j` |

Signed fields store their width-bit two's-complement representation. Extracting
an `i13` interprets those 13 bits as signed, regardless of adjacent bits.

## Field rules

Unqualified `uN` and `iN` fields are bitfields, including the familiar widths
such as `u16`. Their placement follows the record's explicit bit order. A
packed `u32` field therefore does not use host-native memory byte order. Use
`be_u32` or `le_u32` to request an explicitly ordered, whole-byte field.

Bitfields may cross byte and machine-word boundaries. There is no backing-word
capacity limit on a packed record, and no implicit promotion of a narrow field
to a larger field. The supported width range is `u1` through `u64` and `i1`
through `i64`; `bool` remains a separate type.

Endian-qualified fields keep the currently supported widths: 8, 16, 32, and
64 bits. They must start at a byte boundary. Their bytes occupy consecutive
addresses in their declared order and ignore the enclosing record's bit order.
For example, `u13` followed immediately by `be_u32` is rejected; the compiler
must not silently insert three padding bits. A declared three-bit field makes
that space explicit. Qualified sub-byte types such as `be_u13` are deferred.

Other packed records can be embedded at byte boundaries. Each keeps its own
bit order and complete byte extent, including unused bits in its last byte.
Arrays embedded in a packed record also start at byte boundaries; initially
their elements can be `u8` or packed records (including nested arrays of these
kinds). They retain each element's full byte extent. Embedded arrays of
endian-qualified scalars are deferred because their element addresses would
require an unaligned pointer capability. Bitfield arrays such as `[u3; 8]` are
also deferred; ordinary arrays outside packed records retain native slot stride.

The initial field set excludes ordinary native-layout records, pointers, slices,
strings, floats, enums, results, and `bool`. This keeps every admitted layout
independent of native ABI choices. Ordinary structs and arrays may still contain
packed values. Existing generic specialization and method rules apply
when the concrete packed fields satisfy these restrictions.

Explicit `at` offsets, overlapping fields, unions, and explicit alignment
overrides are deferred. The old experimental parser accepting such syntax does
not establish a checked-language contract. Initially, reserved space is an
ordinary declared integer or byte-array field, with an explicit initializer.

## Values, updates, copies, and calls

| Operation | Required behavior |
| --- | --- |
| Construction | Evaluate initializers in source order, check their values, and encode each scalar field. Initialize unused trailing bits to zero. Copy an existing nested packed value's complete bytes. |
| Field read | Decode exactly the declared field into its numeric value type; ignore neighboring and unused bits. |
| Field assignment | Update only the field's bits or bytes. Preserve all neighboring fields and unused bits, including nonzero bits introduced through raw byte access. |
| Typed copy | Preserve every byte, including unused trailing bits. Materialized overlapping copies retain the existing `memmove` semantics. |
| Gloin argument / return | Preserve the complete packed representation and existing value-capture timing. Later argument evaluation or deferred mutation must not change an already captured value. |
| Different-schema conversion | Explicitly construct the destination and convert fields. There is no implicit bit-order change or representation cast. |
| Raw byte copy | Copy bytes without decoding, validation, or normalization. Typed field reads subsequently interpret those bytes using the destination schema. |

Zeroing construction tail bits does not authorize canonicalizing an existing
record during a copy, call, return, or field update. Two records with equal field
values may have different unused bits after raw writes. Copying must retain
that difference. Fresh scalar encoding happens only where a scalar value enters
the representation.

A six-byte `Header` may be placed at any byte address. A reference to the whole
record is valid under the ordinary lifetime and mutability rules. The
implementation rejects ordinary references to packed scalar fields,
including an endian field that happens to be byte-aligned: byte alignment is not
necessarily the natural alignment required by `&be_u32`. A bitfield also has no
independently addressable object representation. The rule must cover implicit
borrows and generic helpers as well as explicit `&header.field`.

Direct field reads and writes remain available. References to byte-aligned
nested packed records can use their alignment-one object type. Supporting scalar
field references later requires a representation-aware reference capability;
an ordinary pointer cannot express the needed access rules.

Packed field writes sharing a byte are read-modify-write operations. This
feature does not define atomic or volatile accesses or memory-mapped device
register semantics.

Internal scalar calls transport numeric values, retaining width and signedness
in GloinIR. Packed aggregate calls transport the stored representation. There is
no automatic foreign by-value ABI mapping for packed records or custom-width
integers; such declarations must be rejected until an explicit adapter contract
exists. A platform C bitfield layout is not the packed-layout authority.

## Checked integer constructors

The destination's mathematical range determines success, independently of byte
order. The rule covers width and signedness changes together:

| Conversion | Outcome |
| --- | --- |
| `u16(value_of_u13)` | Always returns a successful `result<u16>`. |
| `u13(value_of_u16)` | Successful `result<u13>` for 0 through 8191; otherwise `OVERFLOW`. |
| `i13(value_of_u13)` | Accept 0 through 4095. |
| `u13(value_of_i13)` | Success for nonnegative values; `OVERFLOW` for negative values. |
| `be_u32(value_of_u13)` | Preserve the number; encode big-endian only when stored. |
| `le_u32(value_of_be_u32)` | Preserve the number; no numeric range check is needed. |

Every explicit constructor returns `result<T>`, including identity, exact
widening, and endian-only conversions. An argument is evaluated once. Callers
must check `.erroneous` or use `match`; unchecked payload reads and ignored
results are rejected. Successful signed widening sign-extends; unsigned widening
zero-extends. Failure returns `OVERFLOW` with OS error 0, without wrapping,
saturating, or trapping. See [numeric conversions](numbers.md#explicit-conversion)
for an example. Existing named numeric APIs remain compatible; floating
conversions keep their named APIs.

Typed literals, local arithmetic, comparisons, function parameters, returns, and
generic specializations retain the exact integer type. `u13(8192)` returns an
error, while `def x: u13 = 8192` is a compile-time error. Adding one to a runtime
`u13` value of 8191 still traps before a packed assignment can discard the high
bit. `i1` represents only -1 and 0; `u1` represents 0 and 1.

A narrow integer's standalone allocation size is a separate question from its
numeric width. The storage rule outside packed fields is a native
8-, 16-, 32-, or 64-bit slot, choosing the smallest slot containing the value.
Thus standalone `u13` occupies a `u16`-sized slot, while its packed field occupies
13 bits. Slot alignment follows the corresponding native integer type.
Construction zeroes spare slot bits; reads ignore them. Aggregate copies must
preserve those bits when raw storage has changed them, just like byte padding.
Checked arithmetic lowers to exactly 13-bit operations; scalar call arguments
use canonical sign- or zero-extended slot-sized numeric values.

## GloinIR and LLVM implementation

`numeric.h` retains canonical integer identities and derives native narrow
identities from width and signedness. `CoreTypeInfo` records logical width,
signedness, and byte order. The finite standard-type catalog remains separate
from the full width range, so existing native/endian matrices keep their scope.

`CheckedStruct` records bit order, field bit offsets, and byte size. Semantic
analysis computes these independently of target ABI layout and rejects invalid
field types, missing bit order, misaligned byte fields, and recursive values.
Packed schemas are limited to 2^31 bits and 64 nested schema levels. The parser's
old integer backing type and `at` forms remain rejected in checked programs.

In GloinIR, narrow values use types such as `!gloin.int<13, Native, Unsigned>`.
`gloin.integer_convert` records source/destination descriptors and a typed
success-or-overflow result. It is pure; effects and arithmetic traps while
evaluating its input remain observable. Same-width/sign changes use this same
result-producing operation.
Arithmetic retains the logical width until its exact overflow checks are
lowered; narrowing is never implemented by an unchecked truncation.

A packed `gloin.struct_definition` carries its bit order, offsets, field types,
and mutability alongside byte-array storage. `gloin.packed_literal`,
`gloin.packed_extract`, `gloin.packed_load`, and `gloin.packed_store` cross the
numeric/representation boundary. `gloin.packed_address` is restricted to
byte-aligned aggregate fields. Verifiers check these operations against the
nominal schema and reject using ordinary `gloin.field_address` for packed fields.

`packed_ir.cpp` lowers scalar memory access into bounded 1-, 2-, 4-, or 8-byte
chunks, masks, and shifts. Each memory operation stays within the field's
containing bytes with alignment one. Chunks swap bytes when the field's order
differs from the target's. A 64-bit field spanning nine bytes uses an eight-byte
chunk and one byte, without reading or writing beyond those nine bytes.

Construction zeroes fresh storage before writing fields; masked writes preserve
every bit outside the field. Fresh aggregate construction keeps direct byte
encoding: LLVM splits its byte-array SSA representation into bytes, and wider
temporary writes can leave redundant swaps. This distinction avoids that measured
regression while accelerating access to materialized records. See the
[packed performance record](packed-performance.md).
Temporaries live in the function entry block rather than growing the stack on
every loop iteration. LLVM may combine accesses when equivalent and safe.

Packed objects lower to a struct containing `[N x i8]`. Native narrow integer
slots use full 8/16/32/64-bit storage; scalar reads decode only their logical bits.
This keeps spare bits present in aggregate SSA values. Ordinary aggregate byte
padding is still made explicit by `aggregate_representation.cpp`.

Internal aggregate calls pass captured representations in private storage slots,
and return aggregates through a caller-owned result slot. This avoids native
scalar expansion of byte arrays: testing exposed loss of bytes in an AArch64
`-O0` by-value array argument. Scalar calls continue to use machine integers.
`gloin.copy` still lowers to `memmove`, with no field decoding or encoding.
Rebuild Gloin objects together; this internal convention is not a foreign ABI.

## Validation

The compiler's `PackedTest` suite and `run/packed.gloin` acceptance fixture check
integer identities and bounds, exact bytes, storage copies, and malformed IR.
Native guard-page tests use an independent C bit oracle, covering 480
width/offset/sign/order combinations at both `-O0` and `-O2`. The sibling packed
lab supplies runtime binary inputs and uses a separate Python bit oracle. The
following cases define the maintained regression coverage:

- Exhaust all 65,536 `u13`/`u3` combinations for both record orders. Check the
  exact six bytes, decoded values, and unchanged sequence bytes.
- Update each field independently over arbitrary initialized bytes. Check
  adjacent fields, unused tail bits, nested record tail bits, and canaries.
- Cover widths 1, 3, 7, 8, 9, 13, 17, 25, 31, 32, 33, 41, 49, 63, and 64, signed boundaries,
  every starting position within a byte, and fields crossing machine words.
- Test every constructor width/sign direction at the source and destination
  bounds, including negative-to-unsigned and unsigned-to-signed conversions,
  literal failures, unhandled-result diagnostics, and single evaluation.
- Exercise unaligned records, six-byte array stride, mixed BE/LE fields, nested
  records with different bit orders, and exact allocation boundaries. Guard-page
  tests must catch widened loads/stores that touch a byte outside an object.
- Preserve arbitrary unused bits through assignment, arrays, generic calls,
  nested calls, results where supported, return/defer capture, and both directions
  of overlapping materialized copy. Include ordinary aggregates containing
  standalone narrow integer slots with nonzero spare bits.
- Reject missing/unknown bit order, invalid widths, misaligned byte fields,
  unsupported field types, legacy offsets/backing integers, invalid references,
  and malformed or forged packed IR.
- Inspect lowering: whole-record copies contain no field decoding/encoding;
  field writes preserve other bits; unaligned accesses make no stronger alignment
  claim than the schema permits; conversion error results survive optimization when observed.

Synthetic little- and big-endian module layouts check bounded access widths,
alignment one, and the byte swaps required to preserve field order. Execution
evidence is currently from macOS ARM64; big-endian hardware execution is not claimed. The independent
lab is in `../gloin_examples/packed_lab`; run it from the compiler checkout with:

```sh
python3 ../gloin_examples/packed_lab/verify.py --compiler build/gloinc
```

The [release draft](release-0.3.0-draft.md) records counts and remaining platform
validation. The separate endian lab is also rerun because aggregate call lowering
affects ordinary records as well as packed records.
