
#include <gtest/gtest.h>
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/codegen.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

std::string compile_to_mlir_string_basic(const std::string& code) {
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

TEST(BasicCodeGenTest, HandlesBasicTypes) {
    std::string code = R"(
        def main() -> i32 {
            def x: i32 = 10;
            def y: bool = true;
            return x;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("i32") != std::string::npos);
    EXPECT_TRUE(mlir.find("true") != std::string::npos || mlir.find("1 : i1") != std::string::npos);
}

TEST(BasicCodeGenTest, HandlesArithmetic) {
    std::string code = R"(
        def main() -> i32 {
            def x: i32 = 10;
            def y: i32 = 20;
            return x + y;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("arith.addi") != std::string::npos);
}

TEST(BasicCodeGenTest, HandlesFloatTypes) {
    std::string code = R"(
        def main() -> i32 {
            def x: f32 = 3.14;
            return 0;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("f32") != std::string::npos);
    EXPECT_TRUE(mlir.find("3.14") != std::string::npos);
}

TEST(BasicCodeGenTest, HandlesControlFlow) {
    std::string code = R"(
        def main() -> i32 {
            def x: i32 = 10;
            if x > 5 {
                x = 100;
            } else {
                x = 0;
            }
            return x;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("cf.cond_br") != std::string::npos);
    EXPECT_TRUE(mlir.find("cf.br") != std::string::npos);
}

TEST(BasicCodeGenTest, HandlesWhileLoop) {
    std::string code = R"(
        def main() -> i32 {
            def mut x: i32 = 0;
            while x < 10 {
                x = x + 1;
            }
            return x;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("cf.cond_br") != std::string::npos);
    EXPECT_TRUE(mlir.find("cf.br") != std::string::npos);
}

TEST(BasicCodeGenTest, HandlesStructs) {
    std::string code = R"(
        def struct Point {
            def x: i32,
            def y: i32
        }

        def main() -> i32 {
            def mut p: Point = Point { x: 1, y: 2 };
            return p.x;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_basic(code);
    EXPECT_TRUE(mlir.find("llvm.struct<\"Point\", (i32, i32)>") != std::string::npos);
    EXPECT_TRUE(mlir.find("llvm.getelementptr") != std::string::npos);
}
