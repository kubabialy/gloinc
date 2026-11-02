# IPv6 wire codecs (0.3.0 development)

`@packet6` supplies packed IPv6 records and pseudo-header checksum construction.
These helpers operate on caller-owned bytes without allocating storage.

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
unsupported; these codecs do not constitute a complete IPv6 host endpoint.
