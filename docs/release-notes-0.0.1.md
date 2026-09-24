# Gloin 0.0.1

First source and binary release for Apple Silicon macOS. The package includes the
compiler, the standard modules through SPEC-030h, native runtime libraries,
runnable examples, and versioned HTML documentation.

## Highlights

- Compile and run `main() -> i32` with the in-process JIT.
- Produce a Mach-O arm64 object with `--emit-object -o PATH` or a standalone
  executable with `--emit-exe -o PATH`. Executables link the Gloin runtime and
  do not require LLVM at runtime.
- Use ordinary structs, typed pointers and references, methods, function-exit
  `defer`, arenas, local modules, and the basic standard library.
- Use the expanded byte strings, numeric conversion, file and process I/O,
  math, monotonic time, and deterministic seeded randomness modules.

## Install and use

Install the pinned LLVM/MLIR 21.1.6 toolchain described in
[the toolchain guide](toolchain.md), then extract the macOS arm64 archive and run
`bin/gloinc --version`. The compiler and native linking need that toolchain;
programs built with `--emit-exe` do not. See the
[0.0.1 HTML guide](site/0.0.1/index.html) and [release guide](release.md).

Linux support is planned for 0.1.0. Windows support is not planned, but
contributions are welcome. Cross compilation and Intel macOS are not supported
in 0.0.1.

The supported-feature gate has 820 passing tests. The unfiltered suite retains
four documented failures for deferred concurrency syntax and code generation;
release CI audits their exact names and rejects new failures or skipped tests.
See [the test inventory](../tests/README.md) and
[contributing rules](../CONTRIBUTING.md).
