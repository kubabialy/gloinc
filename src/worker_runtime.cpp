#include "worker_runtime.h"
#include "stdlib_runtime.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <pthread.h>

namespace {
using namespace gloin::workers;
static_assert(sizeof(Launch) == 40 && offsetof(Launch, handle) == 8 &&
              offsetof(Launch, message) == 16 && offsetof(Launch, length) == 24 &&
              offsetof(Launch, code) == 32 && offsetof(Launch, os_error) == 36);

thread_local Budget native_budget;
thread_local Budget *current = &native_budget;

struct Worker {
    Budget *owner;
    Entry entry;
    uint64_t completion_size;
    pthread_t thread{};
    using Bytes = std::unique_ptr<void, decltype(&std::free)>;
    Bytes arguments{nullptr, &std::free};
    Bytes completion{nullptr, &std::free};
};

void *run(void *context) {
    auto &worker = *static_cast<Worker *>(context);
    worker.entry(worker.arguments.get(), worker.completion.get());
    return nullptr;
}

void fail(Launch *out, const char *message, int32_t code, int32_t os_error = 0) {
    *out = {1, nullptr, message, std::strlen(message), code, os_error};
}
} // namespace

gloin::workers::Invocation::Invocation(uint32_t capacity)
    : budget{std::min(capacity, default_capacity), 0}, previous(current) {
    current = &budget;
}

gloin::workers::Invocation::~Invocation() {
    if (budget.live)
        std::abort();
    current = previous;
}

uint32_t gloin_worker_capacity() { return current->capacity; }

void gloin_worker_launch(Launch *out, Entry entry, const void *arguments, uint64_t argument_size,
                         uint64_t completion_size) {
    if (current->live == current->capacity) {
        fail(out, "worker capacity exhausted", GLOIN_STD_WOULD_BLOCK);
        return;
    }
    std::unique_ptr<Worker> worker(new (std::nothrow) Worker{current, entry, completion_size});
    if (worker) {
        if (argument_size)
            worker->arguments.reset(std::malloc(argument_size));
        worker->completion.reset(std::malloc(completion_size));
    }
    if (!worker || (argument_size && !worker->arguments) || !worker->completion) {
        fail(out, "cannot allocate worker storage", GLOIN_STD_NO_MEMORY);
        return;
    }
    if (argument_size)
        std::memcpy(worker->arguments.get(), arguments, argument_size);
    int status = pthread_create(&worker->thread, nullptr, run, worker.get());
    if (status != 0) {
        fail(out, "cannot start worker thread", GLOIN_STD_IO_ERROR, status);
        return;
    }
    // Only this caller changes its budget. A finished worker still owns its slot.
    ++current->live;
    *out = {0, worker.release(), nullptr, 0, 0, 0};
}

void gloin_worker_join(void *handle, void *completion, uint64_t completion_size) {
    std::unique_ptr<Worker> worker(static_cast<Worker *>(handle));
    if (!worker || worker->owner != current || worker->completion_size != completion_size ||
        pthread_join(worker->thread, nullptr) != 0)
        std::abort(); // Releasing a frame without confirmed termination is an invariant failure.
    std::memcpy(completion, worker->completion.get(), completion_size);
    --current->live;
}
