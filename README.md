# Gloin compiler

Gloin is a C++23 compiler project built on LLVM/MLIR. It is under development;
the [language specification](SPEC.md) describes the intended language, not a list
of completed features. [SPEC-TODO.md](SPEC-TODO.md) tracks implementation and
verification evidence in order.

## Current status

Fresh builds work locally and in hosted CI on Apple Silicon macOS with LLVM/MLIR 21.1.6.
The CLI compiles source files and runs scalar and ordinary struct programs through the in-process JIT.
The executable core has 142 source-file acceptance cases covering successful
programs, rejected source, and runtime arithmetic traps. Checking and IR inspection
modes are also available. The scalar-core version is `0.0.1`; installation and
package validation are documented in [the release guide](docs/release.md).
The [versioned HTML guide](docs/site/0.0.1/index.html) documents the 0.0.1
language and compiler. Native object and executable output are supported on
Apple Silicon macOS.

| Area | Verified status |
| --- | --- |
| Build | Shared compiler libraries, optional tests, pinned GoogleTest, consistent shared LLVM/MLIR linkage. |
| Execution tests | 31 E2E cases (including IR checks), nine if/while and ten unless/for executions, numeric bit probes, and operator executions verify values, branches/loops, evaluation order, and arithmetic traps. |
| Full test suite | 867 tests discovered; 863 passes and 4 documented deferred-feature failures are required for the release audit. |
| Core acceptance | 142 CLI-driven source fixtures: 38 successful programs, 85 expected compiler errors, and 19 runtime traps. |
| Lexer | All 34 tests pass: vocabulary, UTF-8 validation, malformed literals, and byte positions. Reserved tokens do not establish feature support. |
| Parsing | All 49 parser tests pass: core grammar, precedence, strict annotations/delimiters, and rejection of unsupported syntax. Constants and visibility retain AST metadata. |
| Semantic analysis | Resolved types/scopes, initialization, scalar operators, calls, return paths, and executable entry signatures are verified. Nested if/unless/while/for execution, loop-variable scope, and omitted for components are verified. |
| Generics | Four IR-string checks pass; generic execution is not established. |
| IR verification/lowering | One pipeline verifies source output and conversions, rejects unsupported IR, and produces LLVM-compatible modules for output and execution consumers. |
| JIT | All 16 tests pass: native execution, validated entry/signatures, separate results/errors, repeated runs, and integer/float trap behavior. |
| CLI | All 19 process tests pass: file loading, JIT and native results, object/executable output, checking, verified IR, diagnostics, usage, and exit behavior. |
| Standard output | `@std` loads `stdlib/std.gloin`, which defines `print` and `println`; all 17 standard-module tests pass. |
| Ordinary structs | 14 tests cover nested values, field validation/mutation, visibility, nominal types, native execution, and target padding/alignment. |
| Pointers/references | 18 tests cover typed access, read-only views, recursive links, null traps, and manual lifetimes with no borrow checker. |
| Methods | 19 tests cover instance/static calls, one explicit receiver, mutability, visibility, evaluation order, recursion, diagnostics, and external execution. |
| Defer | 24 tests cover registration-time captures, conditional/loop registration, LIFO function-exit cleanup, early returns, traps, native allocation bookkeeping, and external execution. |
| Arenas | `@arena` exposes `GeneralArena`: 21 compiler/API tests and nine native runtime tests cover initialized allocation, alignment, growth, reset/free, failure, and external linking. See [the arena guide](docs/arenas.md). |
| Local modules | 25 tests cover relative paths, shared dependencies, exported functions/types/constants, privacy, cycles, and source diagnostics. See [modules](docs/modules.md). |
| Standard input/conversions | 24 compiler/API tests and 15 native tests cover bounded input, decimal i32 parsing/formatting, explicit arena storage, error statuses, and external execution. See [the library guide](docs/standard-library.md). |
| Numerical utilities | 12 compiler/API and 12 native tests cover `@math`, finite errors, signed zero, rounding, subnormals, host-state isolation, geometry/statistics, and packaged execution. See [math](docs/math.md). |
| Timing and seeded randomness | 15 compiler/API and 13 native tests cover monotonic clocks, checked durations, scoped clock injection, SplitMix64 vectors, sampling, and packaged simulations. See [time and randomness](docs/time-random.md). |
| Byte strings | 18 compiler/API tests cover 14 documented functions, byte bounds/search/ordering, borrowed views, arena copies, and guide examples. Two native tests cover exact/empty copies. See [usage and costs](docs/strings.md). |
| Text construction | 17 tests cover borrowed cursors, bounded transformations, shared builder state, allocation failures, snapshots, and executable documentation. See [usage and costs](docs/text-construction.md). |
| Package imports, concurrency | Incomplete: SPEC-044, SPEC-040/041. |

[Compiler diagnostics](docs/diagnostics.md) now connect parsing, checking, and high-level
codegen through `compile_source`, with verified high-level or LLVM output via
[the shared lowering pipeline](docs/lowering.md) and [file-reading CLI](docs/cli.md).
[The parser contract](docs/parser.md) distinguishes core compilation from
syntax-only tests of deferred features.
[The checked-program contract](docs/checked-program.md) defines the shared
semantic data and ownership boundary required by normal codegen.

[The JIT API](docs/jit.md) executes compiled modules in process. The
[core acceptance matrix](tests/fixtures/core/README.md) maps the release contract
to source fixtures. [The release guide](docs/release.md) describes package contents,
runtime dependencies, and the SPEC-046 validation gate.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
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
# Exits with status 42; no implicit stdout output.
```

The compiler reads the file, checks types, lowers it to LLVM, and executes `main`.
It does not create a standalone binary. You can also run the included example,
which uses a helper function, a mutable counter, `for`, and `unless`:

```sh
./build/gloinc examples/core_counter.gloin               # Exits 42; no stdout.
./build/gloinc --check examples/core_counter.gloin        # Checks without running.
./build/gloinc --emit-ir examples/core_counter.gloin      # High-level MLIR.
./build/gloinc --emit-llvm examples/core_counter.gloin    # LLVM-dialect MLIR.
./build/gloinc --help
./build/gloinc --version
```

Run mode requires `def main() -> i32`. Its low eight bits become the process exit
status; the result is never printed automatically. Compiler/file/JIT errors use status 1
and stderr; usage errors use status 2. Runtime arithmetic traps terminate the
process with a signal. See [the CLI reference](docs/cli.md) for the complete interface.

Standard output is explicit:

```gloin
import "@std";
def main() -> i32 {
    std.println("Hello World!");
    return 0;
}
```

Run `tests/fixtures/core/run/hello_world.gloin` to print exactly `Hello World!`
and a newline, with exit status 0. `std.print` writes without adding a newline.

The standard library lives in [stdlib/std.gloin](stdlib/std.gloin). These are
ordinary Gloin functions; adding a future `math.gloin` or `io.gloin` uses the same
file-loading path. See [standard module development](stdlib/README.md) for the
native primitive, public exports, and `--stdlib-dir` option.

SPEC-030 adds `std.input(&memory, max_bytes)`, `std.to_int(text)`, and
`std.to_string(&memory, value)`. Input and formatting use an explicit caller-owned
arena; input and parsing report errors through result structs. Conversion is i32
only. See [the API and lifetime rules](docs/standard-library.md), or run:

```sh
printf '10\n-3\n+35\n' | ./build/gloinc examples/standard_library.gloin
```

SPEC-030a adds `@strings` and shared `@status` constants. Byte-string queries,
slices, search, comparison, and ASCII trimming allocate nothing; `strings.copy`
takes an explicit arena and reports allocation failure. Every function documents
usage, ownership, and cost in the [API guide](docs/strings.md) and
[library source](stdlib/strings.gloin). Run `./build/gloinc examples/strings_lab.gloin`
for a configuration parser and an arena-lifetime example.

SPEC-030b adds split/line cursors, bounded text transformations, and a fixed-capacity
`StringBuilder`. Cursors allocate nothing; appends copy into preallocated storage;
snapshots take an explicit destination arena. See [usage and costs](docs/text-construction.md)
or run `./build/gloinc examples/text_lab.gloin` for an escaped report.

SPEC-030c adds typed integer/float/bool parsers, arena-backed formatters, and
explicit checked numeric conversions. See [examples, costs, and rounding rules](docs/numbers.md)
and the [streaming statistics example](examples/numbers_lab.gloin).

SPEC-030d adds `@io`: borrowed standard streams, owned files, explicit open modes,
bounded arena reads, counted writes, flush/close, and recoverable OS errors.
See [ownership, examples, and costs](docs/io.md), the [binary file copier](examples/io_copy.gloin),
and the [line filter](examples/io_filter.gloin).

SPEC-030e adds `@fs` lexical paths, metadata, explicit mutations, and `@process`
arguments/environment/cwd. Programs receive arguments after `FILE --`; copied
values use caller arenas. See [API usage and costs](docs/filesystem-process.md)
and the [command-line file tool](examples/file_tool.gloin).

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
returns. `string`, `import "@std"`, and explicit string output are supported.
Ordinary structs support named literals, nested fields, value parameters/returns,
and checked mutation. See [the struct example](tests/fixtures/core/run/ordinary_struct.gloin).
`*T`/`&T` and read-only `*const T`/`&const T` support manually managed resources;
see [the pointer fixture](tests/fixtures/core/run/pointers.gloin).
Ordinary structs also support instance methods with explicit `self` pointers and
static calls such as `Counter.make(40)`; see [the method fixture](tests/fixtures/core/run/methods.gloin).
`defer call(...)` captures arguments immediately and runs registered calls in
reverse order on normal function return; see [the defer fixture](tests/fixtures/core/run/defer.gloin).
Local imports such as `import "./utils";` resolve relative to their source file;
see [the module example](examples/module_lab.gloin).
There are no implicit numeric conversions. Package imports, arrays, packed
structs, and concurrency remain deferred. `examples/hello_world.gloin` is a
runnable standard-output example.

For a diagnostic example:

```sh
./build/gloinc --check tests/fixtures/core/reject/canonical_10.gloin
# Reports an unknown type (Mystery), including filename, line, and column; exits 1.
```

The acceptance suite checks repeatable results on the supported toolchain and
platform. It does not promise identical native machine code across platforms.

SPEC-030f adds `@math`: 47 concrete numerical helpers and pi/tau constants,
with checked domains/ranges, signed-zero rules, and explicit rounding. The
[math guide](docs/math.md) documents every API and cost; the streaming
[geometry/statistics example](examples/math_lab.gloin) reads `x,y` records.

SPEC-030g adds `@time` monotonic readings and checked durations, plus `@random`
with explicit, copyable SplitMix64 state. Every operation documents units,
failure behavior, and cost in the [timing/randomness guide](docs/time-random.md).
The [seeded simulation](examples/simulation_lab.gloin) combines these modules
with math, CLI arguments, arena-backed formatting, and checked output. Embedders
can inject a clock to test elapsed-time reporting deterministically.

SPEC-030h adds a [configuration reader](examples/config_reader.gloin) and a
[streaming statistics tool](examples/statistics_tool.gloin) alongside the
simulation. They compose source modules with explicit ownership and bounded
storage; the statistics tool selects multiple columns and creates a new report
after validating input. See [formats, usage, costs, and verification](docs/integrated-examples.md).

## Install or package

To produce a standalone executable or object on Apple Silicon macOS:

```sh
./build/gloinc --emit-exe -o hello examples/hello_world.gloin
./hello
./build/gloinc --emit-object -o hello.o examples/hello_world.gloin
```

Generated executables link the static Gloin runtime and do not require LLVM at
runtime. The compiler and native link step require the pinned LLVM toolchain.
See [native output and CLI options](docs/cli.md).

After building and passing `check-core`, install into your user prefix:

```sh
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/gloinc" --version
"$HOME/.local/bin/gloinc" "$HOME/.local/share/gloinc/examples/core_counter.gloin"
```

Add `$HOME/.local/bin` to your `PATH` to use `gloinc` from any directory.
The installed compiler still requires the exact LLVM/MLIR installation used
to build it. With the supported Homebrew setup, that is `/opt/homebrew/opt/llvm`;
LLVM is an external dependency and is not bundled.

To create and verify an archive:

```sh
cmake --build build --target package
bash scripts/check-package.sh build build/package-check
```

CPack writes `build/gloinc-0.0.1-macos-arm64.tar.gz` and its `.sha256` checksum.
The archive contains `bin/gloinc`, standard modules, native arena libraries and header,
documentation, runnable examples, and the core source fixtures. The verification script runs all 443 CLI/standard-library/module/arena/defer/method/pointer/struct/standard-output/source acceptance cases against
both an installed copy and an archive unpacked into a different path containing
spaces. See [the release guide](docs/release.md) for extraction, dependencies,
sanitizer checks, and the supported-platform limits.

## Verify the build

```sh
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
cmake --build build --target check-core
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
```

`check-core` runs 820 required scalar, module, arena, defer, method, pointer, struct, native-output, and standard-output checks, including frontend,
lowering, external execution, source acceptance, CLI, JIT, and dialect setup.
The full suite exits nonzero for the documented
failures; do not disable those cases. The release CI audits the exact four known
failures and rejects any additional failure or skipped test. The CLI suite launches
the built executable directly and verifies actual file-dependent results.

## Architecture and next milestones

`gloin_frontend` contains the lexer, parser, AST interfaces, and semantic analysis;
it uses shared LLVM for resolved integer and floating values.
`gloin_backend` contains codegen, the Gloin dialect, and the JIT runner. Both
executables link these libraries. The CLI and tests use the same compilation,
lowering, and execution APIs. `gloin_runtime` supplies the LLVM-independent
native arena allocator and standard input/conversions; a shared variant is installed for external LLVM execution.

[SPEC-006's contract](SPEC.md#first-release-contract-spec-006) selects a scalar
JIT compiler on Apple Silicon macOS for the first release. SPEC-021 supplies its
executable-core acceptance suite; SPEC-046 remains the release gate. Subsequent
tasks add the strings, standard output, structs, pointers, methods, and defer
listed above, followed by arenas, local modules, and input/i32 conversions.
Native output is included in 0.0.1; concurrency remains deferred. The ordered
backlog replaces the old phase notes as the implementation plan.

Version 0.0.1 supports Apple Silicon macOS. Linux is planned for 0.1.0. Windows
support is not planned, although contributions are welcome. See
[contributing rules](CONTRIBUTING.md) for the manual verification and deterministic
change requirements, including the prohibition on AI slop and vibecoding.

## Continuous integration

[Compiler CI](.github/workflows/ci.yml) targets hosted macOS 15 arm64. It installs
LLVM/MLIR 21.1.6, builds from empty directories with tests disabled and enabled,
and checks core acceptance before running the complete suite serially and in
parallel. It also validates installed/extracted packages and a separate ASan/UBSan
Debug build. The core step has its own result and `core.xml`/`core.log` reports.
JUnit reports, full logs,
environment details, and test inventory are uploaded as `compiler-ci-reports`,
including on failure. The complete suite still reports its four deferred-feature
failures; CI succeeds only when they match the documented list exactly.
Compiler build outputs are not restored from a cache.

[The verified SPEC-021 run](https://github.com/kubabialy/gloinc/actions/runs/35736970025)
built both configurations and passed all **161 focused checks**. Its complete
serial and parallel runs each report **463/470 passes**, with the same seven
deferred-feature failures and no unexpected test-process crashes or skips.
The overall run is red because those full-suite failures remain; the core
acceptance step passes. Download `compiler-ci-reports` for the evidence.
