# Gloin Language Spec

This document defines the intended language, including future designs. The
[versioned 0.0.1 language guide](docs/site/0.0.1/index.html) is the release
boundary. [README.md](README.md) records measured implementation status;
[SPEC-TODO.md](SPEC-TODO.md) tracks implementation and verification.

## Version 0.0.1 release profile

Apple Silicon macOS is the supported host and native target. The release includes
SPEC-001 through SPEC-030, plus SPEC-030a through SPEC-030h standard-library
expansion. It supports the in-process JIT, native object emission, and standalone
native executables on that host. Native output uses the same checked and verified
LLVM lowering as JIT execution and links the installed Gloin runtime. Linux
support is planned for 0.1.0; Windows support is not planned. Cross-compilation
and Intel macOS are outside 0.0.1.

The original SPEC-006 scalar milestone below records the initial scope decision.
Completed later specifications expanded the 0.0.1 release profile. Sections
covering generics, arrays, enums, concurrency, packed bitfields, external
packages, and other unchecked tasks remain design proposals.

## First release contract (SPEC-006)

SPEC-006 originally selected an **executable scalar core** with an in-process JIT
on **Apple Silicon macOS**, using LLVM/MLIR 21.1.6. Its original scope deferred
native output and the later library work; the 0.0.1 release profile above includes
the subsequently completed items.

| Original SPEC-006 scalar baseline | Implementation and acceptance |
| --- | --- |
| Valid UTF-8 source, the declaration/type/terminator rules below, source-aware errors, rejection of unsupported constructs | SPEC-007 through SPEC-010 |
| Local immutable/mutable variables, compile-time scalar constants, lexical scopes, definite initialization | SPEC-011/SPEC-012 |
| `bool`; `i8`, `i16`, `i32`, `i64`; `u8`, `u16`, `u32`, `u64`; `f32`, `f64`; aliases `int` and `usize`; `void` return type | SPEC-010/SPEC-013 |
| Decimal, hexadecimal, binary integer literals and decimal floating literals; checked literal compatibility | SPEC-013 |
| Top-level functions with typed value parameters and explicit return types, direct/forward/recursive calls, `def main() -> i32` | SPEC-011/SPEC-014 |
| Assignment; unary `-` and `!`; binary `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `\|\|`; parentheses and function calls | SPEC-015 |
| Boolean `if`/`else`, `unless`, `while`, C-style `for`, nested blocks, and early returns | SPEC-016/SPEC-017 |
| Verified shared lowering, in-process JIT, file-reading CLI with checking/IR inspection modes and reliable failure status | SPEC-018 through SPEC-020 |
| Complete source-file execution and expected-error fixtures for every advertised core feature | SPEC-021 |
| Installation instructions, packaging, feature-to-test matrix, and release validation | SPEC-046 |

SPEC-021 is the executable-core milestone; SPEC-046 is still required before
publishing a release. Detailed numeric overflow/conversion rules, operator
semantics, scope rules, and CLI exit conventions must be settled under their
listed tasks before their implementation is accepted. The feature list above is
fixed for this release; those tasks must not silently expand it.

The original SPEC-006 scalar baseline had no imports, standard library, text
output, strings, aggregates, pointers/references, allocation, `defer`, or
concurrency. Its minimal complete program is:

```gloin
def main() -> i32 {
    return 42;
}
```

Its observable language result is the `i32` returned by `main`. Runtime/compiler
failure must remain distinguishable from every valid `i32`, including `-1`;
SPEC-020 defines how the CLI presents results within host exit-status limits.
The later hello-world milestone requires strings and `@std` (SPEC-022/SPEC-023).
Those subsequent milestones are now implemented. The first-release scope above
records the scalar baseline; current standard output and revised CLI exit
semantics are specified under SPEC-023 below.

## Core source and syntax rules

These rules are normative, including for declarations in later feature designs.
Examples in this section specify required behavior. The maintained
[source-file acceptance matrix](tests/fixtures/core/README.md) records the
executable fixtures and expected-error cases that verify the core contract.

### Source encoding, identifiers, and trivia

Source files must be valid UTF-8. The first release restricts identifiers to
ASCII `[A-Za-z_][A-Za-z0-9_]*`, except that `_` alone is reserved and cannot bind a
name. Names are case-sensitive, with no normalization or case folding. UTF-8
text is permitted in comments and, when strings are implemented, string data;
it does not make non-ASCII identifiers legal. Invalid UTF-8, an embedded NUL
source byte, and non-ASCII identifier characters must produce a diagnostic,
never truncate the input or depend on the host locale.

Spaces, tabs, LF, and CRLF are whitespace; LF and CRLF each advance the source
line once. Newlines do not terminate statements. `//` comments run to the end
of the line or file. Block comments and a UTF-8 BOM are not accepted in the
first release. SPEC-007/SPEC-008 own encoding validation and source positions.
A lexer may expose newline tokens internally, but the parser treats them as
trivia, including inside expressions.

The core keywords are `def`, `mut`, `const`, `pub`, `priv`, `return`, `if`,
`else`, `unless`, `while`, `for`, `true`, `false`, and the core type spellings.
The later-feature words `import`, `extern`, `struct`, `enum`, `static`, `self`,
`defer`, `deferred`, `spawnable`, `run`, `packed`, `bit`, `at`, `null`, `string`,
`i128`, `u128`, `f128`, `char`, `in`, `break`, and `continue` are reserved. Legacy or
undecided words `fn`, `spawn`, `await`, `switch`, `match`, `case`, and `default`
are also reserved; using them as syntax or names must not succeed accidentally.
Endian-qualified and custom-width integer type spellings are reserved for the
later layout tasks. Unknown type names must never default to `i32`.

### Lexical literal forms (SPEC-008)

The lexer recognizes decimal integers `[0-9]+`, hexadecimal integers
`0[xX][0-9A-Fa-f]+`, and binary integers `0[bB][01]+`. A decimal floating
literal has either a decimal point with digits on both sides, an exponent, or
both: `1.25`, `1e3`, and `2.5E-2`. An exponent is `[eE][+-]?[0-9]+`. Signs
outside exponents are separate operators. Leading/trailing decimal points,
hexadecimal floats, digit separators, and numeric type suffixes are not accepted.
Malformed forms such as `0x`, `0b102`, `123abc`, and `1e+` are lexical errors,
not valid numeric prefixes followed by unrelated names. Width/range checks and
numeric conversion remain SPEC-013; the lexer preserves the original spelling.
`0..10` is three tokens, including the reserved range operator.

Quoted strings are single-line UTF-8 text. The recognized escapes are `\n`,
`\r`, `\t`, `\0`, `\\`, `\"`, and `\'`; other escapes and unescaped ASCII control
characters are errors. A character token uses single quotes and contains exactly
one Unicode scalar or one of those escapes. Well-formed character tokens are
still rejected by the core parser, as character expressions are outside the
release scope. An unterminated quote is an error; lexing resumes at its newline
or EOF. Quoted token payloads preserve escape spelling without their delimiters;
source spans include the delimiters. Escape decoding, string storage, and the
meaning of escaped NUL remain SPEC-022. The `string`/`char` type keywords have
different token kinds from quoted string/character literals.

Reserved words and punctuation receive deliberate tokens even when the feature
is unsupported. Recognizing `spawn`, `await`, `in`, `..`, or `=>` does not enable
legacy syntax: unsupported uses must produce a parser diagnostic. `int` and
`usize` are type keywords; alias resolution remains SPEC-010/SPEC-013. Signed
and unsigned integer width spellings, including `i4`, `u20`, `be_u4`, `le_i20`,
`u16_be`, and `i32_le`, are reserved type names. Their tokenization approves
neither an integer width nor an endian layout; SPEC-037 through SPEC-039 own
those decisions. A word such as `integer` or `spawn_value` remains an identifier.

### Declarations, modifiers, and explicit types

Every independent declaration starts with `def`, including constants and
future structs, fields, enums, and methods. An import is a directive, not a
`def` declaration. Function parameters and struct-literal field labels are
parts of a declaration/expression and do not take their own `def`.

Canonical forms are:

```text
local binding:      def [mut] name: Type [= expression];
constant:           def [pub|priv] const name: Type = constant_expression;
function:           def [pub|priv] name(name: Type, ...) -> ReturnType { ... }
struct:             def [pub|priv] struct Name { ... }
field:              def [pub|priv] [mut] name: Type,
later method:       def [pub|priv] [static|deferred|spawnable] name(...) -> Type { ... }
```

Square brackets in these forms mean optional syntax; `...` is explanatory,
not a language token. Visibility, when present, comes **immediately after
`def`**, before other modifiers. At most one of `pub`/`priv` is allowed;
private is the default. Visibility is allowed on top-level functions/constants
and later type/member declarations, not on local bindings or parameters.
Visibility is recorded but does not restrict calls within the single source
file of a core program. Exports/member-access checks arrive with
SPEC-024/SPEC-026/SPEC-029.
`mut` and `const` cannot be combined. Other modifier combinations must be
explicitly specified by their feature tasks before they are accepted.

Every binding, constant, field, and parameter needs a type annotation. Every
function, including a function returning `void`, needs `-> ReturnType`.
There is no variable, parameter, return-type, or generic-argument inference.
A literal may take its type from an explicit declaration, parameter, return,
or typed operand context; this does not infer a missing declaration annotation.
SPEC-013 must define compatibility and any default type for context-free
literal expressions. A typed expression cannot silently change its type to
match another annotation.

Function parameters are immutable value bindings in the core. Reassigning a
parameter requires an explicitly declared mutable local copy. Later instance
methods must spell their receiver type, for example `self: *Person`; bare
`self` is not an exception to explicit typing. No implicit second receiver is
added when lowering an explicitly declared receiver.

`def x: i32 = compute();` is an immutable runtime binding. In contrast,
`def const LIMIT: i32 = 10;` requires a compile-time initializer under the
SPEC-012 rules below. Local bindings without an initializer may only be read
after definite initialization.
The core allows functions, constants, and (since SPEC-023) `@std` imports at file scope; runtime global
variables, nested functions, and user-defined type aliases are not in scope.

### Declaration visibility and lexical scopes (SPEC-011)

All top-level function signatures are collected before any function body is
checked. A function can call any function in the same file, including itself
and functions declared later; mutual recursion is allowed. Visibility modifiers
do not change lookup within a file. There is no function overloading: a repeated
top-level name is an error even when its signature differs. Functions and
constants share the file's value namespace; constant evaluation and its
dependency rules remain SPEC-012.

Parameters and the outermost function body share one lexical scope. Every
nested block, including an `if` branch or `while` body, introduces a child scope.
Two declarations with the same name in one scope are an error, including two
parameters or a body-local binding that repeats a parameter. A child scope may
shadow an outer binding or function. Lookup uses the nearest visible declaration;
calling a local scalar that shadows a function is an error. Parameters are
immutable, and shadowing does not change the outer declaration's mutability.

Local bindings become visible after their initializer has been checked. An
initializer such as `def x: i32 = x;` refers to an outer `x`, if one exists;
otherwise it is an unresolved-name error. Local declarations are not hoisted.
Leaving a block removes its bindings from lookup; sibling blocks and different
functions do not share local bindings. Compilation invocations have independent
declarations. C-style `for` initializer lifetime follows SPEC-017 below.

Type annotations use the predefined core type registry, available throughout
the file, independently of value lookup. Built-in names and aliases cannot be
redeclared. User-defined types, their declaration collection, and recursive
aggregate rules remain deferred to SPEC-024 and SPEC-031 through SPEC-034; core compilation
rejects them explicitly.

### Variables, constants, and initialization (SPEC-012)

Every local binding has an explicit non-void type. An initializer or assignment
must have exactly that type after type resolution; there is no implicit
conversion of typed values. An assignment target must be a local variable name
in the scalar core. Parameters, functions, and constants cannot be assigned.
Assignment checks the right-hand value before marking the target initialized;
`def mut x: i32; x = x + 1;` therefore reads an uninitialized variable.

A local without an initializer has no value and cannot be read until every
path reaching that read has initialized it. Mutable locals can be assigned
repeatedly. An immutable local can be initialized by assignment only when it
is uninitialized on every incoming path; it cannot be reassigned, including
when an earlier assignment occurred on only one possible path. Assignment in
both arms of an `if` can initialize an immutable local once on each path.
A branch ending in `return` does not contribute to the state after the `if`.
Conditions are checked conservatively: literal `true`/`false` do not remove
paths from initialization analysis.

A `while` may execute zero or multiple times. Its body cannot establish
initialization after the loop. An immutable local declared outside a loop
cannot receive its first assignment inside that loop; use a mutable local or
declare the immutable local inside the body. Body-local declarations start
fresh on each iteration. Scope shadowing preserves each declaration's own
initialization state. Return-path and unreachable-source rules are defined below.

Constants require an initializer, are immutable, and have no runtime storage.
Their expression subset consists of scalar literals, previously declared
constants, parentheses, unary `-`/`!`, numeric `+ - * /` (integer `%`), numeric
comparisons, scalar equality/inequality, and boolean `&&`/`||`. Operands must
have compatible, identical types; boolean arithmetic and floating remainder
are errors. Calls, runtime variables (including immutable ones), parameters,
assignments, and aggregate/pointer expressions are not constant expressions.
Both sides of a boolean expression are type checked, but `&&`/`||` evaluate
their right operand only when needed.

Top-level constants are evaluated in source order before function bodies;
functions can use any top-level constant regardless of their position. A
constant initializer can reference only earlier visible constants, so forward
dependencies and self references without an outer binding are errors. Local
constants obey the same lexical declaration order and shadowing rules as
locals. Constants share the value namespace with functions and variables.

Constant arithmetic must be representable in its resolved type. Integer
overflow and division/remainder by zero are compile-time errors. Dividing or
reducing the signed minimum modulo `-1` is overflow. Floating
operations round to their resolved width at each operation; non-finite results
and division by zero are errors. Values, including negative zero, are retained
in the checked program and materialized at uses by codegen. SPEC-013 supplies
contextual literal typing for all supported integer and floating widths.
Runtime arithmetic follows the checked failure rules in SPEC-015 below.

### Branches and while loops (SPEC-016)

`if` and `while` require a `bool` condition; numeric truthiness is not supported.
An `if` evaluates its condition once and executes only the selected arm. An
omitted `else` does nothing when the condition is false. `else if` applies the
same rule to the next condition only when the preceding condition is false.
Execution continues after the conditional only from arms that reach their end.

A `while` evaluates its condition before each iteration, including the first.
False exits the loop without executing its body. Reaching the end of the body
repeats the complete condition, including calls and short-circuit operators.
Empty blocks, arms, and loop bodies are allowed. A true empty loop can diverge;
the compiler does not infer termination or treat it as a guaranteed return.

Blocks, branches, and loops can nest. Locals follow SPEC-011/012 scope and
initialization rules. `return` exits the enclosing function immediately, including
from nested loops; no later statement, condition, or loop backedge executes on
that path. Runtime arithmetic failure terminates execution according to SPEC-015.
Unreachable source is rejected under SPEC-014, rather than emitted after a
return. Both arms and loop bodies are checked even with constant conditions.

These rules cover `if`/`else` and `while`. SPEC-017 below defines `unless` and
C-style `for`; the selected core does not include `break` or `continue`.

### Unless and C-style for loops (SPEC-017)

`unless condition { body }` evaluates its boolean condition once and executes
the body only when false. It has no `else` clause. Its scope, initialization,
return, and unreachable-source rules match `if !condition { body }`.

`for initializer; condition; update { body }` executes the initializer once,
checks the boolean condition before each iteration, and executes the update
after each body path that reaches its end. It then repeats the complete
condition. A false condition exits without executing the body or update. A
return inside the body exits the function without executing the update.

All three header components may be omitted independently. An absent initializer
or update does nothing; an absent condition is `true`. Both semicolons and body
braces are mandatory, including in `for ;; { ... }`. There are no parentheses
around the complete header, range-based forms, comma-separated updates, `break`,
or `continue` in the core. An initializer is a local binding/constant declaration
or an expression/assignment statement. An update is an expression or assignment,
including a void call, with no trailing semicolon or declaration.

Each `for` owns a scope beginning at its initializer and ending after its body.
Initializer bindings can shadow outer names; their initializers still resolve
the prior binding. They are visible in the condition, body, and update, but not
after the loop. The body adds a nested scope, so body locals cannot be referenced
by the update. Initializer effects on existing outer bindings are guaranteed;
body/update effects cannot establish definite initialization after the loop,
because analysis conservatively includes a zero-iteration path even when the
condition is absent or literally true. A repeated assignment to an immutable
header or outer binding is rejected; mutable counters use `def mut`.

Body and update are both checked even if a constant condition or unconditional
body return prevents their execution. The update uses initialization state from
continuing body paths; if none continue, it is checked against the state before
the body. Like `while`, a `for` alone does not prove a non-void function returns.
No implicit return or divergence inference is added by omitted components.

`defer` follows SPEC-027's function-exit, LIFO contract inside these loops;
neither loop iteration nor block exit is a defer execution point.

### Functions, calls, returns, and entry points (SPEC-014)

Core functions have explicitly typed, immutable scalar value parameters. Parameter
names must be distinct and share the outermost body scope. Calls name a function
through lexical lookup, supply exactly one argument per parameter, and match its
canonical type (including signedness). Arguments evaluate once, left to right.
Literal context and aliases follow SPEC-013; typed values are never implicitly
converted. Direct, forward, and recursive calls use the collected signature.
Function values, indirect calls, default arguments, and variadic parameters are
unsupported. A non-void call's result may be discarded in an expression statement;
a void call is allowed only as an expression statement, never as a value.

`return value;` must match the enclosing non-void function's canonical result
type. A void function permits `return;` and an implicit return when execution
reaches the end of its body. It forbids `return value;`, including a void call.
Non-void functions require a value at every return and cannot reach the end of
their body. Returns outside functions are errors.

Return analysis is structural and conservative: a return ends its path; a block
continues only if its statements do; an `if` ends every path only when both arms
exist and end every path. Conditions are not constant-folded for reachability,
so `if true` still needs an alternative or a subsequent return. Loops are assumed
to execute zero times, including `while true`; a return inside a loop alone does
not satisfy a non-void function. Unconditional divergence is not inferred from
calls. Statements following a structurally unconditional return (including a
block or both arms of an `if`) are rejected as unreachable, even declarations or
empty blocks. Both branches and loop bodies are checked regardless of constant
conditions. `if`/`while` lowering follows SPEC-016; `unless`/`for` follow SPEC-017.

Executable compilation requires exactly one file-scope function named `main`
with no parameters and canonical return type `i32` (`int` is equivalent).
Visibility does not change its entry-point role. Whenever a file-scope name
`main` exists, module compilation also requires that signature; a constant named
`main` is invalid. The compiler's module API permits helper-only source without
`main`; explicit executable mode validates its presence before producing a
checked program or IR. Calling `main` uses the same rules as any other function.
Execution/CLI integration consumes this contract in SPEC-019/020.

### Numeric literals and conversions (SPEC-013)

Integer literals denote mathematical magnitudes in decimal, hexadecimal (`0x`
or `0X`), or binary (`0b` or `0B`), with the same meaning in every base. A leading
zero does not imply octal. Parsing retains the complete spelling; semantic
checking converts it only after choosing its type. Invalid suffixes and partially
consumed spellings are errors. The supported integer types are signed/unsigned
8, 16, 32, and 64 bits; `int = i32` and `usize = u64` on the selected target.
128-bit types remain deferred under SPEC-013b.

A declaration, constant, assignment target, function parameter, or return
annotation provides context for otherwise untyped literals and literal-only
arithmetic expressions. An already typed operand (a variable, constant, or call)
anchors the operand type of its arithmetic expression, including when it is on
the right. For example, `def x: i64 = 40; def y: i64 = 2 + x;` uses i64 operands.
Different typed operands never promote each other: i32 plus i64 is an error.
An outer expected type does not convert a typed operand. Comparisons choose
operand context independently of their boolean result; logical operands remain
boolean. Parentheses do not change these rules.

Without a numeric context or typed operand, integer literals default to i32 and
floating literals to f32. Integer spellings require an integer context; decimal
floating spellings require f32 or f64. Use `1.0` to express a floating value,
not `1`. Mixed integer/floating expressions and boolean/numeric conversions
are errors. Declarations still require explicit annotations.

The value of an integer literal must fit its selected type, without truncation
or wrapping. A unary minus directly applied to a numeric literal forms a signed
literal: `-128` fits i8 and `-9223372036854775808` fits i64, while their positive
magnitudes do not. Negative integer literals cannot have an unsigned type,
including `-0`. The full u64 range through `18446744073709551615` is supported.
Out-of-range values in any base are errors even in otherwise unused code or
constants. This rule concerns literals; runtime operator overflow follows
SPEC-015 below. Constant arithmetic retains SPEC-012's checked-overflow behavior at
its resolved width, including unsigned overflow and underflow.

Decimal floats are converted directly to IEEE binary32 or binary64 using
round-to-nearest, ties-to-even, without first rounding through a host double.
Inexact finite rounding and representable subnormals are accepted. Overflow,
non-finite values, and a nonzero literal rounding to zero are errors. Zero,
including negative zero, is preserved. Constant floating arithmetic rounds at
each operation to the same resolved width; finite underflow during arithmetic
may round to zero, as distinct from loss of a nonzero literal at conversion.

The first release has no numeric cast syntax. Function-like casts (`i32(x)`),
`as` casts, implicit widening/narrowing of typed values, signedness conversion,
and integer/float conversion are rejected. No cast silently truncates, wraps,
saturates, or reinterprets bits. Explicit conversion facilities require a future
specification decision; the fixed first-release feature list is unchanged.
SPEC-030c later adds named runtime library conversions below, without cast syntax,
implicit promotions, or conversion of typed operands by context.

### Built-in type spellings

`int` is an exact alias of `i32`, independent of the host. `usize` is an exact
alias of the target's pointer-width unsigned integer (`u64` on the selected
arm64 target); it is not a separate nominal type. Aliases do not permit implicit
conversions to other widths. Built-in names and aliases cannot be redefined.
`void` is allowed only as a function return type, never as a value binding or
parameter. `bool` is distinct from every integer type; conditions require
`bool`, with no numeric truthiness.

The language spelling for the later pointer-plus-length text type is **`string`**.
`String` is not a built-in name or alias. A backend IR type named `String` is
an implementation detail; it cannot create a language-visible type. A later
user-defined `String` would be an ordinary distinct type. String layout and
ownership remain SPEC-022; this naming decision does not add strings to the
first release. `i128`, `u128`, `f128`, `char`, custom-width integers, and
endian-qualified types are outside the initial scalar set and must be rejected
until their contracts and implementations are added.

### Terminators and delimiters

A declaration of a binding/constant, assignment, expression statement,
`return`, later `defer`, or import directive ends in `;`. A newline cannot
replace it, even immediately before `}`. A function/type definition or a
braced control-flow statement has no trailing `;`. Empty statements are not
part of the core. Parameters, call arguments, and later struct fields/literal
entries are separated by commas. A final comma is permitted in those lists;
struct field declarations use commas rather than statement semicolons.

Braces are mandatory for control-flow bodies; condition parentheses are
optional expression grouping. In `if ready { ... }`, the brace starts the
body, never a literal of a type named `ready`. SPEC-009 must preserve this
rule when aggregate expressions are implemented.

A C-style loop has the shape
`for def mut i: i32 = 0; i < 3; i = i + 1 { ... }`: two header semicolons
and no semicolon after its update. SPEC-017 defines omitted components and
loop-variable scope. Range-loop syntax remains deferred under SPEC-036.
Assignment is a statement (also allowed in a loop update), not a value-producing
expression: chained assignments and assignments inside conditions are rejected.

### Expression operators and arithmetic failure (SPEC-015)

Binary operands must have the same canonical type; aliases and literal contexts
follow SPEC-013. No operator implicitly converts a typed operand.

| Operators | Accepted operands | Result |
| --- | --- | --- |
| Prefix `-` | Signed integers, f32, f64 | Operand type |
| Prefix `!` | bool | bool |
| `+`, `-`, `*`, `/` | Matching integers or matching floats | Operand type |
| `%` | Matching integers | Operand type |
| `==`, `!=` | Matching scalar types, including bool | bool |
| `<`, `<=`, `>`, `>=` | Matching numeric types | bool |
| `&&`, `\|\|` | bool | bool |

Operands evaluate once, left to right, including nested expressions and call
arguments. `&&` evaluates its right operand only when the left is true; `||`
evaluates it only when the left is false. Both operands must be well typed even
when evaluation skips one. Boolean arithmetic/ordering, unsigned unary minus,
floating remainder, unary plus, bitwise/shift/compound operators, and assignment
expressions are errors. Prefix minus directly on a literal retains SPEC-013's
signed-literal range rule, including the signed minimum.

Integer addition, subtraction, multiplication, and negation must fit the declared
width and signedness; overflow and unsigned underflow fail. Signed division
truncates toward zero; remainder has the dividend's sign (or is zero). Unsigned
division/remainder use unsigned magnitudes. Zero divisors fail. Signed minimum
with divisor `-1` fails for both division and remainder, matching constants.
There is no implicit wrapping, saturation, or undefined arithmetic behavior.

Floating operations use IEEE binary32/binary64 with nearest/ties-even rounding
at each operation, without reassociation or fused multiply-add. Finite subnormal
results and underflow to signed zero are permitted. Division by either sign of
zero and any non-finite arithmetic result fail; NaN/infinity are not produced by
valid core programs. Comparisons use numerical ordering, including `-0.0 == 0.0`.
Unary minus flips the sign, including zero. The selected execution environment
must retain the default rounding mode and gradual underflow.

Constant failures are semantic diagnostics. Runtime failures terminate execution
via an explicit trap; they never become an integer program result, including
`-1`. Dangerous integer division/remainder is guarded before the operation.
The external runner reports a failed execution. SPEC-019 defines process-terminating
traps for in-process execution; CLI presentation follows SPEC-020. Runtime expressions are not required to be
constant-folded or rejected at compile time, even when written with literals.

### Expression grouping (SPEC-009)

The core parser uses the following precedence, from weakest to strongest:

| Operators | Grouping |
| --- | --- |
| `\|\|` | Left associative |
| `&&` | Left associative |
| `==`, `!=` | Left associative |
| `<`, `<=`, `>`, `>=` | Left associative |
| `+`, `-` | Left associative |
| `*`, `/`, `%` | Left associative |
| Prefix `-`, `!` | Nest from right to left |
| Function call `(...)` | Left associative |

Parentheses override this order. For example, `-f(1) * 2` groups as
`(-f(1)) * 2`, and `a - b - c` as `(a - b) - c`. Comparison chains are
ordinary grouped binary expressions, not mathematical chained comparisons;
SPEC-013/SPEC-015 check whether the resulting operand types are compatible.
Identifier capitalization never changes expression grammar.

Assignment is excluded from this table. Its left side must be an assignable
form, and its right side is an expression that cannot contain another assignment.
Mutability, scope, and type compatibility are subsequent semantic checks.
C-style loop headers accept independently omitted components under SPEC-017;
both header semicolons and body braces remain required.

Ordinary struct literals and member access are supported by SPEC-024; indexing
remains deferred. Postfix member/index operations have the same precedence as calls. An unparenthesized condition or
loop update always gives its following brace to the control-flow body. Generic
struct-literal recognition requires type arguments followed by a literal brace,
without any capitalization heuristic; generic language acceptance remains
SPEC-031/SPEC-032.

### Verified IR and lowering (SPEC-018)

Successful source compilation returns a verified, owned module. The default
inspection form retains high-level IR; LLVM output runs one shared pipeline
also used by execution consumers. Invalid IR stops before conversion. Every
conversion pass verifies its output, followed by a final legality and verifier
check before LLVM export or execution. A failed stage returns diagnostics and
no partial module.

The lowering input boundary accepts registered operations in `func`, `arith`,
`cf`, `scf`, `memref`, and `llvm`, within one root `builtin.module`. Nested modules are rejected. Temporary
`builtin.unrealized_conversion_cast` operations may be reconciled during
conversion. Types must be builtin or LLVM types, including nested types in
signatures and attributes. These are compiler IR capabilities, not extra source
language features; an operation in an accepted dialect still fails if the
pipeline cannot convert it.

The pipeline converts SCF to control flow, control flow to LLVM, arithmetic to
LLVM, functions to LLVM, and memrefs to LLVM, then reconciles conversion casts.
The final boundary permits only the root builtin module and registered LLVM
operations with LLVM-compatible types, including types nested in signatures,
block arguments, and attributes. LLVM constant operations may retain index-typed
integer literal attributes supported by the exporter; their SSA result must
have a concrete LLVM-compatible type. No Gloin/custom operation or type, high-level
operation, or unrealized conversion cast may remain. LLVM's internal widths and
aggregate descriptors do not add source-language types.

Verification/lowering errors retain available source file, line, and column.
Owned source text supplies byte spans when available; imported IR may provide
only file/line/column, and synthetic unknown locations remain unknown. Lowering
consumes module ownership and destroys partial IR on failure. A consumer that
retains the high-level form must clone it explicitly; the MLIR context outlives
all modules. This task adds no optimization-dependent language behavior. JIT
translation registration, entry ABI, and execution-result handling follow
SPEC-019; CLI commands and exit conventions follow SPEC-020.

### In-process JIT execution (SPEC-019)

The JIT consumes verified IR through SPEC-018's shared lowering pipeline using
an owned clone; it leaves the caller's module unchanged. Both the builtin and
LLVM dialect translation interfaces are required. LLVM export is verified before
native compilation. The selected release executes on the native Apple Silicon
macOS host; it does not introduce cross-compilation or native output files.

The entry must be a defined, non-variadic `main() -> i32`, without parameters,
using the C calling convention and external/internal/private emitted linkage.
Source visibility does not change its role. Missing entries, invalid signatures,
and unsupported external function/global declarations fail before invocation.
The core JIT does not resolve user declarations against ambient host symbols.
Helper functions also require the C calling convention and emitted
external/internal/private linkage.
A compiler-generated adapter with collision-free names uses MLIR's packed calling interface;
user identifiers such as `_mlir_main` remain valid.

A successful execution contains exactly one signed 32-bit value. Every value,
including zero, -1, and both i32 bounds, is a valid result. Verification, lowering,
entry validation, engine creation, and invocation errors contain diagnostics and
no value; failure has no integer sentinel. Diagnostics preserve available source
locations and are rendered by the caller. The JIT emits no routine debug output.
Repeated calls create independent engines and release engine/module resources
before returning. The caller keeps the MLIR context alive during execution.

Runtime arithmetic failure retains SPEC-015's explicit trap semantics. It
terminates the calling process and does not return an `ExecutionResult` or an i32.
This release provides no in-process signal recovery, rollback, timeout, or defer
cleanup after a trap. Tests isolate intentional traps in subprocesses and require
the trap signal, so compiler/setup failure cannot satisfy those tests. Programs
and embedders must retain the specified floating-point environment. CLI status
and presentation follow SPEC-020.

### Command-line interface (SPEC-020)

`gloinc [--run | --check | --emit-ir | --emit-llvm] [--] FILE` accepts exactly
one source file. Run is the default. Options may precede or follow the filename;
`--` ends option recognition. A mode may be specified only once, even if repeated
with the same spelling. Unknown options, conflicting modes, missing/extra input
files, and misplaced help/version options are usage errors. `--help`/`-h` and
`--version`/`-V` are standalone commands. The scalar-core release version is
`0.0.1`; the command also reports the configured LLVM/MLIR version. Publication
and release validation are recorded separately under SPEC-046.

The input must be a readable regular file (symlinks to regular files are allowed).
No extension is required. The complete bytes are read and passed to the shared
compiler; lexical encoding rules still apply. There is no stdin special case,
multi-file compilation, user program argument list, or native output file mode.
Names beginning with a dash require `--` or an explicit path such as `./-file`.

Run uses executable-mode compilation and SPEC-019's JIT. As of SPEC-023, main's
return value sets the process exit status (the low eight bits of the i32); it is
never automatically printed. The JIT API preserves the full i32 separately.
Only explicit output calls write to stdout during execution, regardless of imports.
Compiler, file/output I/O,
and reported JIT failures write diagnostics to stderr and exit 1. Compilation
and JIT failures emit no partial IR or result; stdout write failures can occur
after some bytes have been written. Usage errors write a diagnostic and usage
to stderr and exit 2.
Arithmetic traps retain process-signal termination, with no successful result;
no signal handler or recovery layer is added by the CLI.

`--check` runs shared module-mode compilation and high-level IR verification,
producing no output on success. `--emit-ir` prints verified high-level MLIR;
`--emit-llvm` prints verified LLVM-dialect MLIR through SPEC-018's shared pipeline.
Both include available source locations. These modes do not execute code and
permit helper-only modules without `main`; a present invalid `main` is still
rejected. Successful inspection, help, and version commands exit 0. Diagnostics
are rendered once at the CLI boundary, and no token/debug output is emitted.
Full source-file acceptance and packaging remain SPEC-021/SPEC-046.

### Canonical core examples

A complete typed program with a forward call and a constant returns `42`:

```gloin
def const OFFSET: int = 2;

def main() -> i32 {
    def input: i32 = 40;
    return add(input, OFFSET);
}

def add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

A complete mutation/control-flow program returns `3`:

```gloin
def main() -> i32 {
    def mut count: i32 = 0;
    def enabled: bool = true;
    if enabled {
        for def mut i: i32 = 0; i < 3; i = i + 1 {
            count = count + 1;
        }
    } else {
        return -1;
    }
    unless count == 3 {
        return -1;
    }
    while count > 3 {
        count = count - 1;
    }
    return count;
}
```

These independent fragments require diagnostics (SPEC-007 through SPEC-014):

| Invalid source | Required reason |
| --- | --- |
| `const LIMIT: i32 = 3;` | Missing `def`. |
| `pub def work() -> void {}` | Visibility must follow `def`. |
| `def const mut n: i32 = 0;` | Conflicting modifiers. |
| `def n = 1;` | Missing binding type. |
| `def work(x) -> i32 { return x; }` | Missing parameter type. |
| `def work() { return; }` | Missing explicit return type. |
| `def n: i32 = 1` | Missing semicolon, including at end of file. |
| `def café: i32 = 1;` | Non-ASCII identifier. |
| `def _: i32 = 1;` | Reserved name. |
| `def n: Mystery = 1;` | Unknown type; no `i32` fallback. |
| `def main() -> i32 { if 1 { return 0; } return 1; }` | Condition is not `bool`. |

## Deferred features and implementation-only syntax

No implementation-only syntax is grandfathered into the first release.
Tokenization, parsing, AST/IR printing, and existing unit-test expectations are
not language acceptance. The first release must diagnose unsupported features
before execution, using SPEC-007/SPEC-009/SPEC-010 and the SPEC-021 negative
fixtures. Feature tasks below remain open even if an initial rejection is added.
The full test suite continues reporting failures; scope decisions do not disable
tests or turn unimplemented features into expected successes.

| Syntax or capability outside the core | Disposition and owning tasks |
| --- | --- |
| Bare `const`, `pub def`, bare `struct`, untyped declarations/returns, implicit `self`, omitted semicolons | Reject; use canonical forms above (SPEC-008/SPEC-009/SPEC-012/SPEC-014/SPEC-026). |
| `fn`, `extern`, `switch`/`match`/`case`/`default`, `=>`, `?`, character literals | No accepted core extension. Reject under SPEC-008/SPEC-009/SPEC-015; any future syntax requires a separate contract before implementation. |
| Compound assignment, bitwise operators, shifts, unary `+`, assignment expressions | Reject for the first release (SPEC-015); integer remainder `%` is included, floating remainder is not. |
| `i128`, `u128`, `f128` | Deferred numeric extension (SPEC-013b); core type resolution rejects them (SPEC-010/SPEC-013). |
| `@std`, standard I/O and conversions | `@std`, `std.print(string)` and `std.println(string)` are implemented by SPEC-023. SPEC-030 adds bounded input and explicit i32 conversions; `string` is implemented by SPEC-022. |
| Ordinary structs | Implemented by SPEC-024: named value types, checked fields, and target layout. |
| Pointers and references | Implemented by SPEC-025 with manual lifetimes, typed access, mutability checks, and null traps. |
| Instance/static methods | Implemented for ordinary structs (SPEC-026); explicit typed `self`, checked receivers, and file/module visibility. |
| `defer` | Function-exit LIFO calls with registration-time argument capture (SPEC-027). |
| Arenas | Initialized-value allocation through `@arena` / `arena.GeneralArena` (SPEC-028). |
| Local modules | SPEC-029: file-relative imports and explicit exports. `#package` imports remain deferred (SPEC-044). |
| Generic types/functions, enums, `Result` | Deferred (SPEC-031 through SPEC-034); capitalization must not decide grammar. |
| `[i32; 3]`, `u8[1024]`, array literals, indexing/slicing | Deferred syntax/layout choice (SPEC-035); neither array spelling is approved for the core. |
| `for ... in ...`, `..`, `break`, `continue` | Deferred (SPEC-036); lex as reserved syntax and diagnose unsupported use. |
| Endian spellings, custom-width integers, packed layouts | Deferred (SPEC-037 through SPEC-039). |
| `deferred`, `spawnable`, `run`, `Deferred`, `Spawn`, joins | Deferred contract/runtime (SPEC-040 through SPEC-043). |
| Implementation-only `spawn` and `await` | Reject for the first release; SPEC-040 decides whether to retain any later compatibility syntax. |
| Native object/executable emission | Implemented for Apple Silicon macOS in SPEC-045; additional targets remain deferred. |

## Later language design

The remaining sections describe intended later features and conceptual examples,
not additional first-release requirements. Their task IDs are in the table
above. Core spelling rules still apply. Unsettled APIs, memory guarantees,
packed layouts, and concurrency types must be resolved in their feature tasks
before these examples become normative executable fixtures. In particular,
formatted/numeric printing and the named HTTP package are not established APIs.

All Gloin programs are written in UTF-8. Gloin is by design explicit, and does not have implicit typing or type inference.
The reason for this is that Gloin is designed to be a simple, safe, fast language but most importantly transparent.
It will not hide complexity from you, but will instead provide you with the tools to understand it.

### Ordinary structs (SPEC-024)

Ordinary structs are nominal value types declared at file scope. All struct names
and fields are collected before function signatures and bodies, allowing forward
references and nested records. Each field needs an explicit non-void type. Fields
may use builtin value types, strings, or other ordinary structs. Duplicate names,
unknown types, direct or indirect by-value recursion, and conflicts with builtin
types or other file declarations are errors. Empty structs are permitted.

```gloin
def struct Point {
    def pub mut x: i32,
    def pub mut y: i32,
}

def main() -> i32 {
    def mut p: Point = Point { y: 10, x: 30 };
    def before: Point = p;
    p.x = p.x + 2;
    return p.x + before.y;
}
```

A literal names every field exactly once. Missing, duplicate, unknown, private,
or mistyped fields are errors; there are no implicit field defaults. Initializers
evaluate once in written order, independently of declaration order. The compiler
places each value at its declared field index. Type identity includes the owning
file/module: matching field layouts do not allow conversion between named types.
Structs support value parameters, returns, assignment, and copies. Comparisons,
arithmetic on whole structs, and compile-time struct constants are not supported.

A struct binding must be initialized as a whole before reading or assigning any
field. Whole-value assignment follows the existing definite-initialization rules.
Field writes require a mutable local root and `def mut` on every field traversed,
including the final field. Parameters, immutable roots/fields, and temporary
receivers cannot be assigned through. Replacing a whole mutable struct value is
allowed even if some of its fields are immutable. Nested reads and reads from
function results evaluate the receiver once.

Private is the default for structs and fields. Private fields are accessible
within the defining file; cross-module reads, writes, and literal initialization
require public fields. `import "@records"` can expose a `def pub struct Point` as
`records.Point`, including in annotations and literals. Module type identities
and private helpers remain isolated from application declarations.

Ordinary fields retain declaration order. LLVM's target data layout determines
field offsets, ABI alignment, allocation size, and padding, including nested
structs and strings. Generated modules carry the native target triple and data
layout used by lowering/JIT; pointer sizes are not inferred from C++ `sizeof`
or summed field widths. Padding bytes have no language-visible value. The
supported execution target remains Apple Silicon macOS; layout tests on synthetic
32-bit layouts do not add cross-compilation support.

Packed structs, generics, local struct declarations, field
default initializers, and field-by-field initialization remain unsupported.
Unparenthesized control-flow headers reserve their opening brace for the body;
use grouping or a call argument when a struct literal is needed in a condition.

### Standard output (SPEC-023)

`import "@std";` is a file-scope declaration available throughout the file,
including functions preceding the import. Duplicate imports and file-scope
declarations named `std` conflict. Local bindings may shadow `std`; a shadowed
name cannot be used to call standard members. Import paths decode string escapes.
`@name` loads a lowercase `name.gloin` file from the selected standard-library
directory. Names match `[a-z][a-z0-9_]*`; local paths follow SPEC-029 below.
`stdlib/std.gloin` and `stdlib/arena.gloin` are shipped; adding `math.gloin` or `io.gloin` requires no
compiler changes. Declared `pub` functions and structs are accessible as module members.
Module functions/constants/types use an isolated scope and cannot see application
declarations. Private helpers are callable inside their own module. SPEC-029 adds imports between files, local paths, and exported constants.
Package paths remain deferred and produce diagnostics.

Module files are parsed and type-checked on every compilation, retaining their
own diagnostic locations. Missing/unreadable files and missing/private members
are errors, with no built-in fallback. CLI `--stdlib-dir DIR` selects an explicit
directory; default lookup uses the executable's adjacent `stdlib/` directory or
the installed `../share/gloinc/stdlib/` directory. The compiler API accepts an
optional directory argument and otherwise uses its configured build library.

`print` and `println` are ordinary functions defined in `std.gloin`. Standard
module bodies may call the native primitive `__write_stdout(string) -> void`;
application source cannot access it directly. The primitive writes bytes and
flushes stdout, while `println` implements the newline in Gloin.

`std.print(value: string) -> void` writes exactly the string's byte length to
stdout. `std.println(value: string) -> void` writes those bytes followed by one
LF byte. Both preserve UTF-8, embedded NULs, and literal percent signs; neither
performs formatting or numeric conversion. Arguments evaluate once at the call
site. Missing members, wrong arity/types, missing imports, and using output calls
as values are errors. Output is flushed per call; a write failure is reported as
an execution error. Bytes already written cannot be rolled back on later failure.

The CLI returns main's result as its host exit status (low eight bits); output
calls and imports do not change the returned value. JitRunner continues returning
the complete i32 separately. Return values are never printed automatically.
Check and IR emission modes never execute output calls.

The hello-world program uses SPEC-022/SPEC-023; the scalar entry-point
example above has no imports:

```gloin
import "@std";

def main() -> i32 {
    std.println("Hello World!");
    return 0;
}
```

### Standard input and integer conversions (SPEC-030)

The public functions and result structs below are ordinary declarations in
`stdlib/std.gloin`, which imports `@arena`. `print` and `println` retain their
single-string signatures; numeric formatting is always explicit.

| API | Result |
| --- | --- |
| `std.input(memory: &arena.GeneralArena, max_bytes: u64)` | `std.InputResult { status: i32, value: string }` |
| `std.to_int(value: string)` | `std.IntResult { status: i32, value: i32 }` |
| `std.to_string(memory: &arena.GeneralArena, value: i32)` | `string` |

Result fields are public and immutable. The status constants exported by `@std`
are `OK = 0`, `END = 1`, `INVALID = 2`, `OVERFLOW = 3`, `IO_ERROR = 4`,
`TOO_LONG = 5`, and `NO_MEMORY = 6`. These are i32 constants, not enums or generic
`Result` values; those type features retain their later SPEC tasks.

The caller supplies the owning arena explicitly. Successful input and formatted
strings are counted, non-owning views over its bytes and become invalid on
reset/free. Copying, returning, storing, or deferring a view does not extend its
lifetime or copy bytes. There is no implicit cleanup or per-string free. Register
arena cleanup before deferred uses so LIFO cleanup keeps the arena alive.

`input` attempts `max_bytes + 1` zero-initialized bytes before reading stdin.
Unrepresentable size or allocation failure returns `NO_MEMORY`, an empty value,
and consumes no input. Every attempted buffer remains in the arena until reset
or free, including on EOF/error. Callers may reset after consuming a line.
Input consumes one line through LF or EOF, removing LF and one immediately
preceding CR. Other CR bytes, embedded NULs, UTF-8, and non-UTF-8 bytes are preserved.
The byte limit counts payload after line-ending removal, not Unicode characters.
Runtime strings therefore do not guarantee UTF-8; source syntax still does.

An empty line succeeds with `OK` and an empty value. EOF before any new line byte
returns `END`; a final unterminated nonempty line succeeds once. A zero limit
accepts only empty payload. Oversized input is drained through LF/EOF and returns
`TOO_LONG`; the next call starts on the next line. Read failure returns `IO_ERROR`,
even while draining. Every non-OK input result has the static empty string value,
never a partial line. Consumed input is not rolled back. Input itself prints
nothing; output calls flush prompts before reading. Callers explicitly choose
how statuses affect control flow and return values.

`to_int` accepts the complete counted byte sequence matching `[+-]?[0-9]+`, with
ASCII digits, optional sign and leading zeroes, and range
`[-2147483648, 2147483647]`. It allocates nothing. It neither trims whitespace nor
accepts radix prefixes, separators, fractions, exponents, or NUL terminators.
Malformed syntax returns `INVALID`; valid decimal text outside the range returns
`OVERFLOW`. Malformed syntax takes precedence if both occur. Failures always
return value zero; successful zero is distinguished by `OK`.

`to_string` allocates 12 zeroed arena bytes and returns locale-independent decimal
ASCII with only necessary digits and a minus for negative values. Zero is `"0"`;
minimum i32 is supported without signed overflow. The buffer has a trailing NUL
excluded from the string length. Allocation failure traps without unwinding,
matching `GeneralArena.alloc`; the function returns a string, not an error result.
No conversion/output function prints an implicit return value. Wider integers,
floats, booleans, interpolation, and printf-style arguments are not supported by
these i32 conversion functions. General exceptions/generic results remain deferred.

Variable-length library buffers use `GeneralArena.alloc_bytes(size: u64) -> *u8`
and `try_alloc_bytes(size: u64) -> *u8`. They request alignment-1 storage and zero
all requested bytes before exposing it, including reused storage after reset.
The first traps on failure; the second returns null. Zero-size success returns a
non-null address but does not grant any bytes to access. Handle lifetime, aliasing,
growth, and reset/free rules match SPEC-028. These methods supplement initialized
value allocation; no pointer arithmetic or array indexing is introduced.

Private `@std` primitives lower to these native C signatures:

- `i32 gloin_std_parse_i32(const char *bytes, u64 length, i32 *value)`
- `void gloin_std_format_i32(i32 value, char *bytes, u64 *length)` (12-byte destination)
- `i32 gloin_std_input(char *bytes, u64 limit, u64 *length)` (`limit + 1`-byte destination)

The byte-view primitive builds the existing string descriptor, rejecting null
with nonzero length; it does not copy data or prove pointer bounds/lifetimes.
Only canonical `@std` source may invoke these conversion/input/view primitives.
The `@arena` zeroing primitive uses `void gloin_arena_zero_bytes(void *, u64)`
after successful allocation. JIT execution validates exact external signatures,
calling conventions, and linkage before registering native routines. Native
aggregate-return ABI assumptions are avoided through scalar/output-pointer calls.
Application function names are emitted separately from native symbols. The shared
runtime exports these routines and the byte-output ABI for external LLVM execution;
external byte-output failure aborts, while the compiler JIT reports its existing
execution error. See [the standard-library guide](docs/standard-library.md) for
runnable programs and ownership examples.

### Byte-string library (SPEC-030a)

`import "@strings";` loads ordinary source in `stdlib/strings.gloin`. All offsets,
lengths, and ordering refer to unsigned bytes, not Unicode characters. NUL and
invalid UTF-8 are ordinary data; no operation depends on a terminator or locale.
Borrowed views require their source storage to remain live and unchanged.

`@status` is a dependency-free module defining `OK=0`, `END=1`, `INVALID=2`,
`OVERFLOW=3`, `IO_ERROR=4`, `TOO_LONG=5`, `NO_MEMORY=6`, and `OUT_OF_RANGE=7`
as i32 constants. Existing `std` constants remain compatible aliases. Concrete
`strings.ByteResult { status: i32, value: u8 }`,
`strings.StringResult { status: i32, value: string }`, and
`strings.FindResult { found: bool, offset: u64 }` expose outcomes explicitly.
Failed byte/string results contain zero/static empty text; an absent match has
`found=false, offset=0`. Callers must inspect status/found, not the payload alone.

| Function | Contract | Worst-case time; allocation |
| --- | --- | --- |
| `byte_length(text) -> u64` | Descriptor length | O(1); none |
| `is_empty(text) -> bool` | Length is zero | O(1); none |
| `equal(a, b) -> bool` | Exact counted-byte equality | O(min(n,m)); none |
| `compare(a, b) -> i32` | Unsigned lexicographic order, exactly -1/0/1; shorter equal prefix sorts first | O(min(n,m)); none |
| `byte_at(text, index: u64) -> ByteResult` | OK iff index < length, otherwise OUT_OF_RANGE | O(1); none |
| `slice_bytes(text, start: u64, length: u64) -> StringResult` | Borrowed view; require start <= size and length <= size-start, otherwise OUT_OF_RANGE | O(1); none |
| `starts_with(text, prefix) -> bool` | Empty prefix always matches | O(m); none |
| `ends_with(text, suffix) -> bool` | Empty suffix always matches | O(m); none |
| `find(text, needle) -> FindResult` | First match; empty needle matches offset zero | O(1+n*m); none |
| `contains(text, needle) -> bool` | Whether find succeeds | O(1+n*m); none |
| `trim_start_ascii(text) -> string` | Remove leading ASCII whitespace; borrowed view | O(n); none |
| `trim_end_ascii(text) -> string` | Remove trailing ASCII whitespace; borrowed view | O(n); none |
| `trim_ascii(text) -> string` | Remove both ends; borrowed view | O(n); none |
| `copy(memory: &arena.GeneralArena, text) -> StringResult` | Independent arena bytes on OK, NO_MEMORY on allocation failure | O(n) byte work plus arena allocation; n requested bytes |

Here n is the first text's byte length and m the second's. Constant overhead is
implicit in O(n) bounds. Whitespace is exactly bytes 9–13 and 32. Slicing may cut
a UTF-8 sequence. Empty slices at the end are valid; bounds checks must not add
start and length before validating them. Search is a simple bounded scan, not a
linear-time promise. Comparison/equality are not constant-time security APIs.

Empty copy returns OK with static empty text, without inspecting/allocating from
the arena. Nonempty copy requires a live arena, requests exactly n initialized
bytes, and copies the payload without a terminator. Arena block overhead and
retained capacity follow SPEC-028. Failure returns static empty text and does
not change the source. Copying into the source's arena is allowed, but resetting
that arena invalidates both; use a different arena for independent lifetime.
Successful destination bytes remain valid until destination reset/free.

Only canonical `@strings` source can use its private descriptor-length, checked
byte-load, checked borrowed-slice, and copy primitives. Even private byte/slice
access traps on invalid bounds before memory access. Copy lowers to
`void gloin_strings_copy(const char *source, u64 length, char *destination)`;
the caller supplies nonoverlapping storage of at least length bytes. Zero length
does not access either pointer. The JIT validates and registers its exact native
ABI, and the installed shared runtime exports it. No application pointer
arithmetic, casts, generic operations, or string operator changes are introduced.
See [the string API guide](docs/strings.md) for examples and ownership costs.

### String traversal and construction (SPEC-030b)

These APIs extend ordinary `@strings` source. Strings and delimiters are counted
bytes; NUL and invalid UTF-8 remain data. All new public functions/methods document
examples, failures, ownership, worst-case time, and allocation costs at their
definitions and in [the text construction guide](docs/text-construction.md).

`SplitCursor.create(text, delimiter) -> SplitCursorResult { status: i32,
value: SplitCursor }` borrows both arguments without allocating. An empty delimiter
returns INVALID; otherwise OK. `next(self: &SplitCursor) -> StringResult` returns
each borrowed token with OK and finally repeated END/empty text. Splitting uses
leftmost nonoverlapping delimiters, preserving leading, adjacent, and trailing
empty tokens. Empty input yields one empty token. Invalid cursors return INVALID
from next. Each call scans at most the remaining input times delimiter length;
complete traversal is O(1+n*m), with O(1) auxiliary space and no allocation.

`LineCursor.create(text) -> LineCursor` borrows input without allocating.
`next(self: &LineCursor) -> StringResult` returns lines with OK, removes LF and
one immediately preceding CR, and preserves a final unterminated line including
any standalone CR. Empty input yields no lines; terminal LF adds no extra line.
END/empty text repeats after exhaustion. Complete traversal is O(n) byte work,
O(1) auxiliary space, and no allocation. Cursor constructors take O(1). Copying
either cursor copies its position independently, but never copies/extends the
lifetime of its borrowed bytes. Previously returned views remain valid while
the original bytes remain live and unchanged.

The following functions return `StringResult` and require an explicit u64 output
limit: `concat(memory, a, b, max_bytes)`, `repeat(memory, text, count: u64,
max_bytes)`, `replace_all(memory, text, needle, replacement, max_bytes)`,
`lower_ascii(memory, text, max_bytes)`, and `upper_ascii(memory, text, max_bytes)`.
Here memory is `&arena.GeneralArena` and text arguments are strings. Each
nonempty successful result is an independent allocation of exactly the output
byte count, with no terminator; lifetime ends on destination reset/free.
Empty output returns OK/static empty text without touching the arena.
NO_MEMORY reports allocation failure; TOO_LONG reports exceeding max_bytes,
including an output count that cannot fit u64. Checks avoid arithmetic overflow
and precede allocation/writes. Any error result contains static empty text.

Repeat with zero count or empty input is empty, even with a huge other argument.
Replacement rejects an empty needle with INVALID before other checks. Matches
are leftmost, nonoverlapping, and drawn only from the original input; replacement
bytes are not searched again. An empty replacement deletes matches. Size checks
apply to final output, so shrinking/deleting a large input can succeed under a
small limit. ASCII case conversion changes only A–Z/a–z and always copies a
nonempty result, even if unchanged. All operations leave their inputs unchanged.
Concat/repeat take O(1+output size) byte work; case conversion O(n); replacement
O(1+n*m+output size), using a sizing pass followed by a filling pass. All use
O(1) auxiliary space, plus output and arena overhead. Arena allocation cost is
additional as in SPEC-028. Nonempty output requires a live destination arena.

`StringBuilder.create(memory, capacity: u64) -> BuilderResult { status: i32,
value: StringBuilder }` creates a fixed-capacity builder or returns NO_MEMORY
with an invalid handle. It allocates one initialized state object first, then
capacity zeroed bytes if capacity is nonzero. Buffer allocation failure retains
the state object's arena space until reset/free; there is no rollback. A zero
capacity builder still allocates state. Construction costs O(1+capacity) byte
work plus up to two arena allocations. Builder copies alias the complete state,
including length. Reset/free of the owning arena invalidates every alias.

- `append(self: &StringBuilder, text) -> i32`: OK on complete append, TOO_LONG if
  insufficient remaining capacity. Checks precede writes; failure leaves length
  and contents unchanged. O(1+text size), no allocation; empty append succeeds.
- `append_byte(self: &StringBuilder, byte: u8) -> i32`: append one arbitrary byte
  or return TOO_LONG without mutation. O(1), no allocation.
- `clear(self: &StringBuilder) -> void`: set shared length to zero, retaining
  storage. O(1), no allocation, no secure erasure of previous bytes.
- `byte_length(self: &const StringBuilder) -> u64` and
  `capacity(self: &const StringBuilder) -> u64`: O(1), no allocation.
- `to_string(self: &const StringBuilder, memory) -> StringResult`: independent
  snapshot under the existing copy contract; O(length) byte work plus allocation,
  exactly length requested bytes (none for empty). Source stays unchanged on
  allocation failure. A snapshot survives builder reuse; surviving source arena
  reset/free requires a different destination arena.

There is no borrowed public view or automatic growth/free. Failed-construction
handles trap on method use through their null state; non-null dangling handles
remain a manual lifetime error. Mutating methods require writable receivers.
Private state/buffer fields cannot be accessed or constructed by callers.

Three additional `@strings`-only primitives provide a private buffer view,
checked byte store, and checked counted write. Stores/writes validate unsigned
capacity/index/range and null pointers before accessing bytes; range checks use
subtraction after validating the offset. A null buffer is valid only with zero
capacity (and zero write count). Actual storage size/lifetime remains the private
caller's responsibility. Writes reuse the existing native byte-copy ABI with
nonoverlapping ranges; no new native ABI, public pointer arithmetic, mutable
string API, or generic feature is introduced.

### Numeric library (SPEC-030c)

Ordinary `std.gloin` source defines the APIs in [the numeric guide](docs/numbers.md).
Existing `to_int`, trapping `to_string`, input, and string-only output contracts
remain unchanged. New parse and conversion calls return concrete status/value
structs: IntResult (i32), I64Result, U64Result, F32Result, F64Result, BoolResult.
New fallible format calls return FormatResult with a string value. Every failed
numeric payload is zero/false (positive floating zero); failed text is static
empty text. Shared statuses add INEXACT=8 and UNDERFLOW=9. They are available
through `@status` and new std aliases; existing values remain stable.

`parse_i32`, `parse_i64`, `parse_u64`, `parse_f32`, `parse_f64`, and `parse_bool`
each take a string. Signed integers require `[+-]?[0-9]+`; u64 permits optional
`+` but rejects any minus, including -0. Float grammar is
`[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`. Boolean text is exactly
`true` or `false`. All require complete ASCII input; no whitespace trimming,
prefixes, separators, NUL termination, NaN, infinity, or locale-dependent forms.
Malformed grammar returns INVALID before range checks. Numeric range failure
returns OVERFLOW. Floats round directly to their width, nearest/ties-even;
subnormals are accepted, negative zero is preserved, and nonzero input rounding
to zero returns UNDERFLOW. There is no fixed text-length limit, heap allocation,
or retained storage: parsing is O(n) with bounded auxiliary storage (bool O(1)).

`format_i32`, `format_i64`, `format_u64`, `format_f32`, and `format_f64` take an
explicit `&arena.GeneralArena` followed by a value of the named type. They use
fallible byte allocations of 12, 21, 21, 32, and 32 bytes respectively, including
one uncounted NUL. Integers use canonical decimal. Floats use locale-independent
shortest round-trip decimal, lowercase e for scientific notation, explicit
exponent sign and at least two exponent digits; negative zero formats as -0.
There is no implicit .0 suffix. `format_bool(bool) -> string` returns static
`true`/`false`, with no allocation or failure. Other formatters return FormatResult;
NO_MEMORY is recoverable. Results borrow the supplied arena until reset/free.

`format_f32_fixed(memory, value: f32, precision: u32)` and
`format_f64_fixed(memory, value: f64, precision: u32)` return FormatResult with
exactly precision digits after the decimal point (no point for zero precision),
rounded nearest/ties-even. Precision must be 0..18; INVALID is returned before
allocation otherwise. They request 64/352 bytes including NUL. These bounds
include the largest finite values at maximum precision. Rounded negative zero
keeps its minus. Native formatting rejects non-finite arguments with INVALID;
valid Gloin programs cannot produce them. Fixed-width formatting has bounded
O(1) work/storage plus arena allocation; retained allocation sizes are explicit.

Named conversions return the corresponding concrete result without allocation:

- `i64_from_i32`, `i32_from_i64`, `u64_from_i64`, `i64_from_u64`: exact integral
  conversion; widening always succeeds, out-of-range narrowing/signedness changes
  return OVERFLOW. Never wrap or saturate.
- `f64_from_i64_exact`, `f64_from_u64_exact`: INEXACT unless f64 represents the
  integer exactly. The `_rounded` variants instead choose nearest/ties-even.
- `i64_from_f64_exact`, `u64_from_f64_exact`: reject fractional values with INEXACT.
  The `_trunc` variants explicitly discard the fraction toward zero. Check the
  truncated candidate's range first; outside range is OVERFLOW. Thus unsigned
  truncation of -0.5 succeeds as zero, while exact conversion is INEXACT.
- `f64_from_f32`: exact widening, preserving negative zero.
- `f32_from_f64_exact`, `f32_from_f64_rounded`: check the input magnitude against
  finite f32 maximum before narrowing; larger magnitude is OVERFLOW. Nonzero
  values rounding to zero are UNDERFLOW, including in rounded mode. Exact mode
  reports INEXACT when a nonzero finite rounded result differs from the input;
  rounded mode accepts it. Both preserve negative zero and representable subnormals.

Every conversion takes only its value; type suffixes determine input/output.
All have O(1) time and auxiliary storage and use the core's nearest/ties-even
execution environment. Native non-finite inputs return INVALID. Safe range
checks precede any native float-to-integer cast. No exception, trap, automatic
printing, or implicit conversion is used for these recoverable errors.

Canonical `@std` alone may invoke the new typed private primitives. Native calls
use scalar values and explicit output pointers, never native aggregate returns;
the compiler validates types and the JIT checks exact ABIs before binding them.
The LLVM-independent runtime vendors fast_float at a pinned commit under MIT
for allocation-free direct-width parsing; formatting uses the selected platform's
`to_chars`. Tests cover correctly rounded boundaries, precision loss, decimal
round trips, allocation failure, external execution, and installed documentation.

### Streams and files (SPEC-030d)

`@io` is ordinary [io.gloin](stdlib/io.gloin) source with concrete File, Stream,
IoResult, FileResult, ReadResult, WriteResult, and MessageResult types. Complete
signatures, examples, costs, and error precedence are in [the I/O guide](docs/io.md).
Seven private typed native primitives provide host operations; only canonical
`@io` may call them. Native ABIs use scalar returns and explicit output pointers.

`io.stdin()`, `io.stdout()`, and `io.stderr()` produce borrowed Stream handles
without allocation. Standard streams retain host buffering and share position
with the legacy `std` routines, whose contracts remain unchanged. Stream exposes
read/write/flush and is_open, but no close. I/O methods require mutable receivers;
is_open and File.stream allow read-only receivers. Stdin is read-only; stdout
and stderr are write-only. Wrong-direction operations return INVALID.

`File.open_read`, `File.create_new`, `File.open_truncate`, and `File.open_append`
take an explicit metadata arena and counted path. Empty/NUL-containing paths are
INVALID before allocation or filesystem access. Metadata allocation precedes the
native open, so its failure cannot create or truncate a file. Exclusive create
uses native exclusivity, not an existence precheck. Owned streams are unbuffered;
opening temporarily allocates a terminated native path copy and retains native
FILE state until close. Metadata retained after a failed open is reclaimed with
the arena. File contents preserve all bytes, including NUL.

File copies and streams obtained by File.stream alias one arena-owned control
record and one cursor. File.close clears the shared native handle and consumes
it exactly once, including on close failure. Subsequent alias operations and
repeated close return CLOSED. Failed opens supply closed File values. Close
must occur before owner arena reset/free; arena cleanup does not close files.
All metadata aliases expire with that arena. A separate scratch arena supports
bounded read loops. Deferred close discards its result; check an explicit close
when output success matters. No borrow checker, reference count, automatic file
destruction, implicit flush on write, or implicit filesystem rollback is added.

Both Stream and File provide read_line, read_chunk, and read_all with an explicit
scratch arena and u64 max_bytes. Each valid call requests max_bytes+1 bytes,
zeroed by the arena; one uncounted NUL is reserved. Requests above
9223372036854775806 return NO_MEMORY before allocation. Allocation failure
consumes nothing. Returned bytes and any retained prefix borrow scratch until
reset/free; allocated storage is retained even on EOF/error.

- read_line strips LF/CRLF, preserves standalone CR, accepts final partial lines,
  and returns END only for empty EOF. Oversized lines drain through LF/EOF then
  return TOO_LONG/empty. I/O failure also returns empty; consumed bytes are lost.
- read_chunk requires a positive bound. It reads up to that bound, potentially
  blocking until enough bytes or EOF/error. Final partial EOF is OK; subsequent
  empty EOF is END. I/O errors preserve the received prefix.
- read_all returns OK even for empty EOF. At its bound it probes one byte, pushes
  excess back, and returns TOO_LONG with the bound-sized prefix. It never drains
  unbounded excess. A zero bound checks for empty input without losing a byte.
  I/O errors preserve progress. The EOF probe may block.

Read work includes O(max_bytes) zeroing; line draining adds O(bytes consumed).
Storage is bounded by the caller, independently of input length. Preflight order
is closed handle, direction, bound, allocation. Native read/write error indicators
are cleared for each new attempt; no automatic EINTR retry conceals progress.

write performs one native fwrite and may succeed short. write_all repeats short
successful writes, stops on error, and reports zero progress as IO_ERROR/EIO.
write_line performs write_all(text), then write_all(LF); failure before LF
suppresses it. Every WriteResult reports bytes accepted, including partial
progress and any LF. Writes allocate no Gloin storage and retain no input view;
byte work is O(bytes accepted), plus blocking I/O. Host standard-stream buffering
can allocate internally. Multi-call output, including append lines, is not atomic.
Flush reports buffered errors and does not promise disk durability. Broken pipes
return recoverable EPIPE through per-thread SIGPIPE masking; prior mask,
disposition, and already-pending signals are preserved.

Shared status values append NOT_FOUND=10, PERMISSION_DENIED=11, ALREADY_EXISTS=12,
and CLOSED=13. Results carry errno for OS failures; library preflight/allocation
failures carry zero. ENOENT/ENOTDIR map to NOT_FOUND, EACCES/EPERM to
PERMISSION_DENIED, EEXIST to ALREADY_EXISTS, ENOMEM to NO_MEMORY; other OS errors
map to IO_ERROR. No OS code is fabricated except EIO when stdio reports an error
without errno. Error text is separate: error_message(&arena, code) reserves 256
bytes for bounded localized text, or returns static "no OS error" for zero.
Negative codes/failed lookup return INVALID, long messages TOO_LONG, and failed
allocation NO_MEMORY. Diagnostic strings are not stable error categories.

The maintained copier and filter examples verify bounded scratch reuse, separate
stderr, binary data, exclusive creation, checked flush/close, and cleanup. Tests
inject partial reads/writes, zero progress, permission, flush, and close errors;
check metadata allocation before destructive opens and invalidated aliases; and
exercise native, external LLVM, installed, and relocated execution.

### Filesystem and process context (SPEC-030e)

Ordinary [fs.gloin](stdlib/fs.gloin) and [process.gloin](stdlib/process.gloin)
provide concrete APIs documented with complete costs and examples in the
[filesystem/process guide](docs/filesystem-process.md). Eight private typed native
operations handle metadata/mutations and host context; a guarded string-view
primitive supports immediate arena copies. No generic dispatch is introduced.

Path extraction is lexical and uses only `/` as a separator. basename/dirname
ignore trailing separators; empty/no-parent cases yield `.`, all-separator roots
yield `/`, and dirname removes only its final separator run. Results borrow input
or static literals, with O(path bytes) work and no allocation. Dot components,
backslashes, and interior separator runs are preserved. NUL gives INVALID.
join(&arena, base, child, max_bytes) validates both inputs; absolute child replaces
base unchanged, an empty side copies the other, both empty returns static empty.
Otherwise trailing base separators are reduced at the boundary before joining.
No filesystem lookup, symlink resolution, or `..` normalization occurs. Bounds
and overflow yield TOO_LONG before allocation. Ordinary joins allocate a prefix
and final copy; root/single-side joins allocate once. Both retained requests and
NO_MEMORY failures are explicit; outputs expire on arena reset/free.

metadata(path) returns status/errno/kind/size from native lstat: FILE=1,
DIRECTORY=2, SYMLINK=3, OTHER=4. Terminal symlinks are reported, including broken
links; trailing separators retain host directory-resolution rules. Size is
logical file bytes or link-target text bytes, zero for other kinds. Failed kind
and size are zero. mkdir creates one directory with 0777 filtered by umask;
remove_file unlinks a file/symlink itself and cannot remove directories;
rename_replace uses native POSIX replacement rules, with no cross-filesystem
copy fallback. Filesystem operations reject empty/NUL paths, preserve errno
categories from SPEC-030d, and use temporary native terminated path copies freed
on return. Queries do not conflate permission failures with missing paths.

The CLI forwards only values after FILE --, preserving empty/raw bytes. Argument
zero is the exact supplied source filename spelling; compiler flags are excluded.
The pre-FILE -- delimiter continues to escape filenames beginning with `-`.
Forwarding (even empty) in non-run modes is a usage error; bare extra filenames
remain errors. Program arguments do not change main's signature or exit behavior.

JitRunner::run(module, arguments) takes an optional string vector and owns a copy
for synchronous invocation. Embedding defaults to zero arguments and supplies no
implicit argv[0]; embedded NUL is rejected. Thread-local scopes restore previous
bindings on every returning path. External runners see zero arguments unless the
host uses the installed native push/pop scope API; it deep-copies C argument
strings, requires same-thread LIFO cleanup, and never exposes host tool flags.

process.arg_count() is O(1), allocation-free. arg(&arena, index:u64) gives
OUT_OF_RANGE without allocation for absent indices and copies nonempty values
with one exact-size arena request. env(&arena, name) rejects empty/NUL/equals names,
distinguishes NOT_FOUND from OK/empty, and copies nonempty values immediately;
it uses a temporary native name copy plus host lookup. Arguments and environment
values are counted bytes without encoding conversion. Successful empty values
need no arena storage; other copies expire on arena reset/free. Environment
mutation is externally coordinated, not globally snapshotted.

cwd(&arena, max_bytes:u64) requests max_bytes+1 bytes for bounded native getcwd.
It returns absolute host cwd or status/errno/empty text; too-small bounds are
TOO_LONG/ERANGE, never truncation. Oversized bounds and failed allocation return
NO_MEMORY. Storage remains until reset/free, including on native failure. There
is no implicit chdir. Host environment and cwd remain process-wide resources.

The maintained file tool combines CLI options, cwd/join, metadata, binary copying,
explicit cleanup, and missing/empty/populated environment labels. Tests cover
native and external execution, argument ownership/isolation, exact CLI forwarding,
filesystem errors, allocation failures, and installed/relocated execution from a
different working directory. Directory traversal, recursion, subprocesses, and
a general option parser remain follow-ups.


### Concrete numerical utilities (SPEC-030f)

`@math` loads `math.gloin`, with only `@status` as a source dependency. It provides
concrete `min_T`, `max_T`, and `clamp_T` for i32/i64/u64/f32/f64; `abs_T` for
signed integers and floats; PI/TAU constants for each float width; and f32/f64
floor/ceil/trunc/round/sqrt/pow/exp/log/log10/sin/cos/tan/atan2/hypot functions.
Min/max return their scalar type and select the first operand on equality.
Clamps and absolute values return checked concrete results. All rounding and
other floating functions return checked results of the input width; rounding
retains that floating width and ties are away from zero. No implicit conversion,
generics, arena storage, or hidden allocation is introduced.

`math.I32Result`, `I64Result`, `U64Result`, `F32Result`, and `F64Result` have
`status: i32` and a corresponding scalar `value`. Every failed result is zero
(positive floating zero). Invalid clamp bounds or math domains use INVALID;
minimum signed-integer abs and unrepresentable large float results use OVERFLOW;
nonzero results rounded to zero use UNDERFLOW. Nonzero subnormals succeed.
Approximate finite math does not report INEXACT. Ordinary arithmetic and checked
conversion contracts are unchanged, including unary minus in typed initializers.

Angles are radians; atan2 argument order is (y,x), including signed-zero quadrants.
Sqrt accepts -0 and preserves its sign, but rejects negative numbers. Log/log10
reject inputs <=0. Pow rejects negative bases with nonintegral exponents and
zero bases with negative exponents; 0**0 is defined as 1. Float abs and hypot
canonicalize zero to +0; rounding and sin/tan preserve signed zero. Min/max use
the first equal operand; clamp preserves an in-range operand unchanged.

Native math isolates each operation in the default floating environment and
restores caller errno, rounding mode and exception flags. Nonfinite native inputs
and unsupported private operation selectors fail INVALID/+0. Four typed native
ABIs are accessible only from canonical `math.gloin`. Floating overloads execute
at the requested width. Accuracy follows host libm; no universal ULP bound or
bit-identical portability is promised. The full API, signed-zero/domain rules,
usage, cost, ownership, and validation tolerances are normative in
[the numerical utilities guide](docs/math.md).


### Monotonic timing and seeded randomness (SPEC-030g)

`@time` provides explicit u64 nanosecond `Instant { ticks_ns }` and
`Duration { nanoseconds }` values, checked monotonic readings/elapsed differences,
checked whole-unit constructors and duration arithmetic, flooring whole-unit
accessors, and an explicitly approximate f64 seconds conversion. Clock ticks have
an unspecified epoch and may only be compared within the same clock domain.
Equal readings succeed; backwards elapsed inputs fail INVALID/zero. Duration
construction/addition overflow and negative duration subtraction fail OVERFLOW/zero.
Clock readings fail with zero ticks; only IO_ERROR carries a native errno. Units
do not promise clock resolution, suspend behavior, wall-clock meaning, or CPU time.

The JIT accepts an optional copied clock descriptor with borrowed userdata for a
synchronous invocation. Null explicitly selects the OS clock. Invalid descriptors
or binding allocation failure are setup errors; scopes restore prior thread-local
providers on every returning path. The installed native push/pop API supports
same-thread LIFO scopes and external execution. Native reads preserve errno and
normalize callback failures. Callbacks must not throw or recurse through the same
provider. No wall clock, sleep, or timer API is introduced.

`@random` provides a value-state `SplitMix64` with explicit u64 seed, version 1,
`next_u64`, unbiased half-open `below(bound)` using rejection, and an exact
53-bit `[0,1)` `unit_f64` mapping. All seeds, including zero, are valid; copied
states advance independently. Bound zero fails INVALID/0 without consuming state;
bound one consumes one word. Rejection imposes no retry cap or small fixed draw budget.
There is no global generator, implicit entropy, or cryptographic guarantee.
The fixed-increment SplitMix64-v1 transition and sampling mappings are a versioned
reproducibility contract. One private native transition supplies the wrapping and
bit operations; general source overflow/bitwise rules remain unchanged.

Both modules use ordinary source types/functions, no generics or per-operation
allocation. Clock invocation/host scope binding has one explicit native context
allocation; userdata remains host-owned. All 13 public functions/methods, error
payloads, units, ownership, costs, deterministic vectors, embedding contracts,
and acceptance cases are specified in [the time/random guide](docs/time-random.md).

### Integrated pre-generics library acceptance (SPEC-030h)

The maintained examples compose concrete source-library APIs without new language
features: a strict key/value configuration reader, a one-pass selected-column
statistics tool over an explicitly unquoted format, and a seeded numerical
simulation. Their formats, bounds, ownership, costs, side effects, and error
behavior are documented in [the integration guide](docs/integrated-examples.md).
CLI programs also expose ordinary module functions for embedding/composition.

Acceptance requires successful and rejected source inputs, independent numerical
oracles, bounded long runs, allocation-failure handling, normal-return resource
cleanup, JIT and external execution, native sanitizers, and installed/relocated
package checks. Actual arena backing allocations and descriptor counts verify
resource behavior; a low stack limit checks long-loop storage. These examples
do not enable implicit ownership, generic collections, or deferred concurrency.

### Def keyword

Every declarable item in Gloin must be preceded by the `def` keyword. Whether it's a variable, function, struct, or type you can declare it with the `def` keyword.
It was chosen so that the intent is clear.

### Variables

Variables by default are immutable. You can make them mutable by adding the `mut` keyword before the variable name.
Each variable must be declared with a type.

```gloin
def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def x: i32 = 5;
    x = 6; // Error: cannot assign to immutable variable
    
    def mut y: i32 = 5;
    y = 6;
    // y is now 6. Printing an i32 requires std.to_string(&memory, y).
    return 0;
}
```

### Constants

Constants are declared with `def const`. They are immutable and must be initialized at declaration.

```gloin
    def const PI: f64 = 3.14159;
    // PI is f64. Floating-point formatting is not part of SPEC-030.
```

### Endianness

Gloin allows explicit handling of endianness using `be_` (Big Endian) and `le_` (Little Endian) prefixes for integer types.
These can be used when defining types or performing conversions to ensure data portability.

```gloin
def network_packet: be_u32 = 0x12345678;
def local_data: le_u16 = 0x1234;
```

### Import System

Gloin supports three types of imports:

##### Standard Library `@std`

```gloin
import "@std";
import "@arena";

def main() -> i32 {
    std.println("Hello World");           // Print with newline
    std.print("Enter integer: ");        // Print without newline

    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def line: std.InputResult = std.input(&memory, 128);
    if line.status == std.END { return 0; }
    if line.status != std.OK { return 1; }
    def number: std.IntResult = std.to_int(line.value);
    if number.status != std.OK { return 2; }
    def text: string = std.to_string(&memory, number.value);
    std.println(text);
    
    return 0;
}
```

##### Local modules and exports (SPEC-029)

`import "./utils";` loads `utils.gloin` relative to the importing source file,
not the process working directory. `./` and `../` paths are supported, with an
optional explicit `.gloin` extension. Other extensions, absolute paths, bare
paths, embedded NULs, and `#package` imports are rejected. The last filename
component without the extension becomes the namespace and must be a non-reserved
Gloin identifier. Directory names need not be identifiers. There are no import
aliases, wildcard imports, directory entry files, or implicit extensions other
than `.gloin`.

Files are canonicalized, including symlinks, and loaded, parsed, checked, and
emitted once per compilation. An imported file resolves its own relative imports
from its canonical directory. A shared dependency reached through multiple files
has one nominal type identity and one set of definitions. Different files with
the same basename remain distinct and have distinct linkage names. All imports
in one file must have distinct namespaces and distinct canonical target files;
repeating a file through another spelling or symlink in that same file is an
error. A canonical file reached under distinct `@name` spellings is rejected.

Imports are file-scoped and visible throughout their file, regardless of order.
Each file has its own namespace bindings. Imports are not re-exported and a file
cannot use another file's dependencies without importing them itself. File-scope
functions, structs, and constants cannot collide with an import namespace.
Local variables may shadow it; the shadowed name resolves to that local value.

Only `pub` functions, ordinary structs, and constants are accessible through a
module namespace. Omitted visibility and `priv` are private. Struct fields and
methods retain their independent visibility and receiver rules. Module functions
and methods may use their own private declarations, but cannot see caller scopes.
Imported functions named `main` are ordinary module functions; only the root file
provides the executable entry point. Functions are not first-class values and
constants have no addressable storage.

Modules have no runtime initialization or implicit cleanup. File-scope runtime
variables and executable statements are rejected, including in unused modules.
Dependencies are checked before importers; constants are evaluated in lexical
order within their declaring file and may use already evaluated exported
constants from dependencies. Forward references to constants within one file
remain errors. Bodies are checked even if never called. Importing a module does
not call its functions or change the program's exit status.

All cycles are rejected, including self-imports, cycles through symlinks, cycles
through the root file, and otherwise unused cycles. The error points to the
import closing the cycle and includes the canonical path chain. Dependency
traversal is limited to 128 simultaneously active files (including the root),
with a diagnostic rather than unbounded recursion. Failed loads or checks do not
produce an executable or partial IR. A new compilation reloads its dependencies;
there is no persistent module cache.

Standard `@name` imports use the same dependency graph, resolving names in the
selected standard-library directory. Standard files may import standard modules
or relative helpers. Native primitives are available only to files reached via
standard imports, not to arbitrary local files with matching basenames. The
`GeneralArena` typed bridge belongs specifically to the `@arena` module. A local
file may call ordinary standard-library functions by explicitly importing them.

The compiler API uses its source filename to determine the root directory; for
an in-memory source, callers must still supply the intended filename when using
relative imports. CLI use from another directory needs no special search path.

```gloin
// utils.gloin
def pub calculate(x: i32, y: i32) -> i32 {
    return x * y + 10;
}
```

```gloin
// main.gloin
import "./utils";

def main() -> i32 {
    return utils.calculate(5, 3);
}
```

This program exits with status 25 and produces no implicit output. To print an
i32 result, format it explicitly with the arena-backed SPEC-030 API.

##### External Packages (#package)

```gloin
import "@std";
import "@arena";
import "#math";      // External package
import "#http";      // Another external package

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def sqrt_val: i32 = math.sqrt(16);
    std.println(std.to_string(&memory, sqrt_val));
    return 0;
}
```

### Pointers, References and Memory Management

Gloin supports pointers and references in the same way as C or C++ does, with some key differences for safety and clarity.

#### Pointers (`*T` vs `&T`)

- `*T`: A nullable raw address interpreted as a pointer to `T` when accessed. The address may be `null`; holding it does not establish that a live resource exists there.
- `&T`: A non-null reference to a live, initialized `T` resource. It must not be null.
- `*const T` and `&const T`: The corresponding pointer/reference with read-only access to the pointee.

Gloin uses manually managed memory and **does not use a borrow checker**.
The programmer must keep the referenced resource alive and obey its storage
lifetime. Reference creation does not allocate, copy, retain, or own that resource.
Copies, arguments, returns, and stored pointer/reference fields do not extend its
lifetime. The compiler checks types, initialization when taking an address,
mutability, and reference nullability; it does not prove lifetimes or dynamically
track liveness, ownership, or provenance. A non-null dangling address is not a
valid reference. Dereferencing dangling or otherwise invalid storage is outside
these guarantees and is the programmer's responsibility.

SPEC-025 accepts pointer/reference annotations recursively, including fields and
function signatures. `void` is not an object type, so `*void` and `&void` are not
supported. Pointer fields can break recursive struct layout cycles; by-value
cycles still fail. There is no implicit pointee conversion between different
numeric types or nominal structs.

`&place` takes the address of an initialized runtime variable, parameter,
addressable field, or dereference. It yields `&T` for writable storage and
`&const T` for read-only storage. Constants, functions, literals, and temporary
values are not addressable. Whole-value definite initialization of a local is required
before taking its address; out-parameter initialization is not implemented.
Memory behind a raw pointer is not tracked by local initialization analysis.
The programmer must provide correctly aligned, live storage and initialize it
before reading; null is the only runtime address-validity check.
Taking an address gives locals/parameters stable, correctly typed stack storage.
Loop-local stack slots are allocated at function entry and reused; object lifetime
still follows the declaration's scope and loop iteration.

`*pointer` accesses the declared pointee type, including non-i32 scalars, strings,
structs, and other pointers. `p.field` automatically dereferences one pointer or
reference to a struct, preserving field visibility and mutability. Writable
access through a pointer requires a writable pointee and `def mut` for each
struct field traversed. A pointer-valued field is itself a capability: a read-only
view of its container does not remove access rights from the pointer it contains.
Pointer binding mutability is independent of pointee mutability:
`def p: &i32` may write through `*p` but cannot rebind `p`; `def mut p: *const i32`
may rebind `p` but cannot write through `*p`. Live aliases are permitted; read-only
access does not freeze the resource against writes through other writable aliases.

Conversions may weaken only the outermost pointer layer: a reference may become
a nullable pointer to the same pointee, and writable access may become read-only.
The reverse conversions, arbitrary casts, pointer/integer conversions, and
nested qualifier covariance are rejected. These rules apply consistently to
initialization, assignment, fields, arguments, and returns. `&*p` explicitly
creates a reference from a raw pointer after checking it is non-null; lifetime
validity remains the programmer's responsibility.

`null` needs a contextual nullable pointer type, including in arguments, returns,
fields, assignments, or comparisons. It cannot initialize a reference and is not
an integer zero. Pointers support `==`/`!=` on identical pointee types, ignoring
outer mutability/nullability differences for comparison. They compare addresses,
not resource contents. There is no implicit boolean conversion, pointer ordering,
or pointer arithmetic. `null == null` without a typed pointer operand is rejected.

Every nullable dereference, indirect field access, or `&*p` checks for null and
traps before accessing memory if null. Explicit checks and short-circuit boolean
operators can avoid that trap. Indirect assignment evaluates its destination
address once before its right-hand side; reads and address-taking also evaluate
the receiver once. These checks do not require an unsafe block. Unsafe-block
syntax/checking is a separate future design; no such syntax is introduced here.
Compile-time pointer constants, heap allocation, deallocation, and arenas are
outside this step; SPEC-028 owns arena allocation and its alignment/lifetime API.

`self` in struct methods is always a pointer.

#### Memory Management

##### Function-exit defer (SPEC-027)

`defer function(arguments);` registers one ordinary function, static-method, or
instance-method call when execution reaches that statement. It is allowed only
inside function/method bodies, and its operand must be a call. Normal name,
visibility, receiver, argument, and initialization checks apply. A non-void
callee's result is discarded. This statement is unrelated to asynchronous
`deferred` functions, which remain deferred language work.

Registration evaluates the receiver once (for instance calls), then explicit
arguments from left to right, immediately. Their values are saved; the callee
body runs later. Scalars, strings, and structs are captured by value under their
normal copy rules. Pointers/references, including the implicit address of an
instance receiver, copy the address, not the resource. Subsequent rebinding of
the source variable does not change the saved argument. Mutating a resource
through a saved pointer remains observable by the deferred callee.

Every reached registration is independent, including repeated registrations
from one loop statement. Untaken branches and zero-iteration loops register
nothing. On explicit or implicit normal function return, all registered calls
execute exactly once in reverse registration order. A return expression is
fully evaluated and its result saved before cleanup; mutating the returned
local during cleanup does not change an already copied return value. Each
recursive invocation has its own registrations. A deferred callee runs its own
defers before the caller proceeds to its next cleanup.

Saved values survive their source bindings' lexical scopes, but capturing an
address does **not** extend its resource's lifetime. Function-scope locals stay
alive through that function's cleanup. A caller-owned resource must likewise
remain live until the deferred call finishes. Deferring a method on a block- or
iteration-local object past its scope is invalid lifetime usage; use a value
argument or a resource whose owner outlives cleanup. There is no borrow checker,
escape analysis, automatic ownership, or automatic resource retention.

Registration records use compiler-managed heap bookkeeping, proportional to the
number and size of pending captures, and are released during normal cleanup.
Loop registrations do not allocate a growing stack. Allocation failure traps.
No user allocator API is introduced (SPEC-028 remains separate).

Arithmetic/null traps and other process termination do not unwind or execute
pending defers. If argument evaluation traps, the new call is not registered;
if a cleanup call traps, older pending calls do not run. Returning an error code
normally still executes cleanup. Existing stdout write-error reporting remains
at the JIT execution boundary, after normal returns and their cleanup.

```gloin
import "@std";

def main() -> i32 {
    def mut message: string = "first";
    defer std.println(message);
    message = "second";
    defer std.println(message);
    std.println("body");
    return 42;
}
```

This prints `body`, `second`, and `first`, each on its own line, and exits 42.

##### Arena allocation (SPEC-028)

SPEC-028 implements initialized-value allocation through a real standard module, `stdlib/arena.gloin`, imported as
`@arena`. This module may expose multiple allocator types; it is not tied to a
single built-in `Arena` type.

The first public type is `arena.GeneralArena`: a general-purpose,
growing arena with stable allocation addresses. Other strategies may later have
their own public types in the same file, without renaming this type or changing
its behavior. Additional strategies and a shared allocator interface are not
required for SPEC-028.

The `alloc(value) -> &T` operation evaluates an initialized value of
type `T` once, allocates storage using that type's native size and alignment,
and copies the value into it before returning a non-null reference. Copies are
shallow; referenced resources retain their existing manual lifetimes. The first
API does not use the old conceptual `alloc(Type)` form.

Typed allocation requires compiler support until generic functions are defined.
That support must identify the allocator declaration and its allocation
operation explicitly, rather than treating every method named `alloc`, or every
type exported by `@arena`, as the general allocator. Lifecycle methods belong in
the library, with native storage operations behind a small runtime ABI. Future
allocator types must be able to select their own storage policy.

Memory management remains manual, without a borrow checker. The following
contract governs the source API and native runtime.

**Public operations and failure behavior**

| Operation | Result | Contract |
| --- | --- | --- |
| `GeneralArena.create()` | `GeneralArena` | Creates an empty arena; failure to allocate its control object traps. Backing blocks are allocated lazily. |
| `a.alloc(value)` | `&T` | Stores an initialized copy of `value`; allocation failure traps. |
| `a.try_alloc(value)` | `*T` | Stores an initialized copy on success; returns null on allocation failure. |
| `a.reset()` | `void` | Invalidates all allocated objects and retains backing blocks for reuse. |
| `a.free()` | `void` | Releases every backing block and the control object, then clears this handle. |

Instance operations require a writable receiver, as with `self: &GeneralArena`.
The receiver is evaluated first, then the initializer exactly once, before
attempting allocation. `try_alloc` does not undo initializer side effects on
failure. Allocation accepts supported, non-void value types; the checked
initializer type determines `T`, without changing general type-inference rules.
The returned reference/pointer designates writable storage, subject to ordinary
field mutability rules. No reference is produced before initialization finishes.

`try_alloc` makes allocation failure recoverable; it does not recover from
initializer traps or invalid lifetime use. A failed storage request leaves the
arena's allocation state and existing objects intact and releases any partial
native allocations. Unrepresentable sizes and arithmetic overflow are allocation
failures. Traps do not run deferred cleanup, consistent with SPEC-027.

**Handle ownership and lifetimes**

The handle contains private native state. Copying or passing it by value creates
an alias to the same arena, not a new arena or an additional owner. The programmer
designates one owner responsible for freeing it; helpers should take `&GeneralArena`.
There is no reference counting, implicit free, move tracking, or borrow checking.
Losing the last handle without freeing it leaks its storage.

`reset()` invalidates every allocated object and every pointer/reference into
those objects, including ones obtained through another handle. All live handles
still refer to the now-empty arena. Retaining or reusing the same address does not
make an old pointer/reference valid again. Reset does not run object destructors
or free resources referenced by stored values; those remain manually managed.

`free()` invalidates all allocated objects and all other copies of the handle.
Repeated `free()` on the same cleared handle is a no-op. Allocation and reset
through that cleared handle trap. A new `create()` result may be assigned to it
to start another arena. Other copies are dangling: using or freeing them is an
invalid lifetime operation with no promised runtime detection. Private fields
do not make a handle noncopyable or protect against this error.

The owner must stay alive through `defer owner.free()`, which captures its
address under ordinary method/defer rules. Cleanup of resources stored in the
arena must run before the arena is freed; LIFO registration determines that
order. Returning an arena reference from a function that frees the arena before
returning leaves a dangling reference and is the programmer's error.

The allocator has no internal synchronization. Concurrent operations on the same
arena require external synchronization; separate arenas have independent state.

**Storage and alignment**

The general allocator uses aligned bump allocation within native blocks. When a
block cannot satisfy a request, it reuses a suitable retained block or allocates
another. Existing blocks and objects never move during growth. Large requests
receive suitably sized blocks. Block sizes and growth factors are implementation
tuning choices, not observable capacity guarantees.

Allocation uses the compiler's target layout, including struct padding and
alignment. The runtime checks alignment padding and all size arithmetic before
allocating or advancing a cursor. Alignment must be a nonzero power of two and
must be honored even when it exceeds the backing allocator's default alignment.
Zero-sized values reserve at least one byte so that successful live allocations
still have distinct, non-null, correctly aligned addresses.

Reset rewinds allocation state without obtaining new storage and retains all
backing blocks, including large ones. Retained capacity therefore follows past
demand until `free()` releases it. Reset does not promise zeroed memory. Subsequent
typed allocations initialize their values normally; struct padding has no
specified contents. Individual object deallocation and uninitialized allocation
are outside this initial API.

**Native ABI and compiler boundary**

The general allocator has its own opaque native control object. The runtime uses
the C calling convention and these signatures on the selected native target:

```c
void *gloin_arena_general_create(void);
void *gloin_arena_general_alloc(void *state, uint64_t size, uint64_t alignment);
void gloin_arena_general_reset(void *state);
void gloin_arena_general_destroy(void *state);
```

Create and allocation return null on allocation failure. Allocation returns null
for invalid alignment or a request whose arithmetic cannot be represented on the
target. A successful allocation returns raw storage; compiler-generated typed
code initializes it before exposing the result. Reset and allocation require
live, non-null state. Destroy accepts null as a no-op; otherwise it requires live
state. No native exception crosses the ABI. Library/compiler wrappers enforce
the public trap/null policy and clear the receiver after destruction.

The JIT validates exact runtime signatures and registers these symbols explicitly.
Source functions cannot masquerade as runtime declarations. The native runtime
must also be available to external LLVM execution and relocated installations.
These operations are not a general source FFI, and future allocator types may
use different native state and storage operations.

**Implementation and acceptance sequence**

1. Implement the native general allocator and direct runtime tests, including
   deterministic allocation-failure injection and ASan/UBSan execution.
2. Add `arena.gloin`, checked typed allocation, native-layout lowering, and exact
   runtime linking/ABI validation. Preserve allocator identity and reject
   unrelated methods/types rather than dispatching by spelling alone.
3. Add source execution/rejection/trap fixtures and a substantial runnable
   example covering mixed types, mutation, growth, reset/reuse, and deferred free.
4. Run required and full compiler suites, external execution, and installed and
   relocated package checks. Replace the obsolete unchecked arena test with
   maintained source-based coverage; keep unrelated deferred failures visible.

Tests must cover native alignment, empty structs, large requests, stable addresses
through growth, independent arenas, shallow copies, evaluation order, mutable
receivers, forced failure without state corruption or leaks, checked arithmetic,
reset retention/reuse, complete freeing, and large allocation loops with a small
stack. Live handle aliases must observe the same allocation/reset state. Cleared
handle behavior and defer order need explicit tests. Tests must not dereference
dangling handles or retired objects as if their behavior were defined: retained
storage means ASan alone cannot establish reset-time lifetime correctness.

The following example is executable under SPEC-028.

```gloin
import "@arena";

def struct Particle {
    def mut position: f64,
    def mut velocity: f64,
}

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def particle: &Particle = memory.alloc(
        Particle { position: 0.0, velocity: 2.0 }
    );
    particle.position = particle.position + particle.velocity;
    return 0;
}
```

Example of pointer usage (executable under SPEC-025):

```gloin
import "@std";

def main() -> i32 {
    def mut value: i32 = 42;
    def ptr: &i32 = &value;
    if *ptr != 42 { return 1; }
    *ptr = 100;
    std.println("Value updated through reference");
    return value;
}
```

This prints one line and exits with status 100. Numeric output requires an
explicit call to std.to_string with caller-owned arena storage.

### Strings

The `string` type in Gloin is a fat pointer consisting of a pointer to the character data and a length.

```gloin
// Conceptual representation, not a built-in type declaration.
def struct StringLayout {
    def ptr: *u8,
    def len: usize,
}
```

This means passing strings by value is cheap (two words), and slicing is efficient.

### Structs and Enums

Gloin supports structs and enums. Structs are similar to Go structs, except that they have methods declared in the same scope.

#### Methods (SPEC-026)

Ordinary structs may contain instance methods and `def static` functions. All
signatures are collected before bodies, supporting forward and recursive calls.
Methods and fields share a member namespace: duplicate names and overloading are
not supported. Method names do not introduce bare functions into file scope.

An instance method declares exactly one receiver, as its first parameter named
`self`. Its type is `*Struct`, `&Struct`, `*const Struct`, or `&const Struct`, with
the containing struct's nominal identity. Value receivers, nested pointers, a
different struct type, omitted receivers, and extra `self` parameters are errors.
The binding `self` is immutable; a writable pointee allows mutations to mutable
fields under SPEC-024/025. Static methods have no `self` parameter or implicit
receiver and are called as `Struct.method(args)` (or `module.Struct.method(args)`
for an exported type). Static calls through an object and instance calls through
a type are rejected. Methods cannot be extracted as function values.

For `object.method(args)`, an initialized addressable struct local, parameter,
field, or dereference supplies its address. A pointer/reference receiver supplies
its value, never the address of its pointer slot. The resulting pointer must
convert to the declared `self` type using SPEC-025's outer capability weakening:
an immutable object requires a read-only receiver; a nullable pointer cannot
implicitly become a reference. `(&*pointer).method()` explicitly checks for null
when a reference receiver is required. A method declared with nullable `*Struct`
may itself test `self == null`; passing null does not trap at the call boundary,
but dereferencing it in the body traps normally.

Receiver evaluation occurs exactly once, before explicit arguments; arguments
then evaluate left to right. A struct temporary, such as `make().method()` or
`(Struct { ... }).method()`, must first be assigned to a named local. No hidden
temporary receiver storage or lifetime extension is introduced. Returning a
pointer/reference from a method follows the same manual lifetime contract as an
ordinary function: there is no borrow checker or escape analysis.

Visibility follows the existing file/module rules. Private methods and fields
are usable throughout their declaring file; calls from another module require
public methods. Qualified static calls also require a public type. A public
constructor may initialize private fields, and a public method may call private
helpers. Methods have no extra authority over field mutability. All bodies are
checked even when unused. Async, generic, and packed-struct methods remain deferred.

##### Method lowering

Methods defined inside a struct are purely syntactic sugar. They are lowered to global functions with the struct instance passed as a pointer in the first argument.

```gloin
def struct Foo {
    def x: int,
    
    // Instance method
    def bar(self: *const Foo) -> int {
        return 1; 
    }
}
```

Is exactly equivalent to:

```gloin
def struct Foo {
    def x: int
}

// Lowered global function
// Conceptual name; actual linkage uses a collision-free internal symbol.
def Foo_bar(self: *const Foo) -> int {
    return 1;
}
```

When you call a method:
```gloin
def f: Foo = ...;
f.bar();
```

It is compiled as:
```gloin
Foo_bar(&f);
```

Accessing `self.x` inside the method is simply accessing the field of the pointer passed as the first argument. The declared receiver is the only receiver parameter; lowering never inserts a second `self`. Static methods lower without a receiver. Internal linkage names include nominal struct identity and cannot collide with source-level function names.

To make a method public, you must place `pub` immediately after `def`. The `priv` keyword can be used to make a method private, but since it's the default visibility, it's not necessary to write it.

```gloin
import "@std";

def struct Person {
    def pub name: string,
    def pub age: i32,
    
    def pub static create(name: string, age: i32) -> Person {
        return Person { name: name, age: age };
    }

    def pub greet(self: &const Person) -> void {
        std.print("Hello, I'm ");
        std.println(self.name);
    }
    
    def pub is_adult(self: &const Person) -> bool {
        return self.age >= 18;
    }
}

def main() -> i32 {
    def person: Person = Person.create("Alice", 25);
    
    person.greet();
    
    if person.is_adult() {
        std.println("Person is an adult");
    }
    
    return 0;
}
```

### Functions

Functions are declared with the `def` keyword. Every parameter and return type is explicit; a `void` function returns no value.

```gloin
import "@std";
import "@arena";

// Function with parameters and return value
def add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Function with no return value
def greet(name: string) -> void {
    std.print("Hello, ");
    std.println(name);
}

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def sum: i32 = add(10, 20);
    std.println(std.to_string(&memory, sum));
    greet("Developer");
    return 0;
}
```


### Control Flow

Gloin supports control flow statements like `if`, `unless`, `while`, and `for` loops.

```gloin
import "@std";
import "@arena";

def main() -> i32 {
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def x: i32 = 10;

    // If statement (no parentheses around condition)
    if x > 5 {
        std.println("x is greater than 5");
    }
    
    // Unless statement (opposite of if)
    unless x < 0 {
        std.println("x is not negative");
    }
    
    // While loop
    def mut counter: i32 = 0;
    while counter < 3 {
        std.println(std.to_string(&memory, counter));
        counter = counter + 1;
    }
    
    // For loop
    for def mut i: i32 = 0; i < 5; i = i + 1 {
        std.print("Iteration: ");
        std.println(std.to_string(&memory, i));
    }
    
    return 0;
}
```

### Concurrency
Gloin provides two distinct models for concurrent execution. 
Understanding when to use each is key to writing high-performance applications.

1. Asynchronous Operations (`deferred`)

Asynchronous programming in Gloin is designed for I/O-bound tasks (network requests, file system operations, database queries).
It allows the program to initiate a task and continue working without waiting for the hardware to respond.

Key Concepts
- `deferred` keyword: Marks a function as asynchronous.
- `Deferred<T>`: The wrapper type returned by a deferred function.
- `join()`: Blocks the current thread until the task is complete, returning a `Result<T, E>`.
- `force_join()`: Blocks and returns the value directly, but panics if the task failed.

Example: Async Network Fetch

```gloin
import "@std";
import "#http";
// Define the async function
def deferred fetch_config(url: string) -> Deferred<Result<string, AppError>> {
    std.print("Fetching from "); std.println(url);
    // Simulated async I/O call
    return http.Client::get(url);
}

def main() -> i32 {
    // Calling it returns a handle immediately (non-blocking)
    def handle: Deferred<Result<string, AppError>> = fetch_config("https://api.gloin.org/v1");

    std.println("Doing other work while waiting...");
    // Safe retrieval with Result handling
    def result: Result<string, AppError> = handle.join();
    
    if result.is_ok() {
        std.print("Config: "); std.println(result.ok().value());
    } else {
        std.print("Error: "); std.println(result.err().value());
    }

    // Unsafe retrieval (if you are certain it won't fail)
    // def config: string = handle.force_join().ok().value();

    return 0;
}
```

2. Multi-threading (`spawnable`)
Multi-threading in Gloin is designed for CPU-bound tasks (heavy math, image processing, data sorting). 
It utilizes system-level threads to perform work in parallel across multiple CPU cores.
Key Concepts
   - `spawnable` keyword: Marks a function as safe to run on its own thread.
   - `run` keyword: Used to execute a spawnable function on a new thread.
   - `Spawn<T>`: The handle returned by the run operation.
   - `join()`: Blocks until the thread finishes and returns a `Result<T, E>`.
 
Example: Parallel Computation

```gloin
import "@std";

def struct MathEngine {
    def factor: f32,
    def static new(f: f32) -> MathEngine {
        return MathEngine{ factor: f };
    }

    // A method marked as spawnable
    def spawnable compute_heavy_pi(self: *MathEngine, iterations: i32) -> f32 {
        def mut result: f32 = 0.0;
        for def i: i32 in 0..iterations {
            result = result + (self.factor * 3.14159);
        }
        
        return result;
    }
}

def main() -> i32 {
    def engine: MathEngine = MathEngine::new(1.5);
    defer engine.free();

    // Launch the work on a separate system thread using 'run'
    def thread_handle: Spawn<Result<f32, err>> = run engine.compute_heavy_pi(1000000);

    std.println("Main thread is free to handle UI or other tasks.");

    // Wait for the thread to finish
    def result: Result<f32, err> = thread_handle.join();

    if result.is_ok() {
        std.println("Computation finished"); // f32 formatting remains deferred.
    }

    return 0;
}
```

#### Summary Comparison

| Feature | deferred (Async) | spawnable (Threading) |
| :--- | :--- | :--- |
| **Primary Use** | I/O (Network/Disk) | CPU (Logic/Math) |
| **Execution** | Event loop / Non-blocking | OS-level threads / Parallel |
| **Keyword** | `def deferred name()...` | `def spawnable name()...` |
| **Trigger** | Standard call: `name()` | Explicit: `run name()` |
| **Return Handle** | `Deferred<T>` | `Spawn<T>` |
| **Result Handling** | `join()` or `force_join()` | `join()` |


### Packed bitfield design (unresolved: SPEC-037/SPEC-038)

The examples below preserve open layout proposals: omitted backing storage,
implicit offsets, and mixed endian spellings are not approved alternatives.
SPEC-037/SPEC-038 must replace them with one consistent contract and byte-exact
examples before implementation acceptance.

Networking protocols often pack multiple flags into a single byte. C bit-fields are notoriously non-portable and implementation-defined. 
Gloin can solve this by being explicit about bit-positioning.

Explicit Bit-Mapping:

```gloin
def packed struct Flags {
    def is_syn: bit at 0,
    def is_ack: bit at 1,
    def is_fin: bit at 2,
    def reserved: u5 at 3 // Bits 3 through 7
}
```

Explicit offsets are intended to lower to masks and shifts. Packing alone does
not establish safe access to a network buffer; alignment, bounds, and pointer
rules must also be defined and checked.

### Bit Indexing and Endianness

Bit indexing in `packed` structs is strictly tied to the endianness of the backing storage type.

- **Little Endian (`u32`, `le_u32`)**: Bit 0 is the Least Significant Bit (LSB).
  - Example: `def flags: u4 at 0` occupies the lowest 4 bits of the word.
  - Usage: Standard x86/ARM local processing.

- **Big Endian (`be_u32`)**: Bit 0 is the Most Significant Bit (MSB).
  - Example: `def flags: u4 at 0` occupies the highest 4 bits of the word.
  - Usage: Network protocols (TCP/IP), file formats.

This ensures that "Bit 0" always corresponds to the "first bit" as defined by the protocol or architecture being modeled, avoiding common portability pitfalls.

```gloin
// Network Protocol (Big Endian): Bit 0 is MSB
def packed struct(be_u32) NetworkHeader {
    def version: u4 at 0, // Top 4 bits (31-28)
    def ihl: u4 at 4,     // Next 4 bits (27-24)
}

// Hardware Register (Little Endian): Bit 0 is LSB
def packed struct(le_u32) DeviceReg {
    def enable: bit at 0, // Bottom bit (0)
    def mode: u2 at 1,    // Bits 1-2
}
```

#### `packed` keyword

The intended purpose of `packed` is to control padding and bit layout; matching
a protocol requires the complete layout rules and byte-level verification.
It is mandatory to specify the storage container (backing integer type) for the packed struct to define the "Word" size for bit-manipulation operations.

```gloin
// A 20-byte IPv4 Header definition backed by u32 words
def packed struct(u32) IPv4Header {
    def version: u4,           // 4 bits
    def ihl: u4,               // 4 bits
    def dscp: u6,              // 6 bits
    def ecn: u2,               // 2 bits
    def total_length: u16_be,  // 16 bits, Big Endian (Network Order)
    def identification: u16_be,
    // ... rest of the fields
}
```

The storage type (e.g., `u32`) dictates how the compiler generates shift and mask instructions.
