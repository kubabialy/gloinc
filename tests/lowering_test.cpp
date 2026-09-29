#include "codegen.h"
#include "compiler.h"
#include "lowering.h"
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

TEST(LoweringTest, EveryCoreScalarSignatureAndInternalWideArithmeticExports) {
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
    EXPECT_NE(print(*result.module).find("i128"), std::string::npos);
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
