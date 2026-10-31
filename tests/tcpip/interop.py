#!/usr/bin/env python3
"""Independent kernel TCP + Python HTTP/TLS peer. Run in a private Linux netns."""
import argparse
import concurrent.futures
import contextlib
import json
import os
import statistics
import threading
import pathlib
import socket
import ssl
import subprocess
import tempfile
import time

HOST = "10.0.0.2"
PORT = 8080
TEXT = b"Gloin TCP/IP server\n"
LARGE = bytes(i % 251 for i in range(65536))


def command(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def response(sock, head=False):
    header = bytearray()
    while not header.endswith(b"\r\n\r\n"):
        data = sock.recv(1)
        assert data, ("truncated HTTP head", bytes(header))
        header.extend(data)
        assert len(header) <= 16384
    lines = bytes(header).split(b"\r\n")
    code = int(lines[0].split(b" ")[1])
    fields = {}
    for line in lines[1:-2]:
        name, value = line.split(b":", 1)
        name = name.lower()
        assert name not in fields, ("duplicate header", name)
        fields[name] = value.strip()
    length = 0 if head or code < 200 or code in (204, 304) else int(fields[b"content-length"])
    body = bytearray()
    while len(body) < length:
        data = sock.recv(min(length - len(body), 16384))
        assert data, ("truncated HTTP body", len(body), length)
        body.extend(data)
    return code, fields, bytes(body)


def connect(context=None):
    sock = socket.create_connection((HOST, PORT), timeout=10)
    sock.settimeout(10)
    if context:
        sock = context.wrap_socket(sock, server_hostname="localhost")
    return sock


def close(sock):
    if isinstance(sock, ssl.SSLSocket):
        sock = sock.unwrap()
    with contextlib.suppress(OSError):
        sock.shutdown(socket.SHUT_WR)
    assert sock.recv(1) == b"", "unexpected bytes after final response"
    sock.close()


def request(sock, target=b"/", close_after=False, method=b"GET"):
    sock.sendall(method + b" " + target + b" HTTP/1.1\r\nHost: localhost\r\n" +
                 (b"Connection: close\r\n" if close_after else b"") + b"\r\n")
    code, fields, body = response(sock, method == b"HEAD")
    assert code == 200
    expected = LARGE if target == b"/bytes" else TEXT
    assert fields[b"content-length"] == str(len(expected)).encode()
    assert body == (b"" if method == b"HEAD" else expected)


def checks(context):
    s = connect(context)
    request(s)
    request(s, b"/bytes")
    request(s, b"/bytes", method=b"HEAD")
    payload = bytes(range(256)) * 13
    s.sendall(b"POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nExpect: 100-continue\r\n\r\n")
    assert response(s)[0] == 100
    for offset in range(0, len(payload), 37):
        chunk = payload[offset:offset + 37]
        s.sendall(f"{len(chunk):x};name=value\r\n".encode() + chunk + b"\r\n")
    s.sendall(b"0\r\nX-Trace: done\r\n\r\n" +
              b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n" +
              b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    assert response(s)[2] == payload
    assert response(s)[2] == TEXT
    code, fields, body = response(s)
    assert code == 200 and body == TEXT and fields[b"connection"] == b"close"
    close(s)

    # A stalled header and a stalled consumer cannot hold up independent clients.
    slow = connect(context)
    slow.sendall(b"GET / HTTP/1.1\r\nHost:")
    stalled = connect(context)
    stalled.sendall(b"GET /bytes HTTP/1.1\r\nHost: localhost\r\n\r\n")
    def client(index):
        sock = connect(context)
        for _ in range(6):
            request(sock, b"/bytes" if index % 3 == 0 else b"/")
        request(sock, close_after=True)
        close(sock)
    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
        list(pool.map(client, range(24)))
    slow.close()
    stalled.close()

    # More connections than active slots: full TCP buffers must be reusable while
    # the old four-tuples remain protected in the separate TIME_WAIT pool.
    for _ in range(100):
        sock = connect(context)
        request(sock, close_after=True)
        close(sock)

    # Ambiguous request framing closes the offending connection only.
    bad = connect(context)
    bad.sendall(b"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
    try:
        assert bad.recv(1) == b""
    except (ConnectionResetError, ssl.SSLError):
        pass
    bad.close()
    good = connect(context)
    request(good, close_after=True)
    close(good)


class BufferedPeer:
    def __init__(self, sock):
        self.sock = sock
        self.buffer = bytearray()

    def get(self, target, expected):
        self.sock.sendall(b"GET " + target + b" HTTP/1.1\r\nHost: localhost\r\n\r\n")
        while b"\r\n\r\n" not in self.buffer:
            data = self.sock.recv(16384)
            assert data
            self.buffer.extend(data)
        end = self.buffer.index(b"\r\n\r\n") + 4
        header = bytes(self.buffer[:end])
        assert header.startswith(b"HTTP/1.1 200 OK\r\n")
        assert b"Content-Length: " + str(len(expected)).encode() + b"\r\n" in header
        total = end + len(expected)
        while len(self.buffer) < total:
            data = self.sock.recv(min(65536, total - len(self.buffer)))
            assert data
            self.buffer.extend(data)
        assert self.buffer[end:total] == expected
        del self.buffer[:total]


def metrics(pid):
    fields = pathlib.Path(f"/proc/{pid}/stat").read_text().split()
    ticks = os.sysconf("SC_CLK_TCK")
    rss = {}
    for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith(("VmRSS:", "VmHWM:")):
            key, value, _ = line.split()
            rss[key[:-1]] = int(value) * 1024
    return {"cpu_seconds": (int(fields[13]) + int(fields[14])) / ticks, **rss}


def benchmark(context, pid, journal):
    samples = []
    for target, expected, count in [(b"/", TEXT, 512), (b"/bytes", LARGE, 64)]:
        for concurrency in (1, 16):
            for trial in range(4):
                timing = {}
                def start_timing():
                    timing["before"] = metrics(pid)
                    timing["client_cpu"] = time.process_time()
                    timing["started"] = time.perf_counter()
                def stop_timing():
                    timing["elapsed"] = time.perf_counter() - timing["started"]
                    timing["client_cpu"] = time.process_time() - timing["client_cpu"]
                    timing["after"] = metrics(pid)
                barrier = threading.Barrier(concurrency + 1, action=start_timing)
                finished = threading.Barrier(concurrency + 1, action=stop_timing)
                def client(_):
                    sock = connect(context)
                    peer = BufferedPeer(sock)
                    peer.get(target, expected)
                    barrier.wait(timeout=20)
                    latencies = []
                    for _ in range(count):
                        start = time.perf_counter_ns()
                        peer.get(target, expected)
                        latencies.append(time.perf_counter_ns() - start)
                    finished.wait(timeout=30)
                    request(sock, close_after=True)
                    close(sock)
                    return latencies
                with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as pool:
                    futures = [pool.submit(client, i) for i in range(concurrency)]
                    try:
                        barrier.wait(timeout=20)
                        finished.wait(timeout=30)
                    except threading.BrokenBarrierError:
                        # Report a failed client's original exception, not just
                        # a peer's barrier timeout, and never wait indefinitely.
                        barrier.abort()
                        finished.abort()
                        for future in futures:
                            try:
                                future.result(timeout=15)
                            except threading.BrokenBarrierError:
                                continue
                        raise
                    latencies = [v for f in futures for v in f.result(timeout=15)]
                    before, after = timing["before"], timing["after"]
                    elapsed, client_cpu = timing["elapsed"], timing["client_cpu"]
                latencies.sort()
                samples.append({"target": target.decode(), "concurrency": concurrency, "trial": trial,
                    "warmup": trial == 0, "requests": len(latencies), "seconds": elapsed,
                    "requests_per_second": len(latencies) / elapsed,
                    "body_mib_per_second": len(latencies) * len(expected) / elapsed / 1048576,
                    "p50_ms": statistics.median(latencies) / 1e6,
                    "p95_ms": latencies[int(len(latencies) * .95)] / 1e6,
                    "server_cpu_seconds": after["cpu_seconds"] - before["cpu_seconds"],
                    "client_cpu_seconds": client_cpu, "server_rss_bytes": after["VmRSS"],
                    "server_peak_rss_bytes": after["VmHWM"]})
                with journal.open("a") as stream:
                    stream.write(json.dumps(samples[-1]) + "\n")
                print(journal.stem, target.decode(), concurrency, trial, "done", flush=True)
    return samples


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--reports", required=True)
    parser.add_argument("--benchmark", action="store_true")
    args = parser.parse_args()
    assert os.readlink("/proc/self/ns/net") != os.readlink("/proc/1/ns/net"), "run under unshare --net"
    reports = pathlib.Path(args.reports)
    reports.mkdir(parents=True, exist_ok=True)
    command("ip", "link", "set", "lo", "up")
    command("ip", "tuntap", "add", "dev", "gloin0", "mode", "tun")
    command("ip", "addr", "add", "10.0.0.1/24", "dev", "gloin0")
    command("ip", "link", "set", "gloin0", "up")
    with tempfile.TemporaryDirectory(prefix="gloin-tcpip-cert-") as temp:
        cert, key = f"{temp}/cert.pem", f"{temp}/key.pem"
        command("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost", "-out", cert, "-keyout", key)
        results = {}
        for secure in (False, True):
            label = "https" if secure else "http"
            context = ssl.create_default_context(cafile=cert) if secure else None
            log = open(reports / f"{label}.log", "wb")
            server = subprocess.Popen([args.server, "gloin0"] + ([cert, key] if secure else []), stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 10
                while True:
                    assert server.poll() is None, ("server exited", server.returncode)
                    try:
                        s = connect(context)
                        request(s, close_after=True)
                        close(s)
                        break
                    except (OSError, AssertionError):
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.05)
                started = time.monotonic()
                checks(context)
                assert server.poll() is None
                results[label] = {"passed": True, "seconds": time.monotonic() - started}
                if args.benchmark:
                    results[label]["benchmark"] = benchmark(context, server.pid, reports / f"{label}-samples.jsonl")
                print(label, "passed", flush=True)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
                log.close()
        (reports / "interop.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
