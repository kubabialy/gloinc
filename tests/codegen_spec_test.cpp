#include <gtest/gtest.h>
#include "../src/codegen.h"
#include "../src/parser.h"

TEST(CodeGenSpecTest, GenerateStringLiteral) {
    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def s: string = "Hello";
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("s"), 
        std::make_unique<Identifier>("string"), 
        std::make_unique<StringLiteral>("Hello")
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

TEST(CodeGenSpecTest, GeneratePointerOps) {
    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def mut x: i32 = 10;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        true, 
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(10, "10")
    ));

    // def ptr: *i32 = &x;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("ptr"), 
        std::make_unique<Identifier>("*i32"), 
        std::make_unique<PrefixExpression>("&", std::make_unique<Identifier>("x"))
    ));
    
    // def val: i32 = *ptr;
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("val"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<PrefixExpression>("*", std::make_unique<Identifier>("ptr"))
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
