#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
class TlsRuntimeTest : public ::testing::Test {
  protected:
    std::string directory, certificate, key;
    void SetUp() override {
        char pattern[] = "/tmp/gloin-tls-native-XXXXXX";
        const char *created = mkdtemp(pattern);
        ASSERT_NE(created, nullptr);
        directory = created;
        certificate = directory + "/certificate.pem";
        key = directory + "/key.pem";
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
                                             reinterpret_cast<const unsigned char *>("localhost"),
                                             -1, -1, 0),
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
        file = fopen((directory + "/encrypted.pem").c_str(), "w");
        ASSERT_NE(file, nullptr);
        unsigned char password[] = "secret";
        ASSERT_EQ(
            PEM_write_PrivateKey(file, pair, EVP_aes_256_cbc(), password, 6, nullptr, nullptr), 1);
        ASSERT_EQ(fclose(file), 0);
        file = fopen((directory + "/empty-password.pem").c_str(), "w");
        ASSERT_NE(file, nullptr);
        ASSERT_EQ(
            PEM_write_PrivateKey(file, pair, EVP_aes_256_cbc(), password, 0, nullptr, nullptr), 1);
        ASSERT_EQ(fclose(file), 0);
        EVP_PKEY *other = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1");
        ASSERT_NE(other, nullptr);
        file = fopen((directory + "/mismatched.pem").c_str(), "w");
        ASSERT_NE(file, nullptr);
        ASSERT_EQ(PEM_write_PrivateKey(file, other, nullptr, nullptr, 0, nullptr, nullptr), 1);
        ASSERT_EQ(fclose(file), 0);
        EVP_PKEY_free(other);
        X509_free(cert);
        EVP_PKEY_free(pair);
    }
    void TearDown() override {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }
    void *load() {
        void *config = nullptr;
        int32_t detail = 0;
        EXPECT_EQ(gloin_net_tls_server_config(certificate.data(), certificate.size(), key.data(),
                                              key.size(), &config, &detail),
                  GLOIN_STD_OK);
        return config;
    }
};

struct Exchange {
    int descriptors[2]{-1, -1};
    void *server = nullptr;
    SSL_CTX *context = nullptr;
    SSL *peer = nullptr; // Independent direct OpenSSL client, never Gloin's TLS wrapper.
    ~Exchange() {
        if (server)
            gloin_net_tls_close(server);
        SSL_free(peer);
        SSL_CTX_free(context);
        for (int fd : descriptors)
            if (fd >= 0)
                close(fd);
    }
    bool start(void *config, const std::string &trust, int protocol = 0) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors))
            return false;
        for (int fd : descriptors) {
            if (fcntl(fd, F_SETFL, O_NONBLOCK))
                return false;
            const int small = 4096;
            if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof small))
                return false;
#ifdef __APPLE__
            const int enabled = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled))
                return false;
#endif
        }
        int32_t detail = 0;
        if (gloin_net_tls_server_create(descriptors[0], config, &server, &detail) != GLOIN_STD_OK)
            return false;
        context = SSL_CTX_new(TLS_client_method());
        if (!context || SSL_CTX_load_verify_file(context, trust.c_str()) != 1)
            return false;
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
        if (protocol && (SSL_CTX_set_min_proto_version(context, protocol) != 1 ||
                         SSL_CTX_set_max_proto_version(context, protocol) != 1))
            return false;
        peer = SSL_new(context);
        if (!peer || SSL_set_fd(peer, descriptors[1]) != 1 || SSL_set1_host(peer, "localhost") != 1)
            return false;
        SSL_set_connect_state(peer);
        return true;
    }
    bool handshake() {
        bool server_ready = false, peer_ready = false;
        for (int i = 0; i < 1000 && (!server_ready || !peer_ready); ++i) {
            if (!server_ready) {
                int32_t wait = 0, detail = 0;
                const int status = gloin_net_tls_handshake(server, &wait, &detail);
                if (status != GLOIN_STD_OK && status != GLOIN_STD_WOULD_BLOCK)
                    return false;
                server_ready = status == GLOIN_STD_OK;
            }
            if (!peer_ready) {
                ERR_clear_error();
                const int result = SSL_connect(peer);
                if (result != 1) {
                    const int error = SSL_get_error(peer, result);
                    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
                        return false;
                } else
                    peer_ready = true;
            }
        }
        return server_ready && peer_ready;
    }
    void abort_peer() {
        SSL_free(peer);
        peer = nullptr;
        close(descriptors[1]);
        descriptors[1] = -1;
    }
};
} // namespace

TEST_F(TlsRuntimeTest, ConfigurationRejectsBadPathsKeysAndEncryptedKeysWithoutPrompting) {
    for (const std::string path :
         {std::string(), certificate + std::string("\0tail", 5), std::string(4097, 'x')}) {
        void *config = reinterpret_cast<void *>(1);
        int32_t detail = 99;
        EXPECT_EQ(gloin_net_tls_server_config(path.data(), path.size(), key.data(), key.size(),
                                              &config, &detail),
                  GLOIN_STD_INVALID);
        EXPECT_EQ(config, nullptr);
    }
    for (const auto &path : {directory + "/absent", certificate, directory + "/encrypted.pem",
                             directory + "/empty-password.pem", directory + "/mismatched.pem"}) {
        void *config = nullptr;
        int32_t detail = 0;
        EXPECT_EQ(gloin_net_tls_server_config(certificate.data(), certificate.size(), path.data(),
                                              path.size(), &config, &detail),
                  GLOIN_STD_IO_ERROR);
        EXPECT_EQ(config, nullptr);
    }
    void *config = load();
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
}

TEST_F(TlsRuntimeTest, ReusableConfigurationSurvivesCloseAndInteroperatesWithTls12And13) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange first, second;
    ASSERT_TRUE(first.start(config, certificate, TLS1_2_VERSION));
    ASSERT_TRUE(second.start(config, certificate, TLS1_3_VERSION));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    ASSERT_TRUE(first.handshake());
    ASSERT_TRUE(second.handshake());
    EXPECT_EQ(SSL_version(first.peer), TLS1_2_VERSION);
    EXPECT_EQ(SSL_version(second.peer), TLS1_3_VERSION);
}

TEST_F(TlsRuntimeTest, ServerRefusesProtocolsBelowTls12) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange channel;
    ASSERT_TRUE(channel.start(config, certificate, TLS1_1_VERSION));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    SSL_set_security_level(channel.peer, 0); // Allow the peer to offer the old version.
    EXPECT_FALSE(channel.handshake());
}

TEST_F(TlsRuntimeTest, BinaryPartialWritesResumeAfterBackpressure) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange channel;
    ASSERT_TRUE(channel.start(config, certificate));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    ASSERT_TRUE(channel.handshake());
    std::vector<uint8_t> input(262144), output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = uint8_t(i);
    size_t sent = 0, received = 0;
    bool blocked = false;
    for (int i = 0; i < 10000 && received < input.size(); ++i) {
        if (sent < input.size()) {
            uint64_t count = 0;
            int32_t wait = 0, detail = 0;
            const int status = gloin_net_tls_write(channel.server, input.data() + sent,
                                                   input.size() - sent, &count, &wait, &detail);
            ASSERT_TRUE(status == GLOIN_STD_OK || status == GLOIN_STD_WOULD_BLOCK)
                << status << ":" << detail;
            sent += count;
            blocked |= status == GLOIN_STD_WOULD_BLOCK;
        }
        if (!blocked)
            continue; // Force a real blocked write before draining.
        size_t count = 0;
        ERR_clear_error();
        const int result =
            SSL_read_ex(channel.peer, output.data() + received, output.size() - received, &count);
        if (result != 1) {
            const int error = SSL_get_error(channel.peer, result);
            ASSERT_TRUE(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
        }
        received += count;
    }
    EXPECT_TRUE(blocked);
    EXPECT_EQ(sent, input.size());
    EXPECT_EQ(received, input.size());
    EXPECT_EQ(output, input);
    // The other direction preserves embedded NUL bytes too.
    size_t written = 0;
    ASSERT_EQ(SSL_write_ex(channel.peer, "a\0b", 3, &written), 1);
    std::array<uint8_t, 4> bytes{};
    uint64_t count = 0;
    int32_t wait = 0, detail = 0;
    ASSERT_EQ(
        gloin_net_tls_read(channel.server, bytes.data(), bytes.size(), &count, &wait, &detail),
        GLOIN_STD_OK);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(bytes.data()), count), std::string("a\0b", 3));
}

TEST_F(TlsRuntimeTest, ShutdownWaitsForAuthenticatedPeerAlertAndIsRepeatable) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange channel;
    ASSERT_TRUE(channel.start(config, certificate));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    ASSERT_TRUE(channel.handshake());
    int32_t wait = 0, detail = 0;
    ASSERT_EQ(gloin_net_tls_shutdown(channel.server, &wait, &detail), GLOIN_STD_WOULD_BLOCK);
    EXPECT_EQ(wait, 1);
    uint64_t count = 99;
    const uint8_t byte = 'x';
    EXPECT_EQ(gloin_net_tls_write(channel.server, &byte, 1, &count, &wait, &detail),
              GLOIN_STD_INVALID);
    EXPECT_EQ(count, 0u);
    uint8_t output = 0;
    ERR_clear_error();
    const int read = SSL_read(channel.peer, &output, 1);
    EXPECT_EQ(SSL_get_error(channel.peer, read), SSL_ERROR_ZERO_RETURN);
    ASSERT_EQ(SSL_shutdown(channel.peer), 1);
    EXPECT_EQ(gloin_net_tls_shutdown(channel.server, &wait, &detail), GLOIN_STD_OK);
    EXPECT_EQ(wait, 0);
    EXPECT_EQ(gloin_net_tls_shutdown(channel.server, &wait, &detail), GLOIN_STD_OK);
    EXPECT_EQ(gloin_net_tls_read(channel.server, &output, 1, &count, &wait, &detail),
              GLOIN_STD_EOF);
}

TEST_F(TlsRuntimeTest, TruncatedTransportIsFatalAndCleanupDoesNotAttemptProtocolIo) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange channel;
    ASSERT_TRUE(channel.start(config, certificate));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    ASSERT_TRUE(channel.handshake());
    channel.abort_peer();
    uint8_t output = 0;
    uint64_t count = 0;
    int32_t wait = 0, detail = 0;
    EXPECT_EQ(gloin_net_tls_read(channel.server, &output, 1, &count, &wait, &detail),
              GLOIN_STD_IO_ERROR);
    EXPECT_EQ(gloin_net_tls_shutdown(channel.server, &wait, &detail), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_net_tls_handshake(channel.server, &wait, &detail), GLOIN_STD_INVALID);
    EXPECT_EQ(gloin_net_tls_write(channel.server, &output, 1, &count, &wait, &detail),
              GLOIN_STD_INVALID);
}

TEST_F(TlsRuntimeTest, BrokenPeerWriteDoesNotChangeHostSigpipePolicy) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    Exchange channel;
    ASSERT_TRUE(channel.start(config, certificate));
    ASSERT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
    ASSERT_TRUE(channel.handshake());
    struct sigaction before{}, after{};
    sigset_t mask_before{}, mask_after{};
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &before), 0);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, nullptr, &mask_before), 0);
    channel.abort_peer();
    uint8_t byte = 0;
    uint64_t count = 0;
    int32_t wait = 0, detail = 0;
    EXPECT_EQ(gloin_net_tls_write(channel.server, &byte, 1, &count, &wait, &detail),
              GLOIN_STD_IO_ERROR);
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &after), 0);
    EXPECT_EQ(after.sa_handler, before.sa_handler);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, nullptr, &mask_after), 0);
    EXPECT_EQ(sigismember(&mask_after, SIGPIPE), sigismember(&mask_before, SIGPIPE));
}

TEST_F(TlsRuntimeTest, BufferedTlsVerifiesHostnameAppliesBackpressureAndRejectsBareEof) {
    void *config = load();
    ASSERT_NE(config, nullptr);
    for (const char *hostname : {"localhost", "wrong.example"}) {
        void *client = nullptr, *server = nullptr;
        int32_t detail = 0, wait = 0;
        ASSERT_EQ(gloin_net_tls_buffer_create(1024, hostname, strlen(hostname), certificate.data(),
                                              certificate.size(), &client, &detail),
                  GLOIN_STD_OK);
        ASSERT_EQ(gloin_net_tls_buffer_server_create(1024, config, &server, &detail), GLOIN_STD_OK);
        struct Buffer {
            std::array<uint8_t, 83> bytes{};
            uint64_t start = 0, end = 0;
        };
        Buffer directions[2];
        void *sessions[] = {client, server};
        bool ready[] = {false, false};
        bool rejected = false;
        for (int iteration = 0; iteration < 10000 && !rejected && (!ready[0] || !ready[1]);
             ++iteration) {
            for (int i = 0; i < 2; ++i) {
                if (!ready[i]) {
                    const int code = gloin_net_tls_handshake(sessions[i], &wait, &detail);
                    if (code != GLOIN_STD_OK && code != GLOIN_STD_WOULD_BLOCK) {
                        rejected = true;
                        break;
                    }
                    ready[i] = code == GLOIN_STD_OK;
                }
            }
            if (rejected)
                break;
            for (int i = 0; i < 2; ++i) {
                auto &buffer = directions[i];
                uint64_t count = 0;
                if (buffer.start == buffer.end) {
                    const int code = gloin_net_tls_buffer_drain(sessions[i], buffer.bytes.data(),
                                                                buffer.bytes.size(), &count);
                    ASSERT_TRUE(code == GLOIN_STD_OK || code == GLOIN_STD_WOULD_BLOCK);
                    buffer.start = 0;
                    buffer.end = count;
                }
                if (buffer.start != buffer.end) {
                    const int code = gloin_net_tls_buffer_feed(
                        sessions[1 - i], buffer.bytes.data() + buffer.start,
                        buffer.end - buffer.start, 0, &count);
                    ASSERT_TRUE(code == GLOIN_STD_OK || code == GLOIN_STD_WOULD_BLOCK);
                    buffer.start += count;
                }
            }
        }
        if (strcmp(hostname, "localhost") == 0) {
            ASSERT_FALSE(rejected);
            ASSERT_TRUE(ready[0] && ready[1]);
            uint64_t count = 99;
            EXPECT_EQ(gloin_net_tls_buffer_feed(server, nullptr, 0, 1, &count), GLOIN_STD_OK);
            EXPECT_EQ(count, 0u);
            std::array<uint8_t, 1> output{};
            EXPECT_EQ(
                gloin_net_tls_read(server, output.data(), output.size(), &count, &wait, &detail),
                GLOIN_STD_IO_ERROR);
            EXPECT_EQ(count, 0u); // Missing close_notify is truncation, never clean EOF.
            EXPECT_EQ(gloin_net_tls_buffer_feed(server, nullptr, 0, 1, &count), GLOIN_STD_INVALID);
        } else {
            EXPECT_TRUE(rejected);
            EXPECT_FALSE(ready[0]);
        }
        EXPECT_EQ(gloin_net_tls_close(client), GLOIN_STD_OK);
        EXPECT_EQ(gloin_net_tls_close(server), GLOIN_STD_OK);
    }
    for (uint64_t capacity : {0u, 1023u, 1048577u}) {
        void *session = reinterpret_cast<void *>(1);
        int32_t detail = 99;
        EXPECT_EQ(gloin_net_tls_buffer_server_create(capacity, config, &session, &detail),
                  GLOIN_STD_INVALID);
        EXPECT_EQ(session, nullptr);
        EXPECT_EQ(detail, 0);
    }
    EXPECT_EQ(gloin_net_tls_server_config_close(config), GLOIN_STD_OK);
}
