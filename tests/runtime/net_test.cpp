#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <array>
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
        if (fd[0] >= 0) close(fd[0]);
        if (fd[1] >= 0) close(fd[1]);
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
                                  many_fds.size(), 0, &many_count, &error), GLOIN_STD_OK);
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
                                  results.size(), 0, &ready_count, &error), GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(ready_count, 0u);
    ASSERT_EQ(gloin_net_shutdown_write(client.fd, &error), GLOIN_STD_OK);
    ASSERT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(),
                                  results.size(), 1000, &ready_count, &error), GLOIN_STD_OK);
    EXPECT_EQ(ready_count, 1u);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
    ASSERT_EQ(gloin_net_recv(accepted.fd, bytes.data(), bytes.size(), &count, &error), GLOIN_STD_EOF);
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
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 1,
                                  0, &count, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 0,
                                  0, &count, &error), GLOIN_STD_INVALID);
    std::array<int32_t, 2> duplicate_fds{socket.fd, socket.fd};
    std::array<int32_t, 2> duplicate_events{1, 1}, duplicate_ready{7, 7};
    EXPECT_EQ(gloin_net_wait_many(duplicate_fds.data(), duplicate_events.data(),
                                  duplicate_ready.data(), 2,
                                  0, &count, &error), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_net_wait_many(descriptors.data(), requested.data(), results.data(), 4097,
                                  0, &count, &error), GLOIN_STD_INVALID);
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
    EXPECT_EQ(gloin_net_resolve_ipv4("127.0.0.1", 9, &host, 0, &count, &error),
              GLOIN_STD_INVALID);
}
