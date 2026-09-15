#ifndef GLOINC_CHECKED_PROGRAM_H
#define GLOINC_CHECKED_PROGRAM_H

#include "AST.h"
#include <array>
#include <optional>
#include <unordered_map>
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

using SymbolId = size_t;
inline constexpr SymbolId invalid_symbol = static_cast<SymbolId>(-1);
enum class SymbolKind { Variable, Parameter, Function, Constant };
struct ConstantValue {
    CoreType type;
    std::variant<int64_t, double, bool> value;
};
struct ResolvedSymbol {
    SymbolId id;
    std::string name;
    SymbolKind kind;
    CoreType type; // Value type, or function return type.
    std::vector<CoreType> parameters;
    bool is_mutable;
    SourceSpan span;
};
struct SemanticData {
    TargetInfo target;
    std::unordered_map<SymbolId, ConstantValue> constants;
    std::unordered_map<const Node *, CoreType> types;
    std::unordered_map<const Identifier *, SymbolId> bindings;
    std::vector<ResolvedSymbol> symbols;
};

class Sema;
class CodeGen;
class CheckedProgram {
  public:
    CheckedProgram(const CheckedProgram &) = delete;
    CheckedProgram &operator=(const CheckedProgram &) = delete;
    const std::vector<ResolvedSymbol> &symbols() const { return data.symbols; }
    TargetInfo target() const { return data.target; }
    size_t typed_node_count() const { return data.types.size(); }
    size_t bound_name_count() const { return data.bindings.size(); }

  private:
    friend class Sema;
    friend class CodeGen;
    CheckedProgram(std::vector<std::unique_ptr<Statement>> program, SemanticData data)
        : program(std::move(program)), data(std::move(data)) {}
    // AST storage cannot be accessed/mutated through the public checked-program API.
    std::vector<std::unique_ptr<Statement>> program;
    SemanticData data;
};
#endif
