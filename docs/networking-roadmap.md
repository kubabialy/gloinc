# Networking milestones after the first TCP and HTTP layer

The 0.1.0 candidate provides nonblocking IPv4 TCP, a verified nonblocking TLS
client, bounded multi-socket readiness waits, and a Gloin HTTP/HTTPS client
with streaming fixed-length, chunked, and clean-EOF response decoding,
backpressure, cancellation, and per-exchange deadlines. The
`gloin_examples/http_server` example remains sequential; the sibling
`http_stream/server.gloin` demonstrates concurrent, bounded HTTP fixture
connections. A reusable HTTP server library, server-side TLS, and a full
real-time client remain future work. Slack is one demanding
integration example for the general APIs below. The steps are capability gates,
not claims that these features already work.

## Shared transport foundation

1. Make listener options explicit. `Socket.set_reuse_address` is available
   before `bind`, and `shutdown_write` supports an orderly half close. Test
   restart behavior on macOS and Linux and preserve OS error categories in
   Gloin errors.
2. Resolve hostnames and support IPv6. `resolve_ipv4` is synchronous and
   documents that it can block. Add IPv6 and a nonblocking resolver interface.
   Callers must be able to choose an address and connect with a
   deadline. A failed candidate address must close cleanly before trying the
   next one. Keep address families and port byte order explicit.
3. Wait on many sockets in one call. `wait_many` now uses `poll` on both
   supported systems with caller-owned buffers. An event loop still needs a
   reusable watch set to avoid allocations for large watch sets, cancellation,
   deadlines, and stale-handle rules. A later backend can use `kqueue` or
   `epoll` without changing the public readiness contract.
4. Keep socket state and buffers caller-owned. Preserve the source-typed
   GloinIR ABI boundary and require explicit close before freeing the arena.
   Test short reads/writes, connection refusal, half close, timeouts, and
   thousands of sequential connections with local peers.

## Outbound HTTPS and HTTP clients

Outbound clients need authenticated requests. [Slack's Web API](https://docs.slack.dev/apis/web-api/)
is one example of HTTPS with bearer tokens and TLS 1.2 or later with SNI. A useful
Gloin TLS client now verifies the certificate chain and hostname against a
trusted root store, requires TLS 1.2 or newer, sets SNI for DNS names, and maps
OpenSSL read/write retry states to Gloin readiness bits. Callers still need to
set explicit deadlines, and the client only provides best-effort shutdown.
The [OpenSSL hostname verification API](https://docs.openssl.org/3.0/man3/SSL_set1_host/)
and [nonblocking client guide](https://docs.openssl.org/master/man7/ossl-guide-tls-client-non-block/)
define those checks and retry states.
Use default trust locations in production and an explicit trust file for
private CAs or local fixtures. OpenSSL 3 is a build and runtime dependency for
the compiler, runtime library, and native programs. Local certificate tests
run in JIT and native modes. Local macOS and Ubuntu ARM64 core and package
checks passed; hosted x86_64 CI remains before release.

The [bounded HTTP/HTTPS client](http-client.md) now provides request methods,
validated application headers, fixed-length request bodies, incremental bounded
response decoding (including chunked), status and header inspection, and an
overall deadline. Local HTTP and HTTPS fixtures exercise JIT and native builds.
The convenience API waits synchronously over the same `Exchange` state machine
used by event loops. Streaming known-length uploads and bounded incremental
downloads, early final responses during uploads, cancellation, and backpressure
are implemented. A 32-connection example verifies independent progress while
one peer stalls. Unknown-length/chunked uploads remain pending. Add URL
parsing, redirects, and connection reuse only with explicit limits.
The [bounded JSON module](json.md) now reads complete buffered payloads and
writes request bodies with checked Unicode, exact number text, and explicit
storage limits. Incremental JSON input, DOM/schema mapping, and application
authentication remain separate milestones. Secrets should be
read from the process environment, not embedded in source or logs.

## Inbound HTTPS services

General services need an HTTPS server,
concurrent connection handling, complete HTTP request bodies, size and time
limits, and an optional request-authentication layer. For a concrete integration
test, [Slack's Events API](https://docs.slack.dev/apis/events-api/using-http-request-urls)
receives JSON POST payloads and uses [request signatures](https://docs.slack.dev/authentication/verifying-requests-from-slack/).
Verification must use the raw body, HMAC-SHA256, a constant-time comparison,
and a timestamp window. The
example also needs outbound HTTPS to send replies through the Web API. A local
HTTP-only listener is useful for development but is not a public endpoint;
deployment behind a TLS-terminating reverse proxy is another valid ingress
architecture.

Acceptance: run a local mock HTTPS sender against the server, verify valid and
invalid signatures and replay windows, process concurrent posts without one
stalled client blocking the others, and send a response through a mock HTTPS
Web API. Run JIT and native builds on macOS and Linux without live Slack
credentials in CI.

## Outbound WebSocket clients

General WebSocket clients need authenticated upgrades, masked frames, bounded
lengths, fragmentation, ping/pong, close handling, and reconnection. As an
integration test, [Slack Socket Mode](https://docs.slack.dev/apis/events-api/using-socket-mode/) obtains
a `wss://` URL through an authenticated HTTPS call and then receives events
over a WebSocket. A Gloin client therefore needs the same DNS, TLS, HTTPS,
and JSON foundations, plus an application-level acknowledgement flow. The connection URL can
change, so reconnect must request a fresh one.

Acceptance: connect to a local mock `wss://` service with a trusted test
certificate, reject an untrusted or wrong-host certificate, exchange masked
frames and ping/pong, reconnect after closure, acknowledge a mock event, and
send a reply through a mock HTTPS Web API. Run the same tests in JIT and native
builds on both supported operating systems.
