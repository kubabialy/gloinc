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

TEST_F(CheckedGenericsTest, MethodsSpecializeForSeveralTypesAndNativeExecution) {
    auto file = source(R"(
        import "@strings";
        def struct Box<T> {
            def pub mut value: T,
            def pub static make(value: T) -> Box<T> {
                return Box<T> { value: value };
            }
            def pub get(self: &const Box<T>) -> T { return self.value; }
            def pub set(self: &Box<T>, value: T) -> void { self.value = value; }
            def pub copy(self: &const Box<T>) -> Box<T> {
                return Box<T> { value: self.get() };
            }
            def pub static fact(n: i32) -> i32 {
                if n <= 1 { return 1; }
                return n * Box<T>.fact(n - 1);
            }
        }
        def main() -> i32 {
            def mut number: Box<i32> = Box<i32>.make(40);
            number.set(42);
            def word: Box<string> = Box<string>.make("ok");
            def wrapped: Box<Box<i32>> = Box<Box<i32>>.make(number.copy());
            if strings.equal(word.get(), "ok") && wrapped.get().value == 42
                && Box<i32>.fact(5) == 120 {
                return number.get();
            }
            return 1;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    const auto executable = directory + "/generic-methods-native";
    expect_success(invoke_raw({"-O2", "-o", executable, file}), "");
    const auto out = directory + "/generic-methods-native.stdout";
    const auto err = directory + "/generic-methods-native.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 42) << message << read(err);
}

TEST_F(CheckedGenericsTest, ModulePublicMethodsUsePrivateFieldsAndHelpers) {
    source(R"(
        def pub struct Box<T> {
            def mut value: T,
            def pub static make(value: T) -> Box<T> { return Box<T> { value: value }; }
            def helper(self: &const Box<T>) -> T { return self.value; }
            def pub get(self: &const Box<T>) -> T { return self.helper(); }
            def pub set(self: &Box<T>, value: T) -> void { self.value = value; }
        }
    )",
           "container.gloin");
    auto file = source(R"(
        import "./container";
        def main() -> i32 {
            def mut box: container.Box<i32> = container.Box<i32>.make(40);
            box.set(42);
            return box.get();
        }
    )",
                       "main.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    auto private_method = source("import \"./container\"; "
                                 "def main() -> i32 { "
                                 "def box: container.Box<i32> = container.Box<i32>.make(42); "
                                 "return box.helper(); }",
                                 "private-method.gloin");
    expect_error(invoke({"--check", private_method}), 1, "Private method");
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
             std::pair{"def struct Pair<T> { def get<T>(self: *Pair<T>) -> T { return self.value; } }",
                       "Duplicate or reserved generic method parameter"}}) {
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

    auto bad_receiver = source("def struct Box<T> { def value: T, "
                               "def get(self: &i32) -> T { return self.value; } } "
                               "def main() -> i32 { def x: Box<i32> = Box<i32> { value: 42 }; "
                               "return x.get(); }",
                               "bad-receiver.gloin");
    expect_error(invoke({"--check", bad_receiver}), 1,
                 "Instance method requires first parameter self");

    auto bad_body = source("def struct Broken<T> { def value: T, "
                           "def bad(self: &const Broken<T>) -> T { return self.missing; } } "
                           "def main() -> i32 { "
                           "def value: Broken<i32> = Broken<i32> { value: 42 }; "
                           "return 0; }",
                           "bad-body.gloin");
    expect_error(invoke({"--check", bad_body}), 1, "Unknown field 'missing'");

    auto growing = source("def struct Grow<T> { "
                          "def next(self: &const Grow<T>) -> i32 { "
                          "def child: Grow<Grow<T>> = Grow<Grow<T>> {}; "
                          "return child.next(); } } "
                          "def main() -> i32 { "
                          "def root: Grow<i32> = Grow<i32> {}; return root.next(); }",
                          "growing.gloin");
    expect_error(invoke({"--check", growing}), 1,
                 "Generic struct specialization limit of 256 exceeded");
}

TEST_F(CheckedGenericsTest, GenericFunctionsSpecializeAndExecuteNatively) {
    auto file = source(R"(
        def struct Box<T> { def pub value: T, }
        def identity<T>(value: T) -> T { return value; }
        def duplicate<T>(value: T) -> T { return identity<T>(identity<T>(value)); }
        def fact<T>(n: T) -> T {
            if n <= 1 { return 1; }
            return n * fact<T>(n - 1);
        }
        def main() -> i32 {
            def numbers: [i32; 2] = duplicate<[i32; 2]>({40, 2});
            def box: Box<i32> = identity<Box<i32>>(Box<i32> { value: numbers[0] });
            if identity<bool>(true) && fact<i32>(3) == 6 {
                return duplicate<int>(box.value + numbers[1]);
            }
            return 1;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    const auto executable = directory + "/generic-functions-native";
    expect_success(invoke_raw({"-O2", "-o", executable, file}), "");
    const auto out = directory + "/generic-functions-native.stdout";
    const auto err = directory + "/generic-functions-native.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 42) << message << read(err);
}

TEST_F(CheckedGenericsTest, GenericFunctionsRespectModuleVisibilityAndDefinitionScope) {
    source(R"(
        def local(value: i32) -> i32 { return value; }
        def pub wrap<T>(value: T) -> T { return value; }
        def pub checked<T>(value: T) -> T { return wrap<T>(local(value)); }
        def hidden<T>(value: T) -> T { return value; }
    )", "utils.gloin");
    auto file = source(R"(
        import "./utils";
        def main() -> i32 { return utils.checked<i32>(utils.wrap<i32>(42)); }
    )", "main.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    auto private_call = source("import \"./utils\"; "
                               "def main() -> i32 { return utils.hidden<i32>(42); }",
                               "private-function.gloin");
    expect_error(invoke({"--check", private_call}), 1,
                 "Unknown or inaccessible generic function");
}

TEST_F(CheckedGenericsTest, GenericFunctionCallAndDeclarationDiagnostics) {
    for (const auto &[call, diagnostic] : {
             std::pair{"identity<i32, bool>(42)", "expects 1 type arguments"},
             std::pair{"identity<Missing>(42)", "Unknown or invalid generic type argument"},
             std::pair{"identity<void>(42)", "Unknown or invalid generic type argument"},
             std::pair{"identity<i32>(true)", "Argument 1 type mismatch"},
             std::pair{"identity<i32>()", "Incorrect number of arguments"}}) {
        SCOPED_TRACE(call);
        auto file = source("def identity<T>(value: T) -> T { return value; } "
                           "def main() -> i32 { return " + std::string(call) + "; }");
        expect_error(invoke({"--check", file}), 1, diagnostic);
    }
    auto duplicate = source("def value<T, T>(item: T) -> T { return item; } "
                            "def main() -> i32 { return 0; }");
    expect_error(invoke({"--check", duplicate}), 1, "Duplicate or reserved generic parameter");
    auto unused = source("def broken<T>(value: T) -> T { return value.missing; } "
                         "def main() -> i32 { return 42; }");
    expect_run(invoke({unused}), 42);
    auto used = source("def broken<T>(value: T) -> T { return value.missing; } "
                       "def main() -> i32 { return broken<i32>(42); }");
    expect_error(invoke({"--check", used}), 1, "Field access requires an ordinary struct value");

    auto missing_arguments = source("def identity<T>(value: T) -> T { return value; } "
                                    "def main() -> i32 { return identity(42); }");
    expect_error(invoke({"--check", missing_arguments}), 1,
                 "requires explicit type arguments");

    auto growing = source("def grow<T>(value: T) -> i32 { return grow<*T>(null); } "
                          "def main() -> i32 { return grow<i32>(42); }");
    expect_error(invoke({"--check", growing}), 1,
                 "Generic function specialization limit of 256 exceeded");
}

TEST_F(CheckedGenericsTest, GenericMethodsSpecializeForReceiverAndOwnTypes) {
    auto file = source(R"(
        def identity<T>(value: T) -> T { return value; }
        def struct Box<T> {
            def pub value: T,
            def pub static from<U>(marker: U, fallback: T) -> Box<T> {
                return Box<T> { value: fallback };
            }
            def pub choose<U>(self: &const Box<T>, candidate: U) -> U {
                return identity<U>(candidate);
            }
            def pub static count<U>(n: i32, marker: U) -> i32 {
                if n <= 0 { return 0; }
                return 1 + Box<T>.count<U>(n - 1, marker);
            }
        }
        def struct Tool {
            def pub static pass<U>(value: U) -> U { return value; }
        }
        def main() -> i32 {
            def box: Box<i32> = Box<i32>.from<bool>(true, 40);
            def other: Box<bool> = Box<bool>.from<i32>(7, true);
            if other.value && Box<i32>.count<bool>(2, true) == 2 {
                def first: i32 = box.choose<int>(Tool.pass<i32>(20));
                return first + identity<&const Box<i32>>(&box).choose<i32>(22);
            }
            return 1;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    const auto executable = directory + "/generic-method-arguments-native";
    expect_success(invoke_raw({"-O2", "-o", executable, file}), "");
    const auto out = directory + "/generic-method-arguments-native.stdout";
    const auto err = directory + "/generic-method-arguments-native.stderr";
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
    std::string message;
    bool launch_failed = false;
    const int code = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects,
                                                10, 0, &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(code, 42) << message << read(err);
}

TEST_F(CheckedGenericsTest, GenericMethodsHonorModuleVisibilityAndPrivateHelpers) {
    source(R"(
        def pub struct Box<T> {
            def pub value: T,
            def pub static make<U>(value: T, marker: U) -> Box<T> {
                return Box<T> { value: value };
            }
            def hidden<U>(self: &const Box<T>, alternative: U) -> U {
                return alternative;
            }
            def pub choose<U>(self: &const Box<T>, alternative: U) -> U {
                return self.hidden<U>(alternative);
            }
        }
    )", "container.gloin");
    auto file = source(R"(
        import "./container";
        def main() -> i32 {
            def box: container.Box<i32> = container.Box<i32>.make<bool>(40, true);
            return box.choose<i32>(42);
        }
    )", "main.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({file}), 42);
    auto private_call = source("import \"./container\"; "
                               "def main() -> i32 { "
                               "def box: container.Box<i32> = "
                               "container.Box<i32>.make<bool>(40, true); "
                               "return box.hidden<i32>(42); }",
                               "private-method.gloin");
    expect_error(invoke({"--check", private_call}), 1, "Private method");
}

TEST_F(CheckedGenericsTest, GenericMethodArgumentsAndBodiesAreChecked) {
    const std::string prelude =
        "def struct Box<T> { def value: T, "
        "def choose<U>(self: &const Box<T>, value: U) -> U { return value; } "
        "def get(self: &const Box<T>) -> T { return self.value; } } ";
    for (const auto &[call, diagnostic] : {
             std::pair{"box.choose(42)", "requires explicit type arguments"},
             std::pair{"box.choose<i32, bool>(42)", "expects 1 type arguments"},
             std::pair{"box.choose<Missing>(42)", "Unknown or invalid generic type argument"},
             std::pair{"box.choose<i32>(true)", "Method argument type mismatch"},
             std::pair{"box.get<i32>()", "cannot take type arguments"}}) {
        SCOPED_TRACE(call);
        auto file = source(prelude + "def main() -> i32 { "
                                   "def box: Box<i32> = Box<i32> { value: 40 }; "
                                   "return " + std::string(call) + "; }");
        expect_error(invoke({"--check", file}), 1, diagnostic);
    }
    auto duplicate = source("def struct Box<T> { def value: T, "
                            "def choose<U, U>(self: &Box<T>, value: U) -> U { return value; } } "
                            "def main() -> i32 { return 0; }");
    expect_error(invoke({"--check", duplicate}), 1,
                 "Duplicate or reserved generic method parameter");
    auto unused = source("def struct Box<T> { def value: T, "
                         "def broken<U>(self: &const Box<T>, value: U) -> U { "
                         "return self.missing; } } "
                         "def main() -> i32 { "
                         "def box: Box<i32> = Box<i32> { value: 42 }; return box.value; }");
    expect_run(invoke({unused}), 42);
    auto used = source("def struct Box<T> { def value: T, "
                       "def broken<U>(self: &const Box<T>, value: U) -> U { "
                       "return self.missing; } } "
                       "def main() -> i32 { "
                       "def box: Box<i32> = Box<i32> { value: 42 }; "
                       "return box.broken<i32>(0); }");
    expect_error(invoke({"--check", used}), 1, "Unknown field 'missing'");
    auto bad_receiver = source("def struct Broken { "
                               "def choose<U>(self: &i32, value: U) -> U { return value; } } "
                               "def main() -> i32 { def box: Broken = Broken {}; "
                               "return box.choose<i32>(42); }");
    expect_error(invoke({"--check", bad_receiver}), 1,
                 "Instance method requires first parameter self");
    auto growing = source("def struct Grow { "
                          "def pub static next<T>(value: T) -> i32 { "
                          "return Grow.next<*T>(null); } } "
                          "def main() -> i32 { return Grow.next<i32>(42); }");
    expect_error(invoke({"--check", growing}), 1,
                 "Generic method specialization limit of 256 exceeded");
}
