# Packed-layout performance, 2026-10-29

Bounded word-sized field accesses improve the measured cross-byte 64-bit field
kernels by about **2–2.8×** on Apple M2. The change preserves alignment one,
neighboring bits, exact field bounds, and target-independent record bytes.
Fresh aggregate construction retains byte encoding: applying word-sized writes
there caused a measured regression after LLVM split the aggregate into bytes.

These are native kernel throughput results, not application speedup claims.
The reference point is the initial packed implementation at `c25c8f6`; the
optimized compiler is the development working tree. Compiler/runtime/source
hashes and a source patch identify the exact measured artifacts.

## Method

The standalone lab lives at `../gloin_examples/packed_bench`. It compares saved
before/after Gloin compilers with an explicit C byte-encoding implementation,
compiled by the same LLVM/Clang 21.1.6 toolchain at `-O2` for the native CPU.
The host was macOS 15.1.1, Apple M2, 16 GiB RAM. Driver and kernels are separate
translation units without LTO; only ordinary pointer/scalar arguments cross the
C boundary. Compiler startup, allocation, input generation, validation, and I/O
are outside the timed interval.

Twelve kernels cover both bit orders for six-byte header decoding, construction,
and masked flag updates; decoding and updating a 64-bit field starting at bit
seven of an 18-byte record; individual six-byte copies; and 256-record array
copies. Inputs are generated at runtime. Every invocation checks full output
against an independent bit-at-a-time oracle, checks input preservation and
canaries, and requires matching checksums across the three variants.

The final run uses 512, 16,384, and 1,048,576 records. Combined input/output
working sets range from 3.5 KiB to 26 MiB. Storage occupies a single arena with
native page alignment and fixed page-aligned buffer separation. Packed data
starts at an odd address; native scalar data retains its ordinary alignment.

Each case/size pair is calibrated in batches to approximately 30 ms for the
fastest variant; variants then use the same iteration count and seed. Batches
avoid extrapolating from a single call rounded to zero by the CPU clock. Two
warmups precede seven measured rounds, with case order and variant order shuffled.
The run contains **972 checked timed samples**, including warmups. Both process
CPU time and monotonic wall time are retained. There is no core pinning or
frequency control. Reported time is median CPU nanoseconds per record.

The C control includes narrowing checks and captures construction fields before
writing the destination. It assumes valid pointers; Gloin retains its pointer
checks. It is a comparison implementation, not an assertion of identical
language semantics or the fastest possible C encoding. The before/after Gloin
comparison preserves the same language contract.

## Results

Representative medians for **16,384 records**:

| Operation | Before ns/record | After ns/record | C ns/record | Before/after median ratio |
| --- | ---: | ---: | ---: | ---: |
| Header decode, MSB-first | 1.143 | 1.120 | 1.153 | 1.02× |
| Header decode, LSB-first | 1.224 | 0.954 | 0.778 | 1.28× |
| Header construction, MSB-first | 1.532 | 1.532 | 1.531 | 1.00× |
| Header construction, LSB-first | 1.533 | 1.532 | 1.533 | 1.00× |
| Flag update, MSB-first | 1.019 | 1.019 | 0.797 | 1.00× |
| Flag update, LSB-first | 1.018 | 1.019 | 0.908 | 1.00× |
| 64-bit field read, MSB-first | 2.163 | 1.067 | 0.928 | 2.03× |
| 64-bit field read, LSB-first | 2.159 | 0.774 | 0.631 | 2.79× |
| 64-bit field update, MSB-first | 2.298 | 1.082 | 1.071 | 2.12× |
| 64-bit field update, LSB-first | 2.298 | 1.004 | 0.907 | 2.29× |
| Individual six-byte copy | 0.897 | 1.005 | 0.713 | Inconclusive; see below |
| 256-record block copy, per record | 0.223 | 0.224 | 0.223 | 1.00× |

Across all three sizes, paired median speedups are 2.03–2.19× for MSB-first
wide reads, 2.73–2.79× for LSB-first wide reads, 1.99–2.16× for MSB-first wide
updates, and 2.13–2.35× for LSB-first wide updates. LSB-first header decoding
improves 1.28–1.36×. Construction and flag-update instructions are unchanged.
Block copies remain `memmove` operations and match the C control closely.

The medium individual-copy workload is noisy in all three variants, including
repeats with tighter allocation controls. Its paired median speedup is 0.87×,
with an exploratory 95% bootstrap interval of **0.78–1.08×**. That evidence is
insufficient to claim either an improvement or a reliable regression. The two
Gloin copy kernels have identical instructions after normalizing local branch
relocations, though their positions in the complete binary differ.

The smallest individual-copy case is unchanged. The largest shows a small
slowdown: the paired ratio is 0.97× (interval 0.94–0.99×), while the ratio of
separate medians is 0.96×. Earlier exploratory runs were approximately unchanged
there. This remains an unresolved performance observation; identical instructions
do not prove identical performance when code placement differs. No blanket
no-regression claim is made. All copy samples remain in the reported workload set.

## Why the change helps

Previously, reading the misaligned 64-bit value required nine byte loads plus
shifts and joins. Lowering now consumes the largest power-of-two chunk that fits
entirely inside the field's remaining containing bytes. The nine-byte case uses
eight bytes and one byte. Explicit byte swaps account for the module's target
endianness. Writes mask partial chunks and retain every bit outside the field.
No access gains a stronger alignment claim or reaches past the object's bytes.

The wide-update functions shrink from 34 instructions to 21 (MSB-first) and 20
(LSB-first). Header decoder functions shrink from 455/456 instructions to 131/123.
Wide-reader functions grow from 30 to 186/178 instructions because LLVM adds
vectorized paths and their guards. Their measured throughput improves, but the
extra code size is a tradeoff to check in larger applications.

Widening fresh construction was also tried. It introduced redundant swaps and
byte extraction after LLVM SROA split the byte-array aggregate, making the
six-byte constructor slower in the exploratory run. The final implementation
keeps direct byte encoding for fresh aggregate SSA values. Its constructor
instructions match the baseline exactly, and the final timings confirm that
the regression is removed.

## Further opportunities and limits

- Raw-pointer loops retain per-iteration null checks; the C controls omit them.
  This explains part of the remaining instruction-count gap in tiny updates and
  copies. Any elimination needs a proof from bounded storage or pointer ranges;
  dropping an observable failure check would change the language contract.
- Construction could benefit from directly storing complete encoded chunks into
  its final destination, but must retain initializer capture order, alias behavior,
  and zeroed fresh tail bits. Merely widening temporary stores was insufficient.
- The LLVM vectorization/code-size tradeoff and noisy medium copy case warrant
  an application-level workload or controlled code-placement experiment before
  further optimization. These measurements do not establish end-to-end packet
  processing, cold-cache latency, big-endian hardware, or other-CPU performance.

## Reproduction and correctness

```sh
python3 ../gloin_examples/packed_bench/bench.py \
  --compiler before=/path/to/saved/gloinc \
  --compiler after=build/gloinc \
  --output build/packed-performance/reproduction
python3 ../gloin_examples/packed_bench/analyze.py \
  build/packed-performance/reproduction
```

Each compiler needs its matching `libgloin_runtime.a` alongside it. The installed
results at `../gloin_examples/packed_bench/results/2026-10-29-m2` include sources,
raw samples, summaries, exploratory paired intervals, assembly, object sizes,
hashes, the optimization patch, and correctness evidence. Earlier exploratory
runs remain under `build/packed-performance` and are not treated as the final
result.

The compiler regressions now include 480 width/offset/sign/order layouts, with
70 inputs at both guard-page edges and both native optimization levels:
**134,400 executions**. Widths that span three, five, six, and seven bytes ensure
the power-of-two decomposition is exercised. Structural tests check eight-byte
plus one-byte accesses, alignment one, and byte swapping under synthetic little-
and big-endian target layouts. All **1,109 core tests** and both independent
packed/endian labs pass on the measured compiler. The packed lab compares
20,528,640 output bytes across JIT/`-O0`/`-O2`; the endian lab verifies 1,008,768
byte-sequence comparisons, 226 rejected programs, and 272 expected native traps.
Reports are saved with the benchmark evidence and in the
[0.3.0 draft](release-0.3.0-draft.md).

The independent packed lab also exposed a proof-lifetime bug during this work.
Range proofs were keyed by operation addresses; a newly allocated signed `i13`
operation could reuse an erased unsigned operation's address and incorrectly
receive an unsigned-division shortcut. Lowering now invalidates proofs through
a rewrite listener. A regression checks signed LLVM division and JIT/native
results across 64 functions mixing unsigned and signed packed-field updates.
The final benchmark uses the compiler with this fix, not the earlier candidate.
