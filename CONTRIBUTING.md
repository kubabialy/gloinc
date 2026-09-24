# Contributing to Gloin

Contributions are welcome, including work on platforms outside the maintained
release matrix. Version 0.0.1 supports Apple Silicon macOS. Linux support is
planned for 0.1.0. Windows support is not planned, but contributors may propose
and maintain it.

AI tools are allowed as aids. Every contributor must personally read, understand,
and manually verify every change they submit. Changes must have deterministic,
reproducible behavior and tests appropriate to the affected feature. Include the
commands run and their results in the pull request. Do not claim an AI generated
answer or test result was verified when it was not.

AI slop and vibecoding are strictly prohibited: do not submit unreviewed generated
code, fabricated tests or evidence, unexplained broad rewrites, or changes whose
behavior you cannot explain and reproduce. Repeated or serious violations may
result in exclusion from the project.

For language behavior, update the specification, source acceptance fixtures, and
versioned user documentation together. Keep unsupported features rejected with
clear diagnostics. Run the relevant targeted tests and the `check-core` target
before proposing a release change.
