#include "sema.h"

std::shared_ptr<Type> Sema::arena_value_type_hint(const Expression *value) {
    if (auto type = expression_type_hint(value))
        return type;
    if (dynamic_cast<const IntegerLiteral *>(value))
        return get_builtin_type("i32");
    if (dynamic_cast<const FloatLiteral *>(value))
        return get_builtin_type("f32");
    if (dynamic_cast<const BooleanLiteral *>(value))
        return get_builtin_type("bool");
    if (dynamic_cast<const StringLiteral *>(value))
        return get_builtin_type("string");
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(value)) {
        if (prefix->op == "!")
            return get_builtin_type("bool");
        if (prefix->op == "-")
            return arena_value_type_hint(prefix->right.get());
    }
    if (const auto *binary = dynamic_cast<const InfixExpression *>(value)) {
        if (binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
            binary->op == ">" || binary->op == ">=" || binary->op == "&&" || binary->op == "||")
            return get_builtin_type("bool");
        if (auto anchor = numeric_anchor(value))
            return get_builtin_type(std::string(core_type_info(*anchor).name));
        return arena_value_type_hint(binary->left.get());
    }
    return nullptr;
}

void Sema::register_arena_method(const std::shared_ptr<StructType> &structure,
                                 const FunctionDefinition *method,
                                 const std::shared_ptr<FunctionType> &signature) {
    if (!structure->owner || structure->owner->module_name != "arena" ||
        structure->name != "GeneralArena" ||
        (method->name->value != "alloc" && method->name->value != "try_alloc"))
        return;
    auto receiver = std::make_shared<PointerType>(structure, false, false);
    auto storage = std::make_shared<PointerType>(get_builtin_type("u8"), true, false);
    if (method->is_static || !method->is_public || signature->param_types.size() != 3 ||
        !signature->param_types[0]->equals(*receiver) ||
        !signature->param_types[1]->equals(*get_builtin_type("u64")) ||
        !signature->param_types[2]->equals(*get_builtin_type("u64")) ||
        !signature->return_type->equals(*storage)) {
        log_error("Arena allocation bridge must be public with signature "
                  "(self: &GeneralArena, size: u64, alignment: u64) -> *u8");
        return;
    }
    arena_methods.emplace(method, method->name->value == "try_alloc");
}

std::shared_ptr<Type> Sema::check_arena_primitive(const CallExpression *call, ArenaPrimitive kind) {
    auto pointer = std::make_shared<PointerType>(get_builtin_type("u8"), true, false);
    std::vector<std::shared_ptr<Type>> parameters;
    if (kind != ArenaPrimitive::Create)
        parameters.push_back(pointer);
    if (kind == ArenaPrimitive::Allocate) {
        parameters.push_back(get_builtin_type("u64"));
        parameters.push_back(get_builtin_type("u64"));
    }
    if (call->arguments.size() != parameters.size()) {
        log_error("Incorrect number of arena primitive arguments");
        return nullptr;
    }
    for (size_t i = 0; i < parameters.size(); ++i) {
        auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
        if (!actual)
            return nullptr;
        if (!actual->equals(*parameters[i])) {
            log_error("Arena primitive argument type mismatch");
            return nullptr;
        }
    }
    recording->arena_runtime_calls.emplace(call, kind);
    return kind == ArenaPrimitive::Create || kind == ArenaPrimitive::Allocate
               ? std::shared_ptr<Type>(pointer)
               : get_builtin_type("void");
}
