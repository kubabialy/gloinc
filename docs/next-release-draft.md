# Next release proposal (unreleased)

The latest published compiler is 0.0.4. **0.1.0 is prepared as an unreleased
candidate** for the current development batch. It targets Apple Silicon macOS
with LLVM/MLIR 21.1.6 and Ubuntu 24.04 Linux with LLVM/MLIR 21.1.8. Windows
support is not planned, though contributions are welcome.
This document proposes a boundary and records local validation. It is not a
release announcement.

**Linux acceptance is required for 0.1.0.** The Ubuntu ARM64 compiler builds,
runs through the JIT, emits PIC objects, links native executables, and passes
the full 918-case required core gate. Installed and relocated ARM64 packages
each passed 479 acceptance cases. Its serial and parallel complete suites each
passed 961/965, with only four documented async/spawn failures. The hosted
Ubuntu x86_64 CI passed the same core, package, and full-suite classification
gates. Publication still requires final review.

## Linux release gate

- The Ubuntu 24.04 installer pins the apt.llvm.org 21.1.8 shared toolchain and
  verifies its repository signing key. A Linux ARM64 Release build passed.
- Linux native output now emits PIC objects. JIT, `-O2` executable output, and
  ordinary `clang++` object linking ran successfully on Ubuntu ARM64.
- The package script handles `.so`, `ldd`, and `sha256sum` on Linux. Installed
  and relocated Ubuntu ARM64 packages each passed 479/479 acceptance cases,
  including version, checksum, native output, and module relocation checks.
- The required core suite passed 918/918 on Ubuntu ARM64 after correcting an
  ELF object assertion and raising the Linux trap-test timeout to account for
  four-way VM contention. The hosted x86_64 CI core gate also passed 918/918.

## Proposed contents

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
- Nonblocking IPv4 TCP sockets in `@net` and bounded HTTP/1.1 head parsing,
  fixed-length body framing, and formatting in `@http`, using built-in
  `result<T>` at their public boundary.
  See [networking](networking.md) and the
  [local round trip](../examples/network_http.gloin).
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

Maps, enum payloads and matching, package imports, and async/spawn are
outside this proposed release. A Windows port is outside the maintained plan.

## Candidate validation on Apple Silicon macOS

| Check | Result |
| --- | --- |
| `check-core` | 918/918 passed |
| Full parallel CTest suite | 961/965 passed; only the four documented unsupported async/spawn tests failed |
| Installed package acceptance | 479/479 passed |
| Relocated extracted package acceptance | 479/479 passed, including result and network example smoke runs |
| Example checking | 32/32 `.gloin` files under `examples/` passed `--check` |
| Gloin source formatter | 198 files checked; none need formatting |
| HTML links and runnable guide snippets | Seven pages have valid local links; candidate and development standalone examples passed `--check` |

## Candidate validation on Ubuntu 24.04 ARM64

| Check | Result |
| --- | --- |
| Pinned toolchain | apt.llvm.org LLVM/MLIR 21.1.8 shared targets configured and built |
| JIT and native smoke | Hello world ran via JIT and `-O2` executable; an emitted PIC object linked with ordinary `clang++` and exited 42 |
| `check-core` | 918/918 passed |
| Installed package acceptance | 479/479 passed |
| Relocated extracted package acceptance | 479/479 passed, including checksum, dependency, and network example checks |
| Complete suite | Serial and parallel each passed 961/965; exactly the four documented async/spawn cases failed |

## Candidate validation on Ubuntu 24.04 x86_64

The [hosted CI run](https://github.com/kubabialy/gloinc/actions/runs/37146653312)
passed its Linux job. The core gate passed 918/918. Installed and relocated
packages each passed 479/479 checks. The serial and parallel complete suites
each passed 961/965; the four failures are the documented async/spawn cases.

These checks were rerun after the candidate version bump. Build metadata
reports 0.1.0, and the numbered guide and release notes are prepared. The
0.1.0 archive checksum passed; its installed and relocated copies passed the
package gate. Publication has not occurred.

## Before publication

1. Review the 0.1.0 candidate boundary and compatibility notes. Hosted Ubuntu
   x86_64 CI and local ARM64 Linux and macOS validation have passed.
2. Keep the site root on published 0.0.4 while this is a candidate. After
   approval, remove candidate wording and update `latestVersion` and the root
   redirect; keep older numbered pages unchanged.
3. The required core gate, full-suite failure audit, installed and relocated
   package check, formatting check, HTML links, and archive checksum passed
   after the version change. Rerun affected checks if code or package contents
   change before publication.
4. Review the final diff and commit it before tagging or publishing the archive.
