#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// IPv4 address is a host-order integer: a.b.c.d = (a<<24)|(b<<16)|(c<<8)|d.
// Every returned descriptor is nonblocking and close-on-exec. fd=-1 is invalid.
int32_t gloin_net_open(int32_t *fd, int32_t *os_error);
int32_t gloin_net_bind(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error);
int32_t gloin_net_reuse_address(int32_t fd, int32_t enabled, int32_t *os_error);
int32_t gloin_net_listen(int32_t fd, int32_t backlog, int32_t *os_error);
int32_t gloin_net_accept(int32_t fd, int32_t *accepted, uint32_t *address, uint16_t *port,
                         int32_t *os_error);
int32_t gloin_net_connect(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error);
int32_t gloin_net_finish_connect(int32_t fd, int32_t *os_error);
// events: 1=read, 2=write, 3=both. timeout_ms >= 0. ready receives matching bits.
int32_t gloin_net_wait(int32_t fd, int32_t events, int32_t timeout_ms, int32_t *ready,
                       int32_t *os_error);
int32_t gloin_net_wait_many(const int32_t *fds, const int32_t *events, int32_t *ready,
                            uint64_t count, int32_t timeout_ms, uint64_t *ready_count,
                            int32_t *os_error);
int32_t gloin_net_shutdown_write(int32_t fd, int32_t *os_error);
int32_t gloin_net_resolve_ipv4(const char *host, uint64_t host_length, uint32_t *addresses,
                               uint64_t capacity, uint64_t *count, int32_t *os_error);
int32_t gloin_net_recv(int32_t fd, uint8_t *bytes, uint64_t capacity, uint64_t *received,
                       int32_t *os_error);
int32_t gloin_net_send(int32_t fd, const uint8_t *bytes, uint64_t length, uint64_t *sent,
                       int32_t *os_error);
int32_t gloin_net_send_text(int32_t fd, const char *bytes, uint64_t length, uint64_t *sent,
                            int32_t *os_error);
int32_t gloin_net_local(int32_t fd, uint32_t *address, uint16_t *port, int32_t *os_error);
int32_t gloin_net_close(int32_t fd, int32_t *os_error);
// TLS owns its SSL state, never the underlying socket. `trust_file` may be
// empty to use OpenSSL's default trust paths. Calls never wait for socket readiness.
int32_t gloin_net_tls_create(int32_t fd, const char *hostname, uint64_t hostname_length,
                             const char *trust_file, uint64_t trust_file_length,
                             void **handle, int32_t *detail);
int32_t gloin_net_tls_handshake(void *handle, int32_t *wait_for, int32_t *detail);
int32_t gloin_net_tls_read(void *handle, uint8_t *bytes, uint64_t capacity,
                            uint64_t *count, int32_t *wait_for, int32_t *detail);
int32_t gloin_net_tls_write(void *handle, const uint8_t *bytes, uint64_t length,
                             uint64_t *count, int32_t *wait_for, int32_t *detail);
int32_t gloin_net_tls_close(void *handle);
// Reusable server configuration. PEM certificate chain and unencrypted PEM key.
// Sessions retain their own SSL_CTX reference; closing config does not close them.
int32_t gloin_net_tls_server_config(const char *certificate, uint64_t certificate_length,
                                  const char *key, uint64_t key_length, void **handle, int32_t *detail);
int32_t gloin_net_tls_server_config_close(void *handle);
int32_t gloin_net_tls_server_create(int32_t fd, void *config, void **handle, int32_t *detail);
// OK means both close_notify alerts exchanged; WOULD_BLOCK sets READABLE/WRITABLE.
int32_t gloin_net_tls_shutdown(void *handle, int32_t *wait_for, int32_t *detail);
#ifdef __cplusplus
}
#endif
