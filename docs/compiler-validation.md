# Compiler validation and improvements

This is the development record of compiler issues found while checking larger
and more complex programs after 0.1.0. It covers correctness failures, compilation
cost and unnecessary work in generated code. The fixes belong to 0.2.0; their
implementation, measurements and limits are recorded below.

PERF-001–009 are stable identifiers for these completed investigations. Task
tracking belongs in the
[wiki implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist).
The [0.2.0 development record](release-0.2.0-draft.md) defines release scope.

## Reading the measurements

The timings are historical observations from development experiments. The
measurement setup, sample counts, failures and variation are recorded here;
experimental programs and raw timing archives are not distributed with the
compiler. Correctness regressions are retained in [compiler tests](../tests/README.md),
including [array](../tests/fixtures/core/run/repeated_arrays.gloin),
[Base64](../tests/fixtures/core/run/base64_decode/main.gloin) and
[capability-conversion](../tests/fixtures/core/run/capability_conversions/main.gloin)
fixtures. The measurements are not CI timing thresholds or general speed claims.

The initial observations used Gloin 0.1.0, LLVM/MLIR 21.1.6 and an Apple M2
running macOS 15.1.1. The application comparison used compiler revision `ce54198`.
Later entries identify their own baselines, modes and measurement boundaries.

## Initial findings

| Observation | What it establishes |
| --- | --- |
| 1,024-function optimized build: Gloin 9.35 s, ordinary C 790 ms, checked C 1.06 s | Compilation scales poorly on this arithmetic dependency chain, including against C with overflow traps. About 88% of main-thread samples were in LLVM optimization and machine-code generation. |
| Greeter: 133 GIR function bodies, including 132 imported functions/methods | Unused imported bodies reach lowering; a reachability pass has concrete work to remove. |
| Base64 fixed-array lookup versus borrowed slice: total 2.675 s versus 1.900 s | The controlled source change removes repeated 64-byte alphabet materializations: encoding is 2.34× faster, total time 29% lower, decoding unchanged. |
| Base64 benchmark: 2.347× scalar C's total time | Decoding is the larger remaining component; the totals alone do not attribute the cost to bounds checks, arithmetic or algorithm structure. |
| Matrix multiplication: 1.117× C; Brainfuck short workload: 1.368× C, Mandelbrot workload: 0.961× C | Different kernels and instruction mixes need separate analysis. |
| Large repeated byte-array initializers: two optimized builds stopped after several minutes; vector alternative reportedly compiled in under a second | A compile-time scaling symptom needs a minimal reproducer and bounded measurements; the stopped runs are not completed timing samples. |

The application comparison uses Gloin `-O2` with host CPU selection and C/C++
`-O3` without explicit CPU selection or LTO. JSON's pull reader, Go's typed
unmarshal and Python's DOM have different allocation/representation costs;
the approximately 2× JSON advantage is not a general parser ranking. The prime
case is close to C++ (1.021×), with different map layouts and allocation costs.
Memory and energy were not measured in that suite; its macOS RSS zeros come
from a Linux-only reader and are not usable memory measurements.

## PERF-001 implementation and validation — 2026-10-08

The working compiler adds [`--time-compilation`](compilation-timings.md) to all
compilation modes. It observes the existing GloinIR pipeline. Schema 1 records
exclusive stages, GIR/LLVM-dialect/LLVM instruction counts, compiler/LLVM versions,
mode, success, total/unattributed wall time and peak compiler RSS. Normal stdout
and exit status are preserved. Without the flag there are no timing/count/RSS
queries. Native `-O0` and `-O2` separate object generation from external linking.
JIT resolves its packed entry before freezing compilation measurements, then
runs the application; a callback-based regression proves the report is already
final inside the program. Failure reports contain only entered stages.

A Gloin measurement driver rotated released 0.1.0, working/plain and
working/timed invocations. One warm-up per
variant preceded five measured rounds. Every emitted executable passed its
checksum or matched release output; runner checks used the real task JSON in
dry-run mode. All **108 compilation/executable pairs passed**. Native builds
used `-O2` on Apple M2/macOS 15.1.1, LLVM/MLIR 21.1.6, without concurrent builds
or tests. The desktop was not pinned or isolated.

End-to-end milliseconds, **median ± median absolute deviation**:

| Fixture | Released 0.1.0 | Working, plain | Working, timed |
| --- | ---: | ---: | ---: |
| Greeter | 309.46 ± 8.38 | 313.99 ± 0.87 | 310.47 ± 5.23 |
| Modular task runner | 739.27 ± 4.83 | 733.13 ± 7.88 | 741.68 ± 11.71 |
| 16 arithmetic functions | 310.84 ± 6.48 | 309.44 ± 1.34 | 311.15 ± 1.15 |
| 128 functions | 717.92 ± 12.18 | 709.37 ± 2.02 | 711.76 ± 3.24 |
| 512 functions | 3,532.98 ± 12.37 | 3,486.57 ± 1.94 | 3,496.22 ± 43.17 |
| 1,024 functions | 11,341.06 ± 129.39 | 11,204.53 ± 26.42 | 11,204.05 ± 104.71 |

Timed/plain median differences range from **−1.12% to +1.17%**, with overlapping
ranges for all fixtures. This does not establish zero overhead or a speedup
from instrumentation. The working tree also includes PERF-002 and PERF-009;
use the recorded patch/hashes, not its still-0.1.0 version string, to identify it.
The older 9.35 s batch is not a controlled before/after comparison with this run.

At size 1,024, median exclusive durations are **5.50 s LLVM optimization**,
**4.41 s object emission**, **831 ms lowering** and **71 ms linking**. Optimization
and emission together occupy **88.94%** of the internal total (median per-run
share). The greeter retains 133 GIR definitions through lowering. These results
give PERF-004/PERF-005 explicit stage/count baselines without changing any checks.

Median peak compiler RSS at the finish boundary is **64.88 MiB** for the greeter,
**93.61 MiB** for the runner and **378.78 MiB** at size 1,024. The report uses
`getrusage(RUSAGE_SELF)`, retaining macOS bytes and converting Linux KiB to bytes.
It excludes linker children and application execution. Separate OS `time` checks
on both platforms are retained; whole-process peaks can exceed the snapshot
because reporting/teardown follows it. See the timing guide before comparing
these values with process-tree or live-memory measurements.

A reporting-script failure occurred after all 108 executable checks completed.
The missing report footer and binary hashes were collected afterward; no timing
records were lost.

Validation: **1,000 macOS core tests**, **107 focused Linux ARM64 tests** and
**36 macOS ASan/UBSan tests** passed. CLI tests cover native/JIT/check/inspection,
optimization boundaries, parsing/import/semantic/output failures, duplicate
flags, quiet default output and valid reports. The Gloin benchmark tools pass
`gloinfmt --check`; the documentation checker passes all seven HTML pages.

## PERF-003 initial investigation — 2026-10-08

At this initial checkpoint PERF-003 was still open. A local
bounded Gloin runner tested flat `[u8; N]` and nested `[[u8; 8]; N / 8]` arrays
at 64, 512, 4,096 and 16,384 total bytes, using a parameter-valued repeat or
`zeroed`. Each case had `--check`, native `-O0` and native `-O2` probes, with a
20-second communication deadline per invocation. These are single exploratory
samples, not repeated performance claims. Successful binaries check their first
and last element. There were **46 completed compilations**, including **30
passing native executable checks**, and **two optimized-build timeouts**.

| Total bytes | Flat repeat, `-O2` internal time | Nested repeat, `-O2` internal time | Flat zeroed | Nested zeroed |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 88 ms | 82 ms | 92 ms | 85 ms |
| 512 | 125 ms | 191 ms | 94 ms | 90 ms |
| 4,096 | 1,732 ms | 1,832 ms | 81 ms | 79 ms |
| 16,384 | Timed out | Timed out | 74 ms | 77 ms |

At 4,096 bytes the flat repeat takes **1,646 ms in LLVM optimization**, while
entering LLVM with only **37 instructions** and finishing with 17. Nested repeat
enters with 54 and finishes with **16,416 instructions**, taking **1,389 ms in
optimization** plus **361 ms in object emission**. Small initial operation counts
therefore do not rule out expensive intermediate expansion.

The LLVM-dialect dump shows a counted fill into one stack array, followed by a
whole-array load/store into the local binding's second array. A separate
three-second sample of the 16,384-byte flat repeat found all 2,327 main-thread
samples inside LLVM `InstCombine`, almost entirely visiting allocation sites
and tracking their uses. This is a sampled window, not a complete pass-time
breakdown; the profiled invocation also timed out and is excluded from timings.
The next implementation should preserve compact initialization/copying through
GIR and lowering, avoiding this aggregate path while retaining value semantics
and exactly-once initializer evaluation. A byte fill is valid only for suitable
representations; changing arrays to heap storage is not part of this work.

No completed timing or final RSS report is inferred for the timed-out builds.

## PERF-003 implementation and validation — 2026-10-12

Stored repeats now pass through a verified `gloin.array_fill` operation before
the source-type/layout boundary. The GIR preparation pass retains the captured
SSA seed, flattens chains of nested repeats, and removes unused repeat values
and bridges. It preserves effectful or trapping seed evaluation, including for
empty arrays. Byte elements lower to memset; all other elements use typed stores
with their actual stride. The destination keeps its original storage and array
value semantics. See [the GIR contract](lowering.md) and
[array guide](fixed-arrays.md#compilation-cost-after-010).

For the 4 KiB reproducer, lowered flat repetition goes from two array allocations
to one, and nested repetition from three to one. Both now use one byte fill and
have no aggregate array load/store. The nested case's optimized LLVM instruction
count drops from **16,416 to 19**. Flat and nested fills remain compact through
the tested 1 MiB size. The pass disables generic region simplification so it
cannot introduce source-typed block arguments outside the layout pass's contract;
a dedicated result/array return-branch regression covers that boundary.

Same-source comparison on Apple M2/macOS 15.1.1, LLVM/MLIR 21.1.6, `-O2`:

| Repeated byte array | Before | After, median ± MAD |
| --- | ---: | ---: |
| Flat, 4 KiB | 2,523.71 ± 232.77 ms | 108.24 ± 0.88 ms |
| Nested, 4 KiB | 2,686.09 ± 28.89 ms | 111.43 ± 4.21 ms |
| Flat, 16 KiB | 20-second timeout | 110.66 ± 2.29 ms |
| Nested, 16 KiB | 20-second timeout | 111.08 ± 1.33 ms |
| Flat, 1 MiB | Not measured | 109.49 ± 1.61 ms |
| Nested, 1 MiB | Not measured | 111.12 ± 2.40 ms |

These are internal compiler wall times including linking, excluding process
startup, final report serialization and teardown. Each measured variant has
five samples after one discarded warm-up; before/after variants at 4 KiB ran in
rotated order. The 16 KiB old-compiler timeout was reconfirmed once for each shape
and has no completed timing or final RSS value. No builds/tests ran concurrently;
the desktop was not pinned or isolated. All raw variation is retained: for
example, flat 4 KiB ranged from 2,289.78–3,049.69 ms before and 107.36–165.95 ms
after. The older October 8 times are historical evidence, not the paired baseline.

At 4 KiB, median LLVM optimization falls from **2,408.12 to 2.01 ms** for flat
repetition and **2,064.46 to 2.10 ms** for nested repetition. The fixed 16 KiB and
1 MiB cases also spend about 2.1–2.2 ms there. Median compiler-process RSS stays
around **44–45 MiB** for the fixed cases; this is the timing report's high-water
snapshot and excludes the linker child and application execution.

The complete four-shape, four-size matrix passed all **48 compilations** in
check/native `-O0`/native `-O2` modes, including **32 executable checks**. The
alternating/scaling experiment passed **48 additional compilation/executable
pairs**, including warm-ups, with the two expected old-compiler timeouts recorded
separately.

Correctness validation: **1,004 macOS core tests**, the additional CFG regression,
**303 Linux ARM64 focused tests** and **43 macOS ASan/UBSan tests** passed.
The [acceptance fixture](../tests/fixtures/core/run/repeated_arrays.gloin) covers
byte/wide integer/float/bool/pointer/reference/string/struct values, empty and
nested repeats, array-valued seeds, aliases, independent copies, field assignment,
loop reinitialization and returned arrays in JIT and native `-O0`/`-O2`. Separate
empty-array tests prove trapping initializers still run. GIR tests reject invalid
storage capabilities and element/layout boundaries, and guard compact IR at
16 bytes, 16 KiB and 64 KiB. Formatter and all seven HTML link checks pass.

**Scope:** this fixes stores of repeated values and nested repeat chains.
Large by-value arguments/returns, existing array-valued seeds, and arrays inside
other constructed aggregates retain their current aggregate representation and
can still have substantial copying/compilation costs. Those contexts are covered
for correctness here, not claimed to have a new ABI or bounded compilation cost.
This fix is local and unreleased.

### PERF-003 generated-code runtime comparison — 2026-10-12

The follow-up measures actual initialization into caller-owned storage. Six
Gloin kernels are compiled to native objects with the saved pre-fix and fixed
compilers (`-O2`, host CPU). A separately compiled C driver prevents inlining or
deleting writes across the call boundary; it times a fill and one indexed read
per iteration, and verifies every slot with several values outside the timer.
The native object wrapper's unused `main` symbol is renamed only for linking
that driver. Kernel instructions are unchanged by this step; there is no LTO.

Apple M2/macOS 15.1.1, LLVM 21.1.6; nanoseconds per fill/read iteration,
median ± MAD over seven samples after two discarded warm-ups:

| Kernel | Before | After |
| --- | ---: | ---: |
| 64 bytes | 2.145 ± 0.037 | 2.137 ± 0.062 |
| 4 KiB bytes | 1,266.62 ± 9.44 | 94.55 ± 6.43 |
| 4 KiB nested bytes (64 × 64) | 1,280.98 ± 44.24 | 83.01 ± 5.73 |
| 4 KiB i64 | 245.62 ± 26.07 | 94.51 ± 9.66 |
| 4 KiB nested i64 (64 × 8) | 232.00 ± 5.85 | 92.97 ± 9.11 |
| 4 KiB f64 | 254.16 ± 38.13 | 101.79 ± 7.27 |

All **108 executions** passed content and checksum checks. Compiler order
alternates each round, both variants use the same driver/runtime, and compilation
and full-content checks are outside the monotonic timed loop. The small case is
effectively unchanged; the larger cases improve. These six hot-buffer kernels
show no runtime regression on this host. They do not establish whole-application,
cold-memory, `-O0`, Linux, or universal performance guarantees. No builds/tests ran
during measurement; the desktop was not isolated. Raw samples retain variation
(for example, fixed flat-byte samples span 77.31–211.48 ns per iteration).

The old compiler's
combined six-kernel object initially exceeded the 20-second probe deadline; a
separate bounded 120-second build completed in 69.89 s before runtime measurement.
The timed-out attempt contributes no runtime sample.

## PERF-004 implementation and validation — 2026-10-12

The shared lowering pipeline now has a checked-GIR reachability pass before
array preparation and source-type erasure. It follows symbol references from
`main` and retained non-function references, including module metadata. Methods,
recursive groups and concrete generic instances use the same traversal. Dead
recursive groups are removed. The existing native entry collision diagnostics
remain visible; runtime declarations, LLVM functions, globals and nominal type
declarations are retained. Unknown symbol scopes conservatively prevent pruning.

All parsing, semantic checking, GIR generation and initial verification/legality
checks precede the pass. Module inspection and native object output preserve
functions, including externally callable object symbols. Native executable
`-O0`/`-O2` and JIT lowering of checked GIR enable pruning; the JIT operates on
its owned clone. Arbitrary unmarked or already lowered MLIR keeps its existing
behavior. See [the complete contract](lowering.md#executable-reachability-unreleased-perf-004).

The timing report now includes the exclusive `gir_reachability` stage and a
`reachable_gir` snapshot. Initial `gir` counts and inspection output still show
the full program. This exposes the work removed before expensive lowering while
keeping the cost of parsing/checking the full source graph visible.

Validation passed **1,012 macOS core tests**, **316 Linux ARM64 focused tests**
and **24 macOS ASan/UBSan tests**, with no sanitizer errors. New regressions cover
reachable and unreachable recursion, methods, generic instances, symbol metadata,
indirect function values in verified IR, invalid unused source/IR, retained
object symbols at both optimization levels, and JIT execution. Existing JIT
entry, ABI validation and borrowed-module preservation tests also pass.

The initial alternating comparison passed **36 compilation/executable checks**.
Greeter definitions entering lowering dropped **133 → 10**; the task runner
dropped **347 → 169**, and the 16-function arithmetic fixture **149 → 18**.
The arithmetic program imports `@std` for output, so it also benefits from
removing unused imported functions. Actual executable sizes dropped from
**179,088 → 76,768 bytes**, **264,656 → 217,248 bytes**, and
**179,560 → 75,480 bytes**, respectively. These are file sizes including linked
runtime/platform metadata, not resident-memory measurements.

Greeter complete-invocation time fell from **424.71 ± 17.11 to 207.73 ± 9.94 ms**
(median ± MAD, five samples after one warm-up). The task runner's initial result
was inconclusive: **1,264.31 ± 36.24 versus 1,313.14 ± 117.10 ms**, with broad
overlapping ranges. The follow-up below uses more samples and adds a no-import
minimal control.

The follow-up passed **88 compilation/executable checks**, using two discarded
warm-ups and nine samples per fixture/variant, alternating compiler order:

| Fixture | Before, complete invocation | After, complete invocation |
| --- | ---: | ---: |
| Greeter | 434.86 ± 5.61 ms | 208.31 ± 4.96 ms |
| Task runner | 1,251.73 ± 5.26 ms | 1,004.62 ± 47.96 ms |
| 16 arithmetic functions | 706.04 ± 31.50 ms | 420.16 ± 53.86 ms |
| Minimal, no imports | 148.01 ± 1.78 ms | 147.65 ± 2.46 ms |

These are medians ± MAD on Apple M2/macOS 15.1.1/LLVM 21.1.6, `-O2`, with no
concurrent build/test jobs. The desktop was not isolated or CPU-pinned. Compare
within each alternating batch; do not mix first-batch and follow-up medians.
The task runner now shows a lower median, with overlapping full ranges still
retained (1,239.20–1,721.93 ms before; 949.43–1,398.34 ms after). This is a measured
improvement on these workloads, not a fixed speedup guarantee.

Function counts and executable sizes match the first batch. The minimal program
retains its one function and 74,832-byte executable; its reachability pass takes
a median **0.041 ms**. Greeter reachability costs 3.55 ms and task-runner
reachability 11.42 ms. Greeter LLVM optimization falls from 90.76 to 5.50 ms and
object emission from 112.44 to 7.37 ms. Task-runner LLVM optimization falls from
367.42 to 259.09 ms and object emission from 449.45 to 316.41 ms. The internal
compiler totals, stages, RSS and complete-invocation times were measured separately.

This implementation is local and unreleased. It does not skip diagnostics in
unused source, remove runtime checks, or internalize the surviving function ABI.

## PERF-005 implementation and validation — 2026-10-12

The implementation keeps checked arithmetic in GIR and changes three specific
parts of compilation:

1. GIR preparation folds representable integer constants and safe identities.
   Overflow and invalid division/remainder remain trapping operations; operand
   calls, effects and evaluation order survive a discarded result such as
   `danger() * 0`.
2. Remaining checked `+`, `-` and `*` become LLVM's signed/unsigned overflow
   intrinsics at the source width, followed by a checked overflow-bit guard.
   Division/remainder guards and floating-point rules keep their existing
   behavior. There is no new wrapping or unchecked arithmetic mode.
3. Native O2 keeps LLVM's ordinary inlining profitability decisions until a
   caller reaches 2,048 instructions, then declines further optional inlining.
   This is an initial growth policy, not a maximum source-function size.
   Mandatory inlining follows LLVM's rules. JIT and O0 do not run this pipeline.

See [the lowering contract](lowering.md#checked-integer-simplification-and-overflow-unreleased-perf-005)
for the exact transformations and limits.

### Why the exported-object control matters

The first intrinsic-only comparison improved the original executable benchmark,
but the input is only a choice between two seeds. Auxiliary stock LLVM O2
analysis reduced the new source `main` to four instructions, compared with
24,577 previously; the previous largest basic block contained 24,568 instructions.
This demonstrates specialization as well as cheaper arithmetic representation.
That inspection pipeline retains the full module and omits the native entry
adapter, so its pass times are diagnostic rather than native stage measurements.

The execution benchmark uses exported functions whose input comes from a
separately compiled C caller, without LTO. It exposed a regression in the
intrinsic-only compiler: the 1,024-function object's single build rose from
6.39 s to 35.30 s. A separate sampled run reproduced 36.84 s, with 22.20 s in
LLVM optimization and 12.98 s in object emission. The profile again identifies
InstCombine and recursive known-bits analysis in the giant inlined dependency
chain. This candidate was not accepted unchanged.

A 4,096-instruction inlining threshold removed the large regression, but the
128-function object still took 973.59 ms versus 763.35 ms in its repeated
comparison. The final 2,048 threshold limits that expansion earlier. Keeping
calls can forgo specialization, so execution speed is measured separately;
neither compact IR nor faster compilation alone proves faster programs.

The arithmetic CLI benchmark remains useful for compilation scaling and
correctness. Its short executable run is not an arithmetic-throughput benchmark.
The exported runtime kernels are independently checked for 20 seeds and every
timed invocation contributes to a checked checksum. Timers exclude compilation,
process startup and reference checks. The fixed source bodies are compiled
before/after; only the unused native `main` wrapper symbol is renamed for the
C driver link.

### Accepted compiler measurements

Apple M2, macOS 15.1.1, LLVM 21.1.6, native `-O2`, on 2026-10-12. The baseline
is the saved compiler after PERF-004; the final compiler includes GIR folding,
overflow intrinsics and the 2,048-instruction inlining policy. Compiler order
alternates each round: one discarded warm-up and five measurements per variant
and size. Each emitted executable or object is executed and checked outside its
compilation timer. No profiling, test suite or other agent-started build ran
alongside these measurements. The desktop was not isolated or CPU-pinned.

Outer compilation wall time, median ± median absolute deviation, milliseconds:

| Functions | Executable before | Executable after | Exported object before | Exported object after |
| --- | ---: | ---: | ---: | ---: |
| 16 | 306.71 ± 66.43 | 318.47 ± 86.58 | 596.78 ± 24.92 | 574.08 ± 60.41 |
| 128 | 1,000.46 ± 13.01 | 917.28 ± 5.59 | 1,364.36 ± 68.29 | 1,209.76 ± 330.94 |
| 512 | 6,333.57 ± 441.65 | 2,210.71 ± 81.50 | 3,243.96 ± 93.77 | 2,587.37 ± 182.02 |
| 1,024 | 20,719.02 ± 400.25 | 4,067.03 ± 178.30 | 7,062.58 ± 968.81 | 5,365.12 ± 312.75 |

The largest executable improves **5.09×** in this batch; the exported object
improves **1.32×**. The small executable's distributions overlap, and several
object samples are noisy. Earlier candidates/batches have different absolute
times; their medians are not mixed with this table or treated as confidence
intervals. All **96 final compilation/output checks** pass.

For the 1,024-function executable, median LLVM optimization falls from
10,038.27 to 595.75 ms and object emission from 7,810.52 to 1,372.35 ms.
The original GIR is identical in size (91,864 operations); retained GIR is
85,156 operations in both compilers. LLVM-dialect operations fall
161,922 → 145,538; translated LLVM instructions 148,504 → 132,120; optimized
instructions **60,428 → 34,797**. The executable shrinks **624,944 → 443,312 bytes**.
The exported object's optimized instructions fall **72,290 → 39,191**, and its
size **702,544 → 435,528 bytes**. Median compiler RSS falls from 383,713,280 to
293,666,816 bytes for the executable and 379,518,976 to 318,980,096 for the object.
These process high-water figures exclude linker children.

### Execution speed and trade-offs

Two warm-ups and seven measured runs per kernel/variant, alternating order;
all **144 executions** pass reference/checksum checks. A step is
`(value * 17 + offset) % 1000003`. The normalized times include call and checksum
overhead; the small kernels permit overlap between independent calls, so compare
before/after within a row rather than interpreting these as instruction latency.

| Kernel | Before, ns/step | After, ns/step |
| --- | ---: | ---: |
| 16-function chain | 6.7470 | 6.6388 |
| 128-function chain | 7.2992 | 7.2314 |
| 512-function chain | 7.3243 | 7.3954 |
| 1,024-function chain | 7.3469 | 7.4819 |
| Eight-step `i32` | 2.4046 | 2.3962 |
| Eight-step `i64` | 2.4044 | 2.3764 |
| Eight-step `u32` | 1.3263 | 1.3646 |
| Eight-step `u64` | 1.3217 | 1.3271 |

Execution has small measured regressions: the 1,024-function median is
**1.84% slower** and the `u32` median **2.88% slower**.
The `u32` assembly retains separate initial multiply/add overflow branches;
the previous lowering combined them into one bound check. Several sample ranges
overlap, but that code-generation difference is real. The compilation and size
gains are accepted with these measured limits. General range proofs and improved
guard combining remain separate work; no checks are disabled to obtain a score.

An initial measurement-script failure stopped before execution sampling. The
script was corrected and complete comparisons rerun. The final compiler's
SHA-256 is `9f08e483d653960c13a9565cd23c45a6c4bef21aca4b78c0d64b268134fa15a9`;
the baseline is `32cdc1fbd1a2346af793035d21dba19471993c8af8d135ad79c219e7ac15ddc0`.

### Verification

**1,017 macOS core tests**, **349 focused Linux ARM64 tests** (LLVM 21.1.8), and
**92 macOS ASan/UBSan tests** pass. The new native matrix executes **584 cases**
covering signed/unsigned 8/16/32/64-bit boundaries at O0 and O2, including
overflow, zero divisors, minimum/-1 division and remainder, discarded overflow
results, and overflow hidden inside multiplication by zero. JIT/external
execution tests also cover trapping operands after GIR simplification.

IR tests verify exportable source-width overflow intrinsics, safe constant and
identity folding, retained operand calls in order, and a large native call chain
whose instruction count stays below full expansion while its checksum remains
correct. Full core coverage includes the previous array, source-type, result,
module and reachability regressions. Formatter checks and all seven HTML pages
with local links pass; `git diff --check` is clean. The implementation is local
and unpublished.

## PERF-006 implementation and measurements — 2026-10-12

The development compiler now promotes eligible integer/slice locals with MLIR's
memory-slot interfaces while their operations still use GIR. Dominating unsigned
comparisons, constant/zero-extended ranges and checked affine offsets can prove
arithmetic and array/slice element checks unnecessary. Proofs refer to SSA
snapshots. Escaped local addresses block promotion; fresh element/descriptor loads
through aliases do not inherit earlier facts. Unknown ranges and joins retain
guards. No input-IR attribute can request an unchecked operation. See the
[precise scope and limits](lowering.md#bounds-and-unsigned-range-proofs-unreleased-perf-006).

The paired source experiment keeps the original decoder and adds a grouped
decoder. Complete groups validate source/destination capacity and four alphabet
values before decoding three bytes; padding stays in a separate final group.
Both return built-in `result<u64>`. The new decoder returns errors for short
output/table slices and values outside the table's 0..63 domain. The old decoder
could trap on undersized storage. Like the original, the new codec accepts
nonzero unused bits in padded groups, rejects whitespace/URL-safe characters,
and can leave an output prefix on failure. Its buffers must not overlap. This
is a tested benchmark codec, not a new standard-library API.

### Controlled comparison

The Gloin driver measures the original encoder and either decoder with monotonic
wall clocks; buffer preparation and complete byte/length checks are outside the
phase timers. A scalar C control implements the grouped codec's valid-input,
alphabet/padding and output-capacity behavior and also uses `CLOCK_MONOTONIC`.
It uses no SIMD library, has externally visible non-inlined codec functions, and
is built with Clang 21.1.6 `-O2 -mcpu=native`. Gloin uses native `-O2` with its
default host CPU selection. The C encoder assumes the prepared output capacity
and C does not supply Gloin's general arithmetic checks, so this is an application
control rather than a universal compiler comparison.

Apple M2, macOS 15.1.1, LLVM/MLIR 21.1.6; 131,072 bytes × 8,192 iterations each
way. ASCII repeats `a`; binary cycles through all 256 byte values. Two warmups
and seven measured rounds rotate/reverse the five variants. All **90 executions**
pass their round-trip/output and accumulated-length checks. No task builds,
tests or profiler ran during sampling. The desktop was not isolated or pinned;
raw samples include substantial scheduling outliers.

Decode seconds, median ± median absolute deviation:

| Compiler / codec | Repeated ASCII | Binary sequence |
| --- | --- | --- |
| Before / original | 2.310106 ± 0.207631 | 2.310668 ± 0.116365 |
| After / original | 2.513767 ± 0.169529 | 2.231937 ± 0.080110 |
| Before / grouped | 0.994028 ± 0.022001 | 0.980077 ± 0.033351 |
| After / grouped | 0.915881 ± 0.031345 | 0.928886 ± 0.043192 |
| Scalar C / grouped | 0.936398 ± 0.074615 | 0.921819 ± 0.041246 |

The combined source/compiler change is **2.52× / 2.49×** faster than the
original baseline in this batch. Grouped Gloin and C decoding are close, with
overlapping observed ranges. This does not establish a general Gloin/C ranking.
Encoding remains a separate cost: after/grouped encoding takes
1.067608 ± 0.040517 s (ASCII) and 1.035557 ± 0.066353 s (binary), versus C's
0.765072 ± 0.025231 and 0.787943 ± 0.082354 s. Every phase sample is retained.

**Attribution matters:** the grouped O2 main loop has identical instructions
before and after this compiler pass. LLVM already proved those explicit group
guards sufficient. The large optimized-code improvement belongs primarily to
the source loop restructuring, not to a new compiler-only speedup. The new
compiler removes an additional padded-tail-path bounds check and changes final
checked-length arithmetic scheduling; it does not remove alphabet validation
or the once-per-group output-capacity guard. Powers-of-two division/remainder
are shifts/masks in both versions.

The original decoder's apparent ASCII regression in the five-way batch required
a separate alternating confirmation (two warmups, seven measured rounds, **36 checked executions**). Decode medians were
2.035388 ± 0.018664 → 2.030811 ± 0.012681 s for ASCII, and
2.028783 ± 0.011783 → 2.025474 ± 0.006398 s for binary: effectively unchanged.
The first batch is preserved; do not mix its absolute times with the confirmation.

### Compiler-owned proof effects

Before LLVM optimization, the grouped decoder's conditional branches fall
from **69 to 27**, and overflow intrinsics from **14 to 2**. The complete native
program's exported LLVM instruction count falls from **1,466 to 1,139**; after
LLVM O2 the counts are **1,106 and 1,102**. The original decoder also loses
pre-LLVM work (48 → 39 conditional branches, 18 → 15 overflow intrinsics).
These counts include local promotion and subsequent lowering cleanup; one shared
trap block is not a count of all checks. Compilation reports and disassembly are
saved, but single compilation samples do not establish a latency improvement.

The compiler's independent execution benefit is visible under `-O0`, where LLVM
does not run the O2 proof passes. On the same grouped source at 128 iterations,
two warmups and five measured rounds (**28 checked executions**), decode medians
are 101.369 ± 0.499 → 54.095 ± 0.339 ms for ASCII (**1.87×**) and
107.049 ± 1.066 → 58.930 ± 0.278 ms for binary (**1.82×**). These are a separate
batch/work count; they must not be compared directly with the O2 seconds above.

Baseline compiler SHA-256:
`9f08e483d653960c13a9565cd23c45a6c4bef21aca4b78c0d64b268134fa15a9`.
Measured compiler SHA-256:
`e143f398b597addf105613f47cf030b9c39b6c51fcfa9baf15169d436d50e6b7`.
The final compiler additionally skips GIR analysis for LLVM-only contexts that
have never registered the Gloin dialect (SHA-256
`6eedc4ba1f512268da004973ad0bf8ce2eaef419de8a1797e3250c8345de44ae`).
Rebuilding both benchmark source variants with that compatibility fix produces
identical full executable disassembly to the measured compiler, excluding the
output filename heading. The measurements therefore describe the final generated
code as well; the compatibility change is covered separately by JIT ABI tests.
The [Base64 acceptance fixture](../tests/fixtures/core/run/base64_decode/main.gloin)
retains the codec contract and independent correctness checks.

### Verification scope

New tests inspect exact unsigned proofs and retained checks, execute alias/index
and descriptor mutation, joins, signed negative indices, insufficient bounds and
loop-backedge failures in JIT/O0/O2, and preserve every arithmetic width's overflow
behavior. The registered Base64 fixture covers RFC examples, binary bytes, all
256 single-byte values, every non-alphabet byte in each full-group position,
padding/length failures, short tables, invalid table values, exact output slices
with sentinels and short output boundaries.

The final macOS `check-core` gate passes **1,022 tests** (336.79 s). Linux ARM64
with LLVM 21.1.8 passes **426 distinct focused tests**, combining 423 successful
initial cases and three successful retests after fixes. The first full run found
that LLVM-only JIT contexts need not register GIR; the optional operation lookup
is now checked before accessing its interface. The other Linux failure was an
aggregate timeout in the existing 584-execution arithmetic matrix, not a failed
arithmetic case. It now has a 360-second Linux/sanitizer deadline, retaining each
child's 10-second limit, and passes in 267.34 s on this VM.

macOS ASan/UBSan passes **97 targeted optimization/codec tests** (224.12 s), then
**two LLVM-only compatibility tests** on the final rebuild, with no sanitizer
findings. The source compiler paths and generated code from the measured build
are unchanged by that compatibility fix. Formatter checks, all seven HTML pages
and their local links, and `git diff --check` pass.
This work remains local and unpublished.

## PERF-007 implementation and measurements — 2026-10-12

The retained matrix bounds guard was not redundant from the local facts: the
loop compares `k` with `n`, while the row slices carry independent runtime
lengths. Knowing that both rows were built with `n` elements would require a
broader proof across vector construction, stored descriptors and aliases.

GIR now versions eligible counted slice loops instead. An entry predicate tests
`n <= p.len && n <= r.len` using the exact promoted descriptors. Its true arm
selects a copied loop with proven bounds; its false arm retains the original
checked loop. **The predicate does not raise an early error.** Short slices
still execute the valid prefix, including calls and writes, before the original
failing access; an earlier arithmetic failure still happens first. The fast
copy also proves its matching signed/unsigned `k + 1` safe. Existing unsigned
range proofs are carried into the copy.

Recognition is limited to zero-based `i64`/`u64` counters, `<` with an invariant
limit, unit increments and direct slice indexing. The header contains only its
comparison and branch; the body is a single block with at most 64 GIR operations
and four distinct candidate descriptors. Regions, body allocations and direct
SSA live-outs reject cloning. Escaped counters, changed descriptors and other
starts/strides retain checks. Each candidate receives at most one extra copy.
The [lowering guide](lowering.md#counted-slice-loop-versioning-unreleased-perf-007)
documents the proof and its boundaries. This is compiler work over GIR, with
MLIR SSA/CFG facilities; the original matrix source is unchanged.

### Floating-point findings

Both copies preserve separately rounded products, ordered additions and every
finite-result check. LLVM receives no fast-math, reassociation or contraction
permission. In the original application's optimized fast loop, assembly has
scalar `fmul` and `fadd`, finite comparisons and the loop backedge, with no
per-element slice bounds branch. The fallback still has that branch.

LLVM 21 remarks for the isolated dot product report that the reduction value is
used outside the loop and that an early-exit loop has more than one early exit.
Removing bounds checks alone does **not** make this checked floating-point loop
vectorize. In the C controls, omitting finite-result checks permits two-lane
vector multiplies, interleaving four groups, followed by ordered scalar adds.
The scalar tail handles the remainder. There is no vector/reassociated sum or
fused multiply-add. The C bounds-only control hoists its bounds failure ahead
of the loop: its pure unchecked arithmetic prefix has no effects to preserve.
That does not justify early failure for general Gloin loops.

This investigation does not change the finite-value policy. Further checked
floating-point vectorization would need additional proofs or a transformation
that preserves all relevant failure/effect ordering. Relaxing the policy would
require a separate language decision.

### Controlled measurements and evidence

The matrix experiment extracts the same ascending-index dot product. Before and after compilers receive identical
Gloin source. All six variants use the same separately compiled C driver,
inputs, transposition, ordered reference and bitwise verification of **every
output cell**. Initialization/reference/checking are outside the monotonic
kernel timer. Separate translation units without LTO prevent call elimination.

The four C builds independently enable bounds checks and finite-result checks.
Finite checks use the same ordered comparisons against `DBL_MAX` as Gloin's
lowering. Builds that omit checks deliberately have weaker contracts: they
quantify experiments, not proposed compiler flags. C uses LLVM Clang 21.1.6,
`-O2 -mcpu=native -ffp-contract=off`; Gloin uses native `-O2` on the same Apple
M2 host. The current macOS ARM64 object ABI is verified by the output checks;
this does not establish a portable public C ABI.

Two discarded warmups precede seven measured samples per variant/size, with
rotated/reversed order. Sizes 128/256/512 use 128/16/2 matrix repetitions,
respectively: **268,435,456 products per measured run**. Twelve additional sizes
from 1 through 513 cover empty-loop controls, small dimensions and vector tails.
Cancellation-sensitive and contraction-sensitive inputs also run before timing.

A separate whole-process comparison uses the unmodified original program
at sizes 256/512/768, with two warmups and seven alternating pairs per size.
That timer includes process launch, the existing `calc(101)` self-check,
allocation, matrix construction, output and teardown. Do not mix those numbers
with kernel-only timing or the earlier suite's 1.117× aggregate comparison.

Kernel wall time, median ± MAD in milliseconds (seven samples per cell):

| Variant | 128 × 128 | 256 × 256 | 512 × 512 |
| --- | ---: | ---: | ---: |
| Gloin before | 542.144 ± 66.072 | 628.269 ± 92.467 | 527.416 ± 18.099 |
| Gloin after | 478.548 ± 38.331 | 575.113 ± 75.289 | 516.787 ± 42.293 |
| C, bounds + finite checks | 536.855 ± 59.922 | 547.076 ± 71.437 | 546.631 ± 59.892 |
| C, bounds checks only | 268.756 ± 32.027 | 282.156 ± 19.629 | 381.417 ± 46.688 |
| C, finite checks only | 464.342 ± 39.052 | 483.264 ± 43.313 | 464.701 ± 18.555 |
| C, neither check | 271.001 ± 32.494 | 283.546 ± 5.545 | 391.285 ± 29.023 |

Gloin's kernel medians are 11.7%, 8.5% and 2.0% lower, with overlapping observed
variation. The isolated fast copy removes both bounds assertions and the
counter-overflow assertion; this is not a measurement of a single bounds
branch's cost. The C controls separate the policies, but their changes also
affect vectorization and scheduling. The unchecked controls cannot establish
a C/Gloin language ranking. All **72 size checks and 162 timed executions**
passed, including bitwise output checks.

The original application has mixed whole-process wall times:

| Size | Before, ms ± MAD | After, ms ± MAD |
| --- | ---: | ---: |
| 256 | 39.619 ± 1.054 | 43.846 ± 2.888 |
| 512 | 268.014 ± 9.906 | 273.136 ± 9.522 |
| 768 | 891.446 ± 20.010 | 873.001 ± 14.733 |

All 54 executions passed. A separate longer-batch confirmation used the exact
same saved executables, two warmups and eleven measured samples, batching
16/4/1 invocations at sizes 256/512/768. Per-invocation wall medians were
55.336 → 64.536 ms, 350.115 → 366.088 ms, and 1,197.010 → 1,154.417 ms.
All 546 executions passed. The smallest case is slower by median in both
batches; the overall application results do not establish a consistent speedup.
The confirmation also has very wide ranges (for example, 40.679–68.871 ms before
and 40.874–76.338 ms after at size 256). Background desktop activity continued;
the host was neither isolated nor CPU-pinned. No task builds/tests or profilers
ran during the timing batches. Both initial and confirmation evidence are kept.

A final comparison measured the **same saved executables** with `wait4` child
user + system CPU time as well as monotonic wall time. Two warmups/seven measured
samples used the same 16/4/1 invocation batches; all **378 executions passed**.
Per-invocation CPU times were:

| Size | Before, ms ± MAD | After, ms ± MAD |
| --- | ---: | ---: |
| 256 | 51.399 ± 1.583 | 46.532 ± 4.631 |
| 512 | 279.824 ± 15.806 | 273.514 ± 12.718 |
| 768 | 941.971 ± 70.877 | 936.761 ± 42.482 |

These medians are 9.5%, 2.3% and 0.6% lower. That batch's wall medians also
favor the change, unlike the earlier small-size results. CPU time excludes
descheduled time but still reflects frequency/cache contention; it does not
make this desktop an isolated benchmark host. **The supported conclusion is a
verified reduction in guards with modest, noisy performance changes, not a
uniform application speedup or a demonstrated universal absence of regressions.**
Retain all three application batches. A more precise application performance
claim needs a quiet, controlled host; do not select only the favorable batch.

The isolated dot function grows from **96 to 176 bytes** of machine instructions
because the fallback remains; its object `__TEXT` total grows from 212 to 292
bytes. Entry-predicate overhead and code growth are explicit trade-offs, and
short loops are not guaranteed to improve.

### Verification

- Final macOS `check-core`: **1,025 tests passed**, 352.00 s. The first run
  passed the other 1,024 tests but timed out the existing 584-child arithmetic
  matrix at 30 s under concurrent validation load. Its aggregate timeout is now
  360 s on macOS as on Linux/sanitizer builds; each child retains its 10 s
  deadline. The final matrix completed in 26.30 s.
- Linux ARM64/LLVM 21.1.8: **339 selected tests passed**, covering lowering,
  JIT, operators/numerics, loops/control flow, pointers and core acceptance.
  After the final refinement to inherit existing proofs in cloned loops,
  **all eight affected GIR tests passed again** (61.51 s).
- macOS ASan/UBSan: **114 selected tests passed** before that refinement;
  **all eight affected tests passed again** afterward (150.26 s). No sanitizer
  findings. These are two validation batches, not 122 distinct tests.
- New structural tests verify signed/unsigned loop copies, retained finite
  checks without fast-math, inherited unsigned proofs, and rejection of escaped
  counters, negative starts, non-unit strides and descriptors reloaded inside
  the loop. Existing mutation/join/alias proof regressions remain in the gates.
- Twelve runtime cases run in JIT, O0 and O2: valid/short/unequal/empty slices,
  zero/negative counts, non-finite products and intermediate sums, cancellation
  order and a result that would change under FMA contraction. Printed iteration
  markers check observable failure ordering, including a huge signed limit.
  The initial test harness reused redirect files without truncation; deleting
  those files between native cases fixed stale output tails, with no compiler
  change needed for that issue.
- The Gloin benchmark source passes `gloinfmt --check`; both runners pass shell
  syntax checks. Documentation link checks and `git diff --check` pass.

Before compiler SHA-256:
`6eedc4ba1f512268da004973ad0bf8ce2eaef419de8a1797e3250c8345de44ae`.
Final measured compiler SHA-256:
`51a759bd54b87d289482ca30d5421871e1037ed851b45aa7c4e116358628ed88`.
Both report the development tree's unchanged version string; this work is
unreleased.

## PERF-008 exact widening and interpreter investigation — 2026-10-12

The baseline is the local compiler after PERF-007 (SHA-256
`51a759bd54b87d289482ca30d5421871e1037ed851b45aa7c4e116358628ed88`),
not the published 0.1.0 binary. Both variants used matching runtime and
standard-library snapshots and compiled the unchanged Brainfuck program at
native `-O2` for the host CPU. No instruction folding or interpreter algorithm
changes are included.

### Implementation and semantic boundary

Sema's checked primitive identity selects `__std_i64_from_i32(i32, &i64)`.
Codegen emits pure `gloin.exact_int_widen`, a typed GIR store to its output
reference and status `OK`. The verifier permits only supported scalar signless
integer widths with a strictly wider result, carrying source signedness as an
explicit attribute. Lowering uses sign/zero extension; the current source API
emits the signed i32-to-i64 case. No name-based substitution, implicit cast,
new public conversion or result API migration is involved.

At this stage, `std.i64_from_i32` returned concrete `std.I64Result { status, value }`.
The later [0.2.0 API migration](numbers.md#migration-from-010) changes its public
return to plain i64 while keeping this GloinIR lowering.
All i32 inputs are representable, including the negative minimum. Narrowing,
sign-changing and floating/rounded conversions retain their runtime checks.
The old runtime symbol remains implemented for existing ABI consumers; it may
still appear in a linked executable because other numeric routines use the
same object file. The acceptance criterion is removal of the conversion **call**,
not disappearance of that symbol from the static runtime archive.

In the M2 interpreter move path, `ldrsw` now loads and sign-extends `op.amount`
before the checked i64 addition. The runtime call and output-slot round trip
are gone. `evaluate` occupies 480 bytes instead of 512 and its stack frame is
96 bytes instead of 112. Checked cell addition remains `adds`/`b.vs`; signed
tape-index, length and nullable-pointer guards remain. Four instruction kinds
still use conditional dispatch and nested loops still recurse into `evaluate`.

### Measurement and profiling method

Two warmup rounds precede seven measured rounds; before/after order alternates. Every run
must exit successfully and match the historical checksum: 23280 for `bench.b`,
11962 for `mandel.b`.

Child user+system CPU time (`wait4`) and monotonic wall time measure the whole
process: startup, the built-in Hello World check, input reads, parsing, execution,
cleanup and output. `QUIET=1`; `BENCHMARK_PID` is unset. This is a different
window from the older TCP-notification suite. Do not pool its absolute times
with the historical table or infer a new C/Gloin ratio. The profiling runs are
separate from measurements. No task builds/tests run during either collection;
the desktop remains shared with the user's processes.

### Results and remaining work

M2/macOS 15.1.1, LLVM 21.1.6; seconds, median ± median absolute deviation,
seven measured samples per cell:

| Workload | Before CPU | After CPU | CPU reduction | Before wall | After wall |
| --- | ---: | ---: | ---: | ---: | ---: |
| `bench.b` | 1.824435 ± 0.038534 | 1.671774 ± 0.057197 | 8.37% (1.09×) | 1.826429 ± 0.039363 | 1.674627 ± 0.072486 |
| `mandel.b` | 23.249015 ± 0.024000 | 15.892182 ± 0.104884 | 31.64% (1.46×) | 23.284948 ± 0.061082 | 15.916048 ± 0.087980 |

Wall-time reductions are 8.31% and 31.65%. All **36 runs**, including warmups,
match their expected checksums. The same compiler/runtime configuration was used
for both inputs; the larger `mandel.b` benefit is workload dependent. Seven
samples on a shared desktop support this local comparison, not a universal
speedup or a regression-free claim for unrelated programs. No new C/Go timing
comparison was run.

Separate macOS `sample` profiles use a 1 ms interval, for one second on `bench.b`
and five seconds on `mandel.b`. The old runtime widening helper is the leaf in
8/865 samples (0.9%) for `bench.b` and 579/4316 (13.4%) for `mandel.b`. In the
updated programs all captured leaf samples are inside `evaluate`: 869/869 and
4317/4317. Each profiled process also exits successfully with the same checksum.
These short profiles cover different numbers of interpreted instructions; they
are hotspot evidence, not an isolated cost estimate or a full-run percentage.
Call/return, register spills and output-slot traffic can be charged to the caller
rather than to the tiny runtime helper, so its sampled percentage cannot predict
the full measured speedup.

Assembly identifies the remaining work within `evaluate`:

- Instruction fetch, index progression and conditional dispatch on four kinds.
  Native LLVM already arranges the comparisons; an eventual source switch must
  be measured against this code rather than assumed to imply a faster jump table.
- Cell increments retain signed `adds`/`b.vs`. Generic i32 cells are not proved
  to stay within range; removing that overflow check would need a valid proof.
- Tape access retains negative-position and length comparisons, backing-storage
  and element-pointer null checks. Loop bodies may grow the vector and change
  tape position, so recursive returns reload these values.
- Recursive interpretation remains a call; no instruction folding, wrapping
  cells, hoisted mutable descriptor checks or changed dispatch syntax was added.

Sampling collapses these instructions into `evaluate`; it does **not** resolve
separate costs for checked increments, nullable access and dispatch. Finer
instruction/branch profiling and controlled comparisons are needed before
selecting another transformation. The present task closes the widening change
and initial interpreter investigation; general dispatch work awaits switch/match
design, and wider alias/range proofs remain separate work.

The updated compiler SHA-256 is
`7c72791e891566e8d230dcef4ad7cd82f258595182f4405441b835533344b71d`;
the static runtime is identical in both variants.

### Validation

- macOS `check-core`: **1,028 tests passed**, 101.05 s.
- Linux ARM64/LLVM 21.1.8: **83 focused tests passed**, covering lowering,
  numeric APIs and GIR execution regressions, 47.31 s.
- macOS ASan/UBSan: **83 focused tests passed**, 41.56 s, with no sanitizer
  findings.
- Dynamic parsed inputs exercise i32 minimum/maximum, negative values, zero
  and positive values in JIT and native `-O0`/`-O2`, checking status and payload.
  Narrowing overflow and exact/rounded floating conversions retain their rules.
- GIR tests verify every supported widening-width pair and sign/zero extension,
  with malformed/same-width/narrowing/bool/float/vector cases rejected. Private
  primitive signature checks, forwarded output references, replacement standard
  wrappers and user function names are covered. Inspection confirms the emitted
  module no longer declares/calls the widening runtime function.

## PERF-002 implementation and validation — 2026-10-08

Indexed reads now mark their owning runtime binding as needing storage, including
immutable locals, by-value parameters and nested ordinary struct fields. Existing
code generation initializes the binding once and emits typed GIR element addresses
and loads for subsequent reads. It does not grant write access, turn value copies
into aliases or cache returned temporaries. Loop-local initializers still execute
on each iteration. See [the storage contract](lowering.md).

The minimal GIR regression found two aggregate copies inside each local/parameter
lookup loop, and four for nested field lookups. After the fix, each function has
one aggregate slot and one initialization before its loop. In the Base64 encoder,
GIR now has one alphabet slot/store instead of seven (four main-loop lookups and
three tail lookups). Optimized arm64 assembly has four paired stores before the
loop to initialize 64 bytes, with no alphabet stores inside the loop.

Same-source Base64 comparison on Apple M2/macOS 15.1.1, LLVM/MLIR 21.1.6,
all variants `-O2` with host CPU selection:

| Compiler / source | Total seconds, median ± MAD | Encode seconds, median | Decode seconds, median |
| --- | ---: | ---: | ---: |
| Released 0.1.0 / direct array | 2.719464 ± 0.003517 | 1.376522 | 1.340464 |
| Working compiler / same direct array | 1.930241 ± 0.005256 | 0.588479 | 1.342014 |
| Working compiler / borrowed-slice control | 1.927314 ± 0.001495 | 0.587905 | 1.340054 |

Ten measured runs followed one discarded warm-up per variant, in seeded shuffled
order, using the existing benchmark TCP timing boundaries and correctness checks.
All 30 measured runs passed. Compilation, setup and startup are outside timing;
no builds or tests ran concurrently. The machine was not CPU-pinned or isolated.
Encoding is **2.34× faster**, total time is **29.0% lower**, and direct indexing
matches the slice control within observed variation. Decoding is essentially
unchanged. These are measurements of this kernel on macOS, not a general speedup
or a new C/Go comparison.

Validation: macOS `check-core` passed **989 tests**; Linux ARM64/LLVM 21.1.8
passed **275 focused tests** across lowering, arrays/core acceptance, pointers,
structs, generics and results; macOS ASan/UBSan passed **27 focused tests**.
The array fixture exercises copies, aliases, nested fields, per-iteration
initialization and temporary evaluation order in JIT and native `-O0`/`-O2`.
Negative, out-of-range and empty-array indices still trap in all three modes.
The changed Gloin fixture also passes `gloinfmt --check`.

## PERF-009 implementation and validation — 2026-10-08

Both reported failures were reproduced before changing the compiler:

- Extracting a proven `result<&i32>` payload directly into `*const i32` failed
  GIR verification: `gloin.result_value` had the contextual destination type
  instead of the result's actual payload type.
- Loading a `[i32]` row directly into `[const i32]` failed code generation with
  `Checked load cannot change the stored value's source type`. That path handled
  pointer weakening but omitted slice weakening.

Extraction now produces the declared payload type, and indexed loads retain
their storage type. The existing verified GIR source/layout bridges apply the
permitted conversion afterward. Code generation and dialect verification share
the same outer-capability predicate. The bridges disappear during lowering;
this adds no runtime cast, buffer copy or new language conversion. It also fixes
the corresponding direct conversion of `result<[T]>.value` to `[const T]`.

The [acceptance program](../tests/fixtures/core/run/capability_conversions/main.gloin)
and its generic module cover direct and intermediate bindings, arguments,
returns, field initializers, assignments, array/slice row loads and pointer
dereferences. Mutations through writable aliases remain visible through the
converted views. Source regressions still reject unproven/error-path result
access, restored write access, nullable-to-non-null conversion, writes through
read-only slices and nested pointee/element conversion. A generic imported
function cannot bypass those checks. Six forged GIR bridge cases also reject
strengthening and unrelated types with identical layouts.

Validation: **995 macOS core tests**, **281 focused Linux ARM64 tests**, and
**63 macOS ASan/UBSan tests** passed. The positive module program runs in JIT
and native `-O0`/`-O2` on both platforms. New fixtures pass `gloinfmt --check`.
The real matrix benchmark, changed locally to load `p` and `r` directly as
`[const f64]`, compiles and matches the original output at sizes 2, 32 and 101
under `-O2`; published 0.1.0 rejects that same source.
This was a correctness comparison, not a performance measurement.

These fixes remain unpublished. The [result guide](results.md),
[slice guide](slices-vectors.md) and [GIR guide](lowering.md) distinguish the
working compiler from the intermediate-binding workaround needed in 0.1.0.

## Strided encoder loop versioning — 2026-10-18

This extends the GIR loop versioning introduced in PERF-007. Inspection of a
scalar Base64 encoder found four destination-bounds branches in each three-byte
input/four-byte output iteration. The input and alphabet accesses were already
proven safe in optimized native code. Division/remainder became shifts and
masks, with no calls or allocations inside the loop. Adding the four output
checks to the matched C encoder brought its timing close to the original Gloin
encoder; that control motivated this compiler change.

The encoder tests `i + 3 <= source.len`, increments `i` by three and `j` by four,
and uses both counters in its padded tail. The earlier unit-stride recognizer
could not version it. GIR now recognizes bounded unsigned complete-group loops
and constructs a guarded copy. The overflow-safe capacity predicate for this
case is `source.len / 3 <= output.len / 4`; a separate proof requires that the
final header addition remain representable. A failed predicate selects the
original checked loop, preserving partial writes and the order of any later
failure. Counter values from either path join before the tail, whose checks
remain. [The lowering guide](lowering.md#strided-groups-and-live-tail-counters-2026-10-18)
defines the precise recognition limits and arithmetic proof.

This changes compiler lowering, with identical benchmark source and runtime
before and after. Buffer aliasing, checked arithmetic and floating checks keep
their existing semantics. The alphabet table is still initialized once per
call on the stack. The fast path applies in the shared JIT/native/object
pipeline; native O2 subsequently simplifies the proven operations.

### Measurement

Apple M2, macOS 15.1.1, LLVM/MLIR and Clang 21.1.6, native O2 with host CPU
selection. Each sample encodes 128 KiB 8,192 times (1 GiB of input), then
decodes it with the existing grouped decoder. ASCII repeats `a`; binary cycles
through all 256 byte values. Initialization, byte-for-byte round-trip validation
and accumulated-length checks are outside the timed phases. Two warmup and six
measured rounds per implementation/pattern run in rotated and reversed order.
All 48 runs passed. No builds, tests or profilers ran during timing; the desktop
remained shared and unpinned. All samples, including slower ones, are retained.

Encoding seconds, **median ± median absolute deviation**:

| Implementation | ASCII | Binary | Full-group instructions / conditional branches |
| --- | ---: | ---: | ---: |
| Gloin before | 0.499394 ± 0.001091 | 0.501755 ± 0.000575 | 36 / 5 |
| Gloin after | 0.360247 ± 0.001488 | 0.373298 ± 0.002335 | 24 / 1 |
| Matched scalar C | 0.362230 ± 0.000836 | 0.371424 ± 0.000807 | 23 / 1 |

The compiler change reduces encoding time by **27.9% on ASCII and 25.6% on
binary** (1.39× and 1.34× speedups). Updated Gloin and C medians differ by about
0.5% in opposite directions; their observed ranges overlap. This batch supports
comparable encoding performance for this kernel, not a ranking at that margin
or a general claim about either language. C relies on its caller supplying
enough output storage; Gloin retains the entry guard and checked fallback.
The unchanged grouped decoder remains around 0.47 seconds in Gloin and
0.45 seconds in C. Its before/after Gloin medians move by 0.1–0.4% here.

The fast encoder's only conditional branch is its backedge. The four output
bounds branches and associated index work are gone. Its extra instruction
relative to C is a separate destination-pointer increment: C combines the
last byte store with a post-indexed update. This is an instruction observation,
not an isolated cycle-cost measurement. Both loops remain scalar. Keeping the
fallback increases the entire Gloin encoder from 512 to 672 bytes (+31%);
guard overhead and short-input performance have not been separately measured.

### Validation

- macOS `check-core`: **1,067 tests passed**; CTest discovers **1,114** overall.
- Linux ARM64, LLVM/MLIR 21.1.8: **107 focused tests passed**, covering all
  lowering/CLI cases and the Base64 acceptance fixture.
- macOS ASan/UBSan: **79 focused tests passed**, with leak detection disabled
  for the LLVM embedding, covering lowering, GIR runtime cases and Base64.
- Four new regression tests verify fast/fallback structure, checked float
  operations, tail SSA joins, rejected induction/descriptor patterns, exact and
  spare capacity, sentinels, aliasing and tails. Runtime cases check every short
  capacity across two groups, unrelated arithmetic overflow, final-header
  overflow and extreme strides/limits in JIT, native O0 and native O2.
- The saved compiler passes those same two new runtime tests with identical
  outputs and failure traces. The optimization is not required for their
  expected behavior.

The compiler/runtime snapshots, sources, assembly, raw samples and hashes are
retained with the local measurement artifacts. The runtime archives and standard
library are identical before/after. The initial hash-capture command failed on
an unsupported host locale; the timing runner explicitly used `LC_ALL=C` and
completed normally. A hash checkpoint after round two matches the final hashes;
the baseline binaries and sources also match the earlier retained investigation.
This metadata failure did not discard or replace any timing sample.

## Native call generation — 2026-10-18

The recursive interpreter investigation found direct calls in GloinIR and the
LLVM dialect becoming register-indirect calls in the native executable. Native
emission used `JITTargetMachineBuilder::createTargetMachine`, which passes
`JIT=true` and selects the large code model on the supported 64-bit targets.

Native emission now creates an ordinary AoT target machine from the detected
host CPU/features, with default target options and `JIT=false`. Linux keeps
explicit PIC relocation. JIT execution and the shared GloinIR-to-LLVM pipeline
are unchanged. [The lowering guide](lowering.md#native-target-configuration-2026-10-18)
records the target defaults and address-range boundary.

In the unchanged recursive interpreter, all three calls in `evaluate` become
direct calls. The function shrinks from 120 to 110 AArch64 instructions
(480 to 440 bytes), and its stack frame falls from 96 to 64 bytes. The saved
GloinIR and LLVM-dialect inspection outputs compare byte-for-byte equal.
Bounds, null and arithmetic checks remain. These static differences do not
assign a cycle cost to an individual call or predict other workloads.

The CLI regression checks direct external runtime-call relocations in native
objects at `-O0` and `-O2`, and checks recursive execution/output in JIT and both
native modes. The saved pre-change compiler fails the relocation assertions at
both levels while still computing the correct output; the updated compiler
passes. The ordinary function calls already carry their target symbol in GIR,
so this correction belongs to native target setup rather than a new GIR pass.

### Measurement and validation

The unchanged recursive interpreter's short workload was measured on Apple M2,
macOS 15.1.1, LLVM/Clang 21.1.6. Gloin uses native `-O2`; C uses
`-O2 -mcpu=native`. Frozen before/after compilers use the same frozen standard
library and interpreter source. This batch does not include the separate
cached-cell source experiment.

| Variant | Median whole-process CPU | CPU median absolute deviation | Median elapsed |
| --- | ---: | ---: | ---: |
| Previous native target setup | 2,308.084 ms | 0.625 ms | 2,309.521 ms |
| Ordinary AoT target setup | 2,289.152 ms | 1.417 ms | 2,290.720 ms |
| C reference | 1,899.704 ms | 5.861 ms | 1,900.893 ms |

Each variant has two warmups and six measured runs, with all six run-order
permutations represented. A separate C helper checks exit status and exact
checksum 23280 while measuring child user+system CPU and elapsed time. All 24
runs passed; tracked compiler/runtime, source, library, input and executable
hashes were unchanged. Builds, tests and profiling were absent during timing.
Desktop background activity remained.

The median CPU reduction is **0.82%**. All six paired rounds favor the change;
five reductions lie between 0.66% and 0.90%, while one slower baseline run
produces a 3.12% reduction. Every observation is retained. Absolute CPU times
are substantially higher than the preceding session for both Gloin and C,
so only comparisons within this batch are used. This is a small workload-local
improvement, not evidence of a general speedup. Updated Gloin still takes about
20.5% more CPU time than this C implementation, whose safety and cell-access
contracts differ. Repeated tape checks remain a separate source/compiler
investigation; this result does not attribute their exact cost.

Validation passed **1,068 macOS core tests**, **124 Linux ARM64 focused tests**
(CLI, lowering and JIT), and **50 macOS ASan/UBSan tests** (CLI and JIT). The
sanitizer run disables leak detection; generated Gloin code is not itself
sanitizer-instrumented. Linux uses LLVM 21.1.8. Linux x86-64 execution was not
tested in this session.

Separately emitted O0/O2 objects link and execute with the ordinary system
compiler on both tested platforms; Linux outputs are PIE executables. An
independent small-program oracle passes **117 benchmark checks** across
before/after/C, including invalid-input rejection in both Gloin builds. The
HTML local-link checker passes all seven pages. The final release/package
audit remains pending.

## Matrix floating checks: investigation (2026-10-18)

This investigation uses the current compiler without changing its implementation.
The workload is an ascending-index `f64` dot product inside matrix multiplication,
with the second matrix transposed. The existing GIR bounds proof already removes
bounds checks from the fast loop. Native assembly still has scalar multiplication,
ordered addition, and finite-result checks after both operations. LLVM's
vectorizer diagnostics identify the early exits as an obstacle to vectorization.

The experiment uses one separately compiled C driver for Gloin and seven C
controls, with no LTO. Initialization, allocation and a bitwise reference check of
every output cell are outside the timer. Gloin uses native `-O2`; C uses LLVM
21.1.6 with `-O2 -mcpu=native -ffp-contract=off`, without fast-math. The host is
an Apple M2 running macOS 15.1.1. CPU time is measured around the matrix kernel;
elapsed time is retained separately. Each sample performs 268,435,456 products:
128 repetitions at size 128, 16 at size 256, or two at size 512.

### Checks after the reduction

One C prototype first checks that the complete iteration range fits both slices.
For that path it keeps every multiplication and addition, in the original order
with separate rounding, and checks that the final accumulator is finite. If the
range does not fit, it executes the original loop with bounds and arithmetic
checks in their original sequence. Zero and negative counts return positive zero.
This prototype is also compiled with vectorization disabled to distinguish its
scalar result from LLVM's SIMD version.

The numerical argument is specific to this reduction: a non-finite product makes
the running sum non-finite, and subsequent additions cannot make it finite again.
With a zero initial accumulator, a finite final sum therefore implies that every
product and intermediate sum was finite. This does **not** authorize moving
checks across arbitrary code. A GIR implementation would also have to prove
that all intervening reads are valid, the counter cannot overflow, no effects or
other failures are crossed, and intermediate accumulator values cannot escape.
The final check must remain even if the computed value is unused. Failure is
detected later on this path; retaining the identity/location of the first failed
operation would need additional handling. Operations that can discard a
non-finite value or turn one into a finite value do not qualify.

The resulting AArch64 fast loop multiplies two lanes at a time, with four groups
per iteration, then adds the eight products to the scalar accumulator in their
original order. There is no fused multiply-add or reassociation. LLVM documents
support for such [ordered reductions on AArch64](https://llvm.org/docs/Vectorizers.html#reductions).
The scalar control retains separate scalar multiplication and addition, with the
finite check outside the loop.

Seven variants each have two warmups and seven measured samples per size, with
balanced run positions. Median kernel CPU times and median absolute deviations,
in milliseconds:

| Variant | Size 128 | Size 256 | Size 512 |
| --- | ---: | ---: | ---: |
| Current Gloin | 373.371 ± 0.076 | 399.067 ± 0.098 | 412.027 ± 0.051 |
| C, bounds and finite checks | 392.985 ± 0.098 | 400.593 ± 0.142 | 412.997 ± 0.071 |
| C, bounds checks only | 222.872 ± 0.150 | 232.728 ± 0.046 | 319.861 ± 0.110 |
| C, finite checks only | 382.270 ± 0.105 | 399.752 ± 0.102 | 413.450 ± 0.075 |
| C, neither check | 221.232 ± 0.105 | 232.931 ± 0.344 | 319.690 ± 0.155 |
| C prototype, final check, scalar | 267.061 ± 0.164 | 322.538 ± 0.154 | 366.286 ± 0.180 |
| C prototype, final check, SIMD | 226.427 ± 0.101 | 234.515 ± 0.298 | 320.403 ± 0.092 |

The SIMD prototype uses **39.4%, 41.2% and 22.2% less CPU time** than current
Gloin, and is 2.35%, 0.68% and 0.22% above C without either check. Every paired
round favors the SIMD prototype. Disabling vectorization increases prototype
time: enabling it saves a further 12.5–27.3% relative to that scalar control.
These are measurements of C prototypes, not improvements implemented in Gloin.
Controls that omit checks entirely have weaker contracts.

### Combining checks within each iteration

A smaller prototype removes only the product's finite check, retaining the sum's
check before the next iteration. A non-finite product necessarily fails that
sum check. A corresponding GIR transformation would require the product to feed
the checked addition without intervening observable work or another possible
failure. It would retain separate rounding and the addition's mandatory check.

This remains scalar. In a separate batch with two warmups and six measurements
per variant and size, current Gloin takes 373.443, 399.083 and 412.008 ms; the
prototype takes 354.508, 383.273 and 397.567 ms. Reductions are **5.1%, 4.0% and
3.5%**, favorable in all 18 paired comparisons. The final-check SIMD prototype
is included again as a contemporaneous control and reproduces the larger gain.
The two batches' raw samples and summaries are kept separately.

### Validation and scope

All **261 timed executions** and **96 additional matrix size checks** pass
cell-by-cell bitwise verification. Current Gloin, C with both checks, and the
three prototypes each pass **20,299 independent bitwise cases and 20 expected
traps**. Cases include deterministic random exponents and mantissas, signed
zeros, subnormals, rounding-sensitive cancellation, a contraction-sensitive
example, unequal lengths, zero/negative counts, aliased inputs, protected-page
tails, product/sum overflow, infinities, quiet/signaling NaNs and insufficient
capacity. Trap tests distinguish an arithmetic/bounds trap from an invalid
memory access. They validate the tested cases, not a general compiler proof.

Compiler, runtime, source, object and timed executable hashes remain unchanged
through sampling. No task builds, tests or profiling run during timing. Desktop
background activity remains; all outliers are retained. For example, the scalar
prototype has one size-512 sample slower than Gloin. CPU time excludes
descheduling but not changes in frequency or cache contention.

The evidence supports a bounded GIR optimization for checked floating reductions,
with combining adjacent product/sum checks as a smaller first step. A compiler
implementation still needs explicit proof and rejection rules, retained checked
fallbacks, and regression coverage for observable effects and failure order.
This experiment does not measure end-to-end application speed or establish
results for `f32`, Linux or x86-64.

## Adjacent floating checks — 2026-10-19

The smaller transformation investigated above is now implemented in GIR for
`f32` and `f64`. A checked multiplication must immediately precede a checked
addition in the same block, and its product must have exactly one use, in that
addition. A non-finite product necessarily makes the sum non-finite. The product
therefore becomes an ordinary multiply while the addition retains its mandatory
finite check, including when the sum is discarded. Separate rounding and operand
order are preserved; no fast-math, FMA contraction or reassociation is introduced.

No intervening operation is allowed, including a load, call, other check or even
a constant. The rule makes no pointer-validity, lifetime or aliasing assumption.
It applies inside eligible fast and fallback loops, with a check remaining in
each iteration before the next iteration's reads. At the mixed GIR/LLVM boundary,
the other addend is frozen so that LLVM poison cannot suppress a product failure.
Concrete values are unchanged. Current arithmetic failures share one fatal trap;
distinguishing the first failed operation in future diagnostics would require
additional handling. The [lowering guide](lowering.md#adjacent-floating-checks-2026-10-19)
records the exact acceptance and rejection rules.

### Measurement

The unchanged matrix dot-product source is compiled before and after the change
on Apple M2/macOS with LLVM 21.1.6. Both versions and two C controls link the same
separate-translation-unit driver. Gloin uses native O2; C uses
`-O2 -mcpu=native -ffp-contract=off`, without fast-math or LTO. Initialization,
transposing B, reference calculation and cell-by-cell verification are outside
the timer. Sizes 128/256/512 use 128/16/2 repetitions, respectively, giving
268,435,456 products per sample.

Each variant and size has two warmups and eight measured samples, with balanced
run positions. Median kernel CPU times and median absolute deviations, in
milliseconds:

| Variant | Size 128 | Size 256 | Size 512 |
| --- | ---: | ---: | ---: |
| Gloin before | 373.301 ± 0.046 | 399.047 ± 0.045 | 411.946 ± 0.036 |
| Gloin after | 354.635 ± 0.066 | 383.375 ± 0.046 | 397.562 ± 0.024 |
| C, bounds and finite checks | 393.017 ± 0.065 | 400.493 ± 0.133 | 413.001 ± 0.069 |
| C, neither check | 221.234 ± 0.085 | 232.562 ± 0.042 | 319.611 ± 0.033 |

Gloin uses **5.0%, 3.9% and 3.5% less kernel CPU time**, respectively. Every one
of the 24 paired before/after comparisons improves. Wall medians agree closely:
373.338/399.096/411.984 ms before, and 354.646/383.386/397.607 ms after. All
120 timed executions and 48 additional matrix size checks pass bitwise output
verification. The C control without checks has a weaker safety contract.

The complete dot function shrinks from **176 to 160 bytes**, including the
checked fallback. Its fast inner loop goes from eleven to nine instructions;
the product's comparison disappears and the sum's comparison remains. The
freeze adds no native instruction in that loop. The arithmetic remains scalar.

Compiler, runtime, source, object and timed executable hashes are unchanged
through sampling. No task builds, tests or profiling run during timing. Desktop
background activity remains, and no samples are excluded. CPU time excludes
descheduling, but not frequency or cache contention. These are isolated kernel
measurements on this machine, not end-to-end application results or performance
measurements for `f32`, Linux or x86-64.

### Validation

- All **1,072 supported macOS core tests** pass. Coverage is audited against
  CTest: 931 compiler cases pass in the unfiltered compiler run, followed by all
  141 remaining runtime, tooling and acceptance cases. The unfiltered run has
  978 tests overall, with 974 passing and the four documented deferred async/spawn
  failures outside the supported core set.
- **128 focused Linux ARM64 tests** pass with LLVM 21.1.8, covering lowering,
  CLI and JIT execution. Linux x86-64 was not executed in this validation.
- **80 macOS ASan/UBSan tests** pass. Compiler and test code are instrumented;
  generated Gloin machine code is not. Leak detection is disabled.
- Structural regressions cover both widths and operand positions, discarded
  sums, idempotence, fourteen refusal cases per width, and poison/undef addends.
  The latter retain the mandatory product trap after LLVM O2 optimization.
  Runtime cases cover separate rounding, product/sum overflow, discarded
  results, intervening effects and conditional/division consumers in JIT,
  native O0 and O2.
- An independent C oracle checks **835,168 calls** across before/after compilers
  at O0/O2: 52,198 triples per width, both returned and discarded. Finite results
  agree bitwise with separately rounded multiplication/addition, and invalid
  results trap even when discarded. Inputs include deterministic random raw
  bits, signed zeros, subnormals, extreme finite values, infinities, quiet and
  signaling NaNs, and contraction-sensitive cases.
- A separate dot-product oracle passes **20,299 bitwise cases and 20 expected
  traps per compiler**, including aliasing and protected-page tails. All seven
  HTML pages and their local links pass the Gloin documentation checker.

This adjacent-pair change did not implement whole-loop deferral. The next
milestone below establishes the stronger storage and failure-order conditions
for a bounded set of local reductions.

## Local floating reductions — 2026-10-19

GIR now versions ordered `f32`/`f64` dot products over initialized local fixed
arrays and their borrowed views. A range predicate selects a fast loop with one
mandatory finite check at its exit, or the original checked loop. Products round
separately and additions retain their source order. The final check remains even
when the result is unused. The adjacent-pair rule still applies to the fallback.

The proof requires a zero-initialized private accumulator, a terminating unit
counter, valid reads throughout the selected range, and no crossed effects or
other failures. Storage must trace to a matching local allocation and dominating
whole initialization. Address and descriptor uses are audited; escaped pointers,
calls with borrowed storage, missing/conditional initialization, stack restoration,
unsupported layouts and unrepresentable extents prevent deferral. Read-only slice
types alone supply no lifetime proof. Full rules and bounds are in the
[lowering guide](lowering.md#local-floating-reductions-2026-10-19).

The existing generic borrowed-slice dot function **does not qualify**. Its O2
native object is byte-for-byte identical before and after this change. The
transformation does not infer allocation validity from descriptor lengths or
change the slice ABI. Arena/vector storage and interprocedural lifetime proofs
remain outside this milestone.

### Validation

The full **1,078-test macOS core suite** passes. Subsequent extent, layout and
stack-lifetime rejection hardening is covered by **89 focused tests on the final
macOS compiler**, including all lowering tests and the GIR/JIT execution matrices.
Structural tests cover both widths, arrays and slices, used/discarded results,
the original fallback, and rejection of missing storage/effect/failure facts.
Optimized mixed GIR/LLVM tests require an unconditional trap after early overflow
even when a later input is poison or undef.

**89 focused Linux ARM64 tests** pass with LLVM 21.1.8; three proof/rejection
tests are rerun successfully after the final stack-restoration guard. **89 macOS
ASan/UBSan tests** pass on the final compiler with LLVM 21.1.6. Leak detection is
disabled; compiler and test code are instrumented, generated Gloin code is not.
All seven HTML pages and their local links pass the Gloin documentation checker.
Linux x86-64 execution was not tested in this milestone.

An independent C reference checks **20,299 bitwise cases and 20 expected traps**
per width, compiler and optimization level: **162,392 finite cases and 160 traps**
across before/after, f32/f64 and O0/O2. These cover random exponents/mantissas,
signed zeros, subnormals, extreme values, NaNs/infinities, cancellation,
contraction-sensitive inputs, empty/negative counts, aliases, short descriptors
and protected-page tails. A separate borrowed-slice test requires early overflow
to trap before a later protected-page read; a finite-input control confirms the
memory fault is otherwise reached. All four before/after O0/O2 builds agree.
The final compiler emits byte-identical validation objects after proof hardening.

The measured local-storage program copies inputs into fixed arrays before its
reduction. Those copies are explicit source operations in both compiler builds;
they are not inserted by this optimization. Its correctness and timing results
must be kept separate from the generic-slice benchmark above.

### Measurement

Two separate fixtures use the same matrix values, ascending-index products/adds
and independent cell-by-cell reference. Apple M2/macOS uses LLVM 21.1.6, native
Gloin O2 and C `-O2 -mcpu=native -ffp-contract=off`, without fast-math or LTO.
Each variant links a common separate-translation-unit C driver. Input generation,
transposition and reference/output verification are outside the timer; local
array initialization, copying and calculation are inside it.

Each fixture has five variants: Gloin before/after, C checking products and sums,
C checking the final sum, and C omitting bounds and finite checks. The last has
a weaker failure contract. Each size/variant has two warmups and ten measured
samples with balanced run positions. Sizes 128/256/512 use 128/16/2 repetitions,
giving 268,435,456 products per sample. CPU and monotonic wall time are recorded.
All **360 timed executions** and **125 additional size/variant checks** pass
bitwise verification across the two fixtures.

The first fixture copies two slices into local arrays of capacity 1,024 for
**every dot product**. Initialization and copying add substantial work. Before
CPU medians are 1,122.482/858.957/675.932 ms; after medians are
1,094.237/834.284/668.742 ms. Reductions are **2.5%, 2.9% and 1.1%**; 29 of 30
paired comparisons improve. One size-128 pair regresses by 1.9% and is retained.
This does not establish a benefit from adding copies to an existing program.

The second fixture owns two `[[f64; 512]; 512]` matrices for the entire
multiplication. Each invocation initializes and copies them **once**, then
borrows their rows for every dot product. Both compiler versions compile the
same source; C uses the same owned-array structure. The local matrices occupy
4 MiB of stack storage. Median CPU times ± median absolute deviation, in ms:

| Variant | Size 128 | Size 256 | Size 512 |
| --- | ---: | ---: | ---: |
| Gloin before | 364.708 ± 0.833 | 389.969 ± 0.524 | 400.035 ± 0.466 |
| Gloin after | 243.472 ± 0.701 | 246.267 ± 0.700 | 322.458 ± 0.363 |
| C, product and sum checks | 418.543 ± 0.564 | 424.835 ± 2.833 | 424.471 ± 1.113 |
| C, final sum check | 242.284 ± 0.548 | 245.730 ± 0.291 | 322.209 ± 0.520 |
| C, neither check | 238.411 ± 1.217 | 243.302 ± 0.297 | 321.395 ± 0.167 |

Gloin uses **33.2%, 36.9% and 19.4% less CPU time**. All 30 paired comparisons
improve, with reductions ranging from 18.2% to 37.6%. The updated medians are
0.49%, 0.22% and 0.08% above C with a final check, with overlapping sample ranges;
this batch does not establish a meaningful speed difference between them.
Relative to C without either check, Gloin takes 2.12%, 1.22% and 0.33% more CPU
time. Before wall medians are 365.475/390.328/400.420 ms; after medians are
243.705/246.710/322.894 ms.

The matrix function grows from **488 to 636 bytes**. The fast inner loop uses
two-lane SIMD multiplies and scalar additions in source order, with its finite
check after the reduction. No FMA or reassociation is introduced. The separate
per-dot-copy fixture grows from 716 to 880 bytes for f64, and from 716 to 1,060
bytes for f32; these counts include copying and checked fallbacks/tails.

No task builds, tests or profiling run during either timing batch. Desktop
background activity remains, and all outliers are retained. The second batch
has substantial slow samples: for example, size-256 Gloin after reaches
357.710 ms versus a 246.267 ms median. CPU time excludes descheduling, but not
frequency or cache contention. Compiler, source, runtime, object and executable
hashes remain unchanged through sampling.

These results measure the eligible local-storage programs on this machine.
They do not measure an improvement to the earlier borrowed-slice kernel or the
arena/vector matrix application. Linux and f32 have correctness coverage here;
their performance, and Linux x86-64 execution, are unmeasured.

## Candidate release audit — 2026-10-19

The final working-tree audit passes 1,078 core tests on both macOS ARM64 and
Ubuntu ARM64, plus 1,078 macOS ASan/UBSan core tests. Installed and relocated
packages each pass 552 tests on both platforms, including separate TLS,
HTTP/HTTPS, JSON and million-record integration drivers. Serial and parallel
complete-suite reports each contain 1,121 passes and exactly the four documented
async/spawn failures, with no skipped or duplicate cases.

Linux crash-report contention required running the large arithmetic and GIR
CLI matrices alone. Their assertions, deadlines and process behavior remain
unchanged; the initial failed and interrupted attempts are retained. The
[release guide](release-0.2.0.md#verification-boundary) records the full boundary,
including example, documentation, package and sanitizer checks. Hosted macOS
and Ubuntu x86_64 checks on the tagged revision are required for publication;
their reports accompany the release.

## Remaining limitations

PERF-001 through PERF-009 are complete within the scopes recorded above.
General aggregate/array ABI costs remain. The arithmetic comparison includes
small execution regressions; matrix application timings are mixed and noisy.
Adjacent product/sum finite checks share one mandatory check. Eligible local
reductions now defer that check to their exit, allowing ordered SIMD. Arbitrary
borrowed slices and arena/vector buffers still need stronger storage and lifetime
evidence before applying the same transformation.
Interpreter sampling does not isolate the cost
of each dispatch, bounds or null check.

The fixes preserve arithmetic, bounds, null, initialization, result-handling
and lifetime rules. Further check elimination requires proofs that survive
aliasing and mutation. Future comparisons need equivalent work and timing
boundaries, with source changes measured separately from compiler changes.
