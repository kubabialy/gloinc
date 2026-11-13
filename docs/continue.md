# Continue (development 0.3.0)

`continue;` ends the current iteration of the nearest lexically enclosing
`while` or `for` loop in the same function. The semicolon is required. Labels,
values, and use outside a loop body are rejected. A nested `if`, `unless`,
block, or `switch` does not change the target.

```gloin
def main() -> i32 {
    def mut total: i32 = 0;
    for def mut i: i32 = 0; i < 5; i = i + 1 {
        if i == 2 { continue; }
        total = total + i;
    }
    return total; // 8: the update still runs after skipping i == 2.
}
```

For `while`, the next action is evaluation of the original condition. For
`for`, the update runs exactly once, followed by condition evaluation. An
omitted update performs no work; an omitted condition remains true. The
initializer is never repeated. Condition and update evaluation retain normal
left-to-right evaluation, short-circuiting, and required traps.

Statements later on the same terminated path are unreachable and rejected.
Other conditional arms can still reach subsequent statements. Initialization
and result proofs at a `for` update must hold on every incoming path, including
each `continue` edge. Body-local bindings are unavailable in the update.
Loop exit still includes the conservative zero-iteration path, even when the
condition is omitted; a loop with `continue` alone does not establish a return
or initialize an outer variable for use after the loop.

Iteration-local results must be handled before their scope is exited by
`continue`. Results in outer scopes and in the `for` header remain live, with
their existing handling obligations. A checked error branch may continue while
the remaining body uses the proven success payload.

`defer` remains function-scoped. Reached registrations retain their captured
arguments and run in reverse registration order on normal function return.
`continue` neither drains nor cancels them; skipped registrations do not occur.
Repeated iterations can accumulate registrations. Borrowed pointers captured by
a defer must remain valid until that deferred call executes. Traps do not unwind.

The checked program records a resolved loop target for every continue.
GloinIR uses verified `cf.br` edges within the function: a while condition
header or a for update block. These explicit edges participate in the existing
control-flow analyses and lowering; no runtime control-transfer helper is used.
`break` and enum payloads remain separate work.
