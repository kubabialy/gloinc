# Gloinc project guidelines

Read [README.md](README.md) for current status and [SPEC-TODO.md](SPEC-TODO.md) for
the ordered backlog. Verify one task at a time, record evidence, and commit it
separately. A build or non-null AST/module does not prove language execution.

## Build and test

Use LLVM/MLIR **21.1.6** from one installation, CMake 3.28+, and a C++23 compiler.
The verified platform is Apple Silicon macOS. Setup and offline options are in
[docs/toolchain.md](docs/toolchain.md).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
ctest --test-dir build --output-on-failure -j 4
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
```

`BUILD_TESTING=OFF` skips GoogleTest and test tools. Every `tests/*_test.cpp` suite
must be in CMake's explicit source list. See [tests/README.md](tests/README.md) for
inventory, timeouts, and failure task IDs. Keep full-suite failures visible; do
not disable cases or weaken assertions to make CI green. The CLI still ignores
input files, and examples are not acceptance tests.

## Targets and linkage

Reuse `gloin_frontend` and `gloin_backend` instead of recompiling their sources in
new executables. Keep compiler settings target-scoped and generated dialect files
in the build directory.

Link compiler consumers through `gloin_backend`, which uses shared `MLIR`, `LLVM`,
and `MLIRExecutionEngineShared` targets. Do not add component archives such as
`MLIRIR`, `LLVMCore`, or individual conversion libraries alongside these shared
implementations; that previously caused dialect-loading crashes. Let the C++
compiler select its standard library.

## Code style and compiler behavior

Use C++23, the repository's `.clang-format`, snake_case methods, and PascalCase
types. Prefer clear ownership and early returns. Preserve diagnostics and failure
status; unsupported constructs must not silently become successful output.
Resolve language decisions in SPEC.md and scope implementation to its checklist
task. Diagnostic, type, lowering, and runtime paths remain incomplete until their
acceptance criteria are met.
