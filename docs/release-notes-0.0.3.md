# Gloin 0.0.3 (prepared)

Version 0.0.3 adds fixed arrays to the 0.0.2 language on Apple Silicon macOS.
It is prepared in source; publishing a GitHub release is a separate step.

Write `[T; N]` for an array with exactly `N` elements and use a contextual brace
initializer, for example `def values: [i32; 2] = {1, 2};`. Indexed reads and
writes check bounds at runtime. Arrays copy by value, and nested arrays, struct
elements, zero-length arrays, and references to elements are supported. Slices,
dynamic containers, and pointer arithmetic remain deferred. See the
[0.0.3 HTML guide](site/0.0.3/index.html) and [fixed-array reference](fixed-arrays.md).

The native executable CLI from 0.0.2 is unchanged. The source and packaged
compiler require the matching LLVM/MLIR 21.1.6 shared libraries even to start;
the release archive does not bundle LLVM. It now includes
`share/gloinc/scripts/install-llvm.sh`, which can install the pinned toolchain
before running `bin/gloinc`. The native executables that Gloin produces do not
need LLVM at runtime. The archive also includes `fixed_arrays.gloin`.

Linux support remains planned for 0.1.0. Windows support is not planned, though
contributions are welcome. The [0.0.3 release guide](release-0.0.3.md) describes
the package and validation gates.
