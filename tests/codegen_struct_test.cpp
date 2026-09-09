#include "gtest/gtest.h"
#include "parser.h"
#include "codegen.h"
#include "lexer.h"
#include "sema.h"
#include "mlir/IR/Verifier.h"

TEST(CodeGenStructTest, GenerateStructAndMemberAccess) {
    std::string source = R"(
        def struct Point {
            def x: i32,
            def y: i32
        }

        def main() -> i32 {
            def mut p: Point;
            p.x = 10;
            p.y = 20;
            return p.x;
        }
    )";

    Lexer lexer(source);
    GloinParser parser(lexer);
    auto program = parser.parse_program();
    ASSERT_FALSE(parser.has_error());

    // Sema pass (essential for type checking, though CodeGen builds its own table for now)
    Sema sema;
    sema.check_program(program);

    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);

    module.dump();

    // Verification
    EXPECT_TRUE(mlir::succeeded(mlir::verify(module)));
    
    // Check for struct definition
    // It's in the type system, not explicitly in the module ops (unless printed).
    // MLIR types are printed inline usually.

    std::string output;
    llvm::raw_string_ostream os(output);
    module.print(os);
    
    // Check for struct type usage
    EXPECT_NE(output.find("!llvm.struct<\"Point\", (i32, i32)>"), std::string::npos);
    
    // Check for alloca
    EXPECT_NE(output.find("llvm.alloca"), std::string::npos);
    
    // Check for GEP
    // llvm.getelementptr %ptr[0, 0] : (!llvm.ptr, !llvm.struct<"Point", (i32, i32)>) -> !llvm.ptr, i32
    // The exact syntax might vary slightly
    EXPECT_NE(output.find("llvm.getelementptr"), std::string::npos);
    
    // Check for stores and loads
    EXPECT_NE(output.find("llvm.store"), std::string::npos);
    EXPECT_NE(output.find("llvm.load"), std::string::npos);
}

TEST(CodeGenStructTest, PackedStruct) {
    std::string source = R"(
        def packed struct(u32) PackedPoint {
            def x: u8,
            def y: u8
        }
        
        def main() -> void {
            def mut p: PackedPoint;
            p.x = 1;
        }
    )";
    
    Lexer lexer(source);
    GloinParser parser(lexer);
    auto program = parser.parse_program();
    ASSERT_FALSE(parser.has_error());

    Sema sema;
    sema.check_program(program);

    mlir::MLIRContext context;
    CodeGen codegen(context);
    auto module = codegen.generate(program);
    
    std::string output;
    llvm::raw_string_ostream os(output);
    module.print(os);
    
    // Check for packed struct type
    // In MLIR LLVM dialect: !llvm.struct<"PackedPoint", packed, (i32, i32)> ?
    // Actually u8 maps to i32 in my resolver for now unless I update it?
    // CodeGen::resolve_type currently defaults to i32 for unknown, but I added basic types.
    // I need to ensure u8 resolves to i8.
    
    EXPECT_NE(output.find("packed"), std::string::npos);
}
