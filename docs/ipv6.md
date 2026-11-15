# IPv6 over raw packets (0.3.0 development)

`@packet6` supplies packed IPv6 records and pseudo-header checksum construction.
These helpers operate on caller-owned bytes without allocating storage.
`@ipv6` and `@ip6_stack` build a bounded static-address host endpoint on them,
sharing TCP, HTTP and TLS with IPv4. The OS supplies a TUN/utun packet device;
OpenSSL supplies TLS and cryptographic services. The OS socket and DNS APIs
in `@net` remain IPv4.

| Type | Bytes | Representation |
| --- | ---: | --- |
| `Address` | 16 | `high: be_u64`, `low: be_u64` |
| `Header` | 40 | Version, DSCP/ECN, 20-bit flow label, big-endian payload length, Next Header, Hop Limit and two addresses |
| `FragmentHeader` | 8 | Next Header, reserved fields, 13-bit offset, More flag and big-endian identification |

All three records have alignment one. For example,
`packet6.Address { high: 0xfd00000000000000, low: 2 }` represents `fd00::2`.
`Address.unicast()` excludes unspecified and multicast addresses; it is not an
interface routing or address-assignment check.

`packet6.header(output, source, destination, protocol, length)` constructs a
40-byte base header with Hop Limit 64, zero traffic class/flow label, and the
specified total packet length. Output capacity must hold that total length;
only the header is written. Non-jumbo packets are limited to 65,575 bytes.
Use `memory.read_packed` / `write_packed` for explicit custom header values.
Reading a packed record alone does not validate the represented packet.

`transport_checksum(source, destination, protocol, bytes, omit)` includes both
full 128-bit addresses, the 32-bit transport length and protocol. Set `omit` to
`bytes.len` to verify a checksum (a valid checksum produces zero), or to the
start of the two-byte checksum field to compute it with that field zeroed.
The caller decides how to encode a computed zero for its transport protocol.

These representations follow [RFC 8200](https://www.rfc-editor.org/rfc/rfc8200.html).

## Bounded packet decoding

`packet6.parse(bytes)` returns a borrowed `Packet` bounded by the declared IPv6
payload length. Trailing capture bytes are excluded. It walks at most eight
extension headers and a 1,024-byte prefix, validates option extents, and exposes
the final protocol, payload offset and the byte that selected that protocol.
Hop-by-Hop Options must immediately follow the base header. Destination Options
honor unknown-option action bits. Exhausted routing headers can be skipped;
active routing instructions and routing type zero are rejected. Semantic option
errors carry discard/error information for callers to apply ICMP suppression.
Structural errors return `result` errors. Unknown transport protocols remain
visible; No Next Header is represented by protocol 59.

`tcp(bytes)` and `udp(bytes)` validate complete transport shape and the IPv6
pseudo-header checksum, including both full 128-bit addresses. Ordinary IPv6 UDP
requires a nonzero wire checksum; a computed zero is encoded as 65535.
`seal(bytes, protocol)` validates the packet before replacing the TCP, UDP or
ICMPv6 checksum. It leaves all other bytes, including capture padding, intact.

Atomic fragments are parsed independently. Other fragments expose their offset,
More flag and identification; transport codecs require complete datagrams.
`first_fragment_complete(packet)` checks whether an offset-zero fragment
contains every supported post-fragment extension and the complete upper-layer
header, without attempting transport checksum verification or reassembly.

All returned slices borrow caller storage. Keep those bytes live and unchanged
while using a decoded view. AH/ESP, jumbograms and executing routing headers are
unsupported; see the endpoint limits below.

## Raw packet transport

`net.PacketDevice` carries complete IPv4 or IPv6 packets over Linux TUN or
macOS utun. Reads now require **65,575 bytes** of capacity, including for IPv4
callers. Invalid/unsupported IP datagrams are discarded with `WOULD_BLOCK`;
resume the readiness loop. Writes validate the declared packet length.
The macOS adapter verifies/emits AF_INET or AF_INET6 in its four-byte prefix.
Device addresses and routes remain externally configured.

## Initial sequence numbers

`net.TcpIsnGenerator.next6(local, local_port, remote, remote_port, now_us)`
includes both complete 128-bit addresses and ports in HMAC-SHA256, using the
same per-generator private secret and four-microsecond clock as IPv4. It
rejects unspecified/multicast addresses, zero ports and backward clocks.
Construct the generator once per serial endpoint owner. Its native call passes
through a checked `gloin.abi_call`; the native ABI receives encoded address
bytes rather than assuming a host struct layout.

## TCP connections and tables

The shared `@tcp` engine accepts both families. Use `tcp.Address6`,
`Connection.connect6` / `accept6`, and `Table.create6` / `connect6` for IPv6.
Each connection/table has one address family and rejects mixed-family input.
Send/receive ownership and progress rules are unchanged; see [TCP](tcpip-server.md).
IPv6 SYN output requires at least 64 bytes. `Options.defaults6()` uses MSS 1440
for a 1500-byte link; an IPv6 peer omitting MSS defaults to 1220. Configure MSS
and output storage for the actual path.

`Connection.local_address()` and `remote_address()` now return
`result<Address>`. Their `local_address6()` / `remote_address6()` counterparts
return `result<Address6>`; wrong-family queries fail. Tables add
`remote_address6(handle)` alongside their existing IPv4 query. Tuple matching
and TIME_WAIT records retain all 128 address bits.

`Table.packet_too_big6(local_port, remote, sequence, mtu)` accepts a big-endian
32-bit MTU, checks the complete tuple and an outstanding TCP sequence, and only
reduces the estimate. IPv6 reductions below 1,280 bytes are ignored. Callers
must validate the invoking ICMP packet and quote before using this hook.

## Reassembly and UDP construction

`ipv6.Reassembly.create(storage, presence, timeout_ms)` borrows caller storage.
The presence map holds up to 65,535 bytes; storage must contain an additional
1,024 prefix bytes. The timeout must be between 1 and 60,000 milliseconds.
Fragments may arrive out of order. The first fragment must contain all extension
headers and the complete upper-layer header. Completion repairs the preceding
Next Header field and removes the Fragment header from the returned view.

Every overlap, including an identical duplicate, poisons the source/destination/
identification tuple until its original deadline. Contradictory final extents,
invalid first headers and inconsistent ECN also poison the tuple. New fragments
cannot prolong that deadline. Atomic fragments are processed independently,
even when they share the identification of a queued fragmented datagram.
Completed views borrow the reassembly storage until its next use. Copying a
reassembler aliases its buffers; drive one owner serially. Expiry and incomplete
first headers currently discard without generating ICMP diagnostics.

`ipv6.udp(output, source, destination, source_port, destination_port, payload)`
builds and checksums one UDP datagram. Payloads may contain up to 65,527 bytes;
output must hold the additional 48 header bytes. Destination port zero is
rejected. The helper does not fragment or perform packet I/O. Source payload
must remain unchanged while constructing the output.

## Static host endpoint and ICMPv6

`ip6_stack.Endpoint.create(host, tcp_ports, udp_ports, slots, fragments)` represents
one configured unicast address and borrows the supplied pools. Its `reassemble`,
`input`, `emit` and `next_timeout_ms` methods follow the IPv4 endpoint ownership
and progress rules in [the server guide](tcpip-server.md). `connections` exposes
the shared TCP table. UDP events borrow payload bytes and retain both ports and
the source address. No protocol engine allocates storage.

The endpoint handles ICMPv6 echo, UDP port-unreachable, unknown Next Header and
option/routing Parameter Problem replies. Error replies quote at most 1,232
bytes, fitting the minimum 1,280-byte IPv6 MTU. They are limited to ten per
1,000-millisecond window and suppressed for ICMP errors, redirects, fragments,
unspecified/multicast sources and multicast destinations. This endpoint does
not join multicast groups. A bounded structural walk after an option error
prevents replying to ICMP errors hidden behind further extension headers.
The suppression rules derive from [RFC 4443 §2.4](https://www.rfc-editor.org/rfc/rfc4443.html#section-2.4).

Packet Too Big processing validates the checksum, quoted local source, remote
address/ports, outstanding TCP sequence and a smaller MTU of at least 1,280
bytes. TCP then resegments queued data. The implementation only reduces the
estimate; it does not probe for increases or recover from filtered ICMP using
packetization-layer MTU discovery. See [RFC 8201](https://www.rfc-editor.org/rfc/rfc8201.html).

This bounded endpoint requires externally configured addresses and routes. It
has no Neighbor Discovery, Router Advertisements, SLAAC, Duplicate Address
Detection, multicast membership, forwarding, outbound fragmentation, AH/ESP or
jumbograms. Reassembly expiry and incomplete-header ICMP diagnostics are also
unimplemented. Other received ICMP errors do not currently terminate or report
errors to a connection. These limits preclude complete IPv6 node conformance.

## Running the example

Build `examples/ip_http_server.gloin` as described in the server guide. Its CLI is
`ip-http-server DEVICE [--ipv6] [CERT.pem KEY.pem]`. IPv4 serves `10.0.0.2:8080`;
`--ipv6` serves `[fd00::2]:8080`. One invocation uses one family. The same HTTP
application supports binary echo, keep-alive, pipelining, bounded concurrent
clients, deadlines and verified TLS.

The isolated Linux acceptance harness configures the kernel peer as `fd00::1`:

```sh
sudo unshare --net -- python3 tests/tcpip/interop.py --ipv6 \
  --server "$PWD/build/ip-http-server" --reports /tmp/gloin-ipv6
```

It exercises real kernel TCP and Python TLS over the TUN, and independently
checks ICMPv6 echo using `ping -6`. See [acceptance details](../tests/tcpip/README.md).
The macOS native framing path has automated tests; live utun interoperability
still requires separate interface configuration and validation.
