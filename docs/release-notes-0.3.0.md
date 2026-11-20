# Gloin 0.3.0

## Language and migration

- Use endian-qualified integer fields and packed structs to describe wire bytes directly. Bit order belongs to the record. Byte order belongs to each qualified integer. Copies of materialized aggregates preserve their bytes, including padding, across Gloin calls and returns. Rebuild native objects and link the matching runtime because the internal aggregate calling convention changed. See [endianness](endianness.md) and [packed layouts](packed-layout.md).
- Signed and unsigned integer widths from 1 through 64 bits have checked arithmetic. Integer constructors, including widening and endian-only conversions, return `result<T>`. Check the result before reading its value. See [integer constructors](packed-layout.md).
- Packed arrays and slices support checked placement, alignment, views, splits and overlap-safe copies. Two bit ranges can share a byte. See [packed storage and aliasing](packed-storage.md).
- `continue;` runs the enclosing for-loop update before the next condition. `break;` exits the nearest loop and skips its update. Both preserve function-scoped defer behavior. Code using these words as identifiers must rename them. See [continue](continue.md) and [break](break.md).

## Standard library

- `strings.byte` reads one byte with a bounds trap. Use `strings.byte_at` when an out-of-range index should return an error. String builders accept byte slices, and ordinary slices gain overlap-safe copying. Arena vectors gain recoverable growth and cloning. See [strings](strings.md), [builders](text-construction.md) and [slices and vectors](slices-vectors.md).
- Packed packet codecs validate and seal IPv4/IPv6 transport data. The Gloin TCP implementation drives bounded HTTP and HTTPS servers, and the HTTP request decoder handles partial bodies and backpressure. The IPv6 host uses static addresses. Live raw-device interoperability was exercised on Linux. macOS utun support has automated framing coverage. See [packet codecs](packets.md), [request decoding](http-requests.md), [server limits](tcpip-server.md) and [IPv6](ipv6.md).

Use the [numbered language guide](site/0.3.0/index.html) for examples and the [release guide](release-0.3.0.md) for platform requirements, package verification and provenance. The [development record](release-0.3.0-draft.md) retains the scope decisions and measurements made during integration.
