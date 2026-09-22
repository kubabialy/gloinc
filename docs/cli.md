# Command-line interface (SPEC-020)

Build instructions are in [the README](../README.md). The supported development
platform is Apple Silicon macOS with LLVM/MLIR 21.1.6.

```text
gloinc [--run | --check | --emit-ir | --emit-llvm] [--stdlib-dir DIR] [--] FILE
gloinc --help
gloinc --version
```

Exactly one readable regular source file is required. Symbolic links are followed;
no filename extension is required. Options may precede or follow the file until
`--`, which ends option parsing. Quote paths containing spaces in your shell.
For a filename beginning with a dash, use `--` or an explicit path such as `./-file`.
Standard input, program arguments, and native binary output are not supported.
Duplicate modes, unknown options, and extra/missing files are usage errors.
Help (`-h`) and version (`-V`) must be used alone.

`--stdlib-dir DIR` selects the directory containing `std.gloin` and any other
standard module files. It may appear once, before `--`, with any compilation mode.
By default the CLI uses `stdlib/` beside the executable when that directory
exists (build layout), otherwise `../share/gloinc/stdlib/` relative to the actual
executable (installed layout). It never falls back to compiled-in standard functions
or the source checkout if a selected file is missing. Scalar programs with no
imports do not need this directory. To edit library functions without rebuilding,
run `./build/gloinc --stdlib-dir stdlib examples/hello_world.gloin`.

| Mode | Behavior on success |
| --- | --- |
| `--run` (default) | Compile and execute `def main() -> i32`; return its low eight bits as the process exit status. Only explicit output calls write to stdout. |
| `--check` | Compile and verify high-level IR without execution; stdout is empty. |
| `--emit-ir` | Print verified high-level MLIR with source locations without execution. |
| `--emit-llvm` | Lower and print verified LLVM-dialect MLIR with source locations without execution. This is MLIR syntax, not native LLVM `.ll` syntax. |

Checking and inspection accept helper-only and empty modules. If `main` is
present, its signature must still be valid. These modes do not execute trapping
or non-terminating source programs.

```sh
./build/gloinc examples/core_counter.gloin
# stdout is empty; exit status: 42
./build/gloinc --check examples/core_counter.gloin
./build/gloinc --emit-llvm examples/core_counter.gloin > core_counter.mlir
```

## Results and errors

| Exit status | Meaning |
| --- | --- |
| 0–255 | For run, main's i32 result modulo 256: -1 becomes 255, 256 becomes 0. Other successful modes exit 0. |
| 1 | File, compiler, lowering, JIT, or stdout write error. Diagnostics go to stderr. |
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

`--version` reports `gloinc 0.0.1 (LLVM/MLIR 21.1.6)`. This identifies the
scalar-core compiler. [SPEC-021's fixtures](../tests/fixtures/core/README.md) check
core acceptance; [the release guide](release.md) documents installation,
packaging, and validation. `import "@std";` enables `std.print(string)` and
`std.println(string)`. Output preserves exact bytes, including embedded NULs;
println appends LF. Both return void. Missing modules/members fail before execution.

## Shared compiler path and verification

The executable reads every source byte and calls `compile_source` with the
original filename. Run selects executable mode and invokes `JitRunner::run`;
check/inspection select module mode. LLVM inspection uses the same verified
[lowering pipeline](lowering.md) as execution. Structured compiler diagnostics
are rendered once at the CLI boundary.

All 16 `CliTest` cases launch the actual built executable with argument vectors,
captured output, isolated temporary files, and bounded waits. They verify file
results, the repository counter example, repeated/concurrent runs, all modes,
i32 boundaries, source errors, unreadable/non-regular files, byte/UTF-8 rejection,
literal paths, and separate runtime traps. Emitted IR is reparsed and verified.
Run them with `ctest --test-dir build -R '^CliTest\.' --output-on-failure`.
