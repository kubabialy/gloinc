# Gloin 0.4.0 roadmap proposal

**Decision requested, 2026-11-20.** Make concurrency the organizing feature of 0.4.0. A program should be able to run an independent computation on another OS thread and keep servicing connections while an I/O operation is pending. Give both operations checked, typed completion and visible resource costs. Keep a narrow hosted C interface as a second candidate. Approve its inclusion separately once a real binding passes. This document proposes scope and release gates. Public spellings, implementation dates and release numbering beyond 0.4.0 still need a maintainer decision.

## What was actually planned

I reviewed compiler candidate `0fe96ab`, the [0.3.0 integration record](release-0.3.0-draft.md), repository history, the [wiki implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist) and [language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec) at wiki revision `c20e258`. No GitHub milestones were listed at review time. I found a historical commitment to investigate and implement concurrency, but no recorded assignment of that work to **0.4.0**. The 0.3.0 draft explicitly calls C FFI a *possible* 0.4.0 direction. The wiki withdraws its early concurrency examples pending a type and lifetime contract.

| Historical item | Current evidence | Proposed disposition |
| --- | --- | --- |
| SPEC-040, concurrency contract | The checklist leaves handle types, invocation, captures, failure and join behavior open. It uses both deferred/run and spawn/await vocabulary. | First blocking design review. Adopt one vocabulary for executable examples and diagnostics. |
| SPEC-041, Spawn | Syntax-only and unchecked paths exist. The checked language rejects spawn. | Required. Demonstrate a typed call running on a separate OS thread. |
| SPEC-042, Deferred | The checklist requires genuine suspension and at least one asynchronous I/O operation. | Required. Demonstrate two independent tasks progressing on one executor thread. |
| SPEC-043, join and force_join | Both are historical design items. Their signatures and failure behavior were left open. | Required completion contract. Decide explicitly whether to expose force_join in v1. Preserve recoverable errors at ordinary joins. |
| SPEC-044, external packages | Local source packages already work. Version resolution, locking and offline/cache rules are separate historical requirements. | Carry forward full resolution. Specify only the local dependency/link information needed by the first C binding. See [modules](modules.md). |
| SPEC-034/036 and earlier numeric work | Results, payloadless enums, fixed arrays, slices, loop break/continue, endian integers and packed storage already have implementations. Enum payloads were explicitly excluded from 0.3.0. Range iteration and SPEC-013b 128-bit numerics remain separate design items. | Preserve existing behavior. Require a concrete use case before scheduling enum payloads, range syntax or 128-bit types. |
| C imports and explicit linking | The [0.3.0 draft](release-0.3.0-draft.md#possible-040-direction-c-ffi) proposes the direction. Runtime-private C bridges already have checked signatures. | Independent candidate with a small scalar/pointer ABI and local native package demonstration. |
| Bit operations, wrapping arithmetic, serial I/O, game bindings and firmware | These came from the hosted/embedded usability audit. Packed fields already cover static wire layouts. | Independent bit operations candidate. Serial telemetry and a desktop game are integration pilots. Freestanding compilation has a separate target and runtime program of work. |

The [test inventory](../tests/README.md) names four historical failures. Two [async fixtures](../tests/async_test.cpp) inspect textual IR after syntax-only parsing and unchecked generation. [Semantic fixtures](../tests/sema_async_test.cpp) also use the syntax-only parser. The [dialect operations](../src/dialect/GloinOps.td) and [code generator](../src/codegen.cpp) contain prototypes. Start from the [checked program](checked-program.md) and verify every step through LLVM export. A printed operation or an old unchecked test passing cannot establish working concurrency.

## Release boundary

**Proposed minimum.** One bounded worker-launch mechanism, one single-thread executor, one genuinely asynchronous socket-read operation, typed completion and explicit checks. Supply one realistic example of each, plus failure injection, cleanup, runnable documentation and installed-package tests. Build on the maintained Apple Silicon macOS and Linux ARM64/x86_64 configurations. Reconcile the branch with the actual released 0.3.x baseline before opening a candidate.

**Independent candidates.** Checked C imports and bit operations can proceed alongside the concurrency work once each contract is reviewed. Each must be independently releasable and documented. Serial telemetry is the first device-facing consumer. A desktop multimedia binding exercises a different library and ownership model. Promote a pilot to a release deliverable only when its platform support, maintenance owner and test equipment are identified.

**Scope cut.** If the schedule tightens, move game integration, serial distribution, broader C ABI forms and bit conveniences first. If only one concurrency model meets its gates, bring a written split decision to the maintainer. Shipping threads alone would fulfill SPEC-041 and leave SPEC-042 open. Record precisely which operations work in each execution mode in the numbered guide.

## Milestone 1 — freeze the concurrency contract (SPEC-040)

Deliver a short normative spec, cost table, runnable-style examples and paired rejection examples before stabilizing syntax. Describe three different events in every example: creation of the task, commencement of its work, and observation of completion. Document ordinary function calls and function-scoped `defer` alongside the new operations so the two meanings of “deferred” stay clear.

| Decision | Recommended starting point | Evidence required before acceptance |
| --- | --- | --- |
| Launch and scheduling | Explicit launch of a worker starts a separate OS thread subject to a caller-visible bound. An explicitly owned executor schedules cooperative tasks. Specify when each begins, who drives the executor and where suspension is legal. | A chronology showing launch failure, ready completion, pending I/O, wakeup and shutdown. |
| Ownership of a handle | One owner and one successful consumption. Reject copying, storing through unchecked aliases, use after join and scope exit with an outstanding obligation in the first version. Specify moves or restrict bindings until all exits can be checked. | Compile-fail examples for aliases, branch joins, reassignment, loop exits, early returns and duplicate joins. Make creation failure leave no live obligation. |
| Inputs and outputs | Begin with copied scalars and recursively verified, pointer-free value aggregates of supported layouts. Include multi-argument and non-i32 returns. Admit void explicitly. Define a supported method receiver policy. | Reject borrowed references, slices, arena owners, sockets and records hiding them. A read-only view can still alias writable memory. Exercise same-type packed and padded copies across a worker. |
| Errors and traps | Keep launch failure separate from completion. Specify the shapes for T, void and result<T>. A proposed join on a task returning result<T> preserves one explicitly handled result<T> and preserves the worker's error metadata. Specify how runtime completion errors are distinguished. | Show complete source for success, launch failure and worker failure. Decide whether force_join is a deliberately trapping convenience. Existing arithmetic and bounds traps retain their contract, including process termination in the initial design. |
| Visibility and shared memory | Publish copied inputs before the worker starts. Successful join synchronizes with worker completion before result bytes are read. Keep workers isolated initially. | State the happens-before edges. Reject overlapping writable storage, including packed slices that share a byte. Review runtime globals and thread-local context before approving callable APIs on workers. |
| Shutdown and cancellation | Prefer explicit shutdown, bounded queues and cooperative cancellation at documented suspension points. Define joining an already completed task, pending cancellation, executor reentry, and self-join/deadlock policy. | For each state, list owner, possible next states, cleanup obligations and visible outcome. State which operations may block and which have no bounded completion guarantee. |
| Lifetime across suspension | A suspended task owns its frame. Define which arguments and locals may survive a suspension, when a pending OS registration releases its borrow, and how defers run on normal completion and cancellation. | A socket-read example that can actually pass the proposed rules. Keep the socket and destination storage owned by the executor/operation if the compiler cannot prove an external borrow. Reject reset/free/close races or structurally prevent them. |

For the first worker example, consider a calculation returning `result<u32>`. At launch the caller must handle capacity or thread-creation failure before it can own a handle. After launch there is exactly one obligation to collect the outcome. At join the caller must explicitly check the result before reading its value. Demonstrate a worker-supplied error arriving with its original message, code and OS code. Give launch failure, worker failure and an unexpected runtime join failure separate examples even if their payloads use the same error type.

One practical first read API would let an executor-owned operation hold one socket registration and a fixed-capacity receive buffer. Completion delivers a byte count and access with an explicit lifetime. An alternative is an owned fixed-size value result. Review the size limit, copying cost, partial-read and EOF semantics of both before choosing. A reference to a stack buffer retained past its scope is an invalid design even when it happens to work in a test.

**Exit gate.** The maintainer can trace a two-task example through every normal and error exit and say who owns the stack/frame, buffer, result and native handle at each step. Record canonical spellings, the transfer-safe type set, whether user cancellation ships, and any rejected historical spellings. All subsequent feature branches depend on this decision.

## Milestone 2 — checked types, control flow and verified IR

1. Teach the normal parser and semantic checker the approved subset. Check callable signatures, thread eligibility, result obligations and task-handle consumption on each control-flow edge. Model early exits from loops and functions. Track handle identity separately from the task's result value.
2. Record the resolved operation, argument and return schema, executor association and ownership facts in CheckedProgram. Carry them into typed GloinIR operations. Verify malformed hand-built IR as well as invalid source.
3. Make launch, suspension, synchronization, cleanup and completion observable to optimizer analyses. Reassess bounds and alias proofs across suspension. Model memory effects at joins and foreign calls. Prevent an optimization from moving a checked trap across launch or publishing partly initialized aggregate storage.
4. Lower supported operations to checked runtime ABIs with typed thunks and correctly aligned argument/result storage. Audit every prototype that transports a value through a generic pointer. Reject unsupported signatures before lowering.

**Exit gate.** Negative fixtures use the normal entry point. Positive fixtures reach the verified lowering pipeline behind the development boundary. Tests cover argument order, signedness, required traps, aggregate representation and invalid handles. No public support claim until runnable milestones land. See [lowering](lowering.md), [results](results.md) and [packed storage](packed-storage.md).

## Milestone 3 — implement and prove OS threads (SPEC-041)

Deliver one explicit worker per accepted launch initially, with a documented hard capacity and a recoverable resource-exhaustion path. Publish per-worker ownership of the task record, argument copy, return slot and OS thread. Bound active and completed-but-unjoined records. Document stack reservation versus committed memory on each host. Give clock, allocator and process-context dependencies an explicit worker policy.

Tests must distinguish a worker from an ordinary function call. Use a controlled start/release handshake with a watchdog: the parent receives a “worker started” signal and can do independent work before releasing the worker. Observe distinct thread identities through a test hook. Wall-clock speedup is unnecessary evidence. Test multiple arguments, signed and wider scalar returns, void, a supported value aggregate, worker error propagation, full capacity, simulated thread creation failure, and successful reuse after join. Verify payload visibility after join and count live resources at shutdown.

**Exit gate.** The same source runs in JIT and native O0/O2, with no surviving workers or callable JIT code after its owning invocation shuts down. Any supported method call gets a typed receiver test. Unsupported receivers get a checked diagnostic.

## Milestone 4 — implement and prove asynchronous I/O (SPEC-042)

Use the existing [nonblocking socket operations](../stdlib/net.gloin) and [monotonic clock](../stdlib/time.gloin). The first executor owns bounded task frames, a ready queue and a bounded readiness registration table. A read that would block saves its continuation, yields to another task, and is retried after readiness. State how readiness, a deadline, socket close and cancellation compete for one completion. Use generation-checked registrations or an equally explicit stale-wakeup defense.

Specify fairness in units a test can observe, such as a bounded number of ready-task steps before polling again. Define queue-full behavior and how long-running CPU work can monopolize a cooperative executor. Keep the poll/wakeup bridge native where required and keep transport policy and framing in Gloin. Never call a blocking join on the executor thread while its target needs that executor to progress.

**Exit gate.** With one executor thread, task A blocks waiting for a byte and task B completes before a controlled peer releases that byte. Then A resumes once with the expected byte. Repeat for EOF, partial input, timeout, cancellation/close races, stale notifications, exhausted capacity and shutdown with pending work. Use injected clocks/readiness for boundary cases and real loopback sockets for integration. Check idle behavior for busy polling and verify all frames, registrations and buffers are reclaimed.

## Milestone 5 — completion, diagnostics and end-to-end use (SPEC-043)

Publish a completion table for each handle type: immediate success, task-declared error, runtime failure, cancellation and invalid reuse. Preserve error message, category and OS code through ordinary joins. Specify how launch and completion failures compose without hidden propagation. If force_join ships, add a subprocess test for its exact trap contract. Add compile diagnostics for illegal suspension context, unsafe capture, unhandled result, consumed handle and executor misuse.

Ship two small Gloin examples. One computes independent checksums on copied inputs on workers, then joins and verifies deterministic output. The other handles two clients where one sends a partial frame and stalls while the other receives a complete reply. Each example documents resource bounds, which thread owns each object and how shutdown completes. Keep performance evidence separate from progress/correctness tests.

**Exit gate.** Replace each of the four named legacy assertions with a mapped positive or negative test of approved semantics. Remove an expected-failure entry only alongside its replacement and passing evidence. Exercise join before and after completion, repeated lifecycle reuse and early exits. Cross-check the JIT, native and installed-package test selections.

## Independent milestones for interoperability and low-level operations

| Deliverable | Implementation boundary | Acceptance case |
| --- | --- | --- |
| Hosted C imports | Define fixed-width integers, floats, opaque handles, nullable pointers, explicit buffer lengths and ownership. Specify symbol naming, platform-sized types, library search, link ordering, JIT symbol resolution and synchronous blocking behavior. Keep slices, results and internal aggregate ABI behind Gloin wrappers. Use a small C shim for unsupported C types. | Build an independent fixture library and a local package using a clean installation. Open a handle, pass a buffer with canaries, handle a failing call and close it. Test missing symbols, ABI mappings against a C oracle, native/JIT resolution and relocation. Defer varargs, callbacks into Gloin, cross-thread foreign callbacks and by-value C aggregates until specified. |
| Bit operations and modular arithmetic | Specify AND/OR/XOR/complement at logical width, signed and unsigned right shift, shift counts at or beyond the width, and explicit wrapping add/subtract/multiply. Decide how operations on endian-qualified values compose with extraction and storage. Preserve ordinary checked operators. | Run vectors for u1, u13, u32 and u64 and signed boundaries at O0/O2/JIT. Cover zero, width-minus-one and out-of-range shifts. Move the [SplitMix64 transition](../src/random_runtime.cpp) into Gloin with identical published outputs. Implement incremental CRC-16/MODBUS and verify independent byte vectors under fragmented input. |
| Host serial telemetry pilot | Model line configuration, a single handle owner, partial progress, absolute deadlines, disconnect and close. Choose either a narrow private OS bridge or the proposed C package mechanism. Keep framing and CRC in Gloin. | Pseudoterminal CI delivers split and adjacent frames, timeout, EOF and reconnect cases. Record one physical USB-serial board round trip and reset/disconnect observation before claiming device validation. |
| Desktop game binding pilot | Use a local package and a small C shim for opaque window/render/audio handles and normalized input events. Use queued audio, explicit cleanup and caller-owned frame storage. | Build a small playable 2D game from a relocated installation. Record keyboard, resize, draw, sound and quit checks. Attribute the platform/ABI combinations actually exercised. |

The thread and deferred-I/O tracks have no dependency on public C imports. A multimedia package or serial binding should be buildable without patching compiler source. If that requires broad ABI support, reduce the shim interface first. General host UDP/IPv6 sockets, reusable binary cursors and rings remain useful follow-ups once two consumers establish the common contract.

## Integration order and branch plan

Use one branch per reviewable item against the agreed development base. The names below are suggested PR topics. Confirm the candidate base after the 0.3.x release record is settled. Merge only increments that build and pass the applicable gates.

| Order | PR topic | Depends on | Reviewable outcome |
| --- | --- | --- | --- |
| 1 | Concurrency specification and failure matrix | Scope review | Normative decisions, examples, costs and rejected cases. |
| 2 | Checked task lifetime and callable types | 1 | Parser/sema, CheckedProgram metadata, diagnostics and control-flow fixtures. |
| 3 | Typed task IR and worker launch/join | 2 | Verified operations and a minimal executable thread round trip. |
| 4 | Worker bounds and lifecycle | 3 | Cleanup, failure injection, thread handshake, JIT lifetime, platform tests. |
| 5 | Deferred frame and executor | 1–2 | A deterministic suspend/resume test and state ownership audit. |
| 6 | Socket readiness and deadlines | 5 | Real pending I/O, unrelated-task progress, cancellation/shutdown coverage. |
| 7 | Completion integration and examples | 4, 6 | Public join surface, error fidelity, failure-inventory reconciliation. |
| 8 | C ABI design, scalar imports, package link fixture | Own design gate | Separate PRs for native linking and JIT resolution, then relocated example. |
| 9 | Bit operations, wrapping operations, protocol fixtures | Own design gate | Separate verified increments with signed/width semantics and regression vectors. |
| 10 | Candidate validation and guide | Approved release scope complete | Reproducible platform results, numbered guide, migration notes and inventoried limitations. |

Before implementing threads, model at least one aliasing counterexample and one failed worker launch on paper. Before implementing the executor, model close versus readiness and a task suspended across arena reset. If either proof requires a general borrow checker, narrow the allowed values or give storage to an explicit owner. If the proposed restriction also excludes the reference application, reopen the design decision.

## Release evidence and cost budget

| Area | Required record |
| --- | --- |
| Compatibility | Run existing language and stdlib suites on the 0.3.x base and candidate. Inventory new reserved words, affected identifiers, ABI changes and migration examples. Confirm synchronous code retains its result, defer and overflow contracts. |
| Execution modes | JIT and native O0/O2 for functional concurrency and numerical checks. Cover macOS ARM64 and Linux ARM64/x86_64. Exercise installed and relocated compilers with an external package and examples on the supported combinations. |
| Failure and races | Run ASan/UBSan. Add a ThreadSanitizer lane for worker/runtime interaction where supported and record coverage of instrumented generated code separately. Use deterministic handshakes for correctness and bounded repeated runs for race discovery. Report watchdog failures and retain failing seeds. |
| Resource limits | Test zero, one, capacity and capacity-plus-one for queues, threads, frames and pending registrations. Record stack/frame bytes, peak live records, owned-buffer footprint, and allocation failures. Check cleanup after partial initialization, cancellation and JIT teardown. |
| Performance | Establish synchronous baselines. Measure worker launch/join overhead, ready-task scheduling cost, idle CPU and two-client I/O latency. Warm up, repeat, record machine/compiler/settings, distributions and raw data. Choose regression thresholds from baseline variability before evaluating a final candidate. |
| Distribution | Run check-core, reconcile the full suite with the documented inventory, verify package tests discover the new suites, and reproduce an example from the extracted archive. Record exact commands and remaining platform limits in the release record. |

## Decisions to record at scope review

1. Approve both worker threads and genuine asynchronous I/O as minimum release goals, or explicitly split their release assignments.
2. Select the launch, join and suspension spelling, transfer-safe types, receiver rules, error shape, trap policy and one-shot handle rules in SPEC-040.
3. Select bounded executor ownership, cancellation semantics, initial socket operation, and memory budget. Record what happens when user code never yields or a worker never returns.
4. Decide whether C imports and bit operations are required, independent candidates, or moved to a later minor release. Assign a reviewer and acceptance fixture to each included track.
5. Name a serial board and desktop integration environment only if those pilots become release gates. Keep cross-target firmware distinct: semantic analysis currently [requires 64-bit pointers](../src/sema.cpp), and [native output selects the host and adds a process entry](../src/native_output.cpp). A firmware milestone needs a target data layout, freestanding entry and link, startup code, MMIO semantics, interrupt discipline, and on-board verification.

After those decisions, update this table of deliverables with the accepted scope and PRs. Change the version and publish artifacts only when cutting an authorized release.
