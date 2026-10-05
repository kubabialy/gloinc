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
| `gloin.assert` | Trap if a checked runtime precondition fails, including bounds, null, and arithmetic checks | Conditional branch and LLVM trap |
| `gloin.checked_int_binary` | Signed/unsigned `+`, `-`, `*`, `/`, `%` with overflow and zero-divisor rules | Widened arithmetic or guarded division/remainder |
| `gloin.checked_float_binary` | Finite `f32`/`f64` `+`, `-`, `*`, `/` with zero-divisor rules | Floating arithmetic and finite-result guards |
| `gloin.checked_int_compare` | Checked integer and bool comparison with explicit signedness | Integer comparison with the selected signed or unsigned predicate |
| `gloin.checked_float_compare` | Checked `f32`/`f64` comparison, including unordered `!=` | Floating comparison with the selected ordered or unordered predicate |
| `gloin.array_literal` | Fixed-array elements in source evaluation order | LLVM array construction |
| `gloin.repeat_array` | One source value repeated across a fixed-array layout | Counted loop into a local array slot |
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

The Gloin passes remove these operations before standard conversion;
`--emit-ir` shows them and `--emit-llvm` contains none. The older custom async
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

1. Checked function signatures and calls to their recorded LLVM layouts; remove
   explicit source/layout bridges.
2. Checked runtime ABI calls, constants, arithmetic, runtime guards, aggregates, access,
   storage, and enum operations to standard arithmetic/control flow and LLVM operations.
3. SCF to control flow.
4. Control flow to LLVM.
5. Arithmetic to LLVM.
6. Functions to LLVM.
7. Final memref conversion to LLVM.
8. Reconcile unrealized conversion casts.

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
signature, internal wide-integer overflow checks, nested source control flow,
SCF/memref conversion, ownership, source positions, unsupported IR, unresolved
casts, and failed conversion. A source program is compiled in three independent
contexts, producing identical printed LLVM-dialect IR and returning -1 on all
three external executions. This establishes that fixture's repeatability on the
supported toolchain, not cross-platform or byte-identical native binaries.

All existing external language tests now use production lowering, including
arithmetic trap tests. SPEC-019 adds [JIT translation registration, validated
invocation, and separate execution failure/results](jit.md). [The file CLI](cli.md)
uses these APIs. [SPEC-021's source fixtures](../tests/fixtures/core/README.md) check
core acceptance; the [0.1.0 candidate guide](release-0.1.0.md) covers installation and packaging.
