
#include <gtest/gtest.h>
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/codegen.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

std::string compile_to_mlir_string_as(const std::string& code) {
    Lexer lexer(code);
    GloinParser parser(lexer);
    auto ast = parser.parse_program();
    
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    context.getOrLoadDialect<gloin::GloinDialect>();
    
    CodeGen codegen(context);
    auto module = codegen.generate(ast);
    
    std::string output;
    llvm::raw_string_ostream os(output);
    module.print(os);
    return output;
}

TEST(ArrayStringTest, HandlesStringLiterals) {
    std::string code = R"(
        def main() -> i32 {
            def s: String = "Hello";
            return 0;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_as(code);
    EXPECT_TRUE(mlir.find("llvm.mlir.global internal constant @str_") != std::string::npos);
    EXPECT_TRUE(mlir.find("Hello\\00") != std::string::npos);
    EXPECT_TRUE(mlir.find("llvm.insertvalue") != std::string::npos); // Building struct String
}

TEST(ArrayStringTest, HandlesArrayLiterals) {
    std::string code = R"(
        def main() -> i32 {
            def arr: [i32; 3] = [1, 2, 3];
            return 0;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_as(code);
    EXPECT_TRUE(mlir.find("llvm.array<3 x i32>") != std::string::npos);
    EXPECT_TRUE(mlir.find("llvm.insertvalue") != std::string::npos);
}
