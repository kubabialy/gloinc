# Gloin 0.1.0 candidate

This is an unreleased candidate for Apple Silicon macOS and Ubuntu 24.04 Linux.
The latest published release remains 0.0.4. The candidate uses LLVM/MLIR
21.1.6 on macOS and 21.1.8 on Linux and includes the following work since 0.0.4:

- `result<T>` and `error` are built-in lowercase types. A function returning
  `result<T>` wraps either a `T` value or an `error`; callers must inspect
  `.erroneous` before reading `.value` or `.error`. `result<void>` is supported.
  The standard library adds `std.parse_i32_checked`,
  `strings.byte_at_checked`, and `strings.slice_bytes_checked` while preserving
  existing status-returning functions. See the [result contract](results.md).
- `@memory` adds raw allocation/free, target size and alignment queries, and
  checked typed placement. The [custom arena example](../examples/custom_arena.gloin)
  builds a bump allocator in Gloin. See [raw memory](raw-memory.md).
- Both vector forms gain live-element slice views and collection methods.
  `@slices` adds generic fill, reverse, and nonoverlapping copy helpers.
  Growable `Vector<T>.empty` reserves capacity without filling spare slots.
  See [slices and vectors](slices-vectors.md).
- `@io` can read directly into writable byte slices and write from read-only
  byte slices. The [binary filter](../examples/strip_nuls.gloin) demonstrates
  the API without a string conversion. See [I/O](io.md).
- `@net` adds nonblocking IPv4 TCP sockets with explicit ownership, partial
  read/write outcomes, readiness waits, and checked connection completion.
  `@http` parses bounded HTTP/1.1 request/response heads, frames fixed-length
  bodies, and formats a GET or fixed-length response. Both expose built-in
  `result<T>` for failures. The
  [local round-trip example](../examples/network_http.gloin) runs without an
  external service. See [networking](networking.md) for exact limits.
- Checked calls, returns, and expression values retain source types through
  GloinIR until an explicit storage-layout boundary. Result and error
  construction and access have dedicated verified dialect operations. See the
  [lowering audit](lowering.md).
- Linux native output now emits position independent objects so executables
  link with Ubuntu's default PIE linker settings. Ubuntu 24.04 has a pinned
  LLVM/MLIR installer, a Linux package format, and a separate CI acceptance job.

`result` and `error` are now reserved words; 0.0.4 programs using them as
identifiers must rename those identifiers. Growable
`Vector<T>.create(&memory, capacity, fill)` remains callable, but its `fill`
argument is evaluated without initializing spare slots. Use
`Vector<T>.empty(&memory, capacity)` for new code. Only live vector elements may
be read through methods or slices.

Errors currently contain static literal messages, without source location or
debug metadata. Results cannot yet be fields or be stored behind pointers, in
arrays, or in slices. The compiler's high-level IR still contains standard MLIR
operations and some LLVM operations for string/ABI preparation and defer
bookkeeping. Maps, enum payloads and matching, package imports, and async/spawn
remain unsupported. Windows support is not planned, though contributors may
work on it.

Hosted Ubuntu 24.04 x86_64 CI passed its 918-case core gate, installed and
relocated package suites at 479/479 each, and serial and parallel full-suite
classification. Ubuntu 24.04 ARM64 passed the 918-case core gate, installed and
relocated package suites at 479/479 each, and direct JIT/native output checks.
Its serial and parallel complete suites each passed 961/965; only the four
documented async/spawn cases failed.

The [0.1.0 candidate HTML guide](site/0.1.0/index.html) walks through complete
programs and links to the exact API and ownership rules. The
[candidate release guide](release-0.1.0.md) covers installation, package
contents, and verification. Neither a tag nor a public release has been made.

Candidate validation on Apple Silicon macOS passed all 918 required core tests.
The full parallel suite passed 961 of 965 tests; its only four failures are the
documented unsupported async/spawn cases. Installed and relocated package
suites each passed 479 tests. All 32 example source files check, the standalone
Gloin programs in the candidate HTML guide check, and the formatting, HTML
link, and package checksum checks pass.
