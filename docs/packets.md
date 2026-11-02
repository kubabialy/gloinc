# Packed network packets (0.3.0 development)

`@packet` supplies byte-exact IPv4, UDP, and TCP header types, bounded packet
validation, and checksum generation. It uses Gloin packed structs and endian
integers directly. It performs no allocation or I/O. `@net` continues to provide
OS TCP sockets and TLS; these packet APIs work on captured or caller-constructed
IPv4 datagrams. Writing an IP datagram to a normal TCP socket sends it as
application data and does not inject an IP packet.

The runnable [packet example](../examples/packet_headers.gloin) constructs an
unaligned IPv4/UDP datagram with a binary payload, seals its checksums, and reads
it back. Run `build/gloinc --jit examples/packet_headers.gloin`, or compile it with
`build/gloinc -O2 -o packet-example examples/packet_headers.gloin`.

## Memory representation and bounded access

| Type | Fixed bytes | Representation |
| --- | ---: | --- |
| `packet.Ipv4Header` | 20 | Version/IHL, DSCP/ECN, flags/13-bit fragment offset, and big-endian lengths, identification, checksum, and addresses |
| `packet.UdpHeader` | 8 | Big-endian source/destination ports, length, checksum |
| `packet.TcpHeader` | 20 | Big-endian ports, sequence/acknowledgment numbers, window/checksum/urgent pointer; explicit data-offset and control bitfields |

All three types have alignment one. They describe only the fixed prefix; options
and payloads are separately bounded slices. `Ipv4Header.header_words` and
`TcpHeader.header_words` count four-byte words. Fragment offsets count eight-byte
units. TCP flags are individual `u1` fields named `cwr`, `ece`, `urg`, `ack`, `psh`,
`rst`, `syn`, and `fin`; the remaining four bits are exposed as `reserved`.

The reusable memory bridge also works with user-defined packed records:

| Call | Contract |
| --- | --- |
| `memory.read_packed<T>(bytes: [const u8]) -> result<T>` | Copies the first `size_of<T>()` bytes into an independent value |
| `memory.write_packed<T>(bytes: [u8], value: T) -> result<void>` | Copies the value's complete representation to the prefix; preserves the suffix |

`T` must be a nonempty packed struct. Native structs, scalar integers, pointers,
and arbitrary arrays are compile-time errors. Byte slices may start at any
alignment. Short slices return `OUT_OF_RANGE` with OS error zero before accessing
storage; failed writes leave the destination unchanged. Reads preserve spare
bits, and writes preserve the value's existing bytes. Overlap is safe because
the value argument is captured before the destination is changed. Construction
and field assignment still encode according to the declared field types.

These APIs do not return reinterpreted pointers or validate packet semantics.
All packed bit patterns are representable; only the protocol decoder can decide
whether their field values describe a valid packet. As with other slices, the
caller must supply live storage of the declared length.

The compiler's private `gloin.raw_packed_pointer` operation retains the nominal
packed type and pointer mutability until lowering. Its verifier rejects native
layouts and const removal. It lowers to the existing address; ordinary typed
aggregate loads/copies preserve the representation without per-field encoding.
The standard-library wrappers check the length before forming that address.

## Decoding

| Call | Result |
| --- | --- |
| `packet.parse_ipv4(bytes: [const u8])` | `result<packet.Ipv4Packet>` |
| `packet.parse_udp_ipv4(bytes: [const u8])` | `result<packet.UdpPacket>` |
| `packet.parse_tcp_ipv4(bytes: [const u8])` | `result<packet.TcpPacket>` |

Every input begins at the IPv4 header, without an Ethernet header. `Ipv4Packet`
contains `header`, `bytes`, `options`, and `payload`. `bytes` ends at the declared
IPv4 total length; trailing capture padding or concatenated input remains outside
it. `UdpPacket` contains `ip`, `header`, `payload`, and `checksum_present`.
`TcpPacket` contains `ip`, `header`, `options`, and `payload`.

`packet.tcp_segment(bytes)` accepts bytes starting at a TCP header and returns
`result<packet.TcpSegment>` with `header`, `options`, and `payload`. It checks
the data offset and option framing, but does not verify a checksum or an IP
header. The IP-specific codecs use this shared structural validator and then
apply their own pseudo-header checksum checks. Its slices borrow the input.

Headers are snapshots. Returned slices borrow the input and require it to remain
live and unchanged while the parsed packet is in use. Copying a packet view
copies these borrows. Reparse after mutating backing storage; a snapshot does
not track later changes. Parsers do not trust caller-assembled view structs:
the public transport decoders always start from bytes and validate IPv4 again.

The IPv4 parser checks version, header and total lengths, reserved flag,
fragment extents, option framing, and the complete header checksum. UDP and TCP
decoders additionally require the expected protocol and an unfragmented datagram.
They check the full transport checksum including the IPv4 pseudo-header. TCP
checks its header extent and option framing. UDP requires its declared length
to equal the IPv4 payload length; this is an intentionally strict policy.

IPv4 UDP checksum zero is accepted and exposed as `checksum_present == false`.
A nonzero UDP checksum must verify. TCP verification is mandatory even when its
checksum field is zero. An IPv4 header checksum covers the header only; the
transport checksums cover their headers and payloads.

These rules follow the wire formats in [IPv4 RFC 791](https://www.rfc-editor.org/rfc/rfc791.html),
[UDP RFC 768](https://www.rfc-editor.org/rfc/rfc768.html), and
[TCP RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.1).

## Sealing constructed or updated packets

`seal_ipv4(bytes: [u8])`, `seal_udp_ipv4(bytes: [u8])`, and
`seal_tcp_ipv4(bytes: [u8])` return `result<void>`. Supply a complete datagram
whose lengths, headers, options, and payload are already populated. Use
`memory.write_packed` to write header values and byte slices for payload data.
Sealing validates the structure and replaces the relevant checksum fields,
ignoring their previous contents. Transport sealing also refreshes the IPv4
checksum. A computed UDP checksum of zero is stored as `0xffff` so it cannot be
mistaken for an absent checksum.

All validation precedes writes. On failure the bytes remain unchanged. Success
preserves all other fields, options, payloads, and bytes beyond total length.
Sealing does not infer lengths, insert options, reassemble fragments, or check
connection state. Update lengths explicitly using checked integer constructors.
`seal_ipv4` can checksum a structurally valid fragment; transport sealing rejects
fragments. TCP sealing requires the four reserved bits to be zero.

## Checksum streams

`packet.checksum(bytes: [const u8]) -> result<be_u16>` computes the Internet
checksum of arbitrary bytes. `Checksum.create()`, `sum.update(bytes) ->
result<void>`, and `sum.finish() -> result<be_u16>` provide the same calculation
incrementally. Odd boundaries between updates do not insert padding. `finish`
does not mutate state, so callers can inspect a prefix checksum and continue.
An empty stream has checksum `0xffff`; verification over a complete checksummed
message succeeds when the computed value is zero. The algorithm uses bounded
end-around carry and follows [RFC 1071](https://www.rfc-editor.org/rfc/rfc1071.html).
The result-returning API propagates checked integer conversions.

## Supported scope and costs

This is a representation and validation layer, not a TCP/IP stack. IPv6, Ethernet,
raw-socket/TUN integration, fragmentation/reassembly, routing, TCP state and
retransmission, checksum offload metadata, and option negotiation are separate
work. Checksums detect corruption; they do not authenticate senders.

Options receive structural validation: EOL, NOP, length bounds, zero padding
after EOL, and standard TCP option lengths. Unknown length-bearing options remain
opaque. Option-specific IPv4 semantics are not interpreted. IPv4 loose/strict
source routing is rejected because transport validation would need the ultimate
destination address. Received TCP reserved bits are preserved and ignored;
generation supports the RFC 9293 base header with those bits zero.

Reads and writes copy a fixed header, leaving body slices borrowed. Checksum
processing is linear in the bytes covered, with constant storage and no hidden
allocation. Sealing never decodes and re-encodes every payload field. This release
does not claim a throughput improvement over a hand-written byte codec.

## Validation

Independent C++ byte vectors and checksum calculations exercise JIT, native
`-O0`, and native `-O2`. Coverage includes every truncated prefix and single-bit
corruption of representative packets, all byte alignments modulo 16, exact
lengths, trailing input, malformed options, fragment rejection, UDP absent/zero
checksums, TCP reserved bits, and unchanged output on rejected sealing.
Checksum tests exercise every split of lengths 0 through 257 and a 65,535-byte
input and a maximum-size IPv4/UDP datagram. Packed-memory tests cover mixed byte orders, narrow fields, spare bits,
overlap, short buffers, and canaries. Native guard-page tests put exact and short
buffers at both inaccessible page boundaries. Compiler tests reject invalid
source types and forged IR that removes constness or changes the target type.

On 2026-10-30, all 1,123 core tests passed on macOS ARM64 with LLVM/MLIR 21.1.6.
The four packet networking groups also passed against an installed, relocated
package in all three execution modes. Formatting and nine HTML pages with local
links passed. Linux, sanitizer, and complete release-package validation remain
pending.
