# Switch and match (0.2.0)

`switch` selects statements to execute. `match` selects a value. Both evaluate
their subject once and execute only the selected arm, with no fallthrough.
These constructs are introduced in 0.2.0; 0.1.0 rejects them. Run [the complete example](../examples/switch_match.gloin):

```sh
build/gloinc --jit examples/switch_match.gloin
```

It prints `PONG` and exits zero. The same program compiles to a native executable
with `build/gloinc -O2 -o switch-match examples/switch_match.gloin`.

## Switch statements

```gloin
switch command {
    case "PING" {
        send_pong();
    }
    case "QUIT" {
        return;
    }
    default {
        send_unknown_command();
    }
}
```

The calls above stand for application handlers. A switch accepts an integer,
boolean, string, or payloadless enum subject. Cases are compile-time constants
of the subject's type, or qualified variants of that enum. Integer constant
expressions and exported constants work; runtime bindings and function calls
cannot be case labels. Strings compare exact bytes and length, including
embedded NUL bytes, without allocating a copy of the subject.

`default` is optional and must be last. Without it, an unmatched value continues
after the switch. An empty switch evaluates its subject and continues. Each
arm is a separate block with ordinary local scope and function-exit `defer`
semantics. `return` exits the enclosing function. There is no fallthrough or
need for `break`; `break` and `continue` remain unsupported.

Duplicate case values are errors, even when their spellings differ (`1` and
`0x1`, for example). A default after complete boolean or enum coverage is
unreachable and rejected. Definite initialization and function-return checking
merge the paths that can continue. A switch without exhaustive coverage keeps
an unmatched path, regardless of the subject's particular runtime value.

## Match expressions

```gloin
def response: string = match command {
    case "PING" => "PONG";
    case "QUIT" => "BYE";
    default => "ERR unknown command";
};
```

Every arm is an expression followed by a semicolon. The enclosing declaration
or return also needs its own semicolon. Match can appear in arguments, returns,
initializers, and other expressions. Arms can contain calls or nested matches;
unselected calls and arithmetic do not execute.

A match must have **at least two arms** and be **exhaustive**. A default counts
as an arm. There is no special exception for a single-variant enum: a one-arm
match is rejected, and adding an unreachable default does not make it valid.

| Subject | Coverage |
| --- | --- |
| `bool` | Both values, or explicit cases plus a default |
| Payloadless enum | Every variant, or explicit variants plus a default |
| `result<T>` | Success and failure, or one outcome plus a default |
| Integer or string | Explicit cases plus a required default |

All arms must produce a value compatible with one result type. An enclosing
binding, argument, or return supplies that type when available. Otherwise the
first arm establishes it, using ordinary literal defaults. There are no new
implicit numeric conversions. Existing outer pointer/slice capability
conversions still apply when a contextual type requests them. Arrays can use
contextual brace initializers and `zeroed` inside arms. Void calls cannot be
arm values. Match does not implicitly wrap individual arms as results; a mix of
`T` and `error` arms is not a `result<T>`. Failure forwarding keeps the existing
explicit check-and-return rules.

A default must be last. Duplicate cases and defaults covering no remaining
outcome are rejected. All arms are checked, including those that a literal
subject would never select. Match is currently a runtime expression and cannot
be used in a `def const` initializer.

## Results and bindings

```gloin
def value_or_zero(outcome: result<i32>) -> i32 {
    return match outcome {
        case value(answer) => answer;
        case error(problem) => 0;
    };
}
```

`value(answer)` binds the success payload; `error(problem)` binds the error.
These are patterns in case headers. Each binding is immutable and local to its
arm. Success and failure require a binding, except that `result<void>` success
uses `value()`:

```gloin
def succeeded(outcome: result<void>) -> bool {
    return match outcome {
        case value() => true;
        case error(problem) => false;
    };
}
```

Matching handles the subject result. For a named subject, an explicit success
arm also proves `.value` access on that binding; an explicit error arm proves
`.error`. Matching a boolean such as `outcome.erroneous` does not establish
these result proofs; match the result itself. A default creates no payload
binding or success/failure proof. After the match, proofs are merged
conservatively; handling a result does not imply
that it succeeded. Reassigning a result still requires another check or match.
Nested results such as `result<result<i32>>` remain unsupported.

Payloads have their ordinary value-copy and ownership semantics. Copying a
pointer, slice, or string descriptor does not copy or extend the lifetime of
its backing storage. A result-binding match does not introduce ownership or
borrowing rules beyond those documented for the payload type.

## Current boundary and lowering

This implementation has one pattern per arm. Grouped cases, guards, ranges,
payload-bearing enums, and multi-statement match arms are not implemented.
A block arm would need an explicit way to produce its value; `return` keeps
its existing function-exit meaning. Floats, pointers, and aggregate values are
not supported as subjects. Such values can still be match results.

The compiler emits verified `gloin.switch` and `gloin.match` operations with
source-typed subjects and constant case tables. Match regions end in typed
`gloin.match_yield` operations. Result extraction uses the existing nominal
GloinIR result operations. Lowering expands dispatch into conditional branches
and value joins; string cases use bounded byte comparisons. The current
implementation does not promise jump-table dispatch or optimized string lookup.
