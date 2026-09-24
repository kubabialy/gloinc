# Gloinc 0.0.2 release

Version 0.0.2 includes SPEC-001 through SPEC-030 and the SPEC-030a through
SPEC-030h standard-library expansion. It runs on Apple Silicon macOS with
LLVM/MLIR 21.1.6. The compiler checks source, runs `main() -> i32` through the
JIT, and can emit native arm64 objects and standalone executables.
Executable output is the default: `gloinc -o hello hello.gloin` names the output,
while `gloinc hello.gloin` writes `a.out`. Use `--jit` or `--run` to execute in process.
Since SPEC-023, execution returns main's low eight bits as its process exit
status. Only explicit `std.print`/`std.println` calls write to stdout.
Compiler/file/JIT errors exit 1, usage errors exit 2, and arithmetic traps
terminate with SIGTRAP or SIGILL. `--check`, `--emit-ir`, and `--emit-llvm` do
not execute the program; LLVM inspection prints LLVM-dialect MLIR.

The [versioned HTML guide](site/0.0.2/index.html) teaches the 0.0.2 language and
compiler. Later minor and major versions receive their own immutable directory
under `docs/site/`; update `versions.js` and the site root to select the latest.
The source README also teaches build commands. The maintained matrix
at `tests/fixtures/core/README.md` maps the scalar types, operators, calls, scopes,
initialization, control flow, rejected syntax, and traps to executable fixtures.
The canonical examples are acceptance inputs. Later sections of the
[language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec) and the
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
"$HOME/.local/bin/gloinc" --jit "$HOME/.local/share/gloinc/examples/core_counter.gloin"
```

The last commands report `gloinc 0.0.2 (LLVM/MLIR 21.1.6)` and `42`.
`BUILD_TESTING=OFF` builds/installs without GoogleTest; validation targets require
tests enabled. `make run` defaults to the counter example; use
`make run RUN_ARGS='--check examples/core_counter.gloin'` for another command.
`make install INSTALL_PREFIX=/chosen/prefix` and `make package` delegate to CMake.

## Package contents and external dependencies

`cmake --build build --target package` creates
`gloinc-0.0.2-macos-arm64.tar.gz` and a SHA-256 checksum. The archive contains:

| Path below the archive root | Contents |
| --- | --- |
| `bin/gloinc` | Compiler and in-process JIT client |
| `share/doc/gloinc/docs/site/0.0.2/` | Versioned HTML language and usage guide |
| `share/doc/gloinc/CONTRIBUTING.md` | Contribution guidelines |
| `share/doc/gloinc/` | README and technical guides; the specification and checklist are in the GitHub wiki |
| `share/doc/gloinc/third_party/fast_float/` | MIT license, pinned provenance, and header checksums for compiled-in decimal parsing |
| `share/gloinc/examples/core_counter.gloin` | Runnable example returning 42 |
| `share/gloinc/examples/hello_world.gloin` | Runnable standard-output example |
| `share/gloinc/examples/numbers_lab.gloin` | Bounded numeric input, explicit conversion, incremental mean, and fixed formatting |
| `share/gloinc/examples/standard_library.gloin` | Interactive decimal input, explicit errors, arena reuse, and sum output |
| `share/gloinc/stdlib/math.gloin` | Concrete finite numerical utilities, typed constants and checked results |
| `share/gloinc/stdlib/fs.gloin`, `share/gloinc/stdlib/process.gloin` | Paths, explicit mutations, and invocation context |
| `share/gloinc/examples/file_tool.gloin` | CLI-selected binary file copy from any working directory |
| `share/gloinc/stdlib/io.gloin` | Borrowed streams, owned files, bounded I/O, and explicit OS errors |
| `share/gloinc/examples/io_copy.gloin`, `share/gloinc/examples/io_filter.gloin` | Bounded binary copier and line filter |
| `share/gloinc/stdlib/std.gloin` | Standard utility functions, compiled when imported |
| `share/gloinc/stdlib/arena.gloin` | General arena allocator and explicit lifecycle methods |
| `share/gloinc/examples/module_lab.gloin`, `share/gloinc/examples/modules/` | Shared local dependencies, particle methods/constants, and arena reuse |
| `share/gloinc/examples/arena_lab.gloin` | Linked particles with arena growth/reset/reuse |
| `lib/libgloin_runtime.a`, `lib/libgloin_runtime.dylib` | Native arena, byte, and numeric runtime for static and external LLVM execution |
| `include/gloin/arena_runtime.h`, `include/gloin/stdlib_runtime.h`, `include/gloin/io_runtime.h`, `include/gloin/context_runtime.h`, `include/gloin/math_runtime.h`, `include/gloin/time_runtime.h`, `include/gloin/random_runtime.h` | C runtime ABI headers |
| `share/gloinc/stdlib/time.gloin`, `share/gloinc/stdlib/random.gloin` | Monotonic time, checked durations, and explicit seeded randomness |
| `share/gloinc/examples/simulation_lab.gloin` | Reproducible numeric simulation and elapsed-time reporting |
| `share/gloinc/examples/config_reader.gloin`, `share/gloinc/examples/statistics_tool.gloin`, `share/gloinc/examples/data/` | Integrated bounded configuration/statistics programs and sample inputs |
| `share/gloinc/core-fixtures/` | All 142 source acceptance fixtures and their matrix |

LLVM/MLIR and their Homebrew dependencies are external and are not redistributed
in this package. The installed executable uses the library installation selected
at configure time. Official validation uses `/opt/homebrew/opt/llvm` with LLVM/MLIR
21.1.6 and its pinned Z3 dependency; replacing it with a newer ABI may prevent the
compiler from loading. A custom prefix requires rebuilding against that prefix.
The compiler prefix itself can move without rebuilding.

Use the archive on the same supported Apple Silicon macOS setup after installing
the pinned toolchain. Packages built on newer macOS versions are not claimed to
run on older systems. Native executables use system libraries and embed the
static Gloin runtime; they do not require LLVM at runtime. Intel macOS, Linux,
Windows, cross-compilation, and a bundled LLVM distribution are outside this
release. Linux support is planned for 0.1.0. Windows support is not planned;
contributors may propose and maintain it.

```sh
cd build
LC_ALL=C shasum -a 256 -c gloinc-0.0.2-macos-arm64.tar.gz.sha256
tar -xzf gloinc-0.0.2-macos-arm64.tar.gz
./gloinc-0.0.2-macos-arm64/bin/gloinc \
  --jit \
  ./gloinc-0.0.2-macos-arm64/share/gloinc/examples/core_counter.gloin
```

`bash scripts/check-package.sh build build/package-check` validates a staged
installation and a relocated extraction. Each runs 443 CLI/standard-library/module/arena/defer/method/pointer/struct/standard-output/source
cases against that binary, using the installed fixtures. The script also executes
the packaged counter, hello-world, arena, module, strings, text construction, and interactive standard-library examples, verifies external LLVM
execution against the relocated runtime library, checks missing standard modules,
and records shared-library dependencies. Removing either relocated `std.gloin`
or `arena.gloin` must cause a module loading error, proving there is no fallback to the source checkout. It retains
JUnit reports, archive, and checksum. `GLOIN_TEST_CLI`, `GLOIN_TEST_FIXTURES`, and `GLOIN_TEST_ARENA_RUNTIME`
are explicit test-harness overrides used for this purpose, not compiler options.

## Release validation

The `check-core` target runs 820 required scalar, module, arena, defer, method, pointer, struct, native-output, and standard-output checks, including lower-level
frontend/operator/lowering tests and external execution probes as well as the
142 source fixtures. No known failure is reclassified as success. Full serial
and parallel runs remain separate. The full suite currently retains four
deferred-feature failures: spawn codegen, deferred/spawn generation, and
async types. Those features are rejected by the source compiler. CI checks the
exact four names in serial and parallel JUnit reports and fails on any new
failure or skipped case; the full-suite test results remain visible.

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

This checks the compiler, native arena runtime, and C++ test harness. The
standalone `gloin_arena_test` target directly exercises native allocation and
read/write operations under instrumentation; the external LLVM arena test also
loads the instrumented runtime with ASan initialized at process startup. Prebuilt LLVM/MLIR libraries
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
