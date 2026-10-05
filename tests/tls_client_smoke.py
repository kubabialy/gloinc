#!/usr/bin/env python3
"""Local TLS client smoke test; uses no public network or credentials."""
import argparse
import pathlib
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading


SOURCE = r'''import "@arena";
import "@net";
import "@status";

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    def opened: result<net.Socket> = net.Socket.open(&owner);
    if opened.erroneous { return 1; }
    def mut socket: net.Socket = opened.value;
    def connected: result<bool> = socket.connect(net.Address.loopback(__PORT__));
    if connected.erroneous { return 2; }
    if !connected.value {
        def mut done: bool = false;
        for def mut i: i32 = 0; i < 20 && !done; i = i + 1 {
            def waited: result<i32> = socket.wait(net.WRITABLE, 1000);
            if waited.erroneous { return 3; }
            def finished: result<bool> = socket.finish_connect();
            if finished.erroneous { return 4; }
            done = finished.value;
        }
        if !done { return 5; }
    }
    def created: result<net.TlsClient> = socket.start_tls_client(
    &owner, "__HOST__", "__TRUST__");
    if created.erroneous { return 6; }
    def mut tls: net.TlsClient = created.value;
    def premature_close: result<void> = socket.close();
    if !premature_close.erroneous { return 20; }
    def mut complete: bool = false;
    def mut failed: bool = false;
    for def mut i: i32 = 0; i < 20 && !complete && !failed; i = i + 1 {
        def step: result<i32> = tls.handshake();
        if step.erroneous { failed = true; }
        else if step.value == 0 { complete = true; }
        else {
            def waited: result<i32> = socket.wait(step.value, 1000);
            if waited.erroneous { return 7; }
            if waited.value == 0 { return 7; }
        }
    }
    if __EXPECT_FAILURE__ {
        if !failed { return 8; }
    } else {
        if !complete { return 9; }
        def mut probe: [u8; 1] = {0};
        def raw: result<net.ReadOutcome> = socket.read(probe[..]);
        if !raw.erroneous { return 10; }
        def request: [u8; 4] = {80, 73, 78, 71};
        def mut sent: u64 = 0;
        def four: u64 = 4;
        for def mut i: i32 = 0; i < 20 && sent < four; i = i + 1 {
            def write: result<net.TlsWriteOutcome> = tls.write(request[sent..]);
            if write.erroneous { return 11; }
            sent = sent + write.value.count;
            if write.value.wait_for != 0 {
                def waited: result<i32> = socket.wait(write.value.wait_for, 1000);
                if waited.erroneous { return 12; }
                if waited.value == 0 { return 12; }
            }
        }
        if sent != four { return 13; }
        def mut response: [u8; 4] = zeroed;
        def mut received: u64 = 0;
        for def mut i: i32 = 0; i < 20 && received < four; i = i + 1 {
            def read: result<net.TlsReadOutcome> = tls.read(response[received..]);
            if read.erroneous { return 14; }
            if read.value.state == status.END { return 15; }
            received = received + read.value.count;
            if read.value.wait_for != 0 {
                def waited: result<i32> = socket.wait(read.value.wait_for, 1000);
                if waited.erroneous { return 16; }
                if waited.value == 0 { return 16; }
            }
        }
        def p: u8 = 80;
        def o: u8 = 79;
        def n: u8 = 78;
        def g: u8 = 71;
        if received != four || response[0] != p || response[1] != o ||
        response[2] != n || response[3] != g { return 17; }
    }
    def tls_closed: result<void> = tls.close();
    if tls_closed.erroneous { return 18; }
    def closed: result<void> = socket.close();
    if closed.erroneous { return 19; }
    owner.free();
    return 0;
}
'''


def peer(listener, context, errors):
    try:
        conn, _ = listener.accept()
        conn.settimeout(10)
        try:
            with context.wrap_socket(conn, server_side=True) as stream:
                if stream.recv(4) == b"PING":
                    stream.sendall(b"PONG")
        except (ssl.SSLError, ConnectionResetError, BrokenPipeError):
            pass  # Expected for clients rejecting the certificate.
    except Exception as exc:
        errors.append(exc)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gloin-tls-") as temp:
        temp = pathlib.Path(temp)
        cert, key = temp / "cert.pem", temp / "key.pem"
        brew_openssl = pathlib.Path("/opt/homebrew/opt/openssl@3/bin/openssl")
        intel_brew_openssl = pathlib.Path("/usr/local/opt/openssl@3/bin/openssl")
        openssl = next((str(path) for path in (brew_openssl, intel_brew_openssl)
                        if path.is_file()), shutil.which("openssl"))
        if openssl is None:
            raise RuntimeError("OpenSSL command-line tool is required for TLS fixtures")
        subprocess.run([
            openssl, "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-days", "1", "-keyout", str(key), "-out", str(cert),
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        for label, hostname, trust, expect_failure in (
            ("trusted", "localhost", str(cert), False),
            ("wrong-host", "127.0.0.1", str(cert), True),
            ("untrusted", "localhost", "", True),
        ):
            errors = []
            with socket.socket() as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(10)
                worker = threading.Thread(target=peer, args=(listener, context, errors))
                worker.start()
                source = temp / (label + ".gloin")
                source.write_text(SOURCE.replace("__PORT__", str(listener.getsockname()[1]))
                                  .replace("__HOST__", hostname)
                                  .replace("__TRUST__", trust)
                                  .replace("__EXPECT_FAILURE__", str(expect_failure).lower()))
                if label == "trusted":
                    ir = subprocess.run([str(args.compiler), "--emit-ir", str(source)],
                                        check=True, capture_output=True, text=True, timeout=30)
                    if "gloin.abi_call" not in ir.stdout or "gloin_net_tls_handshake" not in ir.stdout:
                        raise AssertionError("TLS calls bypassed the GloinIR ABI boundary")
                for mode in ("jit", "native"):
                    # One server connection per run.
                    if mode == "native":
                        worker.join(timeout=10)
                        if errors:
                            raise RuntimeError(errors)
                        worker = threading.Thread(target=peer, args=(listener, context, errors))
                        worker.start()
                        output = temp / (label + "-native")
                        subprocess.run([str(args.compiler), "-o", str(output), str(source)],
                                       check=True, timeout=30)
                        command = [str(output)]
                    else:
                        command = [str(args.compiler), "--jit", str(source)]
                    run = subprocess.run(command, capture_output=True, text=True, timeout=30)
                    if run.returncode or run.stdout or run.stderr:
                        raise AssertionError((label, mode, run.returncode,
                                              run.stdout, run.stderr))
                worker.join(timeout=10)
                if errors:
                    raise RuntimeError(errors)
            print(label, "JIT/native OK")


if __name__ == "__main__":
    main()
