# Filesystem helpers and process context (SPEC-030e)

Import `@fs` for paths and filesystem operations and `@process` for invocation
arguments, the host environment, and the current working directory. Public
implementations live in [fs.gloin](../stdlib/fs.gloin) and
[process.gloin](../stdlib/process.gloin). Both depend on `@strings`, `@arena`, and
`@status`. Results are concrete structs; nothing prints or throws automatically.

## Lexical paths

Only `/` is a separator on the supported POSIX host. A leading `/` makes a path
absolute; backslash is an ordinary byte. Helpers operate on counted bytes, reject
embedded NUL with `INVALID`, and do not look up files, expand `~`, resolve symlinks,
or resolve `.`/`..`. Empty lexical paths are allowed, although filesystem
operations reject them. The two extraction helpers return `fs.PathResult
{ status: i32, value: string }`; failed values are static empty text.

| API and example | Result | Ownership and cost |
| --- | --- | --- |
| `fs.basename("/work/report.txt/")` | `OK`, `"report.txt"` | Borrowed input view, O(path bytes), no allocation |
| `fs.dirname("/work/report.txt/")` | `OK`, `"/work"` | Borrowed input view, O(path bytes), no allocation |
| `fs.join(&memory, "/work/", "report.txt", 1024)` | `OK`, `"/work/report.txt"` | Independent arena copy, O(input + output bytes), up to two arena requests |

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
returns `fs.PathResult`. Both inputs are validated, including an ignored base.
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
an intermediate left behind on failure. `NO_MEMORY` returns empty text. Allocator
zeroing and byte copying are included in the O(n) work; arena metadata/slab costs
are additional. There is no promised trailing NUL. Returned nonempty bytes expire
on destination arena reset/free and are independent of source bytes.

```gloin
import "@fs";
import "@std";
import "@status";

def main() -> i32 {
    def base: fs.PathResult = fs.basename("/work/report.txt/");
    def parent: fs.PathResult = fs.dirname("/work/report.txt/");
    if base.status != status.OK || parent.status != status.OK { return 1; }
    std.println(base.value);
    std.println(parent.value);
    return 0;
}
```

This prints `report.txt` and `/work` on separate lines.

## Metadata and explicit mutations

`fs.FsResult` contains `status: i32` and `os_error: i32`.
`fs.MetadataResult` additionally contains `kind: i32` and `size: u64`; both are
zero on failure. Success has `os_error = 0`. Validation/allocation failures also
have zero OS code; OS failures preserve errno, with EIO as a fallback when absent.
Status mapping is shared with [I/O](io.md): missing paths are `NOT_FOUND`, access
denials `PERMISSION_DENIED`, existing destinations where disallowed
`ALREADY_EXISTS`, native ENOMEM `NO_MEMORY`, and other OS failures `IO_ERROR`.
A failed query never pretends the path merely does not exist.

| API | Behavior / example |
| --- | --- |
| `fs.metadata("data.bin") -> MetadataResult` | Native `lstat`; `fs.FILE = 1`, `fs.DIRECTORY = 2`, `fs.SYMLINK = 3`, `fs.OTHER = 4` |
| `fs.mkdir("output") -> FsResult` | Create one directory with mode 0777 filtered by host umask; no parent creation |
| `fs.remove_file("old.bin") -> FsResult` | Unlink one file or symlink itself; never recursively remove or remove a directory |
| `fs.rename_replace("draft.txt", "report.txt") -> FsResult` | Native POSIX rename with explicit replacement of a compatible existing target |

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

Rename replaces compatible regular-file destinations or empty directory
destinations under host rules. It renames symlinks themselves, is subject to host
permissions, and fails across filesystems (`IO_ERROR`, EXDEV) instead of copying.
Naming the operation `rename_replace` makes its replacement policy visible.
Directory removal, iteration, recursive operations, and a no-replace rename API
remain follow-ups. Use `io.error_message(&arena, result.os_error)` to format an
OS diagnostic separately if desired.

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

`process.TextResult { status: i32, os_error: i32, value: string }` has static empty
text on failure. Nonempty successful results are independent caller-arena copies;
empty argument/environment values succeed as static empty text. Argument/env
results have `os_error = 0`; cwd can carry a native error. All copied bytes expire
on arena reset/free, independent of argument scope exit or later environment changes.

| API and example | Contract | Cost and allocation |
| --- | --- | --- |
| `process.arg_count() -> u64` | Current invocation argument count | O(1), no allocation |
| `process.arg(&memory, 1) -> TextResult` | Checked u64 index; `OUT_OF_RANGE` if absent | O(argument bytes) copy/zeroing, one exact-size arena request for nonempty bytes |
| `process.env(&memory, "GLOIN_COPY_LABEL") -> TextResult` | Missing is `NOT_FOUND`; present empty is `OK/empty` | O(name + value bytes) plus host environment lookup; temporary native name-length + 1 copy, then one exact-size arena request for nonempty value |
| `process.cwd(&memory, 4096) -> TextResult` | Absolute host cwd with explicit u64 byte bound | One max_bytes + 1 arena request, O(max_bytes) zeroing plus host getcwd work |

`arg` checks the index before touching the arena. It does not retain invocation
pointers. `env` rejects an empty name, NUL, or `=` with `INVALID`; it does not
expand variables, distinguish text from binary bytes, or make assumptions about
UTF-8. Missing/invalid/empty results do not access arena storage. Native environment
lookup is followed immediately by copying. Host environment mutations must be
externally coordinated with these calls; the library does not snapshot the whole
environment or synchronize unrelated host `setenv` calls.

Cwd's byte bound excludes NUL. A too-small bound is `TOO_LONG` with ERANGE and empty
text, never a truncated path. Bounds above 9,223,372,036,854,775,806 return
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
    def name: process.TextResult = process.arg(&memory, 0);
    if name.status != status.OK || strings.is_empty(name.value) { return 2; }
    if process.arg(&memory, 1).status != status.OUT_OF_RANGE { return 3; }
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
