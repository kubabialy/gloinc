# Packed storage indexing benchmark

`scripts/benchmark-packed-storage.py` compares saved development compilers using
separately compiled native `-O2` kernels and a C driver without LTO. It covers
sequential and indexed reads of `u3`, `u13`, `be_u32`, and three-byte packed
records. Views start three elements into an unaligned buffer. Indexed reads use
runtime-generated indices and an explicit checked length guard.

The driver independently decodes the input bits, validates every kernel result,
and checks complete input preservation and canaries after each timed batch.
Allocation, initialization, validation, process startup and I/O are outside the
timed interval. Kernel view construction remains inside it. CPU and monotonic
wall times are retained. This measures these read kernels, not application
throughput, packed stores/copies, concurrency, or other architectures.

Save the compiler, its `libgloin_runtime.a`, and its `stdlib` directory together
before changing the compiler. Then run with no competing builds or test suites:

```sh
python3 scripts/benchmark-packed-storage.py \
  --compiler before=/path/to/saved/gloinc \
  --compiler after=build/gloinc \
  --output build/packed-storage-performance
```

The default uses 512, 16,384 and 65,520 elements, two warmups and seven measured
rounds. Each case/count is calibrated to about 30 ms for the faster compiler;
all variants use the same repetition count and seeded inputs within a round.
Variant order is randomized. There is no CPU pinning or frequency control.
The default LLVM path matches the documented macOS toolchain; use `--llvm-bin`
for another installation. Output directories must be new.

The output retains generated sources, the runner, compiler/runtime/object/binary
hashes, tool versions, object sizes, disassembly, every warmup and measured
sample, and median CPU nanoseconds per element. Preserve failed and exploratory
runs alongside the final comparison. The initial baseline is saved locally under
`build/packed-storage-2026-11-08/performance/baseline-run`; it uses three measured
rounds and two sizes to validate the harness before optimization. It does not
establish an optimization result.

## Measured comparison, 2026-11-09

On Apple M2 (8 cores, 16 GiB), macOS 15.1.1 ARM64 and LLVM/MLIR 21.1.6,
the saved compiler at `6a4e3b7` was compared with the optimized development
compiler. There were no competing builds/tests. All **432 batches**, including
warmups, passed independent checksums, full input preservation and canaries.
The comparison used the default three sizes, two warmups and seven measured
rounds. Reported values are median CPU nanoseconds per element.

At 16,384 elements:

| Read kernel | Before | After | Before/after ratio |
| --- | ---: | ---: | ---: |
| `sequential_u3` | 2.955 | 2.043 | 1.45× |
| `indexed_u3` | 13.369 | 4.309 | 3.10× |
| `sequential_u13` | 2.451 | 1.966 | 1.25× |
| `indexed_u13` | 9.853 | 7.524 | 1.31× |
| `sequential_be_u32` | 1.014 | 0.696 | 1.46× |
| `indexed_be_u32` | 1.003 | 0.794 | 1.26× |
| `sequential_Record` | 1.163 | 0.744 | 1.56× |
| `indexed_Record` | 1.149 | 0.985 | 1.17× |

Across the tested cases and sizes, ratios range from **1.12× to 3.15×**. The
combined kernel object text shrank from **1,692 to 1,164 bytes** (31%). The
change combines exact-SSA packed bounds proofs, byte-stride address arithmetic,
and conditional bounded reads for native widths up to 56 bits. These data do
not isolate the contribution of each change, establish an application speedup,
or measure other CPUs, packed stores/copies, or every supported width/order.
Wider reads and stores still specialize the eight possible bit positions.

Raw samples, sources, binaries, hashes, disassembly and the compiler patch are
retained locally in
`build/packed-storage-2026-11-08/performance/comparison`. Saved before/after
compiler bundles are in the sibling `baseline` and `optimized` directories.
The earlier baseline-only run remains separate; it is not mixed into these
before/after medians. OS scheduling and frequency effects remain uncontrolled.
