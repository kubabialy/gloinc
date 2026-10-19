# Filesystem helpers and process context (SPEC-030e)

Import `@fs` for paths and filesystem operations and `@process` for invocation
arguments, the host environment, and the current working directory. Public
implementations live in [fs.gloin](../stdlib/fs.gloin) and
[process.gloin](../stdlib/process.gloin). Both depend on `@strings`, `@arena`, and
`@status`. In 0.2.0, every fallible filesystem API uses
built-in `result<T>`; `Directory.is_open()` remains an ordinary bool query.
Failures carry `@status` categories in `.error.code` and captured native errno
in `.error.os_error` (zero when no native diagnostic applies). Process
argument/environment/cwd APIs now return `result<string>` as well. Nothing prints or
throws automatically. The new [child-process API](child-processes.md)
uses built-in `result<T>` for launch, observation, termination, nonblocking
pipes, bounded stdout/stderr capture, deadlines and cleanup. `start_with` adds
child cwd/environment options and an explicit process-group cleanup scope.

## Lexical paths

Only `/` is a separator on the supported POSIX host. A leading `/` makes a path
absolute; backslash is an ordinary byte. Helpers operate on counted bytes, reject
embedded NUL with `INVALID`, and do not look up files, expand `~`, resolve symlinks,
or resolve `.`/`..`. Empty lexical paths are allowed, although filesystem
operations reject them. Both extraction helpers return `result<string>`. Check
`.erroneous` before reading `.value`; failure has no readable empty-string
payload. Path validation failures use `.error.code = status.INVALID` and
`.error.os_error = 0`.

| API and example | Result | Ownership and cost |
| --- | --- | --- |
| `fs.basename("/work/report.txt/")` | `"report.txt"` | Borrowed input view, O(path bytes), no allocation |
| `fs.dirname("/work/report.txt/")` | `"/work"` | Borrowed input view, O(path bytes), no allocation |
| `fs.join(&memory, "/work/", "report.txt", 1024)` | `"/work/report.txt"` | Independent arena copy, O(input + output bytes), up to two arena requests |

`basename` and `dirname` ignore trailing separators. Empty input gives static
`.`; paths made entirely of separators give `/`. No slash gives dirname `.`.
`dirname` removes the separator run immediately preceding basename, retaining
interior runs: `a//b/c` has dirname `a//b`, while `a//b///` has dirname `a`.
Root extraction returns `/` even for `//` or `///`; there is no special lexical
network-root interpretation. Host filesystem resolution of double-leading slashes
still follows the host, because filesystem operations receive the original bytes.
Borrowed views last only as long as the original bytes; static `.` and `/` have
process lifetime. No extension-stripping or normalization occurs.

`join(memory: &arena.GeneralArena, base: string, child: string, max_bytes: u64)`
returns `result<string>`. Both inputs are validated, including an ignored base.
An absolute child replaces base unchanged. An empty side copies the other unchanged;
both empty gives static empty text without allocation. Otherwise trailing base
separators are reduced at the join boundary and one separator joins the components.
Examples: `join("///", "x")` produces `/x`, `join("a/../b", "c")` produces
`a/../b/c`, and `join("a", "/x")` produces `/x`. The shortened notation here omits
the required arena and byte bound.

The final byte bound and overflow are checked before allocation (`TOO_LONG` on
failure). Root joins and single-input copies make one exact output-size request;
ordinary joins make an intermediate `base + "/"` request followed by the final
output-size request. Both allocations remain until arena reset/free, including
an intermediate left behind on failure. `NO_MEMORY` is an error without a text payload. Allocator
zeroing and byte copying are included in the O(n) work; arena metadata/slab costs
are additional. There is no promised trailing NUL. Returned nonempty bytes expire
on destination arena reset/free and are independent of source bytes.

```gloin
import "@fs";
import "@std";
import "@status";

def main() -> i32 {
    def base: result<string> = fs.basename("/work/report.txt/");
    if base.erroneous { return 1; }
    def parent: result<string> = fs.dirname("/work/report.txt/");
    if parent.erroneous { return 1; }
    std.println(base.value);
    std.println(parent.value);
    return 0;
}
```

This prints `report.txt` and `/work` on separate lines.

## Metadata and explicit mutations

In 0.2.0, mutations return built-in `result<void>`.
Their errors carry a static `.message`, a library `.code`, and native
`.os_error`; `fs.FsResult` is removed. Success has no payload.
`metadata` returns `result<fs.Metadata>`, whose success payload contains only
`kind: i32` and `size: u64`. There are no dummy kind/size fields on failure.
Missing paths are errors with code `NOT_FOUND`; callers can handle that category
as an expected absence, but must check the result first. Other failures must
not be mistaken for absence. Validation and Gloin-side allocation failures
have zero OS code; native failures preserve errno, with EIO as a fallback when absent.
Status mapping is shared with [I/O](io.md): ENOENT and ENOTDIR map to `NOT_FOUND`, access
denials `PERMISSION_DENIED`, existing destinations where disallowed
`ALREADY_EXISTS`, native ENOMEM `NO_MEMORY`, and other OS failures `IO_ERROR`.
A failed query never pretends the path merely does not exist.

| API | Behavior / example |
| --- | --- |
| `fs.metadata("data.bin") -> result<fs.Metadata>` | Native `lstat`; `fs.FILE = 1`, `fs.DIRECTORY = 2`, `fs.SYMLINK = 3`, `fs.OTHER = 4` |
| `fs.mkdir("output") -> result<void>` | Create one directory with mode 0777 filtered by host umask; no parent creation |
| `fs.remove_file("old.bin") -> result<void>` | Unlink one file or symlink itself; never recursively remove or remove a directory |
| `fs.rename_replace("draft.txt", "report.txt") -> result<void>` | Native POSIX rename with explicit replacement of a compatible existing target |

These operations reject empty/NUL-containing paths before filesystem access.
Each has O(path bytes) preparation plus potentially blocking host filesystem work.
Each temporarily allocates a native path-length + 1 byte terminated copy and frees
it before returning; rename makes two such copies. They allocate no arena storage
and retain no input reference. Native temporary allocation failure is recoverable.
There are no automatic retries, rollback, copy fallbacks, or existence prechecks.

Metadata does not follow a terminal symlink: even a broken symlink reports
`SYMLINK`. Trailing separators still impose host directory-resolution semantics.
`size` is logical file byte size for regular files, link-target text byte length
for symlinks, and zero for directories/other objects. It is not disk usage or an
atomic snapshot tied to a later open. A negative native file size reports `OVERFLOW`.
`mkdir` returning `ALREADY_EXISTS` does not establish that the entry is a directory.
Check metadata if that distinction matters.

### Mutation migration from 0.1.0

| API | 0.1.0 return | Development return | Failure message |
| --- | --- | --- | --- |
| `fs.mkdir(path)` | `FsResult` | `result<void>` | `cannot create directory` |
| `fs.remove_file(path)` | `FsResult` | `result<void>` | `cannot remove file` |
| `fs.rename_replace(from, to)` | `FsResult` | `result<void>` | `cannot rename path` |
| `Directory.close()` | `FsResult` | `result<void>` | `cannot close directory`, or `directory is closed` before a native call |

Replace `.status != status.OK` with an explicit result check. Inspect
`.error.code` only after proving failure; no success field is available:

```gloin
import "@fs";
import "@status";

def main() -> i32 {
    def created: result<void> = fs.mkdir("output");
    if created.erroneous {
        // ALREADY_EXISTS can also describe a regular file. This program
        // requires a fresh directory and gives that case its own exit code.
        if created.error.code == status.ALREADY_EXISTS { return 2; }
        return 1;
    }
    return 0;
}
```

All failure categories and OS codes from the old wrappers are preserved.
Argument validation precedes mutation; rename still validates both paths first.
Error messages identify the operation, while `.code` supplies the category.
Native errno values vary by platform; `.os_error = 0` means no native code,
not success. Copying or forwarding the error preserves both codes. There is no
partial byte count on these operations. They may still have external effects
before failure, notably close consuming a handle. The remaining filesystem
wrappers are also migrated in development:

| API | 0.1.0 return | Development return |
| --- | --- | --- |
| `basename`, `dirname`, `join` | `PathResult` | `result<string>` |
| `metadata` | `MetadataResult` | `result<Metadata>`; success has `kind` and `size` |
| `Directory.open` | `DirectoryResult` | `result<Directory>` |
| `Directory.next` | `DirectoryEntryResult` | `result<DirectoryRead>`; success has `end` and `name` |

All five old filesystem result structs are removed. A former metadata read
`info.kind` becomes `info.value.kind` after checking `info.erroneous`.
For directory iteration, first check failure, then inspect `read.value.end`;
`status.END` no longer crosses the public API. `DirectoryRead` is a successful
outcome record, not an error wrapper: neither it nor `Metadata` has status or
OS-error fields. There are no automatic conversions from the old structs.
Check each result before beginning another operation that could return early.
Extract needed payloads into ordinary values before loops; result access proofs
are conservatively forgotten across loops.

The existing `symlink`, `read_link`, `canonical_path`, `temp_dir`, `remove_dir`,
and `replace_file` signatures/messages remain the same and now also preserve
library categories and native errno. Their Gloin validation errors use
`INVALID`, excessive byte limits use `TOO_LONG`, and arena allocation failures
use `NO_MEMORY`, all with zero native code. Native failure categories remain
those documented for each operation. Codes do not change ownership, rollback,
or cleanup behavior. Partial I/O progress is not stored on an error.

New static messages identify path validation (`path must not contain NUL`),
join bounds (`joined path exceeds byte limit`), copy/concatenation failure
(`cannot copy path`, `cannot join path`), metadata (`cannot inspect path`),
directory path validation (`directory path must be nonempty and contain no NUL`),
state allocation (`directory state allocation failed`), native opening/reading
(`cannot open directory`, `cannot read directory`), and entry-name allocation
(`cannot copy directory entry`). Use the documented code to classify failures;
message text is explanatory rather than a stable machine-readable key.

Rename replaces compatible regular-file destinations or empty directory
destinations under host rules. It renames symlinks themselves, is subject to host
permissions, and fails across filesystems (`IO_ERROR`, EXDEV) instead of copying.
Naming the operation `rename_replace` makes its replacement policy visible.
Use `remove_dir` below for one empty directory. A public recursive-removal API
and a no-replace rename API remain follow-ups. After proving failure, use
`io.error_message(&arena, failure.error.os_error)` to format a native diagnostic
separately if desired.

## Checked file replacement

`fs.replace_file(path: string, expected: string, replacement: string) -> result<void>`
replaces an existing file only after checking that its contents match the
previously read `expected` bytes. All text is counted: NUL is allowed in file
contents, but not in the nonempty path. Empty contents are supported. Inputs
are borrowed for the duration of the call and are not retained.

The source must be a regular file, readable and writable by the caller, owned
by the effective user, with exactly one hard link and without setuid, setgid
or sticky bits. Final symlinks, trailing slashes and final `.`/`..` components
are rejected. Earlier path components use host resolution and may follow
symlinks. This API assumes exclusive editing and a stable parent namespace.

The runtime opens the parent directory and source, checks metadata and contents,
creates an exclusive temporary sibling with initial mode 0600, writes the new
bytes, restores the original uid/gid and ordinary mode bits, calls `fsync` on
the temporary file, and checks its close result. It then rechecks the original
bytes, identity, size, ownership, mode, link count, mtime and ctime before
`renameat` replaces the name. Reads/writes retry interruptions. Existing open
handles continue to see the old inode. A pre-rename error preserves the original;
temporary unlink/descriptor cleanup is best effort after host errors.

**Limits:** the final comparison and rename are separate host calls, so this
is not a compare-and-swap operation and cannot guarantee detection of concurrent
writers. ACLs, extended attributes and timestamps are not copied. The parent
directory is not synced; success does not promise persistence across a crash.
Do not use it where those metadata or concurrency guarantees are required.

The wrapper returns built-in errors with static messages, library categories in
`.error.code`, and native errno in `.error.os_error`. No Gloin arena is needed. Preparation makes temporary
native path copies; byte work is O(path + expected + replacement), including
two bounded-buffer reads of the original. Native temporary allocation failure
is recoverable; host I/O can block. The call uses `gloin.abi_call` at the
GloinIR/native boundary.

`gloinfmt --write` uses this operation after independently checking tokens and
idempotence. See the [formatter guide](gloinfmt.md#write-contract).

## Symbolic links

Gloin 0.1.0 exposes these operations on macOS and Linux:

| API | Success | Failure |
| --- | --- | --- |
| `fs.symlink(target: string, link_path: string) -> result<void>` | Creates one new link storing `target` verbatim | Invalid input, existing destination, missing parent, permission, allocation or other host error |
| `fs.read_link(memory: &arena.GeneralArena, path: string, max_bytes: u64) -> result<string>` | An independent arena copy of the immediate link's stored target bytes | Invalid input/bound, non-link entry, missing path, permission, allocation, oversized target or other host error |

Check `.erroneous` before accessing `.value` or `.error`. In development these
APIs preserve `@status` categories and native errno in `.error.code` and
`.error.os_error`. Argument validation/allocation errors have zero native code;
OS failures retain it. Do not parse message text as a stable error code.
`metadata` and `remove_file` also use built-in results as described above.

### Creation and resolution

Both target and link path must be nonempty and contain no NUL. They are counted
bytes, with no UTF-8 validation, shell expansion or path normalization. A
relative `link_path` is relative to the current working directory. A relative
**target** is resolved from the link's containing directory when someone later
follows the link. For example, `symlink("../data.txt", "/work/links/current")`
stores `../data.txt`; following `/work/links/current` looks up `/work/data.txt`.
`read_link` returns `../data.txt`, not that resolved path.

The target need not exist. Broken links and cyclic target text are allowed.
Creation performs one host `symlink` call without an existence precheck; any
existing destination, including a broken link, causes failure. It never removes
or replaces that entry and never creates parent directories. Preparation takes
O(target + link-path bytes) and two temporary native terminated copies, freed
on return. No input reference or arena allocation is retained.

### Bounded reading and ownership

`read_link` reads the final link itself without recursively following its
target. It can therefore read a broken link or a link that points to itself.
It does not return a canonical path or prove that the target exists. Earlier
directory components still undergo ordinary host resolution: links in those
components can be followed, and a cycle there can fail. Trailing `/` also
retains host directory-resolution behavior. This is not a path-confinement API.

`max_bytes` limits the successful target length. An exact fit succeeds; a
longer target fails without returning a truncated string. Zero is allowed as a
bound but cannot hold a nonempty target. Bounds above 9223372036854775806 are
rejected before allocation on the supported 64-bit targets. Choose a practical
bound: a representable limit does not guarantee available memory.

The Gloin wrapper requests `max_bytes + 1` zeroed arena bytes. The extra byte
detects overflow in one native `readlink` operation; there is no metadata-size
precheck or grow-and-retry loop. Interrupted host reads retry on `EINTR`.
The native request count is capped at 2147483647 for host ABI compatibility;
filling that cap also fails conservatively rather than returning truncated text.
Preparation/zeroing is O(path bytes + max_bytes); the host copies up to the
buffer capacity. One temporary native path copy is freed on return. The arena
request remains until reset/free, including after a host error or bound failure.
Allocation failure is recoverable. Invalid input/bounds are checked first.

Successful text is independent of the input path and filesystem entry, survives
later reads or unlinking, and expires when its arena is reset/freed. No trailing
NUL is promised. There is no atomic relationship with another metadata/open
operation; the filesystem may change between calls.

| Operation | Final symlink behavior |
| --- | --- |
| `fs.metadata(path)` | Reports the link (`SYMLINK`), including a broken link |
| `fs.read_link(&memory, path, limit)` | Reads the stored target bytes |
| `fs.remove_file(path)` | Unlinks the link; leaves its target untouched |
| `io.File.open_read(...)` (and other file open modes), or `fs.Directory.open(...)` | Follows links under host path-resolution rules |

Run the [complete example](../examples/symlinks.gloin) with a new destination:

```sh
./build/gloinc --jit examples/symlinks.gloin -- absent ./demo-link
```

It creates a broken link, prints `Stored target: absent`, checks its metadata
and removes the link. An existing `./demo-link` is preserved and reported as an
error. The example also works with an existing file or directory target.

Public wrappers, allocation bounds, ownership and result handling live in Gloin. The native POSIX calls
cross `gloin.abi_call`; string/result values retain GloinIR types until their
explicit ABI/storage boundary.

## Canonical paths and temporary directories

These additions are part of Gloin 0.1.0 on macOS and Linux. They
use built-in results: inspect `.erroneous`, then `.value` or `.error`. Errors
carry static messages; in development they also retain library categories and
native errno. Message text is not a stable error code.

| API | Behavior |
| --- | --- |
| `fs.canonical_path(memory: &arena.GeneralArena, path: string, max_bytes: u64) -> result<string>` | Resolve an existing path to absolute text, following symlinks and resolving dot components |
| `fs.temp_dir(memory: &arena.GeneralArena, parent: string, prefix: string, max_bytes: u64) -> result<string>` | Exclusively create a directory under an existing parent; return its absolute path |
| `fs.remove_dir(path: string) -> result<void>` | Remove one empty directory; reject a final symlink and nonempty directories |

### Existing paths and byte limits

`canonical_path` uses native `realpath`, with temporary host-allocated result
storage. It is different from lexical `join`: it consults the filesystem, and
missing paths, broken links, link cycles and permission failures are errors.
Relative inputs use the current working directory. Inputs are counted bytes,
must be nonempty, and cannot contain NUL; there is no UTF-8 validation or shell
expansion. For example, resolving `./link/../file` follows host resolution
order, including the link, before producing an absolute spelling.

The successful result must fit `max_bytes`; an exact fit succeeds, and overflow
fails without a truncated value. Zero is a valid bound but cannot hold an
absolute path. Bounds above 9223372036854775806 are rejected before allocation.
One `max_bytes + 1` zeroed arena request remains until reset/free, including
after host errors. Native input and resolved-path storage are freed before
return. Preparation/zeroing costs O(path bytes + max_bytes), plus host lookup
and copying of the resolved path. The bound limits the returned text and arena
buffer, not all temporary storage inside the host's `realpath`.

The returned bytes are independent of the input and survive subsequent calls
or removal of the named entry. They expire on arena reset/free. Canonicalizing
does not reserve a name, hold a directory handle, or establish that a later
open will refer to the same object. It is a snapshot, not path confinement.

### Exclusive temporary creation

`temp_dir` requires an explicit parent; it does not choose `/tmp`, inspect
`TMPDIR`, or create parents. The parent is canonicalized first. The prefix
must be nonempty and contain neither `/` nor NUL. The generated leaf is
`prefix-XXXXXX`, with the final six bytes replaced by the host. The separating
hyphen preserves a prefix that itself ends in `X`. Names and suffixes vary
between calls; callers must use the returned path rather than predict a name.

Native `mkdtemp` creates the directory exclusively with requested mode 0700
(the host umask may further restrict it). Existing entries are never replaced.
The result is an absolute path whose full byte length must fit `max_bytes`.
This is an explicit filesystem side effect: freeing its arena releases the
path storage, **not the directory**. See the native
[mkdtemp contract](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/mkdtemp.3.html).

All Gloin allocations, size checks and template construction precede creation.
After the successful host creation, returning the already-owned path requires
no further allocation or fallible copy. An allocation or output-bound failure
therefore cannot leave an unreported directory behind. A successful path still
must be explicitly cleaned up if later application work fails.

For an ordinary non-root parent, construction makes five arena byte requests:
the leaf template, the canonical-parent buffer, a join prefix, the joined
template, and the final writable output buffer. Root joins need one fewer.
All intermediate allocations remain until reset/free, including on failure.
Work includes zeroing the canonical-parent bound and copying the constructed
paths, plus native resolution and exclusive creation. The result is already in
caller-owned storage when `mkdtemp` returns. Concurrent namespace changes can
still change which parent the later creation resolves to; this is not a
directory-capability API.

### Empty-directory removal and cleanup order

`remove_dir` uses native `rmdir`. It never traverses contents or removes a
regular file or final symlink. Trailing slashes are stripped before calling the
host, so `link///` cannot turn a final link into a followed target. Empty/NUL
paths, `/` (including repeated slashes), and final `.` or `..` components are
rejected. Earlier path components retain host resolution rules; these lexical
guards do not establish confinement. Missing entries, nonempty directories,
permissions and other host failures return errors. Success allocates no arena
storage; one temporary native path copy is freed on return.

Close files/cursors first, unlink files and links with `remove_file`, then call
`remove_dir` on the empty directory. Free the arena holding those names last.
There is no automatic destructor or public recursive-removal function.

The [complete temporary-workspace example](../examples/temporary_workspace.gloin)
shows this order with `defer`:

```sh
./build/gloinc --jit examples/temporary_workspace.gloin -- ./build
```

It prints the generated absolute path, writes a file, then closes and removes
the file and directory before freeing the arena. The parent must already exist.
The three new host operations use `gloin.abi_call`; bounds, template construction,
arena ownership and built-in results remain in Gloin.

The formatter's [Gloin runner](../tests/formatter/runner.gloin) performs its own
bounded recursive cleanup on exclusively created fixture trees after children
have stopped. It unlinks observed symlinks without following them and retains
failed runs for inspection. Its traversal assumes no concurrent replacement or
renaming; it is not a general deletion API for untrusted changing trees.
Cleanup failure can leave a partly removed tree; completed removals are not
rolled back, and the runner reports failure.

## Directory iteration

`fs.Directory.open(&owner, path)` returns `result<fs.Directory>`. It opens a native directory cursor; the owner arena holds
shared cursor metadata. Copies of `Directory` alias that metadata. Close the
cursor before resetting or freeing the owner arena. Opening an empty or
NUL-containing path returns `INVALID`; missing paths and access errors use the
same status and OS error mapping as `metadata`. A symlink in the path may resolve
to a directory, as with native `opendir`. Open attempts one owner-arena metadata
allocation before the host call; that allocation remains until arena reset/free
even if the host open fails.

`directory.next(&scratch)` returns `result<fs.DirectoryRead>`. First check
`.erroneous`; a failure has no readable success payload. After success:

| `read.value.end` | `read.value.name` | Meaning |
| --- | --- | --- |
| `false` | Nonempty relative entry name, copied into `scratch` | One entry was read |
| `true` | Static empty string | The cursor reached normal end |

End is not an error and allocates no name storage. Repeated reads of an
unchanged exhausted directory succeed with `end = true`. There is no permanent
end latch: concurrent directory changes retain native `readdir` behavior.
The compiler proves result success, not that `end` is false; reading the empty
name at end is valid, and callers must inspect `end` before treating it as an entry.
`.` and `..` are omitted. An arena copy failure reports `NO_MEMORY` and can
consume the entry already read from the host; retrying does not recover that name.
Names are counted bytes, including non-UTF-8 bytes. Copies remain valid until
`scratch` is reset or freed, independent of later reads or cursor close. Each
nonempty name makes one exact-size arena allocation; end and native failures
allocate no name storage. Directory order is host-defined, unsorted and not
snapshotted.
Callers requiring deterministic order must collect and sort names. To inspect
an entry's kind, join its name to the parent path and call `fs.metadata`; the
directory cursor itself does not promise `d_type` information.

`directory.close() -> result<void>` consumes the native cursor, even on an OS
close failure, and invalidates every alias. Later `next` and `close` calls
fail with `.error.code = status.CLOSED` and
`.error.os_error = 0`. Native close failures preserve their mapped status and
errno. Bind and check the close result, including in cleanup helpers. A deferred
cleanup helper must handle/report a failure itself; it cannot return a failure
from the function that scheduled it.
No arena cleanup closes a native cursor automatically. Opening, advancing,
and closing can block on the host filesystem. Calls through aliases must not
advance or close the same cursor concurrently. Concurrent directory mutation
may change which entries are seen. See [directory_walk.gloin](../examples/directory_walk.gloin)
for a complete single-directory CLI example.

## CLI forwarding and argument ownership

```sh
gloinc --jit program.gloin -- --copy 'source file.bin' 'new copy.bin'
gloinc --jit -- -program.gloin -- --help
```

Only arguments after the delimiter **following FILE** reach the program. Argument
zero is the exact source filename spelling supplied to the CLI, not an absolute
or canonicalized filename and not the compiler executable. The first command has
four arguments: filename, `--copy`, source, and destination. With no forwarding,
the CLI still supplies just argument zero. Empty and non-UTF-8 argument bytes are
preserved, with no encoding conversion; native argv cannot contain NUL.
Compiler options are never included. Bare extra arguments without a delimiter
remain usage errors.

The existing delimiter **before FILE** still ends compiler-option parsing, making
filenames beginning with `-` usable. A second delimiter after that filename starts
forwarding. Forwarding, even an empty trailing delimiter, is valid only in run
mode; `--check`, `--emit-ir`, and `--emit-llvm` reject it with usage exit status 2.
Compiler `--help` stays a standalone command. Program `--help` is ordinary forwarded
text that the program handles itself.

`JitRunner::run(module, arguments)` takes an optional `const std::vector<std::string>&`
and copies its strings into an invocation-owned context immediately before entry.
The synchronous caller keeps its inputs valid and does not mutate them concurrently.
Embedded NUL is rejected as an execution-setup diagnostic. No synthetic argument
zero is inserted for embedding; the caller supplies every argument intentionally.
The default argument vector is empty. Contexts are thread-local and nested scopes
restore their predecessor on success or failure. The snapshot costs O(argument
count + total bytes) native memory/time and is freed at invocation exit. It does
not retain the caller's string pointers.

External runners likewise start with zero arguments; their own host/compiler flags
are never exposed. Native embedders can use the installed
[context_runtime.h](../src/context_runtime.h):
`gloin_process_arguments_push(count, argv)` deep-copies NUL-terminated C strings
and returns an opaque scope, or null for invalid inputs/allocation failure without
changing the previous context. `gloin_process_arguments_pop(scope)` must run on the
same thread in LIFO order, frees that snapshot, and restores the previous scope.
An out-of-order/null pop returns INVALID without changing state. The scope token
expires after successful pop. Initialize the same runtime library used by the
external program. This API scopes arguments only, not the host environment or cwd.

## Argument, environment, and cwd functions

`process.arg`, `process.env`, and `process.cwd` now return `result<string>`;
the former `TextResult` is removed. Failure has no readable text payload.
Nonempty successful results are independent caller-arena copies; empty
argument/environment values succeed as static empty text. Errors carry `@status`
in `.error.code`. Argument/env errors have `.error.os_error = 0`; cwd errors can
carry native errno. All copied bytes expire
on arena reset/free, independent of argument scope exit or later environment changes.

| API and example | Contract | Cost and allocation |
| --- | --- | --- |
| `process.arg_count() -> u64` | Current invocation argument count | O(1), no allocation |
| `process.arg(&memory, 1) -> result<string>` | Checked u64 index; `OUT_OF_RANGE` if absent | O(argument bytes) copy/zeroing, one exact-size arena request for nonempty bytes |
| `process.env(&memory, "GLOIN_COPY_LABEL") -> result<string>` | Missing fails `NOT_FOUND`; present empty succeeds | O(name + value bytes) plus host environment lookup; temporary native name-length + 1 copy, then one exact-size arena request for nonempty value |
| `process.cwd(&memory, 4096) -> result<string>` | Absolute host cwd with explicit u64 byte bound | One max_bytes + 1 arena request, O(max_bytes) zeroing plus host getcwd work |

`arg` checks the index before touching the arena. It does not retain invocation
pointers. `env` rejects an empty name, NUL, or `=` with `INVALID`; it does not
expand variables, distinguish text from binary bytes, or make assumptions about
UTF-8. Missing/invalid/empty results do not access arena storage. Native environment
lookup is followed immediately by copying. Host environment mutations must be
externally coordinated with these calls; the library does not snapshot the whole
environment or synchronize unrelated host `setenv` calls.

Cwd's byte bound excludes NUL. A too-small bound fails `TOO_LONG` with ERANGE,
without a truncated path payload. Bounds above 9,223,372,036,854,775,806 return
`NO_MEMORY` before allocation; other allocation failures also return `NO_MEMORY`.
Storage is retained on native cwd failure until arena reset/free. A deleted or
inaccessible working directory preserves the actual OS failure. This query does
not change cwd; host cwd changes affect filesystem operations and require host
coordination when multiple threads depend on relative paths.

```gloin
import "@process";
import "@arena";
import "@strings";
import "@status";
import "@std";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    if process.arg_count() != 1 { return 1; }
    def name: result<string> = process.arg(&memory, 0);
    if name.erroneous { return 2; }
    if strings.is_empty(name.value) { return 2; }
    def missing: result<string> = process.arg(&memory, 1);
    if !missing.erroneous { return 3; }
    if missing.error.code != status.OUT_OF_RANGE { return 3; }
    std.println("arguments ready");
    return 0;
}
```

Run without forwarded arguments; it prints `arguments ready`.

## Complete tool and validation

[file_tool.gloin](../examples/file_tool.gloin) accepts `--copy SOURCE NEW_DESTINATION`
or `--help`. It joins paths against bounded cwd, checks that the source is a regular
file, copies binary data with separate owner/scratch arenas, creates the destination
exclusively, and checks flush/close before reporting a byte count. Existing targets
are preserved. A failed copy can leave a partial newly-created destination.

```sh
build/gloinc --jit examples/file_tool.gloin -- --copy 'source file.bin' 'new copy.bin'
GLOIN_COPY_LABEL=saved build/gloinc --jit examples/file_tool.gloin -- --copy source.bin copy.bin
```

Missing `GLOIN_COPY_LABEL` uses `copied`; an empty value omits the label; a nonempty
value replaces it. The example limits cwd to 4096 bytes and joined paths to 65536
bytes. It rejects unknown/missing options before filesystem mutation and rejects
symlink/non-regular sources. Source filenames beginning with `-` remain ordinary
program arguments after the forwarding delimiter.

`ContextRuntimeTest` checks actual file kinds and mutations, counted paths,
permission errors, nested/owned/thread-isolated argument contexts, raw environment
values, cwd bounds, and deleted cwd. `ContextLibraryTest` checks the lexical matrix,
allocation failures, public/native signatures, CLI parsing and exact forwarding,
JIT context restoration, external execution, and the guide/example programs.
Installed and relocated compilers run the same tool from another working directory,
with spaces in paths and missing/empty/populated labels.
