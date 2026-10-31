# TCP/IP and HTTP/HTTPS servers in Gloin

The 0.3.0 development library implements an IPv4 endpoint and TCP byte streams
in Gloin. It accepts and emits raw IP packets. The operating system supplies a
packet device, clocks and cryptographic services; it does not implement TCP for
these connections. HTTP parsing and server state also run in Gloin. OpenSSL
implements TLS over the Gloin byte stream.

The [server example](../examples/ip_http_server.gloin) serves HTTP or HTTPS at
`10.0.0.2:8080`, with 64 concurrent application slots, separate retained
TIME_WAIT records, bounded request bodies, keep-alive, pipelining, deadlines,
and explicit cleanup. `/echo` returns the request body, `/bytes` returns a
65,536-byte deterministic binary body, and other paths return a short message.
It uses one serial event loop. A stalled client does not require a thread and
does not block other clients.

This is an IPv4 host implementation with a deliberately bounded feature set.
It does not implement IPv6, Ethernet/ARP, routing, IPsec, TCP SACK, window
scaling, timestamps, ECN negotiation, or an out-of-band urgent-data API. Urgent
payload bytes remain in the ordinary byte stream. TCP send/receive storage is
limited to 65,535 bytes per direction. UDP output is one datagram; callers must
respect the path MTU. This development implementation has interoperability and
adversarial tests, not a claim of exhaustive Internet-standard conformance.

## Modules and ownership

| Module | Responsibility |
| --- | --- |
| `@packet` | Packed IPv4/TCP/UDP headers, shape/checksum validation and checksum sealing. |
| `@ipv4` | Bounded fragment reassembly, UDP encoding, ICMP echo and unreachable replies. |
| `@tcp` | Connections, rings, receive reordering, sequence numbers, flow/congestion control, retransmission and close timers; generation-checked connection tables. |
| `@ip_stack` | Local unicast IPv4 dispatch, bounded reassembly pools, TCP listeners, UDP delivery, ICMP handling. |
| `@net` | OS socket APIs, raw `PacketDevice`, secret-dependent `TcpIsnGenerator`, verified TLS configurations and bounded `TlsBuffer`. |
| `@tcp_tls` | Retains partial encrypted input/output while adapting `TlsBuffer` to a TCP table handle. |
| `@http_server` | Transport-independent incremental HTTP/1.1 server sessions. |
| `@http_service` | Bounded HTTP/HTTPS progress over a Gloin TCP handle; suitable for round-robin scheduling. |

Packet layouts use `packed struct(msb_first)` and `be_u16`/`be_u32`. Numeric
operations work on values; packed reads and writes encode their representation.
There are no packet-buffer pointer casts or hand-maintained native structure
offsets. See [the packet API](packets.md).

Protocol objects borrow caller-owned buffers. Keep their storage alive, use
separate send/receive/presence/head/output regions, and drive one owner serially.
Copies of connections, tables, endpoints, sessions and adapters alias storage;
they are not independent connections. Input/output buffers must not overlap
protocol storage unless an API explicitly documents otherwise. Do not mutate
borrowed packet or header views while using them.

The pure packet/TCP/HTTP engines do not allocate. OpenSSL retains native session
allocations; `TlsBuffer` additionally bounds each encrypted BIO direction to the
requested capacity, from 1,024 to 1,048,576 bytes. Close TLS and native device
handles before resetting their owner arenas. The example resets one application
arena per finished connection, while TCP storage belongs to the longer-lived
endpoint arena.

## TCP connections and tables

`tcp.Connection.connect(local, remote, isn, buffers, options, now_ms)` starts an
active open. `accept(initial_syn, local, isn, buffers, options, now_ms)` creates a
passive connection. Addresses and ports are endian-qualified values. The caller
supplies send storage, receive storage, and a presence map with the same length
as receive storage.

- `input(packet, now_ms)` validates a packet and advances protocol state.
- `emit(output, now_ms)` drives timers and emits at most one packet; zero means
  no packet is ready. Output needs at least 44 bytes; normally provide room for
  the configured MSS plus the IP/TCP headers.
- `write(bytes)` copies the prefix that fits, returning the accepted count.
- `read(output)` returns `{count, end}`. Zero bytes with `end == false` means
  retry after progress. `end` becomes true only after the peer FIN and all
  preceding bytes have been consumed.
- `shutdown_write()` queues a FIN after queued data. It preserves the readable
  half of the stream.
- `cancel()` queues a reset when applicable. Drive `emit()` to flush it.
- `next_timeout_ms(now_ms)` gives the next timer/progress deadline. The maximum
  `u64` denotes no scheduled timer. It does not sleep.

All supplied clocks must share one monotonic millisecond domain. Backward clocks
return errors. `Options.defaults()` uses MSS 1460, 12 retries, a 300-second idle
limit, a 120-second TIME_WAIT interval, and Nagle enabled. Handshake, idle,
retransmission, zero-window persistence and close state are bounded. Tests that
shorten TIME_WAIT must account for the peer's retransmission backoff.

TCP uses sequence arithmetic modulo 2^32, immediate ACKs, a bounded out-of-order
receive ring with first-arrival-wins byte overlap, MSS negotiation, slow start,
congestion avoidance, fast retransmission/recovery, RTT-based retransmission
with Karn sampling, exponential backoff, and zero-window probes. Data is
resegmented after a validated ICMP fragmentation-needed quote. Quotes must match
the four-tuple and an outstanding sequence. Old-style zero-MTU messages use the
RFC 1191 plateau list. The implementation only reduces the path estimate during
a connection; it does not probe for increases or implement packetization-layer
MTU discovery when ICMP is filtered.

`tcp.Table.create(host, listener_ports, slots)` supplies bounded demultiplexing
and round-robin packet emission. Construct each `Slot` with its own buffers.
`input(bytes, isn, now_ms, reply_output)` returns a `tcp.Input` with a handle,
a `created` flag, and a possible immediate closed-port reply. A zero handle
generation means no connection. Retain/send that reply before reusing its
buffer. Table methods validate `{index, generation}` on every application
operation; old handles cannot access a replacement connection.

Attach optional `time_wait_storage(records)` before opening connections.
`retire_time_wait(handle)` moves a drained TIME_WAIT connection into this small
record pool and frees its full connection slot. It retains tuple protection,
final ACK state, duplicate-FIN responses and expiry. A full pool leaves the
original connection intact. Otherwise `release(handle)` is permitted only for
terminal connections with no reset pending. Applications must finish consuming
receive data before release. Handle generations never wrap into reuse.

For real traffic use `net.TcpIsnGenerator.create()` once per endpoint owner and
`next(local, local_port, remote, remote_port, now_us)` for new connections. It
uses a private 256-bit random secret, HMAC-SHA256 over the tuple, and a
four-microsecond monotonic clock. Explicit ISNs remain useful for deterministic
simulations. Do not replace the generator with simulation PRNGs or a constant.

## IPv4 endpoint and raw transport

`ipv4.Reassembly.create(storage, presence, timeout_ms)` retains one fragmented
datagram. Storage needs 60 prefix bytes plus the presence-map capacity, with at
most 65,515 payload bytes. Fragments can arrive in any order. Fully covered,
identical payload retransmissions are accepted; conflicting bytes or partial
overlap discard the assembly. Header options come from the first fragment.
The deadline starts with the first received fragment and duplicates do not
extend it. Completed bytes borrow reassembly storage until its next use.

`ip_stack.Endpoint.create(host, tcp_ports, udp_ports, slots, fragments)` dispatches
local unicast packets. UDP events borrow a validated payload and retain source
and destination ports. Closed UDP ports generate ICMP port-unreachable replies;
unknown protocols generate protocol-unreachable replies. Echo replies preserve
identifiers, sequences and binary payload. Nonlocal packets are ignored; this
is not an IP forwarder.

`reassemble(bytes, now_ms)` is public so callers can obtain a complete TCP SYN
before selecting a tuple-dependent ISN. Then call `input(datagram, isn, now_ms,
output)`. `input` also accepts fragments directly when the caller supplies an
ISN policy suitable for that use. `emit` and `next_timeout_ms` service TCP and
reassembly deadlines. Packet errors are per-input errors and do not terminate
unrelated connections. Storage exhaustion returns `WOULD_BLOCK` without
allocating an unbounded queue.

`net.PacketDevice.open(owner, name)` uses a named Linux TUN with `IFF_NO_PI`, or
a named macOS `utunN` device. It never changes interface addresses or routes.
Reads require 65,535 bytes of output capacity and return at most one IPv4 packet.
Writes preserve packet boundaries; retry the same packet after `WOULD_BLOCK`.
`wait(events, timeout_ms)` provides explicit readiness waits. The macOS adapter
adds/removes its four-byte address-family prefix. Device access/configuration
can require OS privileges.

Build the example with:

```sh
build/gloinc -O2 -o build/ip-http-server examples/ip_http_server.gloin
```

The Linux acceptance harness configures a TUN only inside a private network
namespace and talks to Gloin using ordinary kernel TCP sockets:

```sh
sudo unshare --net -- python3 tests/tcpip/interop.py \
  --server "$PWD/build/ip-http-server" --reports /tmp/gloin-tcpip
```

The harness refuses the initial network namespace. It generates a temporary
localhost certificate and verifies both HTTP and HTTPS without public services.
For manual TLS use, pass the certificate and key after the device name. The
certificate must cover the hostname used by clients. The macOS adapter is
compiled and its framing tested; actual utun interface configuration is an
external deployment step.

## HTTP session and application flow

Create `http_server.Session` with head storage, output storage, explicit `Limits`
and a monotonic timestamp. Limits bound body bytes, all request metadata,
request/response/idle time, and requests per connection. Request deadlines are
absolute: a slow drip of bytes cannot extend them indefinitely.

1. Feed input and retain the exact unconsumed suffix. The session pauses at
   `HEAD`, even for a bodyless request. Inspect `head()` and `expected()`.
2. Call `accept_body()` to proceed. Supported `Expect: 100-continue` queues an
   interim response. An unsupported expectation must receive a final rejection.
   Alternatively begin a final response immediately; an undrained request forces
   `Connection: close`.
3. Consume body output until `body_complete()`. Streaming output is provisional
   until framing and trailers finish successfully. Chunked metadata and trailers
   remain subject to the decoder's limits and validation.
4. `begin_response(code, reason, headers, length, chunked, close, now_ms)` queues
   the final head. Drain it before writing body bytes. Framing/connection headers
   are library-owned, and invalid names/values or header injection are rejected.
5. `write_body(bytes)` returns the copied count, or zero while output is pending.
   Fixed-length responses reject overflow; completion requires the exact length.
   Chunked responses finish with `finish_body()` and a final zero chunk.
6. Send `output()` and acknowledge exactly the accepted prefix with
   `consume_output(count)`. After `COMPLETE`, release borrowed request views and
   call `advance(now_ms)`. A reusable session returns to `REQUEST`; a closing
   session becomes `CLOSED`. Pipelined requests are processed in wire order.

HEAD and 304 responses can carry representation lengths without bodies. 204
omits Content-Length; 205 requires zero length. The server supports HTTP/1.1
final responses and `100 Continue`, not protocol upgrades or CONNECT tunnels.
Content encodings, routing, authentication, compression and application Date
headers remain application concerns.

`http_service.Connection.create` combines a session, TCP handle, an optional
caller-owned `tcp_tls.Stream`, input storage and handshake/close deadlines.
`poll(table, body_storage, now_ms)` performs bounded work and returns one event:
`WAIT`, `HAVE_HEAD`, `HAVE_BODY`, `REQUEST_COMPLETE`, `NEED_RESPONSE`,
`RESPONSE_COMPLETE`, or `DONE`. Inspect the event and `body_bytes`; the final
body chunk can arrive before the later `REQUEST_COMPLETE` event. Drive many
connections round-robin. A progress event does not require an OS thread.

On any driver error, call `cancel(table)` before releasing its buffers or arena.
After `DONE`, native TLS is closed and the TCP FIN is queued; continue driving
the endpoint until TCP is terminal or can be retired into TIME_WAIT storage.
Keep pending raw packets until the device accepts them. Do not reuse an output
buffer merely because the application response has completed.

`net.TlsBuffer` exposes `encrypted_input`/`encrypted_output` as well as ordinary
handshake/read/write/shutdown. Partial encrypted input must be retained, and
EOF is fed exactly once after its bytes. Drain output even when TLS wants input.
A blocked plaintext write must be retried with the same bytes and length.
`tcp_tls.Stream` implements these rules. Missing `close_notify` is a TLS error,
not a clean end of HTTP input.

## Validation and measurements

Tests include independent TCP sequence/window/checksum oracles, sequence wrap,
24 seeded bidirectional loss/reordering/duplication simulations, simultaneous
open, zero-window recovery, retransmission, half-close, TIME_WAIT retirement,
handle reuse, fragment ambiguity, raw-device framing, and TLS certificate/EOF
checks. HTTP/HTTPS tests exercise Gloin TCP in JIT/O0/O2 with small buffers and
packet loss. Linux kernel peers additionally test binary echo, chunked uploads,
100 Continue, pipelining, HEAD, large responses, concurrent/stalled clients,
connection churn, and malformed framing.

The [measurement records](benchmarks/tcpip-2026-10-30/) include a parser comparison
against the earlier installed package and complete-server measurements. The
server benchmark validates every response and records latency, throughput,
server CPU/RSS and client CPU. It uses Linux kernel clients and a TUN within a
VM; its results describe that workload and include client and virtual-device
costs. They are not a claim about physical NIC capacity or production latency.

The [2026-10-31 validation record](benchmarks/tcpip-2026-10-30/validation.json)
records 1,132 passing macOS core tests, 37 Linux networking/TLS tests, 13 native
ASan/UBSan tests, and 44 relocated-package network/packed/endian checks. Live
raw-device interoperability was exercised on Linux; macOS utun framing was
compiled and tested using datagram pairs without changing host interfaces.

Protocol references: [TCP, RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html),
[congestion control, RFC 5681](https://www.rfc-editor.org/rfc/rfc5681.html),
[retransmission timers, RFC 6298](https://www.rfc-editor.org/rfc/rfc6298.html),
[path MTU, RFC 1191](https://www.rfc-editor.org/rfc/rfc1191.html),
[HTTP/1.1 framing, RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html), and
[OpenSSL bounded BIO pairs](https://docs.openssl.org/3.5/man3/BIO_s_bio/).
