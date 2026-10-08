# Gloin 0.2.0 draft

Status: scope discussion after the 0.1.0 release. Switch and match are the
priority; their syntax and semantics await design decisions. The remaining
items below are candidates, not an agreed release commitment. The compiler
version and latest published documentation remain 0.1.0 while this is a draft.

The [0.1.0 release notes](release-notes-0.1.0.md) and
[validation record](next-release-draft.md) describe the starting point.

## Priority: switch and match

The current compiler reserves both keywords but rejects both constructs in
the supported compilation path. Enums currently have named variants without
payloads; pattern matching and exhaustiveness checking are unimplemented.
See [enums](enums.md) and [the parser contract](parser.md).

Design the two constructs together before implementing either. A useful
division to evaluate is value dispatch for `switch` and pattern-based
inspection for `match`. This is a proposal for their roles, not a syntax
decision. No historical syntax example establishes the final contract.

### Decisions to settle

| Area | Design questions |
| --- | --- |
| Role and syntax | What does each construct add? Are they statements, expressions, or both? How are arms and their bodies written? |
| Accepted inputs | Which integer, boolean, enum and string values can be switched on? Which patterns can be matched? Are ranges, grouped alternatives and guards in the first milestone? |
| Coverage | When must coverage be exhaustive? How are defaults, duplicate cases, overlapping patterns and unreachable arms handled? Do guards contribute to coverage? |
| Control flow | Is fallthrough prohibited? How do arm-local bindings, return paths and definite initialization join after dispatch? |
| Payloads | Can the first match milestone operate on existing payloadless enums and built-in results, or should payload enums arrive with it? |
| Binding and mutation | Do patterns copy values or borrow them? What can be mutated, and how long do bindings and any success/error proofs remain valid? |

`result<T>` already requires explicit handling. If matching results is added,
it must preserve that obligation: a success arm may expose only the success
payload, an error arm only the error, and a proof must not survive mutation
that invalidates it. `result<void>` needs an explicit rule too. General enum
payloads must not silently change the established result contract.

`break` and `continue` are also currently rejected. Decide their scope alongside
switch and loop interactions if they are included; they are not automatically
part of this milestone. A switch without fallthrough need not require a
trailing break in every arm.

### Implementation and acceptance

- Record the approved language rules in the wiki, then implement the checked
  parser, semantic analysis and diagnostics against those rules.
- Evaluate the subject once. Verify arm evaluation order, single-arm execution,
  exhaustiveness, binding scopes, result handling and definite initialization.
- Preserve language-specific type, variant and dispatch semantics in GloinIR
  until a dedicated lowering pass chooses the appropriate MLIR control flow.
  Specify and test dialect invariants; do not lower directly from the AST to
  LLVM and lose the source contract.
- Exercise nested dispatch, return/defer behavior, boundary values, rejected
  cases, generic use and module visibility in JIT and native `-O0`/`-O2` modes
  on macOS and Linux. Inspect both GloinIR and lowered output.
- Teach the accepted syntax with complete examples, formatter support and
  diagnostics. Update suitable standard-library dispatch code after the
  language behavior is verified.

Payload enums can be a separate milestone if the first match design does not
require them. Their representation, construction, active-payload access,
copying and initialization rules need their own explicit decisions.

## Recommended follow-up: endianness and packed layout

This is the recommended next implementation area after switch/match, subject
to choosing the final 0.2.0 scope. It addresses binary files and protocol work
with fewer new runtime policies than a scheduler and shared-memory concurrency.

Separate the contracts before choosing syntax:

1. **Byte order:** distinguish numerical values from their byte representation.
   Decide whether explicit endian storage types are needed in addition to
   bounded byte-buffer read/write operations. Define conversions and supported
   widths without requiring packed structs first.
2. **Struct layout:** distinguish removing byte padding from assigning individual
   bit fields. Decide size, alignment, field offsets, array stride, nesting and
   whether any foreign-function layout is guaranteed.
3. **Bit fields:** define bit numbering independently of byte order, signed
   extraction, writes, out-of-range values, overlapping fields and reserved bits.
4. **Access:** define unaligned reads/writes and whether an unaligned or partial
   field can have an ordinary typed pointer. Carry the actual alignment and
   storage semantics through GloinIR and its lowering.

There are legacy packed/endian parser and IR fragments, but checked programs
reject packed structs. Review these against the new design before reuse.
Do not treat their presence as implemented support or an approved spelling.

Acceptance should use independent expected byte sequences, round trips in both
byte orders, malformed/bounded input and explicit size/alignment checks on both
supported platforms. Packed storage does not establish a performance win;
measure representative parsing and serialization after correctness is fixed.

## Separate design tracks: threads and async

Neither is committed to 0.2.0 yet. Design them separately, with explicit
interaction rules if both are eventually supported.

| Track | Decisions required before implementation |
| --- | --- |
| Threads and shared memory | Launch/join ownership, argument and result transfer, stack/arena lifetimes, shared mutable aliases, synchronization, atomics and memory ordering, data-race rules, thread-local context and shutdown. |
| Async I/O and tasks | Scheduling and progress, suspension points, task and buffer lifetimes, readiness integration, blocking calls, deadlines, cancellation, failure propagation, defer execution and task cleanup. |

The existing [streaming HTTP client](http-client.md) already permits concurrent
exchanges driven through readiness polling. Language-level async can build on
that contract; it still needs explicit treatment of blocking operations such
as the current hostname resolver. Async does not by itself decide whether
tasks execute on multiple threads.

Manual memory management makes both designs consequential: an arena cannot be
freed while another thread or suspended task still uses it, and existing
alias-aware handles do not establish thread safety. Audit the runtime and
standard library before allowing values to cross a thread boundary. Document
scheduling nondeterminism and use controlled synchronization and independent
expectations in tests.

The four documented unsupported async/spawn tests remain visible until the
corresponding behavior works through the checked compiler, GloinIR, lowering
and runtime. A release without concurrency must continue to reject it clearly.

## Proposed order and release gate

1. Agree the roles, syntax and semantic rules for switch and match.
2. Implement and document their accepted scope through GloinIR.
3. Decide whether 0.2.0 also includes endian operations and a bounded packed
   layout milestone; do not make full async a prerequisite for this release.
4. Develop the thread and async designs independently before committing their
   implementation to a release.

For every accepted feature, require positive and negative compiler cases,
GloinIR verification, JIT/native behavior, formatter support, runnable examples
and thorough documentation. Preserve macOS and Linux acceptance, package
relocation and sanitizer checks, with accurate reporting of deferred features.
Publish a numbered 0.2.0 guide only once its feature boundary is settled; keep
the 0.1.0 guide and release validation record intact.
