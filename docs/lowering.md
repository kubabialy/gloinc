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
compiler now emits these checked Gloin operations:

| Operation | Source behavior retained | Lowering |
| --- | --- | --- |
| `gloin.from_layout`, `gloin.to_layout` | Explicit boundary between a checked source value and its storage layout | Removed after checked signatures and storage results become layout types |
| `gloin.abi_call` | Explicit checked call to a declared runtime ABI function with exact layout arguments and result | LLVM call after callee and signature verification |
| `gloin.constant` | Exact checked scalar literal or folded constant value | `arith.constant` |
| `gloin.exact_int_widen` | Exact widening of a supported integer with explicit source signedness | Sign or zero extension to the verified wider type |
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
| `gloin.error_literal`, `gloin.error_message` | Construct an error from a static message and read that message | Static string layout and field extraction |
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
structs, strings, enums, errors, and results. Calls and returns use those types too. Each checked
function records its LLVM-compatible argument and result layouts in
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
and may disappear only when LLVM proves the access valid. This improvement is
in the working compiler after 0.1.0; published 0.1.0 binaries still have the
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
This is the unreleased [PERF-003 fix](compiler-validation.md). It addresses
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

### Checked integer simplification and overflow (unreleased, PERF-005)

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

### Exact integer widening (unreleased, PERF-008)

Sema resolves the private `@std` primitive `__std_i64_from_i32` through the
checked standard-call table and requires an `i32` plus a writable `&i64` output.
Codegen emits `gloin.exact_int_widen`, a source-typed `gloin.store`, and status
zero (`OK`). The public `std.i64_from_i32` wrapper retains its concrete
`std.I64Result { status, value }` API. This legacy result struct is distinct
from the language's built-in `result<T>`; this change does not migrate that API.
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

### Bounds and unsigned range proofs (unreleased, PERF-006)

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

### Counted slice loop versioning (unreleased, PERF-007)

After the range analysis, GIR can create two copies of a small counted loop.
An entry predicate compares the loop limit with the lengths of the exact SSA
slice descriptors used by the loop. If all lengths cover the limit, the fast
copy omits those per-element bounds assertions. Otherwise execution selects
the original checked loop. **A failed entry predicate does not trap.** Earlier
iterations, calls, writes and arithmetic failures still happen in source order;
an empty or negative-count loop does not start accessing elements.

The current recognition is deliberately narrow: a promoted `i64` or `u64`
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
sums retain their source order, and each operation still checks for a finite
result. There is no reassociation, fused multiply-add permission or fast-math
flag. In the measured LLVM 21 matrix kernel, these remaining trap exits still
prevent vectorization: the fast loop uses scalar multiply and scalar add. See
the [PERF-007 record](compiler-validation.md) for controlled timings and LLVM
remarks, including C experiments that deliberately omit checks and therefore
do not implement the same safety contract.

### Executable reachability (unreleased, PERF-004)

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
