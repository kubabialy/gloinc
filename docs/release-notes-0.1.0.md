# Gloin 0.1.0 candidate

This is an unreleased candidate for Apple Silicon macOS and Ubuntu 24.04 Linux.
The latest published release remains 0.0.4. The candidate uses LLVM/MLIR
21.1.6 on macOS and 21.1.8 on Linux and includes the following work since 0.0.4:

- The former repository Python tools and test drivers now run in Gloin, including HTTP/HTTPS
  peers with independent wire expectations, delayed responses and 32 concurrent
  exchanges. CMake no longer requires Python. See the [tooling record](tooling-roadmap.md)
  and [HTTP acceptance guide](../tests/http/README.md).

- `@process` adds explicit child launch, poll/wait, normal and signaled exit
  statuses, termination and alias-aware cleanup using `result<T>` and GloinIR.
  Streams can be inherited or piped; bounded binary stdout/stderr capture drains
  concurrently with stdin, with explicit deadlines and direct-child cleanup.
  `start_with` adds child cwd, inherited or supplied environments and opt-in
  process-group cleanup, including descendants holding pipes after leader exit.
  Group cleanup is not containment for descendants that leave the group.
  `Options.stack_limit_bytes` sets a child soft stack limit before execution;
  zero inherits and nonzero limits require the host's main thread on macOS.
  See [child processes](child-processes.md).

- `@fs` adds non-replacing `symlink` creation and bounded `read_link`, both
  using built-in `result<T>` and the GloinIR ABI boundary. Reads preserve the
  immediate target text, including broken/cyclic links, and never silently
  truncate. The formatter fixture now creates its symlink in Gloin. See the
  [filesystem guide](filesystem-process.md#symbolic-links).
- `@fs` also adds bounded `canonical_path`, exclusive `temp_dir` and
  empty-directory `remove_dir`, with output allocation before directory creation.
  The formatter driver now launches children, captures results and cleans up its
  fixtures in Gloin; CTest invokes it directly. See the [workspace contract](filesystem-process.md#canonical-paths-and-temporary-directories).

- `@json` adds a bounded pull reader, complete-document validation, string
  conversion, and a writer, all in Gloin with built-in `result<T>`. It checks
  UTF-8/Unicode escapes and retains exact number tokens without implicit float
  conversion. Input/output are borrowed and nesting is limited to 64 levels.
  Duplicate keys remain ordered events; incremental input and automatic struct
  serialization are not implemented. See [JSON](json.md) and the
  [runnable example](../examples/json.gloin).
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
- `@http_client` now performs one bounded HTTP/HTTPS exchange with an overall
  timeout, explicit address/TLS settings, application headers, and caller-owned
  response buffers. `@http` adds incremental chunked/fixed/EOF decoding,
  informational and bodyless response handling, and header iteration. See the
  [client guide](http-client.md) for both streaming and synchronous APIs.
  Streaming `Exchange` objects support bounded upload/download buffers,
  backpressure, early final responses, per-exchange deadlines, and cancellation.
  The synchronous API drives the same Gloin state machine. A local
  [32-connection example](../examples/http_stream.gloin) demonstrates progress
  while one peer stalls; unknown-length request bodies remain unsupported.
  Large `zeroed` array stores lower through GloinIR to a bulk zero operation,
  avoiding an LLVM crash on 64 KiB byte buffers in default native builds.
- Listeners can explicitly enable `SO_REUSEADDR` before binding. `@std`
  provides typed integer formatters and checked `to_string_<width>` conversions,
  including `u16` ports across their full range. The
  [networking milestones](networking-roadmap.md) cover outbound HTTPS,
  concurrent servers, and both Slack bot delivery modes.
- The networking follow-up adds multi-socket readiness waits, write-side TCP
  shutdown, a synchronous IPv4 hostname resolver, and a nonblocking verified
  TLS client built on OpenSSL 3. Local trusted, wrong-host, and untrusted-peer
  fixtures pass in JIT and native modes. IPv6, a reusable event
  queue, and the broader HTTPS/WebSocket use cases remain unfinished.
  Building and running the compiler now requires OpenSSL 3; native executables
  using TLS require the corresponding runtime libraries.
- `@net` adds reusable PEM server configurations, nonblocking accepted TLS
  sessions and explicit bidirectional shutdown for clients and servers.
  Linux TLS calls preserve the host's SIGPIPE policy. The TLS acceptance driver
  is now written in Gloin, with independent direct-OpenSSL interoperability tests.
  See the [server guide and runnable example](server-tls.md).
- Checked calls, returns, and expression values retain source types through
  GloinIR until an explicit storage-layout boundary. Result and error
  construction and access have dedicated verified dialect operations. See the
  [lowering audit](lowering.md).
- Linux native output now emits position independent objects so executables
  link with Ubuntu's default PIE linker settings. Ubuntu 24.04 has a pinned
  LLVM/MLIR installer, a Linux package format, and a separate CI acceptance job.
- `gloinfmt` is now built from Gloin source and packaged with the compiler. It
  formats a file to standard output, checks source trees recursively, and
  supports checked in-place writes and Git-aware discovery. Its
  conservative layout rules, exit codes, and limits are in the
  [formatter guide](gloinfmt.md).
- Extensionless local imports now discover immediate `.gloin` files in a named
  directory, and `#name` imports discover `packages/name/` beside the root
  source. Public declarations form one namespace without a `mod.gloin` entry.
  Existing file imports can be selected explicitly with `.gloin`. See
  [modules](modules.md) and the [example](../examples/module_discovery.gloin).

## Compatibility and current limits

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
bookkeeping. Maps, enum payloads and matching, package downloading and version
resolution, and async/spawn remain unsupported. Windows support is not planned, though contributors may
work on it.

The [0.1.0 candidate HTML guide](site/0.1.0/index.html) walks through complete
programs and links to the exact API and ownership rules. The
[candidate release guide](release-0.1.0.md) covers installation, package
contents, and verification. Neither a tag nor a public release has been made.

## Validation

The 2026-10-05 local Release-build checks passed on macOS ARM64 and Ubuntu
24.04 ARM64: **935/935 required core cases**, **489/489 installed package
cases**, and **489/489 relocated package cases** on each platform. Serial and
four-job parallel complete suites each passed **978/982**, with exactly the
four documented unsupported async/spawn failures and no skips.

Installed and relocated packages also passed TLS certificate verification,
synchronous and streaming HTTP/HTTPS matrices, and JSON codec/state/limit
checks. JSON ran in JIT, `-O0`, and `-O2`. All 41 example source files passed
`--check` on both platforms; formatting, HTML links, checksums, native output,
and relocation checks passed. A networking test timeout was corrected without
changing compiler/runtime code, socket deadlines, or assertions.

Fresh hosted CI, including Linux x86_64 and the macOS sanitizer gate, must
pass on this revision before publication. The
[current validation record](next-release-draft.md#current-local-validation)
contains the results, test-harness correction, and earlier milestone history.
