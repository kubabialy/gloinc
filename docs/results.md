# Results and errors (development tree)

`result<T>` and `error` are built-in lowercase types. They are newer than the
0.0.4 release and its versioned HTML guide. Use a result when a function can
return either a value or an error. `result<void>` reports success without a value.
`result<error>` is invalid.
Both words are reserved in this development compiler. Source written for 0.0.4
that used `result` or `error` as an identifier must rename that identifier.

```gloin
def divide(a: i32, b: i32) -> result<i32> {
    if b == 0 { return error("division by zero"); }
    return a / b;
}

def main() -> i32 {
    def answer: result<i32> = divide(6, 2);
    if answer.erroneous {
        return 1;
    }
    return answer.value;
}
```

The compiler wraps a `T` return as success and an `error` return as failure.
For `result<void>`, `return;` means success. There is no public `result`
constructor, implicit propagation, or direct whole-result forwarding. To
forward a failure, check it and return its error explicitly:

```gloin
def next() -> result<i32> {
    def attempt: result<i32> = divide(8, 2);
    if attempt.erroneous { return attempt.error; }
    return attempt.value + 1;
}
```

Construct an error with `error("literal message")`. Its argument must be a
string literal, including when the error is stored before returning. The
read-only `message` member is a Gloin `string`. The message is static storage:
copying an error or result does not allocate or transfer ownership. This
implementation carries no source location, stack trace, or debug payload yet;
production and debug builds expose the same message. A result payload retains
its normal ownership rules. In particular, an arena-backed pointer inside a
result does not outlive its arena just because the result was copied.

Every result binding, including a function parameter, must be checked using
`.erroneous` on every path that leaves its scope, including early returns, and
a result call cannot be discarded as a statement. `.value`
is available only on a path where the compiler has proved the result is not
erroneous; `.error` is available only on a path proved erroneous. A check does
not invoke a runtime exception or automatically return. The program chooses
what to do in each branch.

The compiler currently recognizes direct `if r.erroneous`,
`if !r.erroneous`, and `unless r.erroneous` tests on a named binding. It tracks
the true and false paths and keeps a proof after a branch that returns. A
`while` or `for` condition can prove a state inside its body, but proofs after
the loop are conservatively forgotten. Reassigning a mutable result invalidates
its earlier proof and requires another check. Taking a pointer to a result
binding is rejected so an alias cannot silently invalidate that proof. Results
cannot currently be stored behind pointers, in arrays or slices, or as struct
fields, because handling through those paths has not been defined. Bind the
result of a call to a local before accessing its members. The compiler does
not currently derive a proof from a stored boolean or a compound condition.

The GloinIR representation carries a nominal `!gloin.result<T>` or
`!gloin.error` across checked function calls and result operations. Lowering
uses a tag plus storage for the value and error; only the tagged variant may be
accessed. The inactive storage is zeroed and has no language-level value. The
compiler rejects unproven member access before lowering. Lowered extraction
also checks the tag and traps if malformed hand-built IR tries to read the
inactive variant.

Existing standard library functions that return `ReadResult`, `FsResult`, or
other status structs keep their documented API for now. Those types are
ordinary structs and do not receive the built-in handling checks.
`std.input` also keeps a status result because EOF is a normal control outcome,
distinct from an error or a successful input line.
The 0.1.0 `@net` and `@http` modules use `result<T>` for actual failures.
Expected socket readiness, EOF, and incomplete HTTP heads are successful
payload states; see [networking](networking.md).
`std.parse_i32_checked(text) -> result<i32>` reports invalid text and overflow
with distinct static messages. `strings.byte_at_checked` and
`strings.slice_bytes_checked` return results for byte bounds failures; a
successful slice still borrows its source bytes. See the
[runnable result example](../examples/result_handling.gloin) for both that API
and explicit failure forwarding.
