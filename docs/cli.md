# Command-line interface (SPEC-020)

Build instructions are in [the README](../README.md). The supported development
platform is Apple Silicon macOS with LLVM/MLIR 21.1.6.

```text
gloinc [--jit | --run | --check | --emit-ir | --emit-llvm | --emit-object | --emit-exe] [-o PATH] [--stdlib-dir DIR] [--] FILE [-- ARG...]
gloinc --help
gloinc --version
```

Exactly one readable regular source file is required. Symbolic links are followed;
no filename extension is required. Options may precede or follow the file until
the forwarding delimiter after FILE. Quote paths containing spaces in your shell.
For a filename beginning with a dash, use `--` or an explicit path such as `./-file`.
Source input from stdin is not supported; executed programs can use stdin through
`@std` or `@io`.
Duplicate modes, unknown options, and extra/missing files are usage errors.
Help (`-h`) and version (`-V`) must be used alone.

SPEC-030e forwards program arguments only after `FILE --`. Argument zero is the
exact source filename spelling; compiler flags are excluded. For example,
`gloinc --jit tool.gloin -- --copy "source file" "new file"` gives four arguments.
A pre-FILE delimiter still escapes dash-prefixed filenames:
`gloinc --jit -- -tool.gloin -- --help`. Empty arguments are preserved. A forwarding
delimiter in any non-run mode is a usage error, including an empty trailing one.
See [process APIs and embedding ownership](filesystem-process.md).

`--stdlib-dir DIR` selects the directory containing `std.gloin` and any other
standard module files. It may appear once, before `--`, with any compilation mode.
By default the CLI uses `stdlib/` beside the executable when that directory
exists (build layout), otherwise `../share/gloinc/stdlib/` relative to the actual
executable (installed layout). It never falls back to compiled-in standard functions
or the source checkout if a selected file is missing. Scalar programs with no
imports do not need this directory. To edit library functions without rebuilding,
run `./build/gloinc --jit --stdlib-dir stdlib examples/hello_world.gloin`.

| Mode | Behavior on success |
| --- | --- |
| Default / `--emit-exe` | Compile and link a standalone macOS arm64 executable. Write `a.out` unless `-o PATH` is supplied. |
| `--jit` / `--run` | Compile and execute `def main() -> i32` in process; return its low eight bits as the compiler process exit status. Only explicit output calls write to stdout. |
| `--check` | Compile and verify high-level IR without execution; stdout is empty. |
| `--emit-ir` | Print verified high-level MLIR with source locations without execution. |
| `--emit-llvm` | Lower and print verified LLVM-dialect MLIR with source locations without execution. This is MLIR syntax, not native LLVM `.ll` syntax. |
| `--emit-object -o PATH` | Compile a macOS arm64 object file with a native entry point. |
| `--emit-exe` | Explicit spelling for the default executable mode. |

Checking and inspection accept helper-only and empty modules. If `main` is
present, its signature must still be valid. These modes do not execute trapping
or non-terminating source programs.
Native output requires `main() -> i32`; object output requires `-o PATH`.
Executable output defaults to `a.out` when no output path is given. The output must differ from
the source path. Successful emission replaces the named output atomically; source
and link errors leave it untouched. Native programs receive their own executable
path as argument zero, followed by ordinary process arguments. Unlike the JIT
CLI, they do not use the `FILE -- ARG...` forwarding delimiter.

```sh
./build/gloinc --jit examples/core_counter.gloin
# stdout is empty; exit status: 42
./build/gloinc --check examples/core_counter.gloin
./build/gloinc --emit-llvm examples/core_counter.gloin > core_counter.mlir
./build/gloinc examples/hello_world.gloin
./a.out
./build/gloinc -o hello examples/hello_world.gloin
./hello
./build/gloinc --emit-object -o hello.o examples/hello_world.gloin
/opt/homebrew/opt/llvm/bin/clang++ hello.o build/libgloin_runtime.a -o hello-from-object
./hello-from-object
```

## Results and errors

| Exit status | Meaning |
| --- | --- |
| 0–255 | For run, main's i32 result modulo 256: -1 becomes 255, 256 becomes 0. Other successful modes exit 0. |
| 1 | File, compiler, lowering, JIT, native emission/link, or stdout write error. Diagnostics go to stderr. |
| 2 | Usage error; stderr includes a diagnostic and usage text. |

SPEC-023 replaces the earlier automatic result printing. Imports and output calls
never change main's return value. The JIT API retains the full signed i32, while
the host process exposes only its low eight bits. A returned 1 or 2 can share a
status with a compiler/usage error; those errors additionally report diagnostics.
Compilation and JIT setup failures produce no partial IR or result on stdout. Source diagnostics carry
the supplied filename and available line/byte-column locations. Output write
failures can occur after some bytes have been written.

Runtime arithmetic traps terminate the process with SIGTRAP or SIGILL on the
supported platform, following [the JIT contract](jit.md). They produce no result;
shells may describe the signal and map it to a shell-specific status. The CLI
does not add signal recovery or execution timeouts.

`--version` reports `gloinc 0.0.3 (LLVM/MLIR 21.1.6)`. This identifies the
scalar-core compiler. [SPEC-021's fixtures](../tests/fixtures/core/README.md) check
core acceptance; [the release guide](release.md) documents installation,
packaging, and validation. `import "@std";` enables `std.print(string)` and
`std.println(string)`. Output preserves exact bytes, including embedded NULs;
println appends LF. Both return void. Missing modules/members fail before execution.

## Shared compiler path and verification

The executable reads every source byte and calls `compile_source` with the
original filename. Native and JIT modes select executable compilation; JIT
invokes `JitRunner::run`, while native mode emits an object or links an
executable. Check/inspection select module mode. LLVM inspection uses the same verified
[lowering pipeline](lowering.md) as execution. Structured compiler diagnostics
are rendered once at the CLI boundary.

All 19 `CliTest` cases launch the actual built executable with argument vectors,
captured output, isolated temporary files, and bounded waits. They verify file
results, the repository counter example, repeated/concurrent runs, all modes,
i32 boundaries, source errors, unreadable/non-regular files, byte/UTF-8 rejection,
literal paths, and separate runtime traps. Emitted IR is reparsed and verified.
Run them with `ctest --test-dir build -R '^CliTest\.' --output-on-failure`.

## Local modules

`import "./utils";` loads `utils.gloin` relative to the importing source file,
independent of the CLI working directory. Dependencies use their own directories
and imports. Only public declarations are accessible through the filename namespace.
All CLI modes load and check dependencies, including unused code.
See [module paths, visibility, and cycles](modules.md), and run
`gloinc --jit examples/module_lab.gloin` for a complete multi-file example.


SPEC-030 reads stdin only when executed code calls `std.input(&memory, max_bytes)`.
Checking and IR inspection never read stdin. Input/parse failures are ordinary
result statuses; the program chooses how to report them and what `main` returns.
See [input, conversions, and ownership](standard-library.md).
