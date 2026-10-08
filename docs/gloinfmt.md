# `gloinfmt`

`gloinfmt` is a formatter written in Gloin. Build it with the compiler:

```sh
cmake --build build --target gloin_formatter
```

It is also installed alongside `gloinc` as `bin/gloinfmt`.
The distributed executable uses the Gloin runtime; it does not invoke the
compiler when formatting a file.

## Use

```sh
build/gloinfmt examples/hello_world.gloin
build/gloinfmt examples/hello_world.gloin > /tmp/hello_world.gloin
build/gloinfmt --check .
build/gloinfmt --check examples stdlib
build/gloinfmt --write examples stdlib
build/gloinfmt --check --git
build/gloinfmt --write --git
```

For example, given this file:

```gloin
def main() -> i32 {
if true {
return 0;
}
return 1;
}
```

`gloinfmt FILE` prints:

```gloin
def main() -> i32 {
    if true {
        return 0;
    }

    return 1;
}
```

With one file argument, it prints formatted source to standard output and does
not change the input file. `--check` compares source with formatted output. It
accepts files and directories; with no path it checks the current directory.
Directory traversal is recursive and sorted by byte order. It checks `.gloin`
files, does not follow symlinks, and skips `.git`, `.idea`, `.cache`,
`node_modules`, `build`, and directories whose names begin with `build-`.
An explicitly named file is checked regardless of its suffix.

`--write` accepts the same files/directories and replaces changed files in
place. Unchanged files are left alone. Before printing, checking or writing,
the formatter independently compares lexical tokens and formats the result a
second time to verify idempotence. A failed safety check stops that file.

`--git` selects tracked and unignored untracked `*.gloin` files beneath the
current Git working directory. It cannot be combined with explicit paths.
It runs `/usr/bin/git ls-files -z --cached --others --exclude-standard -- '*.gloin'`
using direct process arguments, bounded pipes and group cleanup. NUL-separated
names preserve spaces and newlines. Duplicate paths are removed; deleted
tracked files and final symlinks are skipped. Git output is limited to 256 KiB,
stderr to 16 KiB, and communication to ten seconds; exceeding a limit fails.
Git is required only for this option. Ordinary tree traversal has a 64-level
depth bound and uses the skip list above, independently of Git ignore rules.

Exit status is `0` for success or a clean check, `1` when a check finds files
that need formatting, and `2` for usage or I/O errors. In check mode, paths that
need formatting are printed to standard output. An error message goes to
standard error.
Write mode prints each successfully changed path and returns `0` on success.
Files are processed individually; an error can leave earlier files formatted.
Paths printed for people are newline-delimited, even when a filename itself
contains a newline; use the exit status for automation.

### Write contract

Writes use [`fs.replace_file`](filesystem-process.md#checked-file-replacement).
The original must be a readable/writable regular file owned by the current
effective user, with one hard link and no setuid/setgid/sticky bits. Final
symlinks are rejected. Contents and metadata are checked before staging and
again before replacement. A temporary file is created exclusively in the same
directory, fully written, assigned the original uid/gid and ordinary permission
bits, synced and closed, then renamed over the source. A pre-rename failure
leaves the original intact; temporary-file cleanup is best effort on host errors.

Use this on files you are exclusively editing. The last check and rename are
separate operations, so concurrent changes are not reliably excluded. Parent
symlinks may be followed. ACLs, extended attributes and timestamps are not
copied; this is a deliberate limit of this first write API. Directory syncing
and crash durability are not promised. Review the resulting diff.

The formatter changes indentation, blank lines, assignment alignment, trailing
whitespace, and line endings while keeping source tokens in order. It indents
nested blocks and multiline calls, and preserves CRLF when the input uses CRLF.
It aligns `=` in adjacent single-line declaration groups and assignments to the
same kind of target. It separates setup, validation, loops, and final returns,
while keeping immediate guards and `defer` registration with their declarations.
Comments and existing blank lines delimit alignment groups; leading comments
stay attached to the step they describe. Assignments inside one-line guards,
comparisons, and `=` inside strings or comments do not join alignment groups.

These rules run in the Gloin-written formatter for both normal output and
`--check`. Other spacing within a line is preserved. It does not split statements
or rewrap calls. Review output against the [source style guide](gloin-style.md),
which includes those editorial choices. Sources larger than 8 MiB or nested
deeper than 256 braces are rejected. Output is bounded to eight times the input
size plus 64 bytes; the formatter reports an error if alignment exceeds that
limit. Token and line metadata use temporary arena storage proportional to the
source size.

Formatting is idempotent: running it again produces identical bytes. The
test suite checks this and compares lexical tokens before and after formatting
across the repository's Gloin sources. It also tests native and JIT execution,
comments and string literals, CRLF, traversal order, and skipped symlinks.
Assertions, independent token comparison and the process driver are written in
Gloin. Shared capture/cleanup is in `tests/support/tool_runner.gloin`.
CTest invokes `gloinfmt_test --run`; the driver creates an exclusive
private work directory, launches children with pipes and group cleanup, and
captures stdin/stdout/stderr with a 256 KiB bound per stream and a 30-second
communication deadline. Child completion and expected exit codes are checked.
Successful runs remove their fixture trees; failed runs print and retain the
evidence directory. A cleanup failure can leave a partly removed tree and is
reported as a test failure. Capture failures report an error without claiming complete
partial output. Recursive cleanup assumes the owned tree is no longer changing;
it is not a public general-purpose deletion API. Broken, cyclic and
outside-pointing symlinks are tested without following them. The temporary
CMake driver has been removed. Completed Python migrations are tracked in the
[tooling roadmap](tooling-roadmap.md).
See [running the formatter tests](../tests/README.md#running-and-inspecting-tests).

The separate Gloin tooling tests exercise native/JIT writing, Git discovery,
ignored files, newline-containing names, symlink rejection and repeated writes.
CI runs `gloinfmt --check .` after building the compiler. The old Python
formatter has been removed.
