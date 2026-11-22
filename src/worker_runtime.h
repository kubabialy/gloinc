#pragma once
#include <cstdint>

namespace gloin::workers {
inline constexpr uint32_t default_capacity = 4;
struct Budget {
    uint32_t capacity = default_capacity;
    uint32_t live = 0;
};

// Each embedding invocation owns its budget until every returning path has joined.
class Invocation {
    Budget budget;
    Budget *previous;

  public:
    explicit Invocation(uint32_t capacity = default_capacity);
    ~Invocation();
    Invocation(const Invocation &) = delete;
    Invocation &operator=(const Invocation &) = delete;
};

// Native storage for result<Thread<T>> on the supported 64-bit ABIs.
struct Launch {
    uint8_t failed;
    void *handle;
    const char *message;
    uint64_t length;
    int32_t code;
    int32_t os_error;
};
using Entry = void (*)(const void *arguments, void *completion);
} // namespace gloin::workers

extern "C" {
uint32_t gloin_worker_capacity();
void gloin_worker_launch(gloin::workers::Launch *out, gloin::workers::Entry entry,
                         const void *arguments, uint64_t argument_size, uint64_t completion_size);
void gloin_worker_join(void *handle, void *completion, uint64_t completion_size);
}
