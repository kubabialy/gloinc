# Language examples

`core_counter.gloin` is a runnable core example, tested through the actual CLI.
After building, run `./build/gloinc examples/core_counter.gloin`: it exits 42
without printing. It uses a helper call, mutation, a for-loop, and unless.
`./build/gloinc examples/hello_world.gloin` prints `Hello World!` and exits 0.
The arena and module labs below also run in maintained tests. Other files are
design examples and manual inputs; their execution is not
established by the maintained suite.

| Files | Intended subject | Current follow-up |
| --- | --- | --- |
| `core_counter.gloin` | Scalar core execution | Verified by the CLI tests; broader runnable fixtures are in [core acceptance](../tests/fixtures/core/README.md). |
| `hello_world.gloin` | Standard output | Runnable with SPEC-022/SPEC-023. |
| `standard_library.gloin` | Input and i32 conversions | Runnable with SPEC-030; counted lines, explicit errors, caller-owned arena strings, reset/reuse, and totals. |
| `strings_lab.gloin` | Byte strings and explicit retention | Runnable with SPEC-030a; allocation-free configuration parsing, checked access, search, independent copies, and 10,000 scratch-arena resets. See [costs and usage](../docs/strings.md). |
| `text_lab.gloin` | Traversal and bounded construction | Runnable with SPEC-030b; borrowed cursors, escaped report construction, transformations, shared builder state, scratch reuse, and an independent snapshot. See [costs and usage](../docs/text-construction.md). |
| `module_lab.gloin`, `modules/*.gloin` | Local modules | Runnable with SPEC-029; shared nominal types, exported constants, methods, linked particles, and arena reset/reuse. |
| `arena_lab.gloin` | Typed arena allocation | Runnable with SPEC-028; linked particles, native layout, methods, reset/reuse, independent arenas, and deferred free. |
| `simple_test.gloin`, `comprehensive_test.gloin`, `P2_SUMMARY_DEMO.gloin` | Mixed features | Historical design inputs; use the maintained core acceptance fixtures for verified programs. |
| `defer_test.gloin` | Deferred cleanup | Scope and exit paths: SPEC-027. |
| `basic_endianness_test.gloin` | Byte-order-aware types | Representation and semantics: SPEC-037 through SPEC-039. |

Some examples predate the current specification and use unresolved or unsupported
syntax. Standard output is implemented; local and standard module dependencies are implemented; package imports remain deferred. These
files establish neither production readiness nor a specification-coverage percentage.

Use the [root README](../README.md) for setup, the [maintained tests](../tests/README.md)
for observed results, and the [implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist) for the implementation plan.

### Numeric parsing and reports (SPEC-030c)

`numbers_lab.gloin` reads bounded decimal measurements from stdin, trims them,
checks parsing and explicit integer-to-float conversion, computes an incremental
mean, and prints fixed decimal output. Scratch bytes are reused by arena reset.

```sh
printf '1.25\n2.75\n3.5\n' | ./build/gloinc examples/numbers_lab.gloin
# count=3
# mean=2.50
```

Empty input prints `count=0`. Invalid numbers return exit status 2; values outside
±1,000,000 return 3; more than a billion measurements return 4. Input failures
return 1; conversion/allocation failures return 5/6/7. See [the API guide](../docs/numbers.md).
This example runs in the required and installed/relocated test suites.

### Streams and files (SPEC-030d)

`io_copy.gloin` reads source/destination paths as two stdin lines and creates a
**new** destination. It copies binary 4096-byte chunks, resets scratch storage,
checks flush and close, and prints the byte count. Existing destinations are
preserved; failures can leave a partial newly-created file.

```sh
printf '/tmp/source.bin\n/tmp/new-copy.bin\n' | build/gloinc examples/io_copy.gloin
printf ' hello\r\n# skip\n world \n' | build/gloinc examples/io_filter.gloin
# HELLO
# WORLD
```

`io_filter.gloin` trims and uppercases nonempty/non-comment lines, with a 256-byte
input bound and independent stderr error output. Both examples use explicit
arena lifetimes and borrowed standard streams. See [all API costs and errors](../docs/io.md).
Maintained tests execute both examples with installed and relocated compilers.

### Command-line context (SPEC-030e)

`file_tool.gloin` copies a regular source file to an exclusively-created new
output. It composes explicit argument parsing, bounded cwd/path joining, metadata,
stream I/O, arena reuse, checked close, and numeric formatting.

```sh
build/gloinc examples/file_tool.gloin -- --copy 'source file.bin' 'new copy.bin'
GLOIN_COPY_LABEL=saved build/gloinc examples/file_tool.gloin -- --copy source.bin copy.bin
build/gloinc examples/file_tool.gloin -- --help
```

A missing label uses `copied`; an empty label omits it. Paths are relative to the
caller's cwd unless absolute. Unknown options are rejected before mutation;
existing destinations are preserved, while failed copies may leave partial new
files. The source must be regular (symlinks are rejected in this example). Cwd is
bounded to 4096 bytes and joined paths to 65536. Installed/relocated tests run the
tool from another directory. See [filesystem and process contracts](../docs/filesystem-process.md).

## Streaming geometry and statistics

```sh
printf '3,4\n0,0\n6,8\n' | ./build/gloinc examples/math_lab.gloin
```

`math_lab.gloin` composes `@math` radius/bearing calculations and Welford statistics
with bounded input, checked parsing/conversion, scratch reuse, formatting, and
explicit output error handling. It prints count 3, min 0, max 10, mean 5,
population standard deviation 4.082, and final bearing 0.927 radians. Invalid or
empty input returns 2; output failures return 3. The [math guide](../docs/math.md)
documents bounds, ownership, cost, error rules, and numerical tolerances.

## Seeded simulation with monotonic timing

```sh
./build/gloinc examples/simulation_lab.gloin -- 42 1000
```

`simulation_lab.gloin` uses `@random` for two point coordinates and a die roll
per sample, `@math` for the error against pi, and `@time` for loop elapsed time.
Seed 42 and 1,000 samples yield 768 points inside the unit circle, dice sum 3,525,
and pi estimate 3.072000. Elapsed nanoseconds vary with the host. The loop has
constant memory use and performs no allocation; formatting uses an explicit
scratch arena. Defaults are seed 42 and 10,000 samples, with a maximum of one
million. Invalid arguments or clock errors return 2, and output failures return 3.
See [contracts and reproducible fixtures](../docs/time-random.md).

## Integrated library programs (SPEC-030h)

```sh
./build/gloinc examples/config_reader.gloin -- examples/data/simulation.conf
./build/gloinc examples/statistics_tool.gloin -- examples/data/measurements.txt /tmp/new-statistics-report.txt 1,2
```

`config_reader.gloin` validates required keys, borrows per-line slices, and copies
its retained label into an explicit owner. `statistics_tool.gloin` computes
count/min/max/mean for 1–16 selected columns in one pass with bounded memory.
It accepts a documented unquoted format and creates a new report only after
validating the input. Both export functions for module composition;
`simulation_lab.simulate` can consume the loaded seed/sample count.

See [formats, bounds, costs, failures, and resource verification](../docs/integrated-examples.md).
