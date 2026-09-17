#ifndef GLOINC_NUMERIC_H
#define GLOINC_NUMERIC_H
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// Language identities retain signedness even though MLIR uses signless integer storage.
enum class CoreType { Void, Bool, I8, I16, I32, I64, U8, U16, U32, U64, F32, F64 };
struct CoreTypeInfo {
    CoreType id;
    std::string_view name;
    unsigned bits;
    bool is_integer;
    bool is_signed;
};
inline constexpr std::array core_types = {
    CoreTypeInfo{CoreType::Void, "void", 0, false, false},
    CoreTypeInfo{CoreType::Bool, "bool", 1, false, false},
    CoreTypeInfo{CoreType::I8, "i8", 8, true, true},
    CoreTypeInfo{CoreType::I16, "i16", 16, true, true},
    CoreTypeInfo{CoreType::I32, "i32", 32, true, true},
    CoreTypeInfo{CoreType::I64, "i64", 64, true, true},
    CoreTypeInfo{CoreType::U8, "u8", 8, true, false},
    CoreTypeInfo{CoreType::U16, "u16", 16, true, false},
    CoreTypeInfo{CoreType::U32, "u32", 32, true, false},
    CoreTypeInfo{CoreType::U64, "u64", 64, true, false},
    CoreTypeInfo{CoreType::F32, "f32", 32, false, true},
    CoreTypeInfo{CoreType::F64, "f64", 64, false, true},
};
inline const CoreTypeInfo &core_type_info(CoreType type) {
    return core_types.at(static_cast<size_t>(type));
}

// This is the selected release target, independent of the compiler process's pointer size.
struct TargetInfo {
    unsigned pointer_bits = 64;
};
inline std::optional<CoreType> resolve_core_type(std::string_view name, TargetInfo target = {}) {
    if (name == "int")
        return CoreType::I32;
    if (name == "usize" && target.pointer_bits == 64)
        return CoreType::U64;
    for (const auto &type : core_types)
        if (type.name == name)
            return type.id;
    return std::nullopt;
}

struct ConstantValue {
    CoreType type;
    std::variant<llvm::APInt, llvm::APFloat, bool> value;
};

const llvm::fltSemantics &float_semantics(CoreType type);
std::optional<ConstantValue> parse_numeric_literal(std::string_view spelling, bool floating,
                                                   CoreType type, bool negative,
                                                   std::string &error);
#endif
