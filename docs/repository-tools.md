# Repository checks written in Gloin

Build the compiler and tools:

```sh
cmake --build build --target gloin_formatter gloin_checks
build/gloinfmt --check .
build/gloinfmt --write --git
build/gloin-check-docs
build/gloin-check-suite reports/serial.xml reports/parallel.xml
```

The formatter is installed with the compiler. The two checkers are repository
development tools, built even with `BUILD_TESTING=OFF`; they are not installed
into release packages. They need no Python interpreter. CI runs documentation
checks after building them. The [formatter guide](gloinfmt.md) describes its
write/discovery contract.

## HTML links

`gloin-check-docs [SITE_DIRECTORY]` defaults to `docs/site`. It scans `.html`
files, gathers `id`, `href` and `src` attributes, and checks local targets and
fragments. Tag/attribute names are ASCII case insensitive, quoted/unquoted
attribute values are supported, comments and script/style contents are skipped,
and the last duplicate attribute wins. Numeric character references and all
2,125 semicolon-terminated HTML named references are decoded; their fixed table
is in `tools/checks/html_names.gloin`. Paths and fragments are percent-decoded;
queries do not change the target. Directory links require `index.html`.
Existing files outside the scanned tree are checked for existence, but fragments
are checked only for HTML pages indexed by this run, matching the previous tool.

Absolute paths, network-path references and URLs with a scheme are skipped;
there is no network access. File targets are canonicalized and may follow
symlinks, including links outside the documentation tree. Directory traversal
does not follow directory symlinks. No page is counted twice after canonical
resolution. A missing tree or one containing no HTML fails.

This is a bounded attribute scanner, not a browser or an HTML conformance
validator. Inputs must be UTF-8 with XML-compatible characters. Names use ASCII
letters, digits, underscores, dots and hyphens; the HTML5 doctype is supported.
Malformed tags/quotes, malformed percent escapes, unknown semicolon-terminated
entities, invalid Unicode references and unsupported declarations fail visibly.
Use semicolons on character references; browser recovery of malformed or
semicolon-less entity spellings is not implemented. Other ordinary ampersands,
such as query separators, remain literal. The current documentation passes
these stricter input rules.

Limits: 8 MiB per page, 64 MiB total HTML, 4,096 pages, 64 directory levels,
65,536 bytes per path/attribute, 1,024 attributes per tag and 65,536 links/ids
per page. Allocation/host errors fail the check; vector allocation exhaustion
can still trap under the current `@vector` contract. Traversal assumes a stable
checkout. Exit codes: 0 pass, 1 check/I/O/parse failure, 2 invalid CLI usage.

## Complete-suite classification

`gloin-check-suite SERIAL_XML PARALLEL_XML` reads both reports and requires:

- At least one testcase with a nonempty name; names must be unique after entity
  decoding and XML attribute whitespace normalization.
- No skipped testcase.
- Exactly four failures: `AsyncTest.SpawnGeneration`,
  `AsyncTest.DeferredFunctionGeneration`, `SemaAsyncTest.AsyncTypes`, and
  `CodeGenTest.GenerateSpawn`.

Direct `failure` or `error` children mark a testcase failed, as in the previous
checker. A nested diagnostic element called `failure` does not. Each report is
classified independently. The tool prints pass counts and the retained failure
names and returns 1 on any violation. It does not waive, remove or rename tests.

The private XML reader accepts the CTest JUnit shape with a `testsuite` or
`testsuites` root, properly nested tags, quoted attributes, comments, CDATA,
processing instructions, five predefined entities and valid decimal/hex Unicode
references. The optional XML declaration must be CTest's exact
`<?xml version="1.0" encoding="UTF-8"?>` spelling, at the start after an optional
UTF-8 BOM. Duplicate attributes, malformed entities, invalid characters,
unclosed/mismatched tags, multiple roots and text outside the root are errors.
DTDs, custom entities, namespaces, non-UTF-8 encodings and non-ASCII element names
are unsupported and rejected. No external entity or network resolution occurs.
This is a private report reader, not a new public XML standard library.

Limits: 64 MiB per report, 65,536 testcases, 64 element levels, and the same
name/attribute bounds as the HTML reader. Parsing borrows the input; retained
case names use a separate arena. Name duplicate checks are quadratic in the
number of cases. Use it on trusted, bounded CI reports. Missing or unreadable
reports fail; CLI usage errors return 2.

## Gloin test drivers

`GloinToolingSmoke.NativeChecksAndWrites` and `.JitChecksAndWrites` cover malformed
reports, exact failure classification, named/numeric entities, Unicode URLs,
missing pages/anchors, raw script/style text, duplicate attributes, Git discovery,
ignored files, names containing spaces/newlines, symlink rejection, repeated
writes and native/JIT formatter execution. Native runtime tests additionally
check modes/ownership, hard links, binary contents, FIFO rejection and partial
write failure with the original preserved and staging cleaned up.

`JsonSmoke.CodecAndState` now invokes `gloin-json-test`:

```sh
cmake --build build --target gloin_json_tests
build/gloin-json-test build/gloinc . build
```

It runs the fixed independent corpus, state/limit fixture and example in JIT,
`-O0` and `-O2`. See the [corpus provenance](../tests/fixtures/json/README.md).
An optional fourth argument selects the example source; package acceptance
compiles the driver with each installed/relocated compiler and tests that
package's example.

`GloinIntegratedStress.MillionRecords` invokes the Gloin stress driver:

```sh
cmake --build build --target gloin_stress_tests
build/gloin-stress-test build/gloinc examples build
```

It checks million-line/record boundaries and simulation results under a 2 MiB
child soft stack limit, with a 60-second communication deadline per child.
The [independent oracle and driver contract](../tests/integrated/README.md)
describe retained failure evidence and fixed expected results. Both `check-core`
and installed/relocated package acceptance require this check.

`TlsClientSmoke.LocalPeers` now invokes `gloin-tls-test COMPILER ROOT TEMP_PARENT
OPENSSL`. It creates local certificates with the explicit OpenSSL CLI and checks
trusted/wrong-host/untrusted clients plus server lifecycle and graceful shutdown
in JIT, `-O0` and `-O2`. See the [TLS driver and independent peer checks](../tests/tls/README.md).
The HTTP and streaming checks now share `gloin-http-test`, also written in
Gloin. It launches bounded concurrent Gloin HTTP/HTTPS peers with independent
wire expectations and checks JIT/`-O0`/`-O2` clients. See the
[HTTP driver contract and coverage](../tests/http/README.md).
All repository-owned test/tool drivers are now free of Python dependencies;
upstream build dependencies have their own requirements.

Shared child capture and cleanup live in `tests/support/tool_runner.gloin`.
Each child has pipes, an explicit executable, a new process group, bounded
capture, and a monotonic deadline (30 seconds for tooling checks, 120 for JSON/TLS/HTTP,
60 for stress). `capture_limited` additionally selects a child soft stack limit;
the other capture helpers inherit the host limit.
Each stream is limited to 256 KiB. On failure, available captures and fixture
files are retained and their directory printed. Successful runs delete their
exclusive temporary tree. Cleanup assumes stopped children and a stable owned
tree and does not follow final symlinks; process groups do not contain escaped
descendants. These limits and [process-group semantics](child-processes.md)
remain explicit.
