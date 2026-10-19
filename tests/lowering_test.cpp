#include "codegen.h"
#include "compiler.h"
#include "gir_float_checks.h"
#include "lowering.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "support/external_runner.h"
#include "tool_paths.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include <gtest/gtest.h>

namespace {
std::string render(const Diagnostics &diagnostics) {
    std::ostringstream text;
    diagnostics.render(text);
    return text.str();
}
std::string print(mlir::ModuleOp module) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream);
    return text;
}
void expect_exportable(mlir::ModuleOp module) {
    ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
    module.walk([](mlir::Operation *op) {
        EXPECT_TRUE(mlir::isa<mlir::ModuleOp>(op) || op->getName().getDialectNamespace() == "llvm")
            << op->getName().getStringRef().str();
    });
    // Translation registration belongs to the consumer, not the lowering API.
    mlir::registerBuiltinDialectTranslation(*module.getContext());
    mlir::registerLLVMDialectTranslation(*module.getContext());
    llvm::LLVMContext context;
    auto exported = mlir::translateModuleToLLVMIR(module, context);
    ASSERT_NE(exported, nullptr);
    EXPECT_FALSE(llvm::verifyModule(*exported, &llvm::errs()));
}
void execute_lowered(mlir::ModuleOp module, int expected) {
    ASSERT_NO_FATAL_FAILURE(expect_exportable(module));
    auto value = gloin_test::run_external_mlir(print(module), {gloin_test::mlir_opt, {}},
                                               {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(value)) << llvm::toString(value.takeError());
    EXPECT_EQ(*value, expected);
}
mlir::OwningOpRef<mlir::ModuleOp> parse(mlir::MLIRContext &context, llvm::StringRef text) {
    // Match compiler dialect loading, without bypassing source checking in normal compilation.
    CodeGen load_dialects(context);
    return mlir::parseSourceString<mlir::ModuleOp>(text, &context);
}
} // namespace

TEST(LoweringTest, CompilerOffersVerifiedHighLevelAndLLVMOutput) {
    mlir::MLIRContext context;
    auto high = compile_source("def main() -> i32 { return 42; }", "answer.gloin", context);
    ASSERT_TRUE(high.success());
    EXPECT_TRUE(high.module->lookupSymbol<mlir::func::FuncOp>("main"));
    auto low = compile_source("def main() -> i32 { return 42; }", "answer.gloin", context,
                              CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(low.success()) << render(*low.diagnostics);
    EXPECT_TRUE(low.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main"));
    execute_lowered(*low.module, 42);
}

TEST(LoweringTest, ExactIntegerWidenVerifiesWidthsAndLowersSignedness) {
    const auto ir = [](const std::string &input, const std::string &output,
                       const std::string &signedness) {
        return "module { func.func @widen(%x: " + input + ") -> " + output +
            " { %r = \"gloin.exact_int_widen\"(%x) <{is_signed = " + signedness +
            "}> : (" + input + ") -> " + output + " return %r : " + output + " } }";
    };
    for (unsigned from : {8, 16, 32}) {
        for (unsigned to : {16, 32, 64}) {
            if (from >= to) continue;
            for (bool is_signed : {false, true}) {
                mlir::MLIRContext context;
                auto high = parse(context, ir("i" + std::to_string(from),
                    "i" + std::to_string(to), is_signed ? "true" : "false"));
                ASSERT_TRUE(high);
                Diagnostics diagnostics;
                auto low = lower_to_llvm(std::move(high), diagnostics);
                ASSERT_TRUE(low) << render(diagnostics);
                expect_exportable(*low);
                unsigned extensions = 0;
                low->walk([&](mlir::Operation *op) {
                    if (!mlir::isa<mlir::LLVM::SExtOp, mlir::LLVM::ZExtOp>(op)) return;
                    ++extensions;
                    EXPECT_EQ(mlir::isa<mlir::LLVM::SExtOp>(op), is_signed);
                    EXPECT_TRUE(op->getOperand(0).getType().isInteger(from));
                    EXPECT_TRUE(op->getResult(0).getType().isInteger(to));
                });
                EXPECT_EQ(extensions, 1u);
                EXPECT_EQ(print(*low).find("llvm.call"), std::string::npos);
                EXPECT_EQ(print(*low).find("llvm.intr.trap"), std::string::npos);
            }
        }
    }
    mlir::MLIRContext context;
    mlir::ScopedDiagnosticHandler silence(&context, [](mlir::Diagnostic &) { return mlir::success(); });
    for (const auto &[input, output] :
         {std::pair{"i32", "i32"}, {"i64", "i32"}, {"i1", "i64"}, {"i7", "i32"},
          {"i32", "i128"}, {"f32", "i64"}, {"i32", "f64"}, {"si32", "i64"},
          {"i32", "ui64"}, {"vector<2xi32>", "vector<2xi64>"}}) {
        SCOPED_TRACE(std::string(input) + " -> " + output);
        EXPECT_FALSE(parse(context, ir(input, output, "true")));
    }
    EXPECT_FALSE(parse(context, ir("i32", "i64", "1 : i32")));
}

TEST(LoweringTest, CheckedIntegerArithmeticUsesSourceWidthOverflowIntrinsics) {
    for (const auto &info : core_types) {
        if (!info.is_integer)
            continue;
        const std::string type(info.name);
        for (const auto &[symbol, intrinsic] :
             {std::pair{"+", "add"}, std::pair{"-", "sub"}, std::pair{"*", "mul"}}) {
            SCOPED_TRACE(type + " " + symbol);
            mlir::MLIRContext context;
            auto high = compile_source("def calculate(a: " + type + ", b: " + type +
                                           ") -> " + type + " { return a " + symbol + " b; }",
                                       "checked-arithmetic.gloin", context);
            ASSERT_TRUE(high.success()) << render(*high.diagnostics);
            unsigned checked = 0;
            high.module->walk([&](gloin::CheckedIntegerBinaryOp) { ++checked; });
            EXPECT_EQ(checked, 1u);
            Diagnostics diagnostics;
            auto low = lower_to_llvm(std::move(high.module), diagnostics);
            ASSERT_TRUE(low) << render(diagnostics);
            expect_exportable(*low);
            const auto text = print(*low);
            const std::string name = std::string("llvm.intr.") +
                                     (info.is_signed ? "s" : "u") + intrinsic +
                                     ".with.overflow";
            unsigned calls = 0;
            low->walk([&](mlir::Operation *op) {
                if (op->getName().getStringRef() != name) return;
                ++calls;
                EXPECT_TRUE(op->getOperand(0).getType().isInteger(info.bits));
                EXPECT_TRUE(op->getOperand(1).getType().isInteger(info.bits));
            });
            EXPECT_EQ(calls, 1u) << text;
            EXPECT_NE(text.find("llvm.intr.trap"), std::string::npos);
            EXPECT_EQ(text.find("llvm.sext"), std::string::npos);
            EXPECT_EQ(text.find("llvm.zext"), std::string::npos);
            EXPECT_EQ(text.find("llvm.trunc"), std::string::npos);
        }
    }
}

TEST(LoweringTest, GIRProvesIntegerConstantsAndIdentitiesBeforeOverflowLowering) {
    for (const auto &info : core_types) {
        if (!info.is_integer) continue;
        const std::string type(info.name);
        SCOPED_TRACE(type);
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def constants() -> " + type + " { return (14 * 3) + (6 / 3 - 2) + 7 % 7; }"
            "def identity(a: " + type + ") -> " + type + " { return (0 + a + 0 - 0) * 1 / 1; }"
            "def zero(a: " + type + ") -> " + type + " { return a * 0 + 0 * a + a % 1; }",
            "integer-folding.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        expect_exportable(*compiled.module);
        const auto text = print(*compiled.module);
        EXPECT_EQ(text.find("with.overflow"), std::string::npos) << text;
        EXPECT_EQ(text.find("llvm.intr.trap"), std::string::npos) << text;
        EXPECT_NE(text.find("42 : i" + std::to_string(info.bits)), std::string::npos) << text;
    }
    mlir::MLIRContext context;
    auto effects = compile_source(R"(
        def next(counter: &i32, digit: i32) -> i32 {
            *counter = *counter * 10 + digit;
            return *counter;
        }
        def main() -> i32 {
            def mut counter: i32 = 0;
            def value: i32 = next(&counter, 1) * 0 + 0 * next(&counter, 2) + next(&counter, 3) % 1;
            if counter == 123 && value == 0 { return 42; }
            return 1;
        }
    )", "integer-folding-effects.gloin", context, CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(effects.success()) << render(*effects.diagnostics);
    execute_lowered(*effects.module, 42);
}

TEST(LoweringTest, GIRRangeProofsUseUnsignedGuardsAndExactSliceSnapshots) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def indexed(source: [const u8], i: u64) -> u8 {
            if i >= source.len { return 0; }
            return source[i];
        }
        def lookup(source: [const u8], table: [const u8]) -> u8 {
            def four: u64 = 4;
            def entries: u64 = 256;
            if source.len < four { return 0; }
            if table.len < entries { return 0; }
            return table[source[3]];
        }
        def byte_math(a: u8, b: u8) -> u8 {
            if a >= 64 { return 0; }
            if b >= 64 { return 0; }
            return a * 4 + b / 16;
        }
        def groups(source: [const u8]) -> u8 {
            def mut i: u64 = 0;
            def mut last: u8 = 0;
            while i + 4 <= source.len {
                last = source[i + 3];
                i = i + 4;
            }
            return last;
        }
        def fixed(a: u8) -> u8 {
            def values: [u8; 4] = {10, 20, 30, 40};
            return values[a % 4];
        }
    )", "range-proofs.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    expect_exportable(*compiled.module);
    for (const auto *name : {"indexed", "lookup", "byte_math", "groups", "fixed"}) {
        SCOPED_TRACE(name);
        auto function = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
        ASSERT_TRUE(function);
        unsigned traps = 0;
        function.walk([&](mlir::LLVM::Trap) { ++traps; });
        // The header's i+4 still needs an overflow guard. A successful header
        // proves both the body address and the repeated increment safe.
        EXPECT_EQ(traps, std::string(name) == "groups" ? 1u : 0u) << print(*compiled.module);
        if (std::string(name) == "groups") {
            unsigned additions = 0;
            function.walk([&](mlir::LLVM::UAddWithOverflowOp) { ++additions; });
            EXPECT_EQ(additions, 1u) << print(*compiled.module);
        }
    }
}

TEST(LoweringTest, GIRRangeProofsKeepGuardsAfterMutationJoinsAndSignedIndices) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def change(index: &u64, value: u64) -> void { *index = value; }
        def alias(source: [const u8], initial: u64) -> u8 {
            def mut i: u64 = initial;
            if i >= source.len { return 0; }
            change(&i, source.len);
            return source[i];
        }
        def join(source: [const u8], initial: u64, replace: bool) -> u8 {
            def mut i: u64 = initial;
            if i >= source.len { return 0; }
            if replace { i = source.len; }
            return source[i];
        }
        def descriptor(initial: [const u8]) -> u8 {
            def mut source: [const u8] = initial;
            def four: u64 = 4;
            if source.len < four { return 0; }
            source = source[..1];
            return source[3];
        }
        def reload(source: [u8]) -> u8 {
            def one: u64 = 1;
            def limit: u8 = 64;
            if source.len < one { return 0; }
            if source[0] >= limit { return 0; }
            source[0] = 255;
            return source[0] * 4;
        }
        def signed(source: [const u8], index: i64) -> u8 { return source[index]; }
        def sibling(source: [const u8], index: u64, check: bool) -> u8 {
            if check { if index >= source.len { return 0; } }
            return source[index];
        }
        def insufficient(source: [const u8], i: u64) -> u8 {
            if i + 4 <= source.len { return source[i + 4]; }
            return 0;
        }
    )", "range-proof-invalidations.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    expect_exportable(*compiled.module);
    for (const auto *name : {"alias", "join", "descriptor", "reload", "signed", "sibling", "insufficient"}) {
        SCOPED_TRACE(name);
        auto function = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
        ASSERT_TRUE(function);
        unsigned traps = 0;
        function.walk([&](mlir::LLVM::Trap) { ++traps; });
        EXPECT_GT(traps, 0u) << print(*compiled.module);
    }
}

TEST(LoweringTest, GIRRangeProofsRespectUnsignedWidthBoundaries) {
    for (const auto &info : core_types) {
        if (!info.is_integer || info.is_signed) continue;
        const std::string type(info.name);
        SCOPED_TRACE(type);
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def safe(a: " + type + ", b: " + type + ") -> " + type +
            " { if a > 15 { return 0; } if b > 15 { return 0; } return a * 16 + b; }"
            "def unsafe(a: " + type + ") -> " + type + " { return a + 1; }",
            "range-proof-width.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        expect_exportable(*compiled.module);
        unsigned safe_traps = 0, unsafe_traps = 0;
        compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("safe").walk(
            [&](mlir::LLVM::Trap) { ++safe_traps; });
        compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("unsafe").walk(
            [&](mlir::LLVM::Trap) { ++unsafe_traps; });
        EXPECT_EQ(safe_traps, 0u);
        EXPECT_EQ(unsafe_traps, 1u);
    }
}

TEST(LoweringTest, ExecutableReachabilityKeepsRecursionMethodsAndGenericInstances) {
    const std::string source = R"(
        def identity<T>(value: T) -> T { return value; }
        def unused_generic() -> i64 { return identity<i64>(99); }
        def even(value: i32) -> i32 {
            if value == 0 { return 1; }
            return odd(value - 1);
        }
        def odd(value: i32) -> i32 {
            if value == 0 { return 0; }
            return even(value - 1);
        }
        def dead_a() -> i32 { return dead_b(); }
        def dead_b() -> i32 { return dead_a(); }
        def pub unused_public() -> i32 { return 99; }
        def struct Box {
            def value: i32,
            def read(self: &const Box) -> i32 { return identity<i32>(self.value); }
            def unused_method(self: &const Box) -> i32 { return self.value + 9; }
        }
        def main() -> i32 {
            def box: Box = Box { value: 41 };
            return box.read() + even(4);
        }
    )";
    for (auto mode : {CompilationMode::Module, CompilationMode::Object,
                       CompilationMode::Executable}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(source, "reachability.gloin", context, mode,
                                         CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        const bool preserve = mode != CompilationMode::Executable;
        for (const char *name : {"unused_generic", "dead_a", "dead_b", "unused_public"})
            EXPECT_EQ(bool(compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>(name)), preserve);
        EXPECT_TRUE(compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("even"));
        EXPECT_TRUE(compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>("odd"));
        unsigned definitions = 0;
        for (auto function : compiled.module->getOps<mlir::LLVM::LLVMFuncOp>())
            definitions += !function.isExternal();
        if (preserve) EXPECT_GT(definitions, 5u);
        else EXPECT_EQ(definitions, 5u); // main, even, odd, read, identity<i32>
        execute_lowered(*compiled.module, 42);
    }
}

TEST(LoweringTest, ReachabilityKeepsSymbolReferencesOutsideFunctionBodies) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def retained() -> i32 { return 7; } def dead() -> i32 { return 9; } "
        "def main() -> i32 { return 42; }", "symbol-root.gloin", context);
    ASSERT_TRUE(compiled.success());
    // Conservatively retain symbol references in module metadata as well as
    // calls. This exercises the root rule without adding source FFI syntax.
    (*compiled.module)->setAttr("gloin.retained_symbol",
                             mlir::FlatSymbolRefAttr::get(&context, "retained"));
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(compiled.module), diagnostics, {}, nullptr,
                                  CompilationMode::Executable);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_TRUE(lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("retained"));
    EXPECT_FALSE(lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("dead"));
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, ReachabilityDoesNotHideInvalidUnusedIR) {
    for (bool verification_error : {false, true}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def unused() -> i32 { return 9; } def main() -> i32 { return 42; }",
            "invalid-unused.gloin", context);
        ASSERT_TRUE(compiled.success());
        auto unused = compiled.module->lookupSymbol<mlir::func::FuncOp>("unused");
        if (verification_error) {
            unused.front().getTerminator()->setOperands(mlir::ValueRange{});
        } else {
            mlir::OpBuilder builder(&context);
            builder.setInsertionPointToStart(&unused.front());
            auto unsupported = builder.create<gloin::DeferOp>(builder.getUnknownLoc());
            builder.setInsertionPointToStart(&unsupported.getBodyRegion().emplaceBlock());
            builder.create<gloin::YieldOp>(builder.getUnknownLoc(), mlir::ValueRange{});
        }
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(compiled.module), diagnostics, {}, nullptr,
                                    CompilationMode::Executable));
        ASSERT_TRUE(diagnostics.has_errors());
        EXPECT_EQ(diagnostics.all().back().stage, verification_error
            ? DiagnosticStage::Verification : DiagnosticStage::Lowering);
    }
}

TEST(LoweringTest, ReachabilityFollowsFunctionValuesForIndirectCalls) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(module attributes {gloin.checked} {
        func.func @callback() -> i32 { %c = arith.constant 42 : i32 return %c : i32 }
        func.func @unused() -> i32 { %c = arith.constant 9 : i32 return %c : i32 }
        func.func @main() -> i32 {
            %f = func.constant @callback : () -> i32
            %answer = func.call_indirect %f() : () -> i32
            return %answer : i32
        }
    })");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics, {}, nullptr,
                                  CompilationMode::Executable);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_TRUE(lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("callback"));
    EXPECT_FALSE(lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("unused"));
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, ExecutableReachabilityPreservesArbitraryUnmarkedIR) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(module {
        func.func @unused() -> i32 { %c = arith.constant 9 : i32 return %c : i32 }
        func.func @main() -> i32 { %c = arith.constant 42 : i32 return %c : i32 }
    })");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics, {}, nullptr,
                                  CompilationMode::Executable);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_TRUE(lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("unused"));
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, CheckedFunctionSignaturesRetainNominalTypesUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def enum First { Yes }
        def enum Second { Yes }
        def choose(value: First) -> First { return value; }
        def main() -> i32 {
            def mut state: First = First.Yes;
            def value: First = choose(state);
            if value == First.Yes { return 42; }
            return 1;
        }
    )", "typed_calls.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    auto function = high.module->lookupSymbol<mlir::func::FuncOp>("choose");
    ASSERT_TRUE(function);
    EXPECT_TRUE(mlir::isa<gloin::GloinEnumType>(function.getFunctionType().getInput(0)));
    EXPECT_EQ(function.getFunctionType().getInput(0),
              function.getFunctionType().getResult(0));
    auto high_text = print(*high.module);
    EXPECT_NE(high_text.find("gloin.to_layout"), std::string::npos);
    Diagnostics diagnostics;
    auto low = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(low) << render(diagnostics);
    EXPECT_EQ(print(*low).find("!gloin."), std::string::npos);
    execute_lowered(*low, 42);
}

TEST(LoweringTest, TypedCallVerifierRejectsWrongNominalArgument) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def enum First { Yes }
        def enum Second { Yes }
        def consume(value: First) -> i32 { return 42; }
        def echo(value: Second) -> Second { return value; }
        def main() -> i32 {
            def other: Second = echo(Second.Yes);
            return consume(First.Yes);
        }
    )", "typed_mismatch.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    mlir::Value wrong;
    mlir::func::CallOp call;
    high.module->walk([&](mlir::func::CallOp op) {
        if (op.getCallee() == "echo")
            wrong = op.getResult(0);
        if (op.getCallee() == "consume")
            call = op;
    });
    ASSERT_TRUE(wrong);
    ASSERT_TRUE(call);
    call->setOperand(0, wrong);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(LoweringTest, EnumOperationsCarryNominalSSAValues) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def enum First { Yes }
        def enum Second { Yes }
        def main() -> i32 {
            def other: Second = Second.Yes;
            if First.Yes == First.Yes { return 42; }
            return 1;
        }
    )", "enum_values.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    mlir::Value second;
    gloin::EnumCompareOp comparison;
    high.module->walk([&](gloin::EnumConstantOp constant) {
        EXPECT_EQ(constant.getValue().getType(), constant.getSourceType());
        if (mlir::cast<gloin::GloinEnumType>(constant.getSourceType())
                .getName().starts_with("Second#"))
            second = constant.getValue();
    });
    high.module->walk([&](gloin::EnumCompareOp candidate) {
        comparison = candidate;
        EXPECT_EQ(candidate.getLhs().getType(), candidate.getSourceType());
        EXPECT_EQ(candidate.getRhs().getType(), candidate.getSourceType());
    });
    ASSERT_TRUE(second);
    ASSERT_TRUE(comparison);
    comparison->setOperand(1, second);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(LoweringTest, CheckedEnumCannotExposeItsStorageAsHighLevelSSA) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def enum Choice { Yes }
        def main() -> i32 {
            def unused: Choice = Choice.Yes;
            if Choice.Yes == Choice.Yes { return 42; }
            return 1;
        }
    )", "enum_storage.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    gloin::EnumConstantOp constant;
    high.module->walk([&](gloin::EnumConstantOp candidate) {
        if (!constant)
            constant = candidate;
    });
    ASSERT_TRUE(constant);
    constant.getValue().setType(constant.getLayoutType());
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("nominal SSA value"), std::string::npos);
}

TEST(LoweringTest, CheckedSignatureRejectsIncorrectLayoutMetadata) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def enum Choice { Yes }
        def identity(value: Choice) -> Choice { return value; }
        def main() -> i32 {
            def value: Choice = identity(Choice.Yes);
            if value == Choice.Yes { return 42; }
            return 1;
        }
    )", "layout_metadata.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    auto function = high.module->lookupSymbol<mlir::func::FuncOp>("identity");
    ASSERT_TRUE(function);
    function->setAttr("gloin.layout_inputs",
                      mlir::ArrayAttr::get(&context, {mlir::TypeAttr::get(
                          mlir::IntegerType::get(&context, 32))}));
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("source type disagrees"), std::string::npos);
}

TEST(LoweringTest, CheckedRuntimeCallsUseVerifiedAbiBoundary) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        import "@arena";
        def main() -> i32 {
            def mut memory: arena.GeneralArena = arena.GeneralArena.create();
            memory.free();
            return 42;
        }
    )", "abi_call.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    auto high_text = print(*high.module);
    EXPECT_NE(high_text.find("gloin.abi_call"), std::string::npos);
    EXPECT_EQ(high_text.find("llvm.call"), std::string::npos);
    Diagnostics diagnostics;
    auto low = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(low) << render(diagnostics);
    EXPECT_EQ(print(*low).find("gloin.abi_call"), std::string::npos);
    expect_exportable(*low);
}

TEST(LoweringTest, TypedArenaAllocationHasExplicitGloinIRBoundary) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        import "@arena";
        def main() -> i32 {
            def mut memory: arena.GeneralArena = arena.GeneralArena.create();
            def required: &i32 = memory.alloc(40);
            def optional: *i32 = memory.try_alloc(2);
            def value: i32 = *required + *optional;
            memory.free();
            return value;
        }
    )", "typed_arena_ir.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    unsigned seen = 0;
    high.module->walk([&](gloin::ArenaTypedPointerOp allocation) {
        ++seen;
        auto raw = mlir::dyn_cast<gloin::GloinPointerType>(
            allocation.getStorage().getType());
        auto result = mlir::dyn_cast<gloin::GloinPointerType>(
            allocation.getPointer().getType());
        ASSERT_TRUE(raw);
        ASSERT_TRUE(result);
        EXPECT_TRUE(raw.getPointee().isInteger(8));
        EXPECT_TRUE(result.getPointee().isInteger(32));
        EXPECT_EQ(result.getNullable(), seen == 2);
    });
    EXPECT_EQ(seen, 2u);
    Diagnostics diagnostics;
    auto low = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(low) << render(diagnostics);
    expect_exportable(*low);
}

TEST(LoweringTest, ArenaVectorSliceAndReservedStorageHaveCheckedIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        import "@arena";
        import "@vector";
        def main() -> i32 {
            def mut memory: arena.GeneralArena = arena.GeneralArena.create();
            def mut values: vector.Vector<i32> = vector.Vector<i32>.create(&memory, 1, 0);
            values.push(42);
            def mut fresh: vector.Vector<i32> = vector.Vector<i32>.empty(&memory, 0);
            fresh.push(0);
            def view: [const i32] = values.as_const_slice();
            def answer: i32 = view[0];
            memory.free();
            return answer;
        }
    )", "vector_slice_ir.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    unsigned typed_allocations = 0;
    unsigned pointer_slices = 0;
    unsigned fills = 0;
    high.module->walk([&](gloin::ArenaTypedPointerOp) { ++typed_allocations; });
    high.module->walk([&](gloin::SliceFromPointerOp) { ++pointer_slices; });
    high.module->walk([&](gloin::ArenaFillOp) { ++fills; });
    EXPECT_GE(typed_allocations, 2u);
    EXPECT_GT(pointer_slices, 0u);
    EXPECT_EQ(fills, 0u);
    Diagnostics diagnostics;
    auto low = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(low) << render(diagnostics);
    expect_exportable(*low);
}

TEST(LoweringTest, RawMemoryPlacementAndCallsUseGloinIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        import "@memory";
        def main() -> i32 {
            def block: *u8 = memory.alloc(8, memory.align_of<i32>());
            if block == null { return 1; }
            def zero: i64 = 0;
            if memory.padding<i32>(block) != zero { memory.free(block); return 3; }
            def value: *i32 = memory.place<i32>(block, 8, 42);
            if value == null { memory.free(block); return 2; }
            def answer: i32 = *value;
            memory.free(block);
            return answer;
        }
    )", "raw_memory_ir.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    unsigned placements = 0;
    unsigned paddings = 0;
    unsigned abi_calls = 0;
    high.module->walk([&](gloin::RawPlaceOp) { ++placements; });
    high.module->walk([&](gloin::RawPaddingOp) { ++paddings; });
    high.module->walk([&](gloin::AbiCallOp) { ++abi_calls; });
    EXPECT_GT(placements, 0u);
    EXPECT_GT(paddings, 0u);
    EXPECT_GE(abi_calls, 2u);
    Diagnostics diagnostics;
    auto low = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(low) << render(diagnostics);
    expect_exportable(*low);
}

TEST(LoweringTest, CheckedModuleRejectsDirectLLVMCalls) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module attributes {gloin.checked} {
            llvm.func @malloc(i64) -> !llvm.ptr
            func.func @main() -> i32 {
                %size = arith.constant 1 : i64
                %memory = llvm.call @malloc(%size) : (i64) -> !llvm.ptr
                %answer = arith.constant 42 : i32
                return %answer : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    EXPECT_NE(render(diagnostics).find("gloin.abi_call"), std::string::npos);
}

TEST(LoweringTest, AbiCallRejectsWrongRuntimeSignature) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        import "@arena";
        def main() -> i32 {
            def mut memory: arena.GeneralArena = arena.GeneralArena.create();
            memory.free();
            return 42;
        }
    )", "abi_signature.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    gloin::AbiCallOp call;
    high.module->walk([&](gloin::AbiCallOp candidate) {
        if (!call && candidate.getArgs().empty())
            call = candidate;
    });
    ASSERT_TRUE(call);
    call->setAttr("callee", mlir::FlatSymbolRefAttr::get(&context,
                                                         "gloin_arena_general_reset"));
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("exact argument count"), std::string::npos)
        << render(diagnostics);
}

TEST(LoweringTest, CheckedConstantsAndRuntimeGuardsPassThroughGloinIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def half(value: f32) -> f32 { return value / 2.0; }
        def main() -> i32 {
            def mut values: [i32; 1] = {41};
            def pointer: *i32 = &values[0];
            return *pointer + 1;
        }
    )", "guards.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    auto high_text = print(*high.module);
    EXPECT_NE(high_text.find("gloin.constant"), std::string::npos);
    EXPECT_NE(high_text.find("gloin.require_nonnull"), std::string::npos);
    EXPECT_NE(high_text.find("gloin.checked_int_binary"), std::string::npos);
    EXPECT_NE(high_text.find("gloin.checked_float_binary"), std::string::npos);
    EXPECT_NE(high_text.find("gloin.array_element_address"), std::string::npos);
    EXPECT_NE(high_text.find("!gloin.array"), std::string::npos);
    high.module->walk([](gloin::ArrayLiteralOp literal) {
        EXPECT_EQ(literal.getValue().getType(), literal.getSourceType());
        auto element = mlir::cast<gloin::GloinArrayType>(literal.getSourceType()).getElement();
        for (auto value : literal.getElements())
            EXPECT_EQ(value.getType(), element);
    });
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(print(*lowered).find("gloin."), std::string::npos);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, FixedArrayReadsReuseBindingStorageInGloinIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def struct Table { def rows: [[i32; 2]; 2], }
        def local(index: u64) -> i32 {
            def table: [i32; 2] = {10, 11};
            def mut sum: i32 = 0;
            for def mut i: u64 = 0; i < 2; i = i + 1 {
                sum = sum + table[index] + table[index + 1];
            }
            return sum;
        }
        def parameter(table: [i32; 2], index: u64) -> i32 {
            def mut sum: i32 = 0;
            for def mut i: u64 = 0; i < 2; i = i + 1 {
                sum = sum + table[index] + table[index + 1];
            }
            return sum;
        }
        def field(index: u64) -> i32 {
            def table: Table = Table { rows: {{10, 11}, {20, 21}} };
            def mut sum: i32 = 0;
            for def mut i: u64 = 0; i < 2; i = i + 1 {
                sum = sum + table.rows[index][0] + table.rows[index][1];
            }
            return sum;
        }
        def main() -> i32 {
            if local(0) != 42 || parameter({10, 11}, 0) != 42 || field(0) != 42 {
                return 1;
            }
            return 42;
        }
    )", "array_storage.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    for (const auto *name : {"local", "parameter", "field"}) {
        SCOPED_TRACE(name);
        auto function = high.module->lookupSymbol<mlir::func::FuncOp>(name);
        ASSERT_TRUE(function);
        unsigned slots = 0;
        unsigned stores = 0;
        function.walk([&](gloin::StackAllocOp op) {
            if (mlir::isa<gloin::GloinArrayType, gloin::GloinStructType>(op.getSourceType()))
                ++slots;
        });
        function.walk([&](gloin::StoreOp op) {
            if (mlir::isa<gloin::GloinArrayType, gloin::GloinStructType>(op.getSourceType())) {
                ++stores;
                // Initialization belongs before the loop, never at each indexed read.
                EXPECT_EQ(op->getBlock(), &function.getBody().front());
            }
        });
        EXPECT_EQ(slots, 1u);
        EXPECT_EQ(stores, 1u);
    }
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, ResultPayloadWeakensAfterExtractionInGloinIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def borrow(value: &i32) -> result<&i32> { return value; }
        def main() -> i32 {
            def mut value: i32 = 42;
            def outcome: result<&i32> = borrow(&value);
            if outcome.erroneous { return 1; }
            def pointer: *const i32 = outcome.value;
            return *pointer;
        }
    )", "result_capability.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    unsigned extractions = 0;
    unsigned conversions = 0;
    high.module->walk([&](gloin::ResultValueOp op) {
        ++extractions;
        auto result = mlir::cast<gloin::GloinResultType>(op.getOutcome().getType());
        EXPECT_EQ(op.getValue().getType(), result.getValueType());
        auto pointer = mlir::cast<gloin::GloinPointerType>(op.getValue().getType());
        EXPECT_FALSE(pointer.getNullable());
        EXPECT_FALSE(pointer.getReadOnly());
    });
    high.module->walk([&](gloin::FromLayoutOp op) {
        auto bridge = op.getValue().getDefiningOp<gloin::ToLayoutOp>();
        if (!bridge || !bridge.getSourceValue().getDefiningOp<gloin::ResultValueOp>())
            return;
        ++conversions;
        auto pointer = mlir::cast<gloin::GloinPointerType>(op.getSourceValue().getType());
        EXPECT_TRUE(pointer.getNullable());
        EXPECT_TRUE(pointer.getReadOnly());
    });
    EXPECT_EQ(extractions, 1u);
    EXPECT_EQ(conversions, 1u);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, SliceElementLoadsWeakenAfterReadingStorageInGloinIR) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def mut values: [i32; 2] = {20, 22};
            def rows: [[i32]; 1] = {values[..]};
            def view: [const i32] = rows[0];
            return view[0] + view[1];
        }
    )", "slice_capability.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    unsigned conversions = 0;
    high.module->walk([&](gloin::FromLayoutOp op) {
        auto slice = mlir::dyn_cast<gloin::GloinSliceType>(op.getSourceValue().getType());
        if (!slice || !slice.getReadOnly())
            return;
        auto bridge = op.getValue().getDefiningOp<gloin::ToLayoutOp>();
        ASSERT_TRUE(bridge);
        auto load = bridge.getSourceValue().getDefiningOp<gloin::LoadOp>();
        ASSERT_TRUE(load);
        ++conversions;
        auto stored = mlir::cast<gloin::GloinSliceType>(load.getSourceType());
        EXPECT_FALSE(stored.getReadOnly());
        EXPECT_EQ(load.getValue().getType(), stored);
        EXPECT_EQ(mlir::cast<gloin::GloinPointerType>(load.getAddress().getType()).getPointee(),
                  stored);
    });
    EXPECT_EQ(conversions, 1u);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, CheckedPointerOffsetUsesGloinIRUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def mut data: [i32; 2] = {40, 2};
            def first: *i32 = &data[0];
            def second: *i32 = first + 1;
            def readonly: *const i32 = second + 0;
            return *first + *readonly;
        }
    )", "pointer_offset.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    EXPECT_NE(print(*high.module).find("gloin.pointer_offset"), std::string::npos);
    EXPECT_NE(print(*high.module).find("gloin.require_nonnull"), std::string::npos);
    EXPECT_NE(print(*high.module).find("!gloin.ptr"), std::string::npos);
    EXPECT_NE(print(*high.module).find("!gloin.ptr<i32, true, true>"), std::string::npos);
    high.module->walk([](gloin::PointerOffsetOp offset) {
        EXPECT_EQ(offset.getBase().getType(), offset.getSourceType());
        EXPECT_EQ(offset.getAddress().getType(), offset.getSourceType());
    });
    high.module->walk([](gloin::RequireNonNullOp check) {
        EXPECT_EQ(check.getPointer().getType(), check.getSourceType());
        auto source = mlir::cast<gloin::GloinPointerType>(check.getSourceType());
        auto result = mlir::dyn_cast<gloin::GloinPointerType>(
            check.getCheckedPointer().getType());
        ASSERT_TRUE(result);
        EXPECT_FALSE(result.getNullable());
        EXPECT_EQ(result.getPointee(), source.getPointee());
        EXPECT_EQ(result.getReadOnly(), source.getReadOnly());
    });
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(print(*lowered).find("gloin."), std::string::npos);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, PointerComparisonRejectsAnotherPointeeType) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def a: *i32 = null;
            def b: *i64 = null;
            if a == null && b == null { return 42; }
            return 1;
        }
    )", "pointer_compare_type.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    gloin::PointerCompareOp comparison;
    mlir::Value wrong;
    high.module->walk([&](gloin::PointerCompareOp candidate) {
        auto type = mlir::cast<gloin::GloinPointerType>(candidate.getSourceType());
        if (type.getPointee().isInteger(32))
            comparison = candidate;
    });
    high.module->walk([&](gloin::NullOp null_value) {
        auto type = mlir::cast<gloin::GloinPointerType>(null_value.getSourceType());
        if (type.getPointee().isInteger(64))
            wrong = null_value.getValue();
    });
    ASSERT_TRUE(comparison);
    ASSERT_TRUE(wrong);
    comparison->setOperand(1, wrong);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("identical source pointer operand types"),
              std::string::npos);
}

TEST(LoweringTest, LayoutBridgeCannotRetypeKnownPointerPointee) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def pointer: *i32 = null;
            if pointer == null { return 42; }
            return 1;
        }
    )", "pointer_bridge_type.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    gloin::ToLayoutOp bridge;
    high.module->walk([&](gloin::ToLayoutOp candidate) {
        if (candidate.getSourceValue().getDefiningOp<gloin::NullOp>())
            bridge = candidate;
    });
    ASSERT_TRUE(bridge);
    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(bridge);
    auto wrong = gloin::GloinPointerType::get(
        &context, mlir::IntegerType::get(&context, 64), true, false);
    builder.create<gloin::FromLayoutOp>(bridge.getLoc(), wrong, bridge.getValue());
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("cannot retype a known source value"),
              std::string::npos);
}

TEST(LoweringTest, LayoutBridgesRejectStrengtheningAndNestedRetyping) {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        SCOPED_TRACE(scenario);
        mlir::MLIRContext context;
        auto high = compile_source(R"(
            def inputs(pointer: *const i32, row: [const i32], references: [&i32],
                       text: string, nested: &&i32) -> i32 { return 42; }
        )", "invalid_capability_bridge.gloin", context);
        ASSERT_TRUE(high.success()) << render(*high.diagnostics);
        auto function = high.module->lookupSymbol<mlir::func::FuncOp>("inputs");
        ASSERT_TRUE(function);
        auto integer = mlir::IntegerType::get(&context, 32);
        auto nullable = gloin::GloinPointerType::get(&context, integer, true, false);
        const mlir::Type requested[] = {
            gloin::GloinPointerType::get(&context, integer, false, true),
            nullable,
            gloin::GloinSliceType::get(&context, integer, false),
            gloin::GloinSliceType::get(&context, nullable, false),
            gloin::GloinSliceType::get(&context, mlir::IntegerType::get(&context, 8), true),
            gloin::GloinPointerType::get(&context, nullable, false, false),
        };
        const unsigned argument[] = {0, 0, 1, 2, 3, 4};
        auto value = function.getArgument(argument[scenario]);
        mlir::Type layout = mlir::LLVM::LLVMPointerType::get(&context);
        if (scenario >= 2 && scenario <= 4)
            layout = mlir::LLVM::LLVMStructType::getLiteral(
                &context, {layout, mlir::IntegerType::get(&context, 64)});
        mlir::OpBuilder builder(&context);
        builder.setInsertionPointToStart(&function.getBody().front());
        auto bridge = builder.create<gloin::ToLayoutOp>(function.getLoc(), layout, value);
        builder.create<gloin::FromLayoutOp>(function.getLoc(), requested[scenario],
                                            bridge.getValue());
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
        EXPECT_NE(render(diagnostics).find("cannot retype a known source value"),
                  std::string::npos) << render(diagnostics);
    }
}

TEST(LoweringTest, CheckedStorageAndFieldAccessUseGloinIRUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def struct Cell { def mut value: i32, }
        def main() -> i32 {
            def mut cell: Cell = Cell { value: 40 };
            def copy: Cell = cell;
            cell.value = copy.value + 2;
            return cell.value;
        }
    )", "storage.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    const auto high_text = print(*high.module);
    for (const char *op : {"gloin.stack_alloc", "gloin.load", "gloin.store",
                           "gloin.field_address", "gloin.extract_field"})
        EXPECT_NE(high_text.find(op), std::string::npos) << op;
    EXPECT_NE(high_text.find("!gloin.struct"), std::string::npos);
    size_t definitions = 0;
    high.module->walk([&](gloin::StructDefinitionOp definition) {
        ++definitions;
        EXPECT_TRUE(definition.getSymName().starts_with("Cell#"));
        EXPECT_EQ(definition.getFieldSourceTypes().size(), 1u);
    });
    EXPECT_EQ(definitions, 1u);
    high.module->walk([](gloin::StackAllocOp allocation) {
        auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(
            allocation.getAddress().getType());
        ASSERT_TRUE(pointer);
        EXPECT_EQ(pointer.getPointee(), allocation.getSourceType());
    });
    high.module->walk([](gloin::LoadOp load) {
        auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(load.getAddress().getType());
        ASSERT_TRUE(pointer);
        EXPECT_EQ(pointer.getPointee(), load.getSourceType());
        EXPECT_EQ(load.getValue().getType(), load.getSourceType());
    });
    high.module->walk([](gloin::StoreOp store) {
        auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(store.getAddress().getType());
        ASSERT_TRUE(pointer);
        EXPECT_EQ(pointer.getPointee(), store.getSourceType());
        EXPECT_EQ(store.getValue().getType(), store.getSourceType());
    });
    high.module->walk([](gloin::FieldAddressOp field) {
        auto base = mlir::dyn_cast<gloin::GloinPointerType>(field.getBase().getType());
        auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(field.getAddress().getType());
        ASSERT_TRUE(base);
        ASSERT_TRUE(pointer);
        EXPECT_EQ(base.getPointee(), field.getSourceType());
        EXPECT_EQ(pointer.getPointee(), field.getFieldSourceType());
        EXPECT_FALSE(pointer.getNullable());
    });
    high.module->walk([](gloin::StructLiteralOp literal) {
        EXPECT_EQ(literal.getValue().getType(), literal.getSourceType());
        ASSERT_EQ(literal.getFields().size(), 1u);
        EXPECT_EQ(literal.getFields().front().getType(),
                  mlir::cast<mlir::TypeAttr>(literal.getFieldSourceTypes()[0]).getValue());
    });
    high.module->walk([](gloin::ExtractFieldOp field) {
        EXPECT_EQ(field.getAggregate().getType(), field.getSourceType());
        EXPECT_EQ(field.getValue().getType(), field.getFieldSourceType());
    });
    for (const char *op : {"llvm.alloca", "llvm.load", "llvm.store",
                           "llvm.getelementptr", "llvm.extractvalue"})
        EXPECT_EQ(high_text.find(op), std::string::npos) << op;
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(print(*lowered).find("gloin."), std::string::npos);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, CheckedStorageRejectsMismatchedSourcePointee) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def mut first: i32 = 42;
            def mut second: i64 = 7;
            return first;
        }
    )", "storage_mismatch.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    mlir::Value wrong_address;
    gloin::LoadOp load;
    high.module->walk([&](gloin::StackAllocOp allocation) {
        if (allocation.getSourceType().isInteger(64))
            wrong_address = allocation.getAddress();
    });
    high.module->walk([&](gloin::LoadOp candidate) {
        if (candidate.getSourceType().isInteger(32))
            load = candidate;
    });
    ASSERT_TRUE(wrong_address);
    ASSERT_TRUE(load);
    load->setOperand(0, wrong_address);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("loaded pointee type"), std::string::npos);
}

TEST(LoweringTest, StructDefinitionRejectsForgedFieldAddressType) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def struct First { def mut value: i32, }
        def struct Second { def mut value: i32, }
        def struct Holder { def mut first: First, }
        def main() -> i32 {
            def mut holder: Holder = Holder { first: First { value: 40 } };
            holder.first.value = 42;
            return holder.first.value;
        }
    )", "nominal_field_address.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    bool changed = false;
    high.module->walk([&](gloin::FieldAddressOp field) {
        if (changed || !mlir::isa<gloin::GloinStructType>(field.getFieldSourceType()))
            return;
        auto wrong = gloin::GloinPointerType::get(
            &context, gloin::GloinStructType::get(&context, "Second"), false, false);
        EXPECT_NE(field.getFieldSourceType(), wrong.getPointee());
        field.getAddress().setType(wrong);
        field->setAttr("field_source_type", mlir::TypeAttr::get(wrong.getPointee()));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("nominal struct definition"), std::string::npos);
}

TEST(LoweringTest, CheckedStorageRejectsWriteThroughReadOnlyAddress) {
    mlir::MLIRContext context;
    auto high = compile_source(
        "def main() -> i32 { def mut value: i32 = 42; return value; }",
        "readonly_store.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    gloin::StoreOp store;
    high.module->walk([&](gloin::StoreOp candidate) { store = candidate; });
    ASSERT_TRUE(store);
    mlir::OpBuilder builder(&context);
    builder.setInsertionPoint(store);
    auto layout = builder.create<gloin::ToLayoutOp>(
        store.getLoc(), mlir::LLVM::LLVMPointerType::get(&context), store.getAddress());
    auto readonly = gloin::GloinPointerType::get(
        &context, mlir::IntegerType::get(&context, 32), false, true);
    auto address = builder.create<gloin::FromLayoutOp>(store.getLoc(), readonly, layout);
    store->setOperand(1, address);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("writable source address"), std::string::npos);
}

TEST(LoweringTest, ArrayElementAddressRejectsDifferentArrayPointee) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def mut first: [i32; 2] = {40, 2};
            def mut second: [i64; 2] = {7, 8};
            return first[0] + first[1];
        }
    )", "array_address_type.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    mlir::Value wrong_address;
    gloin::ArrayElementAddressOp element;
    high.module->walk([&](gloin::StackAllocOp allocation) {
        if (auto array = mlir::dyn_cast<gloin::GloinArrayType>(allocation.getSourceType());
            array && array.getElement().isInteger(64))
            wrong_address = allocation.getAddress();
    });
    high.module->walk([&](gloin::ArrayElementAddressOp candidate) {
        auto array = mlir::cast<gloin::GloinArrayType>(candidate.getSourceType());
        if (array.getElement().isInteger(32))
            element = candidate;
    });
    ASSERT_TRUE(wrong_address);
    ASSERT_TRUE(element);
    element->setOperand(0, wrong_address);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("matching pointer base/result"), std::string::npos);
}

TEST(LoweringTest, RepeatedArrayStoresStayCompactThroughLLVMExport) {
    for (const unsigned count : {16u, 16384u, 65536u}) {
        for (const bool nested : {false, true}) {
            SCOPED_TRACE(std::to_string(count) + (nested ? " nested" : " flat"));
            const auto size = std::to_string(nested ? count / 8 : count);
            const std::string type = nested ? "[[u8; 8]; " + size + "]" : "[u8; " + size + "]";
            const std::string init = nested ? "{{value; 8}; " + size + "}" : "{value; " + size + "}";
            const std::string index = nested ? "data[index / 8][index % 8]" : "data[index]";
            mlir::MLIRContext context;
            auto compiled = compile_source("def lookup(value: u8, index: u64) -> u8 { def data: " +
                type + " = " + init + "; return " + index + "; }", "large-repeat.gloin", context,
                CompilationMode::Module, CompilationOutput::LLVM);
            ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
            unsigned allocations = 0, fills = 0, aggregate_accesses = 0, operations = 0;
            compiled.module->walk([&](mlir::Operation *op) {
                ++operations;
                allocations += mlir::isa<mlir::LLVM::AllocaOp>(op);
                fills += mlir::isa<mlir::LLVM::MemsetOp>(op);
                if (auto load = mlir::dyn_cast<mlir::LLVM::LoadOp>(op))
                    aggregate_accesses += mlir::isa<mlir::LLVM::LLVMArrayType>(load.getType());
                if (auto store = mlir::dyn_cast<mlir::LLVM::StoreOp>(op))
                    aggregate_accesses += mlir::isa<mlir::LLVM::LLVMArrayType>(store.getValue().getType());
            });
            EXPECT_EQ(allocations, 1u);
            EXPECT_EQ(fills, 1u);
            EXPECT_EQ(aggregate_accesses, 0u);
            EXPECT_LT(operations, 50u);
            expect_exportable(*compiled.module);
        }
    }
}

TEST(LoweringTest, ArrayFillVerifierEnforcesSourceStorageAndElementBoundaries) {
    const std::string array_source = "!gloin.array<!gloin.array<i32, 2>, 3>";
    const std::string array_layout = "!llvm.array<3 x array<2 x i32>>";
    const auto valid_address = "!gloin.ptr<" + array_source + ", false, false>";
    auto module_text = [&](const std::string &address_type, const std::string &element_type,
                           const std::string &element_source, const std::string &element_layout,
                           const std::string &layout) {
        return "module { func.func @fill(%address: " + address_type + ", %element: " + element_type +
            ") { \"gloin.array_fill\"(%address, %element) <{source_type = " + array_source +
            ", layout_type = " + layout + ", element_source_type = " + element_source +
            ", element_type = " + element_layout + "}> : (" + address_type + ", " + element_type +
            ") -> () return } }";
    };
    mlir::MLIRContext context;
    EXPECT_TRUE(parse(context, module_text(valid_address, "i32", "i32", "i32", array_layout)));
    EXPECT_TRUE(parse(context, module_text(valid_address, "!gloin.array<i32, 2>",
        "!gloin.array<i32, 2>", "!llvm.array<2 x i32>", array_layout)));
    mlir::ScopedDiagnosticHandler silence(&context, [](mlir::Diagnostic &) { return mlir::success(); });
    for (const auto &address : {"!gloin.ptr<" + array_source + ", false, true>",
                               "!gloin.ptr<" + array_source + ", true, false>",
                               std::string("!gloin.ptr<i32, false, false>")})
        EXPECT_FALSE(parse(context, module_text(address, "i32", "i32", "i32", array_layout)));
    EXPECT_FALSE(parse(context, module_text(valid_address, "i8", "i8", "i8", array_layout)));
    EXPECT_FALSE(parse(context, module_text(valid_address, "i32", "i32", "i64", array_layout)));
    EXPECT_FALSE(parse(context, module_text(valid_address, "i32", "i32", "i32", "!llvm.array<2 x i32>")));
}

TEST(LoweringTest, ArrayPreparationPreservesSourceTypedReturnBranches) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def choose(flag: bool) -> result<string> {
            if flag { return error("first"); }
            return error("second");
        }
        def pick(flag: bool, first: [i32; 2], second: [i32; 2]) -> [i32; 2] {
            if flag { return first; }
            return second;
        }
        def main() -> i32 {
            def outcome: result<string> = choose(false);
            if outcome.erroneous {
                def values: [i32; 2] = pick(true, {21; 2}, {0; 2});
                return values[0] + values[1];
            }
            return 1;
        }
    )", "array-preparation-branches.gloin", context, CompilationMode::Executable,
        CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    execute_lowered(*compiled.module, 42);
}

TEST(LoweringTest, ZeroedArraysAndNullPointersStayInGloinIRUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            def values: [i32; 2] = zeroed;
            def pointer: *i32 = null;
            if pointer == null && values[0] == 0 { return 42; }
            return 1;
        }
    )", "zeroed.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    auto high_text = print(*high.module);
    EXPECT_NE(high_text.find("gloin.zeroed_array"), std::string::npos);
    high.module->walk([](gloin::ZeroedArrayOp zeroed) {
        EXPECT_EQ(zeroed.getValue().getType(), zeroed.getSourceType());
    });
    high.module->walk([](gloin::ArrayElementAddressOp element) {
        auto base = mlir::dyn_cast<gloin::GloinPointerType>(element.getBase().getType());
        auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(
            element.getAddress().getType());
        ASSERT_TRUE(base);
        ASSERT_TRUE(pointer);
        EXPECT_EQ(base.getPointee(), element.getSourceType());
        EXPECT_EQ(pointer.getPointee(),
                  mlir::cast<gloin::GloinArrayType>(element.getSourceType()).getElement());
    });
    EXPECT_NE(high_text.find("gloin.null"), std::string::npos);
    EXPECT_NE(high_text.find("gloin.pointer_compare"), std::string::npos);
    high.module->walk([](gloin::NullOp null_value) {
        EXPECT_EQ(null_value.getValue().getType(), null_value.getSourceType());
    });
    high.module->walk([](gloin::PointerCompareOp comparison) {
        EXPECT_EQ(comparison.getLhs().getType(), comparison.getSourceType());
        EXPECT_EQ(comparison.getRhs().getType(), comparison.getSourceType());
    });
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(print(*lowered).find("gloin."), std::string::npos);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, NestedControlFlowCallsAndMutableStorageExecute) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def twice(x: i32) -> i32 { return x * 2; }
        def main() -> i32 {
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 3 && true; i = i + 1 {
                def mut j: i32 = 0;
                while j < 2 {
                    unless i < 0 { total = total + twice(3); }
                    j = j + 1;
                }
            }
            if total == 36 { return total + 6; } else { return -1; }
        }
    )",
                                 "nested.gloin", context, CompilationMode::Executable,
                                 CompilationOutput::LLVM);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    execute_lowered(*result.module, 42);
}

TEST(LoweringTest, EveryCoreScalarSignatureAndCheckedArithmeticExports) {
    mlir::MLIRContext context;
    std::string source = "def noop() -> void {}";
    for (const std::string type :
         {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"})
        source += "def copy_" + type + "(x: " + type + ") -> " + type + " { def mut y: " + type +
                  " = x; y = y + " + (type.starts_with("f") ? "1.0" : "1") + "; return y; }";
    source += "def flag(x: bool) -> bool { return !x; }";
    auto result = compile_source(source, "types.gloin", context, CompilationMode::Module,
                                 CompilationOutput::LLVM);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    expect_exportable(*result.module);
    EXPECT_NE(print(*result.module).find("llvm.intr.sadd.with.overflow"), std::string::npos);
    EXPECT_NE(print(*result.module).find("llvm.intr.uadd.with.overflow"), std::string::npos);
    result.module->walk([](mlir::Operation *op) {
        for (auto type : op->getOperandTypes()) EXPECT_FALSE(type.isInteger(128));
        for (auto type : op->getResultTypes()) EXPECT_FALSE(type.isInteger(128));
    });
}

TEST(LoweringTest, StructuredControlFlowAndMemrefsUseTheSamePipeline) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @main() -> i32 {
                %slot = memref.alloca() : memref<i32>
                %zero = arith.constant 0 : index
                %one = arith.constant 1 : index
                %three = arith.constant 3 : index
                %base = arith.constant 0 : i32
                %step = arith.constant 14 : i32
                memref.store %base, %slot[] : memref<i32>
                scf.for %i = %zero to %three step %one {
                    %old = memref.load %slot[] : memref<i32>
                    %next = arith.addi %old, %step : i32
                    memref.store %next, %slot[] : memref<i32>
                }
                %result = memref.load %slot[] : memref<i32>
                return %result : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_FALSE(module);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, CloneLoweringPreservesHighLevelModuleAndLocations) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 { return 42; }", "owned.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    const auto original = print(*result.module);
    Diagnostics diagnostics;
    auto lowered =
        lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp>(result.module->clone()), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    EXPECT_EQ(original, print(*result.module));
    auto function = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("main");
    auto loc = function.getLoc()->findInstanceOf<mlir::FileLineColLoc>();
    ASSERT_TRUE(loc);
    EXPECT_EQ(loc.getFilename().str(), "owned.gloin");
    EXPECT_EQ(loc.getLine(), 1u);
}

TEST(LoweringTest, InvalidIRStopsBeforeConversionAndKeepsSourceSpan) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 {\n  return 42;\n}", "invalid.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    auto function = result.module->lookupSymbol<mlir::func::FuncOp>("main");
    auto *ret = function.front().getTerminator();
    ret->setOperands(mlir::ValueRange{});
    auto source =
        std::make_shared<SourceFile>("invalid.gloin", "def main() -> i32 {\n  return 42;\n}");
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(result.module), diagnostics, source);
    EXPECT_FALSE(lowered);
    EXPECT_FALSE(result.module);
    ASSERT_FALSE(diagnostics.all().empty());
    const auto &error = diagnostics.all().front();
    EXPECT_EQ(error.stage, DiagnosticStage::Verification);
    ASSERT_EQ(error.span.source, source);
    EXPECT_EQ(source->line_column(error.span.begin), (std::pair<size_t, size_t>{2, 3}));
}

TEST(LoweringTest, GloinConstantVerifierRejectsIllTypedIRBeforeLowering) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 { return 42; }", "constant.gloin", context);
    ASSERT_TRUE(result.success());
    bool changed = false;
    result.module->walk([&](gloin::ConstantOp op) {
        op->setAttr("value", mlir::StringAttr::get(&context, "bad"));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(result.module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Verification);
    EXPECT_NE(render(diagnostics).find("matching integer or floating"), std::string::npos);
}

TEST(LoweringTest, GloinFieldVerifierRejectsOutOfRangeLayoutIndex) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def struct Cell { def mut value: i32, }
        def main() -> i32 {
            def mut cell: Cell = Cell { value: 42 };
            cell.value = 42;
            return cell.value;
        }
    )", "field.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    bool changed = false;
    result.module->walk([&](gloin::FieldAddressOp op) {
        op->setAttr("index", mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64), 7));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(result.module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Verification);
    EXPECT_NE(render(diagnostics).find("valid struct field index"), std::string::npos);
}

TEST(LoweringTest, GloinNullVerifierRequiresNullableSourceType) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def main() -> i32 {
            def pointer: *i32 = null;
            if pointer == null { return 42; }
            return 1;
        }
    )", "null-type.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    bool changed = false;
    result.module->walk([&](gloin::NullOp op) {
        auto source = gloin::GloinPointerType::get(&context, mlir::IntegerType::get(&context, 32),
                                                    false, false);
        op->setAttr("source_type", mlir::TypeAttr::get(source));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(result.module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Verification);
    EXPECT_NE(render(diagnostics).find("nullable Gloin pointer"), std::string::npos);
}

TEST(LoweringTest, GloinArrayVerifierRejectsSourceLengthMismatch) {
    mlir::MLIRContext context;
    auto result = compile_source(R"(
        def main() -> i32 {
            def values: [i32; 2] = {40, 2};
            return values[0] + values[1];
        }
    )", "array-type.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    bool changed = false;
    result.module->walk([&](gloin::ArrayLiteralOp op) {
        auto source = gloin::GloinArrayType::get(&context,
            mlir::IntegerType::get(&context, 32), 3);
        op->setAttr("source_type", mlir::TypeAttr::get(source));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(result.module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Verification);
    EXPECT_NE(render(diagnostics).find("matching Gloin fixed-array"), std::string::npos);
}

TEST(LoweringTest, ScalarComparisonsKeepSourceRulesUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def check(a: i32, b: i32, c: u32, d: u32, x: f32, y: f32) -> bool {
            return a < b && c < d && x != y;
        }
        def main() -> i32 {
            if check(-1, 0, 1, 2, 1.0, 2.0) { return 42; }
            return 1;
        }
    )", "typed_comparisons.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    bool signed_less = false;
    bool unsigned_less = false;
    bool float_unequal = false;
    high.module->walk([&](gloin::CheckedIntegerCompareOp comparison) {
        if (comparison.getKind() == "<") {
            signed_less |= comparison.getIsSigned();
            unsigned_less |= !comparison.getIsSigned();
        }
    });
    high.module->walk([&](gloin::CheckedFloatCompareOp comparison) {
        float_unequal |= comparison.getKind() == "!=";
    });
    EXPECT_TRUE(signed_less);
    EXPECT_TRUE(unsigned_less);
    EXPECT_TRUE(float_unequal);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, ScalarComparisonRejectsUnsupportedPredicate) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def main() -> i32 {
            if 1 < 2 { return 42; }
            return 1;
        }
    )", "invalid_comparison.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    bool changed = false;
    high.module->walk([&](gloin::CheckedIntegerCompareOp comparison) {
        comparison->setAttr("kind", mlir::StringAttr::get(&context, "~="));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("unsupported integer comparison"), std::string::npos);
}

TEST(LoweringTest, StructDefinitionRejectsForgedLiteralFieldType) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def struct First { def value: i32, }
        def struct Second { def value: i32, }
        def struct Holder { def first: First, }
        def main() -> i32 {
            def a: First = First { value: 40 };
            def b: Second = Second { value: 2 };
            def holder: Holder = Holder { first: a };
            return holder.first.value + b.value;
        }
    )", "nominal_field.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    mlir::Value wrong;
    gloin::StructLiteralOp holder;
    high.module->walk([&](gloin::StructLiteralOp literal) {
        const auto name = mlir::cast<gloin::GloinStructType>(literal.getSourceType()).getName();
        if (name.starts_with("Second#"))
            wrong = literal.getValue();
        if (name.starts_with("Holder#"))
            holder = literal;
    });
    ASSERT_TRUE(wrong);
    ASSERT_TRUE(holder);
    EXPECT_NE(wrong.getType(),
              mlir::cast<mlir::TypeAttr>(holder.getFieldSourceTypes()[0]).getValue());
    holder->setOperand(0, wrong);
    holder->setAttr("field_source_types", mlir::ArrayAttr::get(
        &context, {mlir::TypeAttr::get(wrong.getType())}));
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("nominal struct definition"), std::string::npos);
}

TEST(LoweringTest, StructDefinitionRejectsForgedExtractedFieldType) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def struct First { def value: i32, }
        def struct Second { def value: i32, }
        def struct Holder { def first: First, }
        def make() -> Holder { return Holder { first: First { value: 42 } }; }
        def main() -> i32 { return make().first.value; }
    )", "nominal_extract.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    bool changed = false;
    high.module->walk([&](gloin::ExtractFieldOp field) {
        if (changed || !mlir::isa<gloin::GloinStructType>(field.getFieldSourceType()))
            return;
        auto wrong = gloin::GloinStructType::get(&context, "Second");
        field.getValue().setType(wrong);
        field->setAttr("field_source_type", mlir::TypeAttr::get(wrong));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(high.module), diagnostics));
    EXPECT_NE(render(diagnostics).find("nominal struct definition"), std::string::npos);
}

TEST(LoweringTest, StringLiteralKeepsSourceTypeUntilLowering) {
    mlir::MLIRContext context;
    auto high = compile_source(R"(
        def echo(value: string) -> string { return value; }
        def main() -> i32 {
            def text: string = echo("hello");
            def other: string = "world";
            return 42;
        }
    )", "typed_string.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(high.success()) << render(*high.diagnostics);
    size_t count = 0;
    high.module->walk([&](gloin::StringLiteralOp literal) {
        ++count;
        EXPECT_TRUE(mlir::isa<gloin::GloinStringType>(literal.getValue().getType()));
        EXPECT_EQ(literal.getValue().getType(), literal.getSourceType());
    });
    EXPECT_EQ(count, 2u);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(high.module), diagnostics);
    ASSERT_TRUE(lowered) << render(diagnostics);
    execute_lowered(*lowered, 42);
}

TEST(LoweringTest, ControlFlowJoinRejectsDifferentNominalStructType) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @join(%incoming: !gloin.struct<"First">) {
                cf.br ^merge(%incoming : !gloin.struct<"First">)
            ^merge(%value: !gloin.struct<"First">):
                return
            }
        }
    )");
    ASSERT_TRUE(module);
    ASSERT_TRUE(mlir::succeeded(mlir::verify(*module)));
    auto function = module->lookupSymbol<mlir::func::FuncOp>("join");
    ASSERT_TRUE(function);
    auto &merge = function.getBody().back();
    merge.getArgument(0).setType(gloin::GloinStructType::get(&context, "Second"));
    Diagnostics diagnostics;
    EXPECT_FALSE(verify_module(*module, diagnostics));
    EXPECT_TRUE(diagnostics.has_errors());
}

TEST(LoweringTest, GloinStoreVerifierRejectsMismatchedSourceType) {
    mlir::MLIRContext context;
    auto result = compile_source("def main() -> i32 { def mut value: i32 = 42; return value; }",
                                 "store-type.gloin", context);
    ASSERT_TRUE(result.success()) << render(*result.diagnostics);
    bool changed = false;
    result.module->walk([&](gloin::StoreOp op) {
        op->setAttr("source_type", mlir::TypeAttr::get(mlir::Float32Type::get(&context)));
        changed = true;
    });
    ASSERT_TRUE(changed);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(result.module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Verification);
    EXPECT_NE(render(diagnostics).find("matching stored pointee type"), std::string::npos);
}

TEST(LoweringTest, RejectsUnsupportedGloinOperationsInsteadOfSilentlyDroppingThem) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @main() -> i32 {
                "gloin.defer"() ({
                    "gloin.yield"() : () -> ()
                }) : () -> () loc("custom.gloin":4:7)
                %c = arith.constant 42 : i32
                return %c : i32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    EXPECT_FALSE(lowered);
    ASSERT_FALSE(diagnostics.all().empty());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    std::ostringstream message;
    diagnostics.render(message);
    EXPECT_NE(message.str().find("custom.gloin:4:7: error:"), std::string::npos);
    EXPECT_NE(message.str().find("gloin.defer"), std::string::npos);
}

TEST(LoweringTest, RejectsCustomTypesNestedInsideFunctionSignatures) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module { func.func private @pending(!gloin.spawn<i32>) }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    EXPECT_NE(diagnostics.all().front().message.find("type"), std::string::npos);
}

TEST(LoweringTest, RejectsUnresolvedConversionCastsAtExportBoundary) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module {
            func.func @cast(%x: i32) -> f32 {
                %y = builtin.unrealized_conversion_cast %x : i32 to f32 loc("cast.gloin":3:9)
                return %y : f32
            }
        }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    std::ostringstream message;
    diagnostics.render(message);
    EXPECT_NE(message.str().find("cast.gloin:3:9"), std::string::npos);
    EXPECT_NE(message.str().find("unrealized_conversion_cast"), std::string::npos);
}

TEST(LoweringTest, RejectsNonLLVMTypesInMetadataAfterConversion) {
    mlir::MLIRContext context;
    auto module = parse(context, "module attributes {test.type = tensor<2xi32>} {}");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_NE(diagnostics.all().front().message.find("Type is not legal at LLVM export"),
              std::string::npos);
}

TEST(LoweringTest, NullModulesAndExistingErrorsCannotSucceed) {
    Diagnostics null_diagnostics;
    EXPECT_FALSE(lower_to_llvm({}, null_diagnostics));
    ASSERT_TRUE(null_diagnostics.has_errors());
    EXPECT_EQ(null_diagnostics.all().front().stage, DiagnosticStage::Verification);
    mlir::MLIRContext context;
    auto module = parse(context, "module {}");
    ASSERT_TRUE(module);
    Diagnostics prior;
    prior.error(DiagnosticStage::Semantic, {}, "previous error");
    EXPECT_FALSE(lower_to_llvm(std::move(module), prior));
    EXPECT_EQ(prior.all().size(), 1u);
}

TEST(LoweringTest, EarlierSourceErrorsKeepTheirOriginalStageInLLVMMode) {
    mlir::MLIRContext context;
    for (const auto &[source, stage] : std::vector<std::pair<std::string, DiagnosticStage>>{
             {"def main() -> i32 { return; }", DiagnosticStage::Semantic},
             {"def main() -> i32 { return 1 }", DiagnosticStage::Parsing},
             {"def main() -> i32 { return 0x; }", DiagnosticStage::Lexing}}) {
        auto result = compile_source(source, "error.gloin", context, CompilationMode::Executable,
                                     CompilationOutput::LLVM);
        EXPECT_FALSE(result.success());
        EXPECT_FALSE(result.module);
        EXPECT_EQ(result.failed_stage, stage);
    }
}

TEST(LoweringTest, RepeatedCompilationAndExecutionHaveStableResults) {
    std::string previous;
    for (unsigned i = 0; i < 3; ++i) {
        mlir::MLIRContext context;
        auto result = compile_source("def main() -> i32 { def mut i: i32 = 0; "
                                     "for ; i < 3; i = i + 1 {} return i - 4; }",
                                     "repeat.gloin", context, CompilationMode::Executable,
                                     CompilationOutput::LLVM);
        ASSERT_TRUE(result.success()) << render(*result.diagnostics);
        const auto ir = print(*result.module);
        if (i)
            EXPECT_EQ(ir, previous);
        previous = ir;
        execute_lowered(*result.module, -1);
    }
}

TEST(LoweringTest, RejectsUnknownOperationsAndNestedModules) {
    mlir::MLIRContext context;
    context.allowUnregisteredDialects();
    for (const std::string text :
         {"module { \"alien.operation\"() : () -> () }", "module { module @nested {} }"}) {
        auto module = parse(context, text);
        ASSERT_TRUE(module);
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(module), diagnostics));
        ASSERT_TRUE(diagnostics.has_errors());
        EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
        EXPECT_NE(diagnostics.all().front().message.find("No supported lowering"),
                  std::string::npos);
    }
}

TEST(LoweringTest, ConversionErrorsDiscardPartialIRAndStayInDiagnostics) {
    mlir::MLIRContext context;
    auto module = parse(context, R"(
        module { func.func @bad(%x: memref<4xi32, "unsupported">) { return } }
    )");
    ASSERT_TRUE(module);
    Diagnostics diagnostics;
    auto lowered = lower_to_llvm(std::move(module), diagnostics);
    EXPECT_FALSE(lowered);
    EXPECT_FALSE(module);
    ASSERT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(diagnostics.all().front().stage, DiagnosticStage::Lowering);
    EXPECT_NE(render(diagnostics).find("memory space"), std::string::npos);
}

TEST(LoweringTest, GIRCombinesAdjacentFloatChecksWithSeparateRoundingAndMandatoryFailure) {
    for (const std::string type : {"f32", "f64"}) {
        for (bool product_first : {false, true}) {
            for (bool unused : {false, true}) {
                SCOPED_TRACE(type + (product_first ? " product first" : " product second") +
                             (unused ? " discarded" : " returned"));
                mlir::MLIRContext context;
                auto high = parse(context, "module { func.func @calculate(%a: " + type +
                    ", %b: " + type + ", %total: " + type + ") -> " + type + " {"
                    " %p = \"gloin.checked_float_binary\"(%a, %b) <{kind = \"*\"}> : (" +
                    type + ", " + type + ") -> " + type + " loc(\"pair.gloin\":2:1)"
                    " %s = \"gloin.checked_float_binary\"(" +
                    (product_first ? "%p, %total" : "%total, %p") + ") <{kind = \"+\"}> : (" +
                    type + ", " + type + ") -> " + type + " loc(\"pair.gloin\":3:1)"
                    " return " + (unused ? "%total" : "%s") + " : " + type + " } }");
                ASSERT_TRUE(high);
                combine_gloin_float_checks(*high);
                ASSERT_TRUE(mlir::succeeded(mlir::verify(*high)));
                unsigned checked = 0, multiplies = 0;
                high->walk([&](gloin::CheckedFloatBinaryOp op) {
                    ++checked;
                    EXPECT_EQ(op.getKind(), "+");
                    EXPECT_EQ(op.getLoc(), mlir::FileLineColLoc::get(&context, "pair.gloin", 3, 1));
                });
                high->walk([&](mlir::arith::MulFOp op) {
                    ++multiplies;
                    EXPECT_EQ(op.getFastmath(), mlir::arith::FastMathFlags::none);
                    EXPECT_EQ(op.getLoc(), mlir::FileLineColLoc::get(&context, "pair.gloin", 2, 1));
                });
                EXPECT_EQ(checked, 1u);
                EXPECT_EQ(multiplies, 1u);
                const auto once = print(*high);
                combine_gloin_float_checks(*high);
                EXPECT_EQ(print(*high), once);
                Diagnostics diagnostics;
                auto low = lower_to_llvm(std::move(high), diagnostics);
                ASSERT_TRUE(low) << render(diagnostics);
                expect_exportable(*low);
                unsigned products = 0, sums = 0, finite = 0, traps = 0;
                low->walk([&](mlir::LLVM::FMulOp op) {
                    ++products;
                    EXPECT_EQ(op.getFastmathFlags(), mlir::LLVM::FastmathFlags::none);
                });
                low->walk([&](mlir::LLVM::FAddOp op) {
                    ++sums;
                    EXPECT_EQ(op.getFastmathFlags(), mlir::LLVM::FastmathFlags::none);
                });
                low->walk([&](mlir::LLVM::FCmpOp) { ++finite; });
                low->walk([&](mlir::LLVM::Trap) { ++traps; });
                EXPECT_EQ(products, 1u);
                EXPECT_EQ(sums, 1u);
                EXPECT_EQ(finite, 2u); // Both comparisons of the sum, even when unused.
                EXPECT_EQ(traps, 1u);
            }
        }
    }
}

TEST(LoweringTest, GIRFloatCheckCombiningRejectsInterveningOperationsAndEscapingProducts) {
    for (const std::string type : {"f32", "f64"}) {
        const std::string multiply =
            " %p = \"gloin.checked_float_binary\"(%a, %b) <{kind = \"*\"}> : (" +
            type + ", " + type + ") -> " + type;
        const auto sum = [&](const std::string &operands, const std::string &kind = "+") {
            return " %s = \"gloin.checked_float_binary\"(" + operands + ") <{kind = \"" +
                kind + "\"}> : (" + type + ", " + type + ") -> " + type;
        };
        const auto addition = sum("%total, %p");
        const std::string result = " return %s : " + type;
        const std::vector<std::pair<std::string, std::string>> bodies{
            {"call", multiply + " func.call @effect() : () -> ()" + addition + result},
            {"load", multiply + " %v = llvm.load %address : !llvm.ptr -> " + type + addition + result},
            {"store", multiply + " llvm.store %a, %address : " + type + ", !llvm.ptr" + addition + result},
            {"assert", multiply + " \"gloin.assert\"(%ok) : (i1) -> ()" + addition + result},
            {"integer failure", multiply + " %i = \"gloin.checked_int_binary\"(%n, %n)"
                " <{kind = \"+\", is_signed = true}> : (i64, i64) -> i64" + addition + result},
            {"float division", multiply + " %d = \"gloin.checked_float_binary\"(%a, %b)"
                " <{kind = \"/\"}> : (" + type + ", " + type + ") -> " + type + addition + result},
            {"infallible constant", multiply + " %c = arith.constant 0 : i64" + addition + result},
            {"unknown operation", multiply + " \"opaque.effect\"() : () -> ()" + addition + result},
            {"block boundary", multiply + " cf.br ^next ^next:" + addition + result},
            {"conditional consumer", multiply + " cf.cond_br %ok, ^next, ^exit ^next:" +
                addition + result + " ^exit: return %total : " + type},
            {"two uses in one consumer", multiply + sum("%p, %p") + result},
            {"escaping product", multiply + addition + " func.call @consume(%p) : (" + type +
                ") -> ()" + result},
            {"subtraction", multiply + sum("%total, %p", "-") + result},
            {"division consumer", multiply + sum("%total, %p", "/") + result},
        };
        for (const auto &[name, body] : bodies) {
            SCOPED_TRACE(type + " " + name);
            mlir::MLIRContext context;
            context.allowUnregisteredDialects();
            auto high = parse(context, "module { func.func private @effect()"
                " func.func private @consume(" + type + ")"
                " func.func @calculate(%a: " + type + ", %b: " + type + ", %total: " + type +
                ", %address: !llvm.ptr, %ok: i1, %n: i64) -> " + type + " {" + body + " } }");
            ASSERT_TRUE(high);
            ASSERT_TRUE(mlir::succeeded(mlir::verify(*high)));
            const auto before = print(*high);
            combine_gloin_float_checks(*high);
            EXPECT_EQ(print(*high), before);
        }
    }
}

TEST(LoweringTest, GIRFloatCheckCombiningPreservesProductFailureWithPoisonOrUndefAddend) {
    for (const std::string type : {"f32", "f64"}) {
        for (const std::string other : {"poison", "undef"}) {
            SCOPED_TRACE(type + " " + other);
            mlir::MLIRContext context;
            auto high = parse(context, "module { func.func @main() -> i32 {"
                " %other = llvm.mlir." + other + " : " + type +
                " %a = arith.constant " + (type == "f32" ? "3.0e+38" : "1.0e+308") + " : " + type +
                " %b = arith.constant 2.0 : " + type +
                " %zero = arith.constant 0 : i32"
                " %p = \"gloin.checked_float_binary\"(%a, %b) <{kind = \"*\"}> : (" +
                type + ", " + type + ") -> " + type +
                " %s = \"gloin.checked_float_binary\"(%other, %p) <{kind = \"+\"}> : (" +
                type + ", " + type + ") -> " + type + " return %zero : i32 } }");
            ASSERT_TRUE(high);
            Diagnostics diagnostics;
            auto low = lower_to_llvm(std::move(high), diagnostics);
            ASSERT_TRUE(low) << render(diagnostics);
            expect_exportable(*low);
            llvm::LLVMContext llvm_context;
            auto exported = mlir::translateModuleToLLVMIR(*low, llvm_context);
            ASSERT_TRUE(exported);
            llvm::LoopAnalysisManager loops;
            llvm::FunctionAnalysisManager functions;
            llvm::CGSCCAnalysisManager calls;
            llvm::ModuleAnalysisManager modules;
            llvm::PassBuilder passes;
            passes.registerLoopAnalyses(loops);
            passes.registerFunctionAnalyses(functions);
            passes.registerCGSCCAnalyses(calls);
            passes.registerModuleAnalyses(modules);
            passes.crossRegisterProxies(loops, functions, calls, modules);
            auto optimizer = passes.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            optimizer.run(*exported, modules);
            ASSERT_FALSE(llvm::verifyModule(*exported, &llvm::errs()));
            unsigned traps = 0;
            for (const auto &block : *exported->getFunction("main"))
                for (const auto &op : block)
                    if (auto call = llvm::dyn_cast<llvm::CallBase>(&op))
                        if (auto callee = call->getCalledFunction())
                            traps += callee->getIntrinsicID() == llvm::Intrinsic::trap;
            // The original always traps on multiplication before using the
            // other addend. Introducing poison UB here could erase that trap.
            EXPECT_EQ(traps, 1u);
        }
    }
}

TEST(LoweringTest, GIRVersionsCountedSliceLoopsAndRetainsEachIterationsFloatCheck) {
    for (const std::string type : {"i64", "u64"}) {
        SCOPED_TRACE(type);
        mlir::MLIRContext context;
        auto high = compile_source(
            "def dot(a: [const f64], b: [const f64], byte: u8, n: " + type + ") -> f64 {"
            " if byte >= 64 { return 0.0; }"
            " def mut total: f64 = 0.0;"
            " for def mut i: " + type + " = 0; i < n; i = i + 1 {"
            " byte * 4; total = total + a[i] * b[i]; } return total; }",
            "versioned-dot.gloin", context);
        ASSERT_TRUE(high.success()) << render(*high.diagnostics);
        unsigned high_products = 0;
        high.module->walk([&](gloin::CheckedFloatBinaryOp op) { high_products += op.getKind() == "*"; });
        EXPECT_EQ(high_products, 1u);
        Diagnostics diagnostics;
        auto low = lower_to_llvm(std::move(high.module), diagnostics);
        ASSERT_TRUE(low) << render(diagnostics);
        expect_exportable(*low);
        unsigned products = 0, sums = 0, bounds = 0, entry_checks = 0;
        unsigned finite_checks = 0, overflow = 0;
        low->walk([&](mlir::LLVM::FMulOp op) {
            ++products;
            EXPECT_EQ(op.getFastmathFlags(), mlir::LLVM::FastmathFlags::none);
        });
        low->walk([&](mlir::LLVM::FAddOp op) {
            ++sums;
            EXPECT_EQ(op.getFastmathFlags(), mlir::LLVM::FastmathFlags::none);
        });
        low->walk([&](mlir::LLVM::FCmpOp) { ++finite_checks; });
        low->walk([&](mlir::LLVM::ICmpOp op) {
            bounds += op.getPredicate() == mlir::LLVM::ICmpPredicate::ult;
            entry_checks += op.getPredicate() == mlir::LLVM::ICmpPredicate::ule;
        });
        low->walk([&](mlir::Operation *op) {
            overflow += mlir::isa<mlir::LLVM::SAddWithOverflowOp, mlir::LLVM::UAddWithOverflowOp>(op);
            EXPECT_FALSE(mlir::isa<mlir::LLVM::UMulWithOverflowOp>(op)); // inherited byte-range proof
        });
        EXPECT_EQ(products, 2u) << print(*low); // checked fast and fallback copies
        EXPECT_EQ(sums, 2u);
        EXPECT_EQ(finite_checks, 4u); // one combined product/sum check in each loop copy
        EXPECT_EQ(entry_checks, 2u);
        EXPECT_EQ(bounds, type == "u64" ? 4u : 2u); // plus unsigned loop headers
        EXPECT_EQ(overflow, type == "u64" ? 0u : 1u); // original signed fallback
    }
}

TEST(LoweringTest, GIRSliceLoopVersioningRejectsUnprovenInductionAndMutableDescriptors) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def change(i: &i64) -> void { *i = -1; }
        def escaped(a: [const f64], n: i64) -> f64 {
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 1 {
                change(&i);
                total = total + a[i];
            }
            return total;
        }
        def negative(a: [const f64], n: i64) -> f64 {
            def mut total: f64 = 0.0;
            for def mut i: i64 = -1; i < n; i = i + 1 { total = total + a[i]; }
            return total;
        }
        def stride(a: [const f64], n: i64) -> f64 {
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 2 { total = total + a[i]; }
            return total;
        }
        def descriptor(rows: [const [f64]], n: i64) -> f64 {
            def mut total: f64 = 0.0;
            for def mut i: i64 = 0; i < n; i = i + 1 {
                def a: [f64] = rows[0];
                total = total + a[i];
            }
            return total;
        }
    )", "unversioned-loops.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    expect_exportable(*compiled.module);
    for (const char *name : {"escaped", "negative", "stride", "descriptor"}) {
        SCOPED_TRACE(name);
        auto function = compiled.module->lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
        unsigned sums = 0, traps = 0;
        function.walk([&](mlir::LLVM::FAddOp) { ++sums; });
        function.walk([&](mlir::LLVM::Trap) { ++traps; });
        EXPECT_EQ(sums, 1u);
        EXPECT_GT(traps, 0u);
    }
}

TEST(LoweringTest, GIRVersionsStridedSlicesAndJoinsTailCounters) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def pack(output: [f64], n: u64, value: f64) -> u64 {
            def mut i: u64 = 0;
            def mut j: u64 = 0;
            def mut total: u64 = 0;
            while i + 3 <= n {
                output[j] = value + value;
                output[j + 1] = value;
                output[j + 1 + 1] = value;
                output[j + 3] = value;
                i = i + 3;
                j = j + 4;
                total = total + 1;
            }
            return i + j + total;
        }
    )", "strided-slices.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    expect_exportable(*compiled.module); // Includes dominance of live values at the shared tail.
    unsigned sums = 0, finite_checks = 0, bounds = 0, divisions = 0, overflow = 0;
    compiled.module->walk([&](mlir::LLVM::FAddOp op) {
        ++sums;
        EXPECT_EQ(op.getFastmathFlags(), mlir::LLVM::FastmathFlags::none);
    });
    compiled.module->walk([&](mlir::LLVM::FCmpOp) { ++finite_checks; });
    compiled.module->walk([&](mlir::LLVM::ICmpOp op) {
        bounds += op.getPredicate() == mlir::LLVM::ICmpPredicate::ult;
    });
    compiled.module->walk([&](mlir::LLVM::UDivOp) { ++divisions; });
    compiled.module->walk([&](mlir::LLVM::UAddWithOverflowOp) { ++overflow; });
    EXPECT_EQ(sums, 2u); // Both copies retain checked floating arithmetic.
    EXPECT_EQ(finite_checks, 4u);
    EXPECT_EQ(bounds, 4u); // Four original guards; none in the fast copy.
    EXPECT_EQ(divisions, 2u); // Entry uses n/3 and capacity/4, never groups*4.
    EXPECT_GT(overflow, 0u); // Unrelated sums and the checked fallback remain.
}

TEST(LoweringTest, GIRStridedVersioningRejectsUnprovenCountersAndDescriptors) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"nonzero primary", "def mut i: u64 = 1; def mut j: u64 = 0; while i + 3 <= n {"
                            " output[j] = x + x; i = i + 3; j = j + 4; }"},
        {"variable stride", "def mut i: u64 = 0; def mut j: u64 = 0; while i + step <= n {"
                            " output[j] = x + x; i = i + step; j = j + 4; }"},
        {"different backedge", "def mut i: u64 = 0; def mut j: u64 = 0; while i + 3 <= n {"
                               " output[j] = x + x; i = i + 2; j = j + 4; }"},
        {"nonzero secondary", "def mut i: u64 = 0; def mut j: u64 = 1; while i + 3 <= n {"
                              " output[j] = x + x; i = i + 3; j = j + 4; }"},
        {"offset outside group", "def mut i: u64 = 0; def mut j: u64 = 0; while i + 3 <= n {"
                                 " output[j + 4] = x + x; i = i + 3; j = j + 4; }"},
        {"escaped secondary", "def mut i: u64 = 0; def mut j: u64 = 0; while i + 3 <= n {"
                              " change(&j); output[j] = x + x; i = i + 3; j = j + 4; }"},
        {"loaded descriptor", "def mut i: u64 = 0; def mut j: u64 = 0; while i + 3 <= n {"
                              " def row: [f64] = rows[0]; row[j] = x + x; i = i + 3; j = j + 4; }"},
        {"conditional body", "def mut i: u64 = 0; def mut j: u64 = 0; while i + 3 <= n {"
                             " if x > 0.0 { output[j] = x + x; } i = i + 3; j = j + 4; }"},
        {"signed primary", "def mut i: i64 = 0; def mut j: u64 = 0; while i + 3 <= signed_n {"
                           " output[j] = x + x; i = i + 3; j = j + 4; }"}
    };
    for (const auto &[name, body] : cases) {
        SCOPED_TRACE(name);
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def change(j: &u64) -> void { *j = 0; }"
            "def pack(output: [f64], rows: [const [f64]], n: u64, step: u64, signed_n: i64, x: f64) -> void {" +
            body + "}", "unproven-strides.gloin", context,
            CompilationMode::Module, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        expect_exportable(*compiled.module);
        unsigned sums = 0, traps = 0;
        compiled.module->walk([&](mlir::LLVM::FAddOp) { ++sums; });
        compiled.module->walk([&](mlir::LLVM::Trap) { ++traps; });
        EXPECT_EQ(sums, 1u);
        EXPECT_GT(traps, 0u);
    }
}

TEST(LoweringTest, MatchVerifiesCoverageKeysAndYieldType) {
    for (int invalid = 0; invalid < 4; ++invalid) {
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def main() -> i32 { return match true { case true => 42; case false => 0; }; }",
            "match.gloin", context);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        gloin::MatchOp matching;
        compiled.module->walk([&](gloin::MatchOp op) { matching = op; });
        ASSERT_TRUE(matching);
        mlir::OpBuilder builder(&context);
        if (invalid == 0)
            matching.setKeysAttr(
                builder.getArrayAttr({builder.getBoolAttr(true), builder.getBoolAttr(true)}));
        else if (invalid == 1)
            matching.setKeysAttr(
                builder.getArrayAttr({builder.getI32IntegerAttr(1), builder.getI32IntegerAttr(0)}));
        else if (invalid == 2)
            matching.setDomainSizeAttr(builder.getI64IntegerAttr(3));
        else {
            auto &arm = matching.getArms().front();
            auto yield = mlir::cast<gloin::MatchYieldOp>(arm.back().getTerminator());
            builder.setInsertionPoint(yield);
            auto wrong = builder.create<gloin::ConstantOp>(yield.getLoc(), builder.getI64Type(),
                                                           builder.getI64IntegerAttr(42));
            yield->setOperand(0, wrong);
        }
        Diagnostics diagnostics;
        EXPECT_FALSE(verify_module(*compiled.module, diagnostics));
        EXPECT_TRUE(diagnostics.has_errors());
    }
}

TEST(LoweringTest, SelectionRejectsLayoutSubjectsAtCheckedBoundary) {
    for (bool matching : {false, true}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(
            matching
                ? "def main() -> i32 { return match \"x\" { case \"x\" => 42; default => 0; }; }"
                : "def main() -> i32 { switch \"x\" { case \"x\" { return 42; } default { return "
                  "0; } } }",
            "selection.gloin", context);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        mlir::Operation *selection = nullptr;
        compiled.module->walk([&](mlir::Operation *op) {
            if (mlir::isa<gloin::MatchOp, gloin::SwitchOp>(op))
                selection = op;
        });
        ASSERT_NE(selection, nullptr);
        mlir::OpBuilder builder(selection);
        auto layout = mlir::LLVM::LLVMStructType::getLiteral(
            &context, {mlir::LLVM::LLVMPointerType::get(&context), builder.getI64Type()});
        auto untyped = builder.create<gloin::ToLayoutOp>(selection->getLoc(), layout,
                                                         selection->getOperand(0));
        selection->setOperand(0, untyped);
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(compiled.module), diagnostics));
        EXPECT_NE(render(diagnostics).find("requires a source-typed subject"), std::string::npos);
    }
}

TEST(LoweringTest, StringMatchDeclaresBorrowedStorageReadEffects) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def main() -> i32 { return match \"x\" { case \"x\" => 42; default => 0; }; }",
        "match-effects.gloin", context);
    ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
    gloin::MatchOp matching;
    compiled.module->walk([&](gloin::MatchOp op) { matching = op; });
    ASSERT_TRUE(matching);
    llvm::SmallVector<mlir::MemoryEffects::EffectInstance> effects;
    matching.getEffects(effects);
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_TRUE(mlir::isa<mlir::MemoryEffects::Read>(effects.front().getEffect()));
}

TEST(LoweringTest, ErrorDiagnosticsRejectMalformedStorageAndCodeWidths) {
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        mlir::MLIRContext context;
        auto compiled =
            compile_source("def main() -> i32 { def problem: error = error(\"failed\", 7, 9); "
                           "return problem.code + problem.os_error; }",
                           "error-layout.gloin", context);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        gloin::ErrorLiteralOp literal;
        compiled.module->walk([&](gloin::ErrorLiteralOp op) { literal = op; });
        ASSERT_TRUE(literal);
        mlir::OpBuilder builder(literal);
        auto text = mlir::LLVM::LLVMStructType::getLiteral(
            &context, {mlir::LLVM::LLVMPointerType::get(&context), builder.getI64Type()});
        if (invalid == 0)
            literal.setLayoutType(text); // The old message-only layout is no longer an error.
        else if (invalid == 1)
            literal.setLayoutType(mlir::LLVM::LLVMStructType::getLiteral(
                &context, {text, builder.getI64Type(), builder.getI32Type()}));
        else if (invalid == 2 || invalid == 3) {
            auto wide = builder.create<gloin::ConstantOp>(literal.getLoc(), builder.getI64Type(),
                                                          builder.getI64IntegerAttr(1));
            literal->setOperand(invalid - 1, wide);
        } else {
            gloin::ErrorCodeOp code;
            compiled.module->walk([&](gloin::ErrorCodeOp op) { code = op; });
            code->setOperand(0, literal.getMessage());
        }
        Diagnostics diagnostics;
        EXPECT_FALSE(verify_module(*compiled.module, diagnostics));
        EXPECT_TRUE(diagnostics.has_errors());
    }
}

TEST(LoweringTest, ErrorAccessRejectsStorageBeforeSourceBoundary) {
    for (const auto *field : {"message", "code", "os_error"}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def main() -> i32 { def problem: error = error(\"failed\", 7, 9); def member: " +
                std::string(std::string(field) == "message" ? "string" : "i32") + " = problem." +
                field + "; return 0; }",
            "error-boundary.gloin", context);
        ASSERT_TRUE(compiled.success()) << render(*compiled.diagnostics);
        gloin::ErrorLiteralOp literal;
        mlir::Operation *access = nullptr;
        compiled.module->walk([&](mlir::Operation *op) {
            if (auto value = mlir::dyn_cast<gloin::ErrorLiteralOp>(op))
                literal = value;
            if (mlir::isa<gloin::ErrorMessageOp, gloin::ErrorCodeOp, gloin::ErrorOsErrorOp>(op))
                access = op;
        });
        ASSERT_TRUE(literal);
        ASSERT_NE(access, nullptr);
        mlir::OpBuilder builder(access);
        auto storage = builder.create<gloin::ToLayoutOp>(access->getLoc(), literal.getLayoutType(),
                                                         access->getOperand(0));
        access->setOperand(0, storage);
        // The message accessor also has a source-typed result; use the matching layout
        // so this is valid intermediate IR, rejected specifically at the input boundary.
        if (auto message = mlir::dyn_cast<gloin::ErrorMessageOp>(access)) {
            auto source_type = message.getMessage().getType();
            message.getMessage().setType(message.getLayoutType());
            builder.setInsertionPointAfter(access);
            auto restored = builder.create<gloin::FromLayoutOp>(access->getLoc(), source_type,
                                                                message.getMessage());
            message.getMessage().replaceAllUsesExcept(restored, restored);
        }
        Diagnostics diagnostics;
        EXPECT_FALSE(lower_to_llvm(std::move(compiled.module), diagnostics));
        EXPECT_NE(render(diagnostics).find("checked error access requires a source-typed error"),
                  std::string::npos)
            << render(diagnostics);
    }
}
