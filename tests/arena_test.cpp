#include <gtest/gtest.h>
#include "../src/codegen.h"
#include "../src/parser.h"

TEST(ArenaTest, ArenaAllocation) {
    // def main() -> void {
    //     def arena: Arena = Arena::new();
    //     defer arena.free();
    //     def ptr: *i32 = arena.alloc(i32);
    //     return;
    // }

    std::vector<std::unique_ptr<Statement>> program;
    std::vector<Parameter> params;
    auto body = std::make_unique<BlockStatement>();
    
    // def arena: Arena = Arena::new();
    // Parsing "Arena::new()" as Call with Identifier "Arena::new"
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false,
        std::make_unique<Identifier>("arena"),
        std::make_unique<Identifier>("Arena"),
        std::make_unique<CallExpression>(
            std::make_unique<Identifier>("Arena::new"),
            std::vector<std::unique_ptr<Expression>>()
        )
    ));
    
    // defer arena.free();
    // Parsing "arena.free()" as Call with MemberAccess
    body->statements.push_back(std::make_unique<DeferStatement>(
        std::make_unique<CallExpression>(
            std::make_unique<MemberAccessExpression>(
                std::make_unique<Identifier>("arena"),
                std::make_unique<Identifier>("free")
            ),
            std::vector<std::unique_ptr<Expression>>()
        )
    ));
    
    // def ptr: *i32 = arena.alloc(i32);
    // Note: 'i32' passed as argument is strictly an IdentifierExpression in Parser
    std::vector<std::unique_ptr<Expression>> allocArgs;
    allocArgs.push_back(std::make_unique<Identifier>("i32"));
    
    body->statements.push_back(std::make_unique<VariableDeclaration>(
        false,
        std::make_unique<Identifier>("ptr"),
        std::make_unique<Identifier>("*i32"),
        std::make_unique<CallExpression>(
            std::make_unique<MemberAccessExpression>(
                std::make_unique<Identifier>("arena"),
                std::make_unique<Identifier>("alloc")
            ),
            std::move(allocArgs)
        )
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
