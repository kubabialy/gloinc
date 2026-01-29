#ifndef GLOINC_AST_H
#define GLOINC_AST_H

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include "lexer.h"

// 1. Base Node
struct Node {
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

struct WhileStatement : public Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<BlockStatement> body;
    
    WhileStatement(std::unique_ptr<Expression> cond, std::unique_ptr<BlockStatement> body)
        : condition(std::move(cond)), body(std::move(body)) {}
        
    std::string to_string() const override {
        return "while " + condition->to_string() + " " + body->to_string();
    }
};

struct DeferStatement : public Statement {
    std::unique_ptr<Expression> call; // Usually a call expression
    
    explicit DeferStatement(std::unique_ptr<Expression> call) : call(std::move(call)) {}
    
    std::string to_string() const override {
        return "defer " + call->to_string() + ";";
    }
};

#endif // GLOINC_AST_H