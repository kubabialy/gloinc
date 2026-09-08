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

GoogleTest is currently fetched during configuration. To reuse an existing source
cache, add `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/googletest-src`.
Pinning this dependency and making tests optional are tracked in SPEC-003.
The full test suite still has known language/JIT failures; use serial execution
because the existing subprocess tests share `temp.mlir` (SPEC-004).

## Shared-library contract

Both executables link the imported `MLIR` and `LLVM` shared targets. The test
executable also links `MLIRExecutionEngineShared`, whose imported dependencies
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
