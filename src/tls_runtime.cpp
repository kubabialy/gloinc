#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

namespace {
struct Client {
    SSL_CTX *context = nullptr;
    SSL *ssl = nullptr;
    bool ready = false;
    ~Client() {
        SSL_free(ssl);
        SSL_CTX_free(context);
    }
};

int32_t ssl_status(SSL *ssl, int result, int32_t *wait_for, int32_t *detail) {
    // SSL_get_error must immediately follow the failed SSL operation.
    const int code = SSL_get_error(ssl, result);
    if (detail)
        *detail = code;
    if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) {
        if (wait_for)
            *wait_for = code == SSL_ERROR_WANT_READ ? 1 : 2;
        return GLOIN_STD_WOULD_BLOCK;
    }
    if (code == SSL_ERROR_ZERO_RETURN)
        return GLOIN_STD_EOF;
    return GLOIN_STD_IO_ERROR;
}

bool valid_string(const char *bytes, uint64_t length, uint64_t limit) {
    return bytes && length > 0 && length <= limit && !memchr(bytes, 0, length);
}
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
    BIO *bio = BIO_new_socket(fd, BIO_NOCLOSE);
    if (!bio)
        return GLOIN_STD_IO_ERROR;
    SSL_set_bio(client->ssl, bio, bio);
    SSL_set_connect_state(client->ssl);
    *handle = client.release();
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
    if (client->ready)
        return GLOIN_STD_OK;
    ERR_clear_error();
    int result = SSL_connect(client->ssl);
    if (result != 1)
        return ssl_status(client->ssl, result, wait_for, detail);
    if (SSL_get_verify_result(client->ssl) != X509_V_OK)
        return GLOIN_STD_IO_ERROR;
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
    if (!client->ready)
        return GLOIN_STD_INVALID;
    size_t received = 0;
    ERR_clear_error();
    int result = SSL_read_ex(client->ssl, bytes,
                             static_cast<size_t>(std::min<uint64_t>(capacity, INT_MAX)), &received);
    if (result != 1)
        return ssl_status(client->ssl, result, wait_for, detail);
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
    if (!client->ready)
        return GLOIN_STD_INVALID;
    size_t sent = 0;
    ERR_clear_error();
    int result = SSL_write_ex(client->ssl, bytes,
                              static_cast<size_t>(std::min<uint64_t>(length, INT_MAX)), &sent);
    if (result != 1)
        return ssl_status(client->ssl, result, wait_for, detail);
    *count = sent;
    return GLOIN_STD_OK;
}

int32_t gloin_net_tls_close(void *handle) {
    if (!handle)
        return GLOIN_STD_INVALID;
    // Best-effort close_notify: the socket is nonblocking and the caller must
    // subsequently close it. This does not perform bidirectional shutdown.
    auto *client = static_cast<Client *>(handle);
    if (client->ready)
        SSL_shutdown(client->ssl);
    delete client;
    return GLOIN_STD_OK;
}
}
