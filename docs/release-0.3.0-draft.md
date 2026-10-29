# Gloin 0.3.0 draft

**Status: unreleased development draft.** First-class integer endianness, with
representation-preserving aggregate copies and Gloin calls, is implemented.
Packed memory layouts, exact-width integers, and checked integer constructors
are implemented as the second feature.
This is a minor release because it adds source-language behavior.
The published baseline remains [0.2.1](release-notes-0.2.1.md); the compiler version,
release assets, and numbered guides will be updated when the release is cut.

## Scope and status

| Area | Status |
| --- | --- |
| Endian-qualified integers | Implemented in the development working tree: distinct `be_` and `le_` signed/unsigned 8-, 16-, 32-, and 64-bit types. |
| Explicit value conversion | Implemented: integer constructors preserve the number when it fits the destination; every constructor returns `result<T>`, with `OVERFLOW` for out-of-range inputs. |
| Aggregate representation | Implemented: copies preserve all bytes, including padding; Gloin by-value arguments and returns retain that representation. |
| GloinIR and LLVM lowering | Implemented: typed numeric SSA values, explicit conversion/copy operations, target-dependent scalar encoding, and explicit padding in internal LLVM aggregate layouts. |
| Packed memory layouts | Implemented: explicit bit order, exact byte layout, endian-qualified byte fields, masked updates, and representation-preserving copies/calls. See the [packed layout contract](packed-layout.md). |
| Packed access performance | Bounded word loads/stores with alignment one, field masks, and explicit target byte order. Construction keeps direct byte encoding. See the [measurement report](packed-performance.md). |
| Exact-width integer values | Implemented: `u1`–`u64` and `i1`–`i64`, with arithmetic checked at their declared widths and separate native storage slots. |
| Checked integer constructors | Implemented: `T(value)` always returns `result<T>`, including widening and endian-only changes; callers handle errors through `.erroneous` or `match`. |
| Validation | macOS ARM64 core, focused regressions, and independent endian and packed torture labs pass. Final release/platform/package validation remains pending. |

The [endianness contract](endianness.md) defines byte-order behavior. The
[development guide](site/development/index.html#endianness) introduces the syntax.

## Packed memory layouts

Packed structs describe memory through named fields with explicit bitfield
order, independently of a field's byte order:

```gloin
def packed struct(msb_first) Header {
    def length: u13,
    def flags: u3,
    def sequence: be_u32,
}
```

For `length = 0x1234`, `flags = 5`, and `sequence = 0x01020304`, the
layout is exactly `91 A5 01 02 03 04`, with size six and alignment one. Selecting
`lsb_first` changes the first two bytes to `34 B2`; the big-endian sequence bytes
stay the same. `u13` and `u3` are integer value types with checked arithmetic,
and `u16(value)` is an explicit checked numeric conversion.

The [packed layout contract](packed-layout.md) defines bit placement, field
eligibility, construction, updates, references, and calls. Endian fields and
nested byte-backed aggregates must begin on byte boundaries; no implicit padding
is inserted. Scalar field references are rejected. Native narrow integer slots
retain their spare bits during aggregate copies, including Gloin calls.

Packed storage uses byte arrays with explicit GloinIR field operations. Reads
and masked writes stay inside the object's bytes with alignment one. Construction
zeroes unused tail bits, while copies and field updates preserve existing bits.
Arrays of bytes and packed records can be embedded; embedded arrays of endian
scalars and sub-byte integers remain deferred.

Internal aggregate arguments and results now use private memory slots. This
fixes byte loss found in native AArch64 `-O0` calls carrying an array of packed
records, and preserves captured values across later argument or deferred
mutations. Rebuild Gloin objects with a matching compiler. Foreign by-value
packed records and custom-width integers require an explicit adapter.

## First-class endianness

Byte order is part of the integer's type. Native, big-endian, and little-endian
types remain distinct on every target, including one-byte types and qualified
types whose storage order happens to match the host.

```gloin
def big: be_u32 = 0x01020304;
def next: be_u32 = big + 1;
def converted: result<le_u32> = le_u32(next);
if converted.erroneous { return converted.error; }
def little: le_u32 = converted.value;
def native: result<u32> = u32(little);
if native.erroneous { return native.error; }
```

`next`, `little`, and `native.value` all have numeric value `0x01020305`. In increasing
address order, `next` occupies `01 02 03 05`, while `little` occupies
`05 03 02 01`. The native representation follows the target.

- Arithmetic between identical types retains that type. Comparisons produce
  `bool`; literals inherit their surrounding type. Existing checked overflow,
  division, remainder, and signed-negation rules apply to the numeric values.
- Mixing native, big-endian, or little-endian values requires explicit
  conversion, including assignments, comparisons, arguments, and returns.
- `T(value)` evaluates its argument once and preserves the number when it fits.
  It always returns `result<T>`, including widening and endian-only changes;
  out-of-range inputs return `OVERFLOW`. Floating conversions
  continue to use the named numeric APIs.
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
Their SSA values are numeric. `gloin.integer_convert` produces a typed success
or overflow result; guaranteed-success checks can disappear during optimization. Scalar loads/stores and scalar
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

## Integer constructor API update, 2026-10-29

Every explicit integer constructor now returns `result<T>`, including identity,
exact widening, and endian-only conversions. Range failures return `OVERFLOW`
with OS error 0; unsupported source kinds remain type errors. This supersedes
the trapping-constructor behavior in the earlier validation snapshots below.
Typed literals retain compile-time range checks, and integer arithmetic retains
its overflow traps. The existing named standard-library numeric APIs keep their
signatures; no source/destination function matrix is added.

GloinIR carries the conversion's result type, integer descriptors, diagnostic,
and layout. Lowering constructs the success or error variant and encodes the
successful payload according to its type. Checked results require the existing
`.erroneous`/`match` handling before extraction. Safe conversions can optimize
to ordinary machine values without changing the source return type.

Validation on local macOS ARM64 with LLVM/MLIR 21.1.6:

- The core run passed 1,111 of 1,113 cases. The two harness failures were a
  10-second child compile timeout in the new large matrix and the old diagnostic
  expectation for an unhandled conversion. Both passed targeted CTest reruns
  after updating their timeout/expectation; compiler behavior was unchanged.
- The new runtime matrix checks 1,296 source/destination pairs in JIT, `-O0`,
  and `-O2`, including every native width 1–64, all conventional endian pairs,
  signedness boundaries, identity conversions, and dynamic values. Additional
  cases check literal failures, generic targets, evaluation order, allocation
  independence, result handling, forged IR, and both synthetic target orders.
- The migrated endian lab passed 35,915,376 exact output bytes, 226 rejection
  cases, and 272 native arithmetic-trap executions. The packed lab passed
  20,528,640 exact output bytes across the three execution modes.
- The active packed benchmark now returns/checks conversion status in both Gloin
  and its C reference. All 12 kernels passed output, input, and canary checks.
  Optimized header decoding eliminates the safe conversion checks; encoding
  retains the necessary bounds checks. Historical benchmark snapshots retain
  their original source, ABI, and timing evidence.
- The numeric guide programs, Gloin formatting, and nine HTML pages with local
  links passed. The source catalog contains 1,160 tests. Final cross-platform,
  sanitizer, and package release validation remains pending.

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

## Packed development validation, 2026-10-29

These are local macOS ARM64 results on the development tree following the
endianness commit `cefc070`. They do not constitute release or other-platform
validation.

| Check | Result |
| --- | --- |
| Full `check-core` gate | 1,107/1,107 passed, including the packed suite and acceptance fixture. |
| Formatting and documentation | Gloin formatter and new C++ source formatting passed; nine HTML pages and their local links passed. |
| Exact six-byte header | All 65,536 `u13`/`u3` combinations in each bit order; JIT and native `-O0`/`-O2`. |
| Width/offset/sign/order matrix | 352 layouts against an independent per-bit C oracle; 70 inputs each, at both guard-page edges and both native optimization levels (98,560 executions). |
| Narrow integer bounds and conversions | All 128 native width/sign types; boundary checks for every pair of native and endian-qualified types; unused-result and arithmetic traps at native `-O0`/`-O2`. |
| Independent packed lab | 2,880 runtime input frames per mode; 6,842,880 exact bytes per mode, 20,528,640 bytes across JIT/`-O0`/`-O2`. |
| Packed lab copy cases | Recursive calls, returns before deferred poisoning, arguments captured before later mutation, result payloads, aggregate arrays, schema conversion, masked updates, and all overlapping offsets 0–18 in both directions. |
| Endian regression lab | The million-comparison corpus and 272 native traps pass; the rejection matrix now has 226 cases because width/sign constructors are supported and checked for range. |

The independent packed lab lives at `../gloin_examples/packed_lab`. Its Python
oracle uses individual bits; scalar lowering now uses bounded word-sized chunks. Raw inputs
include arbitrary unused bits and 64-bit fields crossing machine-word boundaries.
Reports record compiler/source hashes and output hashes. The compiler's core gate
also includes `PackedTest` and `run/packed.gloin`, including generic packed methods,
arrays, malformed IR, and foreign ABI policy checks.

```sh
python3 ../gloin_examples/packed_lab/verify.py --compiler build/gloinc
```

## Packed optimization validation, 2026-10-29

The performance follow-up to `c25c8f6` uses bounded 1-, 2-, 4-, or 8-byte field
accesses with alignment one and explicit target byte swaps. Fresh construction
retains byte encoding; aggregate copies retain representation-preserving copy
operations. The [performance report](packed-performance.md) records paired native
measurements against the original compiler and a C control, including raw samples,
assembly, source/compiler hashes, uncertainty, and code-size tradeoffs.

The independent packed lab exposed a range-proof lifetime bug: a newly allocated
signed operation could reuse an erased unsigned operation's address and receive
its unchecked unsigned-division shortcut. A rewrite listener now invalidates
stale proofs. A regression checks signed LLVM division and JIT/native results
across 64 functions, while existing floating-point optimization checks continue
to pass.

| Check on the optimized macOS ARM64 compiler | Result |
| --- | --- |
| Full `check-core` gate | 1,109/1,109 passed. |
| Guard-page matrix | 480 layouts, 70 inputs at both page edges and `-O0`/`-O2`: 134,400 executions passed. |
| Target-layout inspection | Bounded eight-byte/one-byte accesses for nine-byte fields; alignment one and correct swapping under synthetic little-/big-endian layouts. |
| Packed torture lab | 2,880 frames per mode; 20,528,640 exact output bytes across JIT/`-O0`/`-O2`. |
| Endian torture lab | 1,008,768 byte-sequence comparisons, 226 rejected programs, and 272 expected native traps passed. |

The reproducible benchmark and saved evidence live in
`../gloin_examples/packed_bench`. These are development results on Apple M2;
other-platform and final release validation remain pending.

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

Packed structs, bitfields, exact-width integers, and checked integer constructors
are included in the development implementation. Final release validation must
cover them on every maintained target. Widths above 64 bits, endian-qualified
sub-byte integers and floating types, automatic aggregate schema conversion,
general FFI, and language-level
threads/async/spawn remain outside the current 0.3.0 scope.
