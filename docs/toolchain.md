# Supported LLVM/MLIR toolchain

Gloin currently requires **LLVM and MLIR 21.1.6**, from the same installation.
CMake requests that exact MLIR package version; MLIR's package in turn requires
the matching LLVM version. Other versions are not yet validated and are rejected
at configuration time. Do not change the version requirement without rebuilding
and rerunning the dialect tests.

The tested environment is Apple Silicon macOS, AppleClang 16.0.0, CMake 4.2.1,
Ninja, and Homebrew's LLVM/MLIR 21.1.6. The project requires CMake 3.28 or newer
and uses C++23. Other platforms have not been validated.

## Configure and build

Point both package paths at the same installation. For the tested Homebrew
installation:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
ctest --test-dir build -j 1 --output-on-failure
```

Check that the Homebrew prefix still contains 21.1.6 before using it: an upgrade
may replace the installation. The build requires the development headers,
`mlir-tblgen`, and the shared libraries, not just LLVM command-line tools.

Tests are enabled by default through CMake's `BUILD_TESTING` option. With tests
enabled, configuration fetches the [GoogleTest 1.16.0 release](https://github.com/google/googletest/releases/tag/v1.16.0)
archive and verifies the SHA-256 recorded in `CMakeLists.txt`. GoogleMock and
GoogleTest installation rules are disabled because this project uses neither.
For an offline build with tests, extract that archive and add
`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/googletest-src`.
This explicit source override bypasses archive verification, so use the pinned
release when reproducing test results.

To build the compiler without fetching or building any test dependencies:

```sh
cmake -S . -B build-no-tests -G Ninja -DBUILD_TESTING=OFF \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build-no-tests -j 2
```

This still compiles the frontend, code generator, dialect, and JIT implementation.
It creates no `gloinc_test` target or GoogleTest fetch steps. Use separate build
directories for the two modes so old test artifacts do not confuse inspection.
The full test suite still has known language/JIT failures. Serial and parallel
execution are supported; subprocess tests use isolated temporary directories and
checked tool invocations. See the [test inventory and harness](../tests/README.md)
for timeouts, tool-path overrides, and failure classifications.

## Compiler targets and Makefile

| Target | Sources and dependencies |
| --- | --- |
| `gloin_frontend` | Static library containing lexer, parser, and semantic analysis; no MLIR dependency. |
| `gloin_backend` | Static library containing codegen, the Gloin dialect, and JIT; links the frontend and shared LLVM/MLIR libraries. |
| `gloinc` | CLI entry point linked against `gloin_backend`. It remains a lexer demo until SPEC-020. |
| `gloinc_test` | Test sources linked against the same backend and GoogleTest; present only with `BUILD_TESTING=ON`. |
| `gloin_test_process` | Controlled child-process fixture for harness tests; present only with `BUILD_TESTING=ON`. |

Each compiler source compiles once per build directory. Compiler targets require
C++23 without compiler extensions; include directories and LLVM definitions are
target usage requirements, so the frontend and GoogleTest do not inherit MLIR
settings. TableGen receives its include paths explicitly and generates `.inc`
files under the build directory's `src/dialect`, with generation dependencies
propagated through the backend target to its consumers.

The Makefile delegates to these CMake targets and CTest:

```sh
make build BUILD_DIR=build-no-tests BUILD_TESTING=OFF BUILD_ARGS='-j 2'
make test BUILD_DIR=build CTEST_ARGS='-R ^MLIRSetup'
make run BUILD_DIR=build-no-tests BUILD_TESTING=OFF
make clean BUILD_DIR=build-no-tests
```

`CMAKE_ARGS` passes generator/package paths and other configure options;
`BUILD_ARGS` passes build options; `CTEST_ARGS` passes test filters/options.
`make test` explicitly enables tests and preserves CTest's failure exit status.
`make clean` invokes CMake's clean target in an already configured directory,
removing build products while retaining the configuration and downloaded sources.

## Shared-library contract

Both executables link the imported `MLIR`, `LLVM`, and
`MLIRExecutionEngineShared` shared targets through `gloin_backend`. The latter's imported dependencies
are the same shared MLIR and LLVM libraries. CMake rejects installations missing
these targets or exposing them as non-shared libraries. The C++ compiler selects
its standard library; no explicit `stdc++` link entry is needed.

The previous component list linked static archives such as `MLIRIR` and
`LLVMCore`, while `MLIRExecutionEngine` imported `MLIR` and `LLVM` shared targets
transitively. That brought overlapping implementations into the executable and
caused dialect-loading crashes. Do not add component archives alongside the
shared targets or edit the imported targets to hide their dependencies.

For a custom LLVM build, use the 21.1.6 release with the MLIR project and enable
`LLVM_BUILD_LLVM_DYLIB`, `LLVM_LINK_LLVM_DYLIB`, `MLIR_BUILD_MLIR_DYLIB`,
`MLIR_LINK_MLIR_DYLIB`, and `MLIR_ENABLE_EXECUTION_ENGINE`. Validate the resulting
installation with the build and dialect tests above; custom builds are not part
of the tested platform claim.

On macOS, inspect `otool -L build/gloinc` and `otool -L build/gloinc_test` for the
shared libraries. Inspect Ninja's link commands with
`ninja -C build -t commands gloinc gloinc_test`: they should contain no LLVM/MLIR
component `.a` files. GoogleTest's own static archives are expected.

The `MLIRSetup.AllCompilerDialects` regression test uses the actual `CodeGen`
constructor and checks that Gloin, Func, Arith, ControlFlow, MemRef, SCF, and LLVM
dialects all load into one context. This is a toolchain/linkage check; repairing
the JIT's lowering and translation path remains SPEC-018/SPEC-019.
