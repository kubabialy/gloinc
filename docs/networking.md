# Nonblocking TCP, TLS, and HTTP/1.1 (0.1.0 candidate)

`@net` provides nonblocking IPv4 TCP sockets on Apple Silicon macOS and Ubuntu
24.04 Linux. `@http` parses and formats bounded HTTP/1.1 message heads. The
[local round-trip example](../examples/network_http.gloin) compiles with
`gloinc -o network_http examples/network_http.gloin` and runs without a network
service. It uses built-in `result<T>` for errors throughout both modules.

## Socket ownership and addresses

`net.Socket.open(&owner) -> result<net.Socket>` creates a nonblocking,
close-on-exec IPv4 TCP socket. Each socket has a small shared state object in
`owner`. Copying `net.Socket` aliases that state: a successful `close()` makes
every copy report `is_open() == false`. Call `close()` exactly once **before**
resetting or freeing the owner arena. Arena cleanup does not close an OS socket.
`accept(&owner)` places the accepted socket's state in the supplied arena. A
failed open or accept leaves no open native descriptor. An unavailable accept
does not allocate; metadata is allocated only after a connection is accepted.
Neither module opens a
connection, waits, or allocates in the background.

`Socket.set_reuse_address(true) -> result<void>` explicitly enables
`SO_REUSEADDR`; call it before `bind` on a listener that may restart on the
same local address. It does not permit a second active listener to take over
an address already in use. The setting is per socket and is not enabled by
default. A server may instead bind port zero and use `local_address()` to
discover the assigned port. Format its typed `u16` port with
`std.to_string_u16(&memory, address.port)` or `std.format_u16`.

`net.Address` contains `host: u32` and `port: u16`. `host` is a host-order IPv4
number: `127.0.0.1` is `2130706433`. Use `net.Address.ipv4(127, 0, 0, 1, port)
-> result<net.Address>` for checked octets, `net.Address.loopback(port)` for local
connections, or `net.Address.any(port)` for a listener on all interfaces. Port
zero requests an OS-assigned local port; obtain it with `local_address()` after
binding. Blocking IPv4 hostname lookup is described below; asynchronous DNS,
IPv6, UDP, and Windows sockets remain outside this API. Exposing a listener
with `Address.any` is an explicit decision;
the example binds only to loopback.

```gloin
import "@arena";
import "@net";

def cleanup(socket: &net.Socket) -> void {
    if !socket.is_open() { return; }
    def closed: result<void> = socket.close();
    if closed.erroneous { return; }
}

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def opened: result<net.Socket> = net.Socket.open(&owner);
    if opened.erroneous { return 1; }
    def mut listener: net.Socket = opened.value;
    defer cleanup(&listener);
    def bound: result<void> = listener.bind(net.Address.loopback(0));
    if bound.erroneous { return 2; }
    def listening: result<void> = listener.listen(16);
    if listening.erroneous { return 3; }
    def local: result<net.Address> = listener.local_address();
    if local.erroneous { return 4; }
    def closed: result<void> = listener.close();
    if closed.erroneous { return 5; }
    return 0;
}
```

The short sketch reports failures by exit code. Its cleanup helper closes the
socket on early returns before the owner arena is freed.

## Connection and I/O states

Operations that can fail return built-in `result<T>` or `result<void>`. A
system-call failure returns a static `error` message naming the operation.
Gloin's current `error` type does not carry `errno`, source location, or debug
metadata; the raw runtime C ABI still reports `errno` to its checked `@net`
wrapper. This limitation is explicit so callers do not assume a detailed OS
diagnostic is available from `.error` yet.

Expected network conditions are **success values**:

| Call | Successful payload | Failure |
| --- | --- | --- |
| `Socket.connect(address)` | `bool`: `true` if connected, `false` if pending | `error` on syscall failure |
| `Socket.finish_connect()` | `bool`: `true` when connected, `false` while pending | `error` after failed connection |
| `Socket.wait(events, timeout_ms)` | `i32` readiness bits; zero if not ready by deadline | `error` on invalid arguments or poll failure |
| `Socket.accept(&owner)` | `AcceptOutcome`: `available` and, if true, an owned `socket` and `peer` | `error` on syscall or allocation failure |
| `Socket.read(destination: [u8])` | `ReadOutcome`: `state` is `status.OK`, `status.END`, or `status.WOULD_BLOCK`; `count` is bytes received | `error` on syscall failure |
| `Socket.write(bytes: [const u8])`, `write_text(text)` | `WriteOutcome`: `count` accepted by one send; `would_block` means retry later | `error` on syscall failure |
| `Socket.bind`, `listen`, `close` | `result<void>` success | `error` on failure |
| `Socket.set_reuse_address(enabled)` | `result<void>` success | `error` on failure |

`net.READABLE` is `1`, `net.WRITABLE` is `2`; pass either or both (`3`) to
`wait`. A zero timeout polls immediately. A positive timeout can block for
that many milliseconds. `wait` is a readiness hint, so
`accept`, `read`, and
`write` must still handle an unavailable outcome. `connect` returning `false`
requires a writable wait followed by `finish_connect`; after a connection
error, close that socket. A zero-length read destination is an error. An empty
write succeeds with `count == 0`. `read` returning `status.END` with zero bytes
means the peer performed an orderly shutdown.

## Several sockets and half close

`net.wait_many(sockets, requested, ready, descriptors, timeout_ms) -> result<u64>`
performs one `poll` across several sockets and returns the number with matching
readiness. The four slices must have the same nonzero length and every watched
socket must be distinct. A call accepts at most 4096 sockets. `requested` holds
`READABLE`, `WRITABLE`, or both for each socket; `ready` receives the matching
bits in the same order. `descriptors` is caller-owned `i32` scratch of the same
length. Keep every watched socket open and all four slices live until the call
returns. A zero result means the timeout expired or a signal interrupted the
wait; retry against the caller's own overall deadline. Up to 64 sockets use
bounded stack scratch; larger sets make one temporary native allocation
proportional to their size. It does not retain a watch list or take socket ownership.
Closing an aliased socket between calls makes the next call fail. The
[local round-trip example](../examples/network_http.gloin) exercises this API.

`Socket.shutdown_write() -> result<void>` sends an orderly FIN after queued
bytes and leaves the read half usable. A second shutdown or any later write,
including an empty write, is an error. Every alias observes that state. Still
call `close()` before freeing the owner arena. A peer may need a readable wait
before its next `read` reports `status.END`.

`net.resolve_ipv4(host, hosts: [u32]) -> result<u64>` fills caller-owned host-order
IPv4 numbers in resolver order and returns their count. A numeric address such
as `"127.0.0.1"` works without external DNS. A hostname may invoke DNS and
**block without a timeout**; call it outside a latency-sensitive event loop.
The output must be nonempty and large enough for the entire answer. An empty,
oversized, or NUL-containing hostname fails; a too-small output fails without
partially populating it. Build `net.Address { host: hosts[i], port: port }` for
each candidate and close a failed connection before trying the next. This
resolver only returns IPv4 addresses; IPv6 and asynchronous DNS remain future
work. The [networking milestones](networking-roadmap.md) track that work.

Each read and write performs **one** native call. A successful write can accept
fewer bytes than requested. Advance the byte slice or text view by `count`,
then retry after writability when `would_block` is true. Do not loop without a
deadline or other cancellation policy. `write_text` sends the string's counted
bytes, including NUL, without copying; the source must stay live during the
call. `strings.view_bytes(buffer[0..count])` creates a borrowed, allocation-free
string view over a byte buffer for parsing; do not mutate or release the buffer
while that view is in use.

| Operation | Work and retained storage |
| --- | --- |
| `Socket.open`, successful `accept` | One OS descriptor and one arena state object until explicit close and arena reset/free |
| Unavailable `accept`, `connect`, `finish_connect`, `bind`, `listen`, `local_address`, `close` | O(1) plus one native call; no new Gloin allocation |
| `wait(events, timeout_ms)` | O(1) native poll; may wait up to the supplied timeout; no allocation |
| `wait_many` | O(n²) duplicate validation, one native poll; stack scratch through 64 sockets, one temporary allocation above that, n ≤ 4096 |
| `resolve_ipv4` | One synchronous `getaddrinfo` call with native temporary storage; may block on DNS; fills caller storage only on success |
| `read`, `write`, `write_text` | One native call; work proportional to bytes transferred, no allocation or retained input view |

The native layer uses `O_NONBLOCK`, close-on-exec, and signal-safe send behavior.
See [Linux socket semantics](https://man7.org/linux/man-pages/man7/socket.7.html)
for the `EINPROGRESS`/readiness pattern and
[Linux connect semantics](https://man7.org/linux/man-pages/man2/connect.2.html)
for the `SO_ERROR` completion check. The macOS runtime sets `SO_NOSIGPIPE`;
Linux sends with `MSG_NOSIGNAL`.
Calls from `@net` cross the verified `gloin.abi_call` boundary in GloinIR;
the public `result<T>` and outcome values remain source-typed until the
explicit runtime ABI lowering. The native runtime is kept LLVM-independent;
TLS uses OpenSSL 3.

## Verified TLS client

`Socket.start_tls_client(&owner, hostname, trust_file) -> result<net.TlsClient>`
starts a TLS client on a **connected** TCP socket. A nonempty `trust_file` names
a PEM CA bundle; `""` uses OpenSSL's configured default trust paths. The
certificate chain and the expected DNS hostname or IP address are verified.
DNS names are sent with SNI. There is no API to skip verification. The TLS
configuration requires TLS 1.2 or newer.

`TlsClient.handshake() -> result<i32>` performs one nonblocking step. Zero
means the handshake and peer verification finished. Otherwise the value is
`net.READABLE` or `net.WRITABLE`; wait on the underlying socket, then retry.
`TlsClient.read(destination)` returns `TlsReadOutcome { state, count,
wait_for }`; `state` is `status.OK`, `status.END` for a TLS close notification,
or `status.WOULD_BLOCK`. `TlsClient.write(bytes)` returns `TlsWriteOutcome {
count, wait_for }`. On `wait_for != 0`, wait and retry with the **same bytes and
length**. On a successful short write, advance by `count`. A TCP EOF without
a TLS close notification is an error rather than a clean TLS end. All loops
need caller-chosen deadlines and cancellation. After a successful TLS read,
try another read before waiting: OpenSSL may already hold decrypted bytes even
when the socket is no longer readable. OpenSSL may allocate internally.

Copies of `TlsClient` alias one native TLS session. Close the TLS client once,
then close its socket, then reset/free the arena. Raw `Socket.read`, `write`,
`write_text` and `shutdown_write` reject use after TLS has started, including
after TLS cleanup. Socket close requires TLS cleanup first; no plaintext reuse
or second TLS session is supported. `Socket.wait` and `net.wait_many` remain
available for readiness. `close()`
sends a best-effort TLS close notification and releases TLS state; it does not
wait for the peer's notification. For bidirectional completion use the new
`shutdown() -> result<i32>`: zero means both alerts were exchanged; otherwise
wait for the returned readiness event and retry with a deadline. Consume expected
application input first. Shutdown prohibits further writes, and fatal TLS errors
allow only cleanup. The API
currently exposes static `error` messages, not OpenSSL's detailed error stack.
The [Gloin TLS driver](../tests/tls/README.md) covers a trusted peer,
wrong-host rejection, untrusted-peer rejection and graceful shutdown in JIT,
`-O0` and `-O2`. Native tests also use independent direct-OpenSSL peers.
`gloinc -o` links OpenSSL automatically for TLS programs. If linking a
`--emit-object` output yourself, place `libgloin_runtime.a` before the OpenSSL
SSL and Crypto libraries on the link command.

## Server TLS

Load `net.TlsServerConfig` from a PEM chain and unencrypted private-key file,
then call `accepted_socket.start_tls_server(&owner, &config)`. Each returned
`TlsServer` supports handshake, encrypted read/write, graceful shutdown and
explicit cleanup. A configuration is reusable and existing sessions survive
its closure. Read the [server TLS guide](server-tls.md) for the full API,
ownership, retry rules, restrictions and a runnable loopback server example.

## HTTP/1.1 parsing and clients

`@http` performs no I/O. Its `parse_request(bytes, max_header_bytes)` and
`parse_response(bytes, max_header_bytes)` return built-in results whose successful
payload has `complete == false` until the head is available. Returned method,
target, Host, and header views borrow the input. Keep that storage live and
unchanged. Parsing bounds its head scan and requires CRLF and valid field syntax.

Request parsing requires a Host field and an origin-form target. It supports
Content-Length bodies; absent Content-Length means an empty request body.
Chunked **request** parsing remains unsupported. `frame_request(bytes,
max_header_bytes, max_body_bytes)` returns an allocation-free borrowed body and
consumed offset once the entire fixed-length request is available.

Response parsing now recognizes a single chunked transfer coding and bodyless
status codes. Use `http.ResponseReader` to incrementally decode a bounded response
with Content-Length, chunked, or clean-EOF framing and the request method's HEAD
rule. It also handles informational responses before the final head. The older
`frame_response` helper is still a fixed-length helper: it accepts intrinsically
bodyless status codes but requires Content-Length for other responses. It does
not apply HEAD semantics or decode chunked/EOF bodies.

`http.format_request(&arena, method, host, target, headers, content_length,
max_bytes)` supports application headers and derives a fixed framing contract.
It owns Host, Content-Length, and Connection: close and rejects header injection
or attempts to override framing. `http.headers` supplies a validated iterator
that preserves header order and duplicates. All fallible public operations use
built-in `result<T>`.

`format_get` remains a small GET-head helper. `format_response` formats a
fixed-length response head and still rejects 1xx, 204, 205, and 304 because it
does not implement their sender-side body rules. Both use caller-owned arena
storage and perform no I/O. Send exactly the declared body length and handle
short writes.

`@http_client` adds a streaming HTTP/HTTPS `Exchange` state machine over the
nonblocking transport, with reusable buffers, producer/consumer backpressure,
cancellation, and early final responses during uploads. Its synchronous
`request` wrapper drives the same exchange. It accepts an explicit address, verified TLS settings,
request fields, output buffers, metadata bounds, and one overall timeout.
It closes its connection on completion or failure. The [HTTP client guide](http-client.md)
provides complete programs, ownership and allocation costs, supported framing,
and explicit limits. The [runnable client](../examples/http_client.gloin) sends
a JSON POST to a local service. The [streaming example](../examples/http_stream.gloin)
drives 32 connections through `net.wait_many`. No URL parser,
decompression, redirects, or pooling is implied. The separate [JSON module](json.md)
provides bounded construction and reading of complete buffered payloads.

Framing follows the supported subset of
[RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) and response semantics in
[RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html). The separate
`gloin_examples/http_server` example still handles sequential clients. The
[networking milestones](networking-roadmap.md) track concurrent servers,
asynchronous DNS, IPv6, HTTPS services, and WebSockets.
