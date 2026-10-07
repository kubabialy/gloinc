# Gloin compiler

Gloin is a C++23 compiler project built on LLVM/MLIR. It is under development;
the [language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec) describes the intended language, not a list
of completed features. The [implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist) tracks implementation and
verification evidence in order.

## Current status

Fresh builds work locally and in hosted CI on Apple Silicon macOS with LLVM/MLIR
21.1.6. The 0.1.0 candidate also builds on Ubuntu 24.04 ARM64 with LLVM/MLIR
21.1.8; local ARM64 release acceptance and hosted x86_64 CI passed.
**The matching LLVM/MLIR version must also be installed to run a downloaded
`gloinc` compiler.**
The release archive links to its shared libraries and does not bundle them.
Install the pinned toolchain with `bash scripts/install-llvm.sh` on macOS or
`bash scripts/install-llvm-linux.sh` on Ubuntu 24.04 from a source checkout.
Both scripts are packaged under `share/gloinc/scripts/` for use before running
an extracted `bin/gloinc`. Executables produced by `gloinc`
link the Gloin runtime statically and do not need LLVM at runtime.
The CLI builds native executables by default and can run scalar and ordinary struct
programs through the in-process JIT with `--jit`.
The executable core has source-file acceptance cases covering successful
programs, rejected source, and runtime arithmetic traps. Checking and IR inspection
modes are also available. Version 0.0.4 adds generics, payloadless enums,
`zeroed` and repeated array initialization, borrowed slices, and inline and
arena vectors to the 0.0.3 language.
Installation and package validation are documented
in the [0.0.4 release guide](docs/release-0.0.4.md). The [versioned HTML
guide](docs/site/0.0.4/index.html) documents the 0.0.4 language and compiler.
Native object and executable output work on Apple Silicon macOS and on the
Ubuntu 24.04 ARM64 candidate. Linux x86_64 is covered by the new CI gate.

Version 0.0.4 includes checked [generic structs and functions](docs/generics.md)
and the [fixed-array `zeroed` initializer](docs/fixed-arrays.md#zeroed-and-repeated-initializers).
Methods can also declare their own type parameters and require explicit call arguments.
It supports [payloadless enums](docs/enums.md) with nominal values and checked equality.
Borrowed [slices, inline fixed-capacity vectors, and growable arena vectors](docs/slices-vectors.md) are also
available; each has a runnable example.
The current development tree adds live-element slice views, collection
operations, generic slice helpers, and an empty growable vector constructor.
Arena vector capacity is reserved without filling unused slots. These changes
are newer than the 0.0.4 release and its versioned HTML guide.
The development tree also provides [raw memory and typed placement](docs/raw-memory.md)
so programs can implement their own arenas; see the runnable
[custom arena](examples/custom_arena.gloin).
The development tree also has built-in [`result<T>` and `error`](docs/results.md)
with explicit success/error handling. The [SPEC-034 design record](https://github.com/kubabialy/gloinc/wiki/Language-Spec#resultt-and-error-spec-034-proposed-not-implemented)
predates this implementation; the current guide documents the implemented rules.
The candidate also adds [nonblocking IPv4 TCP, client/server TLS, and bounded HTTP/1.1 heads](docs/networking.md)
through `@net` and `@http`. Their public operations use built-in `result<T>`;
the [local round-trip example](examples/network_http.gloin) runs in JIT and native modes.
The Gloin-written [`@http_client`](docs/http-client.md) adds bounded HTTP/HTTPS
requests, validated application headers, chunked/fixed/EOF response decoding,
and an overall deadline. Its streaming exchanges use reusable buffers and
`net.wait_many`, with backpressure, early upload rejection, and cancellation.
The synchronous convenience wrapper uses the same state machine. See the
[client example](examples/http_client.gloin) and
[32-connection streaming example](examples/http_stream.gloin).
`@net` now exposes an explicit listener reuse-address option, and `@std`
provides checked decimal conversion for every integer width, including typed
`u16` ports without manual digit tables. The
[networking milestones](docs/networking-roadmap.md) track asynchronous DNS,
concurrent HTTP servers, HTTPS services, and WebSocket work. Slack is
one integration example for these general APIs.
The networking follow-up adds bounded multi-socket readiness waits, explicit
write-side shutdown, a synchronous IPv4 hostname resolver, and a verified
nonblocking TLS client. Reusable [server TLS configurations and graceful shutdown](docs/server-tls.md)
now support accepted sessions too, with a [runnable loopback server](examples/tls_server.gloin).
The networking guides specify ownership, blocking and error limits.
The [tooling validation record](docs/tooling-roadmap.md) distinguishes the latest
focused checks from the complete release gates that must rerun before publication.
The repository's former Python tools and test drivers now run in Gloin; Python
is no longer a project test dependency. The [HTTP acceptance driver](tests/http/README.md)
preserves independent wire fixtures and concurrent HTTP/HTTPS checks.
The [0.1.0 candidate HTML guide](docs/site/0.1.0/index.html) presents the
current language as a whole. The [0.0.4 guide](docs/site/0.0.4/index.html)
remains the reference for the published release. Build metadata now reports
0.1.0, but the candidate is **not published**. See its
[release notes](docs/release-notes-0.1.0.md),
[installation guide](docs/release-0.1.0.md), and
[scope and validation draft](docs/next-release-draft.md).
Linux support remains a required 0.1.0 release gate. The current macOS and
Ubuntu 24.04 ARM64 tree passed local core, package, and full-suite classification
gates. Hosted x86_64 CI passed an earlier revision and must rerun on this tree
before publication.

| Area | Verified status |
| --- | --- |
| Build | Shared compiler libraries, optional tests, pinned GoogleTest, consistent shared LLVM/MLIR linkage. |
| Execution tests | 31 E2E cases (including IR checks), nine if/while and ten unless/for executions, numeric bit probes, and operator executions verify values, branches/loops, evaluation order, and arithmetic traps. |
| Full test suite | The development tree discovers 1,034 tests; macOS `check-core` passed 987/987. Focused Linux, sanitizer and package checks cover the new tooling work. Complete macOS/Linux serial, parallel and package release gates predate these additions and must run again. See the [tooling validation](docs/tooling-roadmap.md) and [earlier candidate record](docs/next-release-draft.md#current-local-validation). |
| Core acceptance | CLI-driven source cases cover `zeroed` arrays, enums, borrowed slices, and both vector forms in addition to the 0.0.3 cases. |
| Lexer | All 34 tests pass: vocabulary, UTF-8 validation, malformed literals, and byte positions. Reserved tokens do not establish feature support. |
| Parsing | All 53 development parser tests pass: core grammar, precedence, strict annotations/delimiters, generic call/literal lookahead, and rejection of unsupported syntax. Constants and visibility retain AST metadata. |
| Semantic analysis | Resolved types/scopes, initialization, scalar operators, calls, return paths, and executable entry signatures are verified. Nested if/unless/while/for execution, loop-variable scope, and omitted for components are verified. |
| Generics | Checked generic structs, functions, and methods with their own type parameters execute with explicit arguments, nested types, arrays, pointers, module visibility, and recursive calls. Four older unchecked IR-string tests also pass. |
| IR verification/lowering | One pipeline verifies source output and conversions, rejects unsupported IR, and produces LLVM-compatible modules for output and execution consumers. |
| GloinIR coverage | Checked scalar constants, arithmetic, comparisons, guards, nominal struct definitions, aggregate and string literal construction, pointer access, local storage, struct access, and payloadless enum operations remain in GloinIR until a dedicated pass lowers them. Checked function signatures, calls, returns, enum operations, array/struct/string literals, value-field extraction, pointer construction/offsets/checks/comparisons, typed arena allocation, stack/field/array addresses, loads, and stores retain source types, with explicit source/layout bridges. Runtime calls use a verified Gloin ABI operation; string byte globals, ABI preparation, and defer internals still emit LLVM operations directly. See [the lowering audit and migration boundary](docs/lowering.md#gloinir-coverage). |
| JIT | All 16 tests pass: native execution, validated entry/signatures, separate results/errors, repeated runs, and integer/float trap behavior. |
| CLI | All 20 process tests pass: file loading, JIT and native results, object/executable output, `-O2`, checking, verified IR, diagnostics, usage, and exit behavior. |
| Standard output | `@std` loads `stdlib/std.gloin`, which defines `print` and `println`; all 17 standard-module tests pass. |
| Ordinary structs | 14 tests cover nested values, field validation/mutation, visibility, nominal types, native execution, and target padding/alignment. |
| Pointers/references | 20 tests cover typed access, read-only views, recursive links, null traps, nullable pointer offsets, and manual lifetimes with no borrow checker. |
| Methods | 19 tests cover instance/static calls, one explicit receiver, mutability, visibility, evaluation order, recursion, diagnostics, and external execution. |
| Defer | 24 tests cover registration-time captures, conditional/loop registration, LIFO function-exit cleanup, early returns, traps, native allocation bookkeeping, and external execution. |
| Arenas | `@arena` exposes `GeneralArena`: 21 compiler/API tests and 11 native runtime tests cover initialized allocation, alignment, growth, reset/free, failure, and external linking. See [the arena guide](docs/arenas.md). |
| Local modules | 29 tests cover relative files, directory and `#name` imports, shared dependencies, exports, privacy, cycles, and source diagnostics. See [modules](docs/modules.md). |
| Standard input/conversions | 24 compiler/API tests and 15 native tests cover bounded input, decimal i32 parsing/formatting, explicit arena storage, error statuses, and external execution. See [the library guide](docs/standard-library.md). |
| Numerical utilities | 12 compiler/API and 12 native tests cover `@math`, finite errors, signed zero, rounding, subnormals, host-state isolation, geometry/statistics, and packaged execution. See [math](docs/math.md). |
| Timing and seeded randomness | 15 compiler/API and 13 native tests cover monotonic clocks, checked durations, scoped clock injection, SplitMix64 vectors, sampling, and packaged simulations. See [time and randomness](docs/time-random.md). |
| Byte strings | 18 compiler/API tests cover byte bounds/search/ordering, borrowed views, arena copies, checked result alternatives, and guide examples. Two native tests cover exact/empty copies. See [usage and costs](docs/strings.md). |
| TCP and HTTP | Ten compiler/API tests, three native socket tests, seven native TLS tests, and local TLS/HTTP fixtures cover TCP, client/server TLS, incremental framing, deadlines, certificate verification, graceful shutdown and cleanup. See [networking](docs/networking.md), [server TLS](docs/server-tls.md) and [HTTP clients](docs/http-client.md). |
| Results and raw memory | Eight result tests cover required handling, source-typed aggregate payloads, JIT/AoT execution, and checked standard-library calls. The [result](docs/results.md) and [raw-memory](docs/raw-memory.md) guides state the current contracts and limits. |
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
to source fixtures. The [candidate guide](docs/release-0.1.0.md) describes package contents,
runtime dependencies, and the SPEC-046 validation gate.

Test pass counts are not specification-coverage percentages. The compiler is not
ready for production use. Detailed failure names and task IDs are in
[tests/README.md](tests/README.md).

## Build on Apple Silicon macOS

Requirements: Xcode Command Line Tools, Homebrew, CMake 3.28+, Ninja, OpenSSL 3,
and the
**exact LLVM/MLIR 21.1.6** development installation, including shared libraries
and MLIR tools.

```sh
xcode-select --install  # Only if Command Line Tools are not already installed.
brew install cmake ninja openssl@3
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

## Build on Ubuntu 24.04 Linux

The 0.1.0 Linux candidate uses the shared **LLVM/MLIR 21.1.8** packages from
apt.llvm.org. The installer verifies the repository signing key and pins the
tested package revision. On ARM64 or x86_64 Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ openssl libssl-dev
bash scripts/install-llvm-linux.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm \
  -DMLIR_DIR=/usr/lib/llvm-21/lib/cmake/mlir
cmake --build build -j 2
```

The Linux installer requires `sudo`; it prints `GLOIN_LLVM_PREFIX` when done.
The installed compiler depends on that LLVM/MLIR installation and OpenSSL 3.
Native executables using TLS also need the matching OpenSSL runtime libraries.

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

Compile a native executable, then run it:

```sh
./build/gloinc -o main main.gloin
./main
# Exits with status 42; no implicit stdout output.
```

Without `-o`, `gloinc main.gloin` writes `a.out`. Use `--jit` or its `--run`
alias to compile and execute in process without creating a binary. The included
example uses a helper function, a mutable counter, `for`, and `unless`:

```sh
./build/gloinc --jit examples/core_counter.gloin         # Exits 42; no stdout.
./build/gloinc --check examples/core_counter.gloin        # Checks without running.
./build/gloinc --emit-ir examples/core_counter.gloin      # High-level MLIR.
./build/gloinc --emit-llvm examples/core_counter.gloin    # LLVM-dialect MLIR.
./build/gloinc --help
./build/gloinc --version
```

JIT run mode requires `def main() -> i32`. Its low eight bits become the compiler
process exit status; the result is never printed automatically. Compiler/file/JIT errors use status 1
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
ordinary Gloin functions; existing `math.gloin` and `io.gloin` use the same
file-loading path. See [standard module development](stdlib/README.md) for the
native primitive, public exports, and `--stdlib-dir` option.

SPEC-030 adds `std.input(&memory, max_bytes)`, `std.to_int(text)`, and
`std.to_string(&memory, value)`. Input and formatting use an explicit caller-owned
arena; the released input and parser APIs report errors through status structs.
The development tree also offers `std.parse_i32_checked(text) -> result<i32>`.
Conversion is i32
only. See [the API and lifetime rules](docs/standard-library.md), or run:

```sh
printf '10\n-3\n+35\n' | ./build/gloinc --jit examples/standard_library.gloin
```

SPEC-030a adds `@strings` and shared `@status` constants. Byte-string queries,
slices, search, comparison, and ASCII trimming allocate nothing; `strings.copy`
takes an explicit arena and reports allocation failure. Every function documents
usage, ownership, and cost in the [API guide](docs/strings.md) and
[library source](stdlib/strings.gloin). Run `./build/gloinc --jit examples/strings_lab.gloin`
for a configuration parser and an arena-lifetime example.

SPEC-030b adds split/line cursors, bounded text transformations, and a fixed-capacity
`StringBuilder`. Cursors allocate nothing; appends copy into preallocated storage;
snapshots take an explicit destination arena. See [usage and costs](docs/text-construction.md)
or run `./build/gloinc --jit examples/text_lab.gloin` for an escaped report.

SPEC-030c adds typed integer/float/bool parsers, arena-backed formatters, and
explicit checked numeric conversions. See [examples, costs, and rounding rules](docs/numbers.md)
and the [streaming statistics example](examples/numbers_lab.gloin).

SPEC-030d adds `@io`: borrowed standard streams, owned files, explicit open modes,
bounded arena reads, counted writes, flush/close, and recoverable OS errors.
The development tree also supports direct reads into writable byte slices and
writes from read-only byte slices. See the [binary filter](examples/strip_nuls.gloin).
See [ownership, examples, and costs](docs/io.md), the [binary file copier](examples/io_copy.gloin),
and the [line filter](examples/io_filter.gloin).

SPEC-030e adds `@fs` lexical paths, metadata, explicit mutations, directory
iteration, and `@process`
arguments/environment/cwd. Programs receive arguments after `FILE --`; copied
values use caller arenas. See [API usage and costs](docs/filesystem-process.md)
and the [command-line file tool](examples/file_tool.gloin). The
[directory walker](examples/directory_walk.gloin) shows the new cursor API.
The unreleased tree also provides `fs.symlink` and bounded `fs.read_link` with
built-in `result<T>`; see the [symlink rules](docs/filesystem-process.md#symbolic-links)
and [runnable example](examples/symlinks.gloin). `fs.canonical_path`, `fs.temp_dir`
and `fs.remove_dir` add bounded path resolution, exclusive temporary directories
and empty-directory removal. The [workspace example](examples/temporary_workspace.gloin)
shows explicit cleanup and arena ownership; the formatter test driver now uses
these APIs and child-process capture entirely from Gloin.

The unreleased 0.1.0 tree also adds child processes with built-in `result<T>`,
pipes, bounded output capture, cwd/environment options and explicit group
cleanup. **Group cleanup is opt-in** through `new_process_group = true`;
`start_piped` alone still manages only the direct child. Descendants that change
group/session can survive. Read the [current setup, rationale and guarantees](docs/child-processes.md#current-setup-and-design-rationale)
before using it as a tool runner; the [options example](examples/child_options.gloin)
shows the intended configuration.

`Options.stack_limit_bytes` additionally sets a child soft stack limit before
execution. Zero inherits; the parent and inherited hard limit stay unchanged.
Nonzero limits require the host's main thread on macOS. See the
[platform contract](docs/child-processes.md#child-stack-limits).

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
Fixed arrays use `[T; N]` and brace initializers such as
`def values: [i32; 2] = {1, 2};`. Indexed reads and writes check bounds; see
[the fixed-array guide](docs/fixed-arrays.md).
Nullable pointers also support [signed element offsets](docs/pointer-offsets.md).
Native object and executable output accepts opt-in `-O2`; the default remains
`-O0`. Both features are documented in the versioned guide.
Ordinary structs also support instance methods with explicit `self` pointers and
static calls such as `Counter.make(40)`; see [the method fixture](tests/fixtures/core/run/methods.gloin).
`defer call(...)` captures arguments immediately and runs registered calls in
reverse order on normal function return; see [the defer fixture](tests/fixtures/core/run/defer.gloin).
Local imports such as `import "./utils";` resolve relative to their source file;
when `utils/` exists, its `.gloin` files form one namespace. `import "#math";`
collects `packages/math/*.gloin` beside the root source file. See the
[directory and package example](examples/module_discovery.gloin) and the
[module guide](docs/modules.md). There are no implicit numeric conversions. Packed
structs and concurrency remain deferred. `examples/hello_world.gloin` is a
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

To produce a standalone executable or object on the current host:

```sh
./build/gloinc -o hello examples/hello_world.gloin
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
"$HOME/.local/bin/gloinc" --jit "$HOME/.local/share/gloinc/examples/core_counter.gloin"
```

Add `$HOME/.local/bin` to your `PATH` to use `gloinc` from any directory.
The same installation provides `gloinfmt`, a Gloin-written formatter. Run
`build/gloinfmt --check .` to check a checkout or `build/gloinfmt FILE` to
print formatted source. `build/gloinfmt --write --git` formats tracked and
unignored Gloin sources in place. See the [formatter guide](docs/gloinfmt.md) and
[source style guide](docs/gloin-style.md).
The installed compiler still requires the exact LLVM/MLIR installation used
to build it. The macOS prefix is `/opt/homebrew/opt/llvm`; the Linux prefix is
`/usr/lib/llvm-21`. LLVM is an external dependency and is not bundled. The
installed toolchain scripts are under
`"$HOME/.local/share/gloinc/scripts/"`.

To create and verify an archive:

```sh
cmake --build build --target package
bash scripts/check-package.sh build build/package-check
```

CPack writes a candidate `gloinc-0.1.0-macos-arm64.tar.gz`,
`gloinc-0.1.0-linux-aarch64.tar.gz`, or `gloinc-0.1.0-linux-x86_64.tar.gz`
under `build/`, with a `.sha256` checksum.
The archive contains `bin/gloinc`, `bin/gloinfmt`, standard modules, native arena libraries and header,
documentation, runnable examples, and the core source fixtures. The verification script runs the CLI, standard-library, module, arena, defer, method, pointer, struct, fixed-array, standard-output, and source acceptance cases against
both an installed copy and an archive unpacked into a different path containing
spaces. See [the candidate guide](docs/release-0.1.0.md) for extraction, dependencies,
sanitizer checks, and the supported-platform limits.

## Verify the build

```sh
ctest --test-dir build -R '^MLIRSetup\.' --output-on-failure
cmake --build build --target check-core
ctest --test-dir build -j 1 --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
```

`check-core` runs the required scalar, module, arena, defer, method, pointer, struct, fixed-array, native-output, and standard-output checks, including frontend,
lowering, external execution, source acceptance, CLI, JIT, and dialect setup.
The full suite exits nonzero for the documented
failures; do not disable those cases. The release CI audits the exact four known
failures and rejects any additional failure or skipped test. The CLI suite launches
the built executable directly and verifies actual file-dependent results.

## JSON payloads

The 0.1.0 candidate adds `@json`: a bounded reader and writer implemented in
Gloin, using built-in `result<T>`, checked Unicode, exact number text, and
caller-owned buffers. Run `./build/gloinc --jit examples/json.gloin`; it needs
no external service. See [JSON usage and limits](docs/json.md).

## Architecture and next milestones

`gloin_frontend` contains the lexer, parser, AST interfaces, and semantic analysis;
it uses shared LLVM for resolved integer and floating values.
`gloin_backend` contains codegen, the Gloin dialect, and the JIT runner. Both
executables link these libraries. The CLI and tests use the same compilation,
lowering, and execution APIs. `gloin_runtime` supplies the LLVM-independent
native arena allocator and standard input/conversions; a shared variant is installed for external LLVM execution.

Native output is the default. Version 0.0.4 offers opt-in `-O2` native
compilation and nullable pointer offsets; see [CLI options](docs/cli.md) and
[pointer rules](docs/pointer-offsets.md). Concurrency remains deferred. The
[implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist)
tracks the remaining work.

Version 0.0.4 supports Apple Silicon macOS. Linux is planned for 0.1.0. Windows
support is not planned, although contributions are welcome. See
[contributing rules](CONTRIBUTING.md) for the manual verification and deterministic
change requirements.

## Continuous integration

[Compiler CI](.github/workflows/ci.yml) has macOS 15 ARM64 and Ubuntu 24.04
x86_64 jobs. The macOS job installs LLVM/MLIR 21.1.6, builds from empty
directories with tests disabled and enabled,
and checks core acceptance before running the complete suite serially and in
parallel. It also validates installed/extracted packages and a separate ASan/UBSan
Debug build. The core step has its own result and `core.xml`/`core.log` reports.
JUnit reports, full logs,
environment details, and test inventory are uploaded as `compiler-ci-reports`,
including on failure. The complete suite still reports its four deferred-feature
failures; CI succeeds only when they match the documented list exactly.
Compiler build outputs are not restored from a cache.
The Linux job uses the pinned LLVM/MLIR 21.1.8 packages and runs core,
installed/relocated package, and complete-suite checks. The Linux CI gate must
pass before the 0.1.0 candidate can be published.
