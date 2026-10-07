# Child processes

The unreleased 0.1.0 tree can start and manage a native child through `@process`.
All fallible operations below return built-in `result<T>`. Invocation arguments,
environment lookup and cwd queries retain their existing APIs in the
[process-context guide](filesystem-process.md).

## Current setup and design rationale

This describes the **unreleased 0.1.0 implementation** on macOS and Linux.
The public guarantee is process-group cleanup. No process-tree sandbox or
strict containment mode is currently implemented or enabled by default.

| Launch choice | Standard streams | Signal and cleanup scope |
| --- | --- | --- |
| `start(...)` | Inherited | Direct child only; it inherits the host's process group. |
| `start_piped(...)` | Three pipes | Direct child only. Pipes do not enable group cleanup. |
| `start_with(..., Options.defaults())` | Inherited | Direct child only. |
| `start_with(...)` with `new_process_group = true` | Selected by `piped` | The new group and the direct child; ordinary descendants inherit the group. |

For a managed tool or test server, set **both** `piped = true` and
`new_process_group = true` when you need captured output and worker cleanup.
Keep the command in the foreground. These options are independent: a new group
also works with inherited streams. Merely using `communicate` does not create a
group. The compiler's own CLI does not automatically supervise every process
started by the program it compiles or runs.

### How launch and cleanup are wired

1. Gloin validates counted executable/argument bytes and explicit options,
   then allocates shared child metadata in the caller's arena.
2. Native `posix_spawn` applies child cwd, environment, descriptors and signal
   settings by default. A nonzero `stack_limit_bytes` selects a child-only
   `fork`/`setrlimit`/`execve` path. Either path can create a group whose ID is
   the child's PID. These changes do not alter parent settings.
3. The Gloin capture loop services stdin and both outputs, enforces buffer
   bounds, and checks the monotonic deadline.
4. For a grouped child, native `waitid(..., WNOWAIT)` observes exit while keeping
   its wait status owned. The leader's PID remains reserved until cleanup.
5. `close` closes pipe ends, signals the original group and the direct child,
   then consumes the leader's wait status and invalidates aliases. Group cleanup
   also happens after successful communication. Arena reset/free performs none
   of these steps automatically.

The orchestration and ownership state live in Gloin. Native host operations
pass through source-typed GloinIR and `gloin.abi_call` before LLVM lowering.
The group feature adds no language syntax and does not depend on async/spawn.

### Why this policy

Process groups provide a common mechanism for the macOS/Linux tooling we need:
compiler commands, foreground test servers and their ordinary workers. Launch
and signalling require the caller's ordinary OS permissions, without requiring
a separate privileged supervisor or Linux cgroup configuration. Group creation
is opt-in to preserve conventional inherited-group behavior for existing APIs;
Gloin never broadcasts cleanup to the parent's inherited group.

The boundary comes from group membership. A worker that stays in the group is
a cleanup target even if its parent exits. A descendant that creates another
group or session can leave that scope. This can happen through legitimate
daemon/background-service behavior; it does not require a malicious program.
Signal permissions and concurrent membership changes also apply.

Recursively listing descendants and signalling their PIDs would not provide an
atomic guarantee: processes can fork, exit or be reparented while the list is
being collected. Repeated scans leave races, and released PIDs can be reused.
The retained leader protects the group identifier we actually own; it does not
turn a changing process tree into an owned collection.

| Situation | Current behavior |
| --- | --- |
| Child starts a foreground worker in the same group | Group cleanup targets both. |
| Leader exits while a worker holds stdout/stderr open | Capture still has a deadline; group cleanup can still target the worker. |
| Descendant detaches into another group/session | It can survive cleanup of the original group. |
| `wait_timeout` expires | Returns `ready = false`; ownership and processes remain until explicitly handled. |
| `communicate` expires | Closes pipes, signals the configured cleanup scope and reaps the direct child; cleanup can outlast the budget. |
| Host prevents signalling some members | Host errors/permission rules apply; successful signalling is not proof that every member is gone. |

Stronger containment would need a separate platform-specific design and tests.
Linux cgroup v2 offers hierarchical membership and `cgroup.kill`, with access
and delegation requirements; macOS would need its own supported approach.
No such backend exists here today. If a strict mode is added, unsupported
configurations should fail explicitly instead of falling back silently to
process groups. See the [Linux cgroup documentation](https://docs.kernel.org/admin-guide/cgroup-v2.html)
and [session creation on macOS](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/setsid.2.html).

## Start and wait

```gloin
import "@arena";
import "@process";
import "@std";

def cleanup(child: &process.Child) -> void {
    def closed: result<void> = child.close();
    if closed.erroneous { std.println(closed.error.message); }
}

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();

    def arguments: [string; 2]         = {"Hello from Gloin", "$HOME stays literal"};
    def started: result<process.Child> = process.start(&owner, "/bin/echo", arguments[..]);

    if started.erroneous { return 1; }

    def mut child: process.Child = started.value;
    defer cleanup(&child);

    def finished: result<process.ExitStatus> = child.wait();
    if finished.erroneous { return 2; }

    def closed: result<void> = child.close();
    if closed.erroneous { return 3; }

    def exit_status: process.ExitStatus = finished.value;
    if !exit_status.success() { return 4; }

    return 0;
}
```

The child prints `Hello from Gloin $HOME stays literal`. There is no implicit
shell expansion, quoting, globbing or `PATH` search. `executable` is an explicit
absolute or relative path; relative paths resolve against the host cwd.
`arguments` excludes argument zero: Gloin sets `argv[0]` to the executable
spelling. Empty arguments, spaces, Unicode and non-UTF-8 bytes are preserved.
The executable must be nonempty; executable and argument strings cannot contain
NUL. A shell is available only by explicitly launching one with its arguments.

See [child_process.gloin](../examples/child_process.gloin) for the runnable
example with error reporting:

```sh
build/gloinc --jit examples/child_process.gloin
build/gloinc -O2 -o /tmp/gloin-child examples/child_process.gloin
/tmp/gloin-child
```

## Pipes and bounded capture

`start_piped` replaces all three standard streams with independent pipes. The
parent ends are nonblocking; the child sees ordinary blocking stdin, stdout
and stderr. Cwd and environment are still inherited. This program sends three
bytes to `/bin/cat`, captures its output, and prints `Hi`:

```gloin
// Binary stdin and bounded stdout/stderr capture with a five-second deadline.
import "@arena";
import "@process";
import "@std";
import "@strings";
import "@time";

def cleanup(child: &process.Child) -> void {
    def closed: result<void> = child.close();
    if closed.erroneous { std.println(closed.error.message); }
}

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();

    def arguments: [string; 0]         = {};
    def started: result<process.Child> = process.start_piped(&owner, "/bin/cat", arguments[..]);

    if started.erroneous { std.println(started.error.message); return 1; }

    def mut child: process.Child = started.value;
    defer cleanup(&child);

    // 72, 105, 10 are the counted bytes of "Hi\n". NUL bytes are supported too.
    def input: [u8; 3]         = {72, 105, 10};
    def mut output: [u8; 1024] = zeroed;
    def mut errors: [u8; 1024] = zeroed;
    def captured: result<process.Output> = child.communicate(input[..], output[..], errors[..],
        time.Duration { nanoseconds: 5000000000 });

    if captured.erroneous { std.println(captured.error.message); return 2; }

    // communicate closed the pipes, reaped the child and invalidated its aliases.
    // These views still borrow our buffers; they do not borrow the child handle.
    def outcome: process.Output = captured.value;

    std.print(strings.view_bytes(outcome.stdout));

    if !outcome.status.success() { return 3; }
    if outcome.stdin_written != input[..].len { return 4; }

    return 0;
}
```

Run [child_capture.gloin](../examples/child_capture.gloin) in either mode:

```sh
build/gloinc --jit examples/child_capture.gloin
build/gloinc -O2 -o /tmp/gloin-capture examples/child_capture.gloin
/tmp/gloin-capture
```

`communicate` feeds stdin while draining **both** outputs, closes stdin after
sending the supplied bytes, then waits for output EOF and the child's exit.
The capture loop performs no dynamic allocation: the caller supplies separate
output buffers. Group cleanup can require temporary native storage on macOS,
as described under costs below.
Input, stdout storage and stderr storage must not overlap, and all must remain
live during the call. Output views borrow those buffers after it returns.
Embedded NUL and invalid UTF-8 bytes are preserved. `strings.view_bytes` borrows
a counted string view without copying or validating UTF-8.

Each output buffer is a strict byte limit. Exact-length output succeeds; a
one-byte probe distinguishes EOF from overflow when the buffer fills. Empty
buffers accept only empty streams. Excess output returns an error; there is no
silent truncation. The caller's buffers may contain a prefix on failure, but
this API returns no partial-output lengths on its error branch. Use the manual
pipe API below when partial output must be retained and counted after a failure.

`communicate` captures bytes remaining from the time of the call; it cannot
recover output already consumed by manual reads. Its input count covers only
that call.

`Output` contains `status: ExitStatus`, `stdout: [const u8]`,
`stderr: [const u8]`, and `stdin_written: u64`. Each stream preserves its own
byte order; no relative order between stdout and stderr is promised. A child
may close stdin before consuming all input. This is an ordinary outcome:
`stdin_written` counts bytes accepted by the pipe, which does **not** prove the
child read them. Check that count if sending every byte is required.

Communication consumes the child handle. Success closes all pipes and reaps;
errors, output overflow and expiration close the pipes, kill an unfinished
direct child (and its original group when configured), and reap the direct
child. Every alias becomes closed after successful cleanup.
A cleanup error takes precedence over the original error; retain the handle and
retry `close` if it remains open. The arena does not perform cleanup for you.

### Deadlines

The `time.Duration` budget starts at entry to `communicate`; launch time is
excluded. The deadline covers stdin, both output streams and child completion,
including output handles retained by descendants. A zero budget expires
immediately and triggers cleanup. The pump checks a monotonic clock between
bounded operations. OS scheduling and millisecond wait rounding can overshoot
the budget; termination/reaping cleanup is outside it and can block. This is
not a hard real-time wall-clock guarantee. Clock failure or a backwards clock
reading is an error and also triggers cleanup.

`child.wait_timeout(duration)` observes a child without draining its pipes.
Expiration returns a successful `PollOutcome` with `ready = false`; it does
**not** kill or close the child. A zero duration polls once. Ordinary `wait()`
has no deadline. A child blocked writing to a full pipe cannot finish merely
because the parent called wait: use `communicate` or drain its outputs manually.

### Manual pipe operations

These methods require an open child created with `start_piped`. They do not
allocate or retain caller buffers. Copies share both child and pipe state.

| Method | Contract |
| --- | --- |
| `write_stdin(bytes: [const u8]) -> result<PipeOutcome>` | One nonblocking write; short writes are normal. Advance by `.count`. Empty input succeeds with count zero while stdin is open. A closed/broken stdin returns `status.END` and count zero. |
| `close_stdin() -> result<void>` | Close the parent write end, delivering EOF after queued bytes. Idempotent while the child handle is open. |
| `read_stdout(destination: [u8]) -> result<PipeOutcome>` | One nonblocking read into nonempty storage. EOF closes that endpoint; subsequent reads report END. |
| `read_stderr(destination: [u8]) -> result<PipeOutcome>` | Same contract for the independent stderr pipe. |
| `wait_io(write_input: bool, timeout_ms: i32) -> result<i32>` | Wait on live output endpoints and optionally stdin. Nonnegative milliseconds; zero polls. Returns readiness bits, or zero on timeout/interruption. |
| `wait_timeout(timeout: time.Duration) -> result<PollOutcome>` | Wait up to a duration for exit, leaving ownership intact on expiration. Does not drain output or close stdin. |

`PipeOutcome` has `state: i32` and `count: u64`. The ordinary states are
`status.OK` (progress), `status.WOULD_BLOCK` (retry later), and `status.END`
(output EOF or closed input). An interrupted read/write reports WOULD_BLOCK.
Other host failures are built-in errors. Readiness bits are
`process.STDIN_WRITABLE = 1`, `STDOUT_READABLE = 2`, and `STDERR_READABLE = 4`.
EOF/broken-pipe conditions are also reported as ready. Readiness is a hint;
a subsequent operation can still report WOULD_BLOCK. Disable stdin interest
when no input is pending, to avoid waking repeatedly on an idle writable pipe.

Pipe writes suppress their generated SIGPIPE without changing the host's
process-wide disposition. Parent descriptors are close-on-exec, and launch
closes unused pipe ends on both success and failure. Host applications must
coordinate unrelated fork/exec and descriptor mutation during setup (on macOS,
creating a pipe and setting its close-on-exec flags are separate operations).

The control loop, buffer limits, deadlines and ownership are implemented in
Gloin. Small native pipe/spawn/read/write/poll operations cross `gloin.abi_call`;
Gloin results, slices and control flow use the normal GloinIR lowering pipeline.
See the [POSIX pipe contract](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe.html)
for the host's byte-stream and EOF behavior.

## Launch options and process groups

`process.start_with(owner, executable, arguments, options)` accepts an
ordinary `process.Options` value. Begin with `process.Options.defaults()` and
set the fields you need:

| Field | Default | Meaning |
| --- | --- | --- |
| `cwd: string` | `""` | Inherit the parent cwd when empty; otherwise change only the child cwd before executing. A relative cwd resolves against the parent's cwd at launch. |
| `inherit_environment: bool` | `true` | Pass the host environment unchanged when true. |
| `environment: [const string]` | Empty | With inheritance disabled, supply the complete environment as `NAME=value` entries. An empty slice produces an empty environment. |
| `piped: bool` | `false` | False inherits stdin/stdout/stderr; true connects three independent pipes. |
| `new_process_group: bool` | `false` | Create a new group whose ID is the child's PID; termination and cleanup target that group and the direct child. |
| `stack_limit_bytes: u64` | `0` | Zero inherits the stack limit. Nonzero sets the child's soft `RLIMIT_STACK` before execution, preserving the inherited hard limit. See platform restrictions below. |

Cwd, arguments and environment entries are borrowed only during `start_with`.
The launch makes native copies; later mutations of the options do not affect
an existing child. The owner arena still holds the shared child metadata.
There is no implicit shell or `PATH` search. A relative executable path resolves
**inside the selected child cwd**, and `argv[0]` retains its supplied spelling.
The host's cwd and environment are not modified.

Environment inheritance and supplied entries are mutually exclusive. Each
supplied name must be nonempty, must not contain `=`, and must be unique;
names are case-sensitive. Split at the first `=`: empty values and additional
`=` characters in a value are valid. Missing separators, NUL bytes and duplicate
names are errors before launch. This API provides complete replacement, not
an overlay or automatic expansion. The executed program can subsequently
change its own environment; shells may add variables themselves.

[child_options.gloin](../examples/child_options.gloin) runs an explicitly selected
shell in `/`, with a supplied greeting, bounded capture and a 2 MiB soft stack limit:

```gloin
// Child cwd, environment, pipes, process-group cleanup and a 2 MiB soft stack limit.
import "@arena";
import "@process";
import "@std";
import "@strings";
import "@time";

def cleanup(child: &process.Child) -> void {
    def closed: result<void> = child.close();
    if closed.erroneous { std.println(closed.error.message); }
}

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();

    def environment: [string; 1]     = {"GLOIN_GREETING=Hello from a child"};
    def mut options: process.Options = process.Options.defaults();
    options.cwd                 = "/";
    options.inherit_environment = false;
    options.environment         = environment[..];
    options.piped               = true;
    options.new_process_group   = true;
    options.stack_limit_bytes   = 2097152;

    // A shell runs only because we explicitly launch it. No implicit expansion.
    def arguments: [string; 2]         = {"-c", "printf '%s\n' \"$GLOIN_GREETING\"; pwd -P"};
    def started: result<process.Child> = process.start_with(&owner, "/bin/sh", arguments[..], options);

    if started.erroneous { std.println(started.error.message); return 1; }

    def mut child: process.Child = started.value;
    defer cleanup(&child);

    def input: [u8; 0]         = {};
    def mut output: [u8; 1024] = zeroed;
    def mut errors: [u8; 1024] = zeroed;
    def captured: result<process.Output> = child.communicate(input[..], output[..], errors[..],
        time.Duration { nanoseconds: 5000000000 });

    if captured.erroneous { std.println(captured.error.message); return 2; }

    def outcome: process.Output = captured.value;

    std.print(strings.view_bytes(outcome.stdout));

    if !outcome.status.success() { return 3; }

    return 0;
}
```

It prints `Hello from a child`, then `/`, each on its own line. Run it with
`build/gloinc --jit examples/child_options.gloin`, or compile it with `-O2 -o`.

### Group ownership and cleanup

Use `new_process_group = true` for tool commands that may launch descendants.
Ordinary descendants inherit the group. `terminate()` requests SIGTERM and
`kill()` requests SIGKILL for the original group and the direct child, including
when the leader has already exited. The direct child is also signalled if it
has changed its own group. `close()` requests SIGKILL for the configured scope,
reaps the direct child and invalidates every alias. This group cleanup also
runs after successful `communicate`, so background workers do not intentionally
outlive the command handle.

For a grouped child, `poll`, `wait` and `wait_timeout` **observe and cache** the
leader's exit without consuming its native wait status. The exited leader may
therefore appear as a zombie until `close`. Retaining it reserves its PID while
that number is used as the cleanup group ID. `close` signals the group before
reaping the leader; it never signals that ID after releasing ownership. Close
promptly, even after a successful wait. All cached observations still describe
the direct child's original exit, not a combined status for its descendants.

For a child without a new group, terminal observation still reaps immediately,
and signals/cleanup affect only that direct child. `start` and `start_piped`
retain this behavior. Every launch starts with inherited process-group behavior
unless the caller explicitly enables the option.

A process group is a cleanup scope, **not process-tree containment**. Descendants
can leave it through their own process/session operations. Signals obey host
permissions and successful group signalling does not prove every member has
already stopped. POSIX may report success after signalling only some permitted
members. Processes joining after a signal, descendants that escape, or ones the
host does not permit signalling can survive. Gloin reaps only its direct child;
other parents/the host are responsible for descendant wait status. No Windows
job object or Linux subreaper behavior is implied.

Timeouts still cover communication, with cleanup outside the time budget.
`wait_timeout` expiration by itself leaves the child and group owned. For a
graceful stop, explicitly call `terminate`, inspect `wait_timeout`, then call
`close` to force cleanup of any remaining group members. The final `close` is
needed even when the leader exits within the grace period.

Embedders must not reap the leader externally or operate on handle aliases
concurrently. Before each group signal, the native boundary checks that the
leader is still waitable as an owned child. If ownership was lost, the operation
returns an error and invalidates the handle without signalling a stale ID.
Cached observations alone do not perform that check. External reaping can make
remaining descendants unreachable through this handle; it violates ownership.

The implementation uses POSIX spawn's new-group attribute and non-consuming
exit observation. On macOS, a group containing only zombies can produce EPERM
from the group signal path. The runtime accepts this as completed cleanup only
after a complete process-group snapshot finds no live members; snapshot errors
and genuine permission errors remain failures. See Apple's
[group-signal implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sig.c)
and the [POSIX spawn contract](https://pubs.opengroup.org/onlinepubs/007904975/functions/posix_spawn.html).

## API and outcomes

| API | Contract |
| --- | --- |
| `process.start(owner: &arena.GeneralArena, executable: string, arguments: [const string]) -> result<process.Child>` | Start an executable and retain shared child metadata in `owner`. No wait for program completion. |
| `process.start_piped(owner: &arena.GeneralArena, executable: string, arguments: [const string]) -> result<process.Child>` | Start with all three standard streams connected to pipes. |
| `child.communicate(input: [const u8], output: [u8], errors: [u8], timeout: time.Duration) -> result<process.Output>` | Feed input and capture both outputs within explicit bounds, then clean up the child. Requires a piped child. |
| `process.start_with(owner, executable, arguments, options: process.Options) -> result<process.Child>` | Launch with explicit cwd, environment, stream and group configuration; the first three parameters have the same types as `start`. |
| `child.is_open() -> bool` | Whether the handle is still usable. An exited/reaped child stays open until `close`. |
| `child.poll() -> result<process.PollOutcome>` | Nonblocking observation. `ready = false` means running; use `.status` only when ready. Caches terminal status. Ungrouped children are reaped; grouped leaders remain waitable until close. |
| `child.wait() -> result<process.ExitStatus>` | Wait without a deadline, or return cached status. A grouped leader remains waitable until close. Interrupted native waits are retried. |
| `child.terminate() -> result<void>` | Send SIGTERM to an unfinished child. It may catch or ignore this request. Does not wait for termination. For an ungrouped terminal child, succeeds without signalling. Grouped handles still signal their group. |
| `child.kill() -> result<void>` | Send SIGKILL to an unfinished child. Does not wait/reap. For an ungrouped terminal child, succeeds without signalling. Grouped handles still signal their group. |
| `child.close() -> result<void>` | Close pipes, signal the configured scope, reap the direct child, then invalidate all aliases. Repeated close succeeds. May block while the OS completes termination. |

`ExitStatus` has `exited: bool`, `code: i32`, and `signal: i32`:

- Normal exit: `exited = true`, `code` is the host exit code (0–255), `signal = 0`.
- Signal termination: `exited = false`, `code = 0`, `signal` is the host signal number.
- `success()` is true only for a normal exit with code zero.

An exit code of 37 or termination by a signal is a successful observation,
not a failed `result`. Starting or waiting can independently fail. A pending
`PollOutcome` has an all-zero status with `exited = false`; that status does not
describe an exit. Poll/wait after close return an error. Repeated poll/wait
before close return the same cached terminal status through every alias.

## Child stack limits

Set `options.stack_limit_bytes = 2097152;` for a 2 MiB soft stack limit.
Zero inherits both current limits. A nonzero request must be below
`9223372036854775807` and no greater than the inherited hard limit; the OS
can reject further constraints. Unsupported settings return a built-in error.
There is no unlimited sentinel and no silent fallback to inherited limits.
The parent soft/hard limits are unchanged, including on failed launch.

This controls the initial program's stack resource limit, including the compiler
when launching `gloinc --jit`. It does not bound heap usage, arena storage or
every future thread stack, and it is not a containment mechanism. The hard limit
is preserved: a cooperative program can raise its soft limit again up to that
ceiling. Extremely small limits may allow launch but make the loader or program
fail; successful launch does not promise usable stack space. See the
[host resource-limit semantics](https://sourceware.org/glibc/manual/latest/html_node/Limits-on-Resources.html).

### Platform and embedding contract

- **macOS:** a nonzero request must be launched from the host's original main
  thread. Gloin's ordinary CLI/native main follows this rule. An embedder's
  worker-thread call returns an error before creating a child. Darwin can reject
  stack shrinking when the caller's stack pointer is outside the main stack;
  forking a worker preserves that worker stack. We enforce one predictable rule
  for all nonzero requests instead of changing the parent's process-wide limits.
  The restriction comes from [Darwin's `RLIMIT_STACK` implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_resource.c).
- **Linux:** main-thread and worker-thread calls are supported. This path
  requires `close_range`, available on the supported Ubuntu 24.04 host. If
  descriptor closure is unavailable or denied, launch fails.
- Embedders must coordinate changes to inherited cwd, environment, descriptors,
  resource limits and child-reaping policy. Registered host `pthread_atfork`
  handlers must themselves be valid for the launch context. Neither the runtime
  nor Gloin can make arbitrary host callbacks safe.

### Why there is a second launch path

The default `posix_spawn` API has no common macOS/Linux child resource-limit
setting. For a nonzero request, native code prepares argument/environment copies,
signal structures and a close-on-exec error pipe before forking. The child uses
fixed storage and host calls: reset signals, configure group/cwd/streams, close
other descriptors, set only its own soft limit, then execute. No Gloin code,
C++ allocation, logging, stdio or runtime locks run in this child setup window.
This preserves the [fork restriction on child-side operations](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fork.html);
`setrlimit` is an async-signal-safe glibc operation and a direct Darwin syscall wrapper.

On macOS, descriptor preparation allocates a native snapshot before forking.
Closure scans up to the greater of the current soft descriptor limit and the
highest open descriptor plus one; it includes descriptors left above a lowered
soft limit. A very large descriptor limit increases launch cost. Linux closes
ranges directly. Both paths preserve standard streams as configured and reset
child signal dispositions/masks without modifying the parent.

Child setup or `execve` failure is sent through the error pipe: launch returns
an error after closing all pipes and reaping that child. A program that executes
and returns 127 remains an ordinary `ExitStatus`. A child independently killed
during setup can instead be observed through its signal exit status. The launch
and error handshake are blocking and have no deadline; `communicate` starts
its budget only after launch has returned.

The public field still lowers through source-typed GloinIR and the verified
`gloin.abi_call` boundary. Native probes check the actual soft/hard limits,
unchanged parent state, descriptor/signal behavior and platform thread rules;
Gloin fixtures exercise JIT and native `-O0`/`-O2` execution.

## Ownership, inheritance and costs

The owner arena retains one metadata allocation on a valid start attempt,
including a native launch failure. Argument preparation makes temporary native
copies proportional to argument/environment counts and their bytes plus cwd;
these are freed before `start` returns. The child uses the OS's independent argument storage. Caller
argument bytes and their slice need not remain live after `start` returns.
The OS applies its executable argument/environment limits.

Copies of `Child` alias the same state. Native wait status is consumed once: by
a terminal observation for an ungrouped child, or by `close` for a grouped child.
`close` invalidates every alias. Complete cleanup before resetting or freeing `owner`. Arena cleanup does not terminate or reap a child.
Register a cleanup function with `defer` after starting it; a result-returning
`close` call itself cannot be discarded in a defer. The helper must inspect its
result. On the normal path, check explicit close errors before returning success.

On a signal/wait failure, close retains ownership for retry when possible.
If the host has already reaped the child, the next native observation or group
signal detects lost ownership and invalidates the handle. Cached observations
do not recheck the host. Embedders must not reap Gloin-owned children or
concurrently operate on aliases. Hosts using ignored SIGCHLD or `SA_NOCLDWAIT`
are rejected before launch because they cannot provide this ownership contract.

Children inherit cwd and environment unless changed through `start_with`.
`start` inherits open standard descriptors 0, 1 and 2; `start_piped` replaces
them with pipes, and `start_with` selects either behavior through `piped`.
Other host descriptors are closed in the child, including compiler/runtime
files. Child signal masks are cleared and dispositions reset to defaults.
The parent is unchanged. Embedders must coordinate concurrent changes to cwd,
environment, standard descriptors, resource limits and child-reaping policy during launch.

Poll, cached observations and signalling use constant-size metadata and no Gloin
allocation. Supplied environment validation compares names pairwise, with
quadratic comparisons in entry count. Native launch validation repeats those
checks for C callers. On macOS, resolving the zombie-only group signalling case
allocates a temporary process snapshot proportional to group membership;
allocation failure is recoverable and retains ownership for cleanup retry.
Starting, waiting and closing involve host operations; none promises
a fixed execution time. Native operations pass through the source-typed
GloinIR boundary and `gloin.abi_call` before LLVM lowering. The host launch
primitive for the default inherited stack limit is [POSIX spawn](https://www.man7.org/linux/man-pages/man3/posix_spawn.3p.html).
As specified by that interface, some child setup failures can appear as exit
127 after a successful launch; an ordinary program can also return 127.

## Remaining limits

Explicit search-path lookup, resource limits other than stack, arbitrary file redirection
and per-stream inherit/discard selection remain pending. Choose either three
inherited streams or three pipes. Environment overlays/enumeration are not yet
provided. Process groups cover ordinary descendants; escaped descendants and
full process-tree containment remain outside this API's contract. The remaining
tooling work is tracked in the [0.1.0 roadmap](tooling-roadmap.md).

Errors currently contain static messages, consistent with the built-in `error`
contract. The native ABI records OS error codes, but these public result APIs do
not yet expose them. Signal numbers are host values. This API manages OS
processes and does not enable Gloin's deferred async/spawn language features.
