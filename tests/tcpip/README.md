# Independent TCP/IP and HTTP/HTTPS acceptance

The maintained `NetworkLibraryTest` groups run the pure Gloin TCP/IPv4 engines,
HTTP sessions and buffered TLS adapters in JIT, `-O0` and `-O2`. They require no
packet-device privileges. See [the server guide](../../docs/tcpip-server.md) for
buffer ownership, supported protocols and limits.

`interop.py` adds an independent peer: Linux kernel TCP plus Python's verified
TLS client talks to the Gloin server through a TUN device. The Gloin process
implements TCP for its side of every connection. The script refuses to run in
the initial network namespace, configures only its private namespace, generates
a temporary localhost certificate, and terminates each server in `finally`.
It needs Linux, Python 3, `ip`, OpenSSL and access to `/dev/net/tun`. Creating the
namespace and configuring its device normally requires root or equivalent
namespace/network capabilities.

From the repository root, on Linux:

```sh
build/gloinc -O2 -o build/ip-http-server examples/ip_http_server.gloin
sudo unshare --net -- python3 tests/tcpip/interop.py \
  --server "$PWD/build/ip-http-server" --reports "$PWD/build/tcpip-interop"
```

The checks exercise HTTP and HTTPS, trusted hostname verification, binary
chunked POSTs with `100 Continue`, trailers, pipelined requests, HEAD, 64 KiB
responses, 24 concurrent clients, stalled readers/writers, 100 sequential
connections (more than the server's 64 active slots), clean TLS/TCP closure,
and rejection of ambiguous request framing without breaking another client.
The timeout on each socket is ten seconds. The example binds the Gloin endpoint
to `10.0.0.2:8080`; the namespace's kernel peer uses `10.0.0.1`.

## Measurements

Append `--benchmark` to run four trials per HTTP/HTTPS, 1/16-connection, and
20-byte/64-KiB-body combination. Trial zero warms each case; three trials are
measured. Each connection performs 512 small requests or 64 large requests,
serially over a persistent connection. Every response body is verified.
Connection setup, warm-up requests, TLS handshakes and shutdown occur outside
the measured interval. Barriers have deadlines and report worker exceptions;
a failed peer cannot leave the test waiting indefinitely.

Reports include requests/s, body MiB/s, median/p95 request latency, server CPU
and resident memory from `/proc`, and client CPU. Per-trial JSONL files preserve
completed samples if a later trial fails; `interop.json` is written only after
both protocols succeed. Use a new report directory for every run. Client parsing,
Python scheduling, the virtual packet device and the VM all affect these results.

For comparisons, keep both binaries, build before measuring, stop other CPU-heavy
work, and alternate their run order (for example before/after/after/before).
Do not mix measurements produced by different timing rules. The checked-in
[2026-10-30 records](../../docs/benchmarks/tcpip-2026-10-30/) describe one ARM64
Linux VM run and its limitations.

`decoder_bench.gloin` separately checks and times 200,000 alternating fixed and
chunked binary requests using three-byte reusable body storage. Compile it with
the earlier and current packages to isolate request-decoder cost from TCP/TLS.
It verifies the request count, consumed offsets, body lengths and a checksum
before reporting nanoseconds. Build both binaries first, warm each, and use
several measured runs in a recorded randomized order.
