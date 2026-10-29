# Gloin 0.3.0 draft

**Status: unreleased development draft.** The first included feature is
first-class integer endianness, with representation-preserving aggregate copies
and Gloin calls. This is a minor release because it adds source-language behavior.
The published baseline remains [0.2.1](release-notes-0.2.1.md); the compiler version,
release assets, and numbered guides will be updated when the release is cut.

## Scope and status

| Area | Status |
| --- | --- |
| Endian-qualified integers | Implemented in the development working tree: distinct `be_` and `le_` signed/unsigned 8-, 16-, 32-, and 64-bit types. |
| Explicit value conversion | Implemented: `le_u32(value)`, `be_u32(value)`, and native equivalents preserve the number and require the same width and signedness. |
| Aggregate representation | Implemented: copies preserve all bytes, including padding; Gloin by-value arguments and returns retain that representation. |
| GloinIR and LLVM lowering | Implemented: typed numeric SSA values, explicit conversion/copy operations, target-dependent scalar encoding, and explicit padding in internal LLVM aggregate layouts. |
| Validation | macOS ARM64 core, focused regressions, and the independent endian torture lab pass. Final release/platform/package validation remains pending. |

The [endianness contract](endianness.md) defines the complete behavior. The
[development guide](site/development/index.html#endianness) introduces the syntax.

## First-class endianness

Byte order is part of the integer's type. Native, big-endian, and little-endian
types remain distinct on every target, including one-byte types and qualified
types whose storage order happens to match the host.

```gloin
def big: be_u32 = 0x01020304;
def next: be_u32 = big + 1;
def little: le_u32 = le_u32(next);
def native: u32 = u32(little);
```

`next`, `little`, and `native` all have numeric value `0x01020305`. In increasing
address order, `next` occupies `01 02 03 05`, while `little` occupies
`05 03 02 01`. The native representation follows the target.

- Arithmetic between identical types retains that type. Comparisons produce
  `bool`; literals inherit their surrounding type. Existing checked overflow,
  division, remainder, and signed-negation rules apply to the numeric values.
- Mixing native, big-endian, or little-endian values requires explicit
  conversion, including assignments, comparisons, arguments, and returns.
- `T(value)` evaluates its argument once and preserves the number. Width/sign
  changes and floating conversions continue to use the named numeric APIs.
- Arrays, slices, generic specializations, and typed pointers preserve byte-order
  identity. A value conversion does not reinterpret a pointer.
- Integer size and alignment remain those of the corresponding native type.
  Endian qualification does not pack structs or permit unaligned typed access.

### Construction, copying, and conversion

| Operation | Contract |
| --- | --- |
| Value construction | Encode direct scalar fields/elements according to their declared types; retain the representation of existing aggregate fields/elements. |
| Typed aggregate copy | Preserve the complete representation, including internal and trailing padding. Materialized overlapping copies have `memmove` semantics. |
| Typed conversion | Preserve scalar numeric values. Converting aggregate schemas requires explicit destination construction and field conversions. |
| Raw byte copy | Preserve bytes exactly without interpreting byte order. Subsequent typed reads interpret the destination schema. |

Padding preservation extends through Gloin by-value function arguments and
returns, including nested records, arrays, and aggregate result payloads. The
initial contents of construction padding remain unspecified; the guarantee
does not make reading uninitialized storage valid. Ordinary structs retain their
native layout and are not automatically portable wire formats.

### Compiler representation and ABI

Qualified scalars retain types such as `!gloin.int<32, Big, Unsigned>` in GloinIR.
Their SSA values are numeric. `gloin.endian_convert` records the explicit value
conversion and disappears during lowering. Scalar loads/stores and scalar
aggregate construction/extraction insert byte swaps only when required by the
target data layout; one-byte values need none.

Aggregate copies avoid recursive field decoding and encoding. `gloin.copy`
lowers to `llvm.memmove`. Before LLVM export, internal aggregate layouts gain
explicit padding fields so SSA copies and Gloin calls preserve every byte;
physical sizes, alignments, and source field offsets remain unchanged.

This changes the internal representation of by-value aggregate calls. Rebuild
native objects and programs with a matching compiler/runtime. Gloin scalar calls
use ordinary machine integers after lowering. Padding-bearing foreign by-value
aggregates require an explicit ABI adapter; general FFI and automatic adapters
are outside this release's current scope.

The implementation also fixes a layout-measurement cache lifetime bug: LLVM's
struct-layout cache must not outlive the temporary LLVM context whose type
addresses it records. A regression repeatedly measures original and explicitly
padded layouts.

## Development validation, 2026-10-29

These results cover the local development working tree on **macOS ARM64 with
LLVM/MLIR 21.1.6**, based on commit `e321d07`. They are not a release-tag or package
validation claim.

| Check | Result |
| --- | --- |
| Full `check-core` gate | 1,097/1,097 passed. |
| Final focused lowering, endian, arena, and acceptance checks | 110/110 passed after the final copy-verifier adjustment. |
| Source formatting and documentation | Formatter passed; nine HTML pages and their local links passed. |
| Differential packet corpus | 3,203 cases in JIT, native `-O0`, and native `-O2`; 32 byte-sequence comparisons per case and mode. |
| Differential scalar corpus | 2,435 cases in each mode; 96 byte-sequence comparisons per case across all 24 native/BE/LE integer types. |
| Combined independent byte oracle | 1,008,768 byte-sequence comparisons covering 35,915,376 output bytes passed. |
| Invalid-type matrix | 230 cases rejected. |
| Runtime arithmetic matrix | 272 native executions produced the expected traps at `-O0`/`-O2`. |
| LLVM inspection | Packet copy lowers to `memmove` without byte swaps; pure numeric conversion contains no byte swaps. |

The independent lab lives in the sibling examples workspace at
`../gloin_examples/endian_lab`. It uses Python integer/byte-array operations as
its oracle and supplies runtime binary inputs to Gloin. It covers every repeated
byte value, walking set/cleared bits, asymmetric patterns, and seeded random data.
Cases include arbitrary initialized padding, nested aggregate calls, observable
`match` branches, arrays, arena allocation, both overlap directions, raw aliases,
capture before a later argument mutates storage, and return capture before
deferred mutation. Its `validation.json` records compiler/source hashes, the seed,
counts, and output hashes. The lab is a separate workspace artifact, not currently
part of the compiler's release package.

From a compiler checkout with that sibling directory available:

```sh
python3 ../gloin_examples/endian_lab/verify.py --compiler build/gloinc
```

The compiler regressions also exercise synthetic little- and big-endian module
layouts. Execution evidence is from a little-endian host; big-endian hardware
execution and cross-compilation support are not claimed.

## Remaining release validation

Before publishing 0.3.0, follow the existing
[release verification requirements](release-0.2.1.md#verification-boundary) on
the final committed source:

- Run the required core and complete-suite classification gates on the maintained
  macOS/Ubuntu targets, including hosted Ubuntu x86_64 and local Ubuntu ARM64.
- Run macOS ASan/UBSan, compiler-only builds, installed/relocated package checks,
  runnable examples, documentation checks, and archive checksum verification.
- Freeze the release scope, prepare the numbered 0.3.0 guide and release notes,
  update version metadata, and record the release revision and asset provenance.

Packed structs, bit fields, custom-width/128-bit integers, endian-qualified
floating types, automatic aggregate schema conversion, general FFI, and
language-level threads/async/spawn are outside the current 0.3.0 scope.
