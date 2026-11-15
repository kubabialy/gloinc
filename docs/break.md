# Break (development 0.3.0)

`break;` exits the nearest lexically enclosing `while` or `for` loop in the
same function. The semicolon is required. Labels, values, and use outside a
loop body are rejected. Nested blocks, `if`, `unless`, and `switch` do not
change the target. Switch arms already have no fallthrough: `break` inside
an arm exits the surrounding loop, and a switch alone is not a break target.

```gloin
def main() -> i32 {
    def mut total: i32 = 0;
    for def mut i: i32 = 0; i < 5; i = i + 1 {
        if i == 3 { break; }
        total = total + i;
    }

    return total; // 3: 0 + 1 + 2.
}
```

Execution resumes immediately after the target loop. The remaining body,
`for` update, and further condition evaluations are skipped. A nested loop's
break leaves its outer loop running. Missing conditions or updates do not
change this rule. Operations reached before the break retain their evaluation
order and required checks; a break cannot suppress an earlier trap.

Statements later on the same terminated path are unreachable and rejected.
Other conditional arms can still reach later statements. Break paths do not
participate in the `for` update's initialization or result proofs; normal body
fallthrough and `continue` paths do. An update with no incoming path is still
checked against the pre-body state, as with a returning body.

Loop exit joins the state from every break, normal iteration, and the
conservative zero-iteration path, even for a literal true or omitted condition.
A body assignment followed by break therefore does not establish definite
initialization after the loop, nor does the loop establish a function return.
Results reassigned on a break path must be checked again. Success/error proofs
remain conservative after loops; check the result before accessing its payload.

Results in scopes exited by the break must already be handled. These scopes
include the body and nested blocks, and the header of a target `for`. Outer
results remain live with their handling obligations. An inner loop's break
neither exits an outer loop's header nor discharges its result obligations.

`defer` remains function-scoped. Break does not execute or cancel registered
calls; reached registrations retain their captured arguments until normal
function return, in reverse registration order. Skipped registrations do not
occur. Captured pointers must remain valid until the deferred call executes;
traps do not unwind.

The checked program records the resolved target in `SemanticData::break_targets`.
GloinIR represents the transfer as a verified `cf.br` to that loop's exit block.
The edge remains visible to control-flow analysis and optimization, with no
runtime transfer helper. Unchecked AST generation rejects a break without
checked target metadata. See also the [continue contract](continue.md).
