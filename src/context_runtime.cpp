#include "context_runtime.h"
#include "context_runtime_internal.h"
#include "io_runtime_internal.h"
#include "stdlib_runtime.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
struct Arguments {
    std::vector<std::string> values;
    Arguments *previous = nullptr;
};
thread_local Arguments *current = nullptr;
void *activate(std::unique_ptr<Arguments> next) {
    next->previous = current;
    current = next.release();
    return current;
}
bool valid_path(const char *bytes, uint64_t length) {
    return bytes && length && length <= uint64_t(PTRDIFF_MAX) - 1 && !std::memchr(bytes, 0, length);
}
using NativeText = std::unique_ptr<char, decltype(&std::free)>;
NativeText terminated(const char *bytes, uint64_t length) {
    NativeText result(static_cast<char *>(std::malloc(length + 1)), &std::free);
    if (result) {
        std::memcpy(result.get(), bytes, length);
        result.get()[length] = 0;
    }
    return result;
}
int32_t failure(int error, int32_t *os_error) {
    *os_error = error ? error : EIO;
    return gloin::io::error_status(*os_error);
}
} // namespace
extern "C" void *gloin_process_arguments_push(uint64_t count, const char *const *arguments) {
    if ((count && !arguments) || count > uint64_t(PTRDIFF_MAX) / sizeof(std::string))
        return nullptr;
    try {
        auto next = std::make_unique<Arguments>();
        next->values.reserve(count);
        for (uint64_t i = 0; i < count; ++i) {
            if (!arguments[i])
                return nullptr;
            next->values.emplace_back(arguments[i]);
        }
        return activate(std::move(next));
    } catch (const std::bad_alloc &) {
        return nullptr;
    } catch (const std::length_error &) {
        return nullptr;
    }
}
extern "C" int32_t gloin_process_arguments_pop(void *scope) {
    if (!scope || scope != current)
        return GLOIN_STD_INVALID;
    auto *old = current;
    current = old->previous;
    delete old;
    return GLOIN_STD_OK;
}
gloin::process::ArgumentsScope::ArgumentsScope(std::span<const std::string> arguments) noexcept {
    try {
        auto next = std::make_unique<Arguments>();
        for (const auto &argument : arguments) {
            if (argument.find('\0') != std::string::npos)
                return;
            next->values.push_back(argument);
        }
        scope = activate(std::move(next));
    } catch (const std::bad_alloc &) {
    } catch (const std::length_error &) {
    }
}
gloin::process::ArgumentsScope::~ArgumentsScope() {
    if (scope)
        gloin_process_arguments_pop(scope);
}
extern "C" uint64_t gloin_process_arg_count(void) { return current ? current->values.size() : 0; }
extern "C" const char *gloin_process_arg(uint64_t index, uint64_t *length, int32_t *status) {
    *length = 0;
    *status = GLOIN_STD_OUT_OF_RANGE;
    if (!current || index >= current->values.size())
        return nullptr;
    const auto &value = current->values[index];
    *length = value.size();
    *status = GLOIN_STD_OK;
    return value.data();
}
extern "C" const char *gloin_process_env(const char *name, uint64_t size, uint64_t *length,
                                         int32_t *status) {
    *length = 0;
    *status = GLOIN_STD_INVALID;
    if (!valid_path(name, size) || std::memchr(name, '=', size))
        return nullptr;
    auto key = terminated(name, size);
    if (!key) {
        *status = GLOIN_STD_NO_MEMORY;
        return nullptr;
    }
    const char *value = std::getenv(key.get());
    if (!value) {
        *status = GLOIN_STD_NOT_FOUND;
        return nullptr;
    }
    *length = std::strlen(value);
    *status = GLOIN_STD_OK;
    return value;
}
extern "C" int32_t gloin_process_cwd(char *bytes, uint64_t capacity, uint64_t *length,
                                     int32_t *os_error) {
    *length = 0;
    *os_error = 0;
    if (!capacity || capacity > uint64_t(PTRDIFF_MAX))
        return GLOIN_STD_INVALID;
    bytes[0] = 0;
    errno = 0;
    if (!getcwd(bytes, capacity)) {
        const int error = errno;
        bytes[0] = 0;
        if (error == ERANGE) {
            *os_error = error;
            return GLOIN_STD_TOO_LONG;
        }
        return failure(error, os_error);
    }
    *length = std::strlen(bytes);
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_fs_metadata(const char *path, uint64_t size, int32_t *kind,
                                     uint64_t *length, int32_t *os_error) {
    *kind = 0;
    *length = 0;
    *os_error = 0;
    if (!valid_path(path, size))
        return GLOIN_STD_INVALID;
    auto name = terminated(path, size);
    if (!name)
        return GLOIN_STD_NO_MEMORY;
    struct stat info{};
    errno = 0;
    if (lstat(name.get(), &info))
        return failure(errno, os_error);
    const int32_t category = S_ISREG(info.st_mode)   ? 1
                             : S_ISDIR(info.st_mode) ? 2
                             : S_ISLNK(info.st_mode) ? 3
                                                     : 4;
    if ((category == 1 || category == 3) && info.st_size < 0)
        return GLOIN_STD_OVERFLOW;
    *kind = category;
    *length = (category == 1 || category == 3) ? uint64_t(info.st_size) : 0;
    return GLOIN_STD_OK;
}
extern "C" int32_t gloin_fs_mkdir(const char *path, uint64_t size, int32_t *os_error) {
    *os_error = 0;
    if (!valid_path(path, size))
        return GLOIN_STD_INVALID;
    auto name = terminated(path, size);
    if (!name)
        return GLOIN_STD_NO_MEMORY;
    errno = 0;
    return mkdir(name.get(), 0777) ? failure(errno, os_error) : GLOIN_STD_OK;
}
extern "C" int32_t gloin_fs_remove_file(const char *path, uint64_t size, int32_t *os_error) {
    *os_error = 0;
    if (!valid_path(path, size))
        return GLOIN_STD_INVALID;
    auto name = terminated(path, size);
    if (!name)
        return GLOIN_STD_NO_MEMORY;
    errno = 0;
    return unlink(name.get()) ? failure(errno, os_error) : GLOIN_STD_OK;
}
extern "C" int32_t gloin_fs_rename_replace(const char *from, uint64_t from_size, const char *to,
                                           uint64_t to_size, int32_t *os_error) {
    *os_error = 0;
    if (!valid_path(from, from_size) || !valid_path(to, to_size))
        return GLOIN_STD_INVALID;
    auto source = terminated(from, from_size), destination = terminated(to, to_size);
    if (!source || !destination)
        return GLOIN_STD_NO_MEMORY;
    errno = 0;
    return std::rename(source.get(), destination.get()) ? failure(errno, os_error) : GLOIN_STD_OK;
}
