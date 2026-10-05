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

Exit status is `0` for success or a clean check, `1` when a check finds files
that need formatting, and `2` for usage or I/O errors. In check mode, paths that
need formatting are printed to standard output. An error message goes to
standard error.

The formatter changes indentation, blank lines, trailing whitespace, and line
endings while keeping source tokens in order. It indents nested blocks and
multiline calls, and preserves CRLF when the input uses CRLF. It does not change
spacing *within* a line, split statements, or rewrap calls. Review output against
the [source style guide](gloin-style.md), which includes rules the formatter does
not enforce. Sources larger than 8 MiB or nested deeper than 256 braces are
rejected.

Formatting is idempotent: running it again produces identical bytes. The
test suite checks this and compares lexical tokens before and after formatting
across the repository's Gloin sources. It also tests native and JIT execution,
comments and string literals, CRLF, traversal order, and skipped symlinks.

There is currently no in-place write mode. To replace a file, review the output
and use your editor or a script that preserves permissions and replaces the file
safely. CI runs `gloinfmt --check .` after building the compiler.
