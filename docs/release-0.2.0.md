# Gloinc 0.2.0 release guide

Version 0.2.0 covers
switch/match, the standard-library result migration and compiler fixes described
in the [release notes](release-notes-0.2.0.md). The [0.2.0 HTML guide](site/0.2.0/index.html)
teaches this version; the [development record](release-0.2.0-draft.md) retains
the implementation history and earlier validation boundaries.

## Build and run

On Apple Silicon macOS, install Xcode Command Line Tools and the pinned toolchain:

```sh
brew install cmake ninja openssl@3
bash scripts/install-llvm.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
```

On Ubuntu 24.04 ARM64 or x86_64:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ openssl libssl-dev
bash scripts/install-llvm-linux.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm \
  -DMLIR_DIR=/usr/lib/llvm-21/lib/cmake/mlir
cmake --build build -j 2
```

Both platforms use CMake 3.28+, Ninja and a C++23 compiler. A compiler-only build
can set `-DBUILD_TESTING=OFF`; it needs no GoogleTest. Run:

```sh
./build/gloinc --version
./build/gloinc --jit examples/switch_match.gloin
./build/gloinc -O2 -o hello examples/hello_world.gloin
./hello
./build/gloinfmt --check .
cmake --build build --target check-core
```

The version is `gloinc 0.2.0 (LLVM/MLIR 21.1.6)` on macOS or
`gloinc 0.2.0 (LLVM/MLIR 21.1.8)` on Linux. The examples print `PONG` and
`Hello World!`, respectively, and exit zero.

Native executable output is the default; without `-o`, the compiler writes
`a.out`. `--jit`/`--run` executes in process. `-O2` enables native optimization;
the default is `-O0`. `main() -> i32` supplies the process exit status without
printing it. See the [CLI guide](cli.md) for checking, IR, objects and timing.

**The compiler needs the matching LLVM/MLIR shared libraries and OpenSSL 3 even
for `--version`.** The pinned LLVM tools are also needed for native linking.
Generated executables embed the static Gloin runtime and do not need LLVM;
TLS-enabled programs need OpenSSL 3 shared libraries. Packages do not bundle
LLVM, OpenSSL or their dependencies.

## Installation and package

```sh
cmake --install build --prefix "$HOME/.local"
cmake --build build --target package
bash scripts/check-package.sh build build/package-check
```

Packages are named `gloinc-0.2.0-macos-arm64.tar.gz`,
`gloinc-0.2.0-linux-aarch64.tar.gz` or `gloinc-0.2.0-linux-x86_64.tar.gz`, with a
separate SHA-256 file. Each archive contains:

| Location | Contents |
| --- | --- |
| `bin/` | `gloinc` and the Gloin-written `gloinfmt` |
| `lib/`, `include/gloin/` | Static/shared runtimes and native ABI headers |
| `share/gloinc/stdlib/` | Source standard modules matching this compiler |
| `share/gloinc/examples/` | Runnable examples, modules and data, including switch/match |
| `share/gloinc/core-fixtures/` | Source acceptance fixtures |
| `share/gloinc/scripts/` | macOS/Linux installers for the pinned toolchains |
| `share/doc/gloinc/docs/site/0.2.0/` | Versioned HTML language guide |
| `share/doc/gloinc/` | README, contribution rules, API/release guides, linked source copies and third-party notices |

The language specification and implementation checklist remain in the
[GitHub wiki](https://github.com/kubabialy/gloinc/wiki); they are not packaged
source files. Older numbered HTML guides are retained unchanged.

After running the platform installer, verify and extract the archive. On macOS:

```sh
cd build
LC_ALL=C shasum -a 256 -c gloinc-0.2.0-macos-arm64.tar.gz.sha256
tar -xzf gloinc-0.2.0-macos-arm64.tar.gz
./gloinc-0.2.0-macos-arm64/bin/gloinc --version
```

On Ubuntu, use `sha256sum -c` and the `linux-aarch64` or `linux-x86_64` filename.
Add the extracted `bin/` directory to `PATH`. The compiler prefix may move;
the external toolchain must remain at its configured location. An archive built
on a newer macOS version is not claimed to run on older systems. Linux packages
target Ubuntu 24.04. Intel macOS and cross compilation are outside this release.
Windows support is not planned; contributions are welcome.

## Verification boundary

Local validation on 2026-10-19 uses macOS 15.1.1 ARM64 / LLVM 21.1.6 and
Ubuntu 24.04 ARM64 / LLVM 21.1.8. These are working-tree candidate checks,
not hosted results for a published tag. Hosted run links, reports, archive
provenance and checksums accompany the
[GitHub release](https://github.com/kubabialy/gloinc/releases/tag/v0.2.0).

| Check | macOS ARM64 | Ubuntu ARM64 |
| --- | --- | --- |
| Required core | 1,078/1,078 passed | 1,078/1,078 passed |
| Installed package | 552/552 passed | 552/552 passed |
| Relocated package | 552/552 passed | 552/552 passed |
| Complete suite, serial | 1,121/1,125 passed | 1,121/1,125 passed |
| Complete suite, four workers | 1,121/1,125 passed | 1,121/1,125 passed |
| Debug ASan/UBSan core | 1,078/1,078 passed | Not part of this local gate |

All four complete-suite reports contain exactly the four documented
unsupported async/spawn failures, with no skipped or duplicate cases. The
Gloin report classifier and a separate XML review agree. A fresh macOS build
with `BUILD_TESTING=OFF` passed JIT, O0/O2, standalone object linking, default
`a.out`, selection, arena/module and standard-library examples without fetching
GoogleTest. All 49 repository example sources and 26 complete HTML programs
pass checking on both platforms. On macOS, those HTML programs passed 78
JIT/O0/O2 executions with expected outputs and exit statuses.

Linux's initial parallel audit timed out two matrices that deliberately crash
many child processes. Both passed alone under unchanged deadlines: 60.96 s
against 120 s, and 231.28 s against 360 s. Serializing only those two exposed
the same contention in a GIR trap matrix. CTest now runs the large arithmetic
and GIR CLI matrices alone on Linux; other tests stay parallel. The final core
gate and both complete-suite audits pass with that scheduling. Assertions,
signal behavior, timeouts and host crash-report settings are unchanged. Initial failures and the interrupted
intermediate attempt are retained in the local evidence.

Both platforms passed installed and relocated TLS certificate/shutdown checks,
HTTP/HTTPS client and streaming matrices, JSON codec/state/limit checks and the
million-record programs. Native executable output, default `a.out`, standalone
objects, switch/match in JIT/O0/O2, archive checksums, formatter checks and all
eight HTML pages' local links pass. Removing a relocated standard module fails
as expected, confirming that compilation does not fall back to the checkout.
An offline browser check reproduced directory navigation in the version
selector. Shared navigation now uses explicit `index.html` files; switching
between published, candidate and development guides passes in Chrome without
editing older versioned pages. The site root also opens its guide offline.

The release gates are:

- Required core tests on macOS and Linux, plus macOS ASan/UBSan.
- Installed and relocated package checks, including TLS, HTTP/HTTPS, JSON,
  million-record programs, native output, checksums and HTML links.
- Complete serial/parallel suite classification: exactly the four documented
  unsupported async/spawn failures, with no new failures or skips.
- Source formatting, runnable examples and the numbered guide.
- Hosted macOS and Ubuntu x86_64 CI on the final committed revision. Local
  Ubuntu ARM64 validation does not substitute for x86_64 execution.

ASan/UBSan checks project C++ and native runtime code; prebuilt LLVM and generated
Gloin machine code are not instrumented. `GloinDialect.cpp` retains the existing
ASan exception for type storage sharing an allocator with uninstrumented MLIR;
UBSan remains enabled there. Leak detection is disabled for this local audit.
The [toolchain guide](toolchain.md#sanitizer-build) gives the build commands.
The [test inventory](../tests/README.md)
describes the supported core and the four deferred-feature failures.

## Release provenance

The `v0.2.0` tag identifies the release source. Publication requires passing
hosted macOS and Ubuntu x86_64 checks for that revision and verified local
Ubuntu ARM64 packages. Release assets include the platform archives, SHA-256
files and validation evidence linking each archive to its source and build.
The site root and version selector open 0.2.0; earlier numbered guides remain
unchanged. The wiki specification and checklist record the matching feature
boundary. Rebuilt archives require fresh checksum and package verification.
