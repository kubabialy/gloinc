# Language examples

These files are design examples and manual inputs, not a passing acceptance suite.
The CLI ignores filenames and tokenizes a hardcoded string: running
`gloinc examples/hello_world.gloin` does not compile or execute that example.

| Files | Intended subject | Current follow-up |
| --- | --- | --- |
| `hello_world.gloin` | Standard output | CLI and standard-module output: SPEC-020, SPEC-023. |
| `simple_test.gloin`, `comprehensive_test.gloin`, `P2_SUMMARY_DEMO.gloin` | Mixed features | Not validated end to end; core acceptance: SPEC-021. |
| `defer_test.gloin` | Deferred cleanup | Scope and exit paths: SPEC-027. |
| `basic_endianness_test.gloin` | Byte-order-aware types | Representation and semantics: SPEC-037 through SPEC-039. |

Some examples predate the current specification and use unresolved or unsupported
syntax. For-loops and standard/local/package imports are not complete. These
files establish neither production readiness nor a specification-coverage percentage.

Use the [root README](../README.md) for setup, the [maintained tests](../tests/README.md)
for observed results, and [SPEC-TODO.md](../SPEC-TODO.md) for the implementation plan.
