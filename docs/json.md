# Bounded JSON (`@json`, 0.1.0 candidate)

`import "@json";` provides a pull reader, complete-document validation, quoted
string conversion, and a writer. [json.gloin](../stdlib/json.gloin) is ordinary
Gloin, compiled through the checked GloinIR pipeline. There are no JSON-specific
native functions or external JSON dependencies. Fallible public operations
return built-in `result<T>`.

The reader accepts one **complete input string** and returns one event per
`next()` call. It does not accept successive network fragments. Neither reader
nor writer allocates a DOM, map, heap buffer, or arena storage. Each keeps an
inline stack of 64 `i32` states; callers choose a permitted depth and provide
input/output storage. This API targets bounded API payloads and configuration.

## Run the example

The [packaged example](../examples/json.gloin) builds a nested request, escapes
a newline and Unicode text, writes the largest `u64` exactly, and reads the
payload back with checked conversions:

```sh
./build/gloinc --jit examples/json.gloin
./build/gloinc -O2 -o /tmp/gloin-json examples/json.gloin
/tmp/gloin-json
```

It prints a JSON object and `JSON payload and exact u64 verified`, then exits
zero. No server or external files are needed.

## Reading a document

```gloin
import "@json";
import "@std";

def main() -> i32 {
    def made: result<json.Reader> = json.Reader.create(
        "{\"message\":\"Hello, \\u4e16\\u754c!\"}", 256, 8
    );
    if made.erroneous { return 1; }
    def mut reader: json.Reader = made.value;
    def mut storage: [u8; 64] = zeroed;
    def mut complete: bool = false;
    while !complete {
        def next: result<json.Event> = reader.next();
        if next.erroneous {
            std.println(next.error.message);
            return 2;
        }
        def event: json.Event = next.value;
        if event.kind == json.DONE {
            complete = true;
        } else if event.kind == json.STRING {
            def decoded: result<string> = event.as_string(storage[..]);
            if decoded.erroneous { return 3; }
            std.println(decoded.value);
        }
    }
    return 0;
}
```

This prints `Hello, 世界!`. Keys arrive as `json.KEY`, separately from values.
Decode keys before comparing them: `"name"` and `"\u006eame"` decode to the
same name. Track enclosing events when interpreting a nested schema. Reusing
`storage` invalidates its previous decoded view; save a comparison result or
copy the text when it must survive another decoding call.

| API | Contract |
| --- | --- |
| `Reader.create(text, max_bytes, max_depth) -> result<Reader>` | Borrows complete input. Checks its byte limit and rejects depth limits above 64. Syntax is checked by `next()`. |
| `reader.next() -> result<Event>` | Produces one event or an error. Continue until `DONE`. An error marks this reader failed; subsequent calls fail. |
| `validate(text, max_bytes, max_depth) -> result<void>` | Validates the entire document with the same checks as the reader. |
| `event.as_string(output: [u8]) -> result<string>` | Decodes a `STRING` or `KEY` into caller storage and returns a borrowed counted string. |
| `event.as_bool() -> result<bool>` | Converts a `BOOLEAN`; other event kinds fail. |
| `event.as_i64() -> result<i64>` | Converts integer spelling in the signed 64-bit range. |
| `event.as_u64() -> result<u64>` | Converts unsigned integer spelling in the unsigned 64-bit range. |
| `event.as_f64() -> result<f64>` | Uses checked decimal conversion in `@std`; overflow and nonzero underflow fail. |

Public `Event` fields are `kind: i32`, `raw: string`, and `offset: u64`.
`raw` borrows the exact token spelling; `offset` is its starting byte position.
String/key tokens include quotes and their original escape spelling. Event
conversion methods are intended for events obtained from `Reader`.

| Kind | Meaning / raw text |
| --- | --- |
| `OBJECT_BEGIN`, `OBJECT_END` | `{`, `}` |
| `ARRAY_BEGIN`, `ARRAY_END` | `[`, `]` |
| `KEY` | Quoted object member name |
| `STRING` | Quoted string value |
| `NUMBER` | Decimal token, such as `-1.2300e+04` |
| `BOOLEAN` | `true` or `false` |
| `NULL` | `null` |
| `DONE` | Empty text, with offset after trailing whitespace |

Commas and colons are validated without producing events. `DONE` is repeatable.
Empty input is invalid. Scalar roots need depth zero; empty containers need
depth one. Every simultaneously open container consumes one level, up to
`json.MAX_DEPTH` (64). Parsing is iterative; nesting does not grow the call stack.

**Only `DONE` proves that the complete input is valid.** For example, `0 garbage`
can produce a number event before reporting an error. If handling must be
atomic, first call `validate`, then read the same unchanged input, or stage
application changes until `DONE`. Reader copies share input but have independent
cursors, depth stacks and failure flags. Keep input live and unchanged while
readers or events borrow it. An event conversion error does not poison a reader.
Errors contain static messages; precise error positions are not returned.
`Event.offset` describes successful tokens only.

## Grammar, Unicode and numbers

The grammar follows [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html):
objects, arrays, strings, decimal numbers, booleans and null; only space, tab,
CR and LF are whitespace. Comments, trailing commas, leading `+`, leading
integer zeroes, NaN, Infinity, and multiple root values are rejected.

Strings must contain valid UTF-8 Unicode scalars. Escapes are decoded, including
paired UTF-16 surrogate escapes. Invalid UTF-8, overlong encodings, unescaped
controls, lone surrogates, and a leading UTF-8 BOM are rejected. This is a
deliberate strict Unicode policy. No Unicode normalization occurs. Escaped NUL
becomes an ordinary byte in Gloin's counted string, not a terminator.

Duplicate keys are **preserved as separate events in source order**. The writer
also emits supplied keys in order. There is no implicit first-wins/last-wins
lookup. Applications requiring unique keys must enforce it, including names
that become equal after decoding.

Numbers are never automatically converted to floats. `1e400` is a valid token,
retained in `raw`, but fails `as_f64()`. Integer conversions require integer
spelling: `1.0` and `1e0` fail `as_i64()`. Signed conversion accepts `-0`;
unsigned conversion rejects all negative spellings. Float conversion may round
according to the [numeric contract](numbers.md). Retain raw text when native
numeric types cannot preserve the required range or precision.

## Writing a document

```gloin
import "@json";
import "@std";

def main() -> i32 {
    def mut bytes: [u8; 128] = zeroed;
    def made: result<json.Writer> = json.Writer.create(bytes[..], 4);
    if made.erroneous { return 1; }
    def mut writer: json.Writer = made.value;
    def opened: result<void> = writer.begin_object();
    if opened.erroneous { return 2; }
    def key: result<void> = writer.key("message");
    if key.erroneous { return 3; }
    def value: result<void> = writer.string_value("Hello\nworld");
    if value.erroneous { return 4; }
    def closed: result<void> = writer.end_object();
    if closed.erroneous { return 5; }
    def finished: result<string> = writer.finish();
    if finished.erroneous { return 6; }
    std.println(finished.value);
    return 0;
}
```

The writer inserts separators and validates key/value order. This prints
`{"message":"Hello\u000aworld"}`.

| API | Contract |
| --- | --- |
| `Writer.create(output: [u8], max_depth) -> result<Writer>` | Borrows writable output; rejects limits above 64. Zero capacity/depth are allowed, with bounds enforced by writing operations. |
| `begin_object()`, `end_object()` | Start/finish an object. Closing after an unpaired key fails. |
| `begin_array()`, `end_array()` | Start/finish an array. Mismatched closing operations fail. |
| `key(text)` | Write an escaped member name and colon when an object expects a key. |
| `string_value(text)` | Validate UTF-8 and write an escaped string value. |
| `number(text)` | Write one validated complete number token, retaining its spelling. |
| `boolean(value)` | Write `true` or `false`. |
| `null_value()` | Write `null`. |
| `finish() -> result<string>` | Return a borrowed view when exactly one root is complete. Repeated successful calls are permitted. |

Writing operations return `result<void>`. Any failure, including premature
`finish()`, marks the writer failed. Construct a new writer for another document.
Errors may leave a partial output prefix; publish only successful `finish()`
values. The buffer never grows, and no NUL terminator is appended.

Use one writer per destination. Copies share bytes but copy counters/stacks;
mutating multiple copies is unsupported. Pass helpers `&json.Writer`. Strings
supplied to writing operations must not overlap the entire destination buffer.
Finished text expires when that storage is modified or freed.

For typed numbers, use checked `std.to_string_<width>(&arena, value)` and pass
its success text to `number`. `std.format_f64` supplies finite float text after
checking its status. The writer copies the number, so formatting scratch may
be reused afterward. See [formatting](numbers.md).

## Standalone string conversion and costs

`decode_string(quoted, output) -> result<string>` accepts exactly one quoted
JSON string. `encode_string(text, output) -> result<string>` accepts decoded
UTF-8 and writes a quoted string. Both validate and measure before writing;
failure leaves destination bytes untouched. Exact required capacity suffices,
including zero bytes for an empty decoded string. Source and destination must
not overlap. Returned views borrow output bytes.

The encoder escapes quotes/backslashes and uses lowercase `\u00xx` for bytes
below 32. Other valid UTF-8 is retained. This performs JSON escaping, without
HTML escaping, normalization or canonicalization.

Reading/validation is linear in input bytes with fixed stack storage. String
conversion makes a bounded number of linear passes. Writer work is linear in
supplied strings/numbers, plus fixed structural work. There is no heap
allocation or I/O. Typed conversions have the costs documented in `@std`.

## JSON with HTTP

Set `Content-Type: application/json` and pass a finished writer view as the
body to `http_client.request`. Keep that buffer live and unchanged until the
request finishes. With `Exchange`, retain supplied upload views according to
the [streaming ownership contract](http-client.md#streaming-exchanges).

Wait for complete HTTP framing before parsing a response. JSON input must be
fully buffered in stable storage: use the bounded whole-body client or
accumulate within an application limit. DOM construction, automatic struct
serialization, schema validation, incremental network input, and arbitrary
precision arithmetic remain separate work.

## Verification

`python3 tests/json_smoke.py build/gloinc` checks valid/invalid documents and
compares string conversions and example output with Python's independent
JSON/UTF-8 codecs. A fixed seed supplies extra Unicode/nested cases. The state
fixture checks exact bounds, depth 64, offsets, reader copies, terminal errors,
writer ordering, duplicate keys, and numeric conversion failures. It runs in
JIT, native `-O0`, and native `-O2` modes. `JsonSmoke.CodecAndState` belongs to
the core gate; package checks repeat it with installed and relocated compilers.
