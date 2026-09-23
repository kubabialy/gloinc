#pragma once
#include <array>
#include <optional>
#include <string_view>

enum class ArenaPrimitive { Create, Allocate, Reset, Destroy, Require };
inline constexpr std::array<std::string_view, 5> arena_primitive_names{
    "__arena_general_create", "__arena_general_alloc", "__arena_general_reset",
    "__arena_general_destroy", "__arena_require"};
inline constexpr std::array<std::string_view, 4> arena_runtime_names{
    "gloin_arena_general_create", "gloin_arena_general_alloc", "gloin_arena_general_reset",
    "gloin_arena_general_destroy"};
template <size_t N>
inline std::optional<ArenaPrimitive> arena_operation(std::string_view name,
                                                     const std::array<std::string_view, N> &names) {
    for (size_t i = 0; i < names.size(); ++i)
        if (name == names[i])
            return static_cast<ArenaPrimitive>(i);
    return std::nullopt;
}
