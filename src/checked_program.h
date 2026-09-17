#ifndef GLOINC_CHECKED_PROGRAM_H
#define GLOINC_CHECKED_PROGRAM_H

#include "AST.h"
#include "compilation_mode.h"
#include "numeric.h"
#include <unordered_map>

using SymbolId = size_t;
inline constexpr SymbolId invalid_symbol = static_cast<SymbolId>(-1);
enum class SymbolKind { Variable, Parameter, Function, Constant };
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
    CompilationMode mode = CompilationMode::Module;
    std::optional<SymbolId> entry_point;
    std::unordered_map<SymbolId, ConstantValue> constants;
    std::unordered_map<const Expression *, ConstantValue> literals;
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
