#include "stdlib_runtime.h"
#include "stdlib_runtime_internal.h"
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" int32_t gloin_std_parse_i32(const char *bytes, uint64_t length, int32_t *value) {
    *value = 0;
    if (!length)
        return GLOIN_STD_INVALID;
    uint64_t at = 0;
    const bool negative = bytes[0] == '-';
    if (negative || bytes[0] == '+')
        ++at;
    if (at == length)
        return GLOIN_STD_INVALID;
    const uint32_t limit = negative ? 2147483648u : 2147483647u;
    uint32_t magnitude = 0;
    bool overflow = false;
    for (; at < length; ++at) {
        const unsigned char byte = bytes[at];
        if (byte < '0' || byte > '9')
            return GLOIN_STD_INVALID;
        const uint32_t digit = byte - '0';
        if (magnitude > (limit - digit) / 10)
            overflow = true;
        else if (!overflow)
            magnitude = magnitude * 10 + digit;
    }
    if (overflow)
        return GLOIN_STD_OVERFLOW;
    *value = negative ? static_cast<int32_t>(-static_cast<int64_t>(magnitude))
                      : static_cast<int32_t>(magnitude);
    return GLOIN_STD_OK;
}

extern "C" void gloin_std_format_i32(int32_t value, char *bytes, uint64_t *length) {
    auto converted = std::to_chars(bytes, bytes + 11, value);
    *length = static_cast<uint64_t>(converted.ptr - bytes);
    bytes[*length] = '\0';
}

int32_t gloin::standard::input(FILE *stream, char *bytes, uint64_t limit, uint64_t *length,
                               int32_t *os_error) noexcept {
    if (os_error)
        *os_error = 0;
    *length = 0;
    bytes[0] = '\0';
    bool any = false, pending_cr = false, too_long = false;
    auto append = [&](char byte) {
        if (*length < limit)
            bytes[(*length)++] = byte;
        else
            too_long = true;
    };
    for (;;) {
        errno = 0;
        const int byte = std::fgetc(stream);
        if (byte == EOF) {
            if (std::ferror(stream)) {
                if (os_error)
                    *os_error = errno ? errno : EIO;
                *length = 0;
                bytes[0] = '\0';
                return GLOIN_STD_IO_ERROR;
            }
            if (!any)
                return GLOIN_STD_EOF;
            if (pending_cr)
                append('\r');
            break;
        }
        any = true;
        if (byte == '\n')
            break;
        if (pending_cr)
            append('\r');
        pending_cr = byte == '\r';
        if (!pending_cr)
            append(static_cast<char>(byte));
    }
    if (too_long)
        *length = 0;
    bytes[*length] = '\0';
    return too_long ? GLOIN_STD_TOO_LONG : GLOIN_STD_OK;
}

extern "C" int32_t gloin_std_input(char *bytes, uint64_t limit, uint64_t *length) {
    return gloin::standard::input(stdin, bytes, limit, length);
}

extern "C" void gloin_strings_copy(const char *source, uint64_t length, char *destination) {
    if (length)
        std::memcpy(destination, source, length);
}

extern "C" void gloin_std_output(const char *bytes, uint64_t length) {
    if (length && std::fwrite(bytes, 1, length, stdout) != length)
        std::abort();
    if (std::fflush(stdout))
        std::abort();
}
