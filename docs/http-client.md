# Bounded HTTP and HTTPS clients

The 0.1.0 release includes `@http_client`, written in Gloin on top of `@net`,
`@http`, and `@time`. It opens one connection, sends one request, reads one final
HTTP/1.1 response, and closes the connection. Public operations use built-in
`result<T>` errors. HTTP status codes, including 4xx and 5xx, are successful
response values: the application decides what they mean.

`http_client.Exchange` advances one nonblocking exchange at a time. Drive many
exchanges through `net.wait_many`, supply upload chunks, and consume download
chunks from reusable buffers. `http_client.request` is the synchronous,
whole-response convenience wrapper over that same state machine. A separate
Gloin server demonstrates concurrent readiness handling in the sibling example.

## Sending a request

```gloin
import "@http";
import "@http_client";
import "@net";
import "@std";

def main() -> i32 {
    def endpoint: http_client.Endpoint = http_client.Endpoint {
        address: net.Address.loopback(8080),
        tls: false,
        hostname: "localhost",
        trust_file: ""
    };
    def request: http_client.Request = http_client.Request {
        method: "POST",
        host: "localhost:8080",
        target: "/echo",
        body: "{\"message\":\"hello\"}"
    };
    def headers: [http.Header; 1] = {
        http.Header { name: "Content-Type", value: "application/json" }
    };
    def mut head: [u8; 8192] = zeroed;
    def mut body: [u8; 65536] = zeroed;
    def received: result<http.Response> = http_client.request(
        endpoint, request, headers[..], head[..], body[..], 8192, 32768, 5000);
    if received.erroneous {
        std.println(received.error.message);
        return 1;
    }
    def response: http.Response = received.value;
    if response.code != 200 { return 2; }
    std.println(response.body);
    return 0;
}
```

The address is explicit. `hostname` supplies TLS verification and SNI when
`tls` is true; `request.host` supplies the HTTP Host field, including a port
when needed. Set both names for the intended server. An empty `trust_file`
uses OpenSSL's configured trust locations. A PEM file can supply a private CA
for local testing. There is no option to skip certificate or hostname checks.

The client performs no DNS lookup. Call `net.resolve_ipv4` beforehand if needed,
choose an address, and set its port. That resolver can block without a deadline;
it is outside the HTTP timeout. IPv6 and asynchronous DNS are still pending.

The arguments after the request headers are:

| Argument | Contract |
| --- | --- |
| `head_storage: [u8]` | Nonempty caller buffer holding the final response head; its length bounds each head |
| `body_storage: [u8]` | Caller buffer holding the decoded body; its length is the body limit, and zero length is valid |
| `max_request_head: u64` | Maximum formatted request-head bytes |
| `max_response_metadata: u64` | Cumulative bytes in all response heads, chunk-size lines, chunk CRLFs, and trailers |
| `timeout_ms: i32` | Positive overall timeout for connect, handshake, send, and receive; it is not reset by progress |

The deadline uses the monotonic clock and rounds readiness waits up to the next
millisecond. It is checked before each active exchange step, including after
successful progress. Clock failures and backwards clock readings produce an
error. Synchronous TLS setup, certificate loading, allocation, decoding, cleanup,
and host scheduling can add latency; this is not a hard real-time guarantee.

The request body is a counted string and may contain arbitrary bytes, including
NUL. The library derives Content-Length from its byte length. Request methods
must be valid HTTP tokens; CONNECT is unsupported. Targets use origin form
(`/path?query`), without spaces, fragments, or unescaped non-ASCII bytes. GET
and HEAD bodies are rejected by this convenience API. Arbitrary application
headers, including Authorization, are supported. Read secrets from the process
environment and keep them out of example source and logs.

The formatter owns Host, Content-Length, and Connection. It rejects attempts
to supply those headers or Transfer-Encoding, Trailer, TE, Upgrade, or Expect.
Names are validated with HTTP token rules, and values reject CR, LF, NUL, and
other control bytes except horizontal tab. Empty values are allowed. Header
names are case-insensitive; method names are case-sensitive.

## Response ownership and framing

`http.Response` contains `code: i32`, `head: string`, and `body: string`.
`head` and `body` borrow the supplied buffers. Keep those buffers live and
unchanged until the response is consumed. The buffers must not overlap each
other or request storage. The compiler does not enforce those lifetime or
aliasing rules. A failed exchange can leave partial output in the buffers;
partial bytes are not a successful response.

Supported response framing follows [RFC 9112, section 6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3):

- Content-Length: read exactly that many bytes and reject early EOF.
- A single `Transfer-Encoding: chunked`: decode chunks with checked hexadecimal
  lengths, consume the zero chunk and trailer terminator, and reject truncation.
- Neither header: read until clean transport EOF. TLS requires an authenticated
  close notification; a bare TCP close inside TLS is an error.
- HEAD, 204, and 304: complete at the end of the head. HEAD/304 metadata lengths
  do not require body storage. Framing headers on 1xx/204 are rejected.
- Informational responses: consume them before the final response, within the
  cumulative metadata limit. Status 101 is rejected because upgrades are not
  implemented. A 205 response must decode to an empty body.

Transfer-Encoding plus Content-Length, conflicting Content-Length values,
invalid decimal lengths, unsupported or repeated transfer codings, malformed
CRLF, folded fields, and invalid chunk syntax are errors. Repeated identical
Content-Length fields are accepted; comma-separated length lists are rejected.
Only HTTP/1.1 status lines are accepted.

Chunk extensions support token and quoted values, including quoted escapes;
the values are validated and discarded. Trailer fields are validated and
**discarded**, never merged into the response headers. Content-Length,
Transfer-Encoding, and Host trailers are rejected. Each chunk-size or trailer
line has a 1024-byte limit including CRLF; the cumulative metadata limit also
prevents an unbounded sequence of tiny chunks or informational heads.

Content-Encoding is preserved: gzip or another encoded body remains encoded.
There is no decompressor, redirect following, proxy support, connection reuse,
automatic retry, cookie jar, or URL parser. The separate [JSON module](json.md)
builds request bodies and reads complete buffered response bodies; it does not
consume successive HTTP output fragments. Streaming uploads must
have a known Content-Length; chunked request generation and unknown-length
uploads are not implemented. Both clients monitor responses during uploads.
Once a final head is validated, they stop sending request bytes, read the
bounded response, and close the connection. Bytes already accepted by the OS
or TLS layer cannot be recalled. This includes early 4xx/5xx responses, which
remain successful HTTP response values. See [RFC 9112 §9.5](https://www.rfc-editor.org/rfc/rfc9112.html#section-9.5).

## Inspecting response headers

```gloin
def fields: result<http.Headers> = http.headers(response.head, 8192);
if fields.erroneous { return 1; }
def mut iterator: http.Headers = fields.value;
def mut field: http.HeaderItem = iterator.next();
while field.available {
    if http.header_name_equal(field.field.name, "Content-Type") {
        std.println(field.field.value);
    }
    field = iterator.next();
}
```

This fragment belongs after the response binding in the complete program.
The iterator validates the complete head, preserves field order and duplicates,
and returns borrowed names and trimmed values. End of iteration is
`available == false`; it is not an error. Copies have independent cursors.
Application-specific duplicate-field handling remains the caller's decision.

## Streaming exchanges

The complete [32-connection example](../examples/http_stream.gloin) shows the
scheduler and incremental binary-body validation. It is also available with a
standalone Gloin HTTP server in the sibling `gloin_examples/http_stream`
directory. Run `gloinc --jit server.gloin` there, then `gloinc --jit main.gloin`
in another terminal. Both programs use `net.wait_many`. The example server
serves batches of 32 connections and bounded fixed/chunked responses; it has no
server-side TLS. The independent HTTPS test peers remain in the compiler tests.
There is no implicit event loop, thread, task, callback, or background cleanup.
All fallible operations use built-in `result<T>`.

Create an exchange with:

```gloin
http_client.Exchange.open(owner, endpoint, request, headers, buffers, limits)
```

| Argument/type | Fields and meaning |
| --- | --- |
| `owner: &arena.GeneralArena` | Owns formatted request head, exchange state, and transport metadata; keep alive until after all exchanges are closed |
| `Endpoint` | Same explicit IPv4 address, `tls`, verification `hostname`, and `trust_file` as the convenience API |
| `StreamingRequest` | `method`, `host`, `target`, and `content_length: u64`; body bytes arrive later through `supply` |
| `headers: [const http.Header]` | Same validation and reserved framing fields as the convenience API |
| `Buffers` | Four nonempty, disjoint writable `[u8]` slices: `head`, `receive` (wire input), `send` (upload staging), `body` (decoded output) |
| `Limits` | `request_head: u64`, `response_metadata: u64`, `response_body: u64`, and positive `timeout_ms: i32` |

`response_body` limits the **total decoded bytes**, independently of body-buffer
capacity. A 1 KiB body buffer can stream a much larger response. Zero is a valid
total body limit, though all four buffers must still be nonempty. The head
buffer bounds each informational/final head; metadata accounting is cumulative.

`open` formats the head, allocates state, and opens a nonblocking socket. The
first `step` starts connecting. Each `step(ready: i32) -> result<Progress>`
performs bounded work: one phase transition or one read and at most one write,
plus decoding/staging limited by the supplied buffers. It never waits for
readiness. Pass zero for runnable work or the readiness bits returned by the
last socket wait. TCP connect, TLS handshake, and I/O retain their state across
calls. Pending TLS writes keep the same bytes and length across retries;
read/write operations may request readiness in the opposite direction.

`Progress` has `state: i32`, `wait_for: i32`, `timeout_ms: i32`, and
`head_ready: bool`:

| State | Caller action |
| --- | --- |
| `PROGRESS` | Call `step(0)` again after giving other connections their turn |
| `WAIT` | Watch `exchange.socket()` for `wait_for`; retry with actual readiness bits, or zero after timeout/interruption |
| `NEED_INPUT` | Supply the next upload chunk; if the producer is paused, continue watching `wait_for` for early responses and schedule the deadline |
| `HAVE_OUTPUT` | Process `exchange.output()`, then acknowledge bytes with `consume`; retain them to pause this exchange |
| `COMPLETE` | Framing succeeded, every output byte was acknowledged, and the connection is closed; inspect `head()` |

`head_ready` means `head() -> result<http.ResponseInfo>` is available.
`ResponseInfo` contains `code` and the borrowed final `head`; it never contains
a buffered body. Availability of the head does not imply body completion.
Informational heads are consumed internally. A completed exchange continues to
return `COMPLETE`; its head remains readable while storage remains live.

### Upload and download ownership

- `supply(bytes: string) -> result<u64>` copies up to send-buffer capacity and
  returns the accepted count. Advance the producer by **that count**. Call it
  after `NEED_INPUT`. Empty input, input exceeding the *remaining declared*
  length, or supply while staging is occupied is an error. Clip the offered
  chunk to the remaining Content-Length. Binary NUL bytes are supported.
- Input may be reused immediately after `supply` returns. Once all declared
  bytes have been supplied, no more input is requested. `finish_input()` is
  optional for an exact upload; an early call detects a short producer, cancels
  the exchange, and returns an error. An early final response also ends the
  need for input. It may arrive before all supplied bytes have reached the peer.
- `output() -> string` borrows pending decoded bytes. `consume(count)` accepts
  a positive count up to the pending length; partial consumption is supported.
  While any bytes remain, repeated `step` calls return `HAVE_OUTPUT` without
  reading or writing the socket. A different connection can keep progressing.
- The output view is stable until consumption, cancellation, or terminal error.
  Do not retain it while reusing the body buffer. A stream can deliver valid
  prefixes before a later framing, transport, timeout, or limit failure.
  Only `COMPLETE` confirms a complete response. Use temporary files or another
  explicit commit boundary when partial output must not become permanent.
- Buffers must not overlap one another, request/input bytes, or another live
  exchange's storage. Keep the request method and endpoint strings live and
  unchanged through the exchange. Formatted headers/host/target are copied at
  `open`; the headers array itself is not retained. Gloin currently does not
  prove buffer lifetimes or non-aliasing.

### Deadlines, cancellation, and scheduling

One monotonic deadline starts during `open` and is not reset by progress.
It includes connection, handshake, upload-producer pauses, download-consumer
pauses, and receive time. Every active `step` checks it, even when output is
waiting. `timeout_ms` is the remaining wait budget rounded up to milliseconds,
a snapshot to use promptly; it is not a fresh per-operation timeout.

For several exchanges, take the minimum remaining timeout across **all** active
connections, including ones whose application producer/consumer is paused.
Include only open sockets with nonzero requested masks in `net.wait_many`.
Compact the wait set and keep a mapping back to exchange indices. Never block
while any exchange has runnable work: use a zero-timeout poll instead. A paused
consumer should be removed from socket watches and resumed by the application
or its deadline timer; repeatedly polling a readable socket would spin. A
paused producer can keep its read watch to detect early responses. The example
has immediately available consumers and GET requests, so it needs no separate
application wakeup source.

No background timer cancels an exchange: the application must call `step` when
its deadline expires. If the application stops driving exchanges, they remain
open until explicitly cancelled. Certificate/trust-file loading, allocations,
TLS cleanup, byte processing, and host scheduling can add latency. Socket I/O
is nonblocking, but this is not a hard real-time scheduling guarantee.

`cancel() -> result<void>` closes TLS then TCP, is idempotent, and affects every
copy of an `Exchange`. A subsequent `step` returns `HTTP exchange cancelled`.
Fatal `step` errors also close the transport and remain sticky. Ordinary invalid
`supply`/`consume` arguments return errors without advancing state; the caller
must correct the operation or cancel. Calling `head()` before it is ready is a
nonterminal query error. No socket is automatically closed when an arena is
freed. Arrange deferred cancellation before owner cleanup on every exit path.
The socket returned by `socket()` is borrowed **for readiness only**: do not
read, write, close, or start TLS through that alias.

Copies share exchange state and must be driven serially. Cancelling or completing
an exchange releases native socket/TLS resources; its arena metadata and
formatted request head remain until arena reset/free. For repeated batches,
reset an arena only after closing every exchange and releasing borrowed views.

### Memory and work

Gloin exchange storage is bounded by the four supplied buffers, a 1024-byte
chunk-line scratch array, fixed exchange state, and bounded formatted-head
allocations. No per-body-chunk arena allocation occurs. TLS and kernel socket
buffers are additional. `wait_many` uses stack scratch through 64 sockets and
native temporary allocation above that; it accepts at most 4096 sockets.

The example's application buffers total 192 KiB across 32 exchanges while
31 completed bodies total 4,063,356 bytes. It validates each binary chunk and
discards it. This demonstrates bounded application buffering and independent
progress, not a throughput benchmark or a production connection pool.

## Streaming decoding without sockets

`http.ResponseDecoder.create(head_storage, max_body_bytes, method,
max_metadata_bytes) -> result<http.ResponseDecoder>` creates the allocation-free
framing state machine. `feed(bytes, eof, output: [u8])` returns
`StreamProgress { complete, consumed, produced, head_ready }`. Process only
`output[0..produced]` and retain input after `consumed` for the next call.

The decoder deliberately stops immediately after validating the final head,
even when body bytes remain in the input. It also stops when body output is
full. A zero-sized output is valid: metadata can still be consumed, but body
data waits for space. This lets transport drivers see early final responses
before staging more upload data. `head()` exposes `ResponseInfo` as above.

Apply `eof` again when retrying an unconsumed suffix: EOF takes effect only when
all supplied input has been consumed. A close-framed body completes at that
point; an unfinished Content-Length or chunked body fails. After completion,
feeds consume zero bytes. A failed feed poisons the decoder. Error calls can
have touched output storage without returning a produced count; only bytes
reported by earlier successful calls were delivered to the application.

Decoder copies duplicate counters but alias head storage; use one active copy
per buffer. Input, head storage, and output must not overlap. Output can be
reused after each successful call's bytes are processed. Total body and metadata
limits never reset when output is reused. Chunked, fixed, EOF, status, trailer,
and validation rules are the same as the convenience reader below.

## Driving the decoder yourself

`http.ResponseReader.create(head_storage, body_storage, method,
max_metadata_bytes) -> result<http.ResponseReader>` creates an incremental
reader without allocation or I/O. Call `reader.feed(bytes, eof)` for each
received byte sequence. `eof` means the transport ended cleanly **after** those
bytes. Each successful `http.ResponseProgress` reports `complete` and
`consumed`, the number of bytes taken from that input. Retain any unconsumed
suffix yourself; the decoder does not treat it as another response.

After completion, `reader.response() -> result<http.Response>` exposes the
borrowed final head/body. Calling `response()` early is an error. Calling
`feed` after completion succeeds with zero consumed bytes. Any feed failure
makes the reader unusable; later feeds return an error. Create a new reader
for another exchange. Reader copies share their buffers and must not be driven
concurrently. Input bytes must not overlap either output buffer.

The reader consumes each input byte once and validates each complete head once. Decoding
is linear in the supplied wire bytes, uses fixed 1024-byte line scratch in the
reader, and performs no allocations. The synchronous wrapper additionally uses 4 KiB each for send, receive,
and decoded-output scratch. Request text is staged through send scratch to preserve
TLS retry identity. The request head has an exactly sized arena builder and
final copy, plus a small formatted length. The convenience wrapper has an internal arena retaining exchange/transport
metadata until the exchange closes; OpenSSL has its own allocations. Both
success and failure paths close TLS and TCP before freeing that arena.

## Run the example locally

[http_client.gloin](../examples/http_client.gloin) sends a JSON POST to
`127.0.0.1:8080/echo`, prints the status, Content-Type if present, and body.
Start this small fixture in one terminal:

```sh
build/gloinc --jit examples/http_echo_server.gloin
```

The [Gloin server](../examples/http_echo_server.gloin) accepts one local
`POST /echo`, consumes up to 4 KiB of body after a head bounded to 4 KiB, sends
`{"ok":true}`, and exits. It has a 30-second overall deadline and binds only
loopback. Restart it for each client run. This is a small HTTP fixture, not a
general server or a JSON echo service.

In another terminal:

```sh
build/gloinc --jit examples/http_client.gloin
```

For native execution, compile the client, restart the server, then run it:

```sh
build/gloinc -o /tmp/http-client examples/http_client.gloin
/tmp/http-client
```

For a TLS listener on the same port with a certificate for `localhost`, pass
`-- tls /path/to/ca.pem` after the source in JIT mode, or `tls /path/to/ca.pem`
to the executable. The automated [local client fixtures](../tests/http/README.md)
exercise both transports using an ephemeral port and a temporary certificate:

```sh
cmake --build build --target gloin_http_tests
ctest --test-dir build -R '^HttpClientSmoke\.' --output-on-failure
```

Those tests verify actual request bytes, binary bodies, chunked/fixed/EOF
responses, informational and bodyless responses, error status codes, malformed
framing, body limits, overall deadlines, certificate rejection, and connection
cleanup in JIT, `-O0`, and `-O2` modes. Decoder tests also cover every split point
and one-byte input. HTTP code uses normal source-typed GloinIR lowering; native
transport calls retain the existing verified `gloin.abi_call` boundary.

The [streaming acceptance fixture](../tests/http/README.md) checks bounded
uploads, early final responses, paused producers/consumers, partial consumption,
cancellation through aliases, truncation after delivered output, and the
32-exchange example, over both HTTP and HTTPS in JIT/`-O0`/`-O2` modes:

```sh
ctest --test-dir build -R '^HttpStreamSmoke\.' --output-on-failure
```

Decoder tests additionally vary every wire split and output capacities 1–3,
preserve following bytes, check the final-head boundary, and enforce total
body limits across repeated output reuse.
