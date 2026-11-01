# Gloin 0.2.2

0.2.2 is a compatible compiler optimization and correctness release. It reduces
redundant floating-point checks and enables SIMD in more proved-safe private-array
loops. There are no new language features or standard-library APIs. Hash maps
remain the functionality introduced in 0.2.1.

See the [release guide](release-0.2.2.md),
[language guide](site/0.2.2/index.html), and
[preparation record](release-0.2.2-draft.md).

## Checked floating-point optimization

- Finite-value proofs follow constants, checked expressions, CFG joins and loop
  backedges. Growing ranges widen conservatively; unknown values retain checks.
- Adjacent arithmetic chains combine redundant predicates while retaining checks
  for intermediate overflow, even when a later clamp, cancellation, zero multiply
  or discarded result would hide it.
- Eligible local reductions and private-array batches can share deferred failure
  checks. Storage, lifetime, effects, iteration bounds and fallback paths must
  satisfy the compiler's proof before LLVM can vectorize the loop.
- Short constant batches receive bounded scheduling hints. Eight-element batches
  use two-way interleaving; eligible 16–32-element batches use four-way
  interleaving, with a code-growth limit.
- Private initialized f32/f64 arrays carry interval bounds joining every write,
  including writes through overlapping borrowed views. Branch comparisons narrow
  already-finite values through clamp joins. Escapes, unknown writes and unsupported
  storage layouts prevent the proof.
- Mixed GIR/LLVM inputs are handled conservatively: undef, poison, fast-math
  operations, uninitialized storage and poison select conditions cannot establish
  stable finite facts without the required proof. Checked inputs are stabilized
  where the analysis requires a concrete value.

Arithmetic order and source-format rounding remain intact. The compiler does not
introduce fast math, FMA contraction, unchecked arithmetic or assumptions about
normally observed input values. Bounds, null, initialization, result-handling and
lifetime guarantees remain. Floating overflow still fails; failing batches cannot
expose invalid results through observable effects.

See [GloinIR/lowering](lowering.md) for recognition rules, budgets and exclusions.
General heap/arena-array invariants and automatic synthesis of application input
guards are outside this release.

## Performance evidence

The final optimization experiment measured 100 million particle updates on an
Intel i7-10700 with LLVM/MLIR 21.1.8, pinned to one logical CPU. Across 24 fresh
runs per variant in rotating order, the selected implementation took 105.03 ms,
versus 119.95 ms for the preceding development implementation (12.4% lower).
C with Clang O3 took 96.68 ms and Odin speed/native took 92.56 ms in that run.

This is not an upstream 0.2.1-versus-0.2.2 comparison or a general language speed
claim. The selected benchmark includes explicit input guards and scalar fallback;
the compiler does not synthesize those source changes. The same guarded source
on the preceding compiler took 127.85 ms. Unchanged source on the new compiler
took 118.61 ms, a small difference with overlapping distributions. All variants
compute the same workload; the benchmark permits its documented energy-reduction
tolerance. Every timed output passed an independent Gloin oracle. CPU frequency
and sibling activity were uncontrolled.

## Compatibility and validation

No source migration from 0.2.1 is required. Use the compiler, runtime and standard
modules from the same distribution and rebuild native objects/programs when
updating. The 0.2.1 hash-map contracts are unchanged.

New regression drivers are written in Gloin and cover JIT, native O0/O2 and
structural lowering as appropriate. Cases include signed zero, subnormals,
separate rounding, first/last-lane overflow, aliased views, effect barriers,
fallbacks and mixed-IR invalid inputs. Platform and package validation must pass
before publication; the [preparation record](release-0.2.2-draft.md) distinguishes
completed local checks from pending release gates.
