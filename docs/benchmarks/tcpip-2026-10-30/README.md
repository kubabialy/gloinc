# IPv4/TCP and HTTP/HTTPS measurements, 2026-10-30

These measurements exercise the development Gloin IPv4/TCP stack and its
HTTP/HTTPS server. Linux kernel TCP and Python's verified TLS client communicate
through a TUN device in a private network namespace. The Linux ARM64 VM has four
vCPUs and 8 GiB RAM; the host is macOS ARM64. They measure this complete local
workload, including the client, virtual device and VM, rather than NIC capacity.

`controlled.json` is the comparison used below. Two complete runs per binary
were ordered before/after/after/before, with no concurrent compiler builds or
test runs. Each run has one warm-up and three measured trials per case, giving
six measured samples per cell. Each connection performs 512 small requests or
64 large requests. Setup, the warm-up request, TLS handshake and shutdown are
excluded. All response bodies are checked. The JSON includes individual
throughput, p50/p95 latency, server CPU/RSS, client CPU, binary hashes and source
hashes. [Harness instructions](../../../tests/tcpip/README.md) explain reproduction.

The earlier binary scans the complete 4,096-entry TIME_WAIT allocation. The
current binary scans only the used portion, skips socket-specific Linux SIGPIPE
signal-mask work for memory-BIO TLS, and replaces per-byte ring division with
cursor wrap checks. The current binary also includes path-MTU resegmentation;
these local benchmarks do not trigger it. This is a combined comparison, not
an attribution of each gain to an individual change.

| Protocol / body | Connections | Earlier median | Current median |
| --- | ---: | ---: | ---: |
| HTTP / 20 bytes | 1 | 12,385 requests/s | 15,803 requests/s |
| HTTP / 20 bytes | 16 | 16,208 requests/s | 13,476 requests/s |
| HTTPS / 20 bytes | 1 | 9,835 requests/s | 12,834 requests/s |
| HTTPS / 20 bytes | 16 | 9,955 requests/s | 9,499 requests/s |
| HTTP / 64 KiB | 1 | 48.3 MiB/s | 67.7 MiB/s |
| HTTP / 64 KiB | 16 | 45.8 MiB/s | 57.2 MiB/s |
| HTTPS / 64 KiB | 1 | 40.1 MiB/s | 62.3 MiB/s |
| HTTPS / 64 KiB | 16 | 24.7 MiB/s | 45.3 MiB/s |

Small single-connection trials last only about 30–50 ms, so their CPU counters
have coarse resolution. The 16-connection small-response workload is variable:
HTTP ranges are 12,910–20,596 requests/s before and 12,897–15,360 after; HTTPS
ranges are 8,322–11,900 before and 8,373–11,320 after. The median regresses in
those two cases. Server CPU use falls while Python's threaded client remains
in the measured path; client scheduling is a plausible contributor, not a proven
explanation. No universal latency or throughput improvement is claimed.

Bulk trials are steadier. Current 16-connection HTTP runs span 56.5–57.5 MiB/s,
and HTTPS 43.7–45.5 MiB/s. Median p95 latency for the 64 KiB request falls from
23.8 to 19.2 ms for HTTP and 41.6 to 24.2 ms for HTTPS at that concurrency.
Server peak resident memory stays about 12.6 MiB for HTTP and 15.8 MiB for HTTPS
in this bounded example. Capacity, window sizes, response size and request
count are part of the workload and must accompany any comparison.

## Request decoder in isolation

`decoder.json` compares the earlier relocated package with the current compiler
and library using `tests/tcpip/decoder_bench.gloin`. It decodes 200,000 alternating
fixed-length and chunked requests, with binary bodies and three-byte output
storage, verifying consumed offsets and checksum 131,400,000. Twelve invocations
were shuffled with seed 30,102,026; the first invocation of each binary is warm-up,
leaving five measured runs each.

Median time is 213.567 ms before and 215.883 ms after, a 1.1% difference. This
does not show a meaningful parser improvement; the packet/stack work does not
change the request decoder's parsing path. The tiny difference should not be
interpreted as an exact regression estimate from five runs.

## Exploratory records and harness failure

`baseline.json`, `history.json` and `optimized.json` retain earlier exploratory
runs. They used timing that included connection setup, and the optimized run
overlapped a compiler build. Do not combine them with the controlled samples.
An early quiet repeat stalled because its benchmark barrier had no timeout;
that run was terminated and produced no complete result. The trigger for that
client's failure was not captured. The harness now bounds both barriers and
propagates worker errors. A subsequent bounded run and all four controlled
runs completed for HTTP and HTTPS without an error. This is evidence from
those runs, not proof that an intermittent transport fault is impossible.
