# First-class byte order (development)

This is the development contract for SPEC-037, included in the
[0.3.0 draft](release-0.3.0-draft.md). It is not part of the published 0.2.1 release.
The [development guide](site/development/index.html) contains the source-language
overview.

## Integer types and conversion

Signed and unsigned integers of 8, 16, 32, and 64 bits have three distinct type
identities: native (`u32`), big endian (`be_u32`), and little endian (`le_u32`).
The prefix spelling is canonical. Suffix spellings, custom widths, 128-bit
integers, and endian-qualified floating types remain unsupported.

Type identity never depends on the target. `le_u32` is distinct from `u32` on a
little-endian machine. The 8-bit forms are also distinct even though their
representations are identical. Qualification preserves the underlying integer's
size, alignment, range, and signedness.

```gloin
def big: be_u32 = 0x01020304;
def next: be_u32 = big + 1;
def little: le_u32 = le_u32(next);
def native: u32 = u32(little);
```

All three results have numeric value `0x01020305`. In increasing address order,
`next` occupies bytes `01 02 03 05`, and `little` occupies `05 03 02 01`.
The native representation follows the selected target.

`T(value)` is an explicit integer value conversion. It requires exactly one
argument with the same width and signedness as `T`; it changes only the byte-order
type. Identity conversions are allowed. It never reverses the numeric bits or
reinterprets a pointer. Width/sign changes and floating conversions still use
the existing named numeric APIs. Generic type parameters may name the target.

Literals take their contextual type, including in constructors, and retain the
existing range checks. Constant initializers can use these conversions.
Arguments are evaluated once, and runtime evaluation order is unchanged.

Arithmetic operands must have identical types. Arithmetic retains that type;
comparisons produce `bool`. Existing checked overflow, division, remainder, and
signed negation rules apply to numeric values. Mixed byte orders require an
explicit conversion even for comparison or assignment. Function arguments,
returns, arrays, slices, generic specializations, and pointers retain the same
distinction. `&be_u32` cannot become `&le_u32` through a value conversion.

## Construction, copy, and conversion

These operations have different contracts:

| Operation | Representation behavior |
| --- | --- |
| Scalar store or scalar field construction | Encode the numeric value in the declared byte order. |
| Scalar load or scalar field extraction | Decode the stored representation to a numeric value. |
| Aggregate construction | Encode direct scalar fields/elements; preserve already constructed aggregate fields/elements. |
| Same-type aggregate copy | Preserve the entire existing object representation, including internal and trailing padding. Overlapping materialized copies have `memmove` semantics. |
| Explicit scalar conversion | Preserve the number and change the byte-order type. |
| Conversion between aggregate schemas | Construct the destination and explicitly convert the fields that change type. No automatic aggregate cast or pointer reinterpretation is introduced. |
| Raw byte copy | Preserve bytes exactly, without consulting an endian schema. |

An aggregate value carries its representation even while passed by value to a
Gloin function or returned from one. Copies through immutable bindings, generic
functions, nested records, arrays, and result payloads preserve padding. The
initial contents of construction padding are not a serialization promise.
Reading uninitialized bytes is not made valid by this guarantee.

Byte order does not pack structs or relax alignment. Native field offsets,
padding, array stride, and `memory.size_of<T>()`/`align_of<T>()` still apply.
Consequently an ordinary struct is not automatically a portable wire format.
`memory.place<T>` keeps its size/alignment checks. Raw byte aliases of valid
storage observe the declared representation, and subsequent typed reads decode
raw modifications accordingly.

## GloinIR and lowering

Qualified scalar values use `!gloin.int<32, Big, Unsigned>` (or the corresponding
width/order/signedness). Integer constants, arithmetic, comparisons, function
signatures, and typed pointers preserve this identity. Native scalar integers
continue to use MLIR's signless integer types with semantic signedness retained
by the frontend and checked operations.

`gloin.endian_convert` records both complete integer descriptors, including
`Native` for a native endpoint. Its verifier requires equal widths and
signedness. Checked compilation also requires source-typed operands and results.
The operation disappears when source types are lowered: both endpoints already
represent the same number.

Scalar representation changes become LLVM byte swaps only when the declared
order differs from the module's target data layout. One-byte values need no
swap. Aggregate construction/extraction handles scalar boundaries; aggregate
loads, stores, and copies do not recursively decode or encode fields.

`gloin.copy` represents an exact, same-schema materialized copy and lowers to
`llvm.memmove`. Preparation recognizes adjacent aggregate reads/writes without
moving a read across intervening effects. Other aggregate SSA copies retain
their complete representation as well.

Before LLVM export, `aggregate_representation.cpp` makes padding explicit as byte
array fields in internal aggregate layouts. It remaps field/GEP indices and
Gloin argument/return types, and verifies that object size, alignment, and source
field offsets are unchanged. This prevents LLVM aggregate SSA values from
discarding padding. LLVM intrinsic tuples retain their prescribed signatures.
The layout helper keeps LLVM's struct-layout cache within the lifetime of the
temporary LLVM context used for measurement.

## Function and foreign ABI policy

Gloin-to-Gloin scalar arguments/results are numeric machine integers after
lowering. Their byte-order qualification remains enforced before lowering.
Gloin aggregate arguments/results carry the complete representation, including
padding. This is an internal Gloin calling convention, not a C ABI promise.

The existing runtime boundary continues to use its declared native scalar,
pointer, and padding-free descriptor signatures. Padding-bearing by-value
foreign aggregates are rejected during representation lowering. General FFI
and automatic foreign adapters are not introduced. Future by-value foreign
endian aggregates require an explicit ABI adapter; pointers to encoded storage
must likewise have an explicitly agreed representation contract.

## Validation

The existing numeric boundary/overflow matrices include every new scalar type.
`EndianTest` checks distinct types, conversion constraints, GloinIR verification,
absence of swaps for pure value conversions and packet copies, target-dependent
load lowering, and layout-cache lifetime. `run/endian.gloin` checks exact bytes,
raw aliases, overlapping copies, arrays, aggregate padding through calls, result payloads, arena
allocation, and hash maps in JIT and native `-O0`/`-O2` execution.

Synthetic little- and big-endian module layouts exercise both lowering choices.
They do not constitute execution on big-endian hardware or add cross-compilation
support.
