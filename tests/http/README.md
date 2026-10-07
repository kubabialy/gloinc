# Gloin HTTP acceptance

Build and run both suites:

```sh
cmake --build build --target gloin_http_tests
ctest --test-dir build -R '^Http(Client|Stream)Smoke\.' --output-on-failure
```

The shared driver has two public modes:

```text
gloin-http-test client|stream COMPILER REPOSITORY_ROOT TEMP_PARENT OPENSSL EXAMPLE
```

All paths are explicit and canonicalized. Use an OpenSSL CLI that supports
`req -addext` (OpenSSL 3 is used by CI). It generates a temporary self-signed
localhost certificate and unencrypted key; no public service, credential,
interpreter, shell command, or package download is used.

## Preserved independent expectations

`wire.gloin` contains literal response heads, malformed messages, payload
patterns, and a small request reader. It imports neither `@http` nor
`@http_client`. Expected wire data is not generated using the production
HTTP parser or serializer. Native TLS tests separately exercise the transport
against direct OpenSSL peers.

The former Python drivers passed before removal. Their clients and examples
now run in JIT, `-O0`, and `-O2` (the previous drivers ran JIT/default AoT):

| Suite | Cases in each execution mode |
| --- | --- |
| Buffered client | 15 HTTP cases and 17 HTTPS cases: fixed/chunked/EOF framing, binary NUL, 30,000-byte body, 100/103 responses, HEAD/204/304/205, 429, truncation, ambiguous framing, body limit, invalid chunk, timeout, slow progress, plus TLS wrong hostname/untrusted certificate |
| Client example | Exact POST body, JSON response, status/Content-Type/body stdout over both transports |
| Streaming | Ten cases over each transport: 1 MiB upload/download, early 413 during a 64 MiB upload, paused producer and consumer, deadlines, partial output consumption, cancellation through aliases, short/excess upload, and truncation after a valid prefix |
| Concurrent example | 32 exchanges over each transport: 16 fixed and 15 chunked binary downloads of 131,076 bytes, plus a stalled connection cancelled after the other 31 finish |

That is **102 connections** in client mode and **252 connections** in streaming
mode. The driver requires exact client stdout, empty stderr and exit zero.
Peers independently verify methods, paths, duplicate-free fields, declared
lengths, authorization where applicable, binary request bodies, path counts and
connection cleanup. Early rejection must stop before all declared upload bytes
arrive. Clean EOF responses use TCP write shutdown or authenticated TLS
`close_notify`; abrupt TLS EOF is not substituted for it.

## Scheduling, ownership, and bounds

Each transport/fixture group runs one child peer concurrently with three client
executions. The peer binds loopback port zero and announces the assigned port
through stdout. The driver requires it within five seconds, replaces exactly
one `def const PORT: u16 = 8080;` declaration (allowing formatter alignment),
and preserves the rest of the source. It also tests installed example copies.

The peer uses a single explicit readiness loop and advances each connection
once per round. There are at most 96 accepted connections per group, each with
an 8 KiB request-head buffer and a 45-second lifetime budget. Closed connections
leave the wait set immediately; retained state is bounded until group exit.
Request bodies are checked incrementally using 8 KiB scratch. Response fixtures
use bounded arena storage (at most 1,050,000 builder bytes plus copies per
connection); this is a test oracle, not a server memory benchmark.

The slow-response case schedules one byte every 40 ms; the paused-producer
case schedules its early response after 40 ms. The next timer bounds
`net.wait_many`, and delayed connections do not keep writable watches.
No native threads, language async/spawn, sleeps, or busy waiting are needed.
TLS retries preserve pending bytes/length and honor the returned readiness
direction. The peer's whole-run budget is 300 seconds.

Compiler/client captures have a 120-second monotonic deadline and 256 KiB per
output stream. Peer completion has a ten-second deadline and 64 KiB per stream.
CTest additionally caps each complete suite at 360 seconds. These are
test-harness bounds, not hard real-time guarantees.

Every child uses a new process group; failure/timeout cleanup kills ordinary
descendants and reaps the child. Escaped descendants remain outside this
documented containment. Peer teardown closes TLS sessions before sockets,
then configuration and arena storage. On failure, available captures, sources,
certificates and the announced port remain in the printed private directory.
Successful runs remove the stable owned tree without following final symlinks.
