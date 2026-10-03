#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <array>
#include <fcntl.h>
#include <gtest/gtest.h>

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
} // namespace

TEST(NetRuntimeTest, LoopbackSocketsAreNonblockingAndReportProgress) {
    Socket listener, client, accepted;
    int32_t error = -1;
    ASSERT_EQ(gloin_net_open(&listener.fd, &error), GLOIN_STD_OK);
    ASSERT_EQ(error, 0);
    EXPECT_NE(fcntl(listener.fd, F_GETFL, 0) & O_NONBLOCK, 0);
    EXPECT_NE(fcntl(listener.fd, F_GETFD, 0) & FD_CLOEXEC, 0);
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
}
