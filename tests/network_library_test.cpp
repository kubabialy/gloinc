#include "support/cli_fixture.h"
#include <filesystem>

namespace {
class NetworkLibraryTest : public gloin_test::CliFixture {};
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
    if !parsed_no_body.erroneous { memory.free(); return 12; }
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
    def opened: result<net.Socket> = net.Socket.open(&owner);
    if opened.erroneous { owner.free(); return 4; }
    def mut socket: net.Socket = opened.value;
    def mut alias: net.Socket = socket;
    def closed: result<void> = socket.close();
    if closed.erroneous { owner.free(); return 5; }
    if alias.is_open() { owner.free(); return 6; }
    def attempted: result<net.WriteOutcome> = alias.write_text("");
    if !attempted.erroneous { owner.free(); return 7; }
    owner.free();
    return 0;
})");
    expect_success(invoke({file}), "");
}
