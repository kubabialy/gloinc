# Streams and files (SPEC-030d)

`import "@io";` loads [io.gloin](../stdlib/io.gloin). Ownership, allocation,
progress and convenience methods are ordinary Gloin code, using typed native
operations through GloinIR. The 0.2.0 compiler uses built-in
`result<T>` for every fallible I/O call. The numbered 0.1.0 guide retains the
released signatures.

## Read a line and write it

```gloin
import "@io";
import "@arena";

def main() -> i32 {
    def mut scratch: arena.GeneralArena = arena.GeneralArena.create();
    defer scratch.free();
    def mut input: io.Stream = io.stdin();
    def mut output: io.Stream = io.stdout();
    def mut line: io.ReadState = io.ReadState.empty();
    def read: result<void> = input.read_line(&scratch, 128, &line);
    if read.erroneous { return 1; }
    if line.end { return 0; }
    def mut progress: io.WriteState = io.WriteState.empty();
    def written: result<void> = output.write_line(line.value, &progress);
    if written.erroneous { return 2; }
    def flushed: result<void> = output.flush();
    if flushed.erroneous { return 3; }
    return 0;
}
```

With `hello` on stdin, this prints `hello` and returns zero. Empty EOF is normal
completion. Results do not print, throw or choose an exit status automatically.
`std.print` and `std.println` keep their existing output failure policy. Input
shares the native line reader and standard-stream buffering with `std.input`,
which now returns `result<std.InputLine>`; see [bounded stdin input](standard-library.md).

## Results and explicit transfer state

| API | Development return | Additional output |
| --- | --- | --- |
| Four `File` open methods | `result<io.File>` | File payload only after success |
| `File.close`, `File.flush`, `Stream.flush` | `result<void>` | No success payload |
| `read_line`, `read_chunk`, `read_all` | `result<void>` | Final argument `&io.ReadState` |
| `read_into` | `result<void>` | Final argument `&io.ByteReadState` |
| `write`, `write_all`, `write_line`, `write_bytes`, `write_all_bytes` | `result<void>` | Final argument `&io.WriteState` |
| `io.error_message` | `result<string>` | Diagnostic text only after success |

The six former wrappers (`IoResult`, `FileResult`, `ReadResult`, `ByteReadResult`,
`WriteResult`, `MessageResult`) are removed. Bind each result and check
`.erroneous` before an early return. A failed open has no readable dummy File;
a failed diagnostic lookup has no readable text payload. To forward failure,
return the checked `.error` explicitly.

Transfer state is caller-owned, mutable storage, initialized with the type's
`empty()` method. It carries no error category or errno:

| State | Fields | Meaning |
| --- | --- | --- |
| `ReadState` | `value: string`, `end: bool` | Current text or binary prefix; an empty EOF for line/chunk reads |
| `ByteReadState` | `read: u64`, `end: bool` | Bytes placed in the destination; an empty EOF |
| `WriteState` | `written: u64` | Bytes accepted during this call |

Every operation resets its state **before validation**, including closed handles,
wrong direction, invalid bounds and allocation failures. These failures leave
empty/zero progress and `end = false`. State is per call, not cumulative; save a
count or borrowed view before reusing it. `write_line` sums its text and LF stages.
No state pointer is retained. State must not overlap a handle, its shared metadata,
input bytes, output buffer or allocator state; arena reset must not invalidate it.

A transfer can fail after consuming or accepting bytes. Those bytes remain in
state, independently of the failed result. This is why progress is not stored in
`error` or in an inaccessible success payload. The compiler requires handling
the result; it does **not** prohibit reading progress before that check. Progress
alone never proves success. Check the result first, then apply the documented
recovery rules for that operation. Errors never roll back transferred bytes.

Errors contain a static message, an `@status` category in `.error.code`, and
native errno in `.error.os_error`. Validation/arena failures have zero native
code. Native failures preserve errno, with EIO as a fallback when absent.
ENOENT/ENOTDIR map to `NOT_FOUND`, EACCES/EPERM to `PERMISSION_DENIED`, EEXIST to
`ALREADY_EXISTS`, ENOMEM to `NO_MEMORY`; other native failures map to `IO_ERROR`.
Messages explain an operation and are not stable machine-readable keys. Native
errno values vary by OS. No partial byte count is added to the language's `error`.

## Ownership and lifecycle

`io.Stream` is borrowed and has no public close. `stdin()`, `stdout()` and
`stderr()` produce handles in O(1), with no Gloin allocation. Copies share native
position and buffering. Standard streams have process lifetime: stdin is readable,
stdout/stderr writable. Successful buffered output may still fail when flushed.

`io.File` owns a native stream with shared arena-owned metadata. Open takes
`(owner: &arena.GeneralArena, path: string) -> result<File>`. Copies of File and
Streams from `file.stream()` alias the same position and close state. Closing
any File alias clears that state **before** native close, even if close fails.
Later operations fail with `CLOSED`; repeated close has native code zero.
Never retry native close after failure.

**Close before resetting/freeing the metadata arena.** Reset/free does not close
files; all aliases become invalid memory afterwards. There is no reference count,
automatic destructor or borrow checker. Use a separate scratch arena for repeated
read/reset. Owned files are unbuffered: native FILE storage remains until close,
with no retained byte buffer. `flush` is not `fsync` and promises no disk durability.

I/O and close require mutable receivers. `stream()`/`is_open()` accept read-only
receivers; `is_open()` observes library state, not native descriptor validity.
Aliases require external coordination across threads. A bare `defer file.close()`
is rejected because it discards a result. Use a deferred helper that checks
`is_open()`, binds close's result and handles failure; also close explicitly before
reporting success. See [io_copy.gloin](../examples/io_copy.gloin).

| Method | Behavior |
| --- | --- |
| `File.open_read(&owner, path)` | Existing file, read-only; missing path fails |
| `File.create_new(&owner, path)` | Exclusive create; existing destination is preserved |
| `File.open_truncate(&owner, path)` | Explicitly truncate or create a writable file |
| `File.open_append(&owner, path)` | Open/create writable append; writes target the current end |
| `file.stream()` | Borrow shared metadata and cursor; cannot close it |
| `file.close()` | Consume native resource once; no arena allocation/free |

Paths are counted bytes relative to process cwd. Empty/NUL paths fail `INVALID`
before allocation or filesystem effects. Metadata allocation happens **before**
native open, so its failure cannot create/truncate a file. One FileState request
(one pointer, 8 bytes on arm64, plus arena overhead) remains even if native open
fails. Native preparation uses a temporary path-length+1 terminated copy, freed
before return, and native FILE storage until close. Open costs O(path bytes) plus
potentially blocking OS work. Host path resolution, symlinks, permissions and
append semantics apply; exclusive create has no racy existence precheck.

## Arena-backed reads

The same methods exist on File and Stream. Their full parameter list is
`(memory: &arena.GeneralArena, max_bytes: u64, progress: &io.ReadState)`.
All return `result<void>`. The maximum counts content bytes, excluding a terminator.

| Method | Success and EOF | Failure effects |
| --- | --- | --- |
| `read_line` | Strip LF/CRLF; standalone CR is data. Empty line is `value = "", end = false`. Empty EOF is `end = true`. Final unterminated line succeeds with `end = false`. | Oversized line drains through LF/EOF then fails `TOO_LONG`; every failure exposes empty text, even after consuming input. |
| `read_chunk` | Nonzero bound; fill until bound/EOF/error. Final partial chunk succeeds with `end = false`; subsequent empty EOF succeeds with `end = true`. | Retain received prefix on I/O error. Zero bound fails `INVALID` without consuming input. |
| `read_all` | Bounded whole remainder, including an empty file. EOF completes the operation normally; `end` stays false. | `TOO_LONG` retains the bound-sized prefix and leaves the next byte unread. I/O errors retain received prefix. |

Binary bytes, including NUL, are preserved. Every valid allocating read requests
`max_bytes + 1` zeroed arena bytes, retained until reset/free even on EOF/failure.
Returned text borrows that arena independently of file close or later reads.
Resetting scratch invalidates saved views; resetting the ReadState does not free
bytes. Bounds above 9,223,372,036,854,775,806 fail `NO_MEMORY` before allocation.
Allocation failure consumes nothing. Zero line/all bounds allow empty-line/file
checks: a zero-bound `read_all` on nonempty input leaves its first byte unread.

Chunk/all work is O(max_bytes), including zeroing, plus blocking native I/O.
Line work is O(max_bytes + consumed bytes); draining an oversized line can read
arbitrarily far despite bounded storage. `read_all`'s one-byte excess probe can
block even after exactly max_bytes bytes. Chunk reads may block while filling the
bound. These are blocking file APIs, not network readiness operations.
Native EOF/error indicators are cleared for the next attempt; retries are explicit.
Already consumed bytes are never restored. There is no automatic EINTR retry
that conceals partial failure. Closed precedes wrong direction, then bound and
allocation checks; preflight failures do not touch scratch storage.

## Direct byte I/O

`read_into(destination: [u8], progress: &io.ByteReadState) -> result<void>`
reads into a nonempty mutable slice without allocation, terminator or retained
view. Only `destination[..progress.read]` was received; the rest is untouched.
A final short read succeeds with `end = false`, then empty EOF succeeds with
`end = true`. I/O errors retain count and bytes. Invalid/closed calls reset the
state without changing the destination. Like chunk reads, it may block while
filling the requested size.

`write_bytes(bytes: [const u8], progress: &io.WriteState)` makes one native write;
`write_all_bytes(...)` retries short successful writes. Both return `result<void>`
and accept empty input on a live writable stream. They allocate nothing, retain
no slice and do not flush. Input/destination storage must remain live for the
call. Retrying the entire input after partial failure can duplicate accepted bytes.
The [binary filter](../examples/strip_nuls.gloin) combines fixed arrays, a growing
vector, direct reads and explicit progress.

## Writes and flush

String writes take `(text: string, progress: &io.WriteState) -> result<void>`
on either File or Stream:

| Method | Contract |
| --- | --- |
| `write` | One native fwrite; inspect `written` even on success, which can be short |
| `write_all` | Retry short successful writes; success accepts all input bytes; stop on error/no progress |
| `write_line` | Write all text, then LF; failed text suppresses LF; count includes both stages even if LF fails |
| `flush()` | `result<void>`; explicit buffered delivery, no close or disk-durability promise |

Work is O(bytes accepted), or O(text bytes+1) for a complete line, plus blocking
I/O. No Gloin allocation or input view is retained. Host standard buffering may
allocate internally. Flush costs O(pending bytes) plus native I/O. Line writes
are two operations and are not atomic, including in append mode. No-progress
native writes fail `IO_ERROR`/EIO instead of looping forever.

Empty writes succeed with zero progress on live writable streams. Closed handles
fail `CLOSED`; writing/flushing readable streams fails `INVALID`. Native error
indicators clear per attempt. The runtime temporarily blocks SIGPIPE on the
calling thread, consumes a newly pending EPIPE-related signal, restores the prior
mask and preserves an already-pending signal. Broken pipes therefore fail
recoverably without changing global signal disposition or `std.print` policy.

## OS error messages

`io.error_message(memory: &arena.GeneralArena, os_error: i32) -> result<string>`
formats a native code separately. Zero returns static `"no OS error"` without
allocation; negative codes fail `INVALID`. Other codes reserve 256 arena bytes
and query the localized host message. Allocation/lookup failures use `NO_MEMORY`,
`TOO_LONG` or `INVALID`, with no success text. Lookup errors have native code zero;
they do not reuse the input errno as their own diagnostic. Successful bytes expire
on arena reset/free. Work/storage is O(256) plus host lookup. Unknown-code wording
and lookup success are host-dependent.

```gloin
import "@io";
import "@arena";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def diagnostic: result<string> = io.error_message(&memory, 0);
    if diagnostic.erroneous { return 1; }
    def mut output: io.Stream = io.stdout();
    def mut progress: io.WriteState = io.WriteState.empty();
    def written: result<void> = output.write_line(diagnostic.value, &progress);
    if written.erroneous { return 2; }
    def flushed: result<void> = output.flush();
    if flushed.erroneous { return 3; }
    return 0;
}
```

This prints `no OS error`. After proving an actual failure, pass its
`.error.os_error` and send the formatted text to stderr if appropriate.

## Complete examples and checks

[io_copy.gloin](../examples/io_copy.gloin) reads source/destination paths from
stdin, exclusively creates output, copies bounded binary chunks, and checks
flush/close before reporting a count. Failure can leave a partial new file.
[io_filter.gloin](../examples/io_filter.gloin) reads bounded lines, trims and
uppercases text, skips comments and reports errors separately.

Native tests inject partial reads/writes, zero progress and flush/close failures,
and exercise binary files, broken pipes and resource cleanup. Compiler tests
check explicit state on failure, reset behavior, EOF, allocation failures before
effects, alias invalidation, mandatory result handling, proven payload access,
typed GloinIR, JIT/native/external execution and the complete programs above.
ASan/UBSan instruments the C++ compiler/runtime, not generated Gloin machine code.
