# Language examples

`core_counter.gloin` is a runnable core example, tested through the actual CLI.
After building, run `./build/gloinc examples/core_counter.gloin`: it prints `42`
and exits 0. It uses a helper call, mutation, a for-loop, and unless.
The other files are design examples and manual inputs; their execution is not
established by the maintained suite.

| Files | Intended subject | Current follow-up |
| --- | --- | --- |
| `core_counter.gloin` | Scalar core execution | Verified by the CLI tests; broader runnable fixtures are in [core acceptance](../tests/fixtures/core/README.md). |
| `hello_world.gloin` | Standard output | Strings and standard-module output: SPEC-022, SPEC-023. |
| `simple_test.gloin`, `comprehensive_test.gloin`, `P2_SUMMARY_DEMO.gloin` | Mixed features | Historical design inputs; use the maintained core acceptance fixtures for verified programs. |
| `defer_test.gloin` | Deferred cleanup | Scope and exit paths: SPEC-027. |
| `basic_endianness_test.gloin` | Byte-order-aware types | Representation and semantics: SPEC-037 through SPEC-039. |

Some examples predate the current specification and use unresolved or unsupported
syntax. Standard/local/package imports are not complete. These
files establish neither production readiness nor a specification-coverage percentage.

Use the [root README](../README.md) for setup, the [maintained tests](../tests/README.md)
for observed results, and [SPEC-TODO.md](../SPEC-TODO.md) for the implementation plan.
