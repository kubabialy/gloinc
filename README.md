# Gloin compiler

Gloin is a C++23 compiler project built on LLVM/MLIR. It is under development;
the [language specification](SPEC.md) describes the intended language, not a list
of completed features. [SPEC-TODO.md](SPEC-TODO.md) tracks implementation and
verification evidence in order.

## Current status

Fresh builds work locally and in hosted CI on Apple Silicon macOS with LLVM/MLIR 21.1.6.
The CLI compiles source files and runs scalar programs through the in-process JIT.
The executable core has 125 source-file acceptance cases covering successful
programs, rejected source, and runtime arithmetic traps. Checking and IR inspection
modes are also available. The current version is `0.0.1-dev`.

| Area | Verified status |
| --- | --- |
| Build | Shared compiler libraries, optional tests, pinned GoogleTest, consistent shared LLVM/MLIR linkage. |
| External execution tests | 29 E2E cases, nine if/while and ten unless/for executions, numeric bit probes, and 103 operator executions verify values, branches/loops, evaluation order, and arithmetic traps through external MLIR tools. |
| Full test suite | 470 tests discovered; local serial/parallel runs both have 463 passes, 7 failures, no unexpected test-process crashes. |
| Core acceptance | 125 CLI-driven source fixtures: 28 successful programs, 83 expected compiler errors, and 14 runtime traps. Successful programs run three times with identical results. |
| Lexer | All 34 tests pass: vocabulary, UTF-8 validation, malformed literals, and byte positions. Reserved tokens do not establish feature support. |
| Parsing | All 49 parser tests pass: core grammar, precedence, strict annotations/delimiters, and rejection of unsupported syntax. Constants and visibility retain AST metadata. |
| Semantic analysis | Resolved types/scopes, initialization, scalar operators, calls, return paths, and executable entry signatures are verified. Nested if/unless/while/for execution, loop-variable scope, and omitted for components are verified. |
| Generics | Four IR-string checks pass; generic execution is not established. |
| IR verification/lowering | One pipeline verifies source output and conversions, rejects unsupported IR, and produces LLVM-compatible modules for output and execution consumers. |
| JIT | All 16 tests pass: native execution, validated entry/signatures, separate results/errors, repeated runs, and integer/float trap behavior. |
| CLI | All 16 process tests pass: file loading, native results, checking, verified IR output, diagnostics, usage, and exit behavior. |
| Imports, concurrency | Incomplete: SPEC-023/029/030, SPEC-040/041. |

[Compiler diagnostics](docs/diagnostics.md) now connect parsing, checking, and high-level
codegen through `compile_source`, with verified high-level or LLVM output via
[the shared lowering pipeline](docs/lowering.md) and [file-reading CLI](docs/cli.md).
[The parser contract](docs/parser.md) distinguishes core compilation from
syntax-only tests of deferred features.
[The checked-program contract](docs/checked-program.md) defines the shared
semantic data and ownership boundary required by normal codegen.

[The JIT API](docs/jit.md) executes compiled modules in process. The
[core acceptance matrix](tests/fixtures/core/README.md) maps the release contract
to source fixtures. SPEC-046 is the remaining packaging and release gate.

Test pass counts are not specification-coverage percentages. The compiler is not
ready for production use. Detailed failure names and task IDs are in
[tests/README.md](tests/README.md).

## Build on Apple Silicon macOS

Requirements: Xcode Command Line Tools, Homebrew, CMake 3.28+, Ninja, and the
**exact LLVM/MLIR 21.1.6** development installation, including shared libraries
and MLIR tools. Other platforms have not been validated.

```sh
xcode-select --install  # Only if Command Line Tools are not already installed.
brew install cmake ninja
bash scripts/install-llvm.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/opt/homebrew/opt/llvm/lib/cmake/mlir
cmake --build build -j 2
```

The installer reuses an existing 21.1.6 installation or installs the historical
Homebrew formula, matching Z3 4.15.4 dependency, and checksum-verified bottles.
It refuses to replace an incompatible LLVM or Z3 installation. The prefix is printed as
`GLOIN_LLVM_PREFIX`; use its `lib/cmake/llvm` and `lib/cmake/mlir` paths if different
from the example. Today's `brew install llvm` may install an unsupported release.

Tests are enabled by default and fetch checksum-pinned GoogleTest 1.16.0. Add
`-DBUILD_TESTING=OFF` for a compiler-only build without test dependencies. See
[docs/toolchain.md](docs/toolchain.md) for offline overrides and Makefile options.

## Compile and run a program

Save this as `main.gloin`:

```gloin
def main() -> i32 {
    return 42;
}
```

Compile and execute it with the in-process JIT:

```sh
./build/gloinc main.gloin
# Prints: 42
```

The compiler reads the file, checks types, lowers it to LLVM, and executes `main`.
It does not create a standalone binary. You can also run the included example,
which uses a helper function, a mutable counter, `for`, and `unless`:

```sh
./build/gloinc examples/core_counter.gloin               # Prints 42, exits 0.
./build/gloinc --check examples/core_counter.gloin        # Checks without running.
./build/gloinc --emit-ir examples/core_counter.gloin      # High-level MLIR.
./build/gloinc --emit-llvm examples/core_counter.gloin    # LLVM-dialect MLIR.
./build/gloinc --help
./build/gloinc --version
```

Run mode requires `def main() -> i32`. The signed result is printed to stdout;
every successful result uses exit status 0. Compiler/file/JIT errors use status 1
and stderr; usage errors use status 2. Runtime arithmetic traps terminate the
process with a signal. See [the CLI reference](docs/cli.md) for the complete interface.

### Language essentials

Functions and bindings start with `def`. All parameters, bindings, and function
returns need explicit types. Bindings are immutable unless declared `def mut`;
statements such as declarations, assignments, and returns end with `;`.

```gloin
def add(left: i32, right: i32) -> i32 {
    return left + right;
}

def main() -> i32 {
    def mut total: i32 = 0;
    for def mut i: i32 = 0; i < 3; i = i + 1 {
        total = add(total, 14);
    }
    return total;
}
```

This program returns 42. More runnable examples are in
[the acceptance fixtures](tests/fixtures/core/README.md), including
[recursion](tests/fixtures/core/run/recursion.gloin),
[branches and loops](tests/fixtures/core/run/control_flow.gloin), and
[short-circuit booleans](tests/fixtures/core/run/short_circuit.gloin).

Supported types are `bool`, signed and unsigned 8/16/32/64-bit integers,
`f32`, `f64`, aliases `int` (`i32`) and `usize` (`u64`), and `void` function
returns. There are no implicit numeric conversions. Strings, imports, printing
from Gloin code, arrays, structs, pointers, concurrency, and native executable
output remain deferred. `examples/hello_world.gloin` needs these later features;
use the core examples above to get started.

For a diagnostic example:

```sh
./build/gloinc --check tests/fixtures/core/reject/canonical_10.gloin
# Reports an unknown type (Mystery), including filename, line, and column; exits 1.
```

The acceptance suite checks repeatable results on the supported toolchain and
platform. It does not promise identical native machine code across platforms.

## Verify the build

```sh
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
ctest --test-dir build -j 4 --no-tests=error \
  -R '^(CoreAcceptanceTest|CliTest|JitRunnerTest|MLIRSetup)\.' --output-on-failure
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
```

The focused command runs 161 core acceptance, CLI, JIT, and dialect setup tests.
The full suite exits nonzero for the documented
failures; do not disable those cases to obtain a green run. The CLI suite launches
the built executable directly and verifies actual file-dependent results.

## Architecture and next milestones

`gloin_frontend` contains the lexer, parser, AST interfaces, and semantic analysis;
it uses shared LLVM for resolved integer and floating values.
`gloin_backend` contains codegen, the Gloin dialect, and the JIT runner. Both
executables link these libraries. The CLI and tests use the same compilation,
lowering, and execution APIs.

[SPEC-006's contract](SPEC.md#first-release-contract-spec-006) selects a scalar
JIT compiler on Apple Silicon macOS for the first release. SPEC-021 supplies its
executable-core acceptance suite; SPEC-046 remains the release gate. Strings,
standard I/O, aggregates, concurrency, and native binaries are deferred. These
are planned capabilities, not additions to the verified status above. The ordered
backlog replaces the old phase notes as the implementation plan.

## Continuous integration

[Compiler CI](.github/workflows/ci.yml) targets hosted macOS 15 arm64. It installs
LLVM/MLIR 21.1.6, builds from empty directories with tests disabled and enabled,
and checks core acceptance before running the complete suite serially and in
parallel. The core step has its own result and `core.xml`/`core.log` reports.
JUnit reports, full logs,
environment details, and test inventory are uploaded as `compiler-ci-reports`,
including on failure. The workflow remains red while the full suite fails.
Compiler build outputs are not restored from a cache.

[The verified SPEC-005 run](https://github.com/kubabialy/gloinc/actions/runs/34242653935)
built both configurations and published the complete reports. Both test runs
matched the then-current SPEC-005 baseline: 98 passes and the same 14 failures out of 112 tests,
with no crashes or skipped tests. The run is red because those failures remain.
