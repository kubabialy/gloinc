#include "ast_clone.h"
#include <stdexcept>
#include <utility>

namespace {
template <typename T, typename... Args>
std::unique_ptr<T> copied(const Node &source, Args &&...args) {
    auto node = std::make_unique<T>(std::forward<Args>(args)...);
    node->span = source.span;
    return node;
}

std::unique_ptr<Identifier> identifier(const Identifier *source) {
    return source ? copied<Identifier>(*source, source->value) : nullptr;
}

std::unique_ptr<Expression> expression(const Expression *source);
std::unique_ptr<Statement> statement(const Statement *source);

std::unique_ptr<BlockStatement> block(const BlockStatement *source) {
    if (!source)
        return nullptr;
    auto result = copied<BlockStatement>(*source);
    for (const auto &child : source->statements)
        result->statements.push_back(statement(child.get()));
    return result;
}

std::unique_ptr<Expression> expression(const Expression *source) {
    if (!source)
        return nullptr;
    if (auto *node = dynamic_cast<const Identifier *>(source))
        return identifier(node);
    if (auto *node = dynamic_cast<const IntegerLiteral *>(source))
        return copied<IntegerLiteral>(*node, node->literal);
    if (auto *node = dynamic_cast<const FloatLiteral *>(source))
        return copied<FloatLiteral>(*node, node->literal);
    if (auto *node = dynamic_cast<const BooleanLiteral *>(source))
        return copied<BooleanLiteral>(*node, node->value);
    if (auto *node = dynamic_cast<const NullLiteral *>(source))
        return copied<NullLiteral>(*node);
    if (auto *node = dynamic_cast<const ZeroedLiteral *>(source))
        return copied<ZeroedLiteral>(*node);
    if (auto *node = dynamic_cast<const StringLiteral *>(source))
        return copied<StringLiteral>(*node, node->value);
    if (auto *node = dynamic_cast<const ArrayLiteral *>(source)) {
        std::vector<std::unique_ptr<Expression>> elements;
        for (const auto &element : node->elements)
            elements.push_back(expression(element.get()));
        return copied<ArrayLiteral>(*node, std::move(elements), node->braced);
    }
    if (auto *node = dynamic_cast<const PrefixExpression *>(source))
        return copied<PrefixExpression>(*node, node->op, expression(node->right.get()));
    if (auto *node = dynamic_cast<const InfixExpression *>(source))
        return copied<InfixExpression>(*node, expression(node->left.get()), node->op,
                                       expression(node->right.get()));
    if (auto *node = dynamic_cast<const CallExpression *>(source)) {
        std::vector<std::unique_ptr<Expression>> arguments;
        for (const auto &argument : node->arguments)
            arguments.push_back(expression(argument.get()));
        return copied<CallExpression>(*node, expression(node->function.get()),
                                      std::move(arguments));
    }
    if (auto *node = dynamic_cast<const IndexExpression *>(source))
        return copied<IndexExpression>(*node, expression(node->left.get()),
                                       expression(node->index.get()));
    if (auto *node = dynamic_cast<const MemberAccessExpression *>(source))
        return copied<MemberAccessExpression>(*node, expression(node->left.get()),
                                              expression(node->member.get()));
    if (auto *node = dynamic_cast<const AssignmentExpression *>(source))
        return copied<AssignmentExpression>(*node, expression(node->left.get()),
                                            expression(node->right.get()));
    if (auto *node = dynamic_cast<const StructLiteral *>(source)) {
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        for (const auto &[name, value] : node->fields)
            fields.emplace_back(name, expression(value.get()));
        return copied<StructLiteral>(*node, identifier(node->name.get()), std::move(fields));
    }
    if (auto *node = dynamic_cast<const SpawnExpression *>(source))
        return copied<SpawnExpression>(*node, node->op, expression(node->call.get()));
    if (auto *node = dynamic_cast<const AwaitExpression *>(source))
        return copied<AwaitExpression>(*node, expression(node->expr.get()));
    throw std::logic_error("Unknown expression in generic method clone");
}

std::unique_ptr<Statement> statement(const Statement *source) {
    if (!source)
        return nullptr;
    if (auto *node = dynamic_cast<const BlockStatement *>(source))
        return block(node);
    if (auto *node = dynamic_cast<const ExpressionStatement *>(source))
        return copied<ExpressionStatement>(*node, expression(node->expression.get()));
    if (auto *node = dynamic_cast<const ReturnStatement *>(source))
        return copied<ReturnStatement>(*node, expression(node->return_value.get()));
    if (auto *node = dynamic_cast<const VariableDeclaration *>(source)) {
        auto result = copied<VariableDeclaration>(*node, node->is_mutable,
                                                  identifier(node->name.get()),
                                                  identifier(node->type.get()),
                                                  expression(node->initializer.get()));
        result->is_const = node->is_const;
        result->is_public = node->is_public;
        return result;
    }
    if (auto *node = dynamic_cast<const IfStatement *>(source))
        return copied<IfStatement>(*node, expression(node->condition.get()),
                                   block(node->consequence.get()), statement(node->alternative.get()));
    if (auto *node = dynamic_cast<const UnlessStatement *>(source))
        return copied<UnlessStatement>(*node, expression(node->condition.get()),
                                       block(node->consequence.get()));
    if (auto *node = dynamic_cast<const ForStatement *>(source))
        return copied<ForStatement>(*node, statement(node->init.get()),
                                    expression(node->condition.get()),
                                    expression(node->increment.get()), block(node->body.get()));
    if (auto *node = dynamic_cast<const WhileStatement *>(source))
        return copied<WhileStatement>(*node, expression(node->condition.get()), block(node->body.get()));
    if (auto *node = dynamic_cast<const DeferStatement *>(source))
        return copied<DeferStatement>(*node, expression(node->call.get()));
    throw std::logic_error("Unknown statement in generic method clone");
}
} // namespace

std::unique_ptr<FunctionDefinition> clone_function(const FunctionDefinition &function) {
    std::vector<Parameter> parameters;
    for (const auto &parameter : function.parameters)
        parameters.emplace_back(identifier(parameter.name.get()), identifier(parameter.type.get()));
    auto result = copied<FunctionDefinition>(function, identifier(function.name.get()),
                                             std::move(parameters),
                                             identifier(function.return_type.get()),
                                             block(function.body.get()), function.is_spawnable,
                                             function.is_deferred, function.generic_params);
    result->is_public = function.is_public;
    result->is_static = function.is_static;
    return result;
}
