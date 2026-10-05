#!/usr/bin/env python3
"""Local streaming HTTP/HTTPS acceptance, including 32 concurrent exchanges."""
import argparse
import pathlib
import shutil
import socket
import socketserver
import ssl
import subprocess
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATTERN = b"Gloin\0" * 21846
CASES = ("/upload", "/early", "/input-wait", "/input-timeout", "/output-timeout",
         "/backpressure", "/cancel", "/short", "/excess", "/truncated")


class Peer(socketserver.BaseRequestHandler):
    def handle(self):
        stream = self.request
        path = "handshake"
        try:
            stream.settimeout(35)
            if self.server.context:
                stream = self.server.context.wrap_socket(stream, server_side=True)
            data = b""
            while b"\r\n\r\n" not in data:
                block = stream.recv(2048)
                assert block, "closed before head"
                data += block
                assert len(data) <= 8192
            head, body = data.split(b"\r\n\r\n", 1)
            lines = head.split(b"\r\n")
            method, target, version = lines[0].split(b" ")
            path = target.decode()
            assert version == b"HTTP/1.1"
            fields = dict(line.lower().split(b": ", 1) for line in lines[1:])
            assert fields[b"connection"] == b"close"
            size = int(fields[b"content-length"])
            if path in ("/stall", "/input-timeout", "/short", "/excess"):
                assert stream.recv(1) == b"", "cancel/deadline did not close transport"
                return
            if path == "/input-wait":
                time.sleep(0.04)  # The upload producer has no data yet.
            if path in ("/early", "/input-wait"):
                stream.sendall(b"HTTP/1.1 413 Too Large\r\nContent-Length: 3\r\n\r\naaa")
                total = len(body)
                while block := stream.recv(8192):
                    total += len(block)
                assert total < size, ("uploaded entire rejected body", total)
                return
            assert method == (b"POST" if path == "/upload" else b"GET")
            if path == "/upload":
                assert size == 1048576
                assert body == b"a" * len(body)
                count = len(body)
                while count < size:
                    block = stream.recv(8192)
                    assert block and block == b"a" * len(block)
                    count += len(block)
                assert count == size
                payload = b"a" * size
            elif path in ("/fixed", "/chunked"):
                payload = PATTERN
            elif path == "/backpressure":
                payload = b"a" * 10003
            else:
                payload = b"aaa"
            if path in ("/chunked", "/truncated"):
                stream.sendall(b"HTTP/1.1 103 Early Hints\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
                for offset in range(0, len(payload), 4093):
                    block = payload[offset:offset + 4093]
                    stream.sendall(f"{len(block):x};x=ok\r\n".encode() + block + b"\r\n")
                if path == "/truncated":
                    # Deliver a valid prefix then clean EOF without the terminal chunk.
                    if self.server.context:
                        stream = stream.unwrap()
                    else:
                        stream.shutdown(socket.SHUT_WR)
                else:
                    stream.sendall(b"0\r\nX-End: yes\r\n\r\n")
            else:
                stream.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(payload)}\r\n\r\n".encode())
                for offset in range(0, len(payload), 4093):
                    stream.sendall(payload[offset:offset + 4093])
            assert stream.recv(1) == b"", "completed exchange did not close transport"
        except Exception as exc:
            self.server.errors.append((path, repr(exc)))
        finally:
            stream.close()
            with self.server.condition:
                self.server.finished += 1
                self.server.condition.notify_all()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    request_queue_size = 64

    def __init__(self, context):
        self.context = context
        self.errors = []
        self.finished = 0
        self.condition = threading.Condition()
        super().__init__(("127.0.0.1", 0), Peer)


def compile_source(compiler, source, destination, port):
    text = source.read_text()
    assert "def const PORT: u16 = 8080;" in text
    path = destination.with_suffix(".gloin")
    path.write_text(text.replace("def const PORT: u16 = 8080;", f"def const PORT: u16 = {port};"))
    subprocess.run([str(compiler), "-o", str(destination), str(path)], check=True, timeout=60)
    return path


def run(server, command, count, expected=b""):
    target = server.finished + count
    result = subprocess.run(command, capture_output=True, timeout=65)
    with server.condition:
        server.condition.wait_for(lambda: server.finished >= target, timeout=4)
    assert result.returncode == 0 and result.stdout == expected and not result.stderr, (
        command, result.returncode, result.stdout, result.stderr)
    assert not server.errors, server.errors
    assert server.finished == target, ("unclosed peer", server.finished, target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=pathlib.Path)
    parser.add_argument("--example", type=pathlib.Path, default=ROOT / "examples/http_stream.gloin")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    with tempfile.TemporaryDirectory(prefix="gloin-stream-") as directory:
        temp = pathlib.Path(directory)
        cert, key = temp / "cert.pem", temp / "key.pem"
        candidates = (pathlib.Path("/opt/homebrew/opt/openssl@3/bin/openssl"),
                      pathlib.Path("/usr/local/opt/openssl@3/bin/openssl"))
        openssl = next((str(p) for p in candidates if p.is_file()), shutil.which("openssl"))
        if not openssl:
            raise RuntimeError("OpenSSL is required for local HTTPS fixtures")
        subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
                        "-days", "1", "-keyout", str(key), "-out", str(cert), "-subj", "/CN=localhost",
                        "-addext", "subjectAltName=DNS:localhost"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        for tls in (False, True):
            with Server(context if tls else None) as server:
                worker = threading.Thread(target=server.serve_forever, daemon=True)
                worker.start()
                try:
                    port = server.server_address[1]
                    executable = temp / "cases"
                    source = compile_source(compiler, ROOT / "tests/fixtures/network/http_stream_cases.gloin", executable, port)
                    for mode in ("jit", "native"):
                        command = [str(compiler), "--jit", str(source), "--"] if mode == "jit" else [str(executable)]
                        run(server, command + ["tls" if tls else "plain", str(cert)], len(CASES))
                    executable = temp / "concurrent"
                    source = compile_source(compiler, args.example, executable, port)
                    for mode in ("jit", "native"):
                        command = [str(compiler), "--jit", str(source), "--"] if mode == "jit" else [str(executable)]
                        if tls:
                            command += ["tls", str(cert)]
                        run(server, command, 32, b"31 streamed downloads verified; stalled exchange cancelled\n")
                    print(("HTTPS" if tls else "HTTP") + " streaming, early response, backpressure, cancellation, deadlines, 32 exchanges: JIT/native OK", flush=True)
                finally:
                    server.shutdown()
                    worker.join(timeout=2)


if __name__ == "__main__":
    main()
