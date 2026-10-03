# Nonblocking TCP and HTTP/1.1 (0.1.0 candidate)

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

`net.Address` contains `host: u32` and `port: u16`. `host` is a host-order IPv4
number: `127.0.0.1` is `2130706433`. Use `net.Address.ipv4(127, 0, 0, 1, port)
-> result<net.Address>` for checked octets, `net.Address.loopback(port)` for local
connections, or `net.Address.any(port)` for a listener on all interfaces. Port
zero requests an OS-assigned local port; obtain it with `local_address()` after
binding. DNS, IPv6, UDP, TLS, and cross-platform Windows sockets are outside
this first API. Exposing a listener with `Address.any` is an explicit decision;
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

`net.READABLE` is `1`, `net.WRITABLE` is `2`; pass either or both (`3`) to
`wait`. A zero timeout polls immediately. A positive timeout can block for
that many milliseconds. This first API waits on one socket per call; it does
not yet provide a multi-socket event queue. `wait` is a readiness hint, so
`accept`, `read`, and
`write` must still handle an unavailable outcome. `connect` returning `false`
requires a writable wait followed by `finish_connect`; after a connection
error, close that socket. A zero-length read destination is an error. An empty
write succeeds with `count == 0`. `read` returning `status.END` with zero bytes
means the peer performed an orderly shutdown.

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
| `read`, `write`, `write_text` | One native call; work proportional to bytes transferred, no allocation or retained input view |

The native layer uses `O_NONBLOCK`, close-on-exec, and signal-safe send behavior.
See [Linux socket semantics](https://man7.org/linux/man-pages/man7/socket.7.html)
for the `EINPROGRESS`/readiness pattern and
[Linux connect semantics](https://man7.org/linux/man-pages/man2/connect.2.html)
for the `SO_ERROR` completion check. The macOS runtime sets `SO_NOSIGPIPE`;
Linux sends with `MSG_NOSIGNAL`.
Calls from `@net` cross the verified `gloin.abi_call` boundary in GloinIR;
the public `result<T>` and outcome values remain source-typed until the
explicit runtime ABI lowering. The native runtime is kept LLVM-independent.

## HTTP/1.1 heads

`@http` performs no I/O. Feed newly received bytes into a caller-owned buffer
and pass a counted view to `parse_request(bytes, max_header_bytes)` or
`parse_response(bytes, max_header_bytes)`. Both return `result<...>`.
On successful but incomplete input, the payload has `complete == false`;
append more data and parse again. On a complete head, `header_bytes` is the
number of bytes to consume before the body or next pipelined message. The
returned method, target, and Host views borrow the input buffer. Keep it live
and unchanged until those fields are consumed.

The parser requires CRLF line endings, rejects malformed headers, NUL/control
characters, conflicting `Content-Length` values, and any
`Transfer-Encoding`. Request parsing requires an HTTP/1.1 request line and a
nonempty Host header. Absent request `Content-Length` means a zero-length
body. A response reports `has_content_length`; a false value means the caller
must choose a body framing policy, such as reading until EOF. The library does
not decode chunked bodies, handle upgrades, perform URL parsing, or provide an
HTTP connection manager. A caller must cap both head and body lengths and
enforce its own deadlines. These restrictions are deliberate while packed
structs and explicit endian facilities are still forthcoming. The framing
rules follow [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html); this is a
strict, limited subset rather than a complete RFC implementation.

For a complete fixed-length message, use `frame_request(bytes,
max_header_bytes, max_body_bytes)` or `frame_response(...)`. They return
`result<http.Frame>`. A successful `Frame` with `complete == false` needs more
input; a complete frame has a borrowed `body` and `consumed` byte offset so
the caller can retain any following message. `body` is counted bytes and can
contain NUL or invalid UTF-8. The body limit is checked before
returning a complete frame. Requests without Content-Length have empty bodies.
`frame_response` requires Content-Length; a response framed by EOF requires
the caller to use `parse_response` and manage that EOF itself. The framing
helpers allocate nothing and never read from the socket.

`format_get(&arena, host, target, max_bytes) -> result<string>` produces a GET
head with `Connection: close`. `format_response(&arena, code, reason,
content_type, content_length, max_bytes) -> result<string>` produces a fixed
length response head. Both reject line-break injection in caller-supplied
fields, use caller-owned arena storage, and perform no socket I/O. The caller
must send exactly the declared body length and handle short writes. Each
formatter allocates a builder buffer sized to the complete head and a final
copy in the arena, after checking `max_bytes`;
numeric response formatting can allocate small additional strings. Parsing
allocates nothing and scans at most the supplied header bound. The local
example demonstrates a complete request/response exchange with explicit
`result<T>` handling.
Informational, 204, 205, and 304 responses have body rules outside this first
fixed-length helper and are rejected by both `parse_response` and
`format_response`. A caller handling `HEAD` must likewise apply the request
method's no-body rule separately. See
[RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html) for those semantics.
