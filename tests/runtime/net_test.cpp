#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct Socket {
    int32_t fd = -1;
    ~Socket() {
        if (fd >= 0) {
            int32_t error = 0;
            EXPECT_EQ(gloin_net_close(fd, &error), GLOIN_STD_OK);
        }
    }
};
struct Pipe {
    int fd[2]{-1, -1};
    ~Pipe() {
        if (fd[0] >= 0)
            close(fd[0]);
        if (fd[1] >= 0)
            close(fd[1]);
    }
};
} // namespace

TEST(NetRuntimeTest, LoopbackSocketsAreNonblockingAndReportProgress) {
    Socket listener, client, accepted;
    int32_t error = -1;
    ASSERT_EQ(gloin_net_open(&listener.fd, &error), GLOIN_STD_OK);
    ASSERT_EQ(error, 0);
    EXPECT_NE(fcntl(listener.fd, F_GETFL, 0) & O_NONBLOCK, 0);
    EXPECT_NE(fcntl(listener.fd, F_GETFD, 0) & FD_CLOEXEC, 0);
    EXPECT_EQ(gloin_net_reuse_address(listener.fd, 2, &error), GLOIN_STD_INVALID);
    ASSERT_EQ(gloin_net_reuse_address(listener.fd, 1, &error), GLOIN_STD_OK);
    int enabled = 0;
    socklen_t size = sizeof(enabled);
    ASSERT_EQ(getsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &enabled, &size), 0);
    EXPECT_NE(enabled, 0);
    EXPECT_EQ(gloin_net_reuse_address(-1, 1, &error), GLOIN_STD_CLOSED);
    ASSERT_EQ(gloin_net_bind(listener.fd, 0x7f000001, 0, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_net_listen(listener.fd, 4, &error), GLOIN_STD_OK);
    uint32_t address = 0;
    uint16_t port = 0;
    ASSERT_EQ(gloin_net_local(listener.fd, &address, &port, &error), GLOIN_STD_OK);
    EXPECT_EQ(address, 0x7f000001u);
    ASSERT_NE(port, 0);
    const uint16_t listening_port = port;
    int32_t no_client = 7;
    EXPECT_EQ(gloin_net_accept(listener.fd, &no_client, &address, &port, &error),
              GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(no_client, -1);

    ASSERT_EQ(gloin_net_open(&client.fd, &error), GLOIN_STD_OK);
    int32_t connection = gloin_net_connect(client.fd, 0x7f000001, listening_port, &error);
    ASSERT_TRUE(connection == GLOIN_STD_OK || connection == GLOIN_STD_IN_PROGRESS);
    int32_t ready = 0;
    ASSERT_EQ(gloin_net_wait(listener.fd, 1, 1000, &ready, &error), GLOIN_STD_OK);
    EXPECT_EQ(ready & 1, 1);
    std::array<Pipe, 65> pipes;
    std::array<int32_t, 65> many_fds, many_events, many_ready{};
    for (size_t i = 0; i < pipes.size(); ++i) {
        ASSERT_EQ(pipe(pipes[i].fd), 0);
        many_fds[i] = pipes[i].fd[0];
    }
    const char signal = 'x';
    ASSERT_EQ(write(pipes.back().fd[1], &signal, 1), 1);
    many_events.fill(1);
    uint64_t many_count = 0;
    ASSERT_EQ(gloin_net_wait_many(many_fds.data(), many_events.data(), many_ready.data(),
                                  many_fds.size(), 0, &many_count, &error),
              GLOIN_STD_OK);
    EXPECT_EQ(many_count, 1u);
    for (size_t i = 0; i < many_ready.size(); ++i)
        EXPECT_EQ(many_ready[i], i + 1 == many_ready.size() ? 1 : 0);
    ASSERT_EQ(gloin_net_accept(listener.fd, &accepted.fd, &address, &port, &error), GLOIN_STD_OK);
    EXPECT_EQ(address, 0x7f000001u);
    EXPECT_NE(fcntl(accepted.fd, F_GETFL, 0) & O_NONBLOCK, 0);
    EXPECT_NE(fcntl(accepted.fd, F_GETFD, 0) & FD_CLOEXEC, 0);
    ASSERT_EQ(gloin_net_finish_connect(client.fd, &error), GLOIN_STD_OK);

    std::array<uint8_t, 8> bytes{};
    uint64_t count = 99;
    EXPECT_EQ(gloin_net_recv(accepted.fd, bytes.data(), bytes.size(), &count, &error),
              GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(count, 0u);
    const char payload[] = "Gloin";
    ASSERT_EQ(gloin_net_send_text(client.fd, payload, 5, &count, &error), GLOIN_STD_OK);
    ASSERT_EQ(count, 5u);
    ASSERT_EQ(gloin_net_wait(accepted.fd, 1, 1000, &ready, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_net_recv(accepted.fd, bytes.data(), bytes.size(), &count, &error),
              GLOIN_STD_OK);
    EXPECT_EQ(count, 5u);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes.data()), count), "Gloin");
    EXPECT_EQ(gloin_net_wait(accepted.fd, 1, 0, &ready, &error), GLOIN_STD_WOULD_BLOCK);
    std::array<int32_t, 2> descriptors{listener.fd, accepted.fd};
    std::array<int32_t, 2> requested{1, 1};
    std::array<int32_t, 2> results{7, 7};
    uint64_t ready_count = 99;
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(),
                                  results.size(), 0, &ready_count, &error),
              GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(ready_count, 0u);
    ASSERT_EQ(gloin_net_shutdown_write(client.fd, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(),
                                  results.size(), 1000, &ready_count, &error),
              GLOIN_STD_OK);
    EXPECT_EQ(ready_count, 1u);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
    ASSERT_EQ(gloin_net_recv(accepted.fd, bytes.data(), bytes.size(), &count, &error),
              GLOIN_STD_EOF);
    EXPECT_EQ(count, 0u);
}

TEST(NetRuntimeTest, InvalidDescriptorsAndWaitArgumentsAreRecoverable) {
    int32_t error = 99, ready = 99;
    EXPECT_EQ(gloin_net_close(-1, &error), GLOIN_STD_CLOSED);
    EXPECT_EQ(error, 0);
    Socket socket;
    ASSERT_EQ(gloin_net_open(&socket.fd, &error), GLOIN_STD_OK);
    EXPECT_EQ(gloin_net_wait(socket.fd, 0, 0, &ready, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(ready, 0);
    EXPECT_EQ(gloin_net_wait(socket.fd, 1, -1, &ready, &error), GLOIN_STD_INVALID);
    uint64_t count = 99;
    EXPECT_EQ(gloin_net_send(socket.fd, nullptr, 0, &count, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    std::array<int32_t, 1> descriptors{socket.fd}, requested{0}, results{7};
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 1, 0,
                                  &count, &error),
              GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 0, 0,
                                  &count, &error),
              GLOIN_STD_INVALID);
    std::array<int32_t, 2> duplicate_fds{socket.fd, socket.fd};
    std::array<int32_t, 2> duplicate_events{1, 1}, duplicate_ready{7, 7};
    EXPECT_EQ(gloin_net_wait_many(duplicate_fds.data(), duplicate_events.data(),
                                  duplicate_ready.data(), 2, 0, &count, &error),
              GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 4097, 0,
                                  &count, &error),
              GLOIN_STD_INVALID);
}

TEST(NetRuntimeTest, NumericHostnameResolutionIsCountedAndBounded) {
    uint32_t host = 0;
    uint64_t count = 99;
    int32_t error = 99;
    ASSERT_EQ(gloin_net_resolve_ipv4("127.0.0.1", 9, &host, 1, &count, &error), GLOIN_STD_OK);
    EXPECT_EQ(host, 0x7f000001u);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(error, 0);
    const char embedded[] = {'l', 'o', 0, 'c', 'a', 'l'};
    EXPECT_EQ(gloin_net_resolve_ipv4(embedded, sizeof(embedded), &host, 1, &count, &error),
              GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(gloin_net_resolve_ipv4("127.0.0.1", 9, &host, 0, &count, &error), GLOIN_STD_INVALID);
}

TEST(NetRuntimeTest, RawPacketTransportPreservesDatagramsAndBoundsWithBothFramings) {
    for (int framing : {0, 4}) {
        Pipe pair;
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair.fd), 0);
        ASSERT_EQ(fcntl(pair.fd[0], F_SETFL, O_NONBLOCK), 0);
        ASSERT_EQ(fcntl(pair.fd[1], F_SETFL, O_NONBLOCK), 0);
        const std::array<uint8_t, 20> packet{0x45, 0, 0,   20, 0, 1, 0x40, 0, 64, 1,
                                             0,    0, 192, 0,  2, 1, 192,  0, 2,  2};
        std::array<uint8_t, 65577> storage{};
        storage.fill(0xa5);
        uint64_t count = 99;
        int32_t error = 99;
        ASSERT_EQ(gloin_net_packet_write(pair.fd[0], framing, packet.data(), packet.size(), &count,
                                         &error),
                  GLOIN_STD_OK);
        ASSERT_EQ(count, packet.size());
        ASSERT_EQ(error, 0);
        // Short buffers are rejected before consuming the queued datagram.
        EXPECT_EQ(
            gloin_net_packet_read(pair.fd[1], framing, storage.data() + 1, 20, &count, &error),
            GLOIN_STD_INVALID);
        ASSERT_EQ(
            gloin_net_packet_read(pair.fd[1], framing, storage.data() + 1, 65575, &count, &error),
            GLOIN_STD_OK);
        ASSERT_EQ(count, packet.size());
        for (size_t i = 0; i < packet.size(); ++i)
            EXPECT_EQ(storage[i + 1], packet[i]);
        EXPECT_EQ(storage.front(), 0xa5);
        EXPECT_EQ(storage.back(), 0xa5);
        for (size_t i = packet.size() + 1; i < storage.size(); ++i)
            EXPECT_EQ(storage[i], 0xa5);
        EXPECT_EQ(gloin_net_packet_read(pair.fd[1], framing, storage.data(), 65575, &count, &error),
                  GLOIN_STD_WOULD_BLOCK);
        EXPECT_EQ(count, 0u);
        EXPECT_EQ(gloin_net_packet_write(pair.fd[0], framing, packet.data(), 19, &count, &error),
                  GLOIN_STD_INVALID);
        EXPECT_EQ(
            gloin_net_packet_write(pair.fd[0], 3, packet.data(), packet.size(), &count, &error),
            GLOIN_STD_INVALID);
    }
}

TEST(NetRuntimeTest, TcpInitialSequenceUsesSecretTupleAndWrappingFourMicrosecondClock) {
    std::array<uint8_t, 32> secret{};
    for (size_t i = 0; i < secret.size(); ++i)
        secret[i] = uint8_t(i);
    uint32_t sequence = 0;
    // Python hashlib/hmac SHA256 independent known answer:
    // c9523f0180ef6480a94b382b86515b85fe447f950d1ad0dcb11cd39965bfe3fd
    ASSERT_EQ(gloin_net_isn_next(secret.data(), 0xc0000201, 1234, 0xc6336402, 80, 0, &sequence),
              GLOIN_STD_OK);
    EXPECT_EQ(sequence, 3377610497u);
    ASSERT_EQ(gloin_net_isn_next(secret.data(), 0xc0000201, 1234, 0xc6336402, 80, 3, &sequence),
              GLOIN_STD_OK);
    EXPECT_EQ(sequence, 3377610497u);
    ASSERT_EQ(gloin_net_isn_next(secret.data(), 0xc0000201, 1234, 0xc6336402, 80, 4, &sequence),
              GLOIN_STD_OK);
    EXPECT_EQ(sequence, 3377610498u);
    ASSERT_EQ(
        gloin_net_isn_next(secret.data(), 0xc0000201, 1234, 0xc6336402, 80, UINT64_MAX, &sequence),
        GLOIN_STD_OK);
    EXPECT_EQ(sequence, 3377610496u);
    EXPECT_EQ(gloin_net_isn_next(secret.data(), 0xc0000201, 0, 0xc6336402, 80, 0, &sequence),
              GLOIN_STD_INVALID);
    EXPECT_EQ(sequence, 0u);
    std::array<uint8_t, 32> first{}, second{};
    ASSERT_EQ(gloin_net_isn_seed(first.data()), GLOIN_STD_OK);
    ASSERT_EQ(gloin_net_isn_seed(second.data()), GLOIN_STD_OK);
    EXPECT_NE(first, second);
    EXPECT_EQ(gloin_net_isn_seed(nullptr), GLOIN_STD_INVALID);
}

TEST(NetRuntimeTest, Ipv6RawTransportChecksFamilyAndFullMaximumPacket) {
    for (int framing : {0, 4}) {
        Pipe pair;
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair.fd), 0);
        ASSERT_EQ(fcntl(pair.fd[0], F_SETFL, O_NONBLOCK), 0);
        ASSERT_EQ(fcntl(pair.fd[1], F_SETFL, O_NONBLOCK), 0);
        const int capacity = 262144;
        ASSERT_EQ(setsockopt(pair.fd[0], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof capacity), 0);
        ASSERT_EQ(setsockopt(pair.fd[1], SOL_SOCKET, SO_RCVBUF, &capacity, sizeof capacity), 0);
        std::array<uint8_t, 65575> packet{};
        packet[0] = 0x60;
        packet[4] = 255;
        packet[5] = 255;
        packet[6] = 59;
        packet[7] = 64;
        packet[8] = 0xfd;
        packet[23] = 1;
        packet[24] = 0xfd;
        packet[39] = 2;
        for (size_t i = 40; i < packet.size(); ++i)
            packet[i] = uint8_t(i);
        std::array<uint8_t, 65577> output{};
        output.fill(0xa5);
        uint64_t count = 0;
        int32_t detail = 0;
        ASSERT_EQ(gloin_net_packet_write(pair.fd[0], framing, packet.data(), packet.size(), &count,
                                         &detail),
                  GLOIN_STD_OK);
        ASSERT_EQ(count, packet.size());
        EXPECT_EQ(
            gloin_net_packet_read(pair.fd[1], framing, output.data() + 1, 65535, &count, &detail),
            GLOIN_STD_INVALID);
        ASSERT_EQ(
            gloin_net_packet_read(pair.fd[1], framing, output.data() + 1, 65575, &count, &detail),
            GLOIN_STD_OK);
        ASSERT_EQ(count, packet.size());
        EXPECT_TRUE(std::equal(packet.begin(), packet.end(), output.begin() + 1));
        EXPECT_EQ(output.front(), 0xa5);
        EXPECT_EQ(output.back(), 0xa5);
        // A larger datagram with a valid maximum-size prefix must not be
        // silently truncated and returned as a different, valid packet.
        std::array<uint8_t, 65580> oversized{};
        const uint32_t ipv6_family = htonl(AF_INET6);
        if (framing)
            memcpy(oversized.data(), &ipv6_family, 4);
        memcpy(oversized.data() + framing, packet.data(), packet.size());
        oversized[size_t(framing) + packet.size()] = 0x42;
        const size_t sent_size = size_t(framing) + packet.size() + 1;
        ASSERT_EQ(send(pair.fd[0], oversized.data(), sent_size, 0), ssize_t(sent_size));
        EXPECT_EQ(
            gloin_net_packet_read(pair.fd[1], framing, output.data() + 1, 65575, &count, &detail),
            GLOIN_STD_WOULD_BLOCK);
        EXPECT_EQ(count, 0u);
        EXPECT_EQ(output.front(), 0xa5);
        EXPECT_EQ(output.back(), 0xa5);
        EXPECT_EQ(gloin_net_packet_write(pair.fd[0], framing, packet.data(), packet.size() - 1,
                                         &count, &detail),
                  GLOIN_STD_INVALID);
        if (framing) {
            std::array<uint8_t, 44> mismatched{};
            const uint32_t family = htonl(AF_INET);
            memcpy(mismatched.data(), &family, 4);
            mismatched[4] = 0x60;
            mismatched[10] = 59;
            ASSERT_EQ(send(pair.fd[0], mismatched.data(), mismatched.size(), 0),
                      ssize_t(mismatched.size()));
            EXPECT_EQ(gloin_net_packet_read(pair.fd[1], framing, output.data() + 1, 65575, &count,
                                            &detail),
                      GLOIN_STD_WOULD_BLOCK);
            EXPECT_EQ(count, 0u);
        }
    }
}

TEST(NetRuntimeTest, Ipv6InitialSequenceUsesAllAddressBitsAndIndependentKnownAnswer) {
    std::array<uint8_t, 32> secret{};
    for (size_t i = 0; i < secret.size(); ++i)
        secret[i] = uint8_t(i);
    const std::array<uint8_t, 16> local{0x20, 1, 0x0d, 0xb8, 0x12, 0x34, 0x56, 0x78,
                                        0,    0, 0,    0,    0,    0,    0,    1};
    std::array<uint8_t, 16> remote{0xfd, 0, 0xab, 0xcd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};
    uint32_t sequence = 0;
    // Python hmac/hashlib: a7c9cf9e829b3d24de1b0bde5aeaa3a5...
    ASSERT_EQ(
        gloin_net_isn6_next(secret.data(), local.data(), 1234, remote.data(), 443, 0, &sequence),
        GLOIN_STD_OK);
    EXPECT_EQ(sequence, 2815020958u);
    ASSERT_EQ(
        gloin_net_isn6_next(secret.data(), local.data(), 1234, remote.data(), 443, 4, &sequence),
        GLOIN_STD_OK);
    EXPECT_EQ(sequence, 2815020959u);
    for (size_t i = 0; i < remote.size(); ++i) {
        remote[i] ^= 1;
        ASSERT_EQ(gloin_net_isn6_next(secret.data(), local.data(), 1234, remote.data(), 443, 0,
                                      &sequence),
                  GLOIN_STD_OK);
        EXPECT_NE(sequence, 2815020958u);
        remote[i] ^= 1;
    }
    EXPECT_EQ(gloin_net_isn6_next(secret.data(), nullptr, 1234, remote.data(), 443, 0, &sequence),
              GLOIN_STD_INVALID);
    EXPECT_EQ(sequence, 0u);
}
