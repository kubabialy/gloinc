# Gloin 0.0.3

Version 0.0.3 adds fixed arrays, nullable pointer offsets, and opt-in native
`-O2` output to the 0.0.2 language on Apple Silicon macOS.

Write `[T; N]` for an array with exactly `N` elements and use a contextual brace
initializer, for example `def values: [i32; 2] = {1, 2};`. Indexed reads and
writes check bounds at runtime. Arrays copy by value, and nested arrays, struct
elements, zero-length arrays, and references to elements are supported. A
nullable `*T` can advance by a signed `i64` element offset within one live
allocation. Pointer subtraction, ordering, and indexing remain unsupported;
offsets do not check allocation bounds. Slices and dynamic containers remain
deferred. See the [0.0.3 HTML guide](site/0.0.3/index.html),
[fixed-array reference](fixed-arrays.md), and [pointer-offset rules](pointer-offsets.md).

Native executable output remains the default and writes `a.out` unless `-o`
chooses a name. `-O2` now runs LLVM's O2 optimization pipeline for native
objects and executables; `-O0` remains the default. `--jit` and `--run` keep
their existing behavior and do not accept native optimization flags.
The source and packaged
compiler require the matching LLVM/MLIR 21.1.6 shared libraries even to start;
the release archive does not bundle LLVM. It now includes
`share/gloinc/scripts/install-llvm.sh`, which can install the pinned toolchain
before running `bin/gloinc`. The native executables that Gloin produces do not
need LLVM at runtime. The archive also includes `fixed_arrays.gloin` and
`pointer_offsets.gloin`.

Linux support remains planned for 0.1.0. Windows support is not planned, though
contributions are welcome. The [0.0.3 release guide](release-0.0.3.md) describes
the package and validation gates.
