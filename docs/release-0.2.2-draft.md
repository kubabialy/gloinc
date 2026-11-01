# 0.2.2 preparation record

Prepared on 2026-10-24 from upstream 0.2.1 commit
`e321d076656319b7b83a8e845652d49b5636a54f`, with the cumulative checked-float
optimization series and its Gloin regression drivers. The remote default branch
and latest published release were checked during preparation and still identified
that source and v0.2.1. This record does not announce a published release.

## Candidate scope

The compiler source changes are the tested floating proof, chain-combination,
local-reduction, private-batch and scheduling work. Release preparation updates
CMake/version assertions, package expectations, release documentation and a new
candidate HTML guide. Published guides and the site's 0.2.1 default stay unchanged.
The benchmark's guarded particle implementation is application code, not a new
compiler intrinsic or standard-library feature.

## Validation boundary

The preceding development compiler passed 1,098 of 1,112 required core tests in
the restricted Linux x86_64 environment, with 14 network/TLS failures identical
to the upstream sandbox baseline. All 116 relevant lowering, floating and hashmap
tests passed. Particle fixtures passed at O0, O2 and JIT. These earlier results
motivate the candidate; they are not a substitute for its fresh validation.

Fresh candidate build, core, formatting, documentation, package and relocation
results are recorded in the accompanying preparation reports. Consult their
exact outcomes; this source document alone does not certify the release gates.

The fresh Release build on Ubuntu 24.04.5 x86_64 reports
`gloinc 0.2.2 (LLVM/MLIR 21.1.8)`. Its core audit passed 1,098 of 1,112 tests;
the 14 failures exactly match the preceding compiler's network/TLS sandbox
failures. Source formatting, all ten HTML pages' local links, whitespace checks
and type-checking all 50 example sources passed. Compiler, standard-library and
example source bytes match the tested optimization checkout; the version is
supplied by CMake. This is local candidate evidence, not a passing release gate.

The following remain required before publication:

- A passing required core suite on macOS ARM64 and Ubuntu ARM64/x86_64, with
  networking available. The sandbox's 14 failures are not release exemptions.
- Installed and relocated package gates, including networking/TLS/HTTP and
  integration drivers, on the maintained platforms.
- Serial/parallel complete-suite classification with exactly the four documented
  unsupported async/spawn cases, no new failures and no skipped cases.
- macOS ASan/UBSan, compiler-only build and hosted CI for the final committed tree.
- Final commit identity, package checksums and provenance, then tag/release/site
  publication. Rebuilt or changed artifacts require matching validation.

No tests are disabled or reclassified to make the release appear green. No
0.2.2 tag or GitHub Release is created by this preparation.
