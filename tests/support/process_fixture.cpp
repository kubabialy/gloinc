#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

// Controlled child process for testing the harness independently of MLIR.
int main(int argc, char **argv) {
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
