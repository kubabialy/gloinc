#include <gtest/gtest.h>
#include "../src/sema.h"
#include "../src/parser.h" // For AST building helper if needed, or manual AST construction

TEST(SemaTest, UndefinedVariable) {
    Sema sema;
    
    // { x; }
    // x is undefined
    
    std::vector<std::unique_ptr<Statement>> program;
    auto stmt = std::make_unique<ExpressionStatement>(std::make_unique<Identifier>("x"));
    program.push_back(std::move(stmt));
    
    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_NE(output.find("Error: Undefined variable 'x'"), std::string::npos);
}

TEST(SemaTest, DefinedVariable) {
    Sema sema;
    
    // { def x: i32 = 10; x; }
    
    std::vector<std::unique_ptr<Statement>> program;
    
    // def x: i32 = 10;
    auto decl = std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(10, "10")
    );
    program.push_back(std::move(decl));
    
    // x;
    auto use = std::make_unique<ExpressionStatement>(std::make_unique<Identifier>("x"));
    program.push_back(std::move(use));
    
    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_EQ(output, ""); // No error expected
}

TEST(SemaTest, TypeMismatchInDeclaration) {
    Sema sema;
    
    // def x: i32 = "string";
    
    std::vector<std::unique_ptr<Statement>> program;
    program.push_back(std::make_unique<VariableDeclaration>(
        false, 
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<StringLiteral>("hello")
    ));
    
    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_NE(output.find("Error: Type mismatch in variable declaration"), std::string::npos);
}

TEST(SemaTest, ImmutableAssignment) {
    Sema sema;
    
    // def x: i32 = 10;
    // x = 20; // Error
    
    std::vector<std::unique_ptr<Statement>> program;
    program.push_back(std::make_unique<VariableDeclaration>(
        false, // Not mutable
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(10, "10")
    ));
    
    program.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<AssignmentExpression>(
            std::make_unique<Identifier>("x"),
            std::make_unique<IntegerLiteral>(20, "20")
        )
    ));
    
    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_NE(output.find("Error: Cannot assign to immutable variable 'x'"), std::string::npos);
}

TEST(SemaTest, MutableAssignment) {
    Sema sema;
    
    // def mut x: i32 = 10;
    // x = 20; // OK
    
    std::vector<std::unique_ptr<Statement>> program;
    program.push_back(std::make_unique<VariableDeclaration>(
        true, // Mutable
        std::make_unique<Identifier>("x"), 
        std::make_unique<Identifier>("i32"), 
        std::make_unique<IntegerLiteral>(10, "10")
    ));
    
    program.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<AssignmentExpression>(
            std::make_unique<Identifier>("x"),
            std::make_unique<IntegerLiteral>(20, "20")
        )
    ));
    
    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_EQ(output, ""); // No error
}

TEST(SemaTest, BuiltinTypes) {
    Sema sema;
    std::vector<std::string> types = {
        "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64",
        "f32", "f64",
        "bool", "string"
    };

    for (const auto& type_name : types) {
        // We verify that we can declare variables of these types without "Unknown type" error
        // def var_TYPE: TYPE = ...; 
        // We skip initializer checks by just checking if type resolution works during declaration processing.
        // Actually, if we don't provide initializer, the current sema implementation might error?
        // Let's check sema.cpp again: 
        // if (decl->initializer) { ... }
        // if (!var_type) { Error: Cannot infer type ... }
        
        // So we MUST provide initializer OR explicit type.
        // If explicit type is provided: var_type = resolve_type_from_string(...)
        // If !var_type -> Error: Unknown type.
        // Then if !var_type (which it won't be if resolved) -> Error: Cannot infer.
        
        // So providing explicit type is enough, UNLESS `var_type` is null.
        // So we can just provide explicit type and NO initializer?
        // Wait, line 88: if (!var_type) { Error: Cannot infer ... }
        // If we provide explicit type, var_type is set. So we pass that check.
        // So we don't need initializer.
        
        std::vector<std::unique_ptr<Statement>> program;
        program.push_back(std::make_unique<VariableDeclaration>(
            false,
            std::make_unique<Identifier>("var_" + type_name),
            std::make_unique<Identifier>(type_name),
            nullptr // No initializer
        ));

        testing::internal::CaptureStderr();
        sema.check_program(program);
        std::string output = testing::internal::GetCapturedStderr();
        
        EXPECT_EQ(output, "") << "Failed for type: " << type_name;
    }
}

TEST(SemaTest, StructDefinitionAndAccess) {
    Sema sema;
    std::vector<std::unique_ptr<Statement>> program;
    
    // def struct Point { def x: i32, def y: i32 }
    std::vector<StructField> fields;
    fields.emplace_back(true, std::make_unique<Identifier>("x"), std::make_unique<Identifier>("i32"));
    fields.emplace_back(true, std::make_unique<Identifier>("y"), std::make_unique<Identifier>("i32"));
    
    auto struct_def = std::make_unique<StructDefinition>(
        std::make_unique<Identifier>("Point"),
        std::move(fields),
        std::vector<std::unique_ptr<FunctionDefinition>>(),
        false
    );
    program.push_back(std::move(struct_def));
    
    // def p: Point;
    program.push_back(std::make_unique<VariableDeclaration>(
        false,
        std::make_unique<Identifier>("p"),
        std::make_unique<Identifier>("Point"),
        nullptr
    ));
    
    // p.x;
    program.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<MemberAccessExpression>(
            std::make_unique<Identifier>("p"),
            std::make_unique<Identifier>("x")
        )
    ));

    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_EQ(output, "");
}

TEST(SemaTest, InvalidStructAccess) {
    Sema sema;
    std::vector<std::unique_ptr<Statement>> program;
    
    // def struct Point { def x: i32 }
    std::vector<StructField> fields;
    fields.emplace_back(true, std::make_unique<Identifier>("x"), std::make_unique<Identifier>("i32"));
    
    program.push_back(std::make_unique<StructDefinition>(
        std::make_unique<Identifier>("Point"),
        std::move(fields),
        std::vector<std::unique_ptr<FunctionDefinition>>(),
        false
    ));
    
    // def p: Point;
    program.push_back(std::make_unique<VariableDeclaration>(
        false,
        std::make_unique<Identifier>("p"),
        std::make_unique<Identifier>("Point"),
        nullptr
    ));
    
    // p.z; // Error
    program.push_back(std::make_unique<ExpressionStatement>(
        std::make_unique<MemberAccessExpression>(
            std::make_unique<Identifier>("p"),
            std::make_unique<Identifier>("z")
        )
    ));

    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_NE(output.find("Error: Struct 'Point' has no field 'z'"), std::string::npos);
}
TEST(SemaTest, PackedStructBackingType) {
    Sema sema;
    std::vector<std::unique_ptr<Statement>> program;
    
    // def packed struct(u32) Header { def ver: u4 }
    std::vector<StructField> fields;
    fields.emplace_back(true, std::make_unique<Identifier>("ver"), std::make_unique<Identifier>("u4"));
    
    // Valid packed struct
    program.push_back(std::make_unique<StructDefinition>(
        std::make_unique<Identifier>("Header"),
        std::move(fields),
        std::vector<std::unique_ptr<FunctionDefinition>>(),
        true, // is_packed
        std::make_unique<Identifier>("u32") // backing type
    ));
    
    // Invalid packed struct (missing backing)
    std::vector<StructField> fields2;
    fields2.emplace_back(true, std::make_unique<Identifier>("ver"), std::make_unique<Identifier>("u4"));
    program.push_back(std::make_unique<StructDefinition>(
        std::make_unique<Identifier>("Header2"),
        std::move(fields2),
        std::vector<std::unique_ptr<FunctionDefinition>>(),
        true, // is_packed
        nullptr // missing backing
    ));

    testing::internal::CaptureStderr();
    sema.check_program(program);
    std::string output = testing::internal::GetCapturedStderr();
    
    EXPECT_NE(output.find("Error: Packed struct 'Header2' must specify a backing integer type"), std::string::npos);
}
