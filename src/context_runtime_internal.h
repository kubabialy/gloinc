#pragma once
#include <span>
#include <string>
namespace gloin::process {
// Owns a snapshot during synchronous invocation, restoring the previous scope.
class ArgumentsScope {
    void *scope = nullptr;

  public:
    explicit ArgumentsScope(std::span<const std::string> arguments) noexcept;
    ~ArgumentsScope();
    bool valid() const { return scope != nullptr; }
    ArgumentsScope(const ArgumentsScope &) = delete;
    ArgumentsScope &operator=(const ArgumentsScope &) = delete;
};
} // namespace gloin::process
