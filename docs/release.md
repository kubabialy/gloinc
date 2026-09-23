# Gloinc 0.0.1 scalar-core release

Version 0.0.1 is the scalar-core JIT compiler for Apple Silicon macOS with
LLVM/MLIR 21.1.6. It reads a source file, checks it, and executes `main() -> i32`.
Since SPEC-023, execution returns main's low eight bits as its process exit
status. Only explicit `std.print`/`std.println` calls write to stdout.
Compiler/file/JIT errors exit 1, usage errors exit 2, and arithmetic traps
terminate with SIGTRAP or SIGILL. `--check`, `--emit-ir`, and `--emit-llvm` do
not execute the program; LLVM inspection prints LLVM-dialect MLIR.

The source README teaches the language and build commands. The maintained matrix
at `tests/fixtures/core/README.md` maps the scalar types, operators, calls, scopes,
initialization, control flow, rejected syntax, and traps to executable fixtures.
The canonical examples are acceptance inputs. Later sections of SPEC.md and the
older mixed-feature examples are conceptual designs outside this release.

## Build and installation

From a fresh checkout, install the pinned toolchain with
`bash scripts/install-llvm.sh`. Use CMake 3.28+, Ninja, Xcode Command Line Tools,
and the matching LLVM/MLIR development installation:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
cmake --build build --target check-core
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/gloinc" --version
"$HOME/.local/bin/gloinc" "$HOME/.local/share/gloinc/examples/core_counter.gloin"
```

The last commands report `gloinc 0.0.1 (LLVM/MLIR 21.1.6)` and `42`.
`BUILD_TESTING=OFF` builds/installs without GoogleTest; validation targets require
tests enabled. `make run` defaults to the counter example; use
`make run RUN_ARGS='--check examples/core_counter.gloin'` for another command.
`make install INSTALL_PREFIX=/chosen/prefix` and `make package` delegate to CMake.

## Package contents and external dependencies

`cmake --build build --target package` creates
`gloinc-0.0.1-macos-arm64.tar.gz` and a SHA-256 checksum. The archive contains:

| Path below the archive root | Contents |
| --- | --- |
| `bin/gloinc` | Compiler and in-process JIT client |
| `share/doc/gloinc/` | README, specification, checklist, and technical guides |
| `share/gloinc/examples/core_counter.gloin` | Runnable example returning 42 |
| `share/gloinc/examples/hello_world.gloin` | Runnable standard-output example |
| `share/gloinc/stdlib/std.gloin` | Standard utility functions, compiled when imported |
| `share/gloinc/core-fixtures/` | All 133 source acceptance fixtures and their matrix |

LLVM/MLIR and their Homebrew dependencies are external and are not redistributed
in this package. The installed executable uses the library installation selected
at configure time. Official validation uses `/opt/homebrew/opt/llvm` with LLVM/MLIR
21.1.6 and its pinned Z3 dependency; replacing it with a newer ABI may prevent the
compiler from loading. A custom prefix requires rebuilding against that prefix.
The compiler prefix itself can move without rebuilding.

Use the archive on the same supported Apple Silicon macOS setup after installing
the pinned toolchain. Packages built on newer macOS versions are not claimed to
run on older systems. Intel macOS, Linux, Windows, cross-compilation, standalone
Gloin executables, and a bundled LLVM distribution are outside this release.

```sh
cd build
LC_ALL=C shasum -a 256 -c gloinc-0.0.1-macos-arm64.tar.gz.sha256
tar -xzf gloinc-0.0.1-macos-arm64.tar.gz
./gloinc-0.0.1-macos-arm64/bin/gloinc \
  ./gloinc-0.0.1-macos-arm64/share/gloinc/examples/core_counter.gloin
```

`bash scripts/check-package.sh build build/package-check` validates a staged
installation and a relocated extraction. Each runs 241 CLI/defer/method/pointer/struct/standard-output/source
cases against that binary, using the installed fixtures. The script also executes
the packaged counter and hello-world examples and records shared-library
dependencies. Removing the relocated `std.gloin` must cause a module
loading error, proving there is no fallback to the source checkout. It retains
JUnit reports, archive, and checksum. `GLOIN_TEST_CLI` and `GLOIN_TEST_FIXTURES`
are explicit test-harness overrides used for this purpose, not compiler options.

## Release validation

The `check-core` target runs 526 required scalar, defer, method, pointer, struct, and standard-output checks, including lower-level
frontend/operator/lowering tests and external execution probes as well as the
133 source fixtures. No known failure is reclassified as success. Full serial
and parallel runs remain separate. The full suite currently retains five
deferred-feature failures: spawn codegen, arenas, deferred/spawn generation, and
async types. Those features are rejected by the source compiler.

```sh
ctest --test-dir build -j 1 --output-on-failure --output-junit serial.xml
ctest --test-dir build -j 4 --output-on-failure --output-junit parallel.xml
```

AddressSanitizer and UndefinedBehaviorSanitizer instrument project C++ code in a
separate Debug build:

```sh
cmake -S . -B build-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DGLOIN_ENABLE_SANITIZERS=ON \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build-sanitized -j 2
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  cmake --build build-sanitized --target check-core
```

This checks the compiler and its C++ test harness. Prebuilt LLVM/MLIR libraries
and dynamically generated machine code are not sanitizer-instrumented. JIT
arithmetic failures are checked through explicit signal assertions. LeakSanitizer
is not part of the macOS validation claim. Sanitizer builds have no package target.

CI builds fresh Release configurations with tests on/off, runs `check-core`,
verifies installation and extraction, runs the sanitizer core gate, and retains
the complete serial/parallel results. `compiler-ci-reports` contains the logs,
JUnit reports, package, checksum, environment, and CMake configurations. A red
full-suite step remains distinct from passing scalar-release checks.

SPEC-046 records the exact verified commit and CI evidence. Preparing these
artifacts does not create a Git tag or publish a GitHub Release. Successful
results establish deterministic scalar behavior on the selected platform, not
byte-identical binaries or compatibility with unvalidated systems.
