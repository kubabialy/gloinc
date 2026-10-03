#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// IPv4 address is a host-order integer: a.b.c.d = (a<<24)|(b<<16)|(c<<8)|d.
// Every returned descriptor is nonblocking and close-on-exec. fd=-1 is invalid.
int32_t gloin_net_open(int32_t *fd, int32_t *os_error);
int32_t gloin_net_bind(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error);
int32_t gloin_net_listen(int32_t fd, int32_t backlog, int32_t *os_error);
int32_t gloin_net_accept(int32_t fd, int32_t *accepted, uint32_t *address, uint16_t *port,
                         int32_t *os_error);
int32_t gloin_net_connect(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error);
int32_t gloin_net_finish_connect(int32_t fd, int32_t *os_error);
// events: 1=read, 2=write, 3=both. timeout_ms >= 0. ready receives matching bits.
int32_t gloin_net_wait(int32_t fd, int32_t events, int32_t timeout_ms, int32_t *ready,
                       int32_t *os_error);
int32_t gloin_net_recv(int32_t fd, uint8_t *bytes, uint64_t capacity, uint64_t *received,
                       int32_t *os_error);
int32_t gloin_net_send(int32_t fd, const uint8_t *bytes, uint64_t length, uint64_t *sent,
                       int32_t *os_error);
int32_t gloin_net_send_text(int32_t fd, const char *bytes, uint64_t length, uint64_t *sent,
                            int32_t *os_error);
int32_t gloin_net_local(int32_t fd, uint32_t *address, uint16_t *port, int32_t *os_error);
int32_t gloin_net_close(int32_t fd, int32_t *os_error);
#ifdef __cplusplus
}
#endif
