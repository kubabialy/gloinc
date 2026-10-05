#!/usr/bin/env python3
"""Deterministic local HTTP/HTTPS peers; no public services or credentials."""
import argparse
import pathlib
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time

SOURCE = r'''import "@http";
import "@http_client";
import "@net";
import "@process";
import "@arena";
import "@status";
import "@std";
import "@strings";

def run_case(path: string, tls: bool, trust_file: string) -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def mut method: string = "POST";
    def mut body_text: string = "a\0b";
    if strings.equal(path, "/head") { method = "HEAD"; body_text = ""; }
    if strings.equal(path, "/large") {
        def repeated: strings.StringResult = strings.repeat(&owner, "a\0b", 10000, 30000);
        if repeated.status != status.OK { return 1; }
        body_text = repeated.value;
    }
    def mut hostname: string = "localhost";
    if strings.equal(path, "/wrong-host") { hostname = "wrong.invalid"; }
    def mut timeout: i32 = 10000;
    if strings.equal(path, "/timeout") || strings.equal(path, "/slow") { timeout = 500; }
    def mut trust: string = trust_file;
    if strings.equal(path, "/untrusted") { trust = ""; }
    def endpoint: http_client.Endpoint = http_client.Endpoint {
        address: net.Address.loopback(__PORT__),
        tls: tls,
        hostname: hostname,
        trust_file: trust
    };
    def request: http_client.Request = http_client.Request {
        method: method, host: "localhost", target: path, body: body_text
    };
    def fields: [http.Header; 2] = {
        http.Header { name: "Authorization", value: "Bearer fixture" },
        http.Header { name: "Content-Type", value: "application/octet-stream" }
    };
    def mut head: [u8; 2048] = zeroed;
    def mut body: [u8; 65536] = zeroed;
    def received: result<http.Response> = http_client.request(
        endpoint, request, fields[..], head[..], body[..], 1024, 4096, timeout);
    def failure: bool = strings.starts_with(path, "/bad") ||
        strings.equal(path, "/timeout") || strings.equal(path, "/slow") ||
        strings.equal(path, "/wrong-host") || strings.equal(path, "/untrusted");
    if received.erroneous {
        if !failure { std.println(received.error.message); return 2; }
        if (strings.equal(path, "/timeout") || strings.equal(path, "/slow")) &&
            !strings.equal(received.error.message, "HTTP request timed out") { return 3; }
        return 0;
    }
    if failure { return 4; }
    def response: http.Response = received.value;
    def mut expected_body: string = "a\0b";
    def mut expected_code: i32 = 200;
    if strings.equal(path, "/large") { expected_body = body_text; }
    if strings.equal(path, "/head") { expected_body = ""; }
    if strings.equal(path, "/no-content") { expected_body = ""; expected_code = 204; }
    if strings.equal(path, "/not-modified") { expected_body = ""; expected_code = 304; }
    if strings.equal(path, "/reset") { expected_body = ""; expected_code = 205; }
    if strings.equal(path, "/error-status") { expected_code = 429; }
    if response.code != expected_code || !strings.equal(response.body, expected_body) { return 5; }
    return 0;
}

def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def mode: process.TextResult = process.arg(&owner, 1);
    def trust: process.TextResult = process.arg(&owner, 2);
    if mode.status != status.OK || trust.status != status.OK { return 1; }
    def tls: bool = strings.equal(mode.value, "tls");
    def cases: [string; 17] = {
        "/fixed", "/chunked", "/large", "/close", "/head", "/no-content", "/not-modified",
        "/reset", "/error-status", "/bad-truncated", "/bad-ambiguous", "/bad-large",
        "/bad-chunk", "/timeout", "/slow", "/wrong-host", "/untrusted"
    };
    def mut count: u64 = 15;
    if tls { count = 17; }
    for def mut i: u64 = 0; i < count; i = i + 1 {
        def code: i32 = run_case(cases[i], tls, trust.value);
        if code != 0 {
            std.println(cases[i]);
            return code;
        }
    }
    return 0;
}

'''

CASES = ("/fixed", "/chunked", "/large", "/close", "/head", "/no-content", "/not-modified",
         "/reset", "/error-status", "/bad-truncated", "/bad-ambiguous",
         "/bad-large", "/bad-chunk", "/timeout", "/slow")


def response(path):
    heads = {
        "/fixed": b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b",
        "/chunked": (b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\n\r\n"
                     b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                     b"1;name=\"value\"\r\na\r\n2\r\n\0b\r\n0\r\nX-End: yes\r\n\r\n"),
        "/large": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n7530\r\n" +
                  b"a\0b" * 10000 + b"\r\n0\r\n\r\n",
        "/close": b"HTTP/1.1 200 OK\r\n\r\na\0b",
        "/head": b"HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n",
        "/no-content": b"HTTP/1.1 204 No Content\r\n\r\n",
        "/not-modified": b"HTTP/1.1 304 Not Modified\r\nContent-Length: 999\r\n\r\n",
        "/reset": b"HTTP/1.1 205 Reset\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "/error-status": b"HTTP/1.1 429 Too Many Requests\r\nContent-Length: 3\r\n\r\na\0b",
        "/bad-truncated": b"HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\na\0b",
        "/bad-ambiguous": b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n",
        "/bad-large": b"HTTP/1.1 200 OK\r\nContent-Length: 65537\r\n\r\n",
        "/bad-chunk": b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n",
        "/echo": b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"
                  b"b\r\n{\"ok\":true}\r\n0\r\n\r\n",
    }
    return heads[path]


def peer(listener, context, path, errors, example=False):
    stream = None
    stage = "accept (includes JIT startup)"
    try:
        conn, _ = listener.accept()
        conn.settimeout(15)
        stream = conn
        if context:
            try:
                stream = context.wrap_socket(conn, server_side=True)
            except ssl.SSLError:
                if path in ("/wrong-host", "/untrusted"):
                    return
                raise
        stage = "request head"
        data = b""
        while b"\r\n\r\n" not in data:
            block = stream.recv(4096)
            if not block:
                raise AssertionError("client closed before request head")
            data += block
            if len(data) > 8192:
                raise AssertionError("request too long")
        head, body = data.split(b"\r\n\r\n", 1)
        lines = head.split(b"\r\n")
        assert lines[0] == ((b"HEAD " if path == "/head" else b"POST ") + path.encode() + b" HTTP/1.1")
        fields = {}
        for line in lines[1:]:
            key, value = line.split(b":", 1)
            assert key.lower() not in fields
            fields[key.lower()] = value.strip()
        assert fields[b"connection"] == b"close"
        if not example:
            assert fields[b"authorization"] == b"Bearer fixture"
        size = int(fields[b"content-length"])
        stage = "request body"
        while len(body) < size:
            block = stream.recv(4096)
            if not block:
                raise AssertionError("client closed before request body")
            body += block
        expected = b'{"message":"hello from Gloin"}' if example else (b"" if path == "/head" else b"a\0b")
        if path == "/large":
            expected = b"a\0b" * 10000
        assert body == expected
        stage = "response and close"
        if path == "/timeout":
            assert stream.recv(1) == b"", "client did not close on deadline"
            return
        if path == "/slow":
            try:
                for byte in response("/fixed"):
                    stream.sendall(bytes([byte]))
                    time.sleep(0.04)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                return
            raise AssertionError("slow peer exceeded the overall deadline")
        payload = response(path)
        # Arbitrary write boundaries are supplemented by exhaustive decoder splits.
        stride = 4093 if path == "/large" else 7
        for offset in range(0, len(payload), stride):
            try:
                stream.sendall(payload[offset:offset + stride])
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                if path.startswith("/bad"):
                    return
                raise
        if path in ("/close", "/bad-truncated"):
            if context:
                stream = stream.unwrap()  # Send authenticated close_notify.
            else:
                stream.shutdown(socket.SHUT_WR)
        # Client must release the connection after success as well as failure.
        assert stream.recv(1) == b"", "connection remained open after request"
    except (ConnectionResetError, ssl.SSLError) as exc:
        if not path.startswith("/bad"):
            errors.append((stage, repr(exc)))
    except Exception as exc:
        errors.append((stage, repr(exc)))
    finally:
        if stream is not None:
            stream.close()


def run_cases(compiler, source, native, listener, context, paths, trust, mode, example=False):
    errors = []

    def serve():
        for path in paths:
            peer(listener, context, path, errors, example)
            if errors:
                return

    worker = threading.Thread(target=serve, daemon=True)
    worker.start()
    arguments = (["tls", trust] if context else []) if example else ["tls" if context else "plain", trust]
    command = ([str(compiler), "--jit", str(source), "--"] if mode == "jit" else [str(native)]) + arguments
    run = subprocess.run(command, capture_output=True, timeout=90)
    worker.join(timeout=16)
    if worker.is_alive():
        raise AssertionError((paths, mode, run.returncode, run.stdout, run.stderr, "peer did not finish"))
    expected = b'200\napplication/json\n{"ok":true}\n' if example else b""
    if run.returncode or run.stdout != expected or run.stderr or errors:
        raise AssertionError((paths, mode, run.returncode, run.stdout, run.stderr, errors))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=pathlib.Path)
    parser.add_argument("--example", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / "examples/http_client.gloin")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    with tempfile.TemporaryDirectory(prefix="gloin-http-") as directory:
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
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(8)
            listener.settimeout(45)
            port = listener.getsockname()[1]
            source, native = temp / "client.gloin", temp / "client"
            source.write_text(SOURCE.replace("__PORT__", str(port)))
            subprocess.run([str(compiler), "-o", str(native), str(source)], check=True, timeout=60)
            for tls in (False, True):
                cases = CASES + (("/wrong-host", "/untrusted") if tls else ())
                for mode in ("jit", "native"):
                    run_cases(compiler, source, native, listener, context if tls else None,
                              cases, str(cert), mode)
                print(("HTTPS" if tls else "HTTP") + " framing, deadlines, and cleanup: JIT/native OK", flush=True)
            example, executable = temp / "example.gloin", temp / "example"
            text = args.example.read_text()
            assert "def const PORT: u16 = 8080;" in text
            example.write_text(text.replace("def const PORT: u16 = 8080;", f"def const PORT: u16 = {port};"))
            subprocess.run([str(compiler), "-o", str(executable), str(example)], check=True, timeout=60)
            for tls in (False, True):
                for mode in ("jit", "native"):
                    run_cases(compiler, example, executable, listener, context if tls else None,
                              ("/echo",), str(cert), mode, example=True)
            print("HTTP client example: HTTP/HTTPS JIT/native OK", flush=True)


if __name__ == "__main__":
    main()
