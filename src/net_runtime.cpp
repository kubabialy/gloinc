#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int32_t failure(int32_t *os_error, int code = 0) {
    if (os_error) *os_error = code ? code : errno;
    return GLOIN_STD_IO_ERROR;
}
int32_t blocked(int32_t *os_error) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        if (os_error) *os_error = errno;
        return GLOIN_STD_WOULD_BLOCK;
    }
    return failure(os_error);
}
sockaddr_in endpoint(uint32_t address, uint16_t port) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_addr.s_addr = htonl(address);
    out.sin_port = htons(port);
    return out;
}
bool configure(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)) < 0) return false;
#endif
    return true;
}
bool invalid(int fd, int32_t *os_error) {
    if (os_error) *os_error = 0;
    return fd < 0;
}
} // namespace

extern "C" {
int32_t gloin_net_open(int32_t *fd, int32_t *os_error) {
    if (fd) *fd = -1;
    if (os_error) *os_error = 0;
    if (!fd || !os_error) return GLOIN_STD_INVALID;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return failure(os_error);
    if (!configure(socket_fd)) {
        int saved = errno;
        close(socket_fd);
        return failure(os_error, saved);
    }
    *fd = socket_fd;
    return GLOIN_STD_OK;
}
int32_t gloin_net_bind(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error) {
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    auto target = endpoint(address, port);
    return bind(fd, reinterpret_cast<sockaddr *>(&target), sizeof(target)) == 0
               ? GLOIN_STD_OK : failure(os_error);
}
int32_t gloin_net_listen(int32_t fd, int32_t backlog, int32_t *os_error) {
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (backlog < 1) return GLOIN_STD_INVALID;
    return listen(fd, backlog) == 0 ? GLOIN_STD_OK : failure(os_error);
}
int32_t gloin_net_accept(int32_t fd, int32_t *accepted, uint32_t *address, uint16_t *port,
                         int32_t *os_error) {
    if (accepted) *accepted = -1;
    if (address) *address = 0;
    if (port) *port = 0;
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (!accepted || !address || !port || !os_error) return GLOIN_STD_INVALID;
    sockaddr_in peer{};
    socklen_t size = sizeof(peer);
    int client = accept(fd, reinterpret_cast<sockaddr *>(&peer), &size);
    if (client < 0) return blocked(os_error);
    if (!configure(client)) {
        int saved = errno;
        close(client);
        return failure(os_error, saved);
    }
    *accepted = client;
    *address = ntohl(peer.sin_addr.s_addr);
    *port = ntohs(peer.sin_port);
    return GLOIN_STD_OK;
}
int32_t gloin_net_connect(int32_t fd, uint32_t address, uint16_t port, int32_t *os_error) {
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    auto target = endpoint(address, port);
    if (connect(fd, reinterpret_cast<sockaddr *>(&target), sizeof(target)) == 0)
        return GLOIN_STD_OK;
    if (errno == EINPROGRESS || errno == EALREADY || errno == EINTR) {
        if (os_error) *os_error = errno;
        return GLOIN_STD_IN_PROGRESS;
    }
    if (errno == EISCONN) return GLOIN_STD_OK;
    return failure(os_error);
}
int32_t gloin_net_finish_connect(int32_t fd, int32_t *os_error) {
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    pollfd watch{fd, POLLOUT, 0};
    int result = poll(&watch, 1, 0);
    if (result < 0) return blocked(os_error);
    if (result == 0) return GLOIN_STD_IN_PROGRESS;
    int error = 0;
    socklen_t size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) return failure(os_error);
    if (error) return failure(os_error, error);
    return GLOIN_STD_OK;
}
int32_t gloin_net_wait(int32_t fd, int32_t events, int32_t timeout_ms, int32_t *ready,
                       int32_t *os_error) {
    if (ready) *ready = 0;
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (!ready || !os_error || events < 1 || events > 3 || timeout_ms < 0)
        return GLOIN_STD_INVALID;
    pollfd watch{fd, static_cast<short>(((events & 1) ? POLLIN : 0) |
                                        ((events & 2) ? POLLOUT : 0)), 0};
    int result = poll(&watch, 1, timeout_ms);
    if (result < 0) return blocked(os_error);
    if (!result) return timeout_ms ? GLOIN_STD_TIMED_OUT : GLOIN_STD_WOULD_BLOCK;
    if (watch.revents & POLLNVAL) return failure(os_error, EBADF);
    if (watch.revents & POLLIN) *ready |= 1;
    if (watch.revents & POLLOUT) *ready |= 2;
    // POLLHUP/POLLERR also prompt a read/write or finish_connect; those report detail.
    if (watch.revents & (POLLHUP | POLLERR)) *ready |= events;
    return GLOIN_STD_OK;
}
int32_t gloin_net_recv(int32_t fd, uint8_t *bytes, uint64_t capacity, uint64_t *received,
                       int32_t *os_error) {
    if (received) *received = 0;
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (!bytes || !capacity || !received || !os_error ||
        capacity > static_cast<uint64_t>(std::numeric_limits<ssize_t>::max()))
        return GLOIN_STD_INVALID;
    ssize_t count = recv(fd, bytes, static_cast<size_t>(capacity), 0);
    if (count < 0) return blocked(os_error);
    if (!count) return GLOIN_STD_EOF;
    *received = static_cast<uint64_t>(count);
    return GLOIN_STD_OK;
}
int32_t gloin_net_send(int32_t fd, const uint8_t *bytes, uint64_t length, uint64_t *sent,
                       int32_t *os_error) {
    if (sent) *sent = 0;
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (!bytes || !length || !sent || !os_error ||
        length > static_cast<uint64_t>(std::numeric_limits<ssize_t>::max()))
        return GLOIN_STD_INVALID;
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    ssize_t count = send(fd, bytes, static_cast<size_t>(length), flags);
    if (count < 0) return blocked(os_error);
    *sent = static_cast<uint64_t>(count);
    return count ? GLOIN_STD_OK : GLOIN_STD_WOULD_BLOCK;
}
int32_t gloin_net_local(int32_t fd, uint32_t *address, uint16_t *port, int32_t *os_error) {
    if (address) *address = 0;
    if (port) *port = 0;
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    if (!address || !port || !os_error) return GLOIN_STD_INVALID;
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size) < 0)
        return failure(os_error);
    *address = ntohl(local.sin_addr.s_addr);
    *port = ntohs(local.sin_port);
    return GLOIN_STD_OK;
}
int32_t gloin_net_send_text(int32_t fd, const char *bytes, uint64_t length, uint64_t *sent,
                            int32_t *os_error) {
    return gloin_net_send(fd, reinterpret_cast<const uint8_t *>(bytes), length, sent, os_error);
}
int32_t gloin_net_close(int32_t fd, int32_t *os_error) {
    if (invalid(fd, os_error)) return GLOIN_STD_CLOSED;
    // POSIX close may consume the descriptor even when reporting EINTR. Never retry.
    return close(fd) == 0 ? GLOIN_STD_OK : failure(os_error);
}
}
