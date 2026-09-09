#ifndef GLOINC_SEMA_H
#define GLOINC_SEMA_H

#include "AST.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

struct Type {
    virtual ~Type() = default;
    virtual std::string to_string() const = 0;
    virtual bool equals(const Type& other) const = 0;
};

struct PrimitiveType : public Type {
    std::string name;
    
    explicit PrimitiveType(std::string name) : name(std::move(name)) {}
    
    std::string to_string() const override { return name; }
    bool equals(const Type& other) const override {
        if (const auto* other_prim = dynamic_cast<const PrimitiveType*>(&other)) {
            return name == other_prim->name;
        }
        return false;
    }
};

struct VoidType : public Type {
    std::string to_string() const override { return "void"; }
    bool equals(const Type& other) const override {
        return dynamic_cast<const VoidType*>(&other) != nullptr;
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
            if (i < param_types.size() - 1) ss << ", ";
        }
        ss << ") -> " << return_type->to_string();
        return ss.str();
    }
    
    bool equals(const Type& other) const override {
        if (const auto* other_fn = dynamic_cast<const FunctionType*>(&other)) {
            if (!return_type->equals(*other_fn->return_type)) return false;
            if (param_types.size() != other_fn->param_types.size()) return false;
            for (size_t i = 0; i < param_types.size(); ++i) {
                if (!param_types[i]->equals(*other_fn->param_types[i])) return false;
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
    
    StructType(std::string name, std::vector<Field> fields, bool is_packed)
        : name(std::move(name)), fields(std::move(fields)), is_packed(is_packed) {}
    
    std::string to_string() const override { return "struct " + name; }
    bool equals(const Type& other) const override {
        if (const auto* other_struct = dynamic_cast<const StructType*>(&other)) {
            return name == other_struct->name;
        }
        return false;
    }
    
    const Field* get_field(const std::string& field_name) const {
        for (const auto& field : fields) {
            if (field.name == field_name) return &field;
        }
        return nullptr;
    }
};

struct DeferredType : public Type {
    std::shared_ptr<Type> value_type;
    
    explicit DeferredType(std::shared_ptr<Type> value_type)
        : value_type(std::move(value_type)) {}
        
    std::string to_string() const override {
        return "Deferred<" + value_type->to_string() + ">";
    }
    
    bool equals(const Type& other) const override {
        if (const auto* other_defer = dynamic_cast<const DeferredType*>(&other)) {
            return value_type->equals(*other_defer->value_type);
        }
        return false;
    }
};

struct Symbol {
    std::string name;
    std::shared_ptr<Type> type;
    bool is_mutable = false;
    // Potentially location info etc.
};

class Scope {
public:
    std::unordered_map<std::string, Symbol> symbols;
    std::unordered_map<std::string, std::shared_ptr<Type>> types; // Registry for user-defined types
    std::shared_ptr<Scope> parent;
    
    explicit Scope(std::shared_ptr<Scope> parent = nullptr) : parent(std::move(parent)) {}
    
    void define(const std::string& name, Symbol symbol);
    Symbol* resolve(const std::string& name);
    
    void define_type(const std::string& name, std::shared_ptr<Type> type);
    std::shared_ptr<Type> resolve_type(const std::string& name);
};

class Sema {
public:
    explicit Sema(std::shared_ptr<Diagnostics> diagnostics = std::make_shared<Diagnostics>());
    std::shared_ptr<Diagnostics> diagnostics() const { return diagnostics_; }
    
    // Main entry point
    bool check_program(const std::vector<std::unique_ptr<Statement>>& program);
    
    // Visit methods
    void check_statement(const Statement* stmt);
    std::shared_ptr<Type> check_expression(const Expression* expr);
    
private:
    std::shared_ptr<Scope> current_scope;
    std::shared_ptr<Diagnostics> diagnostics_;
    SourceSpan current_span;
    
    void enter_scope();
    void leave_scope();
    
    // Helpers
    std::shared_ptr<Type> resolve_type_from_string(const std::string& name);
    std::shared_ptr<Type> get_builtin_type(const std::string& name);
    void log_error(const std::string& msg);
    
    // Builtin types cache
    std::unordered_map<std::string, std::shared_ptr<Type>> builtin_types;
    
    // Error handling
    bool has_errors = false;
    std::vector<std::string> errors;
public:
    bool has_error() const { return has_errors || diagnostics_->has_errors(); }
    const std::vector<std::string>& get_errors() const { return errors; }
};

#endif // GLOINC_SEMA_H
