# Server TLS and graceful shutdown

The unreleased 0.1.0 tree provides nonblocking server TLS in `@net`, alongside
the existing verified client. Fallible public operations use built-in
`result<T>`. OpenSSL 3 performs cryptography; the four new host operations pass
through source-typed GloinIR and verified `gloin.abi_call` before LLVM lowering.

## Configuration and accepted connections

```gloin
def loaded: result<net.TlsServerConfig> =
    net.TlsServerConfig.load(&owner, "chain.pem", "key.pem");
if loaded.erroneous { return loaded.error; }
def mut config: net.TlsServerConfig = loaded.value;

// socket is a connected Socket returned by listener.accept(&owner).
def created: result<net.TlsServer> = socket.start_tls_server(&owner, &config);
if created.erroneous { return created.error; }
def mut tls: net.TlsServer = created.value;
```

`load` reads a PEM certificate chain (leaf first) and an **unencrypted PEM
private key**, checks that they match, and requires TLS 1.2 or later. TLS 1.3 is
supported. Paths must contain 1–4,096 bytes and no NUL. Relative paths use the
host cwd and ordinary filesystem path resolution. Files are read synchronously;
loading is not a nonblocking file API and has no deadline or file-size cap.
Encrypted keys fail without prompting on stdin. Missing/malformed files and
mismatched keys return an error. Configuration loading does not check that a
future client will trust the certificate or accept its validity period/name.

One configuration can create multiple sessions. Each session retains a native
reference to its configuration material; closing the configuration prevents new
sessions but existing sessions continue. No certificate reload occurs per
connection. Reload by creating a new configuration and using it for later
connections. There is one certificate identity per configuration; server-name
certificate selection, mutual TLS, ALPN, session resumption and early data are
not exposed. Server session caching and tickets are disabled. Clients still
verify trust and hostname; there is no skip-verification option.

`start_tls_server` requires an open connected socket, with no TLS session
previously started and no write half-close. It allocates arena metadata and a
native SSL session but does not perform a handshake. Failed setup leaves the
socket available for cleanup; successful setup reserves it for TLS.

## Nonblocking operations

Both `TlsClient` and `TlsServer` provide:

| Method | Result and meaning |
| --- | --- |
| `is_open()` | True while native session state is owned, including after shutdown or a fatal error. |
| `handshake() -> result<i32>` | Zero when complete; otherwise `net.READABLE` or `net.WRITABLE`. |
| `read(destination) -> result<TlsReadOutcome>` | `state` is OK, WOULD_BLOCK, or END; `count` gives bytes, `wait_for` gives required readiness. Nonempty destination required. |
| `write(bytes) -> result<TlsWriteOutcome>` | `count` gives accepted bytes; nonzero `wait_for` means retry after readiness. |
| `shutdown() -> result<i32>` | Zero only after both shutdown alerts have been exchanged; otherwise required readiness. |
| `close() -> result<void>` | Releases the native session and invalidates all its aliases. A healthy session gets one best-effort shutdown attempt. |

Check `.erroneous` before accessing the result. On a readiness result call
`socket.wait(event, timeout_ms)` or `net.wait_many`, then retry the same operation.
Readiness is a hint; another WOULD_BLOCK is normal. A successful read can leave
decrypted data buffered in OpenSSL: try reading again before waiting. Reads,
writes and handshakes can need either event, regardless of operation name.

For a blocked write, retain the **same bytes and length** until that attempt
progresses; their address may move. Advance only by `count` after a successful
partial write. Caller buffers are borrowed for a call, but the retry contract
still requires unchanged content. Do not interleave a pending write with another
write or shutdown. A fatal TLS error makes the session unusable for protocol
operations; retain ownership until `close`. Empty writes still check session
liveness, completed handshake, failure state and shutdown state.

All loops need a caller-selected monotonic deadline. Operations never wait for
socket readiness internally; one shutdown call can make two bounded OpenSSL
attempts to handle buffered alerts. This does not promise a fixed CPU time:
handshakes perform cryptography and OpenSSL can allocate native memory. Library
operations have no automatic cancellation or timeout.

## Authenticated shutdown

Consume the application's expected incoming data, finish pending writes, then
call `shutdown` repeatedly until it returns zero. The first call prohibits
further application writes through every alias. It sends our `close_notify` and
waits for the peer's authenticated alert. Merely sending our alert is not
completion. If the peer stalls, readiness/deadline handling remains the caller's
responsibility. On timeout, call `close` and then close the socket.

A peer's `close_notify` appears as `status.END` from `read`. Transport EOF
without that alert is a fatal error, including when it happens during shutdown.
Unexpected unread application data can also make shutdown fail: this API
does not silently discard an application response to finish closing.
Read operations remain available after shutdown starts, subject to OpenSSL's
state and prior errors. A completed shutdown is repeatable and returns zero;
it does **not** free TLS state or the socket.

`close` is always the cleanup step, including after handshake failure. It does
not promise bidirectional shutdown and does not send protocol traffic after a
fatal OpenSSL error. Follow it with `socket.close()`. Raw TCP reads/writes,
`shutdown_write` and another TLS session remain prohibited on a socket once TLS
has started, even after TLS cleanup. This API does not support a TLS-to-plaintext
downgrade. See [OpenSSL's shutdown contract](https://docs.openssl.org/3.0/man3/SSL_shutdown/).

## Ownership, costs and host behavior

Copies of configurations and sessions alias arena-owned state; closing one
invalidates its aliases. Repeated close is an error. Arena reset/free never
closes native resources. Close sessions before their sockets; close every handle
before freeing the arena that holds its metadata. Socket metadata must remain
alive for the whole session. Closing a configuration does not make it safe to
use its aliases after its arena is freed.

Configuration/session creation can retain one metadata allocation in the arena
on failure. OpenSSL owns additional native memory for certificates, keys,
handshake and record buffers; this storage is not charged to a Gloin arena.
Session creation retains configuration storage until the last session closes.
There is no fixed memory bound or automatic admission control for connections.
The caller chooses connection counts, application buffer limits and deadlines.
Calls on aliases require caller serialization.

TLS calls preserve the host's SIGPIPE disposition. macOS sockets use
`SO_NOSIGPIPE`; Linux temporarily blocks SIGPIPE on the calling thread and
consumes a newly pending signal before restoring its mask, preserving one that
was already pending. This covers protocol writes during reads, handshake and
shutdown too. No process-wide signal handler is installed.

## Runnable example

[tls_server.gloin](../examples/tls_server.gloin) accepts one loopback connection,
expects four `PING` bytes, sends four `PONG` bytes, performs graceful shutdown
and exits. It prints its kernel-selected port. One 30-second deadline covers
accept through shutdown; certificate loading occurs before that deadline.

Using an OpenSSL 3 command (on Apple Silicon Homebrew,
`/opt/homebrew/opt/openssl@3/bin/openssl`):

```sh
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 1 \
  -keyout key.pem -out cert.pem -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost
build/gloinc -O2 -o /tmp/tls-server examples/tls_server.gloin
/tmp/tls-server cert.pem key.pem
```

In a second terminal, replace `PORT` with the printed number:

```sh
printf PING | openssl s_client -quiet -connect 127.0.0.1:PORT \
  -servername localhost -verify_hostname localhost -verify_return_error -CAfile cert.pem
```

The client prints `PONG`. The self-signed certificate is an explicit local test
trust anchor. This example is a bounded protocol demonstration, not an HTTP
server framework.

## Verification

The [Gloin TLS driver](../tests/tls/README.md) covers trusted/wrong-host/untrusted
clients, aliases and graceful shutdown in JIT and native `-O0`/`-O2`.
Independent direct-OpenSSL peers cover TLS 1.2/1.3, old-version rejection,
binary partial writes under backpressure, authenticated EOF, truncation and
broken-peer signal handling. Compiler checks require all four new runtime calls
to cross `gloin.abi_call` and reject private intrinsic access.
