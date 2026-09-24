# Phase 2 notes — superseded

The previous report claimed complete for-loops, imports, parsing, and production
readiness without executable acceptance evidence. Those claims and the estimated
specification-coverage percentages have been withdrawn.

The audit and SPEC-001 through SPEC-004 established fresh builds, consistent
LLVM/MLIR linkage, a 112-case test inventory, and a checked external-tool harness.
Those runs had 98 passes and 14 failures; current results are in
[the test inventory](../tests/README.md). SPEC-019 verifies in-process JIT execution,
and SPEC-020 connects the [file-reading CLI](../docs/cli.md). Its tests execute
`core_counter.gloin`; the older example files remain outside the acceptance suite.

| Earlier item | Current tracking |
| --- | --- |
| Declaration/header drift | Repaired in SPEC-001. |
| Toolchain and build structure | Repaired in SPEC-002/SPEC-003 for LLVM/MLIR 21.1.6 on Apple Silicon. |
| For-loops and unless | Implemented and execution-tested in SPEC-017. |
| Standard/local/package imports | Incomplete: SPEC-023, SPEC-029, SPEC-030. |
| Defer across scopes and exits | Incomplete: SPEC-027. |
| Endianness and packed bitfields | Incomplete: SPEC-037 through SPEC-039. |
| Concurrency | Incomplete: SPEC-040 through SPEC-043; the restored spawn assertion fails. |
| Shared lowering, JIT, and file CLI | Verified under SPEC-018 through SPEC-020; SPEC-021 adds [source-file acceptance](../tests/fixtures/core/README.md). |

[The implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist) replaces the phase roadmap. Its completion log
records commands and results; [tests/README.md](../tests/README.md) lists failures.
No specification-coverage percentage is claimed.
