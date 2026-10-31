#include "net_runtime.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <net/if.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/if_tun.h>
#elif defined(__APPLE__)
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#endif
namespace {
int32_t failure(int32_t *error, int saved = 0) {
    const int code = saved ? saved : errno;
    *error = code;
    if (code == EAGAIN || code == EWOULDBLOCK || code == EINTR)
        return GLOIN_STD_WOULD_BLOCK;
    return GLOIN_STD_IO_ERROR;
}
} // namespace
extern "C" {
int32_t gloin_net_tun_open(const char *name, uint64_t length, int32_t *fd, int32_t *framing,
                           int32_t *error) {
    if (fd)
        *fd = -1;
    if (framing)
        *framing = 0;
    if (error)
        *error = 0;
    if (!name || !length || length >= IFNAMSIZ || memchr(name, 0, length) || !fd || !framing ||
        !error)
        return GLOIN_STD_INVALID;
    int descriptor = -1;
#ifdef __linux__
    descriptor = open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0)
        return failure(error);
    ifreq request{};
    memcpy(request.ifr_name, name, size_t(length));
    request.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (ioctl(descriptor, TUNSETIFF, &request) < 0) {
        const int saved = errno;
        close(descriptor);
        return failure(error, saved);
    }
#elif defined(__APPLE__)
    // Named utunN; this creates/opens only the requested device, never routes.
    if (length < 5 || memcmp(name, "utun", 4) != 0)
        return GLOIN_STD_INVALID;
    uint32_t unit = 0;
    for (uint64_t i = 4; i < length; ++i) {
        if (name[i] < '0' || name[i] > '9' || unit > 100000)
            return GLOIN_STD_INVALID;
        unit = unit * 10 + uint32_t(name[i] - '0');
    }
    descriptor = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (descriptor < 0)
        return failure(error);
    ctl_info info{};
    strcpy(info.ctl_name, "com.apple.net.utun_control");
    sockaddr_ctl address{};
    address.sc_len = sizeof address;
    address.sc_family = AF_SYSTEM;
    address.ss_sysaddr = AF_SYS_CONTROL;
    address.sc_unit = unit + 1;
    if (ioctl(descriptor, CTLIOCGINFO, &info) < 0) {
        const int saved = errno;
        close(descriptor);
        return failure(error, saved);
    }
    address.sc_id = info.ctl_id;
    if (connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0 ||
        fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0 || fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        const int saved = errno;
        close(descriptor);
        return failure(error, saved);
    }
    *framing = 4;
#else
    return GLOIN_STD_INVALID;
#endif
    *fd = descriptor;
    return GLOIN_STD_OK;
}
int32_t gloin_net_packet_read(int32_t fd, int32_t framing, uint8_t *bytes, uint64_t capacity,
                              uint64_t *count, int32_t *error) {
    if (count)
        *count = 0;
    if (error)
        *error = 0;
    if (fd < 0 || !bytes || capacity < 65535 || !count || !error || (framing != 0 && framing != 4))
        return GLOIN_STD_INVALID;
    uint32_t family = 0;
    iovec views[] = {{&family, size_t(framing)}, {bytes, 65535}};
    const auto got = readv(fd, views, 2);
    if (got < 0)
        return failure(error);
    if (got == 0)
        return GLOIN_STD_EOF;
    if (got < framing)
        return GLOIN_STD_INVALID;
    if (framing && ntohl(family) != AF_INET)
        return GLOIN_STD_WOULD_BLOCK;
    *count = uint64_t(got - framing);
    return GLOIN_STD_OK;
}
int32_t gloin_net_packet_write(int32_t fd, int32_t framing, const uint8_t *bytes, uint64_t length,
                               uint64_t *count, int32_t *error) {
    if (count)
        *count = 0;
    if (error)
        *error = 0;
    if (fd < 0 || !bytes || length < 20 || length > 65535 || !count || !error ||
        (framing != 0 && framing != 4) || (bytes[0] >> 4) != 4 ||
        ((uint64_t(bytes[2]) << 8) | bytes[3]) != length)
        return GLOIN_STD_INVALID;
    uint32_t family = htonl(AF_INET);
    iovec views[] = {{&family, size_t(framing)}, {const_cast<uint8_t *>(bytes), size_t(length)}};
    const auto sent = writev(fd, views, 2);
    if (sent < 0)
        return failure(error);
    if (uint64_t(sent) != length + framing) {
        *error = EIO;
        return GLOIN_STD_IO_ERROR;
    }
    *count = length;
    return GLOIN_STD_OK;
}
int32_t gloin_net_isn_seed(uint8_t *secret) {
    if (!secret)
        return GLOIN_STD_INVALID;
    return RAND_priv_bytes(secret, 32) == 1 ? GLOIN_STD_OK : GLOIN_STD_IO_ERROR;
}
int32_t gloin_net_isn_next(const uint8_t *secret, uint32_t local, uint16_t local_port,
                           uint32_t remote, uint16_t remote_port, uint64_t microseconds,
                           uint32_t *isn) {
    if (isn)
        *isn = 0;
    if (!secret || !isn || !local_port || !remote_port)
        return GLOIN_STD_INVALID;
    const std::array<uint8_t, 12> tuple{
        uint8_t(local >> 24),  uint8_t(local >> 16),      uint8_t(local >> 8),
        uint8_t(local),        uint8_t(local_port >> 8),  uint8_t(local_port),
        uint8_t(remote >> 24), uint8_t(remote >> 16),     uint8_t(remote >> 8),
        uint8_t(remote),       uint8_t(remote_port >> 8), uint8_t(remote_port)};
    std::array<uint8_t, 32> digest{};
    size_t length = 0;
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, secret, 32, tuple.data(),
                   tuple.size(), digest.data(), digest.size(), &length) ||
        length != 32)
        return GLOIN_STD_IO_ERROR;
    const uint32_t hash = (uint32_t(digest[0]) << 24) | (uint32_t(digest[1]) << 16) |
                          (uint32_t(digest[2]) << 8) | digest[3];
    // RFC 9293: secret-dependent tuple hash plus a four-microsecond clock.
    *isn = hash + uint32_t(microseconds / 4);
    return GLOIN_STD_OK;
}
}
