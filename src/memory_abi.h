#pragma once
#include <array>
#include <optional>
#include <string_view>

enum class MemoryPrimitive { Allocate, Release };
inline constexpr std::array<std::string_view, 2> memory_primitive_names{
    "__memory_alloc", "__memory_free"};
inline constexpr std::array<std::string_view, 2> memory_runtime_names{
    "gloin_memory_alloc", "gloin_memory_free"};
template <size_t N>
inline std::optional<MemoryPrimitive> memory_operation(
    std::string_view name, const std::array<std::string_view, N> &names) {
    for (size_t i = 0; i < names.size(); ++i)
        if (name == names[i])
            return static_cast<MemoryPrimitive>(i);
    return std::nullopt;
}

enum class MemoryIntrinsic { SizeOf, AlignOf, Padding, Place };
