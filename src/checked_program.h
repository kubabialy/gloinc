#ifndef GLOINC_CHECKED_PROGRAM_H
#define GLOINC_CHECKED_PROGRAM_H

#include "AST.h"
#include "arena_abi.h"
#include "stdlib_abi.h"
#include "compilation_mode.h"
#include "numeric.h"
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using SymbolId = size_t;
inline constexpr SymbolId invalid_symbol = static_cast<SymbolId>(-1);
enum class SymbolKind { Variable, Parameter, Function, Constant };
// Checked value types retain nominal struct identity independently of builtin types.
struct PointerLayer {
    bool nullable;
    bool read_only;
    bool operator==(const PointerLayer &) const = default;
};
struct ValueType {
    CoreType scalar = CoreType::Void;
    std::optional<size_t> structure;
    std::vector<PointerLayer> pointers; // Outermost first; base retains its nominal identity.
    std::shared_ptr<ValueType> array_element;
    size_t array_length = 0;
    ValueType() = default;
    ValueType(CoreType scalar) : scalar(scalar) {}
    static ValueType record(size_t id) {
        ValueType type;
        type.structure = id;
        return type;
    }
    static ValueType array(ValueType element, size_t length) {
        ValueType type;
        type.array_element = std::make_shared<ValueType>(std::move(element));
        type.array_length = length;
        return type;
    }
    bool is_pointer() const { return !pointers.empty(); }
    bool is_array() const { return pointers.empty() && static_cast<bool>(array_element); }
    ValueType pointee() const {
        if (!is_pointer())
            throw std::logic_error("Value is not a pointer");
        auto result = *this;
        result.pointers.erase(result.pointers.begin());
        return result;
    }
    CoreType builtin() const {
        if (structure || is_pointer() || is_array())
            throw std::logic_error("Aggregate/pointer is not a builtin type");
        return scalar;
    }
    bool operator==(const ValueType &other) const {
        return scalar == other.scalar && structure == other.structure &&
               pointers == other.pointers && array_length == other.array_length &&
               (array_element && other.array_element
                    ? *array_element == *other.array_element
                    : !array_element && !other.array_element);
    }
};
struct CheckedField {
    std::string name;
    ValueType type;
    bool is_public;
    bool is_mutable;
};
struct CheckedStruct {
    std::string name;
    std::vector<CheckedField> fields;
};
struct ResolvedSymbol {
    SymbolId id;
    std::string name;
    SymbolKind kind;
    ValueType type; // Value type, or function return type.
    std::vector<ValueType> parameters;
    bool is_mutable;
    SourceSpan span;
};
struct SemanticData {
    std::vector<const SourceModule *> modules; // Dependency order, each file once.
    // Concrete copies give every generic method specialization distinct node identities.
    std::vector<std::unique_ptr<FunctionDefinition>> specialized_methods;
    std::unordered_map<const MemberAccessExpression *, SymbolId> module_constants;
    std::unordered_map<const CallExpression *, ArenaPrimitive> arena_runtime_calls;
    std::unordered_map<const CallExpression *, StandardPrimitive> standard_calls;
    // Typed allocation calls target a checked library layout method. true is try_alloc.
    std::unordered_map<const CallExpression *, bool> arena_allocations;
    std::unordered_map<const FunctionDefinition *, std::vector<const DeferStatement *>> defers;
    // Instance calls pass the receiver once, before explicit arguments.
    // true takes the address of struct storage; false passes a pointer value.
    std::unordered_map<const CallExpression *, bool> method_receivers;
    std::unordered_set<SymbolId> address_taken;
    std::unordered_set<const MemberAccessExpression *> indirect_members;
    std::vector<CheckedStruct> structures;
    std::unordered_map<const MemberAccessExpression *, size_t> field_indices;
    std::unordered_map<const StructLiteral *, std::vector<size_t>> literal_fields;
    std::unordered_set<const CallExpression *> runtime_calls;
    std::unordered_map<SymbolId, std::string> linkage_names;
    TargetInfo target;
    CompilationMode mode = CompilationMode::Module;
    std::optional<SymbolId> entry_point;
    std::unordered_map<SymbolId, ConstantValue> constants;
    std::unordered_map<const Expression *, ConstantValue> literals;
    std::unordered_map<const Node *, ValueType> types;
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
    CompilationMode mode() const { return data.mode; }
    std::optional<SymbolId> entry_point() const { return data.entry_point; }
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
