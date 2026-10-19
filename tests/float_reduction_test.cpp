#include "codegen.h"
#include "compiler.h"
#include "gir_range_proofs.h"
#include "lowering.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include <gtest/gtest.h>

namespace {
std::string replace(std::string text, const std::string &from, const std::string &to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}
std::string reduction(const std::string &type, bool sliced = false) {
    std::string text = R"(
module {
  func.func private @escape(!llvm.ptr)
  func.func private @effect()
  func.func @dot(%n: i64, %external: !llvm.ptr) -> $F {
    %a = "gloin.stack_alloc"() <{element_type = !llvm.array<4 x $F>, source_type = !gloin.array<$F, 4>}> : () -> !llvm.ptr
    %b = "gloin.stack_alloc"() <{element_type = !llvm.array<4 x $F>, source_type = !gloin.array<$F, 4>}> : () -> !llvm.ptr
    %acc = "gloin.stack_alloc"() <{element_type = $F, source_type = $F}> : () -> !llvm.ptr
    %init = llvm.mlir.zero : !llvm.array<4 x $F>
    "gloin.store"(%init, %a) <{source_type = !gloin.array<$F, 4>}> : (!llvm.array<4 x $F>, !llvm.ptr) -> ()
    "gloin.store"(%init, %b) <{source_type = !gloin.array<$F, 4>}> : (!llvm.array<4 x $F>, !llvm.ptr) -> ()
    %zero = arith.constant 0 : i64
    %four = arith.constant 4 : i64
    %z = arith.constant 0.0 : $F
    "gloin.store"(%z, %acc) <{source_type = $F}> : ($F, !llvm.ptr) -> ()
    $SLICES
    // entry
    cf.br ^loop(%zero : i64)
  ^loop(%i: i64):
    %more = "gloin.checked_int_compare"(%i, %n) <{kind = "<", is_signed = true}> : (i64, i64) -> i1
    cf.cond_br %more, ^body, ^exit
  ^body:
    %t = "gloin.load"(%acc) <{source_type = $F, layout_type = $F}> : (!llvm.ptr) -> $F
    $ADDRESS_A
    %x = "gloin.load"(%pa) <{source_type = $F, layout_type = $F}> : (!llvm.ptr) -> $F
    $ADDRESS_B
    %y = "gloin.load"(%pb) <{source_type = $F, layout_type = $F}> : (!llvm.ptr) -> $F
    %p = "gloin.checked_float_binary"(%x, %y) <{kind = "*"}> : ($F, $F) -> $F
    %s = "gloin.checked_float_binary"(%t, %p) <{kind = "+"}> : ($F, $F) -> $F
    "gloin.store"(%s, %acc) <{source_type = $F}> : ($F, !llvm.ptr) -> ()
    // body
    %one = arith.constant 1 : i64
    %next = "gloin.checked_int_binary"(%i, %one) <{kind = "+", is_signed = true}> : (i64, i64) -> i64
    cf.br ^loop(%next : i64)
  ^exit:
    %result = "gloin.load"(%acc) <{source_type = $F, layout_type = $F}> : (!llvm.ptr) -> $F
    return %result : $F
  }
})";
    std::string slices;
    for (const std::string name : {"a", "b"}) {
        std::string address;
        if (sliced) {
            slices += "%slice_" + name + " = \"gloin.slice_from_array\"(%" + name +
                ", %zero, %four) <{array_type = !llvm.array<4 x $F>, source_type = !gloin.slice<$F, true>,"
                " layout_type = !llvm.struct<(ptr, i64)>}> : (!llvm.ptr, i64, i64) -> !llvm.struct<(ptr, i64)>\n";
            address = "%p" + name + " = \"gloin.slice_element_address\"(%slice_" + name +
                ", %i) <{element_type = $F, source_type = !gloin.slice<$F, true>,"
                " layout_type = !llvm.struct<(ptr, i64)>}> : (!llvm.struct<(ptr, i64)>, i64) -> !llvm.ptr";
        } else {
            address = "%p" + name + " = \"gloin.array_element_address\"(%" + name +
                ", %i) <{array_type = !llvm.array<4 x $F>, source_type = !gloin.array<$F, 4>}> : (!llvm.ptr, i64) -> !llvm.ptr";
        }
        text = replace(text, name == "a" ? "$ADDRESS_A" : "$ADDRESS_B", address);
    }
    return replace(replace(text, "$SLICES", slices), "$F", type);
}
mlir::OwningOpRef<mlir::ModuleOp> parse(mlir::MLIRContext &context, const std::string &text) {
    CodeGen dialects(context);
    context.allowUnregisteredDialects();
    return mlir::parseSourceString<mlir::ModuleOp>(text, &context);
}
std::string print(mlir::ModuleOp module) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream);
    return text;
}
} // namespace

TEST(LoweringTest, GIRDefersLocalDotChecksAndKeepsOriginalFallback) {
    for (const std::string type : {"f32", "f64"}) {
        for (bool sliced : {false, true}) {
            for (bool discarded : {false, true}) {
                SCOPED_TRACE(type + (sliced ? " slices" : " arrays") + (discarded ? " discarded" : " returned"));
                mlir::MLIRContext context;
                auto text = reduction(type, sliced);
                if (discarded) text = replace(text, "return %result", "return %z");
                auto module = parse(context, text);
                ASSERT_TRUE(module);
                const auto proofs = prove_gloin_ranges(*module);
                ASSERT_TRUE(mlir::succeeded(mlir::verify(*module)));
                EXPECT_EQ(proofs.versioned_reductions.size(), 2u);
                unsigned final_checks = 0, checked = 0, products = 0, sums = 0;
                module->walk([&](gloin::RequireFiniteOp op) {
                    ++final_checks;
                    EXPECT_EQ(op->getBlock()->getOperations().size(), 3u); // load, check, exit
                });
                module->walk([&](gloin::CheckedFloatBinaryOp) { ++checked; });
                module->walk([&](mlir::arith::MulFOp op) {
                    ++products;
                    EXPECT_EQ(op.getFastmath(), mlir::arith::FastMathFlags::none);
                });
                module->walk([&](mlir::arith::AddFOp op) {
                    ++sums;
                    EXPECT_EQ(op.getFastmath(), mlir::arith::FastMathFlags::none);
                });
                EXPECT_EQ(final_checks, 1u);
                EXPECT_EQ(checked, 2u); // Original multiplication and addition stay checked.
                EXPECT_EQ(products, 1u);
                EXPECT_EQ(sums, 1u);
                EXPECT_EQ(proofs.bounds.size(), 2u);
                EXPECT_EQ(proofs.arithmetic.size(), 1u);
            }
        }
    }
}

TEST(LoweringTest, GIRReductionRejectsMissingStorageEffectsAndOtherFailures) {
    for (const std::string type : {"f32", "f64"}) {
        const auto base = reduction(type);
        const std::string init = "\"gloin.store\"(%init, %a) <{source_type = !gloin.array<" + type +
            ", 4>}> : (!llvm.array<4 x " + type + ">, !llvm.ptr) -> ()";
        std::vector<std::pair<std::string, std::string>> cases{
            {"uninitialized storage", replace(base, init, "")},
            {"unrepresentable storage extent", replace(replace(base, "4 x ", "4611686018427387904 x "), ", 4>", ", 4611686018427387904>")},
            {"32-bit addresses", replace(base, "module {", "module attributes {llvm.data_layout = \"e-p:32:32\"} {")},
            {"32-bit pointer indices", replace(base, "module {", "module attributes {llvm.data_layout = \"e-p:64:64:64:32\"} {")},
            {"nonstandard float stride", replace(base, "module {", "module attributes {llvm.data_layout = \"e-f32:64-f64:128\"} {")},
            {"ended stack lifetime", replace(replace(base, "%a = \"gloin.stack_alloc\"", "%saved = llvm.intr.stacksave : !llvm.ptr\n %a = \"gloin.stack_alloc\""), "// entry", "llvm.intr.stackrestore %saved : !llvm.ptr")},
            {"late initialization", replace(replace(base, init, ""), "^exit:", "^exit:\n" + init)},
            {"conditional initialization", replace(replace(base, init, ""), "// entry",
                "%condition = arith.cmpi eq, %n, %zero : i64\n cf.cond_br %condition, ^initialize, ^join\n"
                " ^initialize:\n" + init + "\n cf.br ^join\n ^join:")},
            {"unknown storage", replace(base, "\"gloin.array_element_address\"(%a,", "\"gloin.array_element_address\"(%external,")},
            {"escaped input", replace(base, "// entry", "func.call @escape(%a) : (!llvm.ptr) -> ()")},
            {"escaped accumulator", replace(base, "// entry", "func.call @escape(%acc) : (!llvm.ptr) -> ()")},
            {"nonzero initial accumulator", replace(base, "%z = arith.constant 0.0", "%z = arith.constant 1.0")},
            {"nonzero counter", replace(base, "cf.br ^loop(%zero", "cf.br ^loop(%four")},
            {"stride two", replace(base, "%one = arith.constant 1", "%one = arith.constant 2")},
            {"call", replace(base, "// body", "func.call @effect() : () -> ()")},
            {"unknown operation", replace(base, "// body", "\"unknown.effect\"() : () -> ()")},
            {"unsafe read", replace(base, "// body", "%unsafe = llvm.load %external : !llvm.ptr -> " + type)},
            {"volatile read", replace(base, "// body", "%unsafe = llvm.load volatile %external : !llvm.ptr -> " + type)},
            {"observable store", replace(base, "// body", "llvm.store %s, %external : " + type + ", !llvm.ptr")},
            {"integer failure", replace(base, "// body", "%bad = \"gloin.checked_int_binary\"(%n, %n) <{kind = \"*\", is_signed = true}> : (i64, i64) -> i64")},
            {"assertion", replace(base, "// body", "\"gloin.assert\"(%more) : (i1) -> ()")},
            {"division", replace(base, "kind = \"*\"", "kind = \"/\"")},
            {"escaping product", replace(base, "// body", "llvm.store %p, %external : " + type + ", !llvm.ptr")},
            {"escaping counter", replace(base, "return %result", "%outside = arith.addi %i, %n : i64\n return %result")},
            {"escaping condition", replace(base, "return %result", "%outside = arith.xori %more, %more : i1\n return %result")},
            {"reset accumulator", replace(base, "// body", "\"gloin.store\"(%z, %acc) <{source_type = " + type + "}> : (" + type + ", !llvm.ptr) -> ()")},
            {"conditional exit", replace(base, "cf.br ^loop(%next : i64)", "cf.cond_br %more, ^loop(%next : i64), ^exit")},
        };
        for (const auto &[name, text] : cases) {
            SCOPED_TRACE(type + " " + name);
            mlir::MLIRContext context;
            auto module = parse(context, text);
            ASSERT_TRUE(module);
            ASSERT_TRUE(mlir::succeeded(mlir::verify(*module)));
            auto proofs = prove_gloin_ranges(*module);
            EXPECT_TRUE(proofs.versioned_reductions.empty());
            unsigned checks = 0;
            module->walk([&](gloin::RequireFiniteOp) { ++checks; });
            EXPECT_EQ(checks, 0u);
        }
    }
}

TEST(LoweringTest, GIRFiniteRequirementIsEffectfulAndVerifiesWidths) {
    for (const std::string type : {"f32", "f64"}) {
        mlir::MLIRContext context;
        auto module = parse(context, "module { func.func @check(%x: " + type +
            ") { \"gloin.require_finite\"(%x) : (" + type + ") -> () return } }");
        ASSERT_TRUE(module);
        Diagnostics diagnostics;
        auto low = lower_to_llvm(std::move(module), diagnostics);
        ASSERT_TRUE(low);
        unsigned checks = 0, traps = 0;
        low->walk([&](mlir::LLVM::FCmpOp) { ++checks; });
        low->walk([&](mlir::LLVM::Trap) { ++traps; });
        EXPECT_EQ(checks, 2u);
        EXPECT_EQ(traps, 1u);
    }
    for (const std::string type : {"i32", "f16", "vector<2xf64>"}) {
        mlir::MLIRContext context;
        mlir::ScopedDiagnosticHandler silence(&context, [](mlir::Diagnostic &) { return mlir::success(); });
        EXPECT_FALSE(parse(context, "module { func.func @check(%x: " + type +
            ") { \"gloin.require_finite\"(%x) : (" + type + ") -> () return } }"));
    }
}

TEST(LoweringTest, GIRLocalReductionSourceKeepsOneCheckAfterFastLoop) {
    for (const std::string type : {"f32", "f64"}) {
        for (bool sliced : {false, true}) {
            mlir::MLIRContext context;
            std::string source = "def dot(n: i64, x: " + type + ", y: " + type + ") -> " + type + " {"
                " def a: [" + type + "; 8] = {x; 8}; def b: [" + type + "; 8] = {y; 8};"
                " def mut total: " + type + " = 0.0;";
            if (sliced) source += " def p: [const " + type + "] = a[..]; def r: [const " + type + "] = b[..];";
            source += " for def mut i: i64 = 0; i < n; i = i + 1 { total = total + " +
                std::string(sliced ? "p[i] * r[i]" : "a[i] * b[i]") + "; } return total; }";
            auto high = compile_source(source, "reduction.gloin", context);
            ASSERT_TRUE(high.success());
            Diagnostics diagnostics;
            auto low = lower_to_llvm(std::move(high.module), diagnostics);
            ASSERT_TRUE(low);
            unsigned sums = 0, unchecked_sums = 0;
            low->walk([&](mlir::LLVM::FAddOp op) {
                ++sums;
                unsigned comparisons = 0;
                for (auto &other : *op->getBlock()) comparisons += mlir::isa<mlir::LLVM::FCmpOp>(other);
                unchecked_sums += comparisons == 0;
            });
            EXPECT_EQ(sums, 2u) << print(*low);
            EXPECT_EQ(unchecked_sums, 1u) << print(*low);
        }
    }
}

TEST(LoweringTest, GIRDeferredReductionRetainsFailureBeforeLaterPoisonOrUndef) {
    for (const std::string type : {"f32", "f64"}) {
        for (const std::string undefined : {"poison", "undef"}) {
            SCOPED_TRACE(type + " " + undefined);
            auto text = replace(reduction(type), "@dot(%n: i64, %external: !llvm.ptr)", "@main()");
            text = replace(text, "%init = llvm.mlir.zero", "%n = arith.constant 2 : i64\n %init = llvm.mlir.zero");
            const auto array = "!llvm.array<4 x " + type + ">";
            const auto values = "%huge = arith.constant " + std::string(type == "f32" ? "3.0e38" : "1.0e308") +
                " : " + type + "\n %two = arith.constant 2.0 : " + type +
                "\n %undefined = llvm.mlir." + undefined + " : " + type +
                "\n %first = llvm.insertvalue %huge, %init[0] : " + array +
                "\n %bad = llvm.insertvalue %undefined, %first[1] : " + array +
                "\n %bfirst = llvm.insertvalue %two, %init[0] : " + array +
                "\n %btwo = llvm.insertvalue %two, %bfirst[1] : " + array + "\n";
            text = replace(text, "\"gloin.store\"(%init, %a)", values + "\"gloin.store\"(%bad, %a)");
            text = replace(text, "\"gloin.store\"(%init, %b)", "\"gloin.store\"(%btwo, %b)");
            text = replace(text, "return %result", "return %z");
            mlir::MLIRContext context;
            auto module = parse(context, text);
            ASSERT_TRUE(module);
            Diagnostics diagnostics;
            auto low = lower_to_llvm(std::move(module), diagnostics);
            ASSERT_TRUE(low);
            unsigned freezes = 0;
            low->walk([&](mlir::LLVM::FreezeOp) { ++freezes; });
            EXPECT_GE(freezes, 2u); // Both later input values must remain concrete.
            mlir::registerBuiltinDialectTranslation(context);
            mlir::registerLLVMDialectTranslation(context);
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
            unsigned traps = 0, returns = 0;
            for (const auto &block : *exported->getFunction("main"))
                for (const auto &op : block) {
                    returns += llvm::isa<llvm::ReturnInst>(op);
                    if (auto call = llvm::dyn_cast<llvm::CallBase>(&op))
                        if (auto callee = call->getCalledFunction())
                            traps += callee->getIntrinsicID() == llvm::Intrinsic::trap;
                }
            EXPECT_EQ(traps, 1u);
            EXPECT_EQ(returns, 0u); // This must fail, regardless of the later value.
        }
    }
}
