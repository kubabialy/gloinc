# Next release proposal (unreleased)

The latest published compiler is 0.0.4. **0.1.0 is prepared as an unreleased
candidate** for the current development batch. It targets Apple Silicon macOS
with LLVM/MLIR 21.1.6 and Ubuntu 24.04 Linux with LLVM/MLIR 21.1.8. Windows
support is not planned, though contributions are welcome.
This document proposes a boundary and records local validation. It is not a
release announcement.

**Linux acceptance is required for 0.1.0.** The current Ubuntu ARM64 tree passed
the 935-case core gate and installed/relocated 489-case package gates. Its
serial and parallel complete suites each passed 978/982, with exactly four
documented async/spawn failures. Hosted Ubuntu x86_64 CI passed an earlier
revision and must rerun before publication.

## Linux release gate

- The Ubuntu 24.04 installer pins the apt.llvm.org 21.1.8 shared toolchain and
  verifies its repository signing key. A Linux ARM64 Release build passed.
- Linux native output now emits PIC objects. JIT, `-O2` executable output, and
  ordinary `clang++` object linking ran successfully on Ubuntu ARM64.
- The package script handles `.so`, `ldd`, and `sha256sum` on Linux. Installed
  and relocated Ubuntu ARM64 packages each passed 489/489 acceptance cases,
  including version, checksum, native output, and module relocation checks.
- The current required core suite passed 935/935 on Ubuntu ARM64. The earlier
  hosted x86_64 CI core gate passed 918/918; it must rerun on this revision.

## Current local validation

On 2026-10-05, the compiler and standard-library tree passed these Release-build
checks with LLVM/MLIR 21.1.6 on macOS and 21.1.8 on Ubuntu 24.04 ARM64:

| Check | macOS ARM64 | Ubuntu ARM64 |
| --- | --- | --- |
| Required `check-core` | 935/935 | 935/935 |
| Installed package acceptance | 489/489 | 489/489 |
| Relocated package acceptance | 489/489 | 489/489 |
| Full serial suite | 978/982 | 978/982 |
| Full parallel suite (`-j 4`) | 978/982 | 978/982 |
| Example sources, `--check` | 41/41 | 41/41 |

Both complete-suite reports contain exactly the four retained unsupported
async/spawn failures and no skipped tests. The classification script passed
on both platforms. Installed and relocated compilers also passed trusted,
wrong-host, and untrusted TLS checks, synchronous and streaming HTTP/HTTPS
matrices, and the JSON corpus/state/limit tests. JSON ran in JIT, `-O0`, and
`-O2`. Formatting, seven HTML pages' local links, archive checksums, native
output, dependency checks, and standard-library relocation checks passed.

Initial runs exposed HTTP JIT timeouts: running both platforms together caused
contention, and cold relocated macOS parser cases also exceeded the old
ten-second compiler allowance when macOS ran alone. Networking compiler tests
now allow 60 seconds per invocation and 150 seconds per CTest case on both
platforms. Socket deadlines and assertions are unchanged. Final validation ran
one platform at a time; all 982 tests remained discovered exactly once. The
compiler, runtime, and standard-library sources did not need changes.

The latest successful [hosted CI run](https://github.com/kubabialy/gloinc/actions/runs/37147497367)
tested commit `688c04c`, before this development batch. Fresh hosted Linux
x86_64 and macOS CI, including the sanitizer gate, remain required. Local
report directories are `build/release-validation-macos-final/` and
`build/release-validation-linux-final/`; earlier failed attempts were retained.
The milestone sections below record historical snapshots, not additional
pending local gates. Validation-only documentation updates followed the runs;
rebuild archives with final release documentation before publishing.

## Proposed contents

- A Gloin-written `@json` pull reader, complete-document validator, and bounded
  writer using `result<T>`. UTF-8 and Unicode escapes are checked, numbers keep
  exact source spelling, and callers own buffers. See [JSON](json.md) for
  limits, duplicate keys, terminal failures, and HTTP integration.
- Built-in `result<T>` and `error`, explicit return wrapping and failure
  forwarding, mandatory `.erroneous` inspection, and source-typed GloinIR
  operations. `std.parse_i32_checked`, `strings.byte_at_checked`, and
  `strings.slice_bytes_checked` demonstrate the API without removing existing
  status-returning functions. See [results](results.md).
- Raw native allocation, free, target layout queries, and checked typed
  placement through `@memory`, with a runnable user-written bump arena. See
  [raw memory](raw-memory.md).
- Live-element slice views and collection methods for both vector forms,
  generic `@slices` helpers, and empty growable vector construction that leaves
  spare slots uninitialized. See [slices and vectors](slices-vectors.md).
- Direct byte reads into writable slices and writes from read-only byte slices,
  plus the runnable binary `strip_nuls` example. See [I/O](io.md).
- Explicit `@fs` directory cursors with caller-owned entry names, an `END`
  result, alias-aware close, and a runnable directory walker. See
  [directory iteration](filesystem-process.md#directory-iteration).
- Extensionless local imports now discover immediate `.gloin` files when the
  named directory exists; `#name` imports discover `packages/name/` beside the
  root source file. Both combine public declarations without an entry file.
  See [module discovery](modules.md) and the
  [runnable example](../examples/module_discovery.gloin).
- A Gloin-written bounded HTTP/HTTPS client with application headers, an
  overall timeout, incremental fixed/chunked/EOF response decoding, and explicit
  buffer ownership, streaming exchanges, backpressure, cancellation, and early
  responses during uploads. See [the client guide](http-client.md). Large zeroed network
  buffers use bulk zero stores during GloinIR lowering.
- Nonblocking IPv4 TCP sockets in `@net` and bounded HTTP/1.1 head parsing,
  fixed-length body framing, and formatting in `@http`, using built-in
  `result<T>` at their public boundary.
  See [networking](networking.md) and the
  [local round trip](../examples/network_http.gloin).
- A verified, nonblocking TLS client over connected sockets, with default or
  explicit PEM trust roots, DNS/IP certificate matching, and readiness-driven
  handshake and I/O. OpenSSL 3 is now a build dependency.
- The checked source-type boundary for calls, returns, and expression values in
  GloinIR. The high-level module still mixes standard MLIR and some LLVM
  operations for string/ABI preparation and defer bookkeeping; see the
  [lowering audit](lowering.md).

The [0.1.0 candidate HTML guide](site/0.1.0/index.html) teaches the complete
current language, including the published 0.0.4 features. The numbered
[0.0.4 guide](site/0.0.4/index.html) continues to describe that published
release. The [examples guide](../examples/README.md) links the runnable programs.

## Compatibility and current limits

`result` and `error` are now reserved words. Programs that used them as binding,
field, function, or type names must rename those identifiers. A result binding
must be checked on every path out of scope. Errors currently hold only a static
literal message; source locations and debug-only metadata are not implemented.
Results cannot yet be stored behind pointers, in arrays or slices, or as struct
fields. Existing standard-library status structs remain available. EOF on
`std.input` remains a status because it is a normal input outcome.

The growable `Vector<T>.create(&memory, capacity, fill)` remains callable for
source compatibility, but its `fill` argument is evaluated and no longer used
to initialize spare capacity. `Vector<T>.empty(&memory, capacity)` expresses
the new preferred construction. Only live elements may be accessed through
vector methods or slice views; reset/free of the owning arena still invalidates
them.

Maps, enum payloads and matching, package downloading/version resolution, and async/spawn are
outside this proposed release. A Windows port is outside the maintained plan.

## Earlier candidate validation on Apple Silicon macOS

| Check | Result |
| --- | --- |
| `check-core` | 918/918 passed |
| Full parallel CTest suite | 961/965 passed; only the four documented unsupported async/spawn tests failed |
| Installed package acceptance | 479/479 passed |
| Relocated extracted package acceptance | 479/479 passed, including result and network example smoke runs |
| Example checking | 32/32 `.gloin` files under `examples/` passed `--check` |
| Prior Python style check | 198 files checked; none needed formatting at that snapshot |
| HTML links and runnable guide snippets | Seven pages have valid local links; candidate and development standalone examples passed `--check` |

## Earlier candidate validation on Ubuntu 24.04 ARM64

| Check | Result |
| --- | --- |
| Pinned toolchain | apt.llvm.org LLVM/MLIR 21.1.8 shared targets configured and built |
| JIT and native smoke | Hello world ran via JIT and `-O2` executable; an emitted PIC object linked with ordinary `clang++` and exited 42 |
| `check-core` | 918/918 passed |
| Installed package acceptance | 479/479 passed |
| Relocated extracted package acceptance | 479/479 passed, including checksum, dependency, and network example checks |
| Complete suite | Serial and parallel each passed 961/965; exactly the four documented async/spawn cases failed |

## Earlier candidate validation on Ubuntu 24.04 x86_64

The [hosted CI run](https://github.com/kubabialy/gloinc/actions/runs/37146653312)
passed its Linux job. The core gate passed 918/918. Installed and relocated
packages each passed 479/479 checks. The serial and parallel complete suites
each passed 961/965; the four failures are the documented async/spawn cases.

The subsequent integer formatting family and `set_reuse_address` follow-up
passed macOS 918/918 core cases and installed/relocated 479/479 package cases.
Ubuntu ARM64 passed focused compiler/runtime checks, installed/relocated
479/479 package cases, and the sibling HTTP server verifier in JIT and native
modes. Hosted x86_64 CI must rerun on this follow-up before publication.

Further networking work adds multi-socket readiness waits, write-side shutdown,
a synchronous IPv4 resolver, and a verified nonblocking TLS client. The TLS
client requires OpenSSL 3 for compiler/runtime builds and native programs using
TLS. On the updated macOS and Ubuntu ARM64 trees, the 920-case core gate
passed. Installed compilers on both systems passed local trusted, wrong-host,
and untrusted-peer TLS fixtures in JIT and native modes. Earlier installed and
relocated 479-case package gates and the nine-request HTTP server verifier
passed on both systems before the TLS changes. The updated macOS and Ubuntu
ARM64 installed and extracted package gates passed 479/479 each and repeated
the TLS fixtures. Hosted x86_64 CI must rerun before
any 0.1.0 release. The [roadmap](networking-roadmap.md) tracks the
remaining HTTPS, IPv6, WebSocket, and general integration gates.

The Gloin-written `gloinfmt` then passed native/JIT smoke checks on macOS and
Ubuntu ARM64. Its source-corpus check preserved lexical tokens and was
idempotent across 201 `.gloin` files. The macOS required core gate passed
923/923; installed and relocated packages each passed 480/480, including the
packaged formatter. Ubuntu ARM64 passed the formatter smoke and repository
style check. Hosted x86_64 CI still needs to run on this tree.

Directory and local package discovery now collect immediate `.gloin` files
without an entry file. The updated macOS tree passed 927/927 required core
cases. Installed and relocated packages each passed 484/484 acceptance cases,
including module discovery; the packaged example ran in JIT mode. Ubuntu ARM64
passed all 29 module tests. The example also ran from the source tree in JIT
and native modes on both platforms. Hosted x86_64 CI still needs to rerun on
this tree before publication.

The HTTP/HTTPS client follow-up passed the 931-case macOS required core gate.
Installed and relocated macOS compilers each passed 487 acceptance cases plus
the local TLS and HTTP client fixtures. Ubuntu ARM64 passed all seven focused
networking/large-zeroed-buffer cases and the HTTP/HTTPS fixtures in JIT and
native modes, including the example. The initial Linux integration run hit the
fixture's startup/response timeout; the fixture now allows VM/JIT startup and
uses longer success-case timeouts while retaining short deliberate deadline
checks. All 39 example sources, repository Gloin formatting, and HTML links
passed checks. Full Linux core/package and hosted x86_64 validation still need
to rerun on this revision before publication.

These checks were rerun after the candidate version bump. Build metadata
reports 0.1.0, and the numbered guide and release notes are prepared. The
0.1.0 archive checksum passed; its installed and relocated copies passed the
package gate. Publication has not occurred.

## Streaming HTTP milestone

`@http_client.Exchange` now drives known-length uploads and reusable-buffer
downloads through explicit readiness, input, and output states. It supports
early final responses, total body/metadata limits, deadlines across application
pauses, cancellation shared by aliases, and explicit cleanup. The synchronous
client uses the same Gloin state machine. `@http.ResponseDecoder` exposes the
allocation-free streaming framing layer; `ResponseReader` remains the
whole-body convenience API. All language-level logic follows GloinIR.

The [guide](http-client.md) and development/0.1.0 HTML document scheduling,
ownership, partial-output failures, retained arena allocations, and the exact
HTTP subset. The [32-exchange example](../examples/http_stream.gloin) uses 192 KiB
of application buffers for 31 large binary downloads and one stalled connection.
The sibling `gloin_examples/http_stream` project includes a concurrent Gloin
HTTP server and client. Both use readiness loops and reusable buffers; the
server has explicit 32-connection batch limits. Server-side TLS remains pending.
Unknown-length uploads, pooling, concurrent servers, and WebSockets remain
separate work. This milestone does not publish 0.1.0.

Validation for this milestone passed:

- Required core: **934/934** on macOS and Ubuntu ARM64.
- Installed and relocated macOS packages: **489/489** acceptance cases each,
  plus TLS, synchronous HTTP/HTTPS, and streaming matrices using the packaged
  compilers and examples in JIT/native modes.
- All 40 example sources and the complete client-guide program type-check;
  repository formatting, diff whitespace, and seven HTML pages' links pass.
- The sibling Gloin server/client pair passes JIT and `-O2` native execution
  on macOS and Ubuntu ARM64, including repeated native batches and rejected
  requests. Both source files pass `gloinfmt`; the Python server was removed
  from the sibling project. The compiler's independent HTTP/HTTPS test peers
  remain, and both platforms pass their 32-exchange fixture.

Early validation hit host sleep and the former ten-second Linux compiler
subprocess limit. The networking compiler fixture now has a bounded 60-second
Linux budget, and each integration JIT invocation runs a scenario matrix
without repeated compilation. The final full core runs passed. Request-level
deadlines remain unchanged. Linux package and hosted x86_64 gates remain before
publication; the published site still describes 0.0.4.

## Bounded JSON milestone

`@json` now provides a pull reader over complete buffered input, whole-document
validation, quoted-string conversion, and a bounded structural writer. All
fallible public operations use `result<T>`, and the implementation follows the
ordinary GloinIR pipeline. Number tokens retain exact spelling; Unicode scalar
validation, surrogate escapes, depth/byte limits, ordered duplicate keys, and
terminal reader/writer failures are documented in the [JSON guide](json.md).
Incremental network input and automatic struct/DOM mapping remain separate work.

The macOS required core gate passed **935/935**. On macOS and Ubuntu ARM64,
the independent JSON matrix passed in JIT, native `-O0`, and native `-O2` modes:
142 accepted documents, 53 rejection cases, and 54 Unicode round trips, plus
state/limit checks and the runnable example. Installed and relocated macOS
packages passed that same JSON matrix. This was a focused package check;
the complete package suites and hosted x86_64 CI still need to rerun before
publication. Both Markdown guide programs run, both candidate HTML pages
contain those programs, and HTML links and repository formatting pass.

## Before publication

1. Review the 0.1.0 candidate boundary, compatibility notes, and final diff;
   commit the candidate and run fresh hosted CI, including Ubuntu x86_64 and
   the macOS sanitizer gate.
2. Keep the site root on published 0.0.4 while this is a candidate. After
   approval, remove candidate wording and update `latestVersion` and the root
   redirect; keep older numbered pages unchanged.
3. The current macOS and Ubuntu ARM64 tree passed the core, package, and
   full-suite gates. Rebuild archives with the final documentation and rerun
   affected checks if code or package contents change before publication.
4. Confirm the final version and release artifacts before tagging or publishing.
