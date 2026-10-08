# Gloin TLS acceptance driver

Build `gloin_tls_tests` and run:

```sh
build/gloin-tls-test build/gloinc . build /path/to/openssl
```

Arguments select the compiler, repository, existing temporary parent and explicit
OpenSSL 3 executable. The driver creates an exclusive temporary directory and
uses OpenSSL's CLI to generate a one-day localhost certificate and private key.
It runs `fixtures/network/tls_sessions.gloin` in JIT, `-O0` and `-O2`. Every
compiler/fixture child gets piped output, its own process group, a 120-second
communication budget and 256 KiB per output stream; certificate generation has
30 seconds. Successful runs remove fixtures, including the private key. Failures
retain captures and print the private directory path. Launch/cleanup deadlines
and escaped-descendant limits follow the shared process contract.

This replaces `tests/tls_client_smoke.py`. The old driver passed before removal.
Its trusted, wrong-host and untrusted cases remain, now with an additional `-O2`
run. Fixed PING/PONG bytes are independently specified. The Gloin fixture uses
loopback TCP and explicit readiness state machines to drive both peers without
threads or async/spawn; each exchange has a five-second monotonic deadline.
It additionally checks aliases, configuration closure before handshake, blocked
raw socket access, no plaintext reuse, failed-session reuse, pre-handshake
shutdown rejection, authenticated END, write rejection after shutdown starts,
and repeatable bidirectional completion.

Both Gloin peers use the production TLS wrapper. Independent interoperability
evidence therefore also lives in `tests/runtime/tls_test.cpp`: direct OpenSSL
client calls exercise the Gloin server runtime with TLS 1.2/1.3, reject TLS 1.1,
force backpressure and compare binary payload bytes, distinguish `close_notify`
from abrupt transport EOF, and check host SIGPIPE policy. Configuration tests
reject missing, malformed, mismatched and encrypted keys without prompting.
Those native tests never use the production client wrapper as their peer.
The runnable server example is also checked with the OpenSSL CLI.

`NetworkLibraryTest` checks primitive privacy/signatures and requires server
configuration, session creation, cleanup and shutdown calls to remain
`gloin.abi_call` operations. These native/compiler cases and
`TlsClientSmoke.LocalPeers` are required by `check-core`. Package checks compile
the Gloin driver with installed and relocated compilers and their standard library.
The [HTTP drivers](../http/README.md) also run entirely in Gloin.
