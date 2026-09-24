#pragma once
#include <array>
#include <optional>
#include <string_view>

// Native operations precede descriptor-only operations: indices match both tables.
enum class StandardPrimitive {
    ParseI32,
    FormatI32,
    Input,
    StringCopy,
    ParseI64,
    ParseU64,
    ParseF32,
    ParseF64,
    ParseBool,
    FormatI64,
    FormatU64,
    FormatF32,
    FormatF64,
    FormatF32Fixed,
    FormatF64Fixed,
    I32ToI64,
    I64ToI32,
    I64ToU64,
    U64ToI64,
    I64ToF64,
    U64ToF64,
    F64ToI64,
    F64ToU64,
    F32ToF64,
    F64ToF32,
    IoStandard,
    IoOpen,
    IoRead,
    IoWrite,
    IoFlush,
    IoClose,
    IoErrorMessage,
    FsMetadata,
    FsMkdir,
    FsRemoveFile,
    FsRenameReplace,
    ProcessCount,
    ProcessArg,
    ProcessEnv,
    ProcessCwd,
    MathUnaryF32,
    MathUnaryF64,
    MathBinaryF32,
    MathBinaryF64,
    TimeMonotonic,
    RandomSplitMix64,
    ProcessStringView,
    IoStringView,
    StringView,
    StringLength,
    StringByte,
    StringSlice,
    StringBufferView,
    StringStore,
    StringWrite
};
inline constexpr std::array<std::string_view, 55> standard_primitive_names{"__std_parse_i32",
                                                                           "__std_format_i32",
                                                                           "__std_input",
                                                                           "__strings_copy",
                                                                           "__std_parse_i64",
                                                                           "__std_parse_u64",
                                                                           "__std_parse_f32",
                                                                           "__std_parse_f64",
                                                                           "__std_parse_bool",
                                                                           "__std_format_i64",
                                                                           "__std_format_u64",
                                                                           "__std_format_f32",
                                                                           "__std_format_f64",
                                                                           "__std_format_f32_fixed",
                                                                           "__std_format_f64_fixed",
                                                                           "__std_i64_from_i32",
                                                                           "__std_i32_from_i64",
                                                                           "__std_u64_from_i64",
                                                                           "__std_i64_from_u64",
                                                                           "__std_f64_from_i64",
                                                                           "__std_f64_from_u64",
                                                                           "__std_i64_from_f64",
                                                                           "__std_u64_from_f64",
                                                                           "__std_f64_from_f32",
                                                                           "__std_f32_from_f64",
                                                                           "__io_standard",
                                                                           "__io_open",
                                                                           "__io_read",
                                                                           "__io_write",
                                                                           "__io_flush",
                                                                           "__io_close",
                                                                           "__io_error_message",
                                                                           "__fs_metadata",
                                                                           "__fs_mkdir",
                                                                           "__fs_remove_file",
                                                                           "__fs_rename_replace",
                                                                           "__process_arg_count",
                                                                           "__process_arg",
                                                                           "__process_env",
                                                                           "__process_cwd",
                                                                           "__math_unary_f32",
                                                                           "__math_unary_f64",
                                                                           "__math_binary_f32",
                                                                           "__math_binary_f64",
                                                                           "__time_monotonic",
                                                                           "__random_splitmix64",
                                                                           "__process_string_view",
                                                                           "__io_string_view",
                                                                           "__std_string_view",
                                                                           "__strings_length",
                                                                           "__strings_byte",
                                                                           "__strings_slice",
                                                                           "__strings_buffer_view",
                                                                           "__strings_store",
                                                                           "__strings_write"};
inline constexpr std::array<std::string_view, 46> standard_runtime_names{
    "gloin_std_parse_i32",
    "gloin_std_format_i32",
    "gloin_std_input",
    "gloin_strings_copy",
    "gloin_std_parse_i64",
    "gloin_std_parse_u64",
    "gloin_std_parse_f32",
    "gloin_std_parse_f64",
    "gloin_std_parse_bool",
    "gloin_std_format_i64",
    "gloin_std_format_u64",
    "gloin_std_format_f32",
    "gloin_std_format_f64",
    "gloin_std_format_f32_fixed",
    "gloin_std_format_f64_fixed",
    "gloin_std_i64_from_i32",
    "gloin_std_i32_from_i64",
    "gloin_std_u64_from_i64",
    "gloin_std_i64_from_u64",
    "gloin_std_f64_from_i64",
    "gloin_std_f64_from_u64",
    "gloin_std_i64_from_f64",
    "gloin_std_u64_from_f64",
    "gloin_std_f64_from_f32",
    "gloin_std_f32_from_f64",
    "gloin_io_standard",
    "gloin_io_open",
    "gloin_io_read",
    "gloin_io_write",
    "gloin_io_flush",
    "gloin_io_close",
    "gloin_io_error_message",
    "gloin_fs_metadata",
    "gloin_fs_mkdir",
    "gloin_fs_remove_file",
    "gloin_fs_rename_replace",
    "gloin_process_arg_count",
    "gloin_process_arg",
    "gloin_process_env",
    "gloin_process_cwd",
    "gloin_math_unary_f32",
    "gloin_math_unary_f64",
    "gloin_math_binary_f32",
    "gloin_math_binary_f64",
    "gloin_time_monotonic",
    "gloin_random_splitmix64"};
inline bool standard_primitive_allowed(StandardPrimitive kind, std::string_view module) {
    if (kind == StandardPrimitive::TimeMonotonic)
        return module == "time";
    if (kind == StandardPrimitive::RandomSplitMix64)
        return module == "random";
    if (kind >= StandardPrimitive::MathUnaryF32 && kind <= StandardPrimitive::MathBinaryF64)
        return module == "math";
    if (kind >= StandardPrimitive::FsMetadata && kind <= StandardPrimitive::FsRenameReplace)
        return module == "fs";
    if ((kind >= StandardPrimitive::ProcessCount && kind <= StandardPrimitive::ProcessCwd) ||
        kind == StandardPrimitive::ProcessStringView)
        return module == "process";
    if ((kind >= StandardPrimitive::IoStandard && kind <= StandardPrimitive::IoErrorMessage) ||
        kind == StandardPrimitive::IoStringView)
        return module == "io";
    const bool strings =
        kind == StandardPrimitive::StringCopy || kind == StandardPrimitive::StringLength ||
        kind == StandardPrimitive::StringByte || kind == StandardPrimitive::StringSlice ||
        kind == StandardPrimitive::StringBufferView || kind == StandardPrimitive::StringStore ||
        kind == StandardPrimitive::StringWrite;
    return module == (strings ? "strings" : "std");
}
template <size_t N>
inline std::optional<StandardPrimitive>
standard_operation(std::string_view name, const std::array<std::string_view, N> &names) {
    for (size_t i = 0; i < N; ++i)
        if (name == names[i])
            return static_cast<StandardPrimitive>(i);
    return std::nullopt;
}

enum class NumericPrimitiveGroup { Parse, Format, Fixed, Convert };
struct NumericPrimitiveSignature {
    NumericPrimitiveGroup group;
    const char *input;
    const char *output;
    bool mode;
};
inline std::optional<NumericPrimitiveSignature> numeric_signature(StandardPrimitive kind) {
    switch (kind) {
    case StandardPrimitive::ParseI64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Parse, "string", "i64", false};
    case StandardPrimitive::ParseU64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Parse, "string", "u64", false};
    case StandardPrimitive::ParseF32:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Parse, "string", "f32", false};
    case StandardPrimitive::ParseF64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Parse, "string", "f64", false};
    case StandardPrimitive::ParseBool:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Parse, "string", "u8", false};
    case StandardPrimitive::FormatI64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Format, "i64", "u64", false};
    case StandardPrimitive::FormatU64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Format, "u64", "u64", false};
    case StandardPrimitive::FormatF32:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Format, "f32", "u64", false};
    case StandardPrimitive::FormatF64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Format, "f64", "u64", false};
    case StandardPrimitive::FormatF32Fixed:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Fixed, "f32", "u64", false};
    case StandardPrimitive::FormatF64Fixed:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Fixed, "f64", "u64", false};
    case StandardPrimitive::I32ToI64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "i32", "i64", false};
    case StandardPrimitive::I64ToI32:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "i64", "i32", false};
    case StandardPrimitive::I64ToU64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "i64", "u64", false};
    case StandardPrimitive::U64ToI64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "u64", "i64", false};
    case StandardPrimitive::I64ToF64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "i64", "f64", true};
    case StandardPrimitive::U64ToF64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "u64", "f64", true};
    case StandardPrimitive::F64ToI64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "f64", "i64", true};
    case StandardPrimitive::F64ToU64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "f64", "u64", true};
    case StandardPrimitive::F32ToF64:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "f32", "f64", false};
    case StandardPrimitive::F64ToF32:
        return NumericPrimitiveSignature{NumericPrimitiveGroup::Convert, "f64", "f32", true};
    default:
        return std::nullopt;
    }
}
