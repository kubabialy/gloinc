#include <gtest/gtest.h>
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/sema.h"
#include "../src/codegen.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include <fstream>
#include <cstdio>
#include <array>
#include <iostream>

// Helper to run mlir-runner
int run_with_runner(mlir::ModuleOp module) {
    std::cout << "run_with_runner start\n";

    // Dump to file
    std::string filename = "temp.mlir";
    std::string mlirOutput;
    llvm::raw_string_ostream stream(mlirOutput);
    module.print(stream);
    
    std::ofstream out(filename);
    out << mlirOutput;
    out.close();
    std::cout << "Dumped to " << filename << "\n";
    
    // Construct command: mlir-opt ... | mlir-runner ...
    std::string opt_path = "/opt/homebrew/opt/llvm/bin/mlir-opt";
    std::string runner_path = "/opt/homebrew/opt/llvm/bin/mlir-runner";
    
    // Note: order matters.
    std::string opt_flags = "--convert-scf-to-cf --convert-cf-to-llvm --convert-arith-to-llvm --convert-func-to-llvm --finalize-memref-to-llvm --reconcile-unrealized-casts";
    
    // We assume the host is the target (JIT)
    // -shared-libs might be needed if we call external functions like print, but for simple i32 return it's fine.
    // For now, let's try basic lowering.
    
    std::string cmd = opt_path + " " + filename + " " + opt_flags + " | " + runner_path + " -e main -entry-point-result=i32";
    
    std::cout << "Running: " << cmd << "\n";
    
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        std::cerr << "popen() failed!\n";
        return -1;
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    std::cout << "Runner output: " << result << "\n";
    
    try {
        // Runner output format is "42 : i32" usually.
        // We parse the integer.
        return std::stoi(result);
    } catch (...) {
        return -1;
    }
}

int run_code(const std::string& code) {
    std::cout << "Parsing...\n" << std::flush;
    Lexer lexer(code);
    GloinParser parser(lexer);
    auto ast = parser.parse_program();
    
    // Sema checks 
    std::cout << "Sema...\n" << std::flush;
    Sema sema;
    sema.check_program(ast);
    if (sema.has_error()) {
        std::cerr << "Sema errors:\n";
        for (const auto& err : sema.get_errors()) std::cerr << err << "\n";
        return -1;
    }

    // Context
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::memref::MemRefDialect>();
    context.getOrLoadDialect<gloin::GloinDialect>();
    
    // Codegen
    CodeGen codegen(context);
    auto module = codegen.generate(ast);
    
    return run_with_runner(module);
}

TEST(E2ETest, ReturnInteger) {
    std::string code = R"(
        def main() -> i32 {
            return 42;
        }
    )";
    EXPECT_EQ(run_code(code), 42);
}

TEST(E2ETest, SimpleArithmetic) {
    std::string code = R"(
        def main() -> i32 {
            return 10 + 32;
        }
    )";
    EXPECT_EQ(run_code(code), 42);
}

TEST(E2ETest, VariableUsage) {
    std::string code = R"(
        def main() -> i32 {
            def x: i32 = 10;
            def y: i32 = 32;
            return x + y;
        }
    )";
    EXPECT_EQ(run_code(code), 42);
}

TEST(E2ETest, ControlFlowIf) {
    std::string code = R"(
        def main() -> i32 {
            if true {
                return 42;
            } else {
                return 0;
            }
        }
    )";
    EXPECT_EQ(run_code(code), 42);
}

TEST(E2ETest, ControlFlowLoop) {
    std::string code = R"(
        def main() -> i32 {
            def mut i: i32 = 0;
            def mut sum: i32 = 0;
            while i < 10 {
                sum = sum + 1;
                i = i + 1;
            }
            return sum;
        }
    )";
    EXPECT_EQ(run_code(code), 10);
}
