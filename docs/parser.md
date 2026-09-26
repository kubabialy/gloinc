# Parser contract

`GloinParser` defaults to `ParseMode::Core`. `compile_source` uses this mode and
requires a successful complete parse before invoking Sema. A source file may
contain functions, constants, imports, ordinary structs, and generic struct templates. Runtime globals, nested functions, executable
file-scope statements, and deferred syntax receive parsing diagnostics.
Sema's checked-program path resolves core type identities and rejects unsupported
scalar types under SPEC-010; SPEC-013 checks contextual numeric literals and
rejects unsupported conversions.
Parsing a type name does not establish type support.

Core parsing enforces explicit binding/parameter/return annotations, modifier
order, mandatory statement semicolons, braced control-flow bodies, and comma
separators with optional final commas. Assignment is allowed only as a statement
or loop update. The [language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec#expression-grouping-spec-009) defines operator
precedence. The canonical core examples also have complete semantic and
execution checks in [SPEC-021's source fixtures](../tests/fixtures/core/README.md).

`const` and visibility are preserved in the AST. SPEC-012 evaluates pure
constant expressions in Sema and gives codegen folded values. The unchecked
backend rejects constants because it has no evaluated semantic data.
Private is the default. Semantic checking enforces public/private access across
files under [SPEC-029 module rules](modules.md).

SPEC-017 accepts `unless condition { ... }` without an `else`, and C-style
`for initializer; condition; update { ... }`. Each header component is optional;
both semicolons and the body braces are required. Missing components have null
AST pointers; a missing condition means `true`. Initializers accept local
bindings/constants or expression/assignment statements, while updates accept
expressions/assignments (including void calls), without a trailing semicolon.
Complete-header parentheses, comma updates, ranges, `break`, and `continue`
remain rejected in core mode. `defer` is allowed in a function's loop body,
but not in a for initializer or update.

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

Numeric AST nodes retain the original spelling without converting to a host
integer or double. Valid lexical spellings, including magnitudes larger than
u64 and extreme decimal exponents, can parse successfully; Sema selects the
language type and rejects out-of-range values before codegen. SPEC-013 defines
full-consumption conversion, contextual types, and signed-literal rules.

## Deferred syntax tests

`ParseMode::SyntaxOnly` explicitly permits the existing deferred grammar and
statement-fragment lists for stage-isolated tests. It is not a supported compiler
language mode and is not exposed by `compile_source`. It lets packed-struct,
generic-method, legacy array-literal, and concurrency tests inspect their stages
while core compilation rejects those constructs. Generic struct templates and
applications, and fixed arrays using `[T; N]` and `{...}`, parse in core mode.
Legacy `spawn`/`await`, missing annotations, misplaced modifiers, and malformed
lists still fail in syntax-only mode.

The syntax-only parser shares token consumption and precedence with the core.
Struct literals cannot take a control-flow body's opening brace. Generic
aggregate lookahead uses token structure rather than identifier capitalization;
type parsing splits `>>` only when consuming nested type closers and preserves
each closer's byte position. The lookahead also recognizes module-qualified
constructors and fixed-array or pointer type arguments. These checks do not
establish aggregate layout or concurrency support. Checked generic struct
specialization is a separate semantic and codegen step.

The old struct-method fixture now spells `def pub greet(self: *Person)` and the
multi-parameter generic fixture includes its missing field comma. Their feature
assertions are preserved, and negative tests reject the obsolete forms. Existing
unimplemented feature tests remain visible in the full suite.

## Ordinary structs (SPEC-024)

Core parsing accepts file-scope `def [pub|priv] struct Name { ... }`, comma-separated
`def [pub|priv] [mut] field: Type` declarations, named literals, member expressions,
and module-qualified type names/literals. Struct names can be lowercase.
Generic struct templates use `Name<T, U>` with explicit type applications;
checked specialization currently rejects methods on generic structs. Packed
definitions, field defaults, and local structs remain rejected. Semantic
checking validates fields and visibility before codegen.

## Pointers and references (SPEC-025)

Core parsing accepts recursive `*T`/`&T` annotations with optional `const` after
each pointer marker, unary address-of/dereference, and `null`. In a type annotation
`&&T` splits into two reference layers; in expressions `&&` remains logical AND.
Addressability, initialization, qualifier conversions, and contextual null types
are checked by Sema. Version 0.0.3 accepts `*T + i64` with the
[pointer-offset rules](pointer-offsets.md). No borrow-checker or unsafe-block
syntax is introduced.

## Methods (SPEC-026)

Core struct bodies accept `def [pub|priv] [static] name(...) -> Type { ... }`.
`self` is a reserved expression/parameter name, and its explicit annotation uses
ordinary pointer/reference syntax. Sema checks receiver position, containing
struct identity, mutability, visibility, and static versus instance call form.
Static calls use `Type.method(...)` or `module.Type.method(...)`; no additional
call operator is introduced. Async methods remain syntax-only/deferred.

## Function-exit defer (SPEC-027)

`defer expression;` parses a `DeferStatement`; semantic analysis requires the
expression to be an ordinary function or method call inside a function body.
The call follows the normal receiver/argument/type/visibility rules, including
checking values at the registration point. Blocks/closures and defer in for
headers are not added. Async `deferred` declarations remain separate and unsupported.
