#include "sema.h"

Sema::~Sema() {
    // Self-referential pointer fields form legitimate semantic type cycles.
    // CheckedProgram owns flat value identities, so these temporary graphs can be released.
    for (auto &structure : collected_struct_types)
        structure->fields.clear();
}

bool Sema::pointer_conversion(const PointerType &source, const PointerType &target) const {
    // Only the outermost capability can weaken. Covariance under another pointer
    // would let a writable alias replace a read-only or non-null pointer value.
    return source.pointee->equals(*target.pointee) && (!source.nullable || target.nullable) &&
           (!source.read_only || target.read_only);
}

std::shared_ptr<Type> Sema::check_typed_expression(const Expression *expression,
                                                   const std::shared_ptr<Type> &expected) {
    auto previous = expected_pointer;
    expected_pointer = std::dynamic_pointer_cast<PointerType>(expected);
    auto actual = check_expression(expression, expected ? resolve_core_type(expected->to_string())
                                                        : std::nullopt);
    auto pointer = std::dynamic_pointer_cast<PointerType>(actual);
    if (recording && pointer && expected_pointer &&
        pointer_conversion(*pointer, *expected_pointer)) {
        actual = expected_pointer;
        recording->types[expression] = *value_type(actual);
    }
    expected_pointer = previous;
    return actual;
}

Sema::Place Sema::check_place(const Expression *expression, bool take_address) {
    auto type = check_expression(expression);
    if (!type)
        return {};
    if (const auto *identifier = dynamic_cast<const Identifier *>(expression)) {
        auto *symbol = current_scope->resolve(identifier->value);
        if (!symbol ||
            (symbol->kind != SymbolKind::Variable && symbol->kind != SymbolKind::Parameter)) {
            log_error("Address requires runtime storage, not a function or constant");
            return {};
        }
        if (take_address)
            recording->address_taken.insert(symbol->id);
        return {type, symbol->is_mutable, true};
    }
    if (const auto *prefix = dynamic_cast<const PrefixExpression *>(expression);
        prefix && prefix->op == "*") {
        const auto pointer = recording->types.at(prefix->right.get());
        return {type, !pointer.pointers.front().read_only, true};
    }
    if (const auto *member = dynamic_cast<const MemberAccessExpression *>(expression)) {
        auto base_type = recording->types.at(member->left.get());
        Place base;
        if (recording->indirect_members.contains(member)) {
            base = {nullptr, !base_type.pointers.front().read_only, true};
            base_type = base_type.pointee();
        } else {
            base = check_place(member->left.get(), take_address);
        }
        const auto &field = recording->structures.at(*base_type.structure)
                                .fields.at(recording->field_indices.at(member));
        return {type, base.writable && field.is_mutable, base.addressable};
    }
    log_error(
        "Address or assignment target must be a variable, field, or dereference, not a temporary");
    return {};
}

std::shared_ptr<Type> Sema::check_pointer_unary(const PrefixExpression *expression) {
    if (expression->op == "&") {
        auto place = check_place(expression->right.get(), true);
        if (!place.type || !place.addressable)
            return nullptr;
        return std::make_shared<PointerType>(place.type, false, !place.writable);
    }
    auto type = check_expression(expression->right.get());
    const auto pointer = std::dynamic_pointer_cast<PointerType>(type);
    if (!pointer) {
        if (type)
            log_error("Dereference requires a pointer or reference");
        return nullptr;
    }
    return pointer->pointee;
}

std::shared_ptr<Type> Sema::check_indirect_assignment(const AssignmentExpression *assignment) {
    auto place = check_place(assignment->left.get());
    if (!place.type || !place.addressable)
        return nullptr;
    if (!place.writable)
        log_error("Cannot write through immutable storage or a read-only pointer; field assignment "
                  "requires a mutable local or writable pointer");
    auto right = check_typed_expression(assignment->right.get(), place.type);
    if (right && !right->equals(*place.type))
        log_error("Type mismatch in indirect assignment");
    return place.type;
}

std::shared_ptr<Type> Sema::check_pointer_comparison(const InfixExpression *expression) {
    if (expression->op != "==" && expression->op != "!=") {
        log_error(
            "Pointers support only == and !=; pointer arithmetic and ordering are unsupported");
        return nullptr;
    }
    auto left_hint =
        std::dynamic_pointer_cast<PointerType>(expression_type_hint(expression->left.get()));
    auto right_hint =
        std::dynamic_pointer_cast<PointerType>(expression_type_hint(expression->right.get()));
    auto hint = left_hint ? left_hint : right_hint;
    if (!hint) {
        log_error("Pointer comparison requires a typed pointer operand");
        return nullptr;
    }
    // Comparing identity does not require write capability or a non-null guarantee.
    auto common = std::make_shared<PointerType>(hint->pointee, true, true);
    auto left = check_typed_expression(expression->left.get(), common);
    auto right = check_typed_expression(expression->right.get(), common);
    if (!left || !right)
        return nullptr;
    if (!left->equals(*common) || !right->equals(*common)) {
        log_error("Pointer comparison requires identical pointee types");
        return nullptr;
    }
    return get_builtin_type("bool");
}
