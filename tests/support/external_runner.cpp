#include "external_runner.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <charconv>
#include <optional>

namespace gloin_test {
namespace {
llvm::Expected<std::string> read_file(const std::string &path) {
    auto buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer)
        return llvm::createStringError(buffer.getError(), "Cannot read %s", path.c_str());
    return (*buffer)->getBuffer().str();
}

llvm::Expected<std::string> run_tool(const ToolCommand &tool,
                                     const std::vector<std::string> &arguments,
                                     const std::string &output_prefix, unsigned timeout_seconds) {
    std::vector<llvm::StringRef> argv{tool.path};
    for (const auto &arg : tool.arguments)
        argv.push_back(arg);
    for (const auto &arg : arguments)
        argv.push_back(arg);
    const auto stdout_path = output_prefix + ".stdout";
    const auto stderr_path = output_prefix + ".stderr";
    const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(), stdout_path,
                                                        stderr_path};
    std::string message;
    bool execution_failed = false;
    const int status = llvm::sys::ExecuteAndWait(tool.path, argv, std::nullopt, redirects,
                                                 timeout_seconds, 0, &message, &execution_failed);
    auto stderr_text = read_file(stderr_path);
    if (execution_failed || status != 0) {
        const auto diagnostics =
            stderr_text ? *stderr_text : llvm::toString(stderr_text.takeError());
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(), "%s failed (status %d; timeout %us): %s\n%s",
            tool.path.c_str(), status, timeout_seconds, message.c_str(), diagnostics.c_str());
    }
    if (!stderr_text)
        return stderr_text.takeError();
    return read_file(stdout_path);
}
} // namespace

llvm::Expected<int> run_external_mlir(llvm::StringRef source, const ToolCommand &optimizer,
                                      const ToolCommand &runner, unsigned timeout_seconds) {
    if (timeout_seconds == 0)
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "Timeout must be positive");
    llvm::SmallString<128> directory;
    if (auto ec = llvm::sys::fs::createUniqueDirectory("gloinc-e2e", directory))
        return llvm::createStringError(ec, "Cannot create E2E temporary directory");
    auto cleanup = llvm::make_scope_exit([&] { llvm::sys::fs::remove_directories(directory); });
    const std::string prefix = directory.str().str() + "/";
    const std::string input = prefix + "input.mlir";
    const std::string lowered = prefix + "lowered.mlir";
    std::error_code ec;
    llvm::raw_fd_ostream stream(input, ec);
    if (ec)
        return llvm::createStringError(ec, "Cannot write E2E input");
    stream << source;
    stream.close();
    if (stream.has_error()) {
        auto error = stream.error();
        stream.clear_error();
        return llvm::createStringError(error, "Cannot finish writing E2E input");
    }

    auto optimized =
        run_tool(optimizer,
                 {input, "--convert-scf-to-cf", "--convert-cf-to-llvm", "--convert-arith-to-llvm",
                  "--convert-func-to-llvm", "--finalize-memref-to-llvm",
                  "--reconcile-unrealized-casts", "-o", lowered},
                 prefix + "optimizer", timeout_seconds);
    if (!optimized)
        return optimized.takeError();
    auto output = run_tool(runner, {lowered, "-e", "main", "-entry-point-result=i32"},
                           prefix + "runner", timeout_seconds);
    if (!output)
        return output.takeError();
    auto text = llvm::StringRef(*output).trim();
    int result = 0;
    auto parsed = std::from_chars(text.begin(), text.end(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.end())
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Runner did not return one i32: %s", output->c_str());
    return result;
}
} // namespace gloin_test
