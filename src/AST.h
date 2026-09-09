#ifndef GLOINC_AST_H
#define GLOINC_AST_H

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include "lexer.h"

// 1. Base Node
struct Node {
    SourceSpan span;
    virtual ~Node() = default;
    virtual std::string to_string() const = 0;
};

// 2. Expression Base and Subclasses
struct Expression : public Node {
    virtual ~Expression() = default;
};

struct IntegerLiteral : public Expression {
    int64_t value;
    std::string literal; // To keep the original text
    
    IntegerLiteral(int64_t v, std::string l) : value(v), literal(std::move(l)) {}
    
    std::string to_string() const override {
        return literal;
    }
};

struct FloatLiteral : public Expression {
    double value;
    std::string literal;
    
    FloatLiteral(double v, std::string l) : value(v), literal(std::move(l)) {}
    
    std::string to_string() const override {
        return literal;
    }
};

struct BooleanLiteral : public Expression {
    bool value;
    
    explicit BooleanLiteral(bool v) : value(v) {}
    
    std::string to_string() const override {
        return value ? "true" : "false";
    }
};

struct StringLiteral : public Expression {
    std::string value;
    
    explicit StringLiteral(std::string v) : value(std::move(v)) {}
    
    std::string to_string() const override {
        return "\"" + value + "\"";
    }
};

struct ArrayLiteral : public Expression {
    std::vector<std::unique_ptr<Expression>> elements;
    
    explicit ArrayLiteral(std::vector<std::unique_ptr<Expression>> elements) 
        : elements(std::move(elements)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < elements.size(); ++i) {
            ss << elements[i]->to_string();
            if (i < elements.size() - 1) ss << ", ";
        }
        ss << "]";
        return ss.str();
    }
};

struct Identifier : public Expression {
    std::string value;
    
    explicit Identifier(std::string v) : value(std::move(v)) {}
    
    std::string to_string() const override {
        return value;
    }
};

struct PrefixExpression : public Expression {
    std::string op;
    std::unique_ptr<Expression> right;
    
    PrefixExpression(std::string op, std::unique_ptr<Expression> right)
        : op(std::move(op)), right(std::move(right)) {}
        
    std::string to_string() const override {
        return "(" + op + right->to_string() + ")";
    }
};

struct InfixExpression : public Expression {
    std::unique_ptr<Expression> left;
    std::string op;
    std::unique_ptr<Expression> right;
    
    InfixExpression(std::unique_ptr<Expression> left, std::string op, std::unique_ptr<Expression> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}
        
    std::string to_string() const override {
        return "(" + left->to_string() + " " + op + " " + right->to_string() + ")";
    }
};

struct CallExpression : public Expression {
    std::unique_ptr<Expression> function; // Identifier or Function Literal
    std::vector<std::unique_ptr<Expression>> arguments;
    
    CallExpression(std::unique_ptr<Expression> func, std::vector<std::unique_ptr<Expression>> args)
        : function(std::move(func)), arguments(std::move(args)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << function->to_string() << "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            ss << arguments[i]->to_string();
            if (i < arguments.size() - 1) {
                ss << ", ";
            }
        }
        ss << ")";
        return ss.str();
    }
};

struct IndexExpression : public Expression {
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> index;
    
    IndexExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> index)
        : left(std::move(left)), index(std::move(index)) {}
        
    std::string to_string() const override {
        return "(" + left->to_string() + "[" + index->to_string() + "])";
    }
};

struct MemberAccessExpression : public Expression {
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> member; // Usually an Identifier
    
    MemberAccessExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> member)
        : left(std::move(left)), member(std::move(member)) {}
        
    std::string to_string() const override {
        return "(" + left->to_string() + "." + member->to_string() + ")";
    }
};

struct AssignmentExpression : public Expression {
    std::unique_ptr<Expression> left; // Usually an Identifier
    std::unique_ptr<Expression> right;
    
    AssignmentExpression(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left(std::move(left)), right(std::move(right)) {}
        
    std::string to_string() const override {
        return "(" + left->to_string() + " = " + right->to_string() + ")";
    }
};

struct StructLiteral : public Expression {
    std::unique_ptr<Identifier> name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    
    StructLiteral(std::unique_ptr<Identifier> name, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields)
        : name(std::move(name)), fields(std::move(fields)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << name->to_string() << " { ";
        for (const auto& field : fields) {
            ss << field.first << ": " << field.second->to_string() << ", ";
        }
        ss << "}";
        return ss.str();
    }
};


// 3. Statement Base and Subclasses
struct Statement : public Node {
    virtual ~Statement() = default;
};

struct ExpressionStatement : public Statement {
    std::unique_ptr<Expression> expression;
    
    explicit ExpressionStatement(std::unique_ptr<Expression> expr) 
        : expression(std::move(expr)) {}
        
    std::string to_string() const override {
        if (expression) {
            return expression->to_string() + ";";
        }
        return ";";
    }
};

struct BlockStatement : public Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    
    BlockStatement() = default;
    
    std::string to_string() const override {
        std::stringstream ss;
        ss << "{ ";
        for (const auto& stmt : statements) {
            ss << stmt->to_string() << " ";
        }
        ss << "}";
        return ss.str();
    }
};

struct ReturnStatement : public Statement {
    std::unique_ptr<Expression> return_value;
    
    explicit ReturnStatement(std::unique_ptr<Expression> value) 
        : return_value(std::move(value)) {}
        
    std::string to_string() const override {
        if (return_value) {
            return "return " + return_value->to_string() + ";";
        }
        return "return;";
    }
};

struct VariableDeclaration : public Statement {
    bool is_mutable;
    std::unique_ptr<Identifier> name;
    std::unique_ptr<Identifier> type; // Simplified type for now
    std::unique_ptr<Expression> initializer;
    
    VariableDeclaration(bool is_mut, std::unique_ptr<Identifier> n, std::unique_ptr<Identifier> t, std::unique_ptr<Expression> init)
        : is_mutable(is_mut), name(std::move(n)), type(std::move(t)), initializer(std::move(init)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << "def " << (is_mutable ? "mut " : "") << name->to_string();
        if (type) {
            ss << ": " << type->to_string();
        }
        if (initializer) {
            ss << " = " << initializer->to_string();
        }
        ss << ";";
        return ss.str();
    }
};

struct IfStatement : public Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<BlockStatement> consequence;
    std::unique_ptr<Statement> alternative; // BlockStatement or IfStatement (else if)
    
    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<BlockStatement> cons, std::unique_ptr<Statement> alt = nullptr)
        : condition(std::move(cond)), consequence(std::move(cons)), alternative(std::move(alt)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << "if " << condition->to_string() << " " << consequence->to_string();
        if (alternative) {
            ss << " else " << alternative->to_string();
        }
        return ss.str();
    }
};

struct UnlessStatement : public Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<BlockStatement> consequence;

    explicit UnlessStatement(std::unique_ptr<Expression> condition, std::unique_ptr<BlockStatement> consequence)
        : condition(std::move(condition)), consequence(std::move(consequence)) {}

    std::string to_string() const override {
        return "unless " + condition->to_string() + " " + consequence->to_string();
    }
};

struct ForStatement : public Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<BlockStatement> body;

    ForStatement(std::unique_ptr<Statement> init, std::unique_ptr<Expression> condition,
                 std::unique_ptr<Expression> increment, std::unique_ptr<BlockStatement> body)
        : init(std::move(init)), condition(std::move(condition)),
          increment(std::move(increment)), body(std::move(body)) {}

    std::string to_string() const override {
        std::stringstream ss;
        ss << "for " << init->to_string() << " " << condition->to_string() << "; " << increment->to_string() << " " << body->to_string();
        return ss.str();
    }
};

struct WhileStatement : public Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<BlockStatement> body;
    
    WhileStatement(std::unique_ptr<Expression> cond, std::unique_ptr<BlockStatement> body)
        : condition(std::move(cond)), body(std::move(body)) {}
        
    std::string to_string() const override {
        return "while " + condition->to_string() + " " + body->to_string();
    }
};

struct ImportStatement : public Statement {
    std::string path;

    explicit ImportStatement(std::string path) : path(std::move(path)) {}

    std::string to_string() const override {
        return "import " + path + ";";
    }
};

struct DeferStatement : public Statement {
    std::unique_ptr<Expression> call; // Usually a call expression
    
    explicit DeferStatement(std::unique_ptr<Expression> call) : call(std::move(call)) {}
    
    std::string to_string() const override {
        return "defer " + call->to_string() + ";";
    }
};

struct SpawnExpression : public Expression {
    GloinTokenType op;
    std::unique_ptr<Expression> call; // Should be a CallExpression
    
    explicit SpawnExpression(GloinTokenType op, std::unique_ptr<Expression> call) : op(op), call(std::move(call)) {}
    
    std::string to_string() const override {
        if (op == GLOIN_TOKEN_SPAWN) {
            return "spawn " + call->to_string();
        }
        return "run " + call->to_string();
    }
};

struct AwaitExpression : public Expression {
    std::unique_ptr<Expression> expr; 
    
    explicit AwaitExpression(std::unique_ptr<Expression> expr) : expr(std::move(expr)) {}
    
    std::string to_string() const override {
        return "await " + expr->to_string();
    }
};

struct Parameter {
    std::unique_ptr<Identifier> name;
    std::unique_ptr<Identifier> type;
    
    Parameter(std::unique_ptr<Identifier> n, std::unique_ptr<Identifier> t)
        : name(std::move(n)), type(std::move(t)) {}
};

struct FunctionDefinition : public Statement {
    std::unique_ptr<Identifier> name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Identifier> return_type;
    std::unique_ptr<BlockStatement> body;
    bool is_spawnable;
    bool is_deferred;
    std::vector<std::string> generic_params;

    FunctionDefinition(std::unique_ptr<Identifier> n, std::vector<Parameter> params,
                       std::unique_ptr<Identifier> ret, std::unique_ptr<BlockStatement> b,
                       bool spawn = false, bool defer = false,
                       std::vector<std::string> generics = {})
        : name(std::move(n)), parameters(std::move(params)), return_type(std::move(ret)),
          body(std::move(b)), is_spawnable(spawn), is_deferred(defer),
          generic_params(std::move(generics)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << "def " << (is_spawnable ? "spawnable " : "") << (is_deferred ? "deferred " : "") << name->to_string() << "(";
        for (size_t i = 0; i < parameters.size(); ++i) {
            ss << parameters[i].name->to_string() << ": " << parameters[i].type->to_string();
            if (i < parameters.size() - 1) ss << ", ";
        }
        ss << ") -> " << return_type->to_string() << " " << body->to_string();
        return ss.str();
    }
};

struct StructField {
    bool is_public;
    std::unique_ptr<Identifier> name;
    std::unique_ptr<Identifier> type;
    int offset; // For packed structs (bit offset)
    
    StructField(bool pub, std::unique_ptr<Identifier> n, std::unique_ptr<Identifier> t, int off = -1)
        : is_public(pub), name(std::move(n)), type(std::move(t)), offset(off) {}
};

struct StructDefinition : public Statement {
    std::unique_ptr<Identifier> name;
    std::vector<StructField> fields;
    std::vector<std::unique_ptr<FunctionDefinition>> methods;
    bool is_packed;
    std::vector<std::string> generic_params;
    std::unique_ptr<Identifier> backing_type; // For packed structs

    StructDefinition(std::unique_ptr<Identifier> n, std::vector<StructField> f,
                     std::vector<std::unique_ptr<FunctionDefinition>> m, bool packed = false,
                     std::unique_ptr<Identifier> backing = nullptr,
                     std::vector<std::string> generics = {})
        : name(std::move(n)), fields(std::move(f)), methods(std::move(m)), is_packed(packed),
          generic_params(std::move(generics)), backing_type(std::move(backing)) {}
        
    std::string to_string() const override {
        std::stringstream ss;
        ss << "def " << (is_packed ? "packed " : "") << "struct";
        if (backing_type) {
            ss << "(" << backing_type->to_string() << ")";
        }
        ss << " " << name->to_string() << " { ";
        for (const auto& field : fields) {
            ss << (field.is_public ? "pub " : "") << "def " << field.name->to_string() << ": " << field.type->to_string();
            if (field.offset != -1) {
                ss << " at " << field.offset;
            }
            ss << ", ";
        }
        for (const auto& method : methods) {
            ss << method->to_string() << " ";
        }
        ss << "}";
        return ss.str();
    }
};

#endif // GLOINC_AST_H
