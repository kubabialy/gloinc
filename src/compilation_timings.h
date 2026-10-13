#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mlir { class ModuleOp; }
namespace llvm { class Module; class raw_ostream; }

// Per-compilation instrumentation. No global state or clock reads when absent.
class CompilationTimings {
  public:
    using Clock = std::chrono::steady_clock;
    class Stage {
      public:
        Stage(CompilationTimings *owner, const char *name);
        ~Stage() { stop(); }
        Stage(const Stage &) = delete;
        Stage &operator=(const Stage &) = delete;
        void stop();
      private:
        CompilationTimings *owner;
        const char *name;
        Clock::time_point begin;
    };

    CompilationTimings() : begin(Clock::now()) {}
    void snapshot(const char *name, mlir::ModuleOp module);
    void snapshot(const char *name, const llvm::Module &module);
    // Freeze before program execution. Repeated calls preserve the first result.
    void finish(bool succeeded);
    void print(llvm::raw_ostream &out, const std::string &mode,
               const std::string &optimization, const std::string &compiler_version,
               const std::string &llvm_version) const;

  private:
    struct Measurement { std::string name; int64_t nanoseconds; int64_t calls; };
    struct Counts {
        std::string name;
        int64_t definitions = 0, declarations = 0, operations = 0;
        bool llvm_ir = false;
    };
    Clock::time_point begin;
    std::vector<Measurement> stages;
    std::vector<Counts> counts;
    std::optional<int64_t> peak_rss_bytes;
    int64_t total_ns = 0;
    bool finished = false;
    bool succeeded = false;
};
