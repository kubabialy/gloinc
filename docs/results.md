# Results and errors (0.2.0)

`result<T>` and `error` are built-in lowercase types available in 0.1.0.
Use a result when a function can
return either a value or an error. `result<void>` reports success without a value.
`result<error>` is invalid.
Both words are reserved. Source written for 0.0.4
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

## Error values and diagnostic codes

`error("literal message")` constructs an error with both diagnostic codes set
to zero. The 0.2.0 compiler also accepts
`error("literal message", code, os_error)`. Exactly one or three arguments are
required. The message must still be a string literal; each code must have type
`i32`. Runtime code expressions are evaluated once, from left to right.

| Read-only member | Type | Meaning |
| --- | --- | --- |
| `.message` | `string` | Static explanatory text |
| `.code` | `i32` | Library/application category; zero means unspecified |
| `.os_error` | `i32` | Captured native OS error; zero means unavailable or inapplicable |

The codes are metadata. An error with both codes zero is still an error;
**only the result's variant determines success or failure**. The language does
not assign a universal code namespace or restrict codes to positive values.
An API must document its categories. Migrated filesystem mutations use
`@status` constants in `.code` and retain native errno in `.os_error`.
Native errno numbers are platform-specific: do not hard-code macOS values in
portable programs. These fields do not fetch the current errno when read.

```gloin
def require_positive(value: i32) -> result<i32> {
    if value < 1 { return error("value must be positive", 1, 0); }
    return value;
}

def use() -> i32 {
    def attempt: result<i32> = require_positive(0);
    if attempt.erroneous { return attempt.error.code; }
    return attempt.value;
}
```

Copying or forwarding an error preserves its message and both codes without
allocation or ownership transfer. Result matching preserves them too. Fields
cannot be assigned or borrowed as writable storage. Messages live in static
storage; there is no dynamic message constructor, source location, stack trace,
or debug payload yet. Debug and production builds expose the same fields.
A result payload retains its normal ownership rules: an arena-backed pointer
inside a result still cannot outlive its arena.

The current compiler also rejects a method receiver reference directly through
`result.value`, even for a const receiver. After proving success, copy the payload
into an ordinary local before calling its methods. This preserves that type's
normal value/aliasing behavior; see the [duration example](time-random.md).

Partial I/O progress belongs in explicit I/O state, not in `error`. A failed
result never provides a readable success payload. APIs that can transfer bytes
before failing must document where callers inspect that progress. Development
`@io` transfers use caller-owned `ReadState`, `ByteReadState` or `WriteState`;
see [the I/O contract](io.md).

## Required handling and access proofs

Every result binding, including a function parameter, must be checked using
`.erroneous` (or an exhaustive match in 0.2.0) on every path
that leaves its scope, including early returns, and
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

## Matching a result (0.2.0)

A match can handle either a named result or a call directly:

```gloin
def answer_or_zero() -> i32 {
    return match divide(84, 2) {
        case value(answer) => answer;
        case error(problem) => 0;
    };
}
```

Each payload binding is immutable and scoped to its arm. A successful
`result<void>` uses `case value()`. For a named subject, explicit success/error
patterns also prove the corresponding `.value`/`.error` access inside the arm.
A default supplies no such proof or binding. After the match, handling does
not imply success; reassignment still requires another check or match. This
does not add direct whole-result forwarding or implicit error propagation.
See [switch and match](switch-match.md) for coverage and typing rules.

## Pointer and slice payload conversions

After proving success, a payload can use the ordinary outer capability
conversions: `&T` can become `*T`, either can become read-only, and `[T]` can
become `[const T]`. For example:

```gloin
def borrow(value: &i32) -> result<&i32> {
    return value;
}

def main() -> i32 {
    def mut value: i32        = 42;
    def outcome: result<&i32> = borrow(&value);

    if outcome.erroneous { return 1; }

    def pointer: *const i32 = outcome.value;

    return *pointer;
}
```

The pointer still refers to `value`; converting a slice likewise preserves its
buffer and length. Conversions do not extend lifetimes, add ownership or make
the backing storage immutable through other writable aliases. They cannot
restore write access, make a nullable pointer non-null, change nested pointee or
element types, or bypass the result's success check. The result itself retains
its original payload type; this is a conversion of the extracted value.

Direct payload conversions are fixed in the working compiler after 0.1.0
([PERF-009](compiler-validation.md)). Published 0.1.0 requires an intermediate
binding of the exact payload type for the affected cases, followed by a separate
conversion. The [acceptance example](../tests/fixtures/core/run/capability_conversions/main.gloin)
covers both spellings, generic modules and slice payloads.

## Standard library results

In the 0.2.0 compiler, all 37 fallible `@math` functions and
`random.SplitMix64.below` return built-in results. Their former status structs
are removed. See [math migration](math.md#migration-from-010) and
[bounded randomness](time-random.md). Numeric payloads supply the context for
comparison and arithmetic literals after the normal success proof, including
`result<f64>` and wide integer payloads.

All fallible `@fs` APIs now return built-in results; the five old filesystem
result structs are removed. Paths return `result<string>`, metadata
`result<fs.Metadata>`, opening `result<fs.Directory>`, iteration
`result<fs.DirectoryRead>`, and mutations/close `result<void>`. Errors preserve
library categories and native OS codes. Directory end is a successful payload
with `end = true` and an empty name. See the
[filesystem migration guide](filesystem-process.md#mutation-migration-from-010).
All fallible `@io` calls and process argument/environment/cwd queries are also
migrated. File open returns `result<io.File>`; process text and OS diagnostics
return `result<string>`. Flush, close and transfers return `result<void>`.
Transfers receive caller-owned `ReadState`, `ByteReadState` or `WriteState` as
their last argument. Those records retain valid partial progress on failure;
the compiler checks the result, not access to progress. Empty EOF for line,
chunk and direct reads succeeds with `end = true`. See the [I/O contract](io.md)
and [process-context signatures](filesystem-process.md#argument-environment-and-cwd-functions).
Fallible `@strings` APIs also use built-in results. Byte access returns
`result<u8>`, slicing/copying/transformations/snapshots `result<string>`,
split construction `result<SplitCursor>`, builder construction
`result<StringBuilder>`, and appends `result<void>`. String errors carry
`@status` categories and OS code zero; there is no readable fallback payload.
Advancing a valid cursor is infallible and returns ordinary
`TextItem { end: bool, value: string }`. Empty items differ from exhaustion.
`FindResult { found, offset }` also remains an ordinary search outcome.
The compiler does not require an `end` or `found` check; these flags describe
normal outcomes. See [byte strings](strings.md) and [text construction](text-construction.md).
Successful outcome records such as cursor items, socket readiness and child
exit status remain ordinary values. Previously migrated APIs that
use the one-argument error constructor still return zero diagnostic codes unless
their individual documentation says otherwise.
`@std` parsers, numeric formatters and fallible conversions now use built-in
results. `input` returns `result<InputLine>` with successful `{ end, value }`;
EOF has `end = true`, while an empty line has `end = false`. The old eight std
status wrappers are removed. Errors carry `@status` categories and OS code zero;
the native stdin ABI does not capture errno. `to_string` now reports allocation
failure instead of trapping for it. Plain numbers are returned by infallible
`i64_from_i32`, `f64_from_i64_rounded` and `f64_from_u64_rounded`. All other
conversion checks retain their contracts. See [input](standard-library.md) and
[numeric migration](numbers.md#migration-from-010). The six fallible `@time`
APIs now use `result<Instant>` or `result<Duration>`; the old time wrappers
are removed. Zero/equal readings succeed; backwards elapsed time fails. Time
errors preserve `@status` categories and native clock errno; other time errors
have OS code zero. See [timing migration](time-random.md#migration-from-010).
The 0.1.0 `@net` and `@http` modules use `result<T>` for actual failures.
Expected socket readiness, EOF, and incomplete HTTP heads are successful
payload states; see [networking](networking.md).
`std.parse_i32_checked(text) -> result<i32>` reports invalid text and overflow
with distinct static messages. `strings.byte_at_checked` and
`strings.slice_bytes_checked` remain aliases of the canonical result-returning
`byte_at`/`slice_bytes` functions, including their `OUT_OF_RANGE` codes; a
successful slice still borrows its source bytes. See the
[runnable result example](../examples/result_handling.gloin) for both that API
and explicit failure forwarding.
