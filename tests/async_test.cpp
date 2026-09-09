#include <gtest/gtest.h>
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/sema.h"
#include "../src/codegen.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"

// Helper to parse and generate MLIR
std::string token_type_to_string(const GloinTokenType type); // Forward decl

std::string compile_to_mlir_string(const std::string& code) {
    Lexer lexer_debug(code);
    auto tokens = lexer_debug.tokenize();
    // for (const auto& t : tokens) {
    //    // print_debug_token(t); // Need to expose or reimplement
    //    std::cout << "Token: " << t.literal << " Type: " << token_type_to_string(t.type) << "\n";
    // }

    Lexer lexer(code);
    GloinParser parser(lexer);
    auto ast = parser.parse_program();
    if (parser.has_error()) {
        std::ostringstream errors;
        parser.diagnostics()->render(errors);
        ADD_FAILURE() << errors.str();
        return {};
    }

    // Sema checks (skip for simple codegen test or mock)
    // Sema sema;
    // sema.check_program(ast);

    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    context.getOrLoadDialect<gloin::GloinDialect>();
    
    CodeGen codegen(context);
    mlir::OwningOpRef<mlir::ModuleOp> module(codegen.generate(ast));
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

TEST(AsyncTest, DeferredFunctionGeneration) {
    std::string code = R"(
        def deferred fetch_data(id: i32) -> i32 {
            return id + 1;
        }
        
        def main() {
            let result = fetch_data(10);
        }
    )";
    
    std::string mlir = compile_to_mlir_string(code);
    
    // Check if function is marked deferred (attribute logic added to codegen)
    // Note: The textual IR might not show "is_deferred" unless we print generic op form or it's a discardable attr.
    // Ideally we check for gloin.async_call
    
    EXPECT_TRUE(mlir.find("gloin.async_call") != std::string::npos);
    EXPECT_TRUE(mlir.find("@fetch_data") != std::string::npos);
}

TEST(AsyncTest, SpawnGeneration) {
    std::string code = R"(
        def worker(x: i32) -> i32 {
            return x * 2;
        }
        
        def main() {
            let handle = spawn worker(42);
        }
    )";
    
    std::string mlir = compile_to_mlir_string(code);
    EXPECT_TRUE(mlir.find("gloin.spawn") != std::string::npos);
    EXPECT_TRUE(mlir.find("!gloin.spawn<i32>") != std::string::npos);
}
