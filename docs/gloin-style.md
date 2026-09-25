# Gloin source style

This style follows the layout in `examples/statistics_tool.gloin`. It applies to
examples, standard modules, and source fixtures. Rejection fixtures keep their
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
  Keep related statements together.
- Put one space after commas. Do not leave trailing whitespace. Keep adjacent
  assignments aligned only when the alignment makes a small group easier to scan.
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
```

These fragments illustrate layout; they are not a complete program.

## Applying the style

Run `python3 scripts/format-gloin.py --write` from the repository root to format
all tracked `.gloin` files, or pass paths to format selected files. Run
`python3 scripts/format-gloin.py --check` before submitting changes. The tool
checks that formatting preserves lexical tokens and retains the CRLF line endings
of the dedicated acceptance fixture. Do not repair intentionally invalid fixture
syntax while formatting it.
