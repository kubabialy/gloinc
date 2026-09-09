# Gloin compiler

Gloin is a C++23 compiler project built on LLVM/MLIR. It is under development;
the [language specification](SPEC.md) describes the intended language, not a list
of completed features. [SPEC-TODO.md](SPEC-TODO.md) tracks implementation and
verification evidence in order.

## Current status

Fresh builds work locally and in hosted CI on Apple Silicon macOS with LLVM/MLIR 21.1.6.
The CLI still tokenizes a hardcoded string and ignores input-file arguments.
It cannot yet compile or run the programs under `examples/`.

| Area | Verified status |
| --- | --- |
| Build | Shared compiler libraries, optional tests, pinned GoogleTest, consistent shared LLVM/MLIR linkage. |
| External execution tests | Five small integer/arithmetic/variable/if/while programs pass through the test harness and external MLIR tools. |
| Full test suite | 127 tests discovered; local serial/parallel runs both have 114 passes, 13 failures, no crashes. |
| Parsing and semantic analysis | Structured source diagnostics and stage failure propagation work; vocabulary, grammar, and type checking remain partial. |
| Generics | Four IR-string checks pass; generic execution is not established. |
| JIT | Smoke test fails on missing builtin LLVM translation registration. |
| CLI, imports, for-loops, concurrency | Incomplete: SPEC-017, SPEC-020, SPEC-023/029/030, SPEC-040/041. |

[Compiler diagnostics](docs/diagnostics.md) now connect parsing, checking, and high-level
codegen through `compile_source`; the CLI connection remains SPEC-020.

Test pass counts are not specification-coverage percentages. The compiler is not
ready for production use. Detailed failure names and task IDs are in
[tests/README.md](tests/README.md).

## Build on Apple Silicon macOS

Requirements: Xcode Command Line Tools, Homebrew, CMake 3.28+, Ninja, and the
**exact LLVM/MLIR 21.1.6** development installation, including shared libraries
and MLIR tools. Other platforms have not been validated.

```sh
xcode-select --install  # Only if Command Line Tools are not already installed.
brew install cmake ninja
bash scripts/install-llvm.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
```

The installer reuses an existing 21.1.6 installation or installs the historical
Homebrew formula, matching Z3 4.15.4 dependency, and checksum-verified bottles.
It refuses to replace an incompatible LLVM or Z3 installation. The prefix is printed as
`GLOIN_LLVM_PREFIX`; use its `lib/cmake/llvm` and `lib/cmake/mlir` paths if different
from the example. Today's `brew install llvm` may install an unsupported release.

Tests are enabled by default and fetch checksum-pinned GoogleTest 1.16.0. Add
`-DBUILD_TESTING=OFF` for a compiler-only build without test dependencies. See
[docs/toolchain.md](docs/toolchain.md) for offline overrides and Makefile options.

## Verify the build

```sh
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
```

All four dialect setup tests pass. The full suite exits nonzero for the documented
failures; do not disable those cases to obtain a green run. `./build/gloinc` only
runs the lexer demo. Passing it a filename does not verify that program.

## Architecture and next milestones

`gloin_frontend` contains the lexer, parser, AST interfaces, and semantic analysis.
`gloin_backend` contains codegen, the Gloin dialect, and the JIT runner. Both
executables link these libraries. Tests drive compiler stages directly;
connecting them through a file-reading CLI remains SPEC-020.

[SPEC-006's contract](SPEC.md#first-release-contract-spec-006) selects a scalar
JIT compiler on Apple Silicon macOS for the first release. SPEC-021 is its
executable-core acceptance milestone; SPEC-046 remains the release gate. Strings,
standard I/O, aggregates, concurrency, and native binaries are deferred. These
are planned capabilities, not additions to the verified status above. The ordered
backlog replaces the old phase notes as the implementation plan.

## Continuous integration

[Compiler CI](.github/workflows/ci.yml) targets hosted macOS 15 arm64. It installs
LLVM/MLIR 21.1.6, builds from empty directories with tests disabled and enabled,
and runs the complete suite serially and in parallel. JUnit reports, full logs,
environment details, and test inventory are uploaded as `compiler-ci-reports`,
including on failure. The workflow remains red while the full suite fails.
Compiler build outputs are not restored from a cache.

[The verified SPEC-005 run](https://github.com/kubabialy/gloinc/actions/runs/34242653935)
built both configurations and published the complete reports. Both test runs
matched the then-current SPEC-005 baseline: 98 passes and the same 14 failures out of 112 tests,
with no crashes or skipped tests. The run is red because those failures remain.
