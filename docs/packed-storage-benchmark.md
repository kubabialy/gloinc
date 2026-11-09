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
