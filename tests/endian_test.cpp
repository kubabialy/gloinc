#include "codegen.h"
#include "compiler.h"
#include "lowering.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "target_layout.h"

namespace {
class EndianTest : public gloin_test::CliFixture {};
std::string diagnostics(const Diagnostics &errors) {
    std::ostringstream text;
    errors.render(text);
    return text.str();
}
} // namespace

TEST_F(EndianTest, ArithmeticAndValueConversionsRetainTheirTypes) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def convert(value: be_u32) -> result<le_u32> {
            def converted: result<le_u32> = le_u32(value + 1);
            if converted.erroneous { return converted.error; }
            return converted.value;
        }
        def constant() -> u64 {
            def const value: be_u64 = 18446744073709551615;
            def converted: result<u64> = u64(value);
            if converted.erroneous { return 0; }
            return converted.value;
        }
    )",
                                   "endian.gloin", context);
    ASSERT_TRUE(compiled.success()) << diagnostics(*compiled.diagnostics);
    unsigned arithmetic = 0, conversions = 0;
    compiled.module->walk([&](gloin::CheckedIntegerBinaryOp op) {
        auto type = mlir::dyn_cast<gloin::GloinIntegerType>(op.getType());
        ASSERT_TRUE(type);
        EXPECT_EQ(type.getEndianness(), gloin::Endianness::Big);
        EXPECT_EQ(type.getSignedness(), gloin::IntegerSignedness::Unsigned);
        EXPECT_EQ(type, op.getLhs().getType());
        EXPECT_EQ(type, op.getRhs().getType());
        ++arithmetic;
    });
    compiled.module->walk([&](gloin::IntegerConvertOp op) {
        EXPECT_TRUE(mlir::isa<gloin::GloinResultType>(op.getType()));
        ++conversions;
    });
    EXPECT_EQ(arithmetic, 1u);
    EXPECT_EQ(conversions, 2u);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
}

TEST_F(EndianTest, RejectsImplicitMixingAndRepresentationReinterpretation) {
    for (const std::string text :
         {"def f(x: be_u32) -> le_u32 { return x; }", "def f(x: be_u32) -> u32 { return x; }",
          "def f(x: le_u32) -> u32 { return x; }",
          "def f(x: be_u32, y: le_u32) -> bool { return x == y; }",
          "def f(x: &be_u32) -> &le_u32 { return x; }",
          "def f(x: [be_u32]) -> [le_u32] { return x; }",
          "def f(x: &be_u32) -> le_u32 { return le_u32(x); }",
          "def f() -> le_u32 { return le_u32(); }", "def f() -> le_u32 { return le_u32(1, 2); }",
          "def f() -> le_u32 { return le_u32(-1); }", "def f() -> be_u16 { return 65536; }",
          "def f() -> be_u128 { return 1; }", "def f() -> u32_be { return 1; }"}) {
        SCOPED_TRACE(text);
        mlir::MLIRContext context;
        auto compiled = compile_source(text, "endian.gloin", context);
        EXPECT_FALSE(compiled.success());
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic)
            << diagnostics(*compiled.diagnostics);
    }
}

TEST_F(EndianTest, LoweringUsesTargetByteOrderAndConversionsDoNotSwapValues) {
    for (const std::string type : {"be_u32", "le_u32", "be_i64", "le_i64", "be_u8", "le_i8"}) {
        for (bool little : {false, true}) {
            SCOPED_TRACE(type + (little ? " little target" : " big target"));
            mlir::MLIRContext context;
            auto compiled =
                compile_source("def read(p: &" + type + ") -> " + type + " { return *p; }",
                               "endian.gloin", context);
            ASSERT_TRUE(compiled.success()) << diagnostics(*compiled.diagnostics);
            auto layout = (*compiled.module)
                              ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                              .getValue()
                              .str();
            layout[0] = little ? 'e' : 'E';
            (*compiled.module)
                ->setAttr("llvm.data_layout", mlir::StringAttr::get(&context, layout));
            auto lowered = lower_to_llvm(std::move(compiled.module), *compiled.diagnostics);
            ASSERT_TRUE(lowered) << diagnostics(*compiled.diagnostics);
            unsigned swaps = 0;
            lowered->walk([&](mlir::LLVM::ByteSwapOp) { ++swaps; });
            EXPECT_EQ(swaps,
                      (!type.ends_with("8") && (type.starts_with("le_") != little)) ? 1u : 0u);
        }
    }
    mlir::MLIRContext context;
    auto compiled =
        compile_source("def convert(x: be_u32) -> le_u32 { def r: result<le_u32> = le_u32(x); if "
                       "r.erroneous { return 0; } return r.value; }",
                       "endian.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << diagnostics(*compiled.diagnostics);
    compiled.module->walk(
        [&](mlir::LLVM::ByteSwapOp) { ADD_FAILURE() << "value conversion swapped numeric bits"; });
}

TEST_F(EndianTest, MaterializedPacketCopyIsBytewiseWithoutEndianWork) {
    mlir::MLIRContext context;
    auto compiled =
        compile_source(R"(
        def struct Packet { def tag: u8, def mut count: be_u32, def tail: u8, }
        def copy(output: &Packet, input: &const Packet) -> void { *output = *input; }
    )",
                       "copy.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << diagnostics(*compiled.diagnostics);
    unsigned copies = 0;
    compiled.module->walk([&](mlir::LLVM::MemmoveOp) { ++copies; });
    compiled.module->walk(
        [&](mlir::LLVM::ByteSwapOp) { ADD_FAILURE() << "packet copy re-encoded a field"; });
    EXPECT_EQ(copies, 1u);
}

TEST_F(EndianTest, LayoutMeasurementsDoNotRetainTypesFromDestroyedLLVMContexts) {
    mlir::MLIRContext context;
    CodeGen dialects(context);
    auto target = native_target_layout();
    ASSERT_TRUE(static_cast<bool>(target));
    llvm::DataLayout layout(target->data_layout);
    auto byte = mlir::IntegerType::get(&context, 8);
    auto word = mlir::IntegerType::get(&context, 32);
    auto padding = mlir::LLVM::LLVMArrayType::get(byte, 3);
    auto original = mlir::LLVM::LLVMStructType::getLiteral(&context, {byte, word, byte});
    auto explicit_padding =
        mlir::LLVM::LLVMStructType::getLiteral(&context, {byte, padding, word, byte, padding});
    for (unsigned i = 0; i < 64; ++i) {
        auto first = measure_type_layout(original, layout);
        auto second = measure_type_layout(explicit_padding, layout);
        ASSERT_TRUE(static_cast<bool>(first));
        ASSERT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(first->size, 12u);
        EXPECT_EQ(second->size, first->size);
        EXPECT_EQ(second->alignment, first->alignment);
        EXPECT_EQ(first->field_offsets, (std::vector<uint64_t>{0, 4, 8}));
        EXPECT_EQ(second->field_offsets, (std::vector<uint64_t>{0, 1, 4, 8, 9}));
    }
}

TEST_F(EndianTest, GIRRejectsUnsupportedWidthsAndForgedIntegerOperations) {
    const std::string big = "!gloin.int<32, Big, Unsigned>";
    const std::string little = "!gloin.int<32, Little, Unsigned>";
    for (const std::string body : std::vector<std::string>{
             "func.func @bad(%a: !gloin.int<7, Big, Unsigned>) { return }",
             "func.func @bad(%a: " + big + ", %b: " + little + ") -> " + big +
                 " { %r = \"gloin.checked_int_binary\"(%a, %b) <{kind = \"+\", is_signed = false}> "
                 ": (" +
                 big + ", " + little + ") -> " + big + " return %r : " + big + " }",
             "func.func @bad(%a: " + big + ") -> " + big +
                 " { %r = \"gloin.checked_int_binary\"(%a, %a) <{kind = \"+\", is_signed = true}> "
                 ": (" +
                 big + ", " + big + ") -> " + big + " return %r : " + big + " }",
             "func.func @bad() -> " + big +
                 " { %r = \"gloin.constant\"() <{value = 1 : i16}> : () -> " + big +
                 " return %r : " + big + " }"}) {
        SCOPED_TRACE(body);
        mlir::MLIRContext context;
        CodeGen dialects(context);
        mlir::ScopedDiagnosticHandler silence(&context,
                                              [](mlir::Diagnostic &) { return mlir::success(); });
        EXPECT_FALSE(mlir::parseSourceString<mlir::ModuleOp>("module { " + body + " }", &context));
    }
    mlir::MLIRContext context;
    auto compiled = compile_source("def convert(x: be_u32) -> le_u32 { def r: result<le_u32> = "
                                   "le_u32(x); if r.erroneous { return 0; } return r.value; }",
                                   "conversion.gloin", context);
    ASSERT_TRUE(compiled.success());
    compiled.module->walk([&](gloin::IntegerConvertOp conversion) {
        mlir::OpBuilder builder(conversion);
        auto layout = builder.create<gloin::ToLayoutOp>(conversion.getLoc(), builder.getI32Type(),
                                                        conversion.getInput());
        conversion.getInputMutable().assign(layout);
    });
    EXPECT_FALSE(lower_to_llvm(std::move(compiled.module), *compiled.diagnostics));
    EXPECT_NE(diagnostics(*compiled.diagnostics).find("source-typed"), std::string::npos);

    CodeGen dialects(context);
    auto untyped_copy = mlir::parseSourceString<mlir::ModuleOp>(R"(
        module attributes {gloin.checked} {
            func.func @bad(%a: !llvm.ptr, %b: !llvm.ptr) {
                "gloin.copy"(%a, %b) <{source_type = !gloin.array<i8, 4>,
                    layout_type = !llvm.array<4 x i8>}> : (!llvm.ptr, !llvm.ptr) -> ()
                return
            }
        }
    )",
                                                                &context);
    ASSERT_TRUE(untyped_copy);
    Diagnostics copy_errors;
    EXPECT_FALSE(lower_to_llvm(std::move(untyped_copy), copy_errors));
    EXPECT_NE(diagnostics(copy_errors).find("source-typed addresses"), std::string::npos);

    auto foreign = mlir::parseSourceString<mlir::ModuleOp>(R"(
        module attributes {gloin.checked} {
            llvm.func @foreign(!llvm.struct<(i8, i32)>)
        }
    )",
                                                           &context);
    ASSERT_TRUE(foreign);
    Diagnostics errors;
    EXPECT_FALSE(lower_to_llvm(std::move(foreign), errors));
    EXPECT_NE(diagnostics(errors).find("foreign ABI"), std::string::npos);
}

TEST_F(EndianTest, FallibleConversionsVerifyResultTypesAndEncodeForBothTargets) {
    for (const std::string type : {"be_u32", "le_u32"}) {
        for (bool little : {false, true}) {
            SCOPED_TRACE(type + (little ? " little target" : " big target"));
            mlir::MLIRContext context;
            auto compiled = compile_source(
                "def convert(value: u64) -> result<" + type + "> { def r: result<" + type +
                    "> = " + type + "(value); if r.erroneous { return r.error; } return r.value; }",
                "conversion.gloin", context);
            ASSERT_TRUE(compiled.success()) << diagnostics(*compiled.diagnostics);
            auto layout = (*compiled.module)
                              ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                              .getValue()
                              .str();
            layout[0] = little ? 'e' : 'E';
            (*compiled.module)
                ->setAttr("llvm.data_layout", mlir::StringAttr::get(&context, layout));
            auto lowered = lower_to_llvm(std::move(compiled.module), *compiled.diagnostics);
            ASSERT_TRUE(lowered) << diagnostics(*compiled.diagnostics);
            unsigned swaps = 0;
            lowered->walk([&](mlir::LLVM::ByteSwapOp) { ++swaps; });
            // Construct the conversion result, extract its scalar, then wrap the return.
            // LLVM optimization may cancel the intermediate encode/decode pair.
            EXPECT_EQ(swaps, (type.starts_with("le_") != little) ? 3u : 0u);
            lowered->walk([&](mlir::LLVM::CallOp call) {
                if (call.getCallee())
                    EXPECT_EQ(call.getCallee()->find("trap"), llvm::StringRef::npos);
            });
        }
    }
    for (bool erase_source : {false, true}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def convert(x: u64) -> result<be_u32> { def r: result<be_u32> = be_u32(x); "
            "if r.erroneous { return r.error; } return r.value; }",
            "forged.gloin", context);
        ASSERT_TRUE(compiled.success());
        compiled.module->walk([&](gloin::IntegerConvertOp conversion) {
            if (erase_source)
                conversion.getResult().setType(conversion.getLayoutType());
            else
                conversion.setTargetType(gloin::GloinIntegerType::get(
                    &context, 32, gloin::Endianness::Little, gloin::IntegerSignedness::Unsigned));
        });
        EXPECT_FALSE(lower_to_llvm(std::move(compiled.module), *compiled.diagnostics));
    }
}
