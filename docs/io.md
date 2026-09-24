# Streams and files (SPEC-030d)

`import "@io";` loads [io.gloin](../stdlib/io.gloin). Public ownership, allocation,
and convenience methods are ordinary Gloin code. Seven private primitives bridge
the host's native I/O. Import `@arena` for storage and `@status` for stable error codes.

## Read a line and write it

```gloin
import "@io";
import "@arena";
import "@status";

def main() -> i32 {
    def mut scratch: arena.GeneralArena = arena.GeneralArena.create();
    defer scratch.free();
    def mut input: io.Stream = io.stdin();
    def mut output: io.Stream = io.stdout();
    def line: io.ReadResult = input.read_line(&scratch, 128);
    if line.status != status.OK { return 1; }
    def written: io.WriteResult = output.write_line(line.value);
    if written.status != status.OK { return 2; }
    return output.flush().status;
}
```

With `hello` on stdin, this prints `hello` and returns zero. Results never replace
`main`'s return value or cause automatic printing. `std.print`, `std.println`, and
`std.input` keep their existing contracts. Input shares the same native line-reader
implementation; both modules use the same standard streams and buffering.

## Ownership and result types

`io.Stream` is borrowed and has no public `close` method. `io.stdin()`,
`io.stdout()`, and `io.stderr()` produce handles to process streams in O(1), with
no Gloin allocation. Copies share cursor position and native buffering. These
handles have process lifetime; stdin is readable, stdout/stderr are writable.
Standard buffering is inherited from the host. A successful write can still need
an explicit `flush` before buffered errors become observable.

`io.File` owns an opened native stream. Every open takes an explicit metadata
arena: `io.File.open_read(&owner, path)`, for example. A successful open allocates
one private `FileState` in that arena (one pointer: 8 bytes on the supported
arm64 target, plus arena alignment/metadata overhead). Copies of a File and Streams borrowed via
`file.stream()` reference the **same** state. Closing any File alias clears that
shared state and consumes the native stream even if close reports an error.
Every later operation through an alias reports `CLOSED`; repeated close is
`CLOSED` with `os_error = 0`. There is no reference count or automatic destructor.
A failed open returns a closed File, safe to close again.

**Close before resetting/freeing the metadata arena.** Reset/free does not close
files. Aliases become invalid memory after the metadata arena is reset/freed,
even if `is_open()` would have returned false before that point. Use a separate
scratch arena for read buffers and reset it between chunks. Owned files use
unbuffered native I/O: native `FILE` storage exists until close, but no additional
retained byte buffer is requested. `flush` is still available to observe native
errors; it is not `fsync` and does not promise storage durability.

`defer file.close()` ensures a cleanup attempt but discards the result. Explicitly
check `file.close()` when success matters; a later deferred repeated close is safe
while the metadata remains live. Methods that perform I/O or close require a
mutable handle. `stream()` and `is_open()` accept read-only receivers. Handle
copies are not independent streams and require external coordination between
threads; this first library does not supply concurrent file ownership.

| Result | Fields | Interpretation |
| --- | --- | --- |
| `io.IoResult` | `status: i32`, `os_error: i32` | Flush/close outcome |
| `io.FileResult` | `status`, `os_error`, `value: io.File` | Open outcome; failed value is closed |
| `io.ReadResult` | `status`, `os_error`, `value: string` | Text or counted binary bytes, including defined partial progress |
| `io.WriteResult` | `status`, `os_error`, `written: u64` | Bytes accepted, **including progress before failure** |
| `io.MessageResult` | `status`, `value: string` | OS diagnostic text, or static empty text on failure |

Library validation/allocation failures have `os_error = 0`; OS failures preserve
`errno` where available, falling back to `EIO` if the host reports no code. Existing
status values stay unchanged. New values are `NOT_FOUND = 10` (ENOENT/ENOTDIR),
`PERMISSION_DENIED = 11` (EACCES/EPERM), `ALREADY_EXISTS = 12` (EEXIST), and
`CLOSED = 13`. ENOMEM maps to `NO_MEMORY`; other OS failures map to `IO_ERROR`.
Do not compare OS diagnostic strings to decide program behavior.

## Open and lifecycle methods

All open methods take `(owner: &arena.GeneralArena, path: string) -> io.FileResult`.
They operate on host paths relative to the process working directory. Paths are
counted bytes; empty paths and embedded NUL are `INVALID` before any allocation
or filesystem operation. File contents preserve every byte, including NUL.
Filesystem traversal, symlink handling, permissions, and append behavior follow
the host. Exclusive creation uses native exclusive-open semantics, without a racy
existence precheck.

| Example | Behavior |
| --- | --- |
| `io.File.open_read(&owner, "input.bin")` | Open existing file read-only; missing path is `NOT_FOUND` |
| `io.File.create_new(&owner, "new.bin")` | Create write-only, fail `ALREADY_EXISTS` if present, preserve existing data |
| `io.File.open_truncate(&owner, "report.txt")` | Explicitly truncate existing contents or create a new write-only file |
| `io.File.open_append(&owner, "log.txt")` | Open/create write-only; native writes append at the then-current end |
| `file.stream()` | Borrow a Stream sharing position and close state; O(1), no allocation |
| `file.is_open()` / `stream.is_open()` | Observe shared close state; O(1), no allocation, no OS validity probe |
| `file.close()` | Consume resource once, report close failure; O(1) cleanup plus blocking OS close, no Gloin allocation |

Open costs O(path bytes) plus potentially blocking filesystem work. Metadata is
allocated **before** the native open, so metadata allocation failure cannot create
or truncate a file. Metadata remains in the arena if a later open step fails.
The native boundary temporarily allocates path length + 1 bytes for a terminated
copy, frees that copy before returning, and allocates native FILE state until
close. These native allocations can fail; the arena does not own them. No path
reference is retained after open. New file permissions follow native creation
permissions and the process umask. A failed operation after successful creation
may leave the created file; there is no implicit deletion or rollback.

## Bounded reads

The following methods exist on both mutable `io.Stream` and mutable `io.File`:
`read_line(memory: &arena.GeneralArena, max_bytes: u64)`, `read_chunk(...)`, and
`read_all(...)`, each returning `io.ReadResult`.

| Example on either handle | Success and EOF | Bound/error behavior |
| --- | --- | --- |
| `input.read_line(&scratch, 128)` / `file.read_line(&scratch, 128)` | Strip LF or CRLF; standalone CR is data; final unterminated line is `OK`; empty EOF is `END` | Oversized line is drained through LF/EOF, then `TOO_LONG` with empty text; I/O error also clears text |
| `input.read_chunk(&scratch, 4096)` / `file.read_chunk(&scratch, 4096)` | Fill up to bound until EOF/error; final partial chunk is `OK`; a later empty read is `END` | Zero bound is `INVALID` without consuming input; on I/O error retain the received prefix |
| `input.read_all(&scratch, 65536)` / `file.read_all(&scratch, 65536)` | Read remaining bytes to EOF, including empty `OK` | Stop at bound and probe one byte; if more data exists, push it back and return `TOO_LONG` with the prefix; I/O error retains progress |

Each valid read makes one `max_bytes + 1` byte arena request, including a trailing
NUL not counted in string length. The allocator zeroes that storage first. Storage
is retained even on EOF or I/O failure, until reset/free. Result bytes borrow the
scratch arena, independently of later reads or file close. Bounds above
9,223,372,036,854,775,806 return `NO_MEMORY` before allocation. Allocation failure
consumes nothing. Zero bounds are useful for empty-line/empty-file checks:
`read_line(..., 0)` accepts only empty lines, while `read_all(..., 0)` leaves a
nonempty stream's first byte unread and reports `TOO_LONG`.

Chunk/all costs are O(max_bytes) including zeroing, plus blocking I/O. Line cost
is O(max_bytes + bytes consumed); draining an oversized line can block and read
arbitrarily far despite bounded storage. `read_all` does not drain beyond its
bound. Its single-byte probe can block, even when exactly max_bytes were read.
A chunk read may also block until its requested bytes or EOF/error; it is not a
nonblocking readiness API. An I/O error does not close a handle; subsequent reads
clear native EOF/error indicators and may be retried. Already consumed bytes are
not rolled back. No automatic EINTR retry hides a partial failure.

A closed handle returns `CLOSED` first; reading a write-only stream returns
`INVALID`; then bounds and allocation are checked. Those preflight failures
return empty text and do not access the scratch arena.

## Writes and flush

These methods likewise exist on both `io.Stream` and `io.File`:

| Example on either handle | Contract |
| --- | --- |
| `output.write("a\0b")` / `file.write("a\0b")` | One native fwrite; inspect `written` even on `OK`, which can be short |
| `output.write_all("hello")` / `file.write_all("hello")` | Repeat short successful writes; success means all bytes accepted; stop on error or no progress |
| `output.write_line("hello")` / `file.write_line("hello")` | write_all(text), then write_all(LF); count includes LF progress; failed text write suppresses LF |
| `output.flush()` / `file.flush()` | Explicit flush; report pending output error; no close and no disk-durability promise |

Write byte work is O(bytes accepted), or O(text bytes + 1) for a complete line,
plus blocking native I/O. None allocates Gloin storage or retains an input view.
Host standard-stream buffering may allocate internally. Flush costs O(pending
buffered bytes) plus native I/O. Owned unbuffered files have no pending user byte
buffer. Line writing is two operations and is not atomic; append does not make a
multi-call record atomic. `write_all` reports prefix progress on failure; retrying
the entire input can duplicate that prefix. A native zero-progress write becomes
`IO_ERROR` with EIO, avoiding an infinite loop.

Empty writes succeed with zero progress on live writable streams. Closed handles
return `CLOSED`; writing/flushing a read-only stream is `INVALID`. Native errors
are cleared on each write attempt so the caller can retry deliberately. The
runtime temporarily blocks SIGPIPE for the calling thread and consumes a new
pending SIGPIPE associated with EPIPE; broken pipes return a recoverable error.
It restores the previous signal mask and preserves an already-pending signal,
without changing process-wide signal disposition. This does not install a global
signal handler or change the existing `std.print` policy.

## OS error messages

`io.error_message(memory: &arena.GeneralArena, os_error: i32) -> io.MessageResult`
formats an OS code separately from I/O. Zero returns static `"no OS error"` with
no allocation. Negative codes are `INVALID` before allocation. Other codes reserve
256 arena bytes and query the host's localized message; `NO_MEMORY`, `TOO_LONG`,
or `INVALID` on lookup failure return static empty text. Successful bytes expire
on arena reset/free. Work/storage is bounded O(256) plus the host message lookup
and allocation cost. Unknown-code wording or lookup success is host-dependent.

```gloin
import "@io";
import "@arena";
import "@status";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def diagnostic: io.MessageResult = io.error_message(&memory, 0);
    if diagnostic.status != status.OK { return 1; }
    def mut output: io.Stream = io.stdout();
    if output.write_line(diagnostic.value).status != status.OK { return 2; }
    return output.flush().status;
}
```

This prints `no OS error`. On an actual failed open, pass the result's `os_error`
and send the resulting message to `io.stderr()` if appropriate.

## Complete examples and checks

[io_copy.gloin](../examples/io_copy.gloin) reads source/destination paths from two
stdin lines, uses exclusive creation, copies binary chunks with bounded scratch
reuse, and checks flush/close before reporting the byte count. For example:

```sh
printf '/tmp/source.bin\n/tmp/new-copy.bin\n' | build/gloinc examples/io_copy.gloin
```

Existing destinations are preserved. A copy failure may leave a partial new file.
[io_filter.gloin](../examples/io_filter.gloin) reads bounded lines, trims and
uppercases text, skips comments, writes stdout, and reports errors separately on
stderr. Arguments and general filesystem APIs belong to SPEC-030e.

The native tests inject partial reads/writes, zero progress, permission errors,
and flush/close failures; exercise real binary files and broken pipes; and verify
resource cleanup. Compiler tests cover each public method, alias invalidation,
metadata allocation before destructive opens, allocation failure without input
consumption, read-only/borrowed restrictions, primitive signatures and runtime
ABIs, external execution, examples, and the two complete programs above. Both
normal and installed/relocated compilers run these examples.
