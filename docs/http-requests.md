# Incremental HTTP request decoding (0.3.0 development)

`@http` now provides `RequestDecoder` for streaming request bodies and
`RequestReader` for complete bodies that fit a caller-owned buffer. Both return
built-in results, perform no I/O, and allocate no heap storage. They can be driven
by the existing nonblocking TCP or TLS interfaces without threads. A reusable
server connection scheduler, response lifecycle, deadlines, and cancellation
remain separate work.

## API

| Call | Result |
| --- | --- |
| `http.RequestDecoder.create(head_storage: [u8], max_body_bytes: u64, max_metadata_bytes: u64)` | `result<http.RequestDecoder>` |
| `decoder.feed(bytes: string, eof: bool, output: [u8])` | `result<http.StreamProgress>`: `consumed`, `produced`, `head_ready`, `complete` |
| `decoder.head()` | `result<http.RequestInfo>`: `method`, `target`, `host`, `head`, `content_length`, `has_content_length`, `chunked` |
| `http.RequestReader.create(head_storage: [u8], body_storage: [u8], max_metadata_bytes: u64)` | `result<http.RequestReader>` |
| `reader.feed(bytes: string, eof: bool)` | `result<http.RequestProgress>`: `consumed`, `complete` |
| `reader.request()` | `result<http.Request>`: `method`, `target`, `host`, `head`, `body` |

For example, this complete program decodes a chunked binary request. The
three-byte body contains a NUL byte; chunk delimiters are absent from the payload.

```gloin
import "@http";
import "@strings";

def main() -> i32 {
    def mut head: [u8; 256] = zeroed;
    def mut body: [u8; 16] = zeroed;
    def made: result<http.RequestReader> = http.RequestReader.create(head[..], body[..], 512);
    if made.erroneous { return 1; }

    def mut reader: http.RequestReader = made.value;
    def fed: result<http.RequestProgress> = reader.feed(
        "POST /echo HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n3\r\na\0b\r\n0\r\n\r\n", true);
    if fed.erroneous { return 2; }
    if !fed.value.complete { return 3; }

    def request: result<http.Request> = reader.request();
    if request.erroneous { return 4; }
    if !strings.equal(request.value.body, "a\0b") { return 5; }
    return 0;
}
```

The [loopback example](../examples/network_http.gloin) sends a literal chunked
request over real nonblocking TCP and receives it in three-byte pieces. It runs
without an external service. The [one-request server](../examples/http_echo_server.gloin)
also accepts fixed-length and chunked POST bodies, with a 4 KiB decoded body limit
and its existing 30-second deadline. It deliberately closes after one request;
it is not a complete persistent HTTP server.

## Framing and limits

Requests use HTTP/1.1, an origin-form target beginning with `/`, and one nonempty
Host field. Host cannot contain whitespace, userinfo, a list, or URI path/query/
fragment delimiters. It is retained as text; this module does not resolve it or
validate application routing authority. CONNECT, absolute-form and asterisk-form
targets, other HTTP versions, and transfer codings other than a single `chunked`
are unsupported.

The decoder accepts a decimal Content-Length, a single chunked transfer coding,
or an empty body when both are absent. It never uses connection-close framing
for a request. Incoming HEAD requests follow these same request-body rules.
Conflicting lengths, duplicate transfer codings, and Transfer-Encoding together
with Content-Length are errors. Repeated Content-Length fields are accepted only
when their decimal values agree; comma lists, signs, and u64 overflow are rejected.
These boundaries follow [RFC 9112 section 6.3](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)
with deliberately restricted transfer-coding support.

The head must fit `head_storage`. The decoded body is bounded by `max_body_bytes`,
even when the same output buffer is reused repeatedly. `RequestReader` uses its
body buffer length as that limit. Zero body capacity is valid for empty bodies;
zero head capacity or metadata limit is invalid. The metadata budget covers the
head, chunk-size lines and extensions, chunk delimiters, and trailers together.
Each chunk-size or trailer line, including CRLF, is limited to 1,024 bytes.

Chunk extensions are validated and ignored. Trailer fields are validated and
discarded; they are never merged into the original head. Framing, Host, and
connection-control trailers are rejected. This preserves the separation required
by [RFC 9110 section 6.5.1](https://www.rfc-editor.org/rfc/rfc9110.html#section-6.5.1).
CRLF, token field names, and field-value control-byte rules are enforced for both
headers and trailers. Obsolete folded fields and bare line feeds are rejected.

## Driving a connection

`RequestDecoder.feed` stops immediately after validating the head, before
producing any body bytes. At that point `head_ready` is true and `head()` exposes
its fields and original header bytes. Inspect headers using `http.headers` to
apply routing, authorization, `Expect: 100-continue`, upgrade, or Connection
policy before feeding the body. The decoder sends no interim or final responses
and never switches protocols. `RequestReader` drives past this boundary to
buffer the body; use the streaming decoder when the application must decide
before receiving the body.

Retain all input beyond `consumed`. Output occupies `output[..produced]`; consume
it before reusing that buffer. A full or empty output window pauses body delivery
without discarding input. Chunk metadata can still advance without output space.
After completion, feeds consume and produce zero bytes. Any suffix belongs to a
following request or another application-level protocol decision. Finish using
the current views before creating a fresh reader for the next request.

`eof` means the transport ends after the supplied bytes. It takes effect only
when all those bytes are consumed. If the decoder pauses at the head or output
boundary, retain the unconsumed bytes and the EOF indication for the next feed.
EOF before complete framing is an error, including missing chunk terminators or
trailers. Bytes produced before completion are provisional: a later failure can
invalidate the message. After any decoding failure the instance stays failed;
`feed`, `head`, and `request` cannot recover it. Close or discard the connection's
input according to the server policy, rather than scanning for a new request.

## Ownership and cost

All buffers are caller-owned and must outlive the reader and its borrowed views.
Keep head, body/output, and input storage disjoint during a feed. Keep the head
unchanged while using `RequestInfo`, `Request`, or header iterators. Copies of a
reader alias its external buffers; do not drive both copies against that storage.
The streaming decoder's state includes a fixed 1 KiB chunk-line buffer. Whole-body
storage costs only the buffers supplied by the caller. No DNS, waiting, socket
operations, arena allocation, or hidden global connection state are involved.

Decoding is linear in wire bytes, with each consumed byte counted once. `head()`
revalidates its bounded saved head and costs O(head bytes); capture its result
once if repeated field access is needed. The original `parse_request` and
`frame_request` APIs remain fixed-length helpers and continue rejecting chunked
requests, so existing callers cannot mistake chunked bodies for empty ones.

## Validation

`NetworkLibraryTest` exercises JIT, native `-O0`, and native `-O2`: every split
point, binary bodies, output windows of zero through six bytes, pipelined suffixes,
exact head/body/metadata bounds, malformed framing and trailers, every truncated
prefix of valid messages, permanent failure, and preservation of surrounding
buffer bytes. The existing response reader/decoder tests cover the shared engine's
response-specific framing, including informational responses and clean-EOF bodies.

On 2026-10-29, all 1,117 core tests passed on macOS ARM64 with LLVM/MLIR 21.1.6,
including the loopback example and existing HTTP/HTTPS client suites. The program
above also runs verbatim in JIT, native `-O0`, and native `-O2`. Cross-platform,
sanitizer, and installed-package validation remain pending.
