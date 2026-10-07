#include "support/cli_fixture.h"
#include <filesystem>

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
}

TEST_F(NetworkLibraryTest, ServerTlsCallsRetainTheGloinIRAbiBoundary) {
    const auto fixture = std::filesystem::path(__FILE__).parent_path() /
                         "fixtures/network/tls_sessions.gloin";
    const auto ir = invoke({"--emit-ir", fixture.string()});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const std::string name : {"gloin_net_tls_server_config", "gloin_net_tls_server_config_close",
                                  "gloin_net_tls_server_create", "gloin_net_tls_shutdown"}) {
        const auto position = ir.out.find("callee = @" + name + "}");
        ASSERT_NE(position, std::string::npos) << name;
        const auto line = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(line + 1, position - line).find("gloin.abi_call"), std::string::npos) << name;
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
    for (const std::string body : {
             "__net_tls_server_config_close(null);",
             "def loaded: result<net.TlsServerConfig> = net.TlsServerConfig.load(&owner, 1, 2); if loaded.erroneous { return 1; }",
             "def opened: result<net.Socket> = net.Socket.open(&owner); if opened.erroneous { return 1; } def mut socket: net.Socket = opened.value; socket.start_tls_server(&owner, 1);"}) {
        const auto invalid = source("import \"@net\"; import \"@arena\"; def main() -> i32 { "
            "def mut owner: arena.GeneralArena = arena.GeneralArena.create(); " + body + " return 0; }");
        const auto result = invoke({"--check", invalid});
        EXPECT_EQ(result.status, 1);
        EXPECT_FALSE(result.err.empty());
    }
}

TEST_F(NetworkLibraryTest, LocalHttpRoundTripWorksInJitAndNativeExecutable) {
    const auto example = std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "examples/network_http.gloin";
    expect_success(invoke({example.string()}), "nonblocking HTTP round trip succeeded\n");
    const auto output = directory + "/network-http";
    expect_success(invoke_raw({"-o", output, example.string()}), "");
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

TEST_F(NetworkLibraryTest, HttpRejectsAmbiguousFramingAndHeaderInjection) {
    const auto file = source(R"(import "@arena"; import "@http"; import "@status"; import "@strings";
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
    def following: strings.StringResult = strings.slice_bytes(framed_text, framed.value.consumed, 4);
    if following.status != status.OK || !strings.equal(following.value, "NEXT") { return 17; }
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
        def first: result<http.ResponseProgress> = reader.feed(strings.slice_bytes(wire, 0, split).value, false);
        if first.erroneous { return 2; }
        def second: result<http.ResponseProgress> = reader.feed(strings.slice_bytes(wire, split,
            strings.byte_length(wire) - split).value, eof);
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
        def step: result<http.ResponseProgress> = reader.feed(strings.slice_bytes(wire, i, 1).value, false);
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
                    def bytes: string = strings.slice_bytes(wire, cursor, end - cursor).value;
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
                        if output[i] != strings.byte_at("a\0bcde", count + i).value { return false; }
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
        def next: result<http.StreamProgress> = decoder.feed(strings.slice_bytes(suffix, cursor, 1).value, false, output[..]);
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
