# Gloin

Gloin is an experimental language with explicit types, manual memory management,
and native executables. Its compiler, `gloinc`, is built on LLVM/MLIR, with
language operations represented in the GloinIR dialect.

**[Download 0.1.0](https://github.com/kubabialy/gloinc/releases/tag/v0.1.0)** ·
[Language guide](docs/site/0.1.0/index.html) ·
[Examples](examples/README.md) ·
[Release notes](docs/release-notes-0.1.0.md)

## Get started

Download and extract the archive for your platform. Verify it with the attached
`.sha256` file; see the [installation guide](docs/release-0.1.0.md#installation-and-package).

| Supported platform | Required LLVM/MLIR | Bundled toolchain installer |
| --- | --- | --- |
| Apple Silicon macOS (tested on macOS 15) | 21.1.6 | `share/gloinc/scripts/install-llvm.sh` |
| Ubuntu 24.04, ARM64 or x86_64 | 21.1.8 | `share/gloinc/scripts/install-llvm-linux.sh` |

**LLVM/MLIR and OpenSSL 3 must be installed even to run a downloaded compiler.**
Run the appropriate installer with `bash`. Install OpenSSL with
`brew install openssl@3` on macOS or `sudo apt-get install openssl libssl-dev`
on Ubuntu. These dependencies are not bundled.

Add the extracted archive's `bin/` directory to your `PATH`, then save this as
`hello.gloin`:

```gloin
import "@std";

def main() -> i32 {
    std.println("Hello World!");

    return 0;
}
```

```sh
gloinc -o hello hello.gloin
./hello                         # Prints Hello World!
gloinc --jit hello.gloin         # Compile and run in process
gloinc --check hello.gloin       # Check without running
```

Native output is the default; omitting `-o` writes `a.out`. Add `-O2` to enable
optimization. Generated executables do not need LLVM at runtime; those using
TLS need OpenSSL. See the [CLI reference](docs/cli.md) for all options.

## What works today

- **Language:** structs, methods, explicit generics, payloadless enums, and local
  and directory modules.
- **Memory and collections:** typed pointers, arenas, raw allocation, fixed
  arrays, borrowed slices, and fixed or growable vectors.
- **Error handling:** built-in `result<T>` and `error`, with compiler-enforced
  handling. See the [result contract](docs/results.md).
- **Standard library:** text and numeric conversions, files, child processes
  and pipes, JSON, nonblocking TCP, client/server TLS, and streaming HTTP/HTTPS.
- **Tools:** `gloinfmt`, written in Gloin, formats files and checks source trees.

Gloin is in early development and is not ready for production use. Switch,
match, enum payloads, maps, packed structs, and async/spawn are not supported.
Windows support is not planned; contributions are welcome.

## Documentation

| I want to… | Start here |
| --- | --- |
| Learn the language | [0.1.0 HTML guide](docs/site/0.1.0/index.html) |
| Run complete programs | [Examples and usage](examples/README.md) |
| Use library APIs | [Standard library](docs/standard-library.md), [I/O](docs/io.md), [processes](docs/child-processes.md), [networking](docs/networking.md), [HTTP](docs/http-client.md), [JSON](docs/json.md) |
| Format Gloin code | [gloinfmt](docs/gloinfmt.md) and [source style](docs/gloin-style.md) |
| Understand the compiler | [Checked programs](docs/checked-program.md) and [GloinIR/lowering](docs/lowering.md) |
| Inspect release verification | [Validation record](docs/next-release-draft.md) and [test inventory](tests/README.md) |

To read the HTML guide in a browser, open
`share/doc/gloinc/docs/site/0.1.0/index.html` inside an extracted archive.
Older releases keep their own versioned guides.

## Build from source

Follow the [macOS and Linux build instructions](docs/release-0.1.0.md#build-and-run).
Source builds require a C++23 compiler, CMake 3.28+, Ninja, OpenSSL 3 and the
pinned LLVM/MLIR installation above. See the [toolchain guide](docs/toolchain.md)
for compiler-only builds and offline dependencies.

After building:

```sh
cmake --build build --target check-core
./build/gloinfmt --check .
```

CI checks macOS and Linux, installed and relocated packages, and macOS
sanitizers. The full test suite retains four documented unsupported async/spawn
failures; `check-core` is the required passing gate.

## Roadmap and contributing

The [0.2.0 draft](docs/release-0.2.0-draft.md) prioritizes switch and match.
Endianness, packed layout, threads and async are under discussion. The
[wiki specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec)
includes future designs; use the release guide for implemented behavior.

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. AI assistance
is a personal choice; every change must be understood and manually verified,
with deterministic, reproducible behavior. AI slop and vibecoding are prohibited
and may result in exclusion from the project.
