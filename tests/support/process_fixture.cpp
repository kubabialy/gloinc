#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include "context_runtime.h"
#include "stdlib_runtime.h"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

// Controlled child process for testing the harness independently of MLIR.
int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "limits") {
        if (argc != 6) return 70;
        struct rlimit stack {};
        if (getrlimit(RLIMIT_STACK, &stack) ||
            stack.rlim_cur != std::strtoull(argv[2], nullptr, 10) ||
            stack.rlim_max != std::strtoull(argv[3], nullptr, 10)) return 71;
        const int fd = std::atoi(argv[4]);
        if (fd >= 0 && (fcntl(fd, F_GETFD) >= 0 || errno != EBADF)) return 72;
        struct sigaction action {};
        sigset_t mask;
        if (sigaction(SIGUSR1, nullptr, &action) || action.sa_handler != SIG_DFL ||
            sigprocmask(SIG_SETMASK, nullptr, &mask) || sigismember(&mask, SIGUSR2)) return 73;
        if (std::string(argv[5]) == "1" && getpgrp() != getpid()) return 74;
        std::cout << "limits verified\n";
        return 37;
    }
    if (argc >= 2 && std::string(argv[1]) == "limited-hard") {
        const struct rlimit hard{1048576, 1048576};
        if (setrlimit(RLIMIT_STACK, &hard)) return 75;
        int64_t pid = 99;
        int32_t input = 99, output = 99, errors = 99, error = 99;
        const auto code = gloin_process_start_options("/bin/true", 9, nullptr, 0,
            nullptr, 0, nullptr, 0, 1, 1, 1, 2097152, &pid, &input, &output, &errors, &error);
        return code != GLOIN_STD_OK && error == EINVAL && pid == 0 &&
               input == -1 && output == -1 && errors == -1 ? 0 : 76;
    }
    if (argc >= 2 && std::string(argv[1]) == "limited-low-fd") {
        struct rlimit stack {}, descriptors {};
        if (getrlimit(RLIMIT_STACK, &stack) || getrlimit(RLIMIT_NOFILE, &descriptors)) return 77;
        descriptors.rlim_cur = 1024;
        if (setrlimit(RLIMIT_NOFILE, &descriptors)) return 78;
        const int source = open("/dev/null", O_RDONLY);
        const int high = fcntl(source, F_DUPFD, 512);
        if (source < 0 || high < 0) return 79;
        descriptors.rlim_cur = 64;
        if (setrlimit(RLIMIT_NOFILE, &descriptors)) return 80;
        const std::string hard = std::to_string(stack.rlim_max), number = std::to_string(high);
        const GloinProcessArgument arguments[] = {{"limits", 6}, {"2097152", 7},
            {hard.data(), hard.size()}, {number.data(), number.size()}, {"1", 1}};
        // Also exercise status-pipe allocation when inherited standard fds are closed.
        close(0); close(1); close(2);
        int64_t pid = 0;
        int32_t input = -1, output = -1, errors = -1, error = 0;
        if (gloin_process_start_options(argv[0], std::strlen(argv[0]), nullptr, 0,
                arguments, 5, nullptr, 0, 1, 1, 1, 2097152,
                &pid, &input, &output, &errors, &error) != GLOIN_STD_OK) return 81;
        int32_t kind = 0, code = 0;
        if (gloin_process_wait(pid, 1, &kind, &code, &error) != GLOIN_STD_OK) return 82;
        uint8_t bytes[32]{};
        uint64_t count = 0;
        if (gloin_process_pipe_read(output, bytes, sizeof bytes, &count, &error) != GLOIN_STD_OK) return 83;
        return kind == 1 && code == 37 && std::string(reinterpret_cast<char *>(bytes), count) == "limits verified\n" ? 0 : 84;
    }
    if (argc < 3)
        return 2;
    const std::string mode = argv[1];
    if (mode == "fail") {
        std::cout << "42\n";
        std::cerr << "intentional tool failure\n";
        return 7;
    }
    if (mode == "hang") {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 0;
    }
    std::ifstream input(argv[2]);
    if (!input)
        return 3;
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    if (mode == "copy") {
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "-o") {
                std::ofstream output(argv[i + 1]);
                output << contents;
                return output ? 0 : 4;
            }
        }
        return 5;
    }
    if (mode == "echo") {
        std::cout << contents;
        return 0;
    }
    return 6;
}
