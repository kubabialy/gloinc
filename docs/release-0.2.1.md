# Gloinc 0.2.1 release guide

Version 0.2.1 adds arena-backed hash maps with built-in keys and generic values.
The [release notes](release-notes-0.2.1.md) describe the changes and compatibility;
the [0.2.1 HTML guide](site/0.2.1/index.html) teaches the supported language.

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

Both platforms use CMake 3.28+, Ninja and a C++23 compiler. Set
`-DBUILD_TESTING=OFF` for a compiler-only build without GoogleTest. Run:

```sh
build/gloinc --version
build/gloinc --jit examples/hashmap.gloin
build/gloinc -O2 -o hello examples/hello_world.gloin
./hello
build/gloinfmt --check .
cmake --build build --target check-core
```

The compiler reports `gloinc 0.2.1 (LLVM/MLIR 21.1.6)` on macOS and
`gloinc 0.2.1 (LLVM/MLIR 21.1.8)` on Linux. The examples print `apple=2` and
`Hello World!`, respectively, and exit zero. Native executable output is the
default; without `-o`, output goes to `a.out`. `--jit`/`--run` executes in
process. The default optimization level is O0; O2 applies to native output.
See the [CLI guide](cli.md).

## Installation and package

```sh
cmake --install build --prefix "$HOME/.local"
cmake --build build --target package
bash scripts/check-package.sh build build/package-check
```

Archives are `gloinc-0.2.1-macos-arm64.tar.gz`,
`gloinc-0.2.1-linux-aarch64.tar.gz`, and `gloinc-0.2.1-linux-x86_64.tar.gz`,
each with a SHA-256 file. They contain the compiler, formatter, static/shared
runtimes, ABI headers, matching source standard library, runnable examples,
acceptance fixtures, toolchain installers and versioned documentation.
HashMap's module, runtime header, example and guide are included.

**LLVM/MLIR and OpenSSL 3 are required even for the compiler's `--version`.**
Install the matching platform toolchain using the packaged scripts under
`share/gloinc/scripts/`, and OpenSSL through Homebrew or Ubuntu packages.
These dependencies are not bundled. Generated executables embed the static
Gloin runtime and do not need LLVM; TLS programs still need OpenSSL 3.

```sh
LC_ALL=C shasum -a 256 -c gloinc-0.2.1-macos-arm64.tar.gz.sha256
tar -xzf gloinc-0.2.1-macos-arm64.tar.gz
./gloinc-0.2.1-macos-arm64/bin/gloinc --version
./gloinc-0.2.1-macos-arm64/bin/gloinc --jit \
  ./gloinc-0.2.1-macos-arm64/share/gloinc/examples/hashmap.gloin
```

On Ubuntu use `sha256sum -c` and the corresponding Linux archive name. The
compiler prefix may move; its external toolchain must stay at the configured
location. Linux packages target Ubuntu 24.04. Archives built on newer macOS
versions are not claimed to run on older systems. Intel macOS, cross-compilation
and Windows are outside the maintained release matrix.

## Verification boundary

Publication requires the following gates on the release source:

- Required core tests on macOS ARM64 and Ubuntu ARM64/x86_64.
- Installed and relocated package matrices, including HashMap, TLS, HTTP/HTTPS,
  JSON, million-record programs, native output and archive checksums.
- Complete serial/parallel suite classification with exactly the four documented
  unsupported async/spawn failures and no new failures or skipped tests.
- macOS ASan/UBSan core, a compiler-only build, source formatting, documentation
  links, runnable examples and the numbered guide.
- Hosted macOS and Ubuntu x86_64 CI on the final committed source. Local Ubuntu
  ARM64 execution does not substitute for x86_64 verification.

The [GitHub release](https://github.com/kubabialy/gloinc/releases/tag/v0.2.1)
attaches the actual run links, counts, source revisions, checksums and package
provenance. These reports identify the checked source; a source checkout alone
is not proof that all platform gates ran. Earlier 0.2.0 measurements remain in
their [historical record](release-0.2.0.md#verification-boundary).

ASan/UBSan instruments project C++ and native runtime code, not prebuilt LLVM
or generated Gloin machine code. The existing MLIR type-storage ASan exception
retains UBSan; leak detection is outside the macOS validation claim. See the
[toolchain guide](toolchain.md#sanitizer-build) and [test inventory](../tests/README.md).

## Release provenance

Tag `v0.2.1` identifies the release source. Assets include the three platform
archives and their checksums, plus evidence connecting each archive to its
build and validation. Rebuilt archives need fresh checksum/package verification.
The documentation root and version selector open 0.2.1; published directories
for older versions retain their release text.
