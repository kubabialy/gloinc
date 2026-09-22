
#include <gtest/gtest.h>
#include "../src/compiler.h"
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/sema.h"
#include "../src/codegen.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

std::string compile_to_mlir_string_as(const std::string& code) {
    Lexer lexer(code);
    GloinParser parser(lexer, ParseMode::SyntaxOnly);
    auto ast = parser.parse_program();
    if (parser.has_error()) {
        std::ostringstream errors;
        parser.diagnostics()->render(errors);
        ADD_FAILURE() << errors.str();
        return {};
    }

    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    context.getOrLoadDialect<gloin::GloinDialect>();
    
    CodeGen codegen(context);
    mlir::OwningOpRef<mlir::ModuleOp> module(codegen.generate_unchecked_for_testing(ast));
    if (!module) {
        std::ostringstream errors;
        codegen.diagnostics()->render(errors);
        ADD_FAILURE() << errors.str();
        return {};
    }

    std::string output;
    llvm::raw_string_ostream os(output);
    module->print(os);
    return output;
}

TEST(ArrayStringTest, HandlesStringLiterals) {
    std::string code = R"(
        def main() -> i32 {
            def s: string = "Hello";
            return 0;
        }
    )";
    
    std::string mlir = compile_to_mlir_string_as(code);
    EXPECT_TRUE(mlir.find("llvm.mlir.global internal constant @str_") != std::string::npos);
    EXPECT_TRUE(mlir.find("Hello\\00") != std::string::npos);
    EXPECT_TRUE(mlir.find("llvm.insertvalue") != std::string::npos); // Building struct String
}

TEST(ArrayStringTest, DecodesEscapesPreservesBytesAndReusesGlobals) {
    std::string code = R"(
        def main() -> i32 {
            def first: string = "a\n\0é";
            def second: string = "a\n\0é";
            def empty: string = "";
            return 0;
        }
    )";

    std::string mlir = compile_to_mlir_string_as(code);
    ASSERT_FALSE(mlir.empty());
    EXPECT_NE(mlir.find("llvm.mlir.global internal constant @str_0"), std::string::npos);
    EXPECT_NE(mlir.find("@str_1"), std::string::npos); // distinct empty literal
    EXPECT_EQ(mlir.find("@str_2"), std::string::npos); // repeated first literal was reused
    EXPECT_NE(mlir.find("constant(5 : i64)"), std::string::npos); // a, LF, NUL, UTF-8 e-acute
    EXPECT_NE(mlir.find("llvm.array<1 x i8>"), std::string::npos);
    EXPECT_NE(mlir.find("constant(0 : i64)"), std::string::npos);
}

TEST(ArrayStringTest, CheckedStringSignaturesUseTheSameRepresentation) {
    mlir::MLIRContext context;
    auto result = compile_source(
        "def echo(value: string) -> string { return value; } "
        "def main() -> i32 { def copy: string = echo(\"hello\"); return 0; }",
        "string.gloin", context);
    ASSERT_TRUE(result.success());
    auto echo = result.module->lookupSymbol<mlir::func::FuncOp>("echo");
    ASSERT_TRUE(echo);
    EXPECT_EQ(echo.getFunctionType().getInput(0), echo.getFunctionType().getResult(0));
    EXPECT_TRUE(llvm::isa<mlir::LLVM::LLVMStructType>(echo.getFunctionType().getInput(0)));
}

TEST(ArrayStringTest, ArrayTypesRemainDeferred) {
    std::string code = R"(
        def main() -> i32 {
            def arr: [i32; 3] = [1, 2, 3];
            return 0;
        }
    )";
    
    GloinParser parser(Lexer(code), ParseMode::SyntaxOnly);
    auto parsed = parser.parse_checked_program();
    ASSERT_TRUE(parsed.success);
    Sema sema(parser.diagnostics());
    EXPECT_FALSE(sema.check_program(parsed.program));
}
