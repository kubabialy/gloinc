# SPEC-040 · Tasks and completion

**0.4.0 development.** Free functions declared `def spawnable` can be called synchronously. The compiler checks worker signatures and the transitive call graph, including concrete generic helpers. This first subset supports call-local reference parameters in helpers. It rejects stored borrows, pointer arithmetic, deferred captures and unaudited native primitives on worker paths. Generic worker entry declarations, source-level launch and executor scheduling remain under development. [Roadmap](roadmap-0.4.0.md#milestone-1--freeze-the-concurrency-contract-spec-040)

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

## The spawnable guarantee

Check the declaration even when every caller is synchronous. `work(i)` runs
on the caller's thread and consumes no worker slot. Only `spawn work(i)`
starts a thread, and the target must be declared `spawnable`. Two invocations
can run concurrently without racing through their arguments or ambient state.
A worker may still trap, block its own progress or run indefinitely.

Check the instantiated call graph transitively, including recursive call
groups. A worker may mutate its locals and pass a local reference to a helper
whose checked effects keep that reference within the invocation. Reject any
path to mutable global state, escaping references or unaudited native code.
Report the offending chain, for example `work → sample → global counter`.

Each program invocation has a finite worker-slot limit, exposed to the caller
by `threads.worker_capacity()`. The proposed native/JIT default is four. An
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

## Launch and join

Import `@threads`. A worker with successful payload `T` has handle type
`threads.Thread<T>`. Launch returns `result<threads.Thread<T>>`. Success
creates one obligation to collect the thread. Failure creates none.

| Declared return of the worker | Handle | Result of `join()` |
| --- | --- | --- |
| `i32` or `result<i32>` | `threads.Thread<i32>` | `result<i32>` |
| `void` or `result<void>` | `threads.Thread<void>` | `result<void>` |

**One result at join.** A plain return becomes a successful result. For a
fallible body, successful collection delivers its result directly, preserving
the original error message, code and OS code. A runtime collection failure
uses the same failure arm. Error codes alone cannot establish the origin of a
failure. Launch failure remains separately observable at `spawn`. [Checked results](results.md)

Any join that returns to its caller has consumed the handle, including when the
worker returned an error. The runtime can report a recoverable collection error
only after establishing termination and safe reclamation. If it cannot establish
termination, it must retain the resources and JIT code while resolving it or
trap on an unrecoverable invariant. Arithmetic and bounds traps in workers
retain their process-level semantics.

### Four launches, then four joins

```gloin
import "@threads";

def spawnable work(i: i32) -> i32 {
    return i * i;
}

def main() -> i32 {
    def mut workers: [threads.Thread<i32>; 4] = zeroed;
    def mut count: i32 = 0;

    for def mut i: i32 = 0; i < 4; i = i + 1 {
        def started: result<threads.Thread<i32>> = spawn work(i);
        if started.erroneous { break; }
        workers[i] = started.value;
        count = count + 1;
    }

    def mut total: i32 = 0;
    def mut failed: bool = false;
    for def mut i: i32 = 0; i < count; i = i + 1 {
        def answer: result<i32> = workers[i].join();
        if answer.erroneous {
            failed = true;
            continue;
        }
        total = total + answer.value;
    }

    if count != 4 || failed { return 1; }
    if total != 14 { return 2; }
    return 0;
}
```

If the third launch fails, `count` is two. The second loop still collects slots
0 and 1. If slot 1 finishes before slot 0, it stays joinable until collected.
On a join error, `continue` advances to the next slot. There is one checked
result per launch and one per join.

For a fallible body, the launch and collection syntax stays the same.
Calling `divide(84, 0)` synchronously and collecting that call from a worker
both deliver `error("zero denominator", 37, 9)` when the worker is collected
successfully.

```gloin
def spawnable divide(n: u32, d: u32) -> result<u32> {
    if d == 0 { return error("zero denominator", 37, 9); }
    return n / d;
}

def calculate(n: u32, d: u32) -> result<u32> {
    def started: result<threads.Thread<u32>> = spawn divide(n, d);
    if started.erroneous { return started.error; }
    def job: threads.Thread<u32> = started.value;
    def answer: result<u32> = job.join();
    if answer.erroneous { return answer.error; }
    return answer.value;
}
```

## Verifying thread ownership

**Proposed v1 rule.** Track ownership specifically for thread handles. Each
successful launch creates one obligation. Its owner is initially the launch
result, then a named local or one element of a local fixed array. The spawning
thread is the sole collector. A worker's stack, captured arguments and return
slot stay live independently of the caller's stack. Finishing the body never
discharges the obligation. Ordinary Gloin values keep their existing copy rules.

| Operation | Required state | Effect |
| --- | --- | --- |
| Initialize a handle array with `zeroed` | Fresh local storage | Every slot is empty. No native worker or task record is created. |
| `workers[i] = started.value` | Successful unextracted result and proven empty slot | Transfer the obligation and consume the source. Initialization of a named local from `started.value` follows the same rule. |
| `workers[i].join()` | Slot proven live | Collect once. The slot becomes empty on every returning edge. The join result has its own handling obligation. |
| Leave scope | Every owner leaving scope is empty or consumed | Release local storage. There is no implicit join or detach. |

**A slot can own a worker, but reading it cannot make a copy.** Reject both
`def twin: threads.Thread<i32> = workers[0];` and `workers[1] = workers[0];`.
The only assignment that installs a handle in v1 consumes a fresh launch
success. An indexed join acts directly on its slot.

Reject whole-array copies, repeated initialization from a live handle,
address-taking, slices over handle storage, user aggregates containing
handles, passing or returning handles by value, and deferred captures.
Generic copies and raw placement or view constructors must enforce these
rules after substitution. The launch result is itself noncopyable. Every
successful arm must transfer its payload before the result goes out of scope.

### Static proof for two loops

1. Track each owner by binding or array position in the control-flow graph.
   A successful spawn introduces a fresh identity. Each transfer changes its
   owner once. Asynchronous worker completion leaves the ownership fact intact.
2. At the launch loop head, `count == i` and exactly `[0, count)` is live.
   The store at `i` and increment extend that range by one. A failed spawn
   breaks with the previous range intact. In the drain loop, exactly
   `[i, count)` is live at the head. Join consumes its first element before
   either result branch. Both paths reach the loop increment with identical
   ownership. At loop exit, the live range is empty.
3. Support constant slots and counted ranges first. Check both arms of
   conditionals, loop back edges, `break`, `continue` and every normal return.
   Invalidate index facts across mutation and possible alias writes. If a slot
   cannot be proved empty for a transfer or live for a join on every incoming
   path, reject the operation and identify the binding or range. This limits
   arbitrary runtime-index shuffling in v1. No lifetime annotations are needed.
4. Record transfers and consuming uses in CheckedProgram. Represent launch,
   transfer into a slot and consuming join explicitly in GloinIR. Verify the
   same ownership invariants across its control-flow edges, including the
   range proof for indexed operations. Reject duplicated or lost obligations
   and ordinary copies of handle storage before LLVM lowering. Preserve normal
   index bounds checks until proved redundant. The runtime controls thread
   termination, synchronization and resource reclamation.

### Ownership acceptance cases

| Case | Expected outcome |
| --- | --- |
| Use `started.value` twice, overwrite a live slot or join twice | Reject at the second use. Point to the first transfer or join. |
| Return on a later launch failure or the first join error with workers outstanding | Reject the edge which leaves the live prefix or suffix. |
| Substitute an arbitrary input index for the drain index | Reject when the live-slot proof is missing. |
| Join slot 0, launch again into slot 0, then join it | Accept. The second launch has a fresh identity. |
| Reach a join after only one branch has consumed that slot | Reject. Identify the path on which the slot was consumed. |
| Worker returns `error("bad input", 37, 9)` | One checked join delivers that exact message and both codes. Inject a runtime collection failure separately and check that it also consumes once. |
| Partial launches, arbitrary completion order and injected join failures | In JIT and native O0/O2, verify one reap per successful launch and full capacity reuse. |
| Hand-built IR duplicates a handle, fabricates an occupied slot or bypasses transfer with an aggregate copy | Reject before lowering. |

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
byte and releases its frame. Executor collection is also proposed to return
one result. The driver closes the socket and shuts down the
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

Check that a synchronous call reaches the same body without acquiring a worker
slot. Reject an unsafe helper chain at the spawnable declaration even in a
program with synchronous calls only. Record branch and loop ownership errors
through normal CLI diagnostics.
Inject clock, readiness, launch and join failures. Check resource counters at
capacity, immediately after failure and after shutdown. Execute installed and
relocated examples before advertising concurrency support. [Test inventory](../tests/README.md#remaining-failures)
