#include "support/cli_fixture.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <vector>

namespace {
class NetworkLibraryTest : public gloin_test::CliFixture {
  protected:
    void SetUp() override {
        CliFixture::SetUp();
        // Compiling the complete HTTP module can exceed ten seconds when several
        // JIT cases run together, including against cold relocated packages.
        timeout_seconds = 60;
    }
};
} // namespace

TEST_F(NetworkLibraryTest, ServerTlsCallsRetainTheGloinIRAbiBoundary) {
    const auto fixture =
        std::filesystem::path(__FILE__).parent_path() / "fixtures/network/tls_sessions.gloin";
    const auto ir = invoke({"--emit-ir", fixture.string()});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const std::string name :
         {"gloin_net_tls_server_config", "gloin_net_tls_server_config_close",
          "gloin_net_tls_server_create", "gloin_net_tls_shutdown"}) {
        const auto position = ir.out.find("callee = @" + name + "}");
        ASSERT_NE(position, std::string::npos) << name;
        const auto line = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(line + 1, position - line).find("gloin.abi_call"),
                  std::string::npos)
            << name;
        EXPECT_EQ(ir.out.find("llvm.call @" + name), std::string::npos) << name;
    }
}

TEST_F(NetworkLibraryTest, ServerTlsSetupFailuresAndPrimitivePrivacyAreChecked) {
    const auto program = source(R"(import "@net"; import "@arena";
def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def missing: result<net.TlsServerConfig> = net.TlsServerConfig.load(&owner, "", "");
    if !missing.erroneous { return 1; }
    def opened: result<net.Socket> = net.Socket.open(&owner);
    if opened.erroneous { return 2; }
    def mut socket: net.Socket = opened.value;
    def tls: result<net.TlsClient> = socket.start_tls_client(&owner, "localhost", "");
    if !tls.erroneous { return 3; }
    def closed: result<void> = socket.close();
    if closed.erroneous { return 4; }
    return 0;
})");
    expect_success(invoke({program}), "");
    for (const std::string body :
         {"__net_tls_server_config_close(null);",
          "def loaded: result<net.TlsServerConfig> = net.TlsServerConfig.load(&owner, 1, 2); if "
          "loaded.erroneous { return 1; }",
          "def opened: result<net.Socket> = net.Socket.open(&owner); if opened.erroneous { return "
          "1; } def mut socket: net.Socket = opened.value; socket.start_tls_server(&owner, 1);"}) {
        const auto invalid =
            source("import \"@net\"; import \"@arena\"; def main() -> i32 { "
                   "def mut owner: arena.GeneralArena = arena.GeneralArena.create(); " +
                   body + " return 0; }");
        const auto result = invoke({"--check", invalid});
        EXPECT_EQ(result.status, 1);
        EXPECT_FALSE(result.err.empty());
    }
}

TEST_F(NetworkLibraryTest, LocalHttpRoundTripWorksInJitAndNativeExecutable) {
    const auto example =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "examples/network_http.gloin";
    expect_success(invoke({example.string()}), "nonblocking HTTP round trip succeeded\n");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/network-http";
        expect_success(invoke_raw({mode, "-o", output, example.string()}), "");
        const auto stdout_path = directory + "/native.stdout";
        const auto stderr_path = directory + "/native.stderr";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, stdout_path, stderr_path};
        std::string message;
        bool launch_failed = false;
        const auto status = llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, redirects, 10,
                                                      0, &message, &launch_failed);
        EXPECT_FALSE(launch_failed) << message;
        EXPECT_EQ(status, 0);
        EXPECT_EQ(read(stdout_path), "nonblocking HTTP round trip succeeded\n");
        EXPECT_EQ(read(stderr_path), "");
    }
}

TEST_F(NetworkLibraryTest, HttpRejectsAmbiguousFramingAndHeaderInjection) {
    const auto file =
        source(R"(import "@arena"; import "@http"; import "@status"; import "@strings";
def main() -> i32 {
    def partial: result<http.RequestHead> = http.parse_request("GET / HTTP/1.1\r\nHost: local", 128);
    if partial.erroneous { return 1; }
    if partial.value.complete { return 2; }
    def clash: result<http.RequestHead> = http.parse_request(
        "POST / HTTP/1.1\r\nHost: local\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n", 128);
    if !clash.erroneous { return 3; }
    def chunked: result<http.RequestHead> = http.parse_request(
        "POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n", 128);
    if !chunked.erroneous { return 4; }
    def too_long: result<http.RequestHead> = http.parse_request(
        "GET / HTTP/1.1\r\nHost: local\r\n\r\n", 8);
    if !too_long.erroneous { return 5; }
    def bare_lf: result<http.RequestHead> = http.parse_request(
        "GET / HTTP/1.1\nHost: local\r\n\r\n", 128);
    if !bare_lf.erroneous { return 6; }
    def partial_body: result<http.Frame> = http.frame_request(
        "POST / HTTP/1.1\r\nHost: local\r\nContent-Length: 3\r\n\r\nab", 128, 3);
    if partial_body.erroneous { return 13; }
    if partial_body.value.complete { return 14; }
    def framed_text: string = "POST / HTTP/1.1\r\nHost: local\r\nContent-Length: 3\r\n\r\nabcNEXT";
    def framed: result<http.Frame> = http.frame_request(framed_text, 128, 3);
    if framed.erroneous { return 15; }
    if !framed.value.complete || !strings.equal(framed.value.body, "abc") { return 16; }
    def following: result<string> = strings.slice_bytes(framed_text, framed.value.consumed, 4);
        if following.erroneous { return 97; }
    if following.erroneous || !strings.equal(following.value, "NEXT") { return 17; }
    def oversized_body: result<http.Frame> = http.frame_request(framed_text, 128, 2);
    if !oversized_body.erroneous { return 18; }
    def unframed_response: result<http.Frame> = http.frame_response(
        "HTTP/1.1 200 OK\r\n\r\n", 128, 128);
    if !unframed_response.erroneous { return 19; }
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    def bad: result<string> = http.format_get(&memory, "local\r\nInjected: yes", "/", 128);
    if !bad.erroneous { memory.free(); return 7; }
    def built: result<string> = http.format_get(&memory, "local", "/", 128);
    if built.erroneous { memory.free(); return 8; }
    def exact: u64 = strings.byte_length(built.value);
    def at_limit: result<string> = http.format_get(&memory, "local", "/", exact);
    if at_limit.erroneous { memory.free(); return 9; }
    def below_limit: result<string> = http.format_get(&memory, "local", "/", exact - 1);
    if !below_limit.erroneous { memory.free(); return 10; }
    def no_body: result<string> = http.format_response(
        &memory, 204, "No Content", "text/plain", 0, 128);
    if !no_body.erroneous { memory.free(); return 11; }
    def parsed_no_body: result<http.ResponseHead> = http.parse_response(
        "HTTP/1.1 204 No Content\r\n\r\n", 128);
    if parsed_no_body.erroneous { memory.free(); return 12; }
    if !parsed_no_body.value.complete || parsed_no_body.value.code != 204 {
        memory.free(); return 20;
    }
    memory.free();
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, SocketAliasesObserveCloseAndEmptyWriteStillChecksLiveness) {
    const auto file = source(R"(import "@arena"; import "@net";
def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    def address: result<net.Address> = net.Address.ipv4(127, 0, 0, 1, 80);
    if address.erroneous { owner.free(); return 1; }
    def expected: u32 = 2130706433;
    if address.value.host != expected { owner.free(); return 2; }
    def bad_address: result<net.Address> = net.Address.ipv4(256, 0, 0, 1, 80);
    if !bad_address.erroneous { owner.free(); return 3; }
    def mut hosts: [u32; 2] = zeroed;
    def lookup: result<u64> = net.resolve_ipv4("127.0.0.1", hosts[..]);
    if lookup.erroneous { owner.free(); return 10; }
    def one: u64 = 1;
    if lookup.value != one || hosts[0] != expected { owner.free(); return 11; }
    def bad_host: result<u64> = net.resolve_ipv4("lo\0calhost", hosts[..]);
    if !bad_host.erroneous { owner.free(); return 12; }
    def opened: result<net.Socket> = net.Socket.open(&owner);
    if opened.erroneous { owner.free(); return 4; }
    def mut socket: net.Socket = opened.value;
    def reusable: result<void> = socket.set_reuse_address(true);
    if reusable.erroneous { owner.free(); return 8; }
    def mut alias: net.Socket = socket;
    def closed: result<void> = socket.close();
    if closed.erroneous { owner.free(); return 5; }
    if alias.is_open() { owner.free(); return 6; }
    def attempted: result<net.WriteOutcome> = alias.write_text("");
    if !attempted.erroneous { owner.free(); return 7; }
    def reuse_after_close: result<void> = alias.set_reuse_address(false);
    if !reuse_after_close.erroneous { owner.free(); return 9; }
    owner.free();
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, RequestHeadersHaveExactBoundsAndPreventFramingInjection) {
    const auto file = source(R"(import "@arena"; import "@http"; import "@strings";
def main() -> i32 {
    def mut owner: arena.GeneralArena = arena.GeneralArena.create();
    defer owner.free();
    def headers: [http.Header; 2] = {
        http.Header { name: "Authorization", value: "Bearer test" },
        http.Header { name: "X!#$%&'*+-.^_`|~", value: "ok" }
    };
    def made: result<string> = http.format_request(&owner, "POST", "localhost:8080",
        "/echo?q=1", headers[..], 3, 1024);
    if made.erroneous { return 1; }
    def text: string = made.value;
    if !strings.contains(text, "Content-Length: 3\r\n") ||
        !strings.contains(text, "Authorization: Bearer test\r\n") { return 2; }
    def exact: result<string> = http.format_request(&owner, "POST", "localhost:8080",
        "/echo?q=1", headers[..], 3, strings.byte_length(text));
    if exact.erroneous { return 3; }
    def short: result<string> = http.format_request(&owner, "POST", "localhost:8080",
        "/echo?q=1", headers[..], 3, strings.byte_length(text) - 1);
    if !short.erroneous { return 4; }
    def names: [string; 8] = {"Host", "content-length", "TRANSFER-ENCODING", "Connection",
        "Trailer", "TE", "Upgrade", "Expect"};
    for def mut i: u64 = 0; i < 8; i = i + 1 {
        def bad: [http.Header; 1] = {http.Header { name: names[i], value: "x" }};
        def rejected: result<string> = http.format_request(&owner, "POST", "local", "/", bad[..], 0, 1024);
        if !rejected.erroneous { return 5; }
    }
    def injected: [http.Header; 1] = {http.Header { name: "X-Test", value: "yes\r\nEvil: true" }};
    def rejected: result<string> = http.format_request(&owner, "POST", "local", "/", injected[..], 0, 1024);
    if !rejected.erroneous { return 6; }
    def fields: result<http.Headers> = http.headers(text, 1024);
    if fields.erroneous { return 7; }
    def mut iterator: http.Headers = fields.value;
    def mut field: http.HeaderItem = iterator.next();
    def mut found: bool = false;
    while field.available {
        if http.header_name_equal(field.field.name, "authorization") {
            found = strings.equal(field.field.value, "Bearer test");
        }
        field = iterator.next();
    }
    if !found { return 8; }
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, ResponseReaderHandlesEverySplitAndPreservesFollowingBytes) {
    const auto file = source(R"(import "@http"; import "@strings";
def verify(wire: string, expected: string, method: string, eof: bool) -> i32 {
    for def mut split: u64 = 0; split <= strings.byte_length(wire); split = split + 1 {
        def mut head: [u8; 256] = zeroed;
        def mut body: [u8; 16] = zeroed;
        def made: result<http.ResponseReader> = http.ResponseReader.create(head[..], body[..], method, 1024);
        if made.erroneous { return 1; }
        def mut reader: http.ResponseReader = made.value;
        def first: result<http.ResponseProgress> = reader.feed((match strings.slice_bytes(wire, 0, split) { case value(v) => v; case error(e) => ""; }), false);
        if first.erroneous { return 2; }
        def second: result<http.ResponseProgress> = reader.feed((match strings.slice_bytes(wire, split,
            strings.byte_length(wire) - split) { case value(v) => v; case error(e) => ""; }), eof);
        if second.erroneous { return 3; }
        if !second.value.complete { return 4; }
        def response: result<http.Response> = reader.response();
        if response.erroneous { return 5; }
        if !strings.equal(response.value.body, expected) { return 6; }
    }
    return 0;
}
def main() -> i32 {
    if verify("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b", "a\0b", "POST", true) != 0 { return 1; }
    if verify("HTTP/1.1 103 Hints\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n"
        , "", "GET", true) == 0 { return 2; }
    def wire: string = "HTTP/1.1 103 Hints\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n3;foo=\"b\\\"ar\";flag\r\na\0b\r\n0\r\nX-End: yes\r\n\r\n";
    if verify(wire, "a\0b", "GET", true) != 0 { return 3; }
    if verify("HTTP/1.1 200 OK\r\n\r\nabc", "abc", "GET", true) != 0 { return 4; }
    if verify("HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n", "", "HEAD", false) != 0 { return 5; }
    if verify("HTTP/1.1 204 No Content\r\n\r\n", "", "GET", false) != 0 { return 6; }
    if verify("HTTP/1.1 304 Not Modified\r\nContent-Length: 999\r\n\r\n", "", "GET", false) != 0 { return 7; }
    if verify("HTTP/1.1 205 Reset\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", "", "GET", false) != 0 { return 8; }
    def mut head: [u8; 128] = zeroed;
    def mut body: [u8; 3] = zeroed;
    def made: result<http.ResponseReader> = http.ResponseReader.create(head[..], body[..], "GET", 256);
    if made.erroneous { return 9; }
    def mut reader: http.ResponseReader = made.value;
    for def mut i: u64 = 0; i < strings.byte_length(wire); i = i + 1 {
        def step: result<http.ResponseProgress> = reader.feed((match strings.slice_bytes(wire, i, 1) { case value(v) => v; case error(e) => ""; }), false);
        if step.erroneous { return 10; }
    }
    def tail: result<http.ResponseProgress> = reader.feed("NEXT", false);
    if tail.erroneous { return 11; }
    def zero: u64 = 0;
    if !tail.value.complete || tail.value.consumed != zero { return 12; }
    def response: result<http.Response> = reader.response();
    if response.erroneous { return 13; }
    if !strings.equal(response.value.body, "a\0b") { return 14; }
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, ResponseReaderRejectsMalformedOversizedAndTruncatedMessages) {
    const auto file = source(R"(import "@http";
def rejects(wire: string, metadata: u64) -> bool {
    def mut head: [u8; 256] = zeroed;
    def mut body: [u8; 3] = zeroed;
    def made: result<http.ResponseReader> = http.ResponseReader.create(head[..], body[..], "GET", metadata);
    if made.erroneous { return false; }
    def mut reader: http.ResponseReader = made.value;
    def step: result<http.ResponseProgress> = reader.feed(wire, true);
    if !step.erroneous { return false; }
    def retried: result<http.ResponseProgress> = reader.feed("", false);
    if !retried.erroneous { return false; }
    return true;
}
def main() -> i32 {
    def bad: [string; 18] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab",
        "HTTP/1.1 200 OK\r\nContent-Length: +3\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000000000000000\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;bad=\"unfinished\r\na\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n+1\r\na\r\n0\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 205 Reset\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\nabcd",
        "HTTP/1.1 200 OK\r\nX: incomplete"
    };
    for def mut i: u64 = 0; i < 18; i = i + 1 {
        if !rejects(bad[i], 1024) { return 1; }
    }
    if !rejects("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 10) { return 2; }
    if !rejects("HTTP/1.1 103 Hints\r\n\r\nHTTP/1.1 103 Hints\r\n\r\n", 30) { return 3; }
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, StreamingDecoderStopsAtHeadAndOutputBoundaries) {
    const auto file = source(R"(import "@http"; import "@strings";
def check(wire: string, eof: bool, suffix: u64) -> bool {
    def zero: u64 = 0;
    for def mut split: u64 = 0; split <= strings.byte_length(wire); split = split + 1 {
        for def mut capacity: u64 = 1; capacity <= 3; capacity = capacity + 1 {
            def mut head: [u8; 256] = zeroed;
            def mut output: [u8; 3] = zeroed;
            def made: result<http.ResponseDecoder> = http.ResponseDecoder.create(head[..], 6, "GET", 1024);
            if made.erroneous { return false; }
            def mut decoder: http.ResponseDecoder = made.value;
            def mut cursor: u64 = 0;
            def mut count: u64 = 0;
            def mut complete: bool = false;
            def mut head_seen: bool = false;
            for def mut part: i32 = 0; part < 2; part = part + 1 {
                def mut end: u64 = split;
                if part == 1 { end = strings.byte_length(wire); }
                def mut again: bool = true;
                while again {
                    def bytes: string = (match strings.slice_bytes(wire, cursor, end - cursor) { case value(v) => v; case error(e) => ""; });
                    def fed: result<http.StreamProgress> = decoder.feed(bytes, eof && part == 1, output[0..capacity]);
                    if fed.erroneous { return false; }
                    def step: http.StreamProgress = fed.value;
                    if step.head_ready && !head_seen {
                        if step.produced != zero { return false; }
                        head_seen = true;
                        def info: result<http.ResponseInfo> = decoder.head();
                        if info.erroneous { return false; }
                        if info.value.code != 200 { return false; }
                        // A full output buffer must leave the next body byte untouched.
                        def mut probe: string = "a";
                        if strings.contains(info.value.head, "chunked") { probe = ""; }
                        def paused: result<http.StreamProgress> = decoder.feed(probe, false, output[0..0]);
                        if paused.erroneous { return false; }
                        if paused.value.consumed != zero || paused.value.produced != zero { return false; }
                    }
                    for def mut i: u64 = 0; i < step.produced; i = i + 1 {
                        if output[i] != (match strings.byte_at("a\0bcde", count + i) { case value(v) => v; case error(e) => 0; }) { return false; }
                    }
                    count = count + step.produced;
                    cursor = cursor + step.consumed;
                    complete = step.complete;
                    again = !complete && cursor < end;
                }
            }
            if !complete || !head_seen || count != 6 || cursor + suffix != strings.byte_length(wire) { return false; }
            def after: result<http.StreamProgress> = decoder.feed("NEXT", true, output[..]);
            if after.erroneous { return false; }
            if !after.value.complete || after.value.consumed != zero || after.value.produced != zero { return false; }
        }
    }
    return true;
}
def main() -> i32 {
    if !check("HTTP/1.1 103 Hints\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\na\0bcdeNEXT", false, 4) { return 1; }
    if !check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6\r\na\0bcde\r\n0\r\nX-End: yes\r\n\r\nNEXT", false, 4) { return 2; }
    if !check("HTTP/1.1 200 OK\r\n\r\na\0bcde", true, 0) { return 3; }
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, StreamingDecoderEnforcesTotalLimitAcrossOutputReuse) {
    const auto file = source(R"(import "@http"; import "@strings";
def check(head_text: string, suffix: string) -> bool {
    def mut head: [u8; 128] = zeroed;
    def mut output: [u8; 1] = zeroed;
    def made: result<http.ResponseDecoder> = http.ResponseDecoder.create(head[..], 2, "GET", 1024);
    if made.erroneous { return false; }
    def mut decoder: http.ResponseDecoder = made.value;
    def parsed: result<http.StreamProgress> = decoder.feed(head_text, false, output[..]);
    if parsed.erroneous { return false; }
    if !parsed.value.head_ready { return false; }
    def mut cursor: u64 = 0;
    def mut count: u64 = 0;
    while cursor < strings.byte_length(suffix) {
        def next: result<http.StreamProgress> = decoder.feed((match strings.slice_bytes(suffix, cursor, 1) { case value(v) => v; case error(e) => ""; }), false, output[..]);
        if next.erroneous {
            if count != 2 { return false; }
            def again: result<http.StreamProgress> = decoder.feed("", false, output[..]);
            if !again.erroneous { return false; }
            def info: result<http.ResponseInfo> = decoder.head();
            if !info.erroneous { return false; }
            return true;
        }
        def step: http.StreamProgress = next.value;
        cursor = cursor + step.consumed;
        count = count + step.produced;
    }
    return false;
}
def main() -> i32 {
    if !check("HTTP/1.1 200 OK\r\n\r\n", "abc") { return 1; }
    if !check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", "1\r\na\r\n1\r\nb\r\n1\r\nc\r\n0\r\n\r\n") { return 2; }
    return 0;
})");
    expect_success(invoke({file}), "");
}

TEST_F(NetworkLibraryTest, RequestReadersHandleEverySplitAndKeepPipelinedBytesInAllModes) {
    auto guide =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "docs/http-requests.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        guide = std::filesystem::path(binary).parent_path() /
                "../share/doc/gloinc/docs/http-requests.md";
    const auto documentation = read(guide.string());
    const std::string marker = "```gloin\n";
    const auto start = documentation.find(marker);
    ASSERT_NE(start, std::string::npos);
    const auto end = documentation.find("```", start + marker.size());
    ASSERT_NE(end, std::string::npos);
    expect_success(
        invoke({source(documentation.substr(start + marker.size(), end - start - marker.size()),
                       "guide.gloin")}),
        "");
    const auto file = source(R"(import "@http"; import "@strings";
def piece(text: string, start: u64, size: u64) -> string {
    return match strings.slice_bytes(text, start, size) { case value(v) => v; case error(e) => ""; };
}
def verify(wire: string, expected: string, method: string, message_bytes: u64) -> bool {
    for def mut split: u64 = 0; split <= strings.byte_length(wire); split = split + 1 {
        def mut head: [u8; 256] = zeroed;
        def mut body: [u8; 16] = zeroed;
        def made: result<http.RequestReader> = http.RequestReader.create(head[..], body[..], 512);
        if made.erroneous { return false; }
        def mut reader: http.RequestReader = made.value;
        def unavailable: result<http.Request> = reader.request();
        if !unavailable.erroneous { return false; }
        def first: result<http.RequestProgress> = reader.feed(piece(wire, 0, split), false);
        if first.erroneous { return false; }
        def cursor: u64 = first.value.consumed;
        def second: result<http.RequestProgress> = reader.feed(
            piece(wire, cursor, strings.byte_length(wire) - cursor), true);
        if second.erroneous { return false; }
        if !second.value.complete || cursor + second.value.consumed != message_bytes { return false; }
        def request: result<http.Request> = reader.request();
        if request.erroneous { return false; }
        if !strings.equal(request.value.body, expected) || !strings.equal(request.value.method, method)
            || !strings.equal(request.value.target, "/echo?x=1") || !strings.equal(request.value.host, "local:8080") {
            return false;
        }
        def again: result<http.RequestProgress> = reader.feed("GET /next HTTP/1.1\r\nHost: next\r\n\r\n", true);
        if again.erroneous { return false; }
        def zero: u64 = 0;
        if !again.value.complete || again.value.consumed != zero { return false; }
    }
    return true;
}
def main() -> i32 {
    def fixed: string = "POST /echo?x=1 HTTP/1.1\r\nHost: local:8080\r\nContent-Length: 3\r\n\r\na\0b";
    if !verify(fixed, "a\0b", "POST", strings.byte_length(fixed)) { return 1; }
    def chunked: string = "POST /echo?x=1 HTTP/1.1\r\nHost: local:8080\r\nTransfer-Encoding: ChUnKeD\r\n\r\n1;flag;name=\"b\\\"ar\"\r\na\r\n2\r\n\0b\r\n0\r\nX-End: done\r\n\r\n";
    if !verify(chunked, "a\0b", "POST", strings.byte_length(chunked)) { return 2; }
    def empty: string = "GET /echo?x=1 HTTP/1.1\r\nHost: local:8080\r\n\r\nNEXT";
    if !verify(empty, "", "GET", strings.byte_length(empty) - 4) { return 3; }
    def head_body: string = "HEAD /echo?x=1 HTTP/1.1\r\nHost: local:8080\r\nContent-Length: 3\r\nContent-Length: 003\r\n\r\nabcNEXT";
    if !verify(head_body, "abc", "HEAD", strings.byte_length(head_body) - 4) { return 4; }
    def zero: string = "POST /echo?x=1 HTTP/1.1\r\nHost: local:8080\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\nNEXT";
    if !verify(zero, "", "POST", strings.byte_length(zero) - 4) { return 5; }
    return 0;
})");
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/request-reader";
        const auto built = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 20), 0);
    }
}

TEST_F(NetworkLibraryTest, RequestDecoderHonorsBackpressureHeadBoundaryAndEofInAllModes) {
    const auto file = source(R"(import "@http"; import "@strings";
def piece(text: string, start: u64, size: u64) -> string {
    return match strings.slice_bytes(text, start, size) { case value(v) => v; case error(e) => ""; };
}
def check(wire: string, chunked: bool) -> bool {
    def zero: u64 = 0;
    for def mut split: u64 = 0; split <= strings.byte_length(wire); split = split + 1 {
        for def mut capacity: u64 = 1; capacity <= 6; capacity = capacity + 1 {
            def mut head: [u8; 256] = zeroed;
            def mut output: [u8; 8] = {165; 8};
            def made: result<http.RequestDecoder> = http.RequestDecoder.create(head[..], 6, 512);
            if made.erroneous { return false; }
            def mut decoder: http.RequestDecoder = made.value;
            def absent: result<http.RequestInfo> = decoder.head();
            if !absent.erroneous { return false; }
            def mut cursor: u64 = 0;
            def mut count: u64 = 0;
            def mut complete: bool = false;
            def mut seen_head: bool = false;
            for def mut part: i32 = 0; part < 2; part = part + 1 {
                def mut end: u64 = split;
                if part == 1 { end = strings.byte_length(wire); }
                def mut again: bool = true;
                while again {
                    def step_result: result<http.StreamProgress> = decoder.feed(piece(wire, cursor, end - cursor), part == 1,
                        output[1..1 + capacity]);
                    if step_result.erroneous { return false; }
                    def step: http.StreamProgress = step_result.value;
                    if !seen_head && step.head_ready {
                        if step.produced != zero { return false; }
                        def info: result<http.RequestInfo> = decoder.head();
                        if info.erroneous { return false; }
                        if !strings.equal(info.value.method, "POST") || !strings.equal(info.value.target, "/")
                            || !strings.equal(info.value.host, "local") || info.value.chunked != chunked
                            || info.value.has_content_length == chunked { return false; }
                        if strings.byte_length(info.value.head) != cursor + step.consumed { return false; }
                        seen_head = true;
                    }
                    for def mut i: u64 = 0; i < step.produced; i = i + 1 {
                        def expected: u8 = match strings.byte_at("a\0bcde", count + i) { case value(v) => v; case error(e) => 0; };
                        if output[1 + i] != expected { return false; }
                    }
                    if output[0] != 165 || output[7] != 165 { return false; }
                    cursor = cursor + step.consumed;
                    count = count + step.produced;
                    complete = step.complete;
                    // Reusing an empty output window must neither write nor consume body data.
                    if seen_head && !complete && count == 0 && cursor < end && !chunked {
                        def paused: result<http.StreamProgress> = decoder.feed(piece(wire, cursor, end - cursor), true, output[1..1]);
                        if paused.erroneous { return false; }
                        if paused.value.consumed != zero || paused.value.produced != zero || paused.value.complete { return false; }
                    }
                    again = !complete && cursor < end;
                }
            }
            if !complete || !seen_head || count != 6 || cursor + 4 != strings.byte_length(wire) { return false; }
            def after: result<http.StreamProgress> = decoder.feed("NEXT", true, output[..]);
            if after.erroneous { return false; }
            if !after.value.complete || after.value.consumed != zero || after.value.produced != zero { return false; }
        }
    }
    return true;
}
def main() -> i32 {
    if !check("POST / HTTP/1.1\r\nHost: local\r\nContent-Length: 6\r\n\r\na\0bcdeNEXT", false) { return 1; }
    if !check("POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n5\r\n\0bcde\r\n0\r\nX-Trailer: yes\r\n\r\nNEXT", true) { return 2; }
    return 0;
})");
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/request-stream";
        const auto built = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 20), 0);
    }
}

TEST_F(NetworkLibraryTest, RequestReaderRejectsAmbiguousMalformedOversizedAndTruncatedInput) {
    const std::string head = "POST / HTTP/1.1\r\nHost: local\r\n";
    const std::string chunked = head + "Transfer-Encoding: chunked\r\n\r\n";
    std::vector<std::string> invalid = {
        "GET / HTTP/1.0\r\nHost: local\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: one\r\nHost: two\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: \r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a b\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a,b\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: user@host\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: local\r\n folded: yes\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: local\r\nBad : value\r\n\r\n",
        "GET / HTTP/1.1\nHost: local\n\n",
        "GET / HTTP/1.1\rX\r\n\r\n",
        "CONNECT local:443 HTTP/1.1\r\nHost: local\r\n\r\n",
        "CONNECT / HTTP/1.1\r\nHost: local\r\n\r\n",
        "GET http://local/ HTTP/1.1\r\nHost: local\r\n\r\n",
        "GET /bad path HTTP/1.1\r\nHost: local\r\n\r\n",
        "GET /#fragment HTTP/1.1\r\nHost: local\r\n\r\n",
        head + "Content-Length: 1\r\nContent-Length: 2\r\n\r\nx",
        head + "Content-Length: +1\r\n\r\nx",
        head + "Content-Length: -1\r\n\r\nx",
        head + "Content-Length: 1, 1\r\n\r\nx",
        head + "Content-Length: 18446744073709551616\r\n\r\n",
        head + "Content-Length: 18446744073709551615\r\n\r\n",
        head + "Content-Length: 3\r\n\r\nab",
        head + "Content-Length: 9\r\n\r\n123456789",
        head + "Transfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n",
        head + "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        head + "Transfer-Encoding: gzip, chunked\r\n\r\n",
        head + "Transfer-Encoding: chunked, gzip\r\n\r\n",
        chunked + "10000000000000000\r\n",
        chunked + "ffffffffffffffff\r\n",
        chunked + "9\r\n123456789\r\n0\r\n\r\n",
        chunked + "5\r\n12345\r\n4\r\n6789\r\n0\r\n\r\n",
        chunked + "+1\r\nx\r\n0\r\n\r\n",
        chunked + "g\r\n",
        chunked + "1\r\nxX\n0\r\n\r\n",
        chunked + "1\r\nx\rX0\r\n\r\n",
        chunked + "0\r\n",
        chunked + "1\r\nx\r\n",
        chunked + "1\nx\n0\n\n",
        chunked + "1;name=\"unfinished\r\nx\r\n0\r\n\r\n",
        chunked + "1;=value\r\nx\r\n0\r\n\r\n",
        chunked + "1;flag=\r\nx\r\n0\r\n\r\n",
        chunked + "0\r\nMissingColon\r\n\r\n",
        chunked + "0\r\n folded: no\r\n\r\n",
        chunked + "0\r\nX: a\n\r\n",
        chunked + "0\r\nX: a\rX\r\n",
        chunked + "1;" + std::string(1022, 'a') + "\r\nx\r\n0\r\n\r\n",
        chunked + "0\r\nX: " + std::string(1021, 'a') + "\r\n\r\n",
        head + "X: a" + std::string(1, '\0') + "b\r\n\r\n",
    };
    for (const auto *name : {"Content-Length", "Transfer-Encoding", "Host", "Connection", "Upgrade",
                             "Expect", "TE", "Trailer"})
        invalid.push_back(chunked + "0\r\n" + name + ": 0\r\n\r\n");
    auto quote = [](const std::string &text) {
        std::string output = "\"";
        for (char ch : text) {
            if (ch == '\\' || ch == '"') {
                output += '\\';
                output += ch;
            } else if (ch == '\r')
                output += "\\r";
            else if (ch == '\n')
                output += "\\n";
            else if (ch == '\0')
                output += "\\0";
            else
                output += ch;
        }
        return output + '"';
    };
    std::string program = R"(import "@http"; import "@strings";
def part(text: string, start: u64, size: u64) -> string {
    return match strings.slice_bytes(text, start, size) { case value(v) => v; case error(e) => ""; };
}
def rejects(wire: string) -> bool {
    for def mut split: u64 = 0; split <= strings.byte_length(wire); split = split + 1 {
        def mut head: [u8; 256] = zeroed;
        def mut body: [u8; 8] = zeroed;
        def made: result<http.RequestReader> = http.RequestReader.create(head[..], body[..], 4096);
        if made.erroneous { return false; }
        def mut reader: http.RequestReader = made.value;
        def first: result<http.RequestProgress> = reader.feed(part(wire, 0, split), false);
        if !first.erroneous {
            def final: result<http.RequestProgress> = reader.feed(part(wire, split, strings.byte_length(wire) - split), true);
            if !final.erroneous { return false; }
        }
        def retry: result<http.RequestProgress> = reader.feed("", false);
        if !retry.erroneous { return false; }
        def outcome: result<http.Request> = reader.request();
        if !outcome.erroneous { return false; }
    }
    return true;
}
def main() -> i32 {
)";
    for (const auto &wire : invalid)
        program += "if !rejects(" + quote(wire) + ") { return 1; }\n";
    // Every strict prefix of a valid fixed/chunked message must reject EOF.
    for (const auto &wire :
         {head + "Content-Length: 3\r\n\r\nabc", chunked + "3\r\nabc\r\n0\r\nX-End: yes\r\n\r\n"})
        program += "{ def wire: string = " + quote(wire) + R"(;
            for def mut end: u64 = 0; end < strings.byte_length(wire); end = end + 1 {
                if !rejects(part(wire, 0, end)) { return 2; }
            }
        })";
    const auto file = source(program + "return 0; }");
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/bad-requests";
        const auto built = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, RequestDecoderEnforcesExactLimitsAndInvalidatesFailedHeads) {
    const auto file = source(R"(import "@http"; import "@strings";
def bounded(wire: string, head_limit: u64, body_limit: u64, metadata: u64, valid: bool) -> bool {
    def mut head: [u8; 130] = {165; 130};
    def mut body: [u8; 10] = {165; 10};
    def made: result<http.RequestReader> = http.RequestReader.create(head[1..1 + head_limit], body[1..1 + body_limit], metadata);
    if made.erroneous { return !valid; }
    def mut reader: http.RequestReader = made.value;
    def fed: result<http.RequestProgress> = reader.feed(wire, true);
    if fed.erroneous { return !valid; }
    if !valid || !fed.value.complete { return false; }
    for def mut i: u64 = 1 + head_limit; i < 130; i = i + 1 { if head[i] != 165 { return false; } }
    for def mut i: u64 = 1 + body_limit; i < 10; i = i + 1 { if body[i] != 165 { return false; } }
    return head[0] == 165 && body[0] == 165;
}
def main() -> i32 {
    def fixed: string = "POST / HTTP/1.1\r\nHost: local\r\nContent-Length: 3\r\n\r\nabc";
    def size: u64 = strings.byte_length(fixed) - 3;
    if !bounded(fixed, size, 3, size, true) || !bounded(fixed, size - 1, 3, size, false)
        || !bounded(fixed, size, 2, size, false) || !bounded(fixed, size, 3, size - 1, false) { return 1; }
    def chunked: string = "POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n2\r\nbc\r\n0\r\nX-End: yes\r\n\r\n";
    def metadata: u64 = strings.byte_length(chunked) - 3;
    if !bounded(chunked, 128, 3, metadata, true) || !bounded(chunked, 128, 3, metadata - 1, false)
        || !bounded(chunked, 128, 2, metadata, false) { return 2; }
    if !bounded("POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 128, 0, 128, true)
        || !bounded("GET / HTTP/1.1\r\nHost: local\r\n\r\n", 128, 0, 128, true)
        || !bounded(fixed, 0, 3, 128, false) || !bounded(fixed, 128, 3, 0, false) { return 3; }
    def mut head: [u8; 128] = zeroed;
    def mut output: [u8; 1] = zeroed;
    def made: result<http.RequestDecoder> = http.RequestDecoder.create(head[..], 2, 128);
    if made.erroneous { return 4; }
    def mut decoder: http.RequestDecoder = made.value;
    def started: result<http.StreamProgress> = decoder.feed("POST / HTTP/1.1\r\nHost: local\r\nTransfer-Encoding: chunked\r\n\r\n", false, output[..]);
    if started.erroneous { return 5; }
    def info: result<http.RequestInfo> = decoder.head();
    if info.erroneous { return 6; }
    def prefix: result<http.StreamProgress> = decoder.feed("1\r\na\r\n", false, output[..]);
    if prefix.erroneous { return 7; }
    if prefix.value.produced != 1 || output[0] != 97 { return 8; }
    def bad: result<http.StreamProgress> = decoder.feed("2\r\nbc\r\n0\r\n\r\n", true, output[..]);
    if !bad.erroneous { return 9; }
    def invalidated: result<http.RequestInfo> = decoder.head();
    if !invalidated.erroneous { return 10; }
    def retry: result<http.StreamProgress> = decoder.feed("0\r\n\r\n", true, output[..]);
    if !retry.erroneous { return 11; }
    return 0;
})");
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/request-limits";
        const auto built = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 20), 0);
    }
}

namespace {
using WireBytes = std::vector<uint8_t>;
uint16_t wire_checksum(const WireBytes &bytes) {
    uint32_t sum = 0;
    for (size_t i = 0; i < bytes.size(); i += 2)
        sum += (uint32_t(bytes[i]) << 8) | (i + 1 < bytes.size() ? bytes[i + 1] : 0);
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}
void wire_u16(WireBytes &bytes, size_t offset, uint16_t value) {
    bytes.at(offset) = value >> 8;
    bytes.at(offset + 1) = value & 255;
}
void wire_ip_checksum(WireBytes &bytes) {
    wire_u16(bytes, 10, 0);
    wire_u16(bytes, 10,
             wire_checksum(WireBytes(bytes.begin(), bytes.begin() + (bytes[0] & 15) * 4)));
}
void wire_transport_checksum(WireBytes &bytes) {
    const size_t start = (bytes[0] & 15) * 4;
    const size_t field = bytes[9] == 17 ? 6 : 16;
    wire_u16(bytes, start + field, 0);
    WireBytes pseudo(bytes.begin() + 12, bytes.begin() + 20);
    const size_t size = bytes.size() - start;
    pseudo.insert(pseudo.end(), {0, bytes[9], uint8_t(size >> 8), uint8_t(size)});
    pseudo.insert(pseudo.end(), bytes.begin() + start, bytes.end());
    auto sum = wire_checksum(pseudo);
    wire_u16(bytes, start + field, sum == 0 && bytes[9] == 17 ? 0xffff : sum);
    wire_ip_checksum(bytes);
}
WireBytes wire_packet(bool tcp, const WireBytes &payload, const WireBytes &ip_options = {},
                      const WireBytes &tcp_options = {}) {
    const size_t start = 20 + ip_options.size();
    const size_t head = tcp ? 20 + tcp_options.size() : 8;
    WireBytes bytes(start + head + payload.size(), 0);
    bytes[0] = uint8_t(0x40 | start / 4);
    bytes[1] = 0xab;
    wire_u16(bytes, 2, bytes.size());
    wire_u16(bytes, 4, 0x1234);
    bytes[6] = 0x40;
    bytes[8] = 64;
    bytes[9] = tcp ? 6 : 17;
    const WireBytes addresses{192, 0, 2, 1, 198, 51, 100, 2};
    std::copy(addresses.begin(), addresses.end(), bytes.begin() + 12);
    std::copy(ip_options.begin(), ip_options.end(), bytes.begin() + 20);
    wire_u16(bytes, start, 0x1234);
    wire_u16(bytes, start + 2, 0xabcd);
    if (tcp) {
        const WireBytes numbers{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
        std::copy(numbers.begin(), numbers.end(), bytes.begin() + start + 4);
        bytes[start + 12] = uint8_t(head / 4 << 4);
        bytes[start + 13] = 0x12;
        wire_u16(bytes, start + 14, 0x6789);
        std::copy(tcp_options.begin(), tcp_options.end(), bytes.begin() + start + 20);
    } else {
        wire_u16(bytes, start + 4, head + payload.size());
    }
    std::copy(payload.begin(), payload.end(), bytes.begin() + start + head);
    wire_transport_checksum(bytes);
    return bytes;
}
std::string wire_literal(const WireBytes &bytes) {
    std::string text = "{";
    for (auto byte : bytes)
        text += std::to_string(byte) + ",";
    return text + "}";
}
} // namespace

TEST_F(NetworkLibraryTest, PackedPacketCodecsMatchIndependentWireVectorsInAllModes) {
    std::string program = read(
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/packet_checks.gloin")
            .string());
    program += "\ndef main() -> i32 {\n";
    unsigned index = 0;
    auto add = [&](WireBytes bytes, unsigned protocol, bool valid, bool sealable, bool canonical,
                   bool torture = false) {
        const std::string name = "v" + std::to_string(index++);
        const auto boolean = [](bool b) { return b ? "true" : "false"; };
        program += "def " + name + ": [u8; " + std::to_string(bytes.size()) +
                   "] = " + wire_literal(bytes) + ";\n";
        program += "if check(" + name + "[..], " + std::to_string(protocol) + ", " +
                   boolean(valid) + ", " + boolean(sealable) + ", " + boolean(canonical) +
                   ") != 0 { return " + std::to_string(index) + "; }\n";
        if (torture) {
            program += "if truncated(" + name + "[..], " + std::to_string(protocol) +
                       ") != 0 { return 240; }\n";
            program += "if corrupted(" + name + "[..], " + std::to_string(protocol) +
                       ") != 0 { return 241; }\n";
        }
    };
    const WireBytes payload{0, 0xff, 0x80, 3, 0x7f};
    for (bool tcp : {false, true}) {
        const unsigned protocol = tcp ? 6 : 17;
        auto base = wire_packet(tcp, payload);
        add(base, protocol, true, true, true, true);
        add(wire_packet(tcp, {}), protocol, true, true, true, true);
        auto padded = base;
        padded.insert(padded.end(), {0xde, 0xad, 0xbe, 0xef});
        add(padded, protocol, true, true, true);
        auto broken = base;
        broken[10] ^= 1;
        add(broken, protocol, false, true, false);
        broken = base;
        broken.back() ^= 1;
        add(broken, protocol, false, true, false);
        for (auto fragment : {uint16_t(0x2000), uint16_t(1), uint16_t(0x8000), uint16_t(0x6000)}) {
            broken = base;
            wire_u16(broken, 6, fragment);
            wire_ip_checksum(broken);
            add(broken, protocol, false, false, false);
        }
        for (unsigned offset : {0u, 2u, 3u, 9u}) {
            broken = base;
            broken[offset] = offset == 0 ? 0x44 : offset == 2 ? 0xff : 0;
            wire_ip_checksum(broken);
            add(broken, protocol, false, false, false);
        }
        auto options = wire_packet(tcp, payload, {1, 0x9e, 2, 0});
        add(options, protocol, true, true, true, true);
        for (const auto &bad :
             {WireBytes{0, 1, 0, 0}, WireBytes{0x9e, 1, 0, 0}, WireBytes{0x9e, 5, 0, 0},
              WireBytes{131, 3, 4, 0}, WireBytes{137, 3, 4, 0}}) {
            add(wire_packet(tcp, payload, bad), protocol, false, false, false);
        }
    }
    auto udp = wire_packet(false, payload);
    wire_u16(udp, 26, 0);
    add(udp, 17, true, true, false); // IPv4 UDP explicitly permits absent checksums.
    for (unsigned length : {0u, 7u, 8u, 12u, 14u, 65535u}) {
        udp = wire_packet(false, payload);
        wire_u16(udp, 24, length);
        wire_transport_checksum(udp);
        add(udp, 17, false, false, false);
    }
    // A mathematically zero UDP checksum must be encoded as 0xffff, never absent.
    auto zero = wire_packet(false, {0, 0});
    WireBytes pseudo(zero.begin() + 12, zero.begin() + 20);
    pseudo.insert(pseudo.end(), {0, 17, 0, 10});
    wire_u16(zero, 26, 0);
    pseudo.insert(pseudo.end(), zero.begin() + 20, zero.end());
    wire_u16(zero, 28, wire_checksum(pseudo));
    wire_transport_checksum(zero);
    ASSERT_EQ(zero[26], 255);
    ASSERT_EQ(zero[27], 255);
    add(zero, 17, true, true, true);
    for (unsigned words : {0u, 4u, 15u}) {
        auto tcp = wire_packet(true, payload);
        tcp[32] = words << 4;
        wire_transport_checksum(tcp);
        add(tcp, 6, false, false, false);
    }
    for (const auto &options : {WireBytes{2, 4, 5, 0xb4}, WireBytes{1, 3, 3, 7},
                                WireBytes{30, 3, 0xff, 0}, WireBytes(40, 1)})
        add(wire_packet(true, payload, {}, options), 6, true, true, true, true);
    for (const auto &options :
         {WireBytes{2, 3, 0, 0}, WireBytes{3, 4, 0, 0}, WireBytes{4, 3, 0, 0},
          WireBytes{5, 4, 0, 0}, WireBytes{8, 4, 0, 0}, WireBytes{30, 1, 0, 0}})
        add(wire_packet(true, payload, {}, options), 6, false, false, false);
    auto reserved = wire_packet(true, payload);
    reserved[32] |= 1;
    wire_transport_checksum(reserved);
    add(reserved, 6, true, false, false); // Preserve received bits; prohibit generating them.
    auto fragment = wire_packet(false, WireBytes(8, 0));
    wire_u16(fragment, 6, 0x2000);
    wire_ip_checksum(fragment);
    add(fragment, 0, true, true, true);
    add(fragment, 17, false, false, false);
    wire_u16(fragment, 6, 8191);
    wire_ip_checksum(fragment);
    add(fragment, 0, false, false, false);
    program += "return 0; }\n";
    const auto file = source(program);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/packets";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        const auto executed = llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30);
        EXPECT_EQ(executed, 0);
    }
}

TEST_F(NetworkLibraryTest, PackedByteAccessPreservesRepresentationAndBoundsInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/packed_memory.gloin")
            .string();
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/packed-memory";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
    for (const auto *type : {"u32", "*u8", "Native", "[u8; 4]"}) {
        for (bool writing : {false, true}) {
            const std::string declaration =
                "import \"@memory\"; def struct Native { def x: u32, } ";
            const std::string body =
                writing ? "def test(bytes: [u8], value: " + std::string(type) +
                              ") -> void { def r: result<void> = memory.write_packed<" + type +
                              ">(bytes, value); if r.erroneous { return; } }"
                        : "def test(bytes: [const u8]) -> void { def r: result<" +
                              std::string(type) + "> = memory.read_packed<" + type +
                              ">(bytes); if r.erroneous { return; } }";
            expect_error(invoke({"--check",
                                 source(declaration + body + " def main() -> i32 { return 0; }")}),
                         1, "requires a nonempty packed struct");
        }
    }
    expect_error(invoke({"--check", source(R"(import "@memory";
        def packed struct(msb_first) Header { def x: be_u32, }
        def test(bytes: [const u8], value: Header) -> void {
            def r: result<void> = memory.write_packed<Header>(bytes, value);
            if r.erroneous { return; }
        } def main() -> i32 { return 0; })")}),
                 1, "error:");
    expect_error(invoke({"--check", source(R"(def packed struct(msb_first) Header { def x: u8, }
        def main() -> i32 { __memory_packed_pointer<Header>(null); return 0; })")}),
                 1, "error:");
}

TEST_F(NetworkLibraryTest, InternetChecksumHandlesEverySplitCarryAndMaximumIpv4Size) {
    WireBytes all(65535);
    for (size_t i = 0; i < all.size(); ++i)
        all[i] = (i * 73 + i / 3) % 256;
    std::string program = R"(import "@packet"; import "@memory";
        def main() -> i32 {
            def mut bytes: [u8; 65535] = zeroed;
            for def mut i: u64 = 0; i < 65535; i = i + 1 {
                def value: result<u8> = u8((i * 73 + i / 3) % 256);
                if value.erroneous { return 1; }
                bytes[i] = value.value;
            }
            def expected: [be_u16; 258] = {)";
    for (size_t length = 0; length < 258; ++length)
        program +=
            std::to_string(wire_checksum(WireBytes(all.begin(), all.begin() + length))) + ",";
    program += R"(};
            for def mut length: u64 = 0; length < 258; length = length + 1 {
                def full: result<be_u16> = packet.checksum(bytes[..length]);
                if full.erroneous { return 2; }
                if full.value != expected[length] { return 3; }
                for def mut split: u64 = 0; split <= length; split = split + 1 {
                    def mut sum: packet.Checksum = packet.Checksum.create();
                    def first: result<void> = sum.update(bytes[..split]);
                    if first.erroneous { return 4; }
                    def interim: result<be_u16> = sum.finish();
                    if interim.erroneous { return 5; }
                    if interim.value != expected[split] { return 6; }
                    def second: result<void> = sum.update(bytes[split..length]);
                    if second.erroneous { return 7; }
                    def final: result<be_u16> = sum.finish();
                    if final.erroneous { return 8; }
                    if final.value != expected[length] { return 9; }
                }
            }
            def large: result<be_u16> = packet.checksum(bytes[..]);
            if large.erroneous { return 10; }
            if large.value != )" +
               std::to_string(wire_checksum(all)) + R"( { return 11; }
            return 0;
        })";
    const auto maximum = wire_packet(false, WireBytes(all.begin() + 28, all.end()));
    const auto return_position = program.rfind("return 0;");
    ASSERT_NE(return_position, std::string::npos);
    program.insert(return_position, R"(
        def ip: result<void> = memory.write_packed<packet.Ipv4Header>(bytes[..], packet.Ipv4Header {
            version: 4, header_words: 5, dscp: 42, ecn: 3, total_length: 65535,
            identification: 0x1234, reserved: 0, dont_fragment: 1, more_fragments: 0,
            fragment_offset: 0, ttl: 64, protocol: 17, checksum: 0,
            source: 0xc0000201, destination: 0xc6336402 });
        if ip.erroneous { return 12; }
        def udp: result<void> = memory.write_packed<packet.UdpHeader>(bytes[20..], packet.UdpHeader {
            source_port: 0x1234, destination_port: 0xabcd, length: 65515, checksum: 0 });
        if udp.erroneous { return 13; }
        def sealed: result<void> = packet.seal_udp_ipv4(bytes[..]);
        if sealed.erroneous { return 14; }
        def parsed: result<packet.UdpPacket> = packet.parse_udp_ipv4(bytes[..]);
        if parsed.erroneous { return 15; }
        if parsed.value.payload.len != 65507 || parsed.value.header.length != 65515
            || parsed.value.header.source_port != 0x1234 || parsed.value.header.destination_port != 0xabcd
            || parsed.value.ip.header.dscp != 42 || parsed.value.ip.header.ecn != 3
            || parsed.value.ip.header.source != 0xc0000201 || parsed.value.ip.header.destination != 0xc6336402
            || parsed.value.ip.header.checksum != )" +
                                        std::to_string((maximum[10] << 8) | maximum[11]) +
                                        " || parsed.value.header.checksum != " +
                                        std::to_string((maximum[26] << 8) | maximum[27]) +
                                        " { return 16; }\n");
    const auto file = source(program);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/checksum";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, PackedPacketExampleRunsInAllModes) {
    auto example = std::filesystem::path(__FILE__).parent_path().parent_path() /
                   "examples/packet_headers.gloin";
    if (const char *installed = std::getenv("GLOIN_TEST_CLI"))
        example = std::filesystem::path(installed).parent_path().parent_path() /
                  "share/doc/gloinc/examples/packet_headers.gloin";
    expect_success(invoke({example.string()}), "packed IPv4/UDP round trip succeeded\n");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/packet-example";
        expect_success(invoke_raw({mode, "-o", output, example.string()}), "");
        const auto stdout_path = directory + "/packet.stdout";
        const auto stderr_path = directory + "/packet.stderr";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, stdout_path, stderr_path};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, redirects, 15), 0);
        EXPECT_EQ(read(stdout_path), "packed IPv4/UDP round trip succeeded\n");
        EXPECT_TRUE(read(stderr_path).empty());
    }
}

TEST_F(NetworkLibraryTest, TcpEngineFollowsIndependentSequenceWindowRetransmissionAndCloseOracle) {
    std::string program =
        read((std::filesystem::path(__FILE__).parent_path() / "fixtures/network/tcp_oracle.gloin")
                 .string());
    program += R"(
        def main() -> i32 {
            def mut tx: [u8; 512] = zeroed;
            def mut rx: [u8; 256] = zeroed;
            def mut present: [u8; 256] = zeroed;
            def local: tcp.Address = tcp.Address { host: 0xc6336402, port: 0xabcd };
            def remote: tcp.Address = tcp.Address { host: 0xc0000201, port: 0x1234 };
            def options: tcp.Options = tcp.Options { mss: 128, max_retries: 5,
                idle_timeout_ms: 300000, time_wait_ms: 2000, no_delay: true };
            def made: result<tcp.Connection> = tcp.Connection.connect(local, remote, 4294967292,
                tcp.Buffers { send: tx[..], receive: rx[..], present: present[..] }, options, 0);
            if made.erroneous { return 1; }
            def mut connection: tcp.Connection = made.value;
            def empty: [u8; 0] = zeroed;
            if !emitted(&connection, 0, 4294967292, 0, 2, 256, empty[..], 44) { return 2; }
    )";
    unsigned index = 0;
    auto input = [&](uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
                     const WireBytes &payload, uint64_t now, bool syn_options = false) {
        auto wire =
            wire_packet(true, payload, {}, syn_options ? WireBytes{2, 4, 0, 64} : WireBytes{});
        const auto put32 = [&](size_t offset, uint32_t value) {
            wire_u16(wire, offset, value >> 16);
            wire_u16(wire, offset + 2, value & 65535);
        };
        put32(24, seq);
        put32(28, ack);
        wire[33] = flags;
        wire_u16(wire, 34, window);
        wire_transport_checksum(wire);
        const auto name = "incoming" + std::to_string(index++);
        program += "def " + name + ": [u8; " + std::to_string(wire.size()) +
                   "] = " + wire_literal(wire) + ";\n";
        program += "if !receive(&connection, " + name + "[..], " + std::to_string(now) +
                   ") { return 200; }\n";
    };
    input(0xfffffffe, 0xfffffffd, 0x12, 256, {}, 10, true);
    program += R"(
        if connection.state() != tcp.ESTABLISHED { return 3; }
        if !emitted(&connection, 11, 4294967293, 4294967295, 16, 256, empty[..], 40) { return 4; }
    )";
    input(2, 0xfffffffd, 0x10, 256, {'D', 'E', 'F'}, 20);
    program += R"(
        if connection.readable() != 0 { return 5; }
        if !emitted(&connection, 21, 4294967293, 4294967295, 16, 256, empty[..], 40) { return 6; }
    )";
    input(0xffffffff, 0xfffffffd, 0x10, 256, {'a', 'b', 'c'}, 22);
    program += R"(
        if !emitted(&connection, 23, 4294967293, 5, 16, 250, empty[..], 40) { return 7; }
        def mut received: [u8; 6] = zeroed;
        def expected: [u8; 6] = {97,98,99,68,69,70};
        def read: result<tcp.Read> = connection.read(received[..]);
        if read.erroneous { return 8; }
        if read.value.count != 6 || read.value.end { return 9; }
        for def mut i: u64 = 0; i < 6; i = i + 1 { if received[i] != expected[i] { return 10; } }
        if !emitted(&connection, 24, 4294967293, 5, 16, 256, empty[..], 40) { return 11; }
    )";
    input(0xffffffff, 0xfffffffd, 0x10, 256, {'a', 'b', 'c'}, 25);
    program += R"(
        if connection.readable() != 0 { return 12; }
        if !emitted(&connection, 26, 4294967293, 5, 16, 256, empty[..], 40) { return 13; }
        def mut payload: [u8; 200] = zeroed;
        for def mut i: u64 = 0; i < 200; i = i + 1 {
            def byte: result<u8> = u8(i);
            if byte.erroneous { return 14; }
            payload[i] = byte.value;
        }
        def queued: result<u64> = connection.write(payload[..]);
        if queued.erroneous { return 15; }
        if queued.value != 200 { return 16; }
        if !emitted(&connection, 30, 4294967293, 5, 16, 256, payload[..64], 104) { return 17; }
        if !emitted(&connection, 31, 61, 5, 16, 256, payload[64..128], 104) { return 18; }
        if !emitted(&connection, 32, 125, 5, 16, 256, payload[128..192], 104) { return 19; }
        if !emitted(&connection, 33, 189, 5, 16, 256, payload[192..], 48) { return 20; }
    )";
    input(5, 0xfffffffd, 0x10, 256, {}, 40);
    input(5, 0xfffffffd, 0x10, 256, {}, 41);
    input(5, 0xfffffffd, 0x10, 256, {}, 42);
    program +=
        R"(if !emitted(&connection, 43, 4294967293, 5, 16, 256, payload[..64], 104) { return 21; })";
    input(5, 197, 0x10, 256, {}, 44);
    program += R"(
        if connection.send_available() != 512 { return 22; }
        def more: result<u64> = connection.write(payload[..5]);
        if more.erroneous { return 23; }
        if !emitted(&connection, 50, 197, 5, 16, 256, payload[..5], 45) { return 24; }
        if !emitted(&connection, 1049, 0, 0, 0, 0, empty[..], 0) { return 25; }
        if !emitted(&connection, 1050, 197, 5, 16, 256, payload[..5], 45) { return 26; }
        if !emitted(&connection, 3049, 0, 0, 0, 0, empty[..], 0) { return 27; }
        if !emitted(&connection, 3050, 197, 5, 16, 256, payload[..5], 45) { return 28; }
    )";
    input(5, 202, 0x10, 256, {}, 3051);
    input(5, 202, 0x10, 0, {}, 3052);
    program += R"(
        def probe_data: result<u64> = connection.write(payload[..2]);
        if probe_data.erroneous { return 29; }
        if !emitted(&connection, 4050, 0, 0, 0, 0, empty[..], 0) { return 30; }
        if !emitted(&connection, 4051, 202, 5, 16, 256, payload[..1], 41) { return 31; }
    )";
    input(5, 202, 0x10, 0, {}, 4052);
    input(5, 202, 0x10, 256, {}, 4053);
    program += R"(
        if !emitted(&connection, 4054, 202, 5, 16, 256, payload[..1], 41) { return 32; }
        if !emitted(&connection, 4055, 203, 5, 16, 256, payload[1..2], 41) { return 33; }
    )";
    input(5, 204, 0x10, 256, {}, 4056);
    program += R"(
        def closed: result<void> = connection.shutdown_write();
        if closed.erroneous { return 34; }
        if !emitted(&connection, 4057, 204, 5, 17, 256, empty[..], 40) { return 35; }
    )";
    input(5, 205, 0x10, 256, {}, 4058);
    program += R"(if connection.state() != tcp.FIN_WAIT_2 { return 36; })";
    input(5, 205, 0x11, 256, {}, 4059);
    program += R"(
        if connection.state() != tcp.TIME_WAIT { return 37; }
        if !emitted(&connection, 4060, 205, 6, 16, 256, empty[..], 40) { return 38; }
    )";
    input(6, 205, 0x14, 256, {}, 4061);
    program += R"(if connection.state() != tcp.TIME_WAIT { return 39; })";
    input(5, 205, 0x11, 256, {}, 4062);
    program += R"(
        if !emitted(&connection, 4063, 205, 6, 16, 256, empty[..], 40) { return 40; }
        if !emitted(&connection, 6062, 0, 0, 0, 0, empty[..], 0) { return 41; }
        if connection.state() != tcp.CLOSED { return 42; }
        return 0;
    })";
    const auto file = source(program);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/tcp-oracle";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, TcpPairTransfersWrappedSequencesAndClosesBothDirectionsInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/tcp_pair.gloin")
            .string();
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/tcp-pair";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, TcpLossReorderDuplicateAndSimultaneousOpenSimulationInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/tcp_loss.gloin")
            .string();
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/tcp-loss";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, Ipv4ReassemblyMatchesIndependentFragmentsAndRejectsAmbiguity) {
    WireBytes payload(31);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (i * 71) & 255;
    auto full = wire_packet(false, payload, {1, 1, 0, 0});
    wire_u16(full, 6, 0);
    wire_ip_checksum(full);
    auto fragment = [&](size_t start, size_t length, bool more) {
        WireBytes bytes(full.begin(), full.begin() + 24);
        bytes.insert(bytes.end(), full.begin() + 24 + start, full.begin() + 24 + start + length);
        wire_u16(bytes, 2, bytes.size());
        wire_u16(bytes, 6, start / 8 | (more ? 0x2000 : 0));
        wire_ip_checksum(bytes);
        return bytes;
    };
    std::string program = R"(import "@ipv4"; import "@packet"; import "@status";
        def equal(a: [const u8], b: [const u8]) -> bool {
            if a.len != b.len { return false; }
            for def mut i: u64 = 0; i < a.len; i = i + 1 { if a[i] != b[i] { return false; } }
            return true;
        }
        def incomplete(r: &ipv4.Reassembly, bytes: [const u8], now: u64) -> bool {
            def got: result<ipv4.Datagram> = r.input(bytes, now);
            if got.erroneous { return false; }
            return !got.value.complete;
        }
        def main() -> i32 {
            def mut storage: [u8; 191] = zeroed;
            def mut present: [u8; 130] = zeroed;
            for def mut i: u64 = 0; i < 191; i = i + 1 { storage[i] = 165; }
            for def mut i: u64 = 0; i < 130; i = i + 1 { present[i] = 165; }
            def made: result<ipv4.Reassembly> = ipv4.Reassembly.create(storage[1..189], present[1..129], 30000);
            if made.erroneous { return 1; }
            def mut r: ipv4.Reassembly = made.value;
    )";
    auto declare = [&](const std::string &name, const WireBytes &bytes) {
        program += "def " + name + ": [u8; " + std::to_string(bytes.size()) +
                   "] = " + wire_literal(bytes) + ";\n";
    };
    declare("first", fragment(0, 16, true));
    declare("middle", fragment(16, 16, true));
    declare("last", fragment(32, 7, false));
    declare("overlap", fragment(8, 16, true));
    declare("short_last", fragment(24, 7, false));
    auto conflict = fragment(0, 16, true);
    conflict.back() ^= 1;
    declare("conflict", conflict);
    auto other = fragment(16, 16, true);
    other[5] ^= 1;
    wire_ip_checksum(other);
    declare("other", other);
    declare("expected", full);
    program += R"(
        // Last-first delivery, duplicate first fragment and option preservation.
        if !incomplete(&r, last[..], 0) || !incomplete(&r, first[..], 1)
            || !incomplete(&r, first[..], 2) { return 2; }
        def done: result<ipv4.Datagram> = r.input(middle[..], 3);
        if done.erroneous { return 3; }
        if !done.value.complete || !equal(done.value.bytes, expected[..]) { return 4; }
        def udp: result<packet.UdpPacket> = packet.parse_udp_ipv4(done.value.bytes);
        if udp.erroneous { return 5; }
        if udp.value.payload.len != 31 || r.occupied() { return 6; }
        if storage[0] != 165 || storage[189] != 165 || storage[190] != 165
            || present[0] != 165 || present[129] != 165 { return 7; }
        // Partial overlap, changed duplicate bytes, contradictory final extent.
        if !incomplete(&r, first[..], 4) { return 8; }
        def partial: result<ipv4.Datagram> = r.input(overlap[..], 5);
        if !partial.erroneous { return 9; }
        if r.occupied() { return 9; }
        if !incomplete(&r, first[..], 6) { return 10; }
        def changed: result<ipv4.Datagram> = r.input(conflict[..], 7);
        if !changed.erroneous { return 11; }
        if r.occupied() { return 11; }
        if !incomplete(&r, middle[..], 8) { return 12; }
        def shorter: result<ipv4.Datagram> = r.input(short_last[..], 9);
        if !shorter.erroneous { return 13; }
        if r.occupied() { return 13; }
        // An unrelated datagram must not evict a live assembly. Duplicates cannot
        // keep it alive indefinitely. Expiry occurs exactly at the deadline.
        if !incomplete(&r, first[..], 10) { return 14; }
        def unrelated: result<ipv4.Datagram> = r.input(other[..], 11);
        if !unrelated.erroneous { return 15; }
        if !r.occupied() { return 15; }
        if !incomplete(&r, first[..], 30009) { return 16; }
        def expired: result<bool> = r.expire(30010);
        if expired.erroneous { return 17; }
        if !expired.value || r.occupied() { return 18; }
        def backward: result<ipv4.Datagram> = r.input(first[..], 30009);
        if !backward.erroneous { return 19; }
        if r.occupied() { return 19; }
        // Whole datagrams borrow the original input without disturbing a slot.
        if !incomplete(&r, first[..], 30011) { return 20; }
        def whole: result<ipv4.Datagram> = r.input(expected[..], 30012);
        if whole.erroneous { return 21; }
        if !whole.value.complete || !equal(whole.value.bytes, expected[..]) || !r.occupied() { return 22; }
        return 0;
    })";
    const auto file = source(program);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/ipv4-reassembly";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

namespace {
void network_test_certificate(const std::string &certificate, const std::string &key) {
    EVP_PKEY *pair = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1");
    ASSERT_NE(pair, nullptr);
    X509 *cert = X509_new();
    ASSERT_NE(cert, nullptr);
    ASSERT_EQ(X509_set_version(cert, 2), 1);
    ASSERT_EQ(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1), 1);
    ASSERT_NE(X509_gmtime_adj(X509_getm_notBefore(cert), -60), nullptr);
    ASSERT_NE(X509_gmtime_adj(X509_getm_notAfter(cert), 3600), nullptr);
    ASSERT_EQ(X509_set_pubkey(cert, pair), 1);
    auto *name = X509_get_subject_name(cert);
    ASSERT_EQ(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                         reinterpret_cast<const unsigned char *>("localhost"), -1,
                                         -1, 0),
              1);
    ASSERT_EQ(X509_set_issuer_name(cert, name), 1);
    auto *san = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:localhost");
    ASSERT_NE(san, nullptr);
    ASSERT_EQ(X509_add_ext(cert, san, -1), 1);
    X509_EXTENSION_free(san);
    ASSERT_GT(X509_sign(cert, pair, EVP_sha256()), 0);
    FILE *file = fopen(certificate.c_str(), "w");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(PEM_write_X509(file, cert), 1);
    ASSERT_EQ(fclose(file), 0);
    file = fopen(key.c_str(), "w");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(PEM_write_PrivateKey(file, pair, nullptr, nullptr, 0, nullptr, nullptr), 1);
    ASSERT_EQ(fclose(file), 0);
    X509_free(cert);
    EVP_PKEY_free(pair);
}
} // namespace

TEST_F(NetworkLibraryTest, TcpTableReuseAndHttpServerFramingWorkInAllModes) {
    for (const auto *name : {"tcp_table.gloin", "tcp_time_wait.gloin", "http_server.gloin",
                             "ip_stack.gloin", "ip_mtu.gloin"}) {
        const auto file =
            (std::filesystem::path(__FILE__).parent_path() / "fixtures/network" / name).string();
        SCOPED_TRACE(name);
        expect_success(invoke({file}), "");
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto output = directory + "/server-session";
            ASSERT_EQ(invoke_raw({mode, "-o", output, file}).status, 0);
            EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
        }
    }
}

TEST_F(NetworkLibraryTest, BufferedTlsAndHttpHttpsOverLossyGloinTcpWorkInAllModes) {
    const auto certificate = directory + "/cert.pem", key = directory + "/key.pem";
    ASSERT_NO_FATAL_FAILURE(network_test_certificate(certificate, key));
    for (const auto *name : {"tls_buffer.gloin", "http_tcp.gloin"}) {
        const auto file =
            (std::filesystem::path(__FILE__).parent_path() / "fixtures/network" / name).string();
        SCOPED_TRACE(name);
        expect_success(invoke({file, "--", certificate, key}), "");
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto output = directory + "/tcp-https";
            const auto compiled = invoke_raw({mode, "-o", output, file});
            ASSERT_EQ(compiled.status, 0) << compiled.err;
            EXPECT_EQ(
                llvm::sys::ExecuteAndWait(output, {output, certificate, key}, std::nullopt, {}, 30),
                0);
        }
    }
}

TEST_F(NetworkLibraryTest, Ipv6HeaderLayoutAndPseudoHeaderChecksumsWorkInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/ipv6_headers.gloin")
            .string();
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/ipv6-headers";
        const auto compiled = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(compiled.status, 0) << compiled.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

namespace {
WireBytes wire6(bool tcp, const WireBytes &payload, const WireBytes &options = {}) {
    const size_t head = tcp ? 20 + options.size() : 8;
    WireBytes bytes(40 + head + payload.size());
    bytes[0] = 0x6a;
    bytes[1] = 0xb1;
    bytes[2] = 0x23;
    bytes[3] = 0x45;
    wire_u16(bytes, 4, head + payload.size());
    bytes[6] = tcp ? 6 : 17;
    bytes[7] = 64;
    for (size_t at : {8u, 24u}) {
        bytes[at] = 0x20;
        bytes[at + 1] = 1;
        bytes[at + 2] = 0xd;
        bytes[at + 3] = 0xb8;
    }
    bytes[23] = 1;
    bytes[39] = 2;
    wire_u16(bytes, 40, 0x1234);
    wire_u16(bytes, 42, 0xabcd);
    if (tcp) {
        bytes[44] = 0xff;
        bytes[45] = 0xff;
        bytes[46] = 0xff;
        bytes[47] = 0xfe;
        bytes[52] = uint8_t((head / 4) << 4);
        bytes[53] = 0x12;
        wire_u16(bytes, 54, 4096);
        std::copy(options.begin(), options.end(), bytes.begin() + 60);
    } else
        wire_u16(bytes, 44, head + payload.size());
    std::copy(payload.begin(), payload.end(), bytes.begin() + 40 + head);
    WireBytes pseudo(bytes.begin() + 8, bytes.begin() + 40);
    pseudo.insert(pseudo.end(), {0, 0, bytes[4], bytes[5], 0, 0, 0, bytes[6]});
    pseudo.insert(pseudo.end(), bytes.begin() + 40, bytes.end());
    auto sum = wire_checksum(pseudo);
    if (!tcp && sum == 0)
        sum = 65535;
    wire_u16(bytes, tcp ? 56 : 46, sum);
    return bytes;
}
WireBytes extension6(WireBytes bytes, uint8_t kind, WireBytes extension) {
    extension[0] = bytes[6];
    bytes[6] = kind;
    bytes.insert(bytes.begin() + 40, extension.begin(), extension.end());
    wire_u16(bytes, 4, bytes.size() - 40);
    return bytes;
}
} // namespace

TEST_F(NetworkLibraryTest, Ipv6CodecsMatchIndependentChecksumsExtensionsAndAlignmentInAllModes) {
    std::string program =
        read((std::filesystem::path(__FILE__).parent_path() / "fixtures/network/ipv6_wire.gloin")
                 .string());
    program += "\ndef main() -> i32 {\n";
    unsigned index = 0;
    auto add = [&](WireBytes bytes, unsigned protocol, bool valid, bool sealable, bool canonical,
                   bool torture = false) {
        const auto name = "v" + std::to_string(index++);
        const auto boolean = [](bool v) { return v ? "true" : "false"; };
        program += "def " + name + ": [u8; " + std::to_string(bytes.size()) +
                   "] = " + wire_literal(bytes) + ";\n";
        program += "if check(" + name + "[..]," + std::to_string(protocol) + "," + boolean(valid) +
                   "," + boolean(sealable) + "," + boolean(canonical) + ") != 0 { return " +
                   std::to_string(index) + "; }\n";
        if (torture)
            program += "if truncated(" + name + "[..]," + std::to_string(protocol) +
                       ") != 0 || corrupted(" + name + "[..]," + std::to_string(protocol) +
                       ") != 0 { return 240; }\n";
    };
    for (bool tcp : {false, true}) {
        unsigned protocol = tcp ? 6 : 17;
        auto base = wire6(tcp, {0, 255, 128, 3, 127});
        add(base, protocol, true, true, true, true);
        add(wire6(tcp, {}), protocol, true, true, true, true);
        auto bad = base;
        bad.back() ^= 1;
        add(bad, protocol, false, true, false);
        bad = base;
        bad[0] = 0x4a;
        add(bad, protocol, false, false, false);
        bad = base;
        bad[4] = 255;
        add(bad, protocol, false, false, false);
        auto padded = base;
        padded.insert(padded.end(), {0xde, 0xad});
        add(padded, protocol, true, true, true);
        add(extension6(base, 0, {0, 0, 1, 4, 0, 0, 0, 0}), protocol, true, true, true);
        add(extension6(base, 60, {0, 0, 0x1e, 2, 0xab, 0xcd, 0, 0}), protocol, true, true, true);
        add(extension6(base, 43, {0, 0, 99, 0, 0, 0, 0, 0}), protocol, true, true, true);
        add(extension6(base, 43, {0, 0, 99, 1, 0, 0, 0, 0}), protocol, false, false, false);
        add(extension6(base, 44, {0, 0, 0, 0, 1, 2, 3, 4}), protocol, true, true, true);
        auto twice = extension6(extension6(base, 44, {0, 0, 0, 0, 1, 2, 3, 4}), 44,
                                {0, 0, 0, 0, 5, 6, 7, 8});
        add(twice, protocol, false, false, false);
        for (uint8_t action : {0x40, 0x80, 0xc0})
            add(extension6(base, 60, {0, 0, action, 2, 1, 2, 0, 0}), protocol, false, false, false);
        add(extension6(base, 60, {0, 0, 0x1e, 7, 0, 0, 0, 0}), protocol, false, false, false);
        add(extension6(extension6(base, 0, {0, 0, 0, 0, 0, 0, 0, 0}), 60, {0, 0, 0, 0, 0, 0, 0, 0}),
            protocol, false, false, false);
    }
    auto chain = wire6(false, {1, 2, 3});
    for (int i = 0; i < 8; ++i)
        chain = extension6(chain, 60, {0, 0, 0, 0, 0, 0, 0, 0});
    add(chain, 17, true, true, true);
    add(extension6(chain, 60, {0, 0, 0, 0, 0, 0, 0, 0}), 17, false, false, false);
    auto zero = wire6(false, {0, 0});
    wire_u16(zero, 46, 0);
    add(zero, 17, false, true, false);
    WireBytes pseudo(zero.begin() + 8, zero.begin() + 40);
    pseudo.insert(pseudo.end(), {0, 0, 0, 10, 0, 0, 0, 17});
    pseudo.insert(pseudo.end(), zero.begin() + 40, zero.end());
    wire_u16(zero, 48, wire_checksum(pseudo));
    wire_u16(zero, 46, 65535);
    add(zero, 17, true, true, true);
    for (const auto &options :
         {WireBytes{2, 4, 5, 0xa0}, WireBytes{30, 3, 0xff, 0}, WireBytes(40, 1)})
        add(wire6(true, {1, 2, 3}, options), 6, true, true, true);
    add(wire6(true, {1}, {2, 3, 0, 0}), 6, false, false, false);
    program += "return 0; }\n";
    const auto file = source(program);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        const auto output = directory + "/ipv6-wire";
        expect_success(invoke_raw({mode, "-o", output, file}), "");
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, Ipv6InitialSequenceBridgeWorksInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/ipv6_isn.gloin")
            .string();
    const auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    const auto position = ir.out.find("callee = @gloin_net_isn6_next}");
    ASSERT_NE(position, std::string::npos);
    const auto line = ir.out.rfind('\n', position);
    EXPECT_NE(ir.out.substr(line + 1, position - line).find("gloin.abi_call"), std::string::npos);
    EXPECT_EQ(ir.out.find("llvm.call @gloin_net_isn6_next"), std::string::npos);
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/ipv6-isn";
        const auto compiled = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(compiled.status, 0) << compiled.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, Ipv6ReassemblyAndUdpBoundsWorkInAllModes) {
    const auto file =
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/network/ipv6_reassembly.gloin")
            .string();
    expect_success(invoke({file}), "");
    for (const auto *mode : {"-O0", "-O2"}) {
        SCOPED_TRACE(mode);
        const auto output = directory + "/ipv6-reassembly";
        const auto compiled = invoke_raw({mode, "-o", output, file});
        ASSERT_EQ(compiled.status, 0) << compiled.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(NetworkLibraryTest, Ipv6EndpointDispatchAndPathMtuAreBoundedInAllModes) {
    for (const auto *name : {"ipv6_datagrams.gloin", "ipv6_mtu.gloin"}) {
        SCOPED_TRACE(name);
        const auto file =
            (std::filesystem::path(__FILE__).parent_path() / "fixtures/network" / name).string();
        expect_success(invoke({file}), "");
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto output = directory + "/ipv6-endpoint";
            const auto compiled = invoke_raw({mode, "-o", output, file});
            ASSERT_EQ(compiled.status, 0) << compiled.err;
            EXPECT_EQ(llvm::sys::ExecuteAndWait(output, {output}, std::nullopt, {}, 30), 0);
        }
    }
}
