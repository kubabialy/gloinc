#ifndef GLOINC_SEMA_H
#define GLOINC_SEMA_H

#include "AST.h"
#include "checked_program.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Type {
    virtual ~Type() = default;
    virtual std::string to_string() const = 0;
    virtual bool equals(const Type &other) const = 0;
};

struct PrimitiveType : public Type {
    std::string name;

    explicit PrimitiveType(std::string name) : name(std::move(name)) {}

    std::string to_string() const override { return name; }
    bool equals(const Type &other) const override {
        if (const auto *other_prim = dynamic_cast<const PrimitiveType *>(&other)) {
            return name == other_prim->name;
        }
        return false;
    }
};

struct VoidType : public Type {
    std::string to_string() const override { return "void"; }
    bool equals(const Type &other) const override {
        return dynamic_cast<const VoidType *>(&other) != nullptr;
    }
};

struct FunctionType : public Type {
    std::shared_ptr<Type> return_type;
    std::vector<std::shared_ptr<Type>> param_types;

    FunctionType(std::shared_ptr<Type> ret, std::vector<std::shared_ptr<Type>> params)
        : return_type(std::move(ret)), param_types(std::move(params)) {}

    std::string to_string() const override {
        std::stringstream ss;
        ss << "fn(";
        for (size_t i = 0; i < param_types.size(); ++i) {
            ss << param_types[i]->to_string();
            if (i < param_types.size() - 1)
                ss << ", ";
        }
        ss << ") -> " << return_type->to_string();
        return ss.str();
    }

    bool equals(const Type &other) const override {
        if (const auto *other_fn = dynamic_cast<const FunctionType *>(&other)) {
            if (!return_type->equals(*other_fn->return_type))
                return false;
            if (param_types.size() != other_fn->param_types.size())
                return false;
            for (size_t i = 0; i < param_types.size(); ++i) {
                if (!param_types[i]->equals(*other_fn->param_types[i]))
                    return false;
            }
            return true;
        }
        return false;
    }
};

struct StructType : public Type {
    struct Field {
        std::string name;
        std::shared_ptr<Type> type;
        bool is_public;
    };

    std::string name;
    std::vector<Field> fields;
    bool is_packed;
    std::optional<size_t> identity;
    const SourceModule *owner = nullptr;
    bool is_public = false;
    std::unordered_map<std::string, const FunctionDefinition *> methods;

    StructType(std::string name, std::vector<Field> fields, bool is_packed)
        : name(std::move(name)), fields(std::move(fields)), is_packed(is_packed) {}

    std::string to_string() const override { return "struct " + name; }
    bool equals(const Type &other) const override {
        if (const auto *other_struct = dynamic_cast<const StructType *>(&other)) {
            return identity || other_struct->identity ? identity == other_struct->identity
                                                      : name == other_struct->name;
        }
        return false;
    }

    const Field *get_field(const std::string &field_name) const {
        for (const auto &field : fields) {
            if (field.name == field_name)
                return &field;
        }
        return nullptr;
    }
};

struct PointerType : public Type {
    std::shared_ptr<Type> pointee;
    bool nullable;
    bool read_only;
    PointerType(std::shared_ptr<Type> pointee, bool nullable, bool read_only)
        : pointee(std::move(pointee)), nullable(nullable), read_only(read_only) {}
    std::string to_string() const override {
        return std::string(nullable ? "*" : "&") + (read_only ? "const " : "") +
               pointee->to_string();
    }
    bool equals(const Type &other) const override {
        const auto *pointer = dynamic_cast<const PointerType *>(&other);
        return pointer && nullable == pointer->nullable && read_only == pointer->read_only &&
               pointee->equals(*pointer->pointee);
    }
};

struct ArrayType : public Type {
    std::shared_ptr<Type> element;
    size_t length;
    ArrayType(std::shared_ptr<Type> element, size_t length)
        : element(std::move(element)), length(length) {}
    std::string to_string() const override {
        return "[" + element->to_string() + "; " + std::to_string(length) + "]";
    }
    bool equals(const Type &other) const override {
        const auto *array = dynamic_cast<const ArrayType *>(&other);
        return array && length == array->length && element->equals(*array->element);
    }
};

struct DeferredType : public Type {
    std::shared_ptr<Type> value_type;

    explicit DeferredType(std::shared_ptr<Type> value_type) : value_type(std::move(value_type)) {}

    std::string to_string() const override { return "Deferred<" + value_type->to_string() + ">"; }

    bool equals(const Type &other) const override {
        if (const auto *other_defer = dynamic_cast<const DeferredType *>(&other)) {
            return value_type->equals(*other_defer->value_type);
        }
        return false;
    }
};

struct Symbol {
    SymbolId id = invalid_symbol;
    std::string name;
    std::shared_ptr<Type> type;
    bool is_mutable = false;
    SymbolKind kind = SymbolKind::Variable;
    unsigned loop_depth = 0;
};

class Scope {
  public:
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<Type>> types; // Registry for user-defined types
    std::shared_ptr<Scope> parent;

    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent(std::move(parent)) {}

    void define(const std::string &name, Symbol symbol);
    Symbol *resolve(const std::string &name);

    void define_type(const std::string &name, std::shared_ptr<Type> type);
    std::shared_ptr<Type> resolve_type(const std::string &name);
};

class Sema {
  public:
    explicit Sema(std::shared_ptr<Diagnostics> diagnostics = std::make_shared<Diagnostics>());
    ~Sema();
    std::shared_ptr<Diagnostics> diagnostics() const { return diagnostics_; }

    // Main entry point
    bool check_program(const std::vector<std::unique_ptr<Statement>> &program);
    // Takes exclusive ownership. Only successful core checking can construct this result.
    std::unique_ptr<CheckedProgram>
    check_for_codegen(std::vector<std::unique_ptr<Statement>> program, TargetInfo target = {},
                      CompilationMode mode = CompilationMode::Module);

    // Visit methods
    void check_statement(const Statement *stmt);
    std::shared_ptr<Type> check_expression(const Expression *expr,
                                           std::optional<CoreType> expected = std::nullopt,
                                           bool statement_context = false);

  private:
    std::optional<SemanticData> recording;
    using ModuleImports = std::unordered_map<std::string, const SourceModule *>;
    ModuleImports imports;
    std::unordered_map<const SourceModule *, ModuleImports> module_imports;
    std::unordered_map<const SourceModule *, std::shared_ptr<Scope>> module_scopes;
    const SourceModule *current_module = nullptr;
    std::shared_ptr<Type> check_standard_primitive(const CallExpression *call, StandardPrimitive kind);
    bool prepare_modules(const std::vector<std::unique_ptr<Statement>> &program);
    void select_module(const SourceModule *module);
    void collect_declarations(const std::vector<std::unique_ptr<Statement>> &program);
    void check_bodies(const std::vector<std::unique_ptr<Statement>> &program);
    Symbol *module_member(const MemberAccessExpression *member, bool &handled, bool diagnose);

    bool resolving_callee = false;
    bool checking_constant = false;
    std::optional<CoreType> expected_type;
    std::shared_ptr<Type> current_return_type;
    const FunctionDefinition *current_function = nullptr;
    std::optional<CoreType> numeric_anchor(const Expression *expression);
    std::shared_ptr<Type> check_numeric_literal(const Expression *expression);
    std::pair<std::shared_ptr<Type>, std::shared_ptr<Type>>
    check_binary_operands(const InfixExpression *expression);
    enum class Initialization { Uninitialized, Initialized, MaybeInitialized };
    using InitializationState = std::unordered_map<SymbolId, Initialization>;
    InitializationState initialization;
    bool falls_through = true;
    unsigned loop_depth = 0;
    void check_conditional(const Expression *condition, const BlockStatement *body,
                           const Statement *alternative, std::string_view construct);
    void check_for(const ForStatement *statement);
    void check_constant(const VariableDeclaration *declaration);
    std::shared_ptr<Type> check_constant_expression(const Expression *expression);
    std::optional<ConstantValue> evaluate_constant(const Expression *expression);
    void merge_initialization(const InitializationState &before, const InitializationState &left,
                              bool left_reaches, const InitializationState &right,
                              bool right_reaches);
    std::unordered_map<const FunctionDefinition *, std::shared_ptr<FunctionType>>
        collected_functions;
    std::shared_ptr<FunctionType> collect_function(const FunctionDefinition *function);
    std::optional<ValueType> value_type(const std::shared_ptr<Type> &type) const;
    std::vector<std::shared_ptr<StructType>> collected_struct_types;
    void collect_structs(const std::vector<std::unique_ptr<Statement>> &program);
    void collect_methods(const std::vector<std::unique_ptr<Statement>> &program);
    std::unordered_map<const FunctionDefinition *, bool> arena_methods;
    void register_arena_method(const std::shared_ptr<StructType> &structure,
                               const FunctionDefinition *method,
                               const std::shared_ptr<FunctionType> &signature);
    std::shared_ptr<Type> check_arena_primitive(const CallExpression *call, ArenaPrimitive kind);
    std::shared_ptr<Type> arena_value_type_hint(const Expression *value);
    void check_methods(const StructDefinition *definition);
    std::shared_ptr<StructType> method_type_receiver(const Expression *expression);
    const FunctionDefinition *method_target(const MemberAccessExpression *member);
    std::shared_ptr<Type> check_method_call(const CallExpression *call, bool &handled);
    void validate_struct_cycles();
    std::shared_ptr<Type> expression_type_hint(const Expression *expression);
    std::shared_ptr<Type> check_struct_literal(const StructLiteral *literal);
    std::shared_ptr<Type> check_field(const MemberAccessExpression *member);
    struct Place {
        std::shared_ptr<Type> type;
        bool writable = false;
        bool addressable = false;
    };
    std::shared_ptr<PointerType> expected_pointer;
    std::shared_ptr<ArrayType> expected_array;
    Place check_place(const Expression *expression, bool take_address = false);
    std::shared_ptr<Type> check_pointer_unary(const PrefixExpression *expression);
    std::shared_ptr<Type> check_indirect_assignment(const AssignmentExpression *assignment);
    std::shared_ptr<Type> check_typed_expression(const Expression *expression,
                                                 const std::shared_ptr<Type> &expected);
    std::shared_ptr<Type> check_pointer_comparison(const InfixExpression *expression);
    bool pointer_conversion(const PointerType &source, const PointerType &target) const;
    std::shared_ptr<Type> check_expression_impl(const Expression *expr);
    std::shared_ptr<Type> resolve_annotation(const Identifier *annotation, bool allow_void = false);
    bool define_symbol(const Identifier *name, Symbol symbol, SymbolKind kind);
    std::shared_ptr<Scope> current_scope;
    std::shared_ptr<Diagnostics> diagnostics_;
    SourceSpan current_span;

    void enter_scope();
    void leave_scope();

    // Helpers
    std::shared_ptr<Type> resolve_type_from_string(const std::string &name);
    std::shared_ptr<Type> get_builtin_type(const std::string &name);
    void log_error(const std::string &msg);

    // Builtin types cache
    std::unordered_map<std::string, std::shared_ptr<Type>> builtin_types;

    // Error handling
    bool has_errors = false;
    std::vector<std::string> errors;

  public:
    bool has_error() const { return has_errors || diagnostics_->has_errors(); }
    const std::vector<std::string> &get_errors() const { return errors; }
};

#endif // GLOINC_SEMA_H
