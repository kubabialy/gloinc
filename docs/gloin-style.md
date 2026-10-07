# Gloin source style

This style follows the layout in `examples/statistics_tool.gloin` and the
declaration alignment and statement grouping in `tools/gloinfmt/`. It applies to
examples, standard modules, tools, and source fixtures. Rejection fixtures keep their
intentionally invalid tokens; formatting changes only whitespace and layout.

## Layout

- Indent with four spaces. Put an opening `{` on the declaration or control-flow
  line and align its closing `}` with that line. Keep `} else {` together.
- Put one statement on each line. A short `if` or `unless` guard with one
  statement may stay on one line. Expand longer bodies and every body with more
  than one statement.
- Put one struct field or struct-literal field on each line. A closing brace may
  share a line with a following `;`, `,`, or `)`.
- Leave one blank line between top-level functions or structs and between imports
  and declarations. Related constants may stay together. Inside a function, use
  blank lines to separate setup, validation, processing, and cleanup or output.
  Keep a declaration with its immediate validation guards and `defer` registration.
  Separate the next operation after validation, leave space around processing
  loops, and separate a final return from the work that produces its value.
  A short error-reporting call and its return may stay together inside a guard.
- Align `=` in consecutive single-line declarations at the same indentation.
  Align consecutive assignments to local variables, dereferenced pointers, or
  fields/elements of the same object. A blank line, comment, different target
  group, or multiline statement ends the alignment group. Put one space after
  `=`; do not align assignments inside one-line guards with surrounding statements.
- Put one space after commas. Do not leave trailing whitespace.
- For a call or function declaration that spans lines, put the opening `(` on
  the first line. Indent continuation arguments or parameters one level beyond
  that line, and align the closing `)` with it. Put one argument per line when
  that makes a long list clearer; short, related arguments may stay together.
  Avoid an empty line immediately after the opening `(`.
- In fixed-array types, put one space after the semicolon: `[i32; 2]`. Keep a
  short initializer on one line: `{1, 2}`. Use braces for array values and
  brackets for indexing.
- For a long boolean condition, break before `&&` or `||` and indent the
  continuation one level. Keep the opening `{` with the last condition line.
- Put explanatory comments immediately before the declaration or step they
  describe. Preserve an inline comment when it explains that statement.

```gloin
def mut head: *Column = null;
def mut tail: *Column = null;

if input.status != status.OK
    || output.status != status.OK {
    return 2;
}

def node: *Column = owner.try_alloc(Column {
    next: null,
    index: 0,
    count: 0
});

def reply: result<void> = respond(
    socket,
    memory,
    431,
    "Request Header Fields Too Large", "request head too large\n"
);
```

These fragments illustrate layout; they are not a complete program.

Keep setup and its error check together, then separate the next step:

```gloin
def mut cursor: u64         = 0;
def mut wrote: bool         = false;
def mut pending_blank: bool = false;

def opened: io.FileResult = io.File.open_read(&memory, path);
if opened.status != status.OK { return error("cannot open source file"); }

def mut file: io.File = opened.value;
defer file.close();

def read: io.ReadResult = file.read_all(&memory, 8388608);
if read.status != status.OK { return error("cannot read source file"); }

return read.value;
```

Existing blank lines express additional logical groups and are preserved as a
single blank line. Avoid padding unrelated operations into one alignment group.

## Applying the style

Build `gloinfmt` with `cmake --build build --target gloin_formatter`. Run
`build/gloinfmt --check .` from the repository root before submitting changes.
Use `build/gloinfmt FILE` to print a formatted file to standard output. The
`build/gloinfmt --write --git` command formats tracked and unignored sources
in place; review the resulting diff. The
[formatter guide](gloinfmt.md) describes its commands and limits.

The formatter handles indentation, statement separation, assignment alignment,
blank lines, and multiline continuations. These rules are implemented in Gloin
and are checked by `gloinfmt --check`; a second formatting pass produces the same
bytes. It preserves token spelling, including comments and strings, but does not
split statements or rewrap long calls. Review those choices against this guide. Keep
intentionally invalid fixture tokens intact when formatting rejection cases.
