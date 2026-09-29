# Gloin 0.0.4

Version 0.0.4 extends the Apple Silicon macOS language released in 0.0.3:

- Generic structs, functions, and methods accept explicit type arguments. Structs can also take compile-time `usize` capacity arguments, including `[T; N]` fields.
- Fixed arrays accept `zeroed` when their element type has a defined zero value, and `{value; N}` to copy one evaluated value into N elements.
- Payloadless nominal enums support named variants, equality, and module exports.
- Borrowed `[T]` and `[const T]` slices support checked ranges and indexing.
- `@vector` provides inline `Vector<T; N>` with a fixed capacity and growable `Vector<T>` backed by a caller-owned arena.
- More checked operations remain in GloinIR until a dedicated lowering pass, retaining language types and semantics longer in the pipeline.

The [0.0.4 HTML guide](site/0.0.4/index.html) walks through complete programs and links to the detailed [generic](generics.md), [array](fixed-arrays.md), [enum](enums.md), and [slice/vector](slices-vectors.md) contracts. The release archive includes their runnable examples and the `@vector` standard module.

`gloinc hello.gloin` still creates `a.out`; `-o` names a native executable and `--jit` runs immediately. The compiler requires LLVM/MLIR 21.1.6 to start and to link native output; generated executables do not require LLVM at runtime. This release supports Apple Silicon macOS only. Linux remains planned for 0.1.0. Windows support is not planned, though contributions are welcome.

Built-in `result<T>`/`error`, enum payloads and matching, maps, package imports, and async/spawn remain unavailable. The [release guide](release-0.0.4.md) describes installation, package contents, and validation.

Release validation on Apple Silicon macOS passed all 895 supported-language checks. Installed and relocated package suites each passed 468 checks, including runnable 0.0.4 examples from the archive. The complete serial and parallel suites each passed 938 of 942 tests; their four failures are the documented deferred async/spawn cases. Documentation links, Gloin formatting, and the package checksum also passed.
