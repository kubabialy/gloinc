#include "codegen.h"
#include "lexer.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "parser.h"
#include "llvm/Support/raw_ostream.h"
#include "support/cli_fixture.h"
#include <gtest/gtest.h>
#include <utility>

std::string compile_to_mlir_string_generics(const std::string &code) {
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
    context.getOrLoadDialect<mlir::arith::ArithDialect>();

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
            def first: K,
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

namespace {
class CheckedGenericsTest : public gloin_test::CliFixture {};
}

TEST_F(CheckedGenericsTest, NestedStructsArraysAliasesAndNativeExecution) {
    auto file = source(R"(
        def struct Box<T> { def mut value: T, }
        def struct Pair<A, B> { def first: A, def second: B, }
        def main() -> i32 {
            def mut x: Box<Box<i32>> = Box<Box<i32>> {
                value: Box<i32> { value: 40 }
            };
            def other: Box<int> = Box<i32> { value: 2 };
            def pair: Pair<[i32; 2], Box<i32>> =
                Pair<[i32; 2], Box<i32>> { first: {1, 2}, second: other };
            x.value.value = x.value.value + pair.second.value;
            return x.value.value;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    const auto executable = directory + "/generic-native";
    expect_success(invoke_raw({"-o", executable, file}), "");
    const auto out = directory + "/generic-native.stdout";
    const auto err = directory + "/generic-native.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 42) << message << read(err);
}

TEST_F(CheckedGenericsTest, RecursivePointerField) {
    auto file = source(R"(
        def struct Node<T> { def value: T, def next: *Node<T>, }
        def main() -> i32 {
            def node: Node<i32> = Node<i32> { value: 42, next: null };
            return node.value;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(CheckedGenericsTest, RejectsArityUnknownArgumentsAndByValueCycles) {
    for (const auto &[declaration, diagnostic] : {
             std::pair{"def x: Box<i32, i32> = Box<i32, i32> { value: 1 };",
                       "expects 1 type arguments"},
             std::pair{"def x: Box<Missing> = Box<Missing> { value: 1 };",
                       "Unknown or invalid generic type argument"},
             std::pair{"def x: Box = Box { value: 1 };",
                       "requires 1 type arguments"}}) {
        SCOPED_TRACE(declaration);
        auto file = source("def struct Box<T> { def value: T, } "
                           "def main() -> i32 { " + std::string(declaration) + " return 0; }");
        expect_error(invoke({"--check", file}), 1, diagnostic);
    }
    auto recursive = source("def struct Loop<T> { def inner: Loop<T>, } "
                            "def main() -> i32 { def x: Loop<i32>; return 0; }",
                            "recursive.gloin");
    expect_error(invoke({"--check", recursive}), 1, "Recursive by-value struct");
}

TEST_F(CheckedGenericsTest, ModuleQualifiedPublicTemplate) {
    source("def pub struct Box<T> { def pub value: T, }", "box.gloin");
    auto file = source("import \"./box\"; "
                       "def main() -> i32 { "
                       "def x: box.Box<i32> = box.Box<i32> { value: 42 }; "
                       "return x.value; }",
                       "main.gloin");
    expect_run(invoke({file}), 42);
}

TEST_F(CheckedGenericsTest, InvalidTemplateDeclarationsAndPrivateImports) {
    for (const auto &[declaration, diagnostic] : {
             std::pair{"def struct Pair<T, T> { def value: T, }",
                       "Duplicate or reserved generic parameter"},
             std::pair{"def struct Pair<i32> { def value: i32, }", "Expected identifier"},
             std::pair{"def struct Pair<T> { def value: T, def value: T, }",
                       "Duplicate field"},
             std::pair{"def struct Pair<T> { def get(self: *Pair<T>) -> T { return self.value; } }",
                       "Methods on generic structs"}}) {
        SCOPED_TRACE(declaration);
        auto file = source(std::string(declaration) + " def main() -> i32 { return 0; }");
        expect_error(invoke({"--check", file}), 1, diagnostic);
    }
    source("def struct Hidden<T> { def pub value: T, }", "hidden.gloin");
    auto file = source("import \"./hidden\"; "
                       "def main() -> i32 { "
                       "def x: hidden.Hidden<i32> = hidden.Hidden<i32> { value: 42 }; "
                       "return x.value; }",
                       "private-main.gloin");
    expect_error(invoke({"--check", file}), 1, "Unknown or inaccessible generic struct");

    auto function = source("def identity<T>(value: T) -> T { return value; } "
                           "def main() -> i32 { return 0; }",
                           "generic-function.gloin");
    expect_error(invoke({"--check", function}), 1,
                 "Generic functions await SPEC-033 specialization");
}
