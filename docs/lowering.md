# Verified IR and shared LLVM lowering

`compile_source` returns verified high-level IR by default. Its optional fifth
argument, `CompilationOutput::LLVM`, returns a verified LLVM-dialect module:

```cpp
auto result = compile_source(text, filename, context,
                             CompilationMode::Executable,
                             CompilationOutput::LLVM);
if (!result.success()) {
    result.diagnostics->render(std::cerr);
    return 1;
}
// result.module owns verified LLVM-dialect IR; context must outlive it.
```

Executable mode checks the source `main() -> i32` contract; choosing LLVM output
does not itself execute a program. Module mode still permits helper-only input.
The default high-level output preserves a mixed module of `func`, `arith`, `cf`,
LLVM, and supported Gloin operations for inspection and tests of compiler
stages. It is not yet a uniform GloinIR module.

## GloinIR coverage

Some checked language constructs still bypass the custom Gloin dialect.
Generic function, arithmetic, and control-flow mechanisms may use MLIR's
standard `func`, `arith`, and `cf` dialects. Gloin-specific behavior needs a
Gloin operation while its semantics are still visible. The development
compiler emits these checked Gloin operations:

| Operation | Source behavior retained | Lowering |
| --- | --- | --- |
| `gloin.from_layout`, `gloin.to_layout` | Explicit boundary between a checked source value and its storage layout | Removed after checked signatures and storage results become layout types |
| `gloin.abi_call` | Explicit checked call to a declared runtime ABI function with exact layout arguments and result | LLVM call after callee and signature verification |
| `gloin.constant` | Exact checked scalar literal or folded constant value | `arith.constant` |
| `gloin.exact_int_widen` | Exact widening of a supported integer with explicit source signedness | Sign or zero extension to the verified wider type |
| `gloin.endian_convert` | Explicit same-width, same-signedness integer value conversion | Identity on numeric SSA values after source-type verification |
| `gloin.copy` | Same-type materialized aggregate copy including padding, with overlap support | `llvm.memmove`, without decoding or encoding fields |
| `gloin.assert` | Trap if a checked runtime precondition fails, including bounds, null, and arithmetic checks | Conditional branch and LLVM trap |
| `gloin.checked_int_binary` | Signed/unsigned `+`, `-`, `*`, `/`, `%` with overflow and zero-divisor rules | Source-width LLVM overflow intrinsics or guarded division/remainder |
| `gloin.checked_float_binary` | Finite `f32`/`f64` `+`, `-`, `*`, `/` with zero-divisor rules | Floating arithmetic and finite-result guards |
| `gloin.checked_int_compare` | Checked integer and bool comparison with explicit signedness | Integer comparison with the selected signed or unsigned predicate |
| `gloin.checked_float_compare` | Checked `f32`/`f64` comparison, including unordered `!=` | Floating comparison with the selected ordered or unordered predicate |
| `gloin.array_literal` | Fixed-array elements in source evaluation order | LLVM array construction |
| `gloin.repeat_array` | One captured source value repeated across a fixed-array layout | Stored repeats become `array_fill`; remaining by-value uses materialize through a counted loop |
| `gloin.array_fill` | Write one captured element across verified writable fixed-array storage, including nested repeat dimensions | Byte fill for `i8`/`u8`; a counted loop of typed stores for other elements |
| `gloin.struct_definition` | Nominal struct identity, ordered source field types, and storage layout | Removed after source types are resolved |
| `gloin.struct_literal` | Exact named fields in source evaluation order | LLVM struct construction at checked field indices |
| `gloin.zeroed_array` | Contextual recursive zero initialization | LLVM zero aggregate |
| `gloin.string_literal` | Checked string literal backed by static bytes and an exact byte length | LLVM string struct construction |
| `gloin.error_literal`, `gloin.error_message`, `gloin.error_code`, `gloin.error_os_error` | Construct an error from a static message and two i32 diagnostic codes; read its immutable fields | Error aggregate with string/i32/i32 fields, constructed and extracted after the source-type boundary |
| `gloin.result_success`, `gloin.result_failure` | Construct exactly one active result variant, including `result<void>` | Tag and initialized value/error storage |
| `gloin.result_is_error` | Inspect the active result variant | Read the tag |
| `gloin.result_value`, `gloin.result_error` | Extract the semantically proven active variant | Runtime tag guard, then payload extraction |
| `gloin.null` | Contextual nullable pointer null value | LLVM zero pointer |
| `gloin.arena_typed_pointer` | Give raw arena byte storage a checked element pointer, trapping if a required allocation failed | Null guard when required, then the same LLVM address |
| `gloin.raw_place` | Check raw storage, available bytes, alignment, and source element type before placement | Conditional typed store and nullable result pointer |
| `gloin.raw_padding` | Query padding for a target element type at a raw address | Native address alignment arithmetic |
| `gloin.pointer_offset` | Checked pointer offset measured in pointee elements | Null guard, then LLVM GEP |
| `gloin.require_nonnull` | Nullable pointer access must trap on null | Pointer comparison and trap guard |
| `gloin.pointer_compare` | Checked pointer equality or inequality | LLVM pointer comparison |
| `gloin.array_element_address` | Checked fixed-array element address | Bounds guard, then LLVM GEP |
| `gloin.slice_from_array` | Borrow an exclusive-end array range with source mutability | Range guards, then pointer/length descriptor |
| `gloin.slice_subrange` | Borrow an exclusive-end range of an existing slice | Range guards, then adjusted pointer/length descriptor |
| `gloin.slice_element_address` | Checked slice element address | Length guard, then LLVM GEP |
| `gloin.slice_length` | Read a slice's stored length | LLVM field extraction |
| `gloin.arena_fill` | Initialize every typed arena slot with one captured source value | Counted loop of typed element stores |
| `gloin.stack_alloc` | One checked local slot placed at function entry | LLVM alloca |
| `gloin.load` | Checked source value read from an address | LLVM load |
| `gloin.store` | Checked source value write to an address | LLVM store; direct zero-array stores use a bulk memset |
| `gloin.field_address` | Addressable checked struct field at a verified index | LLVM GEP |
| `gloin.extract_field` | Value-field read at a verified index | LLVM extractvalue |
| `gloin.enum_constant` | Construct a nominal payloadless enum SSA value | Private `u32` tag in enum storage |
| `gloin.enum_compare` | Equality or inequality of operands with the same nominal enum SSA type | Compare extracted tags |

Checked `func.func` signatures now use Gloin source types for pointers, arrays, slices,
structs, strings, enums, errors, results, and endian-qualified integers. Calls and
returns use those types too. Each checked function records its LLVM-compatible argument and result layouts in
`gloin.layout_inputs` and `gloin.layout_results`; lowering rejects missing or
incompatible layout metadata. `gloin.to_layout` converts an entry argument or
call result for the existing body, and `gloin.from_layout` converts a call
argument or return value. A dedicated pass converts signatures and call results
to their recorded layouts, removes these bridges and metadata, and then runs the
existing Gloin operation lowering. An enum or struct of the wrong nominal type
cannot pass the `func.call` verifier even when its storage layout matches. A
layout value that is visibly produced from a source value cannot be bridged
back as a different source type; the only allowed changes weaken the outer
pointer's write/non-null capability or a slice's element write capability.
Typed arena allocation is an
explicit `gloin.arena_typed_pointer` operation because its raw `*u8` storage
becomes a pointer to the element being initialized; an ordinary layout bridge
cannot express that change. The initializer is stored after this operation.

Endian-qualified scalars remain numeric SSA values; scalar memory and aggregate
field boundaries encode or decode their declared order. Aggregate values carry
their existing representation. Before LLVM export, internal aggregate layouts
gain explicit padding fields so copies and Gloin arguments/returns preserve all
bytes. The transformation retains physical size, alignment, and field offsets;
foreign aggregate signatures that would change require an explicit adapter.
See the [endianness contract](endianness.md) for the conversion and ABI rules.

Pointer, fixed-array, struct, enum, and local storage operations carry Gloin source type
attributes. For example,
`!gloin.ptr<i32, true, false>` records an `i32` pointee, nullability, and
read-only status; `!gloin.array<i32, 2>` records a fixed length;
`!gloin.slice<i32, false>` records an element type and write capability; and
`!gloin.struct<"Name#id">` and `!gloin.enum<"Name#id">` retain a readable name
and a compilation-local checked type ID. The suffix distinguishes same-named
types from separate modules and generic specializations; it is an internal IR
identity, not source syntax. The
source type builder also understands nested arrays, structs, and strings for
pointer pointees. The verifiers check the attribute kind and available layout
constraints, including array length and loaded/stored value shape. Checked
array, struct, zeroed-array, and string literal results now have source SSA
types. Array and struct literal operands have source types too. Each checked
struct has a `gloin.struct_definition` symbol with its ordered source field
types and LLVM layout. Struct literals and field accesses verify against that
definition. Value-field extraction receives a source-typed struct and produces
a source-typed field value. Enum operations,
function boundaries, checked pointer construction, typed arena allocation,
offsets, non-null checks, comparisons, stack/field/array addresses, loads, and
stores also retain source types. A checked load records its LLVM layout
separately; the signature lowering pass resolves these types and removes their
bridges before core lowering. Checked expression results, immutable bindings,
parameters, function calls, and returns now keep their source SSA type. Mutable
storage uses a typed Gloin address and explicit source/layout bridges. Pointer
and slice values may lose only an outer access capability through a checked
bridge. The remaining checked `cf` branch arguments are booleans, and MLIR
verifies their type at each edge. `!gloin.result<T>` and `!gloin.error` are
nominal source types with Gloin operations for construction and access.

Result payload extraction and indexed loads keep the type provided by the
result or storage before applying a contextual capability conversion. For
example, extracting `.value` from `result<&i32>` produces `!gloin.ptr<i32, false, false>`
even when its destination is `*const i32`. A `gloin.to_layout`/`gloin.from_layout`
pair then records the permitted weakening. Loading a `[T]` row into `[const T]`
likewise loads the original slice type before weakening the descriptor value.
`gloin.result_value` still verifies the exact payload type, and `gloin.load`
still verifies its address's pointee type.

Code generation and GIR verifiers share `canWeakenSourceValue`: a pointer may
become nullable and/or read-only, and a writable slice may become read-only.
Pointee and element types must remain identical, including nested capabilities.
Equal LLVM layouts alone never authorize a conversion between known source
values. Result success proofs remain mandatory. These bridges are removed at
the existing lowering boundary; this introduces no runtime conversion or buffer
copy. The direct result/row conversions were repaired after 0.1.0 under
[PERF-009](compiler-validation.md).

Fixed-array indexing plans storage for the owning local or by-value parameter,
including immutable bindings and arrays nested in ordinary struct fields.
GIR initializes that binding once at its declaration (or at function entry for
a parameter), then uses `gloin.array_element_address` and `gloin.load` for each
read. This avoids copying the entire array into a temporary for every lookup.
The stack slot can be reserved at entry, but a declaration inside a loop still
initializes its value on every iteration. LLVM may eliminate storage when safe.

This storage decision grants no new source-level write or borrowing capability.
Assignments and parameters still copy array values; slices still borrow their
backing storage. Calls returning temporary arrays still evaluate at their source
position, once per expression, and materialize there when indexed. Slice and
pointer accesses already refer to storage. Bounds checks remain GIR operations
and may disappear when GIR or LLVM proves the access valid. This improvement is
in 0.2.0; published 0.1.0 binaries still have the
repeated-copy issue described in [PERF-002](compiler-validation.md).

Stored repeated initializers have a separate GIR preparation pass before source
signature lowering. It turns `gloin.store(gloin.repeat_array(value), address)`
into `gloin.array_fill(address, value)`. Consecutive nested repeats use their
innermost captured SSA value. The verifier requires a writable, non-null pointer
to the exact destination array, matching source/storage layouts, and an element
type at a complete nested-array boundary. Layout equality alone does not permit
writing a different source type or restoring write access.

The pass retains initializer evaluation at its original position; only copying
the already captured value moves to the store. Thus assignments that read their
destination, intervening alias mutations, loop-local initialization, pointer
identity, and zero-length initializer effects keep their semantics. Array value
construction is pure after its element is evaluated, so unused repeat values
and layout bridges can be removed while calls and trapping operations remain.
The pass disables generic region simplification to preserve the checked CFG
until the source-type boundary has been lowered.

Byte fills become LLVM memset without an intermediate array-valued load/store.
Other elements use typed stores with their actual element stride, preserving
wider numeric values, floats, booleans, references, strings and struct fields.
Empty dimensions emit no writes; nested empty repeats still evaluate the seed.
This is the 0.2.0 [PERF-003 fix](compiler-validation.md). It addresses
stored repeated values and nested repeat chains. General by-value array
arguments/returns, array-valued seeds and arrays embedded in constructed
aggregates still use the existing aggregate representation and can incur its
copying/compilation costs; this is not a new array ABI.

The Gloin passes remove these operations before standard conversion.
`--emit-ir` shows the source operations; `array_fill` is introduced by the GIR
preparation pass during lowering. `--emit-llvm` contains no Gloin operations. The older custom async
op is emitted only in an unchecked path; checked source rejects that feature.
Checked modules reject direct `llvm.call` in high-level IR; external runtime
declarations and layout preparation remain LLVM dialect operations for now.

This is still a partial architecture correction. Checked source storage,
struct access, pointer access, arithmetic, comparisons, and aggregate construction
now use Gloin operations. Unary floating negation and boolean inversion use
standard arithmetic operations. Runtime calls now pass through `gloin.abi_call`, but
string representation, runtime declarations, ABI value preparation, and defer
bookkeeping still emit LLVM dialect operations directly. Checked string literal
assembly now uses `gloin.string_literal`; global byte storage and its address
still use LLVM operations. The arena allocation path also still writes the
initialized value with an LLVM store. Source-typed aggregate construction,
field extraction, and enum operations reject operands with the wrong nominal
source type, even when the LLVM layouts match. Checked pointer and storage
operations have typed SSA addresses and values, so their verifiers reject a
wrong pointee or a store through a read-only address. Backend preparation
still uses layout values after explicit bridges. New source constructs should
use typed Gloin operations and cross to layout only at storage or ABI boundaries.

In particular, field and array element addresses verify source-typed bases
and results. A field address checks its field index and projected source type
against the nominal struct definition. A read may weaken the outer capability
of a pointer field, such as viewing a writable pointer as read-only, but cannot
change its nominal pointee or strengthen that capability. Struct literals also
check their field types and layout against the same definition. Semantic
analysis still selects field indices and checks visibility and mutability.
After the source/layout bridge, an LLVM struct holding an enum or result loses
its nominal identity. The compiler keeps those values source typed until the
single Gloin-to-LLVM boundary, where the signature pass converts them and
removes the bridges. The remaining backend cleanup is to move string and
defer internals behind the ABI boundary and reject unrelated direct LLVM
operations in checked high-level IR. This cleanup retains `func`, `arith`,
and `cf` for generic MLIR machinery; the current result operations already
have typed GloinIR representation and verification.

## One conversion pipeline

[lowering.cpp](../src/lowering.cpp) owns the only conversion pass list:

1. For checked executable/JIT input, prune unreachable source functions in GIR.
2. Prepare stored repeated arrays as verified GIR fills and simplify proven-safe
   integer constants/identities; preserve the checked CFG and operand effects.
3. Checked function signatures and calls to their recorded LLVM layouts; remove
   explicit source/layout bridges.
4. Promote eligible GIR locals with MLIR memory-slot interfaces and prove unsigned
   ranges/bounds from SSA values and dominating branches. Lower checked runtime
   ABI calls, constants, arithmetic, guards, aggregates, access, storage and enums
   to standard arithmetic/control flow and LLVM operations, retaining unproven checks.
5. SCF to control flow.
6. Control flow to LLVM.
7. Arithmetic to LLVM.
8. Functions to LLVM.
9. Final memref conversion to LLVM.
10. Reconcile unrealized conversion casts.

<a id="checked-integer-simplification-and-overflow-unreleased-perf-005"></a>

### Checked integer simplification and overflow (0.2.0, PERF-005)

Integer arithmetic enters lowering as `gloin.checked_int_binary`, including its
signedness. Before source layouts are substituted, GIR preparation folds an
operation with two constant operands only if its exact mathematical result is
representable and its division/remainder preconditions hold. It also simplifies
`x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x`, `x / 1`, multiplication by zero and
`x % 1`. Operand producers stay in their original evaluation order: simplifying
`danger() * 0` must still call `danger`, and `(maximum + 1) * 0` must still trap.
Potentially trapping checked operations remain effectful even when their value
becomes unused. Failed constant checks remain runtime traps; this pass does not
turn ordinary runtime expressions into new compile-time diagnostics.

Remaining `+`, `-` and `*` operations become the corresponding signed or unsigned
LLVM `*.with.overflow` intrinsic at the original 8/16/32/64-bit width. Its result
is paired with an overflow bit; `gloin.assert(!overflow)` lowers to a conditional
trap. The compiler no longer constructs doubled-width arithmetic plus
truncate/extend comparisons for these operations. LLVM may remove a guard when
it proves that overflow is impossible. This preserves checked arithmetic at
both `-O0` and `-O2`; it does not introduce wrapping arithmetic or fast-math.

Division and remainder keep their explicit zero-divisor guard and, for signed
types, their minimum-value/-1 guard. Signed remainder traps for that pair too,
even though the mathematical remainder would be zero. No floating-point checks
change. This constant/identity pass does not infer loop ranges. The subsequent
PERF-006 analysis described below can discharge unsigned checks using GIR values
and dominating control flow.

Native `-O2` also bounds optional LLVM inlining: once a caller contains at least
2,048 LLVM instructions, further optional calls remain calls. Below that point,
LLVM's normal profitability decisions apply; mandatory inlining retains LLVM's
rules. This is a growth threshold, not a hard maximum function size: one inline
can cross it, and a large source function can already exceed it. It prevents
repeated inlining from expanding arithmetic call chains into enormous blocks
that are expensive for LLVM's known-bits analysis and machine combiner.
The policy lives in native optimization, after GIR lowering, and applies equally
to object and executable output. `-O0` and JIT do not run this native O2 pipeline.
It changes optimization decisions without weakening checks or changing source
evaluation semantics. Retaining a call can forgo further specialization or
constant propagation; the threshold is an initial compiler policy, not a
language guarantee or a promise of faster execution for every program.

<a id="exact-integer-widening-unreleased-perf-008"></a>

### Exact integer widening (0.2.0, PERF-008)

Sema resolves the private `@std` primitive `__std_i64_from_i32` through the
checked standard-call table and requires an `i32` plus a writable `&i64` output.
Codegen emits `gloin.exact_int_widen`, a source-typed `gloin.store`, and status
zero (`OK`). Following the 0.2.0 standard-library migration, the public
`std.i64_from_i32` wrapper returns plain i64; exact widening cannot fail. Its
internal primitive still writes the output and returns zero for ABI compatibility.
User functions with similar names are ordinary calls, and replacement standard
libraries retain their own wrapper bodies.

The pure GIR operation carries source signedness explicitly because MLIR integer
types are signless. Its verifier requires scalar signless 8/16/32/64-bit integer
types and a strictly wider result. Same-width conversions, narrowing, booleans,
floats, vectors and unsupported widths are rejected. Lowering emits `arith.extsi`
or `arith.extui`, then LLVM sign/zero extension. The compiler currently emits
this operation for the signed i32-to-i64 primitive only; supporting additional
width pairs in GIR does not expose additional source conversion APIs.

Every i32 value is representable in i64, including the signed minimum. No
runtime call, range check, allocation or failure path is needed for this
conversion. JIT, native `-O0`, and native `-O2` share this lowering; native
optimization may inline the public wrapper and fold the status/store as well.
The existing runtime ABI symbol remains implemented and validated for consumers
of previously emitted IR. Narrowing, sign-changing and floating/rounded
conversions keep their runtime status and payload rules. No implicit casts or
arithmetic, bounds or null-check relaxations are introduced.

<a id="bounds-and-unsigned-range-proofs-unreleased-perf-006"></a>

### Bounds and unsigned range proofs (0.2.0, PERF-006)

After resolving source layouts, while operations still carry GIR semantics,
the compiler runs MLIR memory-slot promotion on integer and slice-descriptor
locals. GIR stack allocations, loads and stores implement the promotion
interfaces through internal models. MLIR constructs SSA block arguments at
joins and loop backedges. Only exact whole-slot loads and stores qualify:
passing a slot's address to a call, deriving an alias, or another unsupported
use prevents its promotion. Array storage and its element loads remain memory.
Definite initialization remains a source-checking requirement.

The range analysis uses unsigned constants, zero extension, checked arithmetic,
constant array-slice lengths and the outcomes of dominating unsigned comparisons.
A conditional edge supplies a fact only when its successor has that branch as
its sole predecessor and dominates the operation being checked. For example,
the continuation after `if index >= source.len { return ...; }` proves
`source[index]` safe for that exact SSA slice and index. A successful
`index + 4 <= source.len` loop test can justify `source[index + 3]` and the
body's repeated `index + 4`; the addition in the loop test still requires its
own overflow check. An unsigned byte index is always below 256, so a checked
256-entry table needs no per-lookup bounds check.

Unsigned intervals also prove operations such as `a * 4 + b / 16` safe when
both bytes have passed `< 64` guards. Division/remainder require a positive
divisor. These operations lower to ordinary arithmetic only after the proof;
unproven operations retain overflow intrinsics or explicit guards. Address
operations similarly omit only a proven bounds assertion. Proofs live in
temporary compiler-owned sets, not trusted attributes in input IR. The same
analysis runs for JIT, native `-O0`, native `-O2` and object output.

Facts describe SSA snapshots. Reassigning a promoted index or slice produces a
different value; a join or loop backedge cannot reuse an earlier snapshot's
proof. A fresh element load is never treated as an earlier load, even through a
read-only slice: other aliases can mutate its backing storage. Calls and memory
effects remain in their original order. A proof does not grant non-aliasing,
remove a null check or change arithmetic/trap semantics.

This is deliberately bounded analysis. It does not solve arbitrary induction
variables, infer loop modulo invariants, combine general short-circuit/merged
conditions, analyze signed ranges, prove general alias equivalence, or change
floating-point policy. Recursive expression queries and affine offsets have
fixed depth limits; unknown facts keep checks. LLVM may independently remove
further checks under `-O2`. Reduced pre-LLVM guards do not by themselves imply
faster machine code; the [performance record](compiler-validation.md) separates
compiler changes from the Base64 loop restructuring.

<a id="counted-slice-loop-versioning-unreleased-perf-007"></a>

### Counted slice loop versioning (0.2.0, PERF-007)

After the range analysis, GIR can create two copies of a small counted loop.
An entry predicate compares the loop limit with the lengths of the exact SSA
slice descriptors used by the loop. If all lengths cover the limit, the fast
copy omits those per-element bounds assertions. Otherwise execution selects
the original checked loop. **A failed entry predicate does not trap.** Earlier
iterations, calls, writes and arithmetic failures still happen in source order;
an empty or negative-count loop does not start accessing elements.

The unit-stride recognition is deliberately narrow: a promoted `i64` or `u64`
counter starts at zero, the header tests `i < limit`, a single straight-line
body increments by exactly one, and its slice accesses use `i` directly.
The limit and descriptors must dominate the entry. The header has only its
comparison and branch; the body has at most 64 GIR operations and at most four
distinct candidate descriptors. Loops with regions, body stack allocations,
or SSA values used outside the two blocks are left alone. Escaped counters,
different starts/strides and descriptors loaded or reassigned inside the loop
do not satisfy this proof. Other accesses within an eligible loop retain their
checks. Each original candidate is considered once, bounding cloning to one
extra copy of that candidate.

In the fast copy, `0 <= i < limit` also proves that the matching `i + 1` cannot
overflow, including when the limit is the signed or unsigned maximum. This is
a specific induction proof, not general signed range analysis. Like PERF-006,
proofs are temporary compiler-owned data; textual attributes cannot request
unchecked accesses. The transformation runs in JIT, native O0/O2 and object
lowering. It increases code size and adds an entry branch; a short loop is not
guaranteed to get faster.

**Floating-point semantics are unchanged.** Products are rounded separately,
sums retain their source order, and non-finite products or sums still trap.
The adjacent-operation rule below can let one sum check cover both failures;
the loop-versioning proof itself does not remove floating checks. There is no
reassociation, fused multiply-add permission or fast-math flag. In the measured
LLVM 21 matrix kernel, these remaining trap exits still
prevent vectorization: the fast loop uses scalar multiply and scalar add. See
the [PERF-007 record](compiler-validation.md) for controlled timings and LLVM
remarks, including C experiments that deliberately omit checks and therefore
do not implement the same safety contract.

### Floating SSA bounds (2026-10-19)

After the integer and storage-based loop proofs, exact nonescaping `f32` and
`f64` local slots are promoted to SSA. Promotion deliberately follows local
floating reduction recognition, which still needs the accumulator's storage.
Taking an address or exposing an alias blocks promotion. Opaque loads and call
results remain unknown. Private arrays can contribute the invariants below;
the floating type alone never establishes finiteness.

Before adjacent-check combining, a forward worklist computes finite interval and magnitude
bounds for constants, successful checked arithmetic, ordinary arithmetic,
negation, selects, and explicit CFG joins/backedges. The domain distinguishes
not-yet-contributed edges, finite bounds, and unknown values (including NaN and
infinity). All incoming edges participate, including late backedges. Increasing
bounds widen to the largest finite magnitude after four increases, bounding
analysis work for growing loops. Unknown inputs still invalidate finite facts.
The worklist uses deterministic IR order.

A successful checked add, subtract, or multiply also implies finite operands.
That implication refines its result bound even when an operand came from opaque
storage: for example, checked `x * 0.5` has magnitude at most half the largest
finite value. The same refinement applies to division by a finite nonzero
constant. This implication only bounds the result; it cannot remove that
operation's own check.

Bounds use LLVM APFloat in the source format, with round-to-nearest ties-to-even.
Addition/subtraction use the sum of operand magnitudes; multiplication uses
their product. Division requires a known finite, nonzero constant divisor.
Rounding the extremum bounds interior results because these positive operations
are monotone. Underflow is allowed and signed zeros retain ordinary IEEE
behavior. No algebraic rewriting, reassociation, contraction, or fast-math flags
are introduced.

Only a checked operation whose operands prove a finite result is replaced by
ordinary arithmetic, subject to a separate profitability policy. A checked producer may establish finiteness, but remains
effectful unless independently proved safe. Decisions use the completed fixed
point before any operation is rewritten. This supports shrinking recurrences
and branch joins without removing the necessary check in a growing recurrence.
It also prevents a later multiply by zero, a discarded result, or a clamp from
hiding earlier overflow. Checks never move across effects or memory accesses.
LLVM `freeze` is opaque outside the stabilized array analysis below: freezing
poison can produce a non-finite value.

For profitability, checks in conditional natural-loop blocks are retained. An
operation's block must dominate every latch of its innermost loop to be eligible
for removal. Removing the last effect in a conditional path can otherwise cause
LLVM to speculate floating multiplies into selects, lengthening the recurrence's
dependency chain. The particle experiment reproduced a substantial regression
from that transformation despite fewer checks. The retained operations still
contribute their result bounds to unconditional work. This conservative policy
can be refined with target cost/profile evidence; it is not a safety requirement.

The analysis does not infer bounds for heap fields, inspect unknown callees,
or synthesize source guards. Existing reduction exit checks remain intact.
The Gloin programs in `tests/float_proofs/` exercise native O0/O2 and JIT
semantics plus structural LLVM-lowering checks for both floating widths.

### Private floating array invariants (2026-10-23)

Flat, nonescaping stack arrays of f32/f64 can carry an invariant joining their
whole initializer and **every** scalar write. Array indices are not distinguished.
Borrowed and overlapping subranges contribute to the same root; no disjointness
is assumed. Every load needs a dominating whole-array initializer. Unknown writes
invalidate finite facts. Calls exposing the root, unknown derived uses,
incompatible widths, unsupported layouts, stack restore, and raw LLVM stores
disable the relevant proof. The scan examines at most 64 allocations and 256
derived values per root.

Comparisons with finite constants narrow already-finite values on dominated
branch paths and explicit outgoing CFG edges. False ordered comparisons do not
establish finiteness. Signed interval endpoints survive joins, including clamps;
ordinary arithmetic still uses conservative magnitude bounds and source rounding.
All entry writes and backedges join before checks are removed. The existing
widening limit prevents loop simulation.

In functions with accepted array roots, opaque operands of checked floating
arithmetic are frozen once at their definitions. This establishes concrete
checked results at the public mixed GIR/LLVM boundary, where undef/poison would
otherwise invalidate per-value branch facts. Freeze transfers finite facts only
within this stabilized analysis; opaque poison, undef and loads remain unknown.
The batch pass recomputes the proof after check combining.

The particle benchmark explicitly validates and bounds its fast-path input,
then uses the scalar algorithm for batches outside that domain. This is source
code, not a compiler assumption about typical inputs. Energy overflow checks
remain. The proof introduces no fast math, reassociation, FMA contraction or
unchecked arithmetic.

The Gloin driver in `tests/float_arrays/` checks both floating widths structurally
and executes defined fixtures at O0/O2. It covers clamped and growing recurrences,
late writes, overlapping slices, unknown storage, signed zero, subnormals, and
retained overflow traps. Undefined-input and foreign-layout cases are structural
only. Existing native/JIT floating suites remain part of check-core.

### Adjacent floating checks (2026-10-19)

Extended on 2026-10-20 to bounded expression trees. After range proofs,
floating-local promotion and finite-bound proofs, `combine_gloin_float_checks`
examines verified GIR before general core lowering. It groups contiguous runs
of checked `f32` or `f64` `+`, `-`, and `*` operations of one type. A window
contains at most 64 operations, bounding temporary storage and analysis work.
Every other operation is a barrier, including division, constants, ordinary
arithmetic, loads, stores, calls, assertions, and control flow.

Within a window, a checked result with exactly one use by another member can
lose its own check. Its consumer necessarily propagates a non-finite input:
ordinary IEEE addition, subtraction, and multiplication cannot turn NaN or
infinity into a finite result, including `Inf * 0` and `Inf - Inf`. Following
single-use dependencies must end at a retained check, because SSA is acyclic
inside a block. Unused results and results escaping the window retain checks.
For example, `(a*a + b*b)*c + d` can use one final check when its five checked
operations are contiguous. Intermediate arithmetic retains source order,
separate rounding, and original locations; no reassociation, fast-math, or
contraction is enabled. A final result that is unused must still be checked.

The rewrite freezes every external input to a changed window before its first
operation. This includes inputs to independent intermediate checked operations,
not just the last consumer's other operand. The public lowering API accepts
mixed GIR/LLVM modules; a later poison value must not create undefined behavior
that hides an earlier deferred failure. After freezing external inputs, ordinary
IEEE operations inside the window cannot produce poison. Freezing preserves
concrete values, including NaNs, infinities, and signed zeros. It neither
initializes source storage nor justifies moving a memory read.

Checks never cross the next barrier or a block boundary. At least one effectful
check remains on every combined dependency chain, including within conditional
loop blocks. The pass makes no pointer validity, lifetime, or aliasing
assumptions and does not defer checks across loop iterations. The separate
[local reduction proof](#local-floating-reductions-2026-10-19) supplies additional
storage and iteration facts for its narrower family of loops.

All finite-result failures currently use the same fatal trap without identifying
the failed operation. An independent intermediate check can trap before a
deferred producer's final check, with the same observable result. Distinct
failure diagnostics would require preserving the first failing operation before
using this transformation under that richer contract.

The Gloin `tests/float_chains` suite checks execution in JIT and native O0/O2,
check counts for both widths, external-input freezing, effect barriers, escaping
and discarded results, overflow cancellation, multiplication by zero, signed
zero, underflow, and separate rounding. Existing mixed-IR regressions continue
to test poison/undef through LLVM O2. See the
[validation journal](compiler-validation.md#floating-check-chains--2026-10-20).

### Local floating reductions (2026-10-19)

`version_gloin_float_reductions` runs after local slot promotion and ordinary
range analysis, before general slice-loop versioning. It recognizes an ordered
dot product over storage whose initialization and lifetime GIR can establish.
It creates a fast copy with one final `gloin.require_finite` and retains the
original checked loop as a fallback. Both copies are excluded from further
slice-loop cloning. The adjacent-check rule still applies to the fallback.

#### Arithmetic and control flow

The accumulator must be an unescaped local `f32` or `f64` slot initialized to
zero. The body loads it, multiplies two input elements, adds the product to the
accumulator in either operand position, and writes the sum back to that slot.
The product has one use in the addition; the addition has one use in the update.
Other writes to the accumulator may only initialize it to zero outside the body,
and a zero initialization must dominate loop entry. A private accumulator store
is unobservable because no address or intermediate value escapes.

The counter is an SSA `i64` or `u64` starting at zero, tested with `i < n` and
incremented by one. The body is straight-line and returns to the header. No
counter, header result or body SSA result may escape these blocks. This proves
termination and counter safety for reached iterations, including maximum limits.
The bound must dominate entry or be a slice length read from a dominating
descriptor. The recognizer limits the body to 48 operations.

For this recurrence, a non-finite product makes the sum non-finite, and subsequent
additions cannot make it finite again. A finite final sum therefore implies
finite products and intermediate sums. The fast copy uses separately rounded
`arith.mulf` and `arith.addf`, preserving operand order and introducing no
fast-math or contraction flags. Each input value is frozen at the mixed GIR/LLVM
boundary so a later poison/undef operand cannot suppress an earlier failure.
Freezing does not establish that a memory read is valid.

`gloin.require_finite` is an effectful, resultless operation accepting only `f32`
or `f64`. It traps on infinity or NaN, including when the reduction result is
unused. It executes on the fast exit before the continuation can observe the
accumulator. The zero-iteration fast path checks the already finite zero;
negative signed limits select the original loop and perform no element reads.
Current arithmetic failures have one fatal-trap behavior. Distinct diagnostic
identities or source locations for the first failure would require additional
handling before using this transformation under that richer contract.

#### Storage and effect proof

Each input address must be an indexed fixed array or a slice derived from a fixed
array. GIR traces it back to a matching `gloin.stack_alloc`, with a dominating
whole-value store or `gloin.array_fill`. Nested array elements and slice subranges
are accepted when their checked construction already dominates entry. Provenance
traversal is bounded to eight levels.
The complete nested extent must fit a signed 64-bit byte count, and the local
allocation must use the ordinary native address space. An array type alone does
not authorize wrapped size or address calculations.
Explicit mixed-IR layouts with different pointer/index widths or floating
allocation strides are rejected. A function containing LLVM stack restoration
is also rejected, since that operation can end local lifetimes without using
the allocations' addresses.

The proof follows every derived address and descriptor use, up to 256 values.
Only recognized loads, stores, array fills, array/slice element addresses,
array-to-slice construction, subranges and length reads are accepted. Passing an
address or descriptor to a call, storing it elsewhere, or using it through an
unknown operation rejects the proof. This ensures the allocation remains local
and live. The two inputs may overlap: the reduction performs no writes to them.
Read-only slice types alone do not establish storage validity or prevent aliases.

Inside the body, only the two proven input reads, private accumulator read/write,
recognized arithmetic/counter operations, constants and backedge are allowed.
Calls, external stores, volatile/atomic or unknown reads, assertions, unrelated
arithmetic checks, conditional exits and unknown operations prevent deferral.
Consequently finishing the loop after an arithmetic failure cannot introduce
another observable effect, memory fault or language failure.

An unsigned entry comparison requires `n` to fit every input's fixed capacity or
slice length. It does not read the elements or multiply the bound by an element
size. A failed predicate selects the original checked loop rather than trapping
at entry. Original reads, partial progress and failure ordering are retained.
The static storage proof and this runtime range predicate are both required.

#### Current scope

Local initialized fixed arrays, nested array rows and their borrowed subranges
can qualify. Arbitrary slice parameters, arena/vector buffers, raw pointers,
escaped storage and missing or conditional initialization do not qualify.
There is no new source annotation, slice ABI, runtime allocation registry or
interprocedural lifetime assumption. Textual attributes cannot assert a proof.
This is a local optimization, not a general source-language lifetime checker.

Removing the inner finite exits allows LLVM to vectorize eligible products while
keeping additions in source order on ARM64. Target cost models still decide
whether to vectorize. A fast copy, final check and fallback increase code size;
small loops are not guaranteed to improve. The
[validation journal](compiler-validation.md#local-floating-reductions--2026-10-19)
records correctness and measurements separately from the generic borrowed-slice
benchmark, whose storage remains unproven.

### Checked private-array batches (2026-10-20)

`version_gloin_float_batches` runs after floating-local promotion, finite-bound
proofs, and expression check combining. It creates a fast loop with a persistent
validity flag, keeping the original checked loop as its fallback. The ordinary
native LLVM pipeline can then vectorize eligible independent lanes. No source
annotation, vector ABI, fast-math flag, FMA contraction, or global optimizer
threshold is introduced.

The recognizer accepts an innermost natural loop with a single entry and latch,
one i64/u64 counter starting at zero, `i < n`, and a unit increment. Removing the
backedge must leave an acyclic region of at most 32 blocks and 256 operations.
Branch diamonds are supported, but internal exits, additional cycles, and SSA
live-outs are rejected. Only the counter is loop-carried before transformation.
The count is a dominating value, a header constant, or a length of a dominating
slice descriptor. An existing slice-versioning true edge can be specialized;
its original false-edge fallback is retained without another specialization.

Every memory access must index the counter directly into a proven private
floating array or its borrowed view. The shared `LocalStorage` proof requires
an actual local allocation, a dominating whole initializer, ordinary native
layout, and confined address/descriptor uses. Nested arrays and subranges are
supported; provenance depth is capped at eight and each confinement walk at
256 values. Each load/store width must match the addressed element. Unknown
storage, escaped addresses, LLVM stack restoration, raw/volatile accesses,
external writes, calls, and unrelated fallible operations reject the batch.
Checked division is not deferred. Source-visible pointer and slice types alone
cannot establish this proof.

An unsigned entry predicate requires the count to fit every accessed capacity.
It performs only non-faulting SSA/descriptor work. Failure selects the original
loop, preserving partial progress and error order, including signed negative
counts and oversized ranges. The guard establishes every iteration's address
validity and the counter increment's safety. Views may overlap: the pass adds
no no-alias claims and does not reorder iterations. LLVM must independently
prove vectorization legal or retain scalar execution.

The fast copy replaces each remaining checked `+`, `-`, or `*` with separately
rounded arithmetic and accumulates its finite-result predicate into a validity
flag. Each CFG edge and backedge carries that flag. Once false, it remains false,
including through clamps, overwritten state, and later successful iterations.
The sole fast exit asserts the flag before any continuation can observe the
private state. Zero iterations leave it true. All reads and writes that occur
after a deferred failure remain within proven live, initialized private storage;
no external effect or different language failure can intervene.

Every external scalar input and every floating load in the fast copy is frozen.
The supported internal operations cannot create poison from those inputs.
Thus a later poisoned load, branch condition, or operand cannot hide an earlier
arithmetic failure at the public mixed GIR/LLVM boundary. Freezing values does
not establish memory safety; that is proved separately before cloning.

Finite bounds are recomputed on the current IR after expression combining.
Checks retained solely for scalar profitability need no flag update in the fast
copy. Reusing facts from before combining would be unsound: combining may move
the producer check that anchored a consumer's finite bound. The original scalar
fallback retains its profitability policy. Current failures are fatal traps
without per-operation diagnostic identity; richer failure diagnostics would
require additional handling before using this transformation.

#### Scheduling short constant batches (2026-10-23)

The proven fast copy receives an LLVM interleave-count hint of two for a direct
constant bound of eight, or four for direct constant bounds from 16 through 32.
The original operation count multiplied by that bound must be at most 4096. The hint is attached
only to the fast copy's latch; fallback and dynamic loops keep the ordinary cost
model. The matcher already limits loops to 256 operations and 32 blocks.

This is a bounded profitability policy, not a new safety proof. LLVM still
chooses the vector width, proves memory dependences (including overlapping
views), and generates any scalar tail. The hint does not assert independence,
force a vector width, remove a check, or enable fast-math. It groups vector
iterations rather than forcing complete scalar unrolling before vectorization.
The native O2 pipeline and target selection remain unchanged.

On the tested AVX2 host this schedules a 16-element f64 particle batch as four
vector groups without the inner four-trip vector loop. Finite masks combine
across those groups before the same checked exit. There are still three
finite predicates per four-particle group. Target-specific profitability on
other CPUs is not established; scalar-only batches retain the prior policy's
known profitability limitation.

The Gloin scheduling fixtures check f32/f64 constant bounds 16, 17 and 32,
smaller/larger/dynamic loops, code-growth refusal, overlapping views, first and
last failing lanes, clamps, signed zero, underflow and separate rounding. The
structural test verifies that only the fast copy carries the hint.

The eight-iteration policy was added on 2026-10-23 after register-pressure
experiments. An eight-lane particle layout plus two-way interleaving lowers the
hot loop's stack-access instruction count from 74 per sixteen particles to four
per eight particles when paired with combined collision conditions. These are
static stack-access counts, including array accesses, not hardware spill events.
There are still three finite predicates per four particles. The old sixteen-lane
executable is byte-identical under both compilers. Alternate bit/exponent masks,
a maximum-encoding accumulator and post-vectorization unrolling were rejected
after measurements; none is part of the selected compiler.

#### Combining predicates within a proven batch

Before cloning, a reverse walk of each accepted basic block identifies checks
whose failures must reach a later retained predicate. The walk seeds coverage
only from checks that emit a predicate, then propagates it backwards through
checked or raw IEEE `+`, `-`, `*`, unary negation, and floating freeze. Every
non-finite operand remains non-finite through these operations, including
`Inf * 0` and `Inf - Inf`. Batch matching rejects fast-math flags; the fast
copy's existing freezing policy prevents poison from invalidating this argument.

The covered producer keeps its arithmetic and loses only its flag update.
Multiple uses and intervening private loads/stores are allowed because the
entire batch already proves their safety, termination, and lack of observable
effects before the checked exit. Values stored to private memory may be used
elsewhere, but a later retained predicate in the same block still executes.
No arithmetic is reordered, reassociated, or contracted.

Each block starts with an empty coverage set. A conditional consumer or a
consumer beyond a block boundary cannot justify removal. Selects, block
arguments, and memory reloads do not propagate coverage. Unused failing
operations retain a predicate unless their result also reaches a retained sink.
Range-proven checks never seed coverage: such a check emits no flag, and its
finite fact may depend on the earlier check under consideration. Reverse order
ensures every removed predicate ends at a retained one, without circular
reasoning. Work is bounded by the existing 256-operation batch limit.

Gloin tests in `tests/float_batches` exercise normal results, signed zero,
underflow, rounding, first/last-lane overflow, clamps, discarded results, empty
and negative ranges, bounds fallbacks, overlapping views, and refusal cases.
A thin public-lowering adapter lets the Gloin driver construct mixed-IR tests
for poison/undef, NaN/infinity, invalid reads, mismatched widths, missing
initialization, stack restoration, and unusual layouts. Native failures are
compared with a control program's trap signal, not merely any child crash.
The coverage driver adds exact predicate counts and native execution checks for
intervening stores, multiple uses, raw arithmetic, discarded sinks, poison/undef,
conditional consumers, selects, reloads, and safe consumers that cannot act as
check anchors.

### Strided groups and live tail counters (2026-10-18)

GIR also recognizes unsigned `u64` loops with the shape `i = 0; i + S <= n;
i += S`, where `S` is a positive constant. Secondary counters start at zero
and advance by a positive constant `P` on every backedge. A slice access at
`j + offset` is eligible when `0 <= offset < P`. Up to four chained constant
additions may form the offset. For a three-byte input/four-byte output codec,
this includes `output[j]` through `output[j + 3]` with `i += 3; j += 4`.

The entry predicate computes `groups = n / S` and requires both:

- `groups <= slice.len / P` for each candidate descriptor/counter stride.
- `groups <= (u64_max - S) / S`, so the final header test's `i + S` is also
  representable after all complete groups have run.

Division by known positive constants avoids overflowing a capacity calculation.
In iteration `k < groups`, `j = k * P`, so `j + offset < groups * P <= slice.len`;
the backedge's next `j` is representable as well. Only these bounds, offset
additions, matching counter increments and the primary header addition receive
new proofs. Other arithmetic and accesses retain their checks. A failed
predicate still selects the original loop, including cases where its first
few writes succeed before a bounds or arithmetic trap. It never traps early.

The header has at most eight GIR operations: its arithmetic/comparison/branch,
constants and, optionally, the limit slice's length. That slice descriptor or
the scalar limit must dominate the entry. The single straight-line body has
at most 128 operations and at most four distinct descriptor/stride constraints.
Regions, body stack allocations, nonzero starts, variable or signed strides,
and escaped/reloaded induction variables are outside this proof. Candidate
slice descriptors must dominate entry; elements may alias other slices, and
memory operations and calls keep their order. Unsupported accesses in an
otherwise eligible loop remain checked.

Header SSA arguments used after the loop are merged at its shared exit. This
preserves both counters for a padded tail and any other promoted loop state.
The exit must have the original header as its sole predecessor and dominate
all those external uses; operation results escaping the two loop blocks remain
unsupported. The tail keeps its checks. This extension adds one guarded copy
per recognized loop and does not promote constant lookup tables or change
floating-point rules. The [validation record](compiler-validation.md) records
correctness checks and the measured encoder result.

<a id="executable-reachability-unreleased-perf-004"></a>

### Executable reachability (0.2.0, PERF-004)

After complete source checking, GIR generation, verification and initial legality
checking, executable lowering treats `main` as the source entry root. The pass
follows MLIR symbol references through function bodies until no new functions are
reachable. Ordinary calls, methods, recursive cycles and instantiated generic
functions therefore use the same graph. A recursive group with no path from a
root is removed. This is function reachability: branches and runtime conditions
are not evaluated by this pass.

References from retained non-function operations or module metadata also root
their targets. Runtime ABI declarations, LLVM functions, globals and nominal
type declarations are retained. The native runtime's reserved argument-entry
names remain visible so existing collision diagnostics are preserved. The pass
uses both module-attribute and module-region symbol uses; the module itself is
a symbol-table boundary. Unknown symbol scopes conservatively prevent pruning.

Only checked GIR source function definitions are candidates. `--check`,
`--emit-ir`, `--emit-llvm` and `--emit-object` retain the complete function set.
Object output still requires `main`, and retains its existing externally callable
symbols. Source `pub` controls module visibility; it does not independently root
an unused function in a standalone executable. Pruning runs for native executable
output at both `-O0` and `-O2`, and for JIT lowering of checked GIR.

`lower_to_llvm` defaults to `CompilationMode::Module`, which preserves functions.
Its explicit `Executable` mode enables pruning; `Object` preserves definitions.
Arbitrary unmarked MLIR and already lowered input retain their existing behavior.
JIT lowers an owned clone with executable reachability, so the caller's original
GIR remains intact. All source diagnostics and initial IR verification/legality
checks precede pruning; an unused invalid function is still rejected.

The timing report exposes `gir_reachability` and a `reachable_gir` snapshot before
array preparation and source-type erasure. The `gir` snapshot and inspection
output still describe the complete generated module. Parsing, semantic checking
and GIR generation costs are therefore not eliminated by this pass.

`lower_to_llvm` verifies the input before conversion, enables verification after
each pass, checks final legality, and verifies the final module. Input operations
must be registered `func`, `arith`, `cf`, `scf`, `memref`, or `llvm` operations,
the checked Gloin operations above, the root builtin module, or temporary
unrealized conversion casts. Nested modules and other Gloin/custom operations
fail explicitly. Gloin source types are accepted in checked function signatures,
selected SSA values, and the supported operations' source type attributes;
unrelated custom types still fail. Types nested in
signatures, operands/results, block arguments, and attributes are checked too.
An accepted input dialect is not a promise that every operation can be lowered;
unsupported conversions still fail.

Successful output contains only the root builtin module and registered LLVM
operations with LLVM-compatible types. High-level operations, custom types,
and unrealized casts cannot cross this boundary. The LLVM constant operation's
index-typed integer payload is allowed because LLVM's converter/exporter uses
it with a concrete integer result; runtime index types remain forbidden. Raw
SCF/memref fixtures exercise this internal compiler capability independently
of source-language fixed arrays or structured IR syntax.

`JitRunner` and the `run_external_module` test helper both lower owned clones
through this function. The external helper then uses `mlir-opt` only to
parse/verify the serialized LLVM module and `mlir-runner` to execute it. It no
longer maintains a separate conversion pipeline. Low-level subprocess harness
tests retain `run_external_mlir` for already-lowered IR and controlled fake tools.

## Native target configuration (2026-10-18)

Native objects and executables use host CPU/features with LLVM's ordinary AoT
target defaults. The native emitter creates its target machine with `JIT=false`
and default `TargetOptions`; Linux retains explicit PIC relocation for normal
PIE linking. Both native `-O0` and `-O2` use this configuration. JIT execution
continues to use its own target setup.

Previously, native emission also called
`JITTargetMachineBuilder::createTargetMachine`. That API passes `JIT=true` to
LLVM, which defaults to the large code model on the supported AArch64 and
x86-64 targets. Known function calls could therefore become address
materialization followed by an indirect call. The large model accommodates
JIT allocations whose addresses can be far apart; linked native output uses
the platform's ordinary code model and linker relocations instead. See the
[LLVM builder](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.6/llvm/lib/ExecutionEngine/Orc/JITTargetMachineBuilder.cpp#L35-L51)
and [AArch64 target defaults](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.6/llvm/lib/Target/AArch64/AArch64TargetMachine.cpp#L305-L327).

Source operations still pass through checked GloinIR and shared LLVM-dialect
lowering. This change is after that boundary: a direct `func.call`/`llvm.call`
already identifies its callee. It changes native instruction selection and
addressing, without removing language checks or changing function signatures.
It does not force genuinely indirect source calls to become direct.

The native code model now has ordinary target address-range constraints;
there is no user-selectable large-model mode. Direct call relocations may use
linker stubs or branch islands where the target/linker supports them. The CLI
regression checks direct external runtime-call relocations in emitted objects
at both optimization levels and executes recursive code in JIT and native
modes. Measurements and validation are recorded in
[the compiler validation journal](compiler-validation.md#native-call-generation--2026-10-18).

## Ownership and errors

`verify_module` borrows its module. `lower_to_llvm` consumes an
`mlir::OwningOpRef<mlir::ModuleOp>` and returns ownership only after success.
Failure destroys partial IR and returns an empty owner. Consumers retaining
high-level IR explicitly clone it before lowering. The caller owns the MLIR
context and keeps it alive through every module's lifetime.

Malformed input IR receives a `Verification` diagnostic. Unsupported operations,
conversion errors, and final legality failures receive `Lowering` diagnostics.
MLIR error handlers capture errors even if a pass reports success. Earlier
source errors keep their lexical/parsing/semantic/codegen stage and stop before
verification. Existing errors prevent a new successful lowering result.

File/line/column locations survive lowering. Available owned source text is
mapped back to byte spans. Imported IR without source text uses the diagnostic's
explicit position; it does not manufacture source text. Unknown locations render
as `<unknown>:1:1`. Consumers render collected errors at their boundary.

## Verification and remaining work

`LoweringTest` checks real LLVM IR export and LLVM verification, every scalar
signature, source-width signed/unsigned overflow intrinsics, nested source control flow,
SCF/memref conversion, ownership, source positions, unsupported IR, unresolved
casts, and failed conversion. A source program is compiled in three independent
contexts, producing identical printed LLVM-dialect IR and returning -1 on all
three external executions. This establishes that fixture's repeatability on the
supported toolchain, not cross-platform or byte-identical native binaries.

All existing external language tests now use production lowering, including
arithmetic trap tests. SPEC-019 adds [JIT translation registration, validated
invocation, and separate execution failure/results](jit.md). [The file CLI](cli.md)
uses these APIs. [SPEC-021's source fixtures](../tests/fixtures/core/README.md) check
core acceptance; the [0.1.0 release guide](release-0.1.0.md) covers installation and packaging.

## Switch and match (SPEC-017a)

Checked dispatch remains in `gloin.switch` and `gloin.match`, with source-typed
subjects and a verified constant key table. Match owns one lazy region per arm,
ending in `gloin.match_yield`. Its output retains the checked source type until
signature lowering. Match effects include reads of borrowed string storage as
well as the effects of its arms; an SSA string descriptor does not make its
backing bytes immutable.

Selection lowering builds conditional branches and typed value joins. Result
patterns test the error tag and extract payloads through existing GloinIR result
operations. String comparisons first check length and then read bounded bytes,
including embedded NULs. Unmatched integer/string matches are impossible after
checking because default is required; an impossible unmatched finite tag traps.
There is no specialized jump-table or string-lookup optimization in this pass.
