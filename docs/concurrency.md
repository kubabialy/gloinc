# SPEC-040 · Tasks and completion

**0.4.0 design proposal for maintainer review.** The examples specify future
checked Gloin. The candidate compiler rejects concurrency at its normal entry
point. This document proposes the contract for subsequent compiler and runtime
changes. [Roadmap](roadmap-0.4.0.md#milestone-1--freeze-the-concurrency-contract-spec-040)

## Who runs a call?

| Expression | Creation and first instruction | Observation |
| --- | --- | --- |
| `f(x)` | Evaluate arguments and call on the current thread. | Return to the caller. |
| `defer cleanup(x)` | Capture arguments now. Run cleanup in reverse registration order at normal function exit. | Cleanup finishes on the exiting thread. |
| `spawn f(x)` | Copy arguments once in source order, reserve a worker slot and start one OS thread. The worker can finish before launch returns. | The caller consumes its worker handle at join. |
| `schedule(exec, f(x))` | Copy arguments into an executor-owned frame and enqueue it. The body first runs when the owner drives the executor. | The driver consumes its task handle after driving the executor to completion. |
| `await net.read(connection, deadline)` | Inside a deferred body, attempt a nonblocking read. On would-block, retain the continuation and register for readiness. | Resume the same frame on the same executor thread with one checked outcome. |

Use `def spawnable` for worker entry points and `def deferred` for cooperative
entry points. A spawnable function may also be called synchronously. A deferred
entry requires explicit scheduling. A successful schedule returns a checked
`task.Deferred<U>` handle tied to that executor. Worker and deferred handles
are distinct even when their declared return type U matches. Both launches name a free function
with a resolved signature. Closures, implicit receivers, method launches,
nested launches and cross-executor joins are outside the proposed subset.
`schedule` is contextual in the scheduling expression and library names
remain qualified. Existing synchronous identifiers must be inventoried before
parser changes. The proposal omits `force_join` and reserves ordinary `defer`
for cleanup. Existing code already uses qualified names such as `fs.join`.
The proposed join spelling is a handle method and needs no reserved word.

## Worker boundary

Each program invocation has a finite worker-slot limit, exposed to the caller
by `task.worker_capacity()`. The proposed native/JIT default is four. An
embedding may set a smaller limit, including zero, before entry. Independent
JIT invocations have separate budgets. A completed worker keeps its slot until
collection. A full invocation fails launch with `status.WOULD_BLOCK`. Record
allocation and OS thread creation failures unwind every reservation before
returning their checked error. A failed launch never enters the body.

Transfer scalars, payloadless enums, fixed arrays and recursively transfer-safe
ordinary and packed structs by value. Void is a valid return. Copy same-type
aggregates with their full stored representation, including padding, endian
fields and spare packed bits. Completion may also carry a checked result of a
transfer-safe payload or void. An error carries its static message and both
integer codes. Publish captured bytes before worker entry. Publish completion
before join permits the caller to read its payload. A worker never holds a
pointer into its caller's argument or result storage.

| Worker code | Proposed eligibility |
| --- | --- |
| Arithmetic, checked conversions and value-only Gloin helpers | Check instantiated callees transitively, including all branches. Local scalar mutation is permitted. |
| Mutable globals, slices, strings as data, pointers/references, arenas, sockets and collections containing borrowed storage | Reject at transfer boundaries and reject ambient access in the call graph. A const view and a writable alias can address the same byte. |
| Local seeded RNG | Admit only after auditing the private primitive as operating solely on worker-local state. Passing an RNG by value transfers its state. |
| Host clock, process arguments, allocator overrides, I/O, FFI and unaudited runtime calls | Initially reject on workers. Host callback bindings and their data can be thread-local. Copying a descriptor would leave the callback lifetime unresolved. |

Two disjoint-looking packed bit slices can still update the same backing byte.
Passing either slice to a worker fails the transfer check, even through a field
of a copied struct. Taking a value snapshot into a packed *record* before launch
gives the worker independent storage. [Packed storage](packed-storage.md)

## Handle ownership and control flow

A successful launch result owns one obligation. After proving success,
`.value` transfers that obligation once into a single immutable local handle.
Extracting the handle a second time is an error. The failure arm owns nothing.
The compiler must track unextracted successful results as well as extracted
handles. V1 permits neither returning nor storing a handle, taking its address,
capturing it in a defer, assigning it to another binding, or hiding it in a
generic call. The owning thread collects it exactly once, including when the
body has already finished. Self-join is structurally impossible.

| Source edge | Accept | Reject |
| --- | --- | --- |
| Launch | Handle launch failure, then extract once. | Discard launch, return on success without collecting, or copy the launch result. |
| Alias | Use the original local for collection. | `def alias: task.Worker<u32> = job;`, `&job`, or assigning to an aggregate. |
| Conditional | Consume on both outgoing paths, or keep live on both. | Join on only one path then use the handle after the branches meet. |
| Loop | Consume a per-iteration launch before the back edge, break or continue. | Carry a new obligation past its binding's lifetime or rejoin on the next iteration. |
| Exit | Collect all live handles before returning, including on an application error. | An early return with another live handle. |
| Reuse | Create a fresh task after collection. | Join twice, reassign a live binding, drop an unhandled join result or read its unchecked payload. |

For example, after a successful launch with local handle `job`, these are
independent negative source fragments. Check each through the normal parser
and semantic checker once the feature is implemented.

```gloin
def other: task.Worker<u32> = job;          // Would create two owners.
if skip { return 0; }                      // This path loses the live worker.
if take { def r: result<u32> = job.join(); if r.erroneous { return 1; } }
def final: result<u32> = job.join();        // Already consumed on the take path.
```

## Collection and the worker's returned result

**Maintainer decision.** Joining a worker that returns a result requires two
explicit checks. First the caller checks whether the runtime collected the
worker. On success it binds and checks the worker's own result. Both layers
preserve their error metadata, including when their numeric codes coincide.
Launch is a separate, earlier check.

| Declared return | Worker handle | `job.join()` |
| --- | --- | --- |
| `u32` | `task.Worker<u32>` | `result<u32>` |
| `void` | `task.Worker<void>` | `result<void>` |
| `result<u32>` | `task.Worker<result<u32>>` | `result<result<u32>>` |
| `result<void>` | `task.Worker<result<void>>` | `result<result<void>>` |

The candidate currently rejects nested result types. Milestone 2 must support
these completion types through CheckedProgram and verified GloinIR. Each layer
has its own tag and handling obligation. Reject reading the inner payload after
checking only the outer tag, and reject dropping the inner result on an
outer-success path, including a fallible void return. An outer failure has no
inner payload. The draft applies the same rule to executor collection.
[Checked results](results.md)

```gloin
import "@task";
import "@strings";

// Proposed source. Both arguments are copied. The denominator controls the worker outcome.
def spawnable quotient(n: u32, d: u32) -> result<u32> {
    if d == 0 { return error("zero denominator", 37, 9); }
    return n / d;
}

def run(n: u32, d: u32) -> i32 {
    def launched: result<task.Worker<result<u32>>> = spawn quotient(n, d);
    if launched.erroneous { return 1; }
    def job: task.Worker<result<u32>> = launched.value;
    def collected: result<result<u32>> = job.join();
    if collected.erroneous { return 2; }

    def answer: result<u32> = collected.value;
    if answer.erroneous {
        if strings.equal(answer.error.message, "zero denominator")
            && answer.error.code == 37 && answer.error.os_error == 9 { return 3; }
        return 4;
    }
    if answer.value != 42 { return 5; }
    return 0;
}

def main() -> i32 { return run(84, 2); }
```

| Run of the program above | Expected outcome and owner |
| --- | --- |
| `run(84, 2)` | Exit 0. The worker runs on its own thread. Join observes 42, consumes the handle and releases the slot. |
| No slot or injected native launch failure | Exit 1. No handle or live worker exists. Preserve the OS code when thread creation fails. |
| `run(84, 0)` | Exit 3. Collection succeeds. The inner error message is `zero denominator`. The worker has been reaped. |
| Inject a completion transport failure after termination | Exit 2. The outer result fails. Join consumes on both variants. |
| A checked arithmetic or bounds trap in the worker | The process traps under the ordinary language contract. No recoverable completion or defer execution is promised. |

Returning a recoverable runtime error requires proof that execution ended
and storage can be released safely. If the host cannot establish termination,
it must retain the worker resources and code until termination is established,
or fail the process on an unrecoverable runtime invariant. Returning a
collectable error while freeing storage under a running worker is invalid.
An executor collection failure occupies the outer result. If a task returns an
error from an awaited operation, successful collection delivers that error in
the inner result. Cancellation that prevents a return is an outer failure.

## Executor frames and bounded I/O

The executor has one owner thread. At construction the caller supplies hard
limits for live frames, total aligned frame bytes, sockets and registrations.
Reserve space for a ready queue entry and terminal result for every admitted
frame. Exceeding capacity is a checked failure before publishing work. V1
allows one pending operation per task. Completed, uncollected frames still
count. An executor cannot be copied or entered recursively.

`schedule` takes a direct deferred call. Ordinary value arguments follow the
worker transfer rules. An executor-issued connection key is a special additional
argument whose executor identity and slot generation must match. The executor
opens or accepts and owns the native socket. A key grants operations on that
socket only on its owner thread. Existing freely copied `net.Socket` values
cannot be adopted safely. Reject a second simultaneous read on one socket with
`status.IN_PROGRESS` before consuming any data.

The first read returns `result<net.ReadChunk>`. This proposed value contains a
count and a fixed array of 256 initialized bytes. A positive count is one read,
possibly short. Zero means EOF. Bytes beyond the count are zero. The executor
retains the frame and buffer until the registration ends. The caller may keep
the returned value after collecting the task. A borrowed result could avoid a
copy of up to 256 payload bytes but would pin its slot and require a separate
release protocol. The fixed value bounds both retention and copying.

```gloin
// Proposed source. The connection key and deadline belong to this executor's domain.
def deferred first_byte(c: net.ConnectionKey, deadline: time.Deadline) -> result<u8> {
    def read: result<net.ReadChunk> = await net.read(c, deadline);
    if read.erroneous { return read.error; }
    if read.value.count == 0 { return error("EOF before first byte"); }
    return read.value.bytes[0];
}

def deferred marker() -> u32 { return 7; }
```

The driver schedules A (`first_byte`) and B (`marker`) into a two-slot executor.
Neither body runs during scheduling. The owner's `exec.drive_and_join(b)` drives
the shared ready queue. A tries one read, sees would-block, saves its continuation
and relinquishes the thread. B runs and yields 7 to the driver. The test peer
then sends one byte. `exec.drive_and_join(a)` polls, resumes A, collects the
byte and releases its frame. The driver closes the socket and shuts down the
empty executor. If scheduling A fails, close the socket and shut down. If B
fails to schedule, cancel and collect A first. If B's collection fails, A still
needs collection. If A's read fails, its collection releases the frame with the
error. No branch frees the JIT module, socket table or executor under live work.

Dispatch FIFO, at most one ready continuation per turn, then inspect I/O with
a zero timeout while any task remains runnable. Block in the poller only when
the ready queue is empty, bounded by the earliest outstanding deadline.
Readiness is permission to retry. Another would-block retains the original
deadline. A computation that never yields can occupy the executor indefinitely.

| State or event | Owner action and completion |
| --- | --- |
| Queued → running | Owner dispatches once. Ordinary task return records a terminal outcome. |
| Running → pending | Register the frame, socket generation and operation generation before polling. A registration-capacity failure is delivered at the await. |
| Pending → ready | Validate both generations before queuing one continuation. Ignore stale and duplicate notifications. |
| Competing events | Serialize on the owner thread. At a read attempt, process accepted cancellation, socket close and observed expiry, in that order, before retrying I/O. A published terminal outcome wins over all later events. |
| Cancel or close | Owner cancellation invalidates the pending operation and resumes cleanup with a cancellation outcome. Closing the socket invalidates its key and wakes pending reads with `status.CLOSED`. No asynchronous interruption of running code. |
| Cleanup | Run registered function defers once in reverse order on normal return, error return or cooperative cancellation. Defers cannot suspend. Retire the registration before releasing the frame or reusing the socket slot. |
| Collection and shutdown | Collection consumes a terminal outcome once. Shutdown requires all tasks collected and sockets closed. The owner can cancel pending tasks then drive and collect them. OS workers have no cancellation, detachment or bounded join time in v1. |

Only copied transfer-safe values and executor-owned operation state may be live
across suspension. A reference, raw pointer, slice, arena owner, captured defer
argument or transitive alias into caller storage live at an await is a checked
rejection. For example, an arena reset while A is pending cannot invalidate the
read buffer because the frame owns that buffer. A caller-provided arena slice
in the same example must fail checking.

## Cost and acceptance

| Operation | Resource and progress bound |
| --- | --- |
| Worker launch | Copy argument representation bytes, reserve one task record, aligned result storage, one native thread and its host stack. Report stack reservation and actual commitment separately for each host. |
| Worker join | May wait indefinitely for work. Copy out at most the result representation and reap the native thread once. |
| Schedule | Charge the compiler-known frame size including live locals, read storage, defers and result slot. One ready entry per frame. No native thread per task. |
| Pending read | One registration and at most one 256-byte payload per pending read, plus metadata. A completed frame retains its result until collection. |
| Driver turn | One continuation followed by readiness processing bounded by the socket/registration limits. User code between suspension points has no time bound. |

Future tests must use the checked parser, semantic analysis, CheckedProgram
and verified GloinIR. A typed launch records the concrete callee, argument
schema, result layout, ownership and synchronization effects. Suspension
invalidates proofs that depend on mutable aliases. Keep JIT code live through
the last join and reject malformed IR before LLVM export. [Checked program](checked-program.md) · [Lowering](lowering.md)

Replace the historical assertions as evidence becomes available.

| Existing assertion | Replacement acceptance case |
| --- | --- |
| `SemaAsyncTest.AsyncTypes` | Positive multi-argument worker and checked result, paired with unsafe captures and a duplicate consumption diagnostic. |
| `AsyncTest.SpawnGeneration` | Use a test-only handshake and distinct host thread IDs to prove worker entry before the parent releases it. Exercise full capacity and failed launch without leaks. |
| `CodeGenTest.GenerateSpawn` | Verify typed launch and join, padded and packed value round trips, void completion, and preserved traps in JIT and native O0/O2. |
| `AsyncTest.DeferredFunctionGeneration` | Run the two-task socket case above, then repeat with EOF, short read, would-block after readiness, expiry, cancellation, stale wakeup and close. |

Record all branch and loop ownership errors through normal CLI diagnostics.
Inject clock, readiness, launch and join failures. Check resource counters at
capacity, immediately after failure and after shutdown. Execute installed and
relocated examples before advertising concurrency support. [Test inventory](../tests/README.md#remaining-failures)
