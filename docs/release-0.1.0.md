# Gloinc 0.1.0 candidate guide

This page describes an **unreleased candidate**. The latest published version
is 0.0.4. The candidate targets Apple Silicon macOS with LLVM/MLIR 21.1.6 and
Ubuntu 24.04 Linux with LLVM/MLIR 21.1.8. The
[release draft](next-release-draft.md) records the exact validation boundary
for builds, JIT/native execution, tests, sanitizers and packages. Windows
support is not planned, though
contributions are welcome.

Child-process, filesystem and server-TLS APIs are implemented, and all
repository-owned Python-driver migrations are complete. The
[Gloin tooling record](tooling-roadmap.md) describes their implementation and
validation. Publication requires the completed candidate to pass the release
gates below.

The [0.1.0 HTML guide](site/0.1.0/index.html) teaches the language accepted by
this candidate. The [release notes](release-notes-0.1.0.md) list changes since
0.0.4 and compatibility limits. The [language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec)
also contains proposed features; the HTML guide and API pages describe the
implemented boundary.

## Build and run

Install Xcode Command Line Tools, CMake 3.28+, Ninja, OpenSSL 3, and the pinned toolchain.
On Apple Silicon macOS, from a source checkout:

```sh
brew install cmake ninja openssl@3
bash scripts/install-llvm.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
cmake --build build --target check-core
./build/gloinc --version
./build/gloinc --jit examples/result_handling.gloin
./build/gloinc --jit examples/network_http.gloin
./build/gloinc -O2 -o hello examples/hello_world.gloin
./hello
```

On Ubuntu 24.04 ARM64 or x86_64, use the pinned Linux packages:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ openssl libssl-dev
bash scripts/install-llvm-linux.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm \
  -DMLIR_DIR=/usr/lib/llvm-21/lib/cmake/mlir
cmake --build build -j 2
cmake --build build --target check-core
./build/gloinc -O2 -o hello examples/hello_world.gloin
./hello
```

`--version` reports `gloinc 0.1.0 (LLVM/MLIR 21.1.6)` on macOS or
`gloinc 0.1.0 (LLVM/MLIR 21.1.8)` on Linux. The result example
prints one line and exits with status 84; the hello executable prints `Hello
World!` and exits zero. The result example's nonzero exit is intentional, so a
shell using `set -e` should run it with explicit status handling.

Native executable output is the default: `gloinc source.gloin` writes `a.out`,
and `-o PATH` chooses the output. `--jit` or `--run` executes without creating
a binary. `--check`, `--emit-ir`, and `--emit-llvm` inspect without running;
`--emit-object -o PATH` creates an object. Native output defaults to `-O0` and
accepts opt-in `-O2`. `main() -> i32` supplies the low eight bits of the process
exit status and never prints it automatically. Use `std.print` or `std.println`
for output. Compiler/file/JIT errors exit 1, usage errors exit 2, and checked
runtime traps terminate the process.

The compiler itself, even for `--version`, requires the matching LLVM/MLIR
shared libraries and OpenSSL 3. It also needs the pinned LLVM tools to link a
native executable. Generated executables link the Gloin runtime statically and
do not need LLVM at runtime; TLS-enabled executables need OpenSSL 3 libraries.
The archive does not bundle LLVM, OpenSSL, or their dependencies.

## Installation and package

Install locally with `cmake --install build --prefix "$HOME/.local"`. The
compiler finds its installed standard modules under
`$HOME/.local/share/gloinc/stdlib`. The installed copy of the toolchain
installer is `share/gloinc/scripts/install-llvm.sh` under that prefix.

```sh
cmake --build build --target package
bash scripts/check-package.sh build build/package-check
```

The package target creates a `gloinc-0.1.0-macos-arm64.tar.gz`,
`gloinc-0.1.0-linux-aarch64.tar.gz`, or
`gloinc-0.1.0-linux-x86_64.tar.gz` archive for the current host, with a SHA-256
checksum. A candidate archive contains:

| Path below archive root | Contents |
| --- | --- |
| `bin/gloinc` | Compiler and JIT client |
| `bin/gloinfmt` | Gloin-written source formatter and recursive style checker; see [its guide](gloinfmt.md) |
| `lib/libgloin_runtime.a`, `lib/libgloin_runtime.dylib` or `.so` | Native runtime for executables and external LLVM use |
| `include/gloin/` | Native runtime ABI headers, including raw memory and direct byte I/O |
| `share/gloinc/stdlib/` | Source standard modules, including `memory.gloin`, `slices.gloin`, `vector.gloin`, `net.gloin`, `http.gloin`, `http_client.gloin`, `json.gloin`, and checked result APIs |
| `share/gloinc/examples/` | Runnable programs, including result handling, directory/package discovery, custom arena, binary NUL filtering, bounded JSON, a local HTTP round trip, and synchronous/streaming HTTP/HTTPS clients |
| `share/gloinc/core-fixtures/` | Source acceptance cases |
| `share/gloinc/scripts/install-llvm.sh`, `install-llvm-linux.sh` | Platform installers for the pinned external LLVM/MLIR toolchains |
| `share/doc/gloinc/docs/site/0.1.0/` | Candidate HTML language guide |
| `share/doc/gloinc/docs/` | API, ownership, lowering, and diagnostic guides |
| `share/doc/gloinc/examples/`, `share/doc/gloinc/stdlib/` | Source copies preserving the guides' relative links, including example modules and data |
| `share/doc/gloinc/CONTRIBUTING.md` | Contribution and manual verification rules |

`check-package.sh` installs into one prefix and extracts the archive into a
different prefix whose path contains spaces. It runs CLI, standard library,
module, compiler, and source acceptance cases against both copies. It also
checks the version, archive checksum, installed HTML links, executable
output, and module relocation behavior. See [the full check script](../scripts/check-package.sh).

To inspect a macOS archive from the build directory:

```sh
cd build
LC_ALL=C shasum -a 256 -c gloinc-0.1.0-macos-arm64.tar.gz.sha256
tar -xzf gloinc-0.1.0-macos-arm64.tar.gz
./gloinc-0.1.0-macos-arm64/bin/gloinc --version
```

On Linux, substitute the host architecture (`aarch64` or `x86_64`):

```sh
cd build
arch=$(uname -m)
sha256sum -c "gloinc-0.1.0-linux-$arch.tar.gz.sha256"
tar -xzf "gloinc-0.1.0-linux-$arch.tar.gz"
"./gloinc-0.1.0-linux-$arch/bin/gloinc" --version
```

Run the bundled platform installer first if the matching toolchain is absent.
An archive built on a newer macOS version is not claimed to run on an older
system. Linux packaging is scoped to Ubuntu 24.04 and the architecture named
by the archive. Intel macOS, cross compilation, and bundled LLVM are outside
this candidate.

## Verification boundary

The complete hosted tooling gate passed on candidate commit `3ce4e81`, with
independently inspected test reports and archive checksums:

| Check | macOS 15 ARM64 | Ubuntu 24.04 x86_64 |
| --- | --- | --- |
| Required `check-core` | 987/987 passed | 987/987 passed |
| Installed package acceptance | 501/501 passed | 501/501 passed |
| Relocated package acceptance | 501/501 passed | 501/501 passed |
| Complete suite, serial | 1,030/1,034 passed | 1,030/1,034 passed |
| Complete suite, four parallel jobs | 1,030/1,034 passed | 1,030/1,034 passed |
| Debug ASan/UBSan core | 987/987 passed | Not part of this job |

Both complete-suite audits contained exactly the four documented unsupported
async/spawn failures, with no skipped tests. Installed and relocated compilers
also passed TLS certificate checks, synchronous and streaming HTTP/HTTPS
matrices, and JSON codec/state/limit checks. JSON ran in JIT, `-O0`, and `-O2`
modes. All 48 example sources passed `--check` locally on macOS and Ubuntu
ARM64. Formatting,
HTML links, archive checksums, native output, and relocation checks passed.

The [validation record](next-release-draft.md#current-local-validation) explains
local ARM64 results, corrected cleanup races, test budgets and retained failed
attempts. The [test inventory](../tests/README.md) describes the checks.
Both the [PR workflow](https://github.com/kubabialy/gloinc/actions/runs/37695280617)
and [push workflow](https://github.com/kubabialy/gloinc/actions/runs/37695275816)
passed the corrected code. The [candidate PR](https://github.com/kubabialy/gloinc/pull/6)
tracks later documentation revisions and their checks. Rerun affected checks
when candidate code or package contents change. A public tag and release
require a separate final decision.
