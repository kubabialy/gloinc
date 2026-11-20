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
enum class ByteOrder { Native, Big, Little };
enum class CoreType {
    Void,
    Bool,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    String,
    BeI8,
    BeI16,
    BeI32,
    BeI64,
    BeU8,
    BeU16,
    BeU32,
    BeU64,
    LeI8,
    LeI16,
    LeI32,
    LeI64,
    LeU8,
    LeU16,
    LeU32,
    LeU64,
    NativeSignedBase = 64,
    NativeUnsignedBase = 128
};
struct CoreTypeInfo {
    CoreType id;
    std::string_view name;
    unsigned bits;
    bool is_integer;
    bool is_signed;
    ByteOrder byte_order = ByteOrder::Native;
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
    CoreTypeInfo{CoreType::String, "string", 0, false, false},
    CoreTypeInfo{CoreType::BeI8, "be_i8", 8, true, true, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeI16, "be_i16", 16, true, true, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeI32, "be_i32", 32, true, true, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeI64, "be_i64", 64, true, true, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeU8, "be_u8", 8, true, false, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeU16, "be_u16", 16, true, false, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeU32, "be_u32", 32, true, false, ByteOrder::Big},
    CoreTypeInfo{CoreType::BeU64, "be_u64", 64, true, false, ByteOrder::Big},
    CoreTypeInfo{CoreType::LeI8, "le_i8", 8, true, true, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeI16, "le_i16", 16, true, true, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeI32, "le_i32", 32, true, true, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeI64, "le_i64", 64, true, true, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeU8, "le_u8", 8, true, false, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeU16, "le_u16", 16, true, false, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeU32, "le_u32", 32, true, false, ByteOrder::Little},
    CoreTypeInfo{CoreType::LeU64, "le_u64", 64, true, false, ByteOrder::Little},
};
const CoreTypeInfo &core_type_info(CoreType type);
CoreType native_integer_type(unsigned bits, bool is_signed);
inline unsigned integer_storage_bits(unsigned bits) {
    return bits <= 8 ? 8 : bits <= 16 ? 16 : bits <= 32 ? 32 : 64;
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
    if (name.size() >= 2 && name.size() <= 3 && (name[0] == 'u' || name[0] == 'i') &&
        name[1] >= '1' && name[1] <= '9') {
        unsigned bits = name[1] - '0';
        if (name.size() == 3) {
            if (name[2] < '0' || name[2] > '9')
                return std::nullopt;
            bits = bits * 10 + name[2] - '0';
        }
        if (bits <= 64)
            return native_integer_type(bits, name[0] == 'i');
    }
    return std::nullopt;
}

struct ConstantValue {
    CoreType type;
    std::variant<llvm::APInt, llvm::APFloat, bool, std::string> value;
};
std::optional<ConstantValue> convert_integer_constant(const ConstantValue &value, CoreType target);

const llvm::fltSemantics &float_semantics(CoreType type);
std::optional<ConstantValue> parse_numeric_literal(std::string_view spelling, bool floating,
                                                   CoreType type, bool negative,
                                                   std::string &error);
#endif
