#include <gtest/gtest.h>
#include "../src/codegen.h"
#include "../src/parser.h"

TEST(CodeGenTest, GenerateComplexFunction) {
    // def main() -> i32 {
    //     def mut x: i32 = 10;
    //     if x < 20 {
    //         x = x + 1;
    //     }
    //     return x;
    // }

    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def mut x: i32 = 10;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        true, // mut
        std::make_unique<Identifier>("x"),
        std::make_unique<Identifier>("i32"),
        std::make_unique<IntegerLiteral>(10, "10")
    ));
    
    // if x < 20 { x = x + 1; }
    auto cond = std::make_unique<InfixExpression>(
        std::make_unique<Identifier>("x"),
        "<",
        std::make_unique<IntegerLiteral>(20, "20")
    );
    
    auto thenBlock = std::make_unique<BlockStatement>();
    thenBlock->statements.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<AssignmentExpression>(
            std::make_unique<Identifier>("x"),
            std::make_unique<InfixExpression>(
                std::make_unique<Identifier>("x"),
                "+",
                std::make_unique<IntegerLiteral>(1, "1")
            )
        )
    ));
    
    body->statements.push_back(std::make_unique<IfStatement>(
        std::move(cond),
        std::move(thenBlock)
    ));
    
    // return x;
    body->statements.push_back(std::make_unique<ReturnStatement>(std::make_unique<Identifier>("x")));
    
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("main"),
        std::move(params),
        std::make_unique<Identifier>("i32"),
        std::move(body)
    ));
    
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    ASSERT_TRUE(module != nullptr);
    module.dump();
}

TEST(CodeGenTest, GenerateWhileLoop) {
    // def main() -> void {
    //     def mut i: i32 = 0;
    //     while i < 10 {
    //         i = i + 1;
    //     }
    //     return;
    // }
    
    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def mut i: i32 = 0;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        true, 
        std::make_unique<Identifier>("i"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(0, "0")
    ));
    
    // while i < 10 { i = i + 1; }
    auto cond = std::make_unique<InfixExpression>(
        std::make_unique<Identifier>("i"), "<", std::make_unique<IntegerLiteral>(10, "10")
    );
    
    auto loopBody = std::make_unique<BlockStatement>();
    loopBody->statements.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<AssignmentExpression>(
            std::make_unique<Identifier>("i"),
            std::make_unique<InfixExpression>(
                std::make_unique<Identifier>("i"), "+", std::make_unique<IntegerLiteral>(1, "1")
            )
        )
    ));
    
    body->statements.push_back(std::make_unique<WhileStatement>(
        std::move(cond), std::move(loopBody)
    ));
    
    body->statements.push_back(std::make_unique<ReturnStatement>(nullptr));
    
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("main"),
        std::move(params),
        std::make_unique<Identifier>("void"),
        std::move(body)
    ));
    
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    ASSERT_TRUE(module != nullptr);
    module.dump();
}

TEST(CodeGenTest, GenerateImmutableVariable) {
    // def main() -> i32 {
    //     def x: i32 = 42;
    //     return x;
    // }
    
    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def x: i32 = 42;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(42, "42")
    ));
    
    body->statements.push_back(std::make_unique<ReturnStatement>(std::make_unique<Identifier>("x")));
    
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("main"),
        std::move(params),
        std::make_unique<Identifier>("i32"),
        std::move(body)
    ));
    
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    ASSERT_TRUE(module != nullptr);
    module.dump();
}

TEST(CodeGenTest, GenerateFunctionCall) {
    // def add(a: i32, b: i32) -> i32 { return a + b; }
    // def main() -> i32 { return add(1, 2); }
    
    std::vector<std::unique_ptr<Statement>> program;
    
    // def add(a: i32, b: i32) -> i32
    std::vector<Parameter> addParams;
    addParams.emplace_back(std::make_unique<Identifier>("a"), std::make_unique<Identifier>("i32"));
    addParams.emplace_back(std::make_unique<Identifier>("b"), std::make_unique<Identifier>("i32"));

    auto addBody = std::make_unique<BlockStatement>();
    addBody->statements.push_back(std::make_unique<ReturnStatement>(
        std::make_unique<InfixExpression>(
            std::make_unique<Identifier>("a"), "+", std::make_unique<Identifier>("b")
        )
    ));
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("add"),
        std::move(addParams),
        std::make_unique<Identifier>("i32"),
        std::move(addBody)
    ));
    
    // def main() -> i32
    std::vector<Parameter> mainParams;
    auto mainBody = std::make_unique<BlockStatement>();
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(std::make_unique<IntegerLiteral>(1, "1"));
    args.push_back(std::make_unique<IntegerLiteral>(2, "2"));
    
    mainBody->statements.push_back(std::make_unique<ReturnStatement>(
        std::make_unique<CallExpression>(
            std::make_unique<Identifier>("add"),
            std::move(args)
        )
    ));
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("main"),
        std::move(mainParams),
        std::make_unique<Identifier>("i32"),
        std::move(mainBody)
    ));
    
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    ASSERT_TRUE(module != nullptr);
    module.dump();
}

TEST(CodeGenTest, GenerateSpawn) {
    // def worker(id: i32) -> i32 { return id; }
    // def main() -> void {
    //     run worker(1);
    //     return;
    // }
    
    std::vector<std::unique_ptr<Statement>> program;
    
    // worker
    std::vector<Parameter> workerParams;
    workerParams.emplace_back(std::make_unique<Identifier>("id"), std::make_unique<Identifier>("i32"));
    auto workerBody = std::make_unique<BlockStatement>();
    workerBody->statements.push_back(std::make_unique<ReturnStatement>(std::make_unique<Identifier>("id")));
    
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("worker"),
        std::move(workerParams),
        std::make_unique<Identifier>("i32"),
        std::move(workerBody),
        true // is_spawnable
    ));
    
    // main
    std::vector<Parameter> mainParams;
    auto mainBody = std::make_unique<BlockStatement>();
    
    // run worker(1)
    std::vector<std::unique_ptr<Expression>> args;
    args.push_back(std::make_unique<IntegerLiteral>(1, "1"));
    auto call = std::make_unique<CallExpression>(std::make_unique<Identifier>("worker"), std::move(args));
    auto spawn = std::make_unique<SpawnExpression>(GLOIN_TOKEN_RUN, std::move(call));
    
    mainBody->statements.push_back(std::make_unique<ExpressionStatement>(std::move(spawn)));
    mainBody->statements.push_back(std::make_unique<ReturnStatement>(nullptr));
    
    program.push_back(std::make_unique<FunctionDefinition>(
        std::make_unique<Identifier>("main"),
        std::move(mainParams),
        std::make_unique<Identifier>("void"),
        std::move(mainBody)
    ));
    
    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    ASSERT_TRUE(module != nullptr);
    module.dump();
}
