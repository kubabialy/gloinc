#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <climits>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>
#include <pthread.h>
#include <sys/socket.h>

namespace {
struct Client {
    SSL_CTX *context = nullptr;
    SSL *ssl = nullptr;
    bool ready = false;
    bool server = false;
    bool failed = false;
    bool closing = false;
    bool shutdown_complete = false;
    ~Client() {
        SSL_free(ssl);
        SSL_CTX_free(context);
    }
};

// OpenSSL's socket BIO does not use MSG_NOSIGNAL on Linux. Protect every SSL
// operation, including reads/handshakes that can write protocol records.
struct SignalScope {
    bool valid = true;
#ifndef __APPLE__
    sigset_t previous{}, blocked{};
    bool active = false, already_pending = false;
    SignalScope() {
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &blocked, &previous)) { valid = false; return; }
        active = true;
        sigset_t pending{};
        if (sigpending(&pending)) { valid = false; return; }
        already_pending = sigismember(&pending, SIGPIPE) == 1;
    }
    ~SignalScope() {
        const int saved = errno;
        if (active) {
            sigset_t pending{};
            if (valid && !already_pending && !sigpending(&pending) && sigismember(&pending, SIGPIPE) == 1) {
                const timespec zero{};
                while (sigtimedwait(&blocked, nullptr, &zero) < 0 && errno == EINTR) {}
            }
            pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        }
        errno = saved;
    }
#endif
};

int32_t ssl_status(Client *client, int result, int32_t *wait_for, int32_t *detail) {
    // SSL_get_error must immediately follow the failed SSL operation.
    const int code = SSL_get_error(client->ssl, result);
    if (detail)
        *detail = code;
    if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) {
        if (wait_for)
            *wait_for = code == SSL_ERROR_WANT_READ ? 1 : 2;
        return GLOIN_STD_WOULD_BLOCK;
    }
    if (code == SSL_ERROR_ZERO_RETURN)
        return GLOIN_STD_EOF;
    client->failed = true;
    return GLOIN_STD_IO_ERROR;
}

bool valid_string(const char *bytes, uint64_t length, uint64_t limit) {
    return bytes && length > 0 && length <= limit && !memchr(bytes, 0, length);
}

bool attach(Client *client, int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || !(flags & O_NONBLOCK)) return false;
#ifdef __APPLE__
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled)) return false;
#endif
    BIO *bio = BIO_new_socket(fd, BIO_NOCLOSE);
    if (!bio) return false;
    SSL_set_bio(client->ssl, bio, bio);
    SSL_set_mode(client->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    // Transport EOF without close_notify must remain an error, never clean EOF.
    SSL_clear_options(client->ssl, SSL_OP_IGNORE_UNEXPECTED_EOF);
    return true;
}

// Zero is a valid empty password; -1 rejects encrypted input unconditionally.
int reject_password(char *, int, int, void *) { return -1; }
} // namespace

extern "C" {
int32_t gloin_net_tls_create(int32_t fd, const char *hostname, uint64_t hostname_length,
                             const char *trust_file, uint64_t trust_file_length, void **handle,
                             int32_t *detail) {
    if (handle)
        *handle = nullptr;
    if (detail)
        *detail = 0;
    if (!handle || !detail || fd < 0 || !valid_string(hostname, hostname_length, 253) ||
        (trust_file_length && !valid_string(trust_file, trust_file_length, 4096)))
        return GLOIN_STD_INVALID;
    auto client = std::unique_ptr<Client>(new (std::nothrow) Client);
    if (!client)
        return GLOIN_STD_NO_MEMORY;
    client->context = SSL_CTX_new(TLS_client_method());
    if (!client->context)
        return GLOIN_STD_IO_ERROR;
    if (SSL_CTX_set_min_proto_version(client->context, TLS1_2_VERSION) != 1)
        return GLOIN_STD_IO_ERROR;
    SSL_CTX_set_verify(client->context, SSL_VERIFY_PEER, nullptr);
    std::array<char, 254> hostname_copy{};
    memcpy(hostname_copy.data(), hostname, static_cast<size_t>(hostname_length));
    if (trust_file_length) {
        std::array<char, 4097> path{};
        memcpy(path.data(), trust_file, static_cast<size_t>(trust_file_length));
        if (SSL_CTX_load_verify_file(client->context, path.data()) != 1)
            return GLOIN_STD_IO_ERROR;
    } else if (SSL_CTX_set_default_verify_paths(client->context) != 1)
        return GLOIN_STD_IO_ERROR;
    client->ssl = SSL_new(client->context);
    if (!client->ssl)
        return GLOIN_STD_IO_ERROR;
    SSL_set_mode(client->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    in_addr ipv4{};
    in6_addr ipv6{};
    const bool ip = inet_pton(AF_INET, hostname_copy.data(), &ipv4) == 1 ||
                    inet_pton(AF_INET6, hostname_copy.data(), &ipv6) == 1;
    if (ip) {
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(client->ssl), hostname_copy.data()) != 1)
            return GLOIN_STD_IO_ERROR;
    } else if (SSL_set1_host(client->ssl, hostname_copy.data()) != 1 ||
               SSL_set_tlsext_host_name(client->ssl, hostname_copy.data()) != 1)
        return GLOIN_STD_IO_ERROR;
    if (!attach(client.get(), fd)) return GLOIN_STD_IO_ERROR;
    SSL_set_connect_state(client->ssl);
    *handle = client.release();
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_server_config(const char *certificate, uint64_t certificate_length,
                                  const char *key, uint64_t key_length, void **handle, int32_t *detail) {
    if (handle) *handle = nullptr;
    if (detail) *detail = 0;
    if (!handle || !detail || !valid_string(certificate, certificate_length, 4096) ||
        !valid_string(key, key_length, 4096)) return GLOIN_STD_INVALID;
    std::array<char, 4097> cert_path{}, key_path{};
    memcpy(cert_path.data(), certificate, size_t(certificate_length));
    memcpy(key_path.data(), key, size_t(key_length));
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if (!context) return GLOIN_STD_IO_ERROR;
    // Never prompt on stdin or invoke an application's password callback.
    SSL_CTX_set_default_passwd_cb(context.get(), reject_password);
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_session_cache_mode(context.get(), SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(context.get(), SSL_OP_NO_TICKET);
    SSL_CTX_set_num_tickets(context.get(), 0);
    if (SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(context.get(), cert_path.data()) != 1 ||
        SSL_CTX_use_PrivateKey_file(context.get(), key_path.data(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context.get()) != 1) return GLOIN_STD_IO_ERROR;
    *handle = context.release();
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_server_config_close(void *handle) {
    if (!handle) return GLOIN_STD_INVALID;
    SSL_CTX_free(static_cast<SSL_CTX *>(handle));
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_server_create(int32_t fd, void *config, void **handle, int32_t *detail) {
    if (handle) *handle = nullptr;
    if (detail) *detail = 0;
    if (!handle || !detail || !config || fd < 0) return GLOIN_STD_INVALID;
    auto session = std::unique_ptr<Client>(new (std::nothrow) Client);
    if (!session) return GLOIN_STD_NO_MEMORY;
    session->ssl = SSL_new(static_cast<SSL_CTX *>(config));
    if (!session->ssl || !attach(session.get(), fd)) return GLOIN_STD_IO_ERROR;
    session->server = true;
    SSL_set_accept_state(session->ssl);
    *handle = session.release();
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_handshake(void *handle, int32_t *wait_for, int32_t *detail) {
    if (wait_for)
        *wait_for = 0;
    if (detail)
        *detail = 0;
    if (!handle || !wait_for || !detail)
        return GLOIN_STD_INVALID;
    auto *client = static_cast<Client *>(handle);
    if (client->failed || client->closing) return GLOIN_STD_INVALID;
    if (client->ready)
        return GLOIN_STD_OK;
    SignalScope signals;
    if (!signals.valid) return GLOIN_STD_IO_ERROR;
    ERR_clear_error();
    int result = SSL_do_handshake(client->ssl);
    if (result != 1)
        return ssl_status(client, result, wait_for, detail);
    if (!client->server && SSL_get_verify_result(client->ssl) != X509_V_OK) {
        client->failed = true;
        return GLOIN_STD_IO_ERROR;
    }
    client->ready = true;
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_read(void *handle, uint8_t *bytes, uint64_t capacity, uint64_t *count,
                           int32_t *wait_for, int32_t *detail) {
    if (count)
        *count = 0;
    if (wait_for)
        *wait_for = 0;
    if (detail)
        *detail = 0;
    if (!handle || !bytes || !capacity || !count || !wait_for || !detail)
        return GLOIN_STD_INVALID;
    auto *client = static_cast<Client *>(handle);
    if (!client->ready || client->failed)
        return GLOIN_STD_INVALID;
    size_t received = 0;
    SignalScope signals;
    if (!signals.valid) return GLOIN_STD_IO_ERROR;
    ERR_clear_error();
    int result = SSL_read_ex(client->ssl, bytes,
                             static_cast<size_t>(std::min<uint64_t>(capacity, INT_MAX)), &received);
    if (result != 1)
        return ssl_status(client, result, wait_for, detail);
    *count = received;
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_write(void *handle, const uint8_t *bytes, uint64_t length, uint64_t *count,
                            int32_t *wait_for, int32_t *detail) {
    if (count)
        *count = 0;
    if (wait_for)
        *wait_for = 0;
    if (detail)
        *detail = 0;
    if (!handle || !bytes || !length || !count || !wait_for || !detail)
        return GLOIN_STD_INVALID;
    auto *client = static_cast<Client *>(handle);
    if (!client->ready || client->failed || client->closing)
        return GLOIN_STD_INVALID;
    size_t sent = 0;
    SignalScope signals;
    if (!signals.valid) return GLOIN_STD_IO_ERROR;
    ERR_clear_error();
    int result = SSL_write_ex(client->ssl, bytes,
                              static_cast<size_t>(std::min<uint64_t>(length, INT_MAX)), &sent);
    if (result != 1)
        return ssl_status(client, result, wait_for, detail);
    *count = sent;
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_shutdown(void *handle, int32_t *wait_for, int32_t *detail) {
    if (wait_for) *wait_for = 0;
    if (detail) *detail = 0;
    if (!handle || !wait_for || !detail) return GLOIN_STD_INVALID;
    auto *client = static_cast<Client *>(handle);
    if (!client->ready || client->failed) return GLOIN_STD_INVALID;
    if (client->shutdown_complete) return GLOIN_STD_OK;
    client->closing = true;
    SignalScope signals;
    if (!signals.valid) return GLOIN_STD_IO_ERROR;
    // A zero return is progress, not an SSL error. Retry once immediately so an
    // alert already buffered by OpenSSL does not require new socket readiness.
    for (int attempt = 0; attempt < 2; ++attempt) {
        ERR_clear_error();
        const int result = SSL_shutdown(client->ssl);
        if (result == 1) { client->shutdown_complete = true; return GLOIN_STD_OK; }
        if (result < 0) return ssl_status(client, result, wait_for, detail);
    }
    *wait_for = 1;
    return GLOIN_STD_WOULD_BLOCK;
}

int32_t gloin_net_tls_close(void *handle) {
    if (!handle)
        return GLOIN_STD_INVALID;
    // Best-effort close_notify: the socket is nonblocking and the caller must
    // subsequently close it. This does not perform bidirectional shutdown.
    auto *client = static_cast<Client *>(handle);
    if (client->ready && !client->failed && !client->shutdown_complete) {
        SignalScope signals;
        if (signals.valid) { ERR_clear_error(); SSL_shutdown(client->ssl); }
    }
    delete client;
    return GLOIN_STD_OK;
}
}
