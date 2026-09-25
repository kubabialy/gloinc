# Contributing to Gloin

Contributions are welcome, including work on platforms outside the maintained
release matrix. The 0.0.3 source version supports Apple Silicon macOS. Linux
support is planned for 0.1.0. Windows support is not planned, but contributors may propose
and maintain it.

Whether to use AI tools to supplement development is each contributor's choice.
Every contributor must personally read, understand, and manually verify every
change they submit. Changes must have deterministic, reproducible behavior and
tests appropriate to the affected feature. Include the commands run and their
results in the pull request. Do not claim generated code, an answer, or a test
result was verified when it was not.

AI slop and vibecoding are prohibited. Do not submit unreviewed generated code,
fabricated tests or evidence, unexplained broad rewrites, or changes whose
behavior you cannot explain and reproduce. Repeated or serious violations may
result in exclusion from the project. Keep personal tool instructions and other
tool-specific development files out of the repository.

For language behavior, update the specification, source acceptance fixtures, and
versioned user documentation together. Keep unsupported features rejected with
clear diagnostics. Run the relevant targeted tests and the `check-core` target
before proposing a release change.

Follow the [Gloin source style](docs/gloin-style.md) for `.gloin` files and run
`python3 scripts/format-gloin.py --check` before submitting them.
