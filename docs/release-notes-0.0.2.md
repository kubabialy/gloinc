# Gloin 0.0.2

Apple Silicon macOS patch release that makes ahead-of-time executable output the
default CLI behavior. Language and standard-library behavior from 0.0.1 is
unchanged.

## CLI changes

- `gloinc -o hello hello.gloin` writes a standalone executable named `hello`.
- `gloinc hello.gloin` writes `a.out` in the current directory.
- `gloinc --jit hello.gloin` compiles and runs in process; `--run` is an alias.
- `--emit-exe` remains an explicit spelling for executable output.
- `--emit-object -o PATH`, `--check`, `--emit-ir`, and `--emit-llvm` retain their
  existing behavior.

Executables link the Gloin runtime and do not need LLVM at runtime. Building
them still needs the pinned LLVM/MLIR 21.1.6 toolchain. Open the packaged HTML
guide at `share/doc/gloinc/docs/site/0.0.2/index.html`; see the
[release guide](https://github.com/kubabialy/gloinc/blob/v0.0.2/docs/release.md).
The [0.0.1 release](https://github.com/kubabialy/gloinc/releases/tag/v0.0.1)
remains available with its original JIT-default CLI.

Only Apple Silicon macOS is supported. Linux support is planned for 0.1.0;
Windows support is not planned, though contributions are welcome.
