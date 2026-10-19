# Compilation timings (0.2.0)

The working compiler adds `--time-compilation` for PERF-001. It observes the
existing checked source → GloinIR → LLVM dialect → LLVM IR pipeline. It does
not change source semantics, select a different lowering path or enable
optimization. Released 0.1.0 does not accept this option.

```sh
./build/gloinc --time-compilation -O2 -o hello examples/hello_world.gloin 2> build.log
./build/gloinc --time-compilation --emit-llvm examples/hello_world.gloin > hello.mlir
./build/gloinc --time-compilation --jit examples/hello_world.gloin
```

## Output and boundaries

A normal compilation return emits one JSON line on stderr, prefixed with
`gloinc-timings: `. Diagnostics may precede it; parse the prefixed line rather
than treating the whole stream as JSON. The flag may appear once with any
compilation mode. It preserves stdout and exit status. Help, version and
argument-validation errors do not produce a report.

Times are elapsed wall time from a steady monotonic clock, in integer
nanoseconds. They include scheduling delays; they are not thread CPU time.
Each stage is exclusive: do not add nested pass timers to these totals.
Repeated entries with the same name are combined, with `calls` giving their
invocation count. Only entered stages appear, including a stage that fails.

| Stage | Included work |
| --- | --- |
| `input_setup` | Root file status/read, standard-library path selection and MLIR context construction. |
| `parsing` | Root source/lexer setup and parsing. |
| `module_loading` | Dependency discovery, reading and parsing, including standard-library imports. |
| `semantic_analysis` | Semantic checks and checked-program construction. |
| `gir_generation` | Code generator/dialect setup and high-level GIR construction. |
| `ir_verification` | Standalone module verification before inspection or lowering. JIT verifies the generated module and its clone, so this normally has two calls. |
| `gir_reachability` | Executable/JIT pruning of unreachable checked source functions and pass verification. Absent for module/object output and unmarked/already lowered IR. |
| `lowering` | Legality checks, pass setup, GIR preparation, local SSA promotion, range proofs and counted slice loop versioning, GloinIR and standard-dialect conversion passes, their enabled verifiers and final LLVM-dialect verification. |
| `native_target_setup` | Host target detection and target-machine creation. |
| `llvm_translation` | LLVM translation registration, translation, target/layout setup, native entry adapter and initial LLVM verification. |
| `llvm_optimization` | Native `-O2` analysis/pipeline setup, O2 passes with the compiler's inlining growth limit, and final LLVM verification. Absent for `-O0`. |
| `object_emission` | Temporary file creation, target code generation, object writes/close; also the final rename for object-only output. Code generation itself uses the requested optimization level. |
| `linking` | Runtime path resolution, linker launch/wait and final executable rename. |
| `ir_output` | Printing/flushing inspection output, including source locations. |
| `jit_preparation` | Entry adapter/verification, engine creation, runtime symbols and contexts, lazy LLVM translation/code generation and packed-entry resolution. JIT does not expose separate optimization/object/link phases here. |

`total_ns` starts after CLI argument validation, before input setup. Successful
native/check/inspection reports stop when the requested output is ready.
`unattributed_ns` is the remaining total: IR-count traversal, orchestration,
cloning, and cleanup outside explicit stages. It is not an extra compiler pass.
The JSON serialization and final CLI teardown are outside the total. Therefore
an external process timer also sees startup, dynamic loading, argument parsing,
reporting and final teardown, and will generally be larger.

On ordinary compilation failure, the report freezes during CLI scope cleanup.
It contains the work completed or attempted, including diagnostics/cleanup
before that point. A crash, signal or forced timeout may leave no report; that
absence does not identify the slow stage and is not a completed timing sample.

For JIT, the packed entry is resolved before freezing the report, then invoked.
Application execution and subsequent engine teardown do not change the frozen
time/counts/RSS. The CLI prints the report after the application returns. A
trapping or non-returning application therefore may produce no timing line.
`compilation_succeeded` describes compilation/preparation, not the application's
return code, output success or runtime behavior.

## JSON schema 1

| Field | Meaning |
| --- | --- |
| `schema_version` | Integer `1`; consumers should reject unknown schema versions. |
| `compiler_version`, `llvm_version` | Build versions. Unreleased binaries can still identify as 0.1.0: also record revision, patch and binary hash. |
| `mode` | `check`, `gir`, `llvm_dialect`, `object`, `executable`, or `jit`. |
| `optimization` | `O0`/`O2` for native output; `none` otherwise. |
| `compilation_succeeded` | Whether compilation reached its requested success boundary. |
| `total_ns`, `unattributed_ns` | Durations as defined above. Stage durations plus unattributed duration equal total. |
| `stages` | Array of `{name, nanoseconds, calls}`. |
| `counts` | IR snapshots described below, only for boundaries actually reached. |
| `rss_scope` | `compiler_process`. |
| `peak_rss_bytes` | Process high-water resident memory in bytes, or `null` when unavailable. |

Each counts entry has `name`, `function_definitions`, `function_declarations`
and either `operations` (MLIR) or `instructions` (LLVM IR):

- `gir`: after generation, before standalone verification. Operations include
  the module, functions and nested operations across all loaded modules.
- `reachable_gir`: after executable/JIT reachability, before array preparation
  and source/layout conversion. Module and object modes do not emit this snapshot.
- `llvm_dialect`: after the verified lowering pipeline.
- `llvm_ir`: native translation and entry adapter, before optimization.
- `optimized_llvm_ir`: after the requested native optimization policy; for
  `-O0` this is the same IR as the preceding snapshot.

LLVM instruction counts exclude declarations and metadata. These snapshots
measure different representations; operation and instruction counts are not
interchangeable. Large aggregate types can have few operations yet represent
many bytes/elements. JIT reports generated/reachable GIR and LLVM-dialect counts,
with the reachable snapshot present only when lowering checked GIR.

Peak RSS comes from `getrusage(RUSAGE_SELF)`: macOS already returns bytes;
Linux returns KiB, converted by multiplying by 1024. This is the compiler
process's lifetime high-water mark at the finish boundary, including startup
and libraries. It is not live memory, allocated bytes or a per-stage delta.
It excludes the linker child and any separately launched program. JIT freezes
it before program execution; it cannot isolate compiler memory from earlier
work when the collector is used inside a long-lived embedding process.

## Cost and repeatable comparisons

Without the flag there is no collector, clock read, RSS query or IR-count walk;
small null-pointer checks remain at instrumented boundaries. With the flag,
clock reads, stage recording, IR walks and report writing add work. The count
walks fall into `unattributed_ns`; report writing is outside `total_ns` but
inside an external invocation timer. Measure that external overhead by
alternating the same compiler with and without the flag.

The initial overhead experiment alternated released, working/plain and
working/timed compilers, discarded one warm-up round and recorded five rounds.
Every emitted executable was checked outside compilation timing. A comparison
needs identical source and flags, identified compiler/runtime revisions and
platform versions, raw samples and explicit timing boundaries. These
measurements have no timing threshold in CI. The
[compiler validation record](compiler-validation.md) contains the results and
their limits; the commands above work with sources in this repository.
