# Gloin 0.1.0 scope and validation record

Gloin 0.1.0 was released on 2026-10-08. It targets Apple Silicon macOS
with LLVM/MLIR 21.1.6 and Ubuntu 24.04 Linux with LLVM/MLIR 21.1.8. Windows
support is not planned, though contributions are welcome.
This document preserves the release scope and validation history, including
earlier incomplete milestones. See the [release notes](release-notes-0.1.0.md)
and [release guide](release-0.1.0.md) for the published release.

**Linux acceptance is required for 0.1.0.** Hosted Ubuntu x86_64 passed the
987-case core gate, both 501-case package gates, and serial/parallel complete
audits on commit `3ce4e81`. Each audit passed 1,030/1,034 with exactly the four
documented async/spawn failures. Local Ubuntu ARM64 results are recorded below.

## Linux release gate

- The Ubuntu 24.04 installer pins the apt.llvm.org 21.1.8 shared toolchain and
  verifies its repository signing key. A Linux ARM64 Release build passed.
- Linux native output now emits PIC objects. JIT, `-O2` executable output, and
  ordinary `clang++` object linking ran successfully on Ubuntu ARM64.
- The package script handles `.so`, `ldd`, and `sha256sum` on Linux. Installed
  and relocated packages run 501 acceptance cases, plus TLS, HTTP, JSON and
  million-record stress matrices, version/checksum checks and module relocation.
- The required core suite passed 987/987 on Ubuntu ARM64 and hosted x86_64.

## Gloin tooling release gate

As of 2026-10-06, 0.1.0 also requires Gloin replacements for the remaining
repository-owned Python tools and test drivers. The
[tooling roadmap](tooling-roadmap.md) records the completed migrations; no
repository-owned Python files remain. Required capabilities include child
processes/pipes, stack limits, symlink and temporary-directory operations,
timed waits, server TLS and explicit graceful TLS shutdown. Preserve independent
JSON, protocol and simulation expectations while porting the tests.

The first [child-process milestone](child-processes.md) implements explicit
launch with inherited streams/cwd/environment, polling, waiting, exit status,
termination and direct-child cleanup. It also provides nonblocking pipes,
bounded binary stdout/stderr capture, concurrent stdin writes and explicit
deadlines. `start_with` adds child cwd, replacement environments and opt-in
process-group cleanup that remains usable after the leader exits. Explicit
search paths, stream redirection and resource limits other than stack remain pending.
`Options.stack_limit_bytes` now applies a child-only soft stack limit, with a
documented macOS main-thread requirement.
`fs.symlink` and bounded `fs.read_link` are implemented with built-in results;
bounded canonical paths, exclusive temporary directories and empty-directory
removal are implemented too. Checked file replacement now supports
`gloinfmt --write`, and `--git` discovers tracked/unignored sources. Formatter,
documentation links, complete-suite classification and JSON orchestration now
run in Gloin. The [tool guide](repository-tools.md) documents bounds and input
rules; the JSON oracle remains independent. The million-record stress driver
also runs in Gloin with independent fixed expectations and a 2 MiB child soft
stack limit. Server TLS, graceful shutdown and the TLS driver now run through
the Gloin API. The HTTP drivers now also run in Gloin, including independent
wire fixtures, delayed responses and 32 concurrent HTTP/HTTPS exchanges.
The completed tooling tree and validation follow-ups are covered by the
records below. Earlier milestone sections retain their original boundaries.

## Current local validation

The 2026-10-07 and 2026-10-08 local tooling audits used LLVM/MLIR 21.1.6 on
macOS and 21.1.8 on Ubuntu 24.04 ARM64:

| Check | macOS ARM64 | Ubuntu ARM64 |
| --- | --- | --- |
| Required `check-core` | 987/987 | 987/987 |
| Installed package acceptance | 501/501 | 501/501 |
| Relocated package acceptance | 501/501 | 501/501 |
| Full serial suite | 1,030/1,034 | 1,030/1,034 |
| Full parallel suite (`-j 4`) | 1,030/1,034 | 1,030/1,034 |
| Example sources, `--check` | 48/48 | 48/48 |

Both complete-suite reports contain exactly the four retained unsupported
async/spawn failures and no skipped tests. The Gloin classifier passed on both
platforms. Formatting and seven HTML pages' local links passed.

The full local audits began before the final cleanup corrections: macOS used
the completed tooling implementation at `e5db67f`; Linux used `4c1420f`.
Package checks included the installed documentation-link fix. After the macOS
process cleanup and HTTP peer corrections, all 27 affected Release checks
passed; the native cleanup probe passed 1,000 repetitions. The complete hosted
matrix below verifies the corrected code together.

Local sanitizer runs exposed two harness limits described below. After their
correction, every required case passed across the complete run and its focused
rerun. The cleanup follow-up passed all 26 process checks under ASan/UBSan;
laptop clamshell sleep interrupted its HTTP check. The fresh hosted sanitizer
gate passed all 987 cases on the corrected code without that interruption.

Reports and failed attempts are retained under
`build/release-validation-tooling-macos/`,
`build/release-validation-tooling-linux/`, and
`build/release-validation-tooling-hosted/`.

## Hosted validation

The [PR workflow](https://github.com/kubabialy/gloinc/actions/runs/37695280617)
and independent [push workflow](https://github.com/kubabialy/gloinc/actions/runs/37695275816)
both passed on `3ce4e814d325848814e8af7b00e0b1209562f316`. Downloaded PR artifacts
were independently checked for counts, unique names, skips, exact failure sets
and archive checksums.

| Check | macOS 15 ARM64 | Ubuntu 24.04 x86_64 |
| --- | --- | --- |
| Required core | 987/987 | 987/987 |
| Installed package | 501/501 | 501/501 |
| Relocated package | 501/501 | 501/501 |
| Complete serial suite | 1,030/1,034 | 1,030/1,034 |
| Complete parallel suite | 1,030/1,034 | 1,030/1,034 |
| Debug ASan/UBSan core | 987/987 | Not part of this job |
| Archive SHA-256 | Verified | Verified |

Both complete audits contain exactly the four documented unsupported
async/spawn failures and no skipped or duplicate cases. The macOS job also
builds with `BUILD_TESTING=OFF` and checks JIT, object and executable output.
Both platforms run the Gloin TLS, HTTP/HTTPS, JSON and million-record drivers
against installed and relocated compilers. JSON and HTTP run in JIT, `-O0`
and `-O2`. The [candidate PR](https://github.com/kubabialy/gloinc/pull/6) records
subsequent documentation-only revisions and their checks. Final implementation
revision `95eb48d` passed both the
[PR workflow](https://github.com/kubabialy/gloinc/actions/runs/37742310590) and
[push workflow](https://github.com/kubabialy/gloinc/actions/runs/37742305156),
reproducing every count above; downloaded reports and archive checksums were
independently verified.

## Corrections found during final validation

- Packaged HTML example links were broken. Packages now include documentation
  source copies preserving the relative paths, and the package gate checks all
  seven HTML pages in both installed and extracted copies.
- Instrumented compiler startup exceeded the ordinary ten-second CLI allowance.
  Sanitizer invocations now allow 30 seconds; ordinary Release invocations
  retain ten. The 143-invocation filesystem/process signature matrix took
  119.9 seconds alone, so its sanitizer aggregate limit is 300 seconds.
  Other limits and assertions remain intact.
- macOS group cleanup could report EPERM between descendant pipe closure and
  zombie state. A diagnostic probe caught a runnable-looking process carrying
  `P_WEXIT`; the runtime now recognizes that exiting state while preserving
  genuine permission failures. The native regression repeats termination,
  EOF and repeated cleanup with an unrelated child kept alive. See the
  [precise contract](child-processes.md#current-setup-and-design-rationale).
- The HTTP peer incorrectly rejected a reset after sending an early 413.
  That fixture now accepts this cleanup outcome while still checking response
  completion, upload bytes and early upload termination. See
  [the independent expectations](../tests/http/README.md#preserved-independent-expectations).
- One hosted macOS job exhausted 45 minutes after its package and sanitizer
  stages. Its overall budget is now 60 minutes for three builds and all gates;
  individual test and network deadlines remain bounded.

The preceding [PR run](https://github.com/kubabialy/gloinc/actions/runs/37679022475)
retains the process and HTTP failures; the preceding
[push run](https://github.com/kubabialy/gloinc/actions/runs/37679015477) retains
the signature timeout and job cancellation. Neither is counted as a pass.
Publication updates documentation only. The release archives retain the
validated compiler, runtime, headers and runnable source payload. Their
documentation was refreshed, HTML links rechecked, and new checksums recorded.

## Release contents

- Gloin replacements for every repository-owned Python tool and test driver,
  including independent TLS/HTTP peers, JSON and million-record stress checks.
- Checked child processes, pipes, bounded capture, cwd/environment options,
  process groups and child stack limits; symlinks, canonical paths, exclusive
  temporary directories and checked replacement; reusable server TLS and
  explicit graceful shutdown. Host calls retain the GloinIR ABI boundary.
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

The [0.1.0 HTML guide](site/0.1.0/index.html) teaches the complete
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

## Publication

The completed candidate was approved for release on 2026-10-08. The final
implementation passed hosted macOS and Ubuntu x86_64 CI, including macOS
sanitizers; local Ubuntu ARM64 validation is recorded above. The site root and
version selector now open 0.1.0. Older numbered guides retain their contents.

Release archives carry the final publication documentation with unchanged
validated compiler, runtime, headers and runnable sources. Package HTML links,
payload equality and SHA-256 checksums are checked during publication. The
[release](https://github.com/kubabialy/gloinc/releases/tag/v0.1.0) includes the
archives, checksum files and provenance record. Historical milestone sections
above describe the evidence available at those earlier points in development.
