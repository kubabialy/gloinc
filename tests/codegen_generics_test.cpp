#include "codegen.h"
#include "lexer.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "parser.h"
#include "llvm/Support/raw_ostream.h"
#include <gtest/gtest.h>

std::string compile_to_mlir_string_generics(const std::string &code) {
    Lexer lexer(code);
    GloinParser parser(lexer);
    auto ast = parser.parse_program();

    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    context.getOrLoadDialect<gloin::GloinDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();

    CodeGen codegen(context);
    auto module = codegen.generate(ast);

    std::string output;
    llvm::raw_string_ostream os(output);
    module.print(os);
    return output;
}

TEST(CodeGenGenericsTest, InstantiatesGenericStruct) {
    std::string code = R"(
        def struct Box<T> {
            def value: T
        }

        def main() -> i32 {
            def b: Box<i32> = Box<i32> { value: 10 };
            return b.value;
        }
    )";

    std::string mlir = compile_to_mlir_string_generics(code);
    // Should contain instantiation for Box<i32>
    EXPECT_TRUE(mlir.find("llvm.struct<\"Box<i32>\", (i32)>") != std::string::npos);
}

TEST(CodeGenGenericsTest, InstantiatesMultipleSpecializations) {
    std::string code = R"(
        def struct Box<T> {
            def value: T
        }

        def main() -> i32 {
            def b1: Box<i32> = Box<i32> { value: 10 };
            def b2: Box<f32> = Box<f32> { value: 3.14 };
            return 0;
        }
    )";

    std::string mlir = compile_to_mlir_string_generics(code);
    EXPECT_TRUE(mlir.find("llvm.struct<\"Box<i32>\", (i32)>") != std::string::npos);
    EXPECT_TRUE(mlir.find("llvm.struct<\"Box<f32>\", (f32)>") != std::string::npos);
}

TEST(CodeGenGenericsTest, InstantiatesNestedGenerics) {
    std::string code = R"(
        def struct Box<T> {
            def value: T
        }

        def main() -> i32 {
            def nested: Box<Box<i32>> = Box<Box<i32>> { 
                value: Box<i32> { value: 42 } 
            };
            return nested.value.value;
        }
    )";

    std::string mlir = compile_to_mlir_string_generics(code);
    EXPECT_TRUE(mlir.find("llvm.struct<\"Box<Box<i32>>\", (struct<\"Box<i32>\", (i32)>)>") !=
                std::string::npos);
}

TEST(CodeGenGenericsTest, InstantiatesMultiParamGenerics) {
    std::string code = R"(
        def struct Pair<K, V> {
            def first: K
            def second: V
        }

        def main() -> i32 {
            def p: Pair<i32, f32> = Pair<i32, f32> { first: 1, second: 2.0 };
            return p.first;
        }
    )";

    std::string mlir = compile_to_mlir_string_generics(code);
    // Expected name might vary slightly depending on how I mangle names, checking for likely
    // structure
    EXPECT_TRUE(mlir.find("Pair<i32, f32>") != std::string::npos);
}

// TODO: Methods on generic structs are not yet fully implemented in CodeGen (needs specialization
// of methods) TEST(CodeGenGenericsTest, GenericMethods) { ... }
