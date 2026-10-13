#include "compilation_timings.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <sys/resource.h>

CompilationTimings::Stage::Stage(CompilationTimings *owner, const char *name)
    : owner(owner), name(name) {
    if (owner)
        begin = Clock::now();
}

void CompilationTimings::Stage::stop() {
    if (!owner)
        return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - begin).count();
    auto found = std::find_if(owner->stages.begin(), owner->stages.end(),
                             [&](const auto &stage) { return stage.name == name; });
    if (found == owner->stages.end())
        owner->stages.push_back({name, elapsed, 1});
    else {
        found->nanoseconds += elapsed;
        ++found->calls;
    }
    owner = nullptr;
}

void CompilationTimings::snapshot(const char *name, mlir::ModuleOp module) {
    Counts count{name};
    module.walk([&](mlir::Operation *op) {
        ++count.operations;
        if (auto function = mlir::dyn_cast<mlir::func::FuncOp>(op)) {
            if (function.isExternal()) ++count.declarations;
            else ++count.definitions;
        } else if (auto function = mlir::dyn_cast<mlir::LLVM::LLVMFuncOp>(op)) {
            if (function.isExternal()) ++count.declarations;
            else ++count.definitions;
        }
    });
    counts.push_back(std::move(count));
}

void CompilationTimings::snapshot(const char *name, const llvm::Module &module) {
    Counts count{name};
    count.llvm_ir = true;
    for (const auto &function : module) {
        if (function.isDeclaration()) ++count.declarations;
        else ++count.definitions;
        count.operations += function.getInstructionCount();
    }
    counts.push_back(std::move(count));
}

void CompilationTimings::finish(bool success) {
    if (finished)
        return;
    total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    succeeded = success;
    finished = true;
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        peak_rss_bytes = usage.ru_maxrss;
#elif defined(__linux__)
        peak_rss_bytes = static_cast<int64_t>(usage.ru_maxrss) * 1024;
#endif
    }
}

void CompilationTimings::print(llvm::raw_ostream &out, const std::string &mode,
                                const std::string &optimization,
                                const std::string &compiler_version,
                                const std::string &llvm_version) const {
    llvm::json::Array stage_json;
    int64_t measured_ns = 0;
    for (const auto &stage : stages) {
        stage_json.push_back(llvm::json::Object{{"name", stage.name},
            {"nanoseconds", stage.nanoseconds}, {"calls", stage.calls}});
        measured_ns += stage.nanoseconds;
    }
    llvm::json::Array counts_json;
    for (const auto &count : counts) {
        counts_json.push_back(llvm::json::Object{{"name", count.name},
            {"function_definitions", count.definitions},
            {"function_declarations", count.declarations},
            {count.llvm_ir ? "instructions" : "operations", count.operations}});
    }
    llvm::json::Object report{{"schema_version", 1}, {"compiler_version", compiler_version},
        {"llvm_version", llvm_version}, {"mode", mode}, {"optimization", optimization},
        {"compilation_succeeded", succeeded}, {"total_ns", total_ns},
        {"unattributed_ns", total_ns - measured_ns}, {"stages", std::move(stage_json)},
        {"counts", std::move(counts_json)}, {"rss_scope", "compiler_process"},
        {"peak_rss_bytes", peak_rss_bytes ? llvm::json::Value(*peak_rss_bytes)
                                         : llvm::json::Value(nullptr)}};
    out << "gloinc-timings: " << llvm::json::Value(std::move(report)) << '\n';
    out.flush();
}
