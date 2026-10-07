# Gloin tooling required for 0.1.0

Repository-owned Python tools and test drivers must have Gloin replacements
before 0.1.0 is published. This is an additional release requirement, not a
claim that the APIs below are implemented. The existing C++ compiler/runtime
tests and the CMake build remain part of the toolchain.

## First milestone: child lifecycle

On 2026-10-07, [explicit child launch and lifecycle](child-processes.md) passed
34/34 process/context tests on macOS ARM64 and Ubuntu ARM64, including Gloin
JIT and native `-O0`/`-O2` execution. The macOS core gate passed 944/944; all six
new native child-process cases also passed under ASan/UBSan. The runnable
example, guide program, repository formatting and HTML links passed checks.
These are local milestone results; full Linux, sanitizer and package release
gates must run again as the remaining tooling work is completed.

## Second milestone: pipes, capture and deadlines

The implementation now provides `start_piped`, nonblocking stdin writes,
stdout/stderr reads, readiness waits, `wait_timeout`, and bounded binary
`communicate` with a monotonic deadline. Capture, limits and lifecycle logic
are written in Gloin; native host calls cross `gloin.abi_call`. The Gloin stress
peer fills both output pipes before reading stdin. Tests cover binary bytes,
exact/overflow/empty limits, early stdin closure, timeouts and cleanup.

Validation on 2026-10-07: macOS passed 40/40 process/context cases and the
950/950 core gate. All 11 native child-process cases passed under macOS
ASan/UBSan and on Ubuntu ARM64. The Gloin pipe fixture passed JIT, native `-O0`
and native `-O2` on both hosts. The capture example, `gloinfmt --check .` and
HTML link checks passed. Full Linux and package release gates still need a
fresh run on the completed tooling work.

## Third milestone: launch options and process-group cleanup

`Options` and `start_with` now select child cwd, inherited or fully supplied
environment, pipes and a new process group. Duplicate environment names and
ambiguous options are rejected. Grouped handles retain the leader's wait status
until close so cleanup can safely signal ordinary descendants after the leader
exits. This does not contain descendants that deliberately leave the group.
The [current setup and rationale](child-processes.md#current-setup-and-design-rationale)
explain the opt-in defaults, OS boundary and absent strict-containment backend.
All new host calls use `gloin.abi_call`; configuration and lifecycle control
remain in Gloin. See the [ownership contract and example](child-processes.md).

Validation on 2026-10-07: the macOS core gate passed 956/956 and the focused
process/context suite passed 46/46. All 16 native child-process cases passed on
Ubuntu ARM64 and under macOS ASan/UBSan. The launch-options fixture passed JIT,
native `-O0` and native `-O2` on both platforms. Checks include a descendant
holding output open after leader exit, cached exit status, unrelated-child
survival, missing-cwd descriptor cleanup and rejecting signals after external
reaping. Formatting and candidate HTML links passed. Full Linux and package
release gates still need a fresh run after the remaining tooling work.

## Fourth milestone: symbolic links

`fs.symlink(target, link_path) -> result<void>` creates without replacing an
existing entry. `fs.read_link(&memory, path, max_bytes) -> result<string>` returns
the immediate stored text, including broken/cyclic links, with an explicit bound
and no truncated success. Host calls pass through `gloin.abi_call`; public
results and arena ownership remain in Gloin. The [filesystem guide](filesystem-process.md#symbolic-links)
documents relative targets, host resolution, allocation costs and errors.
The formatter fixture now creates its traversal symlink in Gloin.

Validation on 2026-10-07: the macOS core gate passed 961/961; the focused
process/context/formatter run passed 52/52. The symlink fixture passed JIT,
native `-O0` and native `-O2` on macOS and Ubuntu ARM64. Four native symlink
checks passed on Ubuntu and under macOS ASan/UBSan. A final host-count guard
also passed six focused macOS cases and the four native checks on both hosts,
including a large virtual buffer to catch Linux syscall count narrowing.
Formatting and all seven HTML pages' local links passed. The package and full
Linux release gates still need fresh runs when the tooling work is complete.

## Fifth milestone: filesystem workspaces and a Gloin formatter driver

`fs.canonical_path`, `fs.temp_dir` and `fs.remove_dir` now expose bounded existing
path resolution, exclusive private temporary directories and empty-directory
removal through built-in results and `gloin.abi_call`. All output allocation
precedes directory creation. The [filesystem guide](filesystem-process.md#canonical-paths-and-temporary-directories)
explains costs, lifetime, symlink behavior and namespace-race limits.

The formatter [runner](../tests/formatter/runner.gloin) now owns temporary
storage, child launch, bounded capture, deadlines and fixture cleanup in Gloin.
CTest invokes it directly; the temporary CMake driver is removed. Original
native/JIT assertions, independent token checks and golden fixtures remain.
Cleanup is private to stable, exclusively created fixture trees and is tested
against broken/cyclic links and links to a separate sentinel directory. Failed
runs retain evidence. This does not add a public recursive-deletion API.

Validation on 2026-10-07: macOS passed 58/58 focused process/context/formatter
cases and the 967/967 core gate. The workspace fixture passed JIT, native
`-O0` and native `-O2` on macOS and Ubuntu ARM64. The Gloin formatter driver
passed on both hosts. All four new native filesystem tests passed on Ubuntu
and under macOS ASan/UBSan. A deliberate child failure preserved capture status
and fixture evidence. A fresh macOS install included all five new process and
filesystem examples; its workspace example passed JIT and native `-O2` and
removed its directory. Formatting and seven HTML pages' local links passed.
Full Linux, hosted CI and complete installed/relocated release gates still
need fresh runs after the remaining tooling migrations.

## Sixth milestone: four Python tools retired

`gloinfmt --write` and `--git` now provide the remaining formatter workflow.
Token preservation and idempotence are checked before writing. The new
`fs.replace_file` stages and syncs a sibling, preserves ordinary permissions
and uid/gid, checks the original bytes/metadata, and replaces by rename.
Final symlinks, hard links, non-owner files and special mode bits are rejected.
The [contract](filesystem-process.md#checked-file-replacement) explicitly excludes
concurrent editing guarantees, ACL/xattr preservation and crash durability.
The native operation crosses `gloin.abi_call`.

The Gloin-written `gloin-check-docs` and `gloin-check-suite` replace the HTML
link and complete-report scripts. Their bounded HTML/XML readers, supported
syntax, stricter malformed-input rejection and exit codes are documented in
the [repository-tool guide](repository-tools.md). CI now runs them after build.

`gloin-json-test` replaces the JSON driver. It retains all 142 accepted
documents, 53 rejected documents and 54 Unicode cases, and runs the matrix,
state/limit fixture and example in JIT, `-O0` and `-O2`. Expected values were
frozen from independent codecs and checked separately against the documented
encoder spelling; [provenance](../tests/fixtures/json/README.md) forbids deriving
the oracle from the production codec. Installed/relocated package checks now
compile and run this Gloin driver. Shared capture/cleanup lives in
`tests/support/tool_runner.gloin`.

Validation on 2026-10-07: the macOS core gate passed 972/972 with 1,019 total
CTest cases discovered. Seven targeted Linux cases passed, covering the three
native replacement tests, formatter driver, native/JIT tooling checks and JSON
driver. The three replacement tests passed macOS ASan/UBSan, including a real
partial-write failure induced by a child file-size limit. The Gloin classifier
matched saved complete macOS/Linux serial and parallel reports. Seven HTML
pages and both recursive/Git formatter checks passed.

A fresh macOS installation and its moved prefix passed all JSON cases in JIT,
`-O0` and `-O2` with each package's compiler/example; the installed formatter
performed a checked write and the relocated formatter checked the result.
These are focused install/relocation checks, not a fresh complete CPack gate.
Full Linux, full installed/archived/relocated gates and hosted CI remain due
after the remaining tooling work.

Removed: `scripts/format-gloin.py`, `scripts/check-doc-links.py`,
`scripts/check-full-suite.py` and `tests/json_smoke.py`. At that milestone four
Python files remained; the stress migration follows below. Explicit executable
search paths and stream redirection also remain pending.

## Seventh milestone: stack limits and the stress driver

`process.Options.stack_limit_bytes` now sets the child's soft stack limit
before execution, preserving the parent's limits and the child's inherited hard
ceiling. Zero inherits; invalid or unsupported requests fail explicitly. It
crosses the existing source-typed GloinIR / `gloin.abi_call` boundary. The
[process guide](child-processes.md#child-stack-limits) documents the child-only
launch path, macOS main-thread restriction, Linux descriptor-closure requirement,
blocking setup and embedding responsibilities.

The Gloin-written `gloin-stress-test` replaces
`scripts/check-integrated-examples.py`. It preserves million-record limits,
one-over-limit rejection, 2 MiB child stack limits, 60-second communication
budgets and independent expected simulation values. Fixtures use bounded
reusable write blocks; failures retain evidence, successes clean up.
Both drivers passed before removal of the Python script. The
[oracle provenance](../tests/integrated/README.md) records exact reference
arithmetic instead of deriving expectations with `@random`.

`GloinIntegratedStress.MillionRecords` is required by `check-core`. Installed
and relocated package checks compile the driver with the selected compiler
and exercise the packaged examples.

Validation on 2026-10-07: macOS passed **978/978** required core cases, with
**1,025** total CTest cases discovered. All nine focused Ubuntu ARM64 checks
passed: native stack-limit cases, Gloin JIT/`-O0`/`-O2` options and OS probes,
signature/ABI validation and the million-record driver. The four new native
cases also passed macOS ASan/UBSan.

A fresh macOS installation and its moved prefix each compiled and passed the
Gloin stress driver using their packaged examples; the options example passed
installed JIT and relocated native execution. Seven HTML pages, recursive/Git
formatting, shell syntax and diff whitespace checks passed. These are focused
install/relocation checks, not a new complete CPack gate. Full Linux, full
sanitizer, complete serial/parallel suites, archived-package gates and hosted CI
must still be rerun on the completed tooling work.

## Eighth milestone: server TLS and graceful shutdown

`net.TlsServerConfig` loads a PEM chain/key once and creates nonblocking server
sessions on connected sockets. Configurations and sessions have explicit shared
ownership state; existing sessions retain their configuration after it closes.
Both client/server sessions expose readiness-driven `shutdown`, distinguish
authenticated TLS END from transport truncation, and reject protocol use after
fatal errors. Raw plaintext reuse after TLS cleanup is prohibited. Linux TLS
operations now suppress SIGPIPE per calling thread without changing the host's
process-wide disposition. All four new native calls use `gloin.abi_call`.
See the [complete contract and example](server-tls.md).

`gloin-tls-test` replaces `tests/tls_client_smoke.py`. It invokes an explicitly
selected OpenSSL CLI to generate local certificates and runs trusted, wrong-host,
untrusted, alias and shutdown checks in JIT/`-O0`/`-O2`. Native tests pair the
server runtime with an independent direct-OpenSSL client, including TLS 1.2/1.3,
old-protocol rejection, backpressure, encrypted/mismatched-key rejection and
abrupt EOF. The [test guide](../tests/tls/README.md) records the preserved
expectations. Installed/relocated checks now compile and run this Gloin driver.
The old TLS driver passed before removal.

Validation on 2026-10-07: macOS `check-core` passed **987/987**, with **1,034**
total CTest cases. Ten focused macOS and Ubuntu ARM64 checks cover the seven
native TLS cases, two compiler/API cases and Gloin driver. All seven native
cases passed macOS ASan/UBSan. Final review strengthened encrypted-key rejection
to include empty passwords; the focused and sanitizer checks were repeated
after that change.

Fresh macOS installed and relocated prefixes each compiled and passed the
Gloin driver in JIT/`-O0`/`-O2` and type-checked the packaged server example.
The native server example exchanged PING/PONG and completed graceful shutdown
with an independent OpenSSL CLI client. Seven HTML pages, recursive/Git
formatting, shell syntax and diff whitespace checks passed. These focused
package results do not replace complete CPack, full Linux/sanitizer,
serial/parallel complete-suite and hosted CI gates on the final release tree.

## Ninth milestone: HTTP drivers in Gloin

`gloin-http-test` replaces the final two Python drivers. Its buffered and
streaming modes launch concurrent Gloin HTTP/HTTPS peers, retain independent
literal wire responses and request checks, and run the fixtures and examples in
JIT/`-O0`/`-O2`. The [driver contract](../tests/http/README.md) documents all
cases, byte limits, deadlines, ownership and retained failure evidence.

Coverage includes malformed framing, binary bodies, TLS verification failures,
1 MiB uploads/downloads, early rejection, paused producers/consumers,
backpressure, cancellation, graceful EOF, slow progress and 32 simultaneous
exchanges. The old drivers passed before removal. Timer deadlines bound the
peer's readiness wait, preserving deliberate 40 ms delays without threads or
busy waiting. New code uses ordinary source-typed GloinIR and the existing
verified native boundaries; no new runtime primitive was needed.

The inline Python documentation server is replaced by
[http_echo_server.gloin](../examples/http_echo_server.gloin), a bounded,
single-request loopback example packaged with the compiler.

Validation on 2026-10-07: macOS `check-core` passed **987/987**. Both new
HTTP suites also passed on Ubuntu 24.04 ARM64, covering **354 connections**
across JIT/`-O0`/`-O2` on each platform. A deliberately corrupted
Authorization fixture failed at the independent peer check and retained its
diagnostics. The documentation server passed with a Gloin JIT client, and its
native executable returned the expected HTTP head/body to curl. Inspection of
the emitted IR confirms `gloin.abi_call` for sockets, TLS and readiness waits.

Fresh macOS installed and relocated prefixes each compiled the HTTP driver
and passed both suites with their packaged examples, then type-checked the
packaged documentation server. Seven HTML pages and their local links,
recursive/Git formatter checks, shell syntax and diff whitespace checks passed.
These focused package checks do not replace full CPack acceptance, complete
serial/parallel macOS/Linux suites, the full sanitizer gate or hosted CI on
the final 0.1.0 release tree.

## Inventory

No repository-owned Python files or Python tool/test invocations remain.
CMake no longer requires a Python interpreter; CI setup, package checks and
current installation documentation no longer list it as a project dependency.
Upstream dependencies retain their own build requirements.

Formatter, documentation checks, full-suite classification, JSON, integrated
stress, TLS and HTTP orchestration run in Gloin. The migrations preserve
independent JSON, simulation and protocol expectations.

## Required standard-library capabilities

### Child processes and pipes

The first [child-process milestone](child-processes.md) now provides explicit
executable/argument launch, inherited standard streams/environment/cwd,
poll/wait, exit status, termination and direct-child cleanup. Nonblocking pipes,
bounded concurrent capture, deadlines, child cwd, inherited/supplied environment
and opt-in process-group cleanup are implemented. Groups cover ordinary
descendants; they do not contain processes that leave the group. Add executable
lookup through an explicitly selected search path.
Arguments are counted values passed directly to the executable; shell parsing
must require explicitly launching a shell.

Support inherited, discarded and redirected standard streams, pipes, closing
child stdin, and bounded capture of stdout and stderr. A child that fills both
output pipes must not deadlock the parent. Waiting, polling, normal exit codes,
signal termination, deadlines, termination escalation and reaping need defined
outcomes. A nonzero child exit is an observed outcome, distinct from failure to
start or wait for that child. Use the new-group option for timeout cleanup of ordinary descendants that
could retain pipe ends; preserve the documented limits for escaped descendants.

The stress runner uses the explicit child soft stack limit, applied before the
child program starts. Process handles have documented ownership, alias rules,
and cleanup on failed setup and every terminal path. Prefer ordinary Gloin
control flow for orchestration and a small native runtime boundary for host
operations. New fallible public APIs use built-in `result<T>`.

### Filesystem operations

`fs.symlink` and bounded `fs.read_link` now cover creation and immediate target
reading, using built-in results. Bounded canonical path resolution, unique
temporary directories and empty-directory removal are implemented too. Existing `fs.metadata`
already uses `lstat`, and `fs.remove_file` unlinks a symlink itself. Preserve that
distinction when adding operations that follow links.

The formatter builds private recursive cleanup from explicit traversal and
removal, assuming a stable owned fixture tree. A public recursive-removal API
would need a separate contract for concurrent mutation and symlink races. Temporary directory creation must be
exclusive. An in-place formatter needs a documented replacement policy,
including permissions, symlinks and what remains after a failed write.

### Timed waits and TLS servers

The HTTP peers combine monotonic deadlines with bounded `net.wait_many` waits
for deliberate delays. A public standalone sleep API is not needed by these
migrations and remains a separate library decision.

Reusable server TLS configuration and nonblocking accepted sessions are now
implemented. Handshake, reads, writes and graceful shutdown expose readiness
requirements. Tests distinguish authenticated TLS EOF (`close_notify`) from
truncated transport and abrupt cleanup. `shutdown` provides the bidirectional
state machine; `close` remains best-effort cleanup.

The test peers can run as child processes and service connections through
readiness loops. Native threads and language-level async/spawn are not required
for these migrations. Certificate generation can continue to use OpenSSL's CLI;
implementing cryptography in Gloin is outside this tooling milestone.

## Preserve independent evidence

Porting a test must preserve what it proves. In particular:

- JSON expected strings/documents must come from independently verified fixture
  data or an independent test implementation. Calling `@json` to compute both
  actual and expected results is insufficient. Retain the existing accepted,
  rejected, Unicode, state and limit cases in all three execution modes.
- The simulation oracle uses wrapping 64-bit operations and exact arithmetic
  beyond 64 bits. A direct translation to checked `u64` or `f64` changes its
  meaning. Use a separate limb implementation or verified reference vectors;
  do not derive expected values through the `@random` code being tested.
- HTTP peers must retain independently specified wire bytes and malformed
  responses. Do not use the production parser/serializer to decide whether
  its own behavior is correct.
- The full-suite classifier must still reject malformed reports, duplicate
  names, skipped cases and any change to the exact deferred-failure set.

Maps, a general HTML/XML library, arbitrary-precision language types, regexes
and async/spawn are not prerequisites: bounded collections, purpose-built
readers and explicit state machines can cover these tools.

## Implementation order and release gate

1. Add child processes/pipes and filesystem primitives, then move the formatter
   driver completely into Gloin. Test binary/large output, both output streams,
   arguments with spaces, failed launch, timeouts, signals, stale aliases and
   cleanup; test normal, broken and cyclic symlinks.
2. Formatter writing, documentation/report tools, JSON and the stress runner
   are done. Independent expected results and the child stack limit are preserved.
3. Server TLS, graceful shutdown, TLS/HTTP drivers and the documentation server
   are migrated, including concurrent and delayed peers.
4. CMake, CI and package checks use the Gloin replacements. Gloin tools build
   after the compiler. The repository's mandatory Python test/tool dependency
   is removed; upstream build dependencies remain separate.
5. Verify source-typed GloinIR and `gloin.abi_call` at native boundaries; run
   JIT/native, macOS/Linux, sanitizer, installed and relocated-package gates.
   All Gloin tools must pass `gloinfmt`. Document the public APIs and their
   ownership, limits, failure and cleanup contracts in the candidate guide.

Existing release validation predates these additions. Repeat affected gates on
the completed implementation before publishing 0.1.0.
