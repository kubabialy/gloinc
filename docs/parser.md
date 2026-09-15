# Parser contract

`GloinParser` defaults to `ParseMode::Core`. `compile_source` uses this mode and
requires a successful complete parse before invoking Sema. A source file may
contain functions and constants. Runtime globals, nested functions, executable
file-scope statements, and deferred syntax receive parsing diagnostics.
Sema's checked-program path resolves core type identities and rejects unsupported
scalar types under SPEC-010; numeric compatibility/conversions remain SPEC-013.
Parsing a type name does not establish type support.

Core parsing enforces explicit binding/parameter/return annotations, modifier
order, mandatory statement semicolons, braced control-flow bodies, and comma
separators with optional final commas. Assignment is allowed only as a statement
or loop update. [SPEC.md](../SPEC.md#expression-grouping-spec-009) defines operator
precedence. The canonical core examples parse; their complete semantic and
execution acceptance remains SPEC-021.

`const` and visibility are preserved in the AST. SPEC-012 evaluates pure
constant expressions in Sema and gives codegen folded values. The unchecked
backend rejects constants because it has no evaluated semantic data.
Private is the default; public/private metadata
does not impose cross-module access restrictions in the single-file core.

## Token consumption and failure

The parser buffers tokens and removes newline trivia before parsing. Every
routine consumes its complete construct and leaves the first unconsumed token
current. Neither callers nor callees guess whether an expression stopped on its
last token. `expect` consumes a required token or reports an error at the token
that prevented completion. Owned source spans include consumed delimiters but
exclude following whitespace and comments.

`parse_checked_program()` consumes through EOF or returns failure with an empty
program. Failures stop parsing immediately, unwind contextual state, and discard
all previously parsed declarations. Subsequent entry on a failed parser returns
failure. There are no recovery loops that can retry the same token indefinitely.
This is a deliberate first-error policy; multi-error recovery is not implemented
or required for the core release. A limit of 512 nested parser entries produces a
diagnostic before excessive recursive nesting can exhaust the stack.

`parse_statement()` and `parse_expression(0)` parse individual fragments, without
claiming that they form a complete source file. Fragment callers must inspect
`has_error()` and, when they require full consumption, `at_end()`. Declaration
suffix helpers remain available for focused tests: variable/function helpers
start after `def` and modifiers, while the struct helper starts at `struct`.

Numeric spellings are retained in the AST. The current signed 64-bit/double
conversion reports overflow instead of returning partial data; full unsigned,
width, contextual typing, and range semantics remain SPEC-013.

## Deferred syntax tests

`ParseMode::SyntaxOnly` explicitly permits the existing deferred grammar and
statement-fragment lists for stage-isolated tests. It is not a supported compiler
language mode and is not exposed by `compile_source`. It lets struct, generic,
pointer, array, string, and concurrency tests continue to inspect their stages
while core compilation rejects those constructs. Legacy `spawn`/`await`, missing
annotations, misplaced modifiers, and malformed lists still fail in this mode.

The syntax-only parser shares token consumption and precedence with the core.
Struct literals cannot take a control-flow body's opening brace. Generic
aggregate lookahead uses token structure rather than identifier capitalization;
type parsing splits `>>` only when consuming nested type closers and preserves
each closer's byte position. These checks do not establish aggregate layout,
generic execution, or concurrency support.

The old struct-method fixture now spells `def pub greet(self: *Person)` and the
multi-parameter generic fixture includes its missing field comma. Their feature
assertions are preserved, and negative tests reject the obsolete forms. Existing
unimplemented feature tests remain visible in the full suite.
