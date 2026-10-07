#include "context_runtime.h"
#include "io_runtime_internal.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct Descriptor {
    int value = -1;
    ~Descriptor() { if (value >= 0) close(value); }
    int consume() { const int saved = value; value = -1; return close(saved); }
};
struct StagedFile {
    int parent;
    char name[96]{};
    bool exists = false;
    ~StagedFile() { if (exists) unlinkat(parent, name, 0); }
};
bool same(const struct stat &a, const struct stat &b) {
#ifdef __APPLE__
    const auto am = a.st_mtimespec, bm = b.st_mtimespec, ac = a.st_ctimespec, bc = b.st_ctimespec;
#else
    const auto am = a.st_mtim, bm = b.st_mtim, ac = a.st_ctim, bc = b.st_ctim;
#endif
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
           a.st_mode == b.st_mode && a.st_nlink == b.st_nlink && a.st_uid == b.st_uid &&
           a.st_gid == b.st_gid && am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec &&
           ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
bool matches(int fd, const char *expected, uint64_t size) {
    if (lseek(fd, 0, SEEK_SET) < 0) return false;
    char buffer[8192];
    uint64_t offset = 0;
    while (true) {
        ssize_t count;
        do { count = read(fd, buffer, sizeof buffer); } while (count < 0 && errno == EINTR);
        if (count < 0) return false;
        if (!count) {
            if (offset == size) return true;
            errno = 0;
            return false;
        }
        if (uint64_t(count) > size - offset || std::memcmp(buffer, expected + offset, count)) {
            errno = 0;
            return false;
        }
        offset += count;
    }
}
int32_t failure(int32_t *os_error) {
    *os_error = errno ? errno : EIO;
    return gloin::io::error_status(*os_error);
}
} // namespace

extern "C" int32_t gloin_fs_replace_file(const char *path, uint64_t path_size,
    const char *expected, uint64_t expected_size, const char *replacement, uint64_t replacement_size,
    int32_t *os_error) {
    *os_error = 0;
    if (!path || !path_size || path_size > uint64_t(PTRDIFF_MAX) - 1 ||
        std::memchr(path, 0, path_size) || path[path_size - 1] == '/' ||
        (!expected && expected_size) || (!replacement && replacement_size) ||
        expected_size > uint64_t(PTRDIFF_MAX) || replacement_size > uint64_t(PTRDIFF_MAX))
        return GLOIN_STD_INVALID;
    try {
        const std::string full(path, path_size);
        const auto slash = full.rfind('/');
        const auto parent_name = slash == std::string::npos ? "." : slash == 0 ? "/" : full.substr(0, slash);
        const auto leaf = slash == std::string::npos ? full : full.substr(slash + 1);
        if (leaf == "." || leaf == "..") return GLOIN_STD_INVALID;
        Descriptor parent{open(parent_name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
        if (parent.value < 0) return failure(os_error);
        // O_NONBLOCK prevents a replaced path/FIFO from blocking before fstat.
        Descriptor original{openat(parent.value, leaf.c_str(), O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        if (original.value < 0) return failure(os_error);
        struct stat before{};
        if (fstat(original.value, &before)) return failure(os_error);
        if (!S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_uid != geteuid() ||
            (before.st_mode & (S_ISUID | S_ISGID | S_ISVTX)))
            return GLOIN_STD_INVALID;
        errno = 0;
        if (!matches(original.value, expected, expected_size))
            return errno ? failure(os_error) : GLOIN_STD_INVALID;

        StagedFile staged{parent.value};
        Descriptor temporary;
        static std::atomic<uint64_t> sequence{0};
        for (unsigned attempt = 0; attempt < 256; ++attempt) {
            std::snprintf(staged.name, sizeof staged.name, ".gloin-replace-%ld-%llu",
                          long(getpid()), static_cast<unsigned long long>(sequence.fetch_add(1)));
            temporary.value = openat(parent.value, staged.name,
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (temporary.value >= 0) { staged.exists = true; break; }
            if (errno != EEXIST) return failure(os_error);
        }
        if (temporary.value < 0) return failure(os_error);
        uint64_t offset = 0;
        while (offset < replacement_size) {
            const auto count = write(temporary.value, replacement + offset,
                                     std::min<uint64_t>(replacement_size - offset, 1024 * 1024));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { if (!count) errno = EIO; return failure(os_error); }
            offset += count;
        }
        if (fchown(temporary.value, before.st_uid, before.st_gid) ||
            fchmod(temporary.value, before.st_mode & 0777) || fsync(temporary.value))
            return failure(os_error);
        if (temporary.consume()) return failure(os_error);
        // Best-effort conflict detection. POSIX rename is not compare-and-swap:
        // callers still require exclusive editing during the final check/rename.
        struct stat current{}, named{};
        errno = 0;
        if (!matches(original.value, expected, expected_size))
            return errno ? failure(os_error) : GLOIN_STD_INVALID;
        if (fstat(original.value, &current) ||
            fstatat(parent.value, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW))
            return failure(os_error);
        if (!same(before, current) || !same(before, named)) return GLOIN_STD_INVALID;
        if (renameat(parent.value, staged.name, parent.value, leaf.c_str())) return failure(os_error);
        staged.exists = false;
        return GLOIN_STD_OK;
    } catch (const std::bad_alloc &) {
        return GLOIN_STD_NO_MEMORY;
    } catch (const std::length_error &) {
        return GLOIN_STD_INVALID;
    }
}
