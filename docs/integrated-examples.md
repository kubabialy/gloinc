# Integrated library acceptance (SPEC-030h)

These three programs use concrete types, checked results, manual lifetimes, and
ordinary local/standard modules. No generics or additional source-language native
primitives are needed. Each example is a runnable CLI and also exports a function
for local-module composition and embedding.

## Configuration reader

```sh
build/gloinc --jit examples/config_reader.gloin -- examples/data/simulation.conf
```

Output:

```text
label=quarter circle
seed=42
samples=1000
```

[config_reader.gloin](../examples/config_reader.gloin) reads a strict `key=value`
format. Each required key appears exactly once, in any order:

| Key | Value |
| --- | --- |
| `label` | 1–128 bytes after ASCII trimming; no NUL or `=` |
| `seed` | Strict unsigned decimal u64, including zero and u64 MAX |
| `samples` | Strict unsigned decimal in 1–1,000,000 |

Blank lines and whole-line comments whose first non-whitespace byte is `#` are
ignored. LF, CRLF, and a final line without a newline are accepted. There is no
quoting, escaping, interpolation, or inline comment syntax. Whitespace around keys
and values is trimmed; label bytes otherwise remain unchanged, including UTF-8.
Unknown/duplicate keys, missing keys, extra `=`, empty values, malformed numbers,
and NUL anywhere in a line are rejected. A line may contain at most 1024 bytes
(excluding the line ending), and the file at most 1,000,000 lines, including comments.

`config_reader.load(&owner, path)` returns `ConfigResult { status, line, value }`.
Its successful `Config` contains `label: string`, `seed: u64`, and `samples: u64`.
Keys and values borrow the current scratch line only while it is parsed. The label
is explicitly copied into the caller's arena; numbers become independent scalar
values. The local scratch arena resets each iteration and is freed before load
returns. Copying a Config copies the string view: the original caller arena must
remain alive for both copies. Neither Config owns or frees that arena.

Failures return an empty label/zero scalar payload; inspect status before use.
The one-based line identifies malformed data or a read failure. Missing required
keys report the line after EOF. Line zero identifies open/close failure. Native
OS error detail is intentionally not carried by this example's result. Failed
loads may retain partial allocations in the caller arena until its reset/free.
Deferred close handles early returns; successful loads explicitly check close.

`config_reader.execute(path)` owns its arena, loads, and prints canonical values.
It returns 0 on success, 2 for input errors (diagnostic on stderr), or 3 for
formatting/output failure. Diagnostics include the line, with `?` if formatting
that line number fails. The CLI expects exactly one FILE argument after `--`.

Work is O(input bytes); scratch storage is bounded by the line limit. The owner
retains file metadata, at most one label copy, and fixed-size numeric formatting.
The CLI uses another arena proportional to its supplied path's byte length. Arena state creation
retains GeneralArena's documented fatal allocation policy; subsequent fallible
buffer/metadata allocations return checked errors.

## Streaming selected-column statistics

```sh
build/gloinc --jit examples/statistics_tool.gloin -- examples/data/measurements.txt /tmp/new-measurements-report.txt 1,2
cat /tmp/new-measurements-report.txt
```

The output path must not already exist. Expected file contents:

```text
column,count,min,max,mean
1,3,0.000000,6.000000,3.000000
2,3,0.000000,8.000000,4.000000
```

[statistics_tool.gloin](../examples/statistics_tool.gloin) takes INPUT,
NEW_OUTPUT, and COLUMNS. COLUMNS is a comma-separated list of **1–16 distinct,
zero-based indices from 0 through 63**; output preserves this order. Whitespace
around indices is trimmed. There is no implicit header skipping.

The input is a simple **unquoted comma-delimited byte format, not CSV**:

- One record per LF/CRLF line; the final line may lack a newline.
- Nonempty input, no blank records, quotes, escapes, or NUL bytes.
- At most 4096 bytes per line, 64 fields per record, and 1,000,000 records.
- Every record has the same field count and contains every selected column.
- Selected fields are ASCII-trimmed finite f64 numbers with magnitude at most
  1,000,000,000,000. Empty/malformed/nonfinite/out-of-range fields are errors.
- Unselected fields may contain arbitrary other bytes, including empty fields.

`statistics_tool.execute(input_path, output_path, columns)` makes one pass over
the input. A concrete linked list holds each selected column's count, minimum,
maximum, and incremental mean. The value/count bounds keep arithmetic finite and
count conversion exact; the floating mean is approximate and output uses six
fractional decimal places. The independent test oracle uses integer sums for
exactly representable integer inputs, rather than the same incremental algorithm.

Scanning costs O(input bytes × selected columns); retained storage is
O(selected columns + maximum line bytes), independent of record count. List
nodes/file metadata belong to one owner arena. Borrowed field slices stay inside
one loop iteration, and a separate scratch arena resets before the next read.
Report formatting reuses scratch per output row. The CLI argument arena remains
separate from both and retains bytes proportional to the three arguments. Native stdio and path conversion have the costs documented
by [the I/O API](io.md).

Input must parse and close successfully **before** output creation. The program
uses `File.create_new`, so existing destinations (including the input path itself)
are never truncated. Success writes the report, checks flush and close, and leaves
stdout empty. Failure after creation may leave a partial new file; it is not an
atomic replace operation. Defers close live files before destroying their owners.

Exit 2 means bad arguments, invalid input, or input I/O/allocation failure. Exit 3
means selection-node allocation or report creation/formatting/write/flush/close
failure. Errors are diagnosed on stderr. Paths resolve relative to the caller's
working directory. Selected-column storage and all loops work without arrays,
generic containers, or automatic lifetime management.

## Numerical simulation and composition

```sh
build/gloinc --jit examples/simulation_lab.gloin -- 42 1000
```

[simulation_lab.gloin](../examples/simulation_lab.gloin) retains its seeded
quarter-circle estimate and dice checksum. It now also exports
`simulation_lab.simulate(seed: u64, samples: u64) -> i32`. The CLI parses its
arguments in an arena separate from the simulator's report-formatting arena.
No per-sample allocation occurs. Existing deterministic vectors, input bounds,
exact injected-clock test, and output/exit conventions remain as described in
[the timing/randomness guide](time-random.md).

A local module can load configuration, keep its owner alive, and pass
`parsed.value.seed` and `parsed.value.samples` to `simulate`. The maintained suite
executes this composition using the real modules and verifies the expected
768 circle hits / 3525 dice sum for seed 42 with 1000 samples. The copied label
is also checked after the loader's scratch arena has been freed.

## Verification and resource accounting

The integration suite runs the real examples through the CLI, in-process JIT,
and external LLVM runner. Package checks repeat CLI and external execution with
installed and relocated modules, samples, and runtime. It covers grammar/number/
size boundaries, column order, destination preservation, module composition,
allocation failures, and injected read/close/report-write/flush/close failures.
Existing native tests cover size arithmetic overflow and low-level partial I/O.

`gloin::arena::AllocatorScope` is an internal native host/testing facility,
not a Gloin primitive or an installed public API. It temporarily selects the
backing allocator for newly created arenas on the current thread. Arenas retain
that allocator after scope exit; its context must outlive all such arenas.
Callbacks must be non-null and non-throwing. Strictly nested scopes restore the
previous allocator, and other threads keep their own allocator. Normal execution
continues to use malloc/free.

Integration tests count **actual backing allocations and releases**, impose a
512 KiB arena backing budget, and require zero live allocations at return on
success and failure. 100,000-row and 100,000-comment-line runs each use two native
backing blocks with peak arena storage below 140,000 bytes; allocation budgets
would fail if per-line storage accumulated. Small/large simulations have identical
arena backing peaks. Open file-descriptor counts before/after execution catch
resource leaks, including repeated failures. These counts cover program arenas
and file descriptors, not compiler/LLVM heap usage. ASan/UBSan instrument project
native code; [JIT/prebuilt-library limitations](release-0.0.3.md) still apply.

The reproducible larger stress check is:

```sh
python3 scripts/check-integrated-examples.py build/gloinc
```

It compiles/runs all three programs with a 2 MiB stack, validates a million
records/lines, rejects one over each bound, and compares a million simulation
samples with an independent exact-integer oracle. Temporary files are removed
automatically. This is correctness/stack evidence, not a performance benchmark.
