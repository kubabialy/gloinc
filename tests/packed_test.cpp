#include "compiler.h"
#include "dialect/GloinDialect.h"
#include "lowering.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Verifier.h"
#include "native_output.h"
#include "numeric.h"
#include "support/cli_fixture.h"

namespace {
class PackedTest : public gloin_test::CliFixture {};
std::string errors(const Diagnostics &diagnostics) {
    std::ostringstream output;
    diagnostics.render(output);
    return output.str();
}
} // namespace

TEST_F(PackedTest, EveryNativeWidthHasExactConstantBoundsAndCanonicalIdentity) {
    for (unsigned width = 1; width <= 64; ++width) {
        for (bool signed_value : {false, true}) {
            auto name = std::string(signed_value ? "i" : "u") + std::to_string(width);
            SCOPED_TRACE(name);
            auto type = resolve_core_type(name);
            ASSERT_TRUE(type);
            EXPECT_EQ(core_type_info(*type).bits, width);
            EXPECT_EQ(core_type_info(*type).is_signed, signed_value);
            EXPECT_EQ(core_type_info(*type).name, name);
            uint64_t maximum = signed_value
                                   ? (uint64_t{1} << (width - 1)) - 1
                                   : (width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1);
            mlir::MLIRContext context;
            auto compiled = compile_source(
                "def f() -> " + name + " { return " + std::to_string(maximum) + "; }",
                "narrow.gloin", context, CompilationMode::Module, CompilationOutput::LLVM);
            ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
            if (maximum < UINT64_MAX) {
                auto rejected = compile_source("def f() -> " + name + " { return " +
                                                   std::to_string(maximum + 1) + "; }",
                                               "range.gloin", context);
                EXPECT_FALSE(rejected.success());
                EXPECT_EQ(rejected.failed_stage, DiagnosticStage::Semantic);
            }
        }
    }
    EXPECT_EQ(resolve_core_type("i32"), CoreType::I32);
    EXPECT_EQ(resolve_core_type("u64"), CoreType::U64);
}

TEST_F(PackedTest, IntegerConstructorsCheckEveryWidthSignAndByteOrderBoundaryPair) {
    std::vector<CoreType> types;
    for (unsigned width = 1; width <= 64; ++width)
        for (bool sign : {false, true})
            types.push_back(native_integer_type(width, sign));
    for (const auto &type : core_types)
        if (type.byte_order != ByteOrder::Native)
            types.push_back(type.id);
    auto bounds = [](CoreType type) {
        const auto &info = core_type_info(type);
        __int128 extent = __int128{1} << (info.bits - (info.is_signed ? 1 : 0));
        return std::pair<__int128, __int128>{info.is_signed ? -extent : 0, extent - 1};
    };
    for (auto source : types) {
        const auto &from = core_type_info(source);
        auto [source_min, source_max] = bounds(source);
        for (auto target : types) {
            const auto &to = core_type_info(target);
            auto [target_min, target_max] = bounds(target);
            for (auto number : {__int128{0}, __int128{1}, __int128{-1}, source_min, source_max,
                                target_min - 1, target_min, target_max, target_max + 1}) {
                if (number < source_min || number > source_max)
                    continue;
                auto value = convert_integer_constant(
                    {source, llvm::APInt(from.bits, static_cast<uint64_t>(number))}, target);
                bool fits = number >= target_min && number <= target_max;
                ASSERT_EQ(value.has_value(), fits) << from.name << " -> " << to.name;
                if (value) {
                    const auto &bits = std::get<llvm::APInt>(value->value);
                    __int128 actual = to.is_signed ? __int128{bits.getSExtValue()}
                                                   : __int128{bits.getZExtValue()};
                    ASSERT_TRUE(actual == number) << from.name << " -> " << to.name;
                }
            }
        }
    }
}

TEST_F(PackedTest, RejectsInvalidLayoutsReferencesAndConstantConversions) {
    for (const std::string text :
         {"def packed struct H { def x: u13, }", "def packed struct(native) H { def x: u13, }",
          "def packed struct(msb_first) H { def x: u13, def y: be_u32, }",
          "def packed struct(msb_first) H { def x: u13 at 0, }",
          "def packed struct(msb_first) H { def x: *u8, }",
          "def packed struct(msb_first) H { def x: bool, }",
          "def packed struct(msb_first) H { def x: [be_u32; 2], }",
          "def struct A { def x: u32, } def packed struct(msb_first) H { def x: A, }",
          "def packed struct(msb_first) H { def x: be_u13, }",
          "def packed struct(msb_first) H { def x: u0, }",
          "def packed struct(msb_first) H { def x: u65, }",
          "def packed struct(msb_first) H { def x: u13, } def f(h: &H) -> &const u13 { return "
          "&h.x; }",
          "def packed struct(msb_first) H { def mut x: be_u32, } def f(h: &H) -> &be_u32 { return "
          "&h.x; }",
          "def f() -> u3 { return u3(u16(8)); }",
          "def f() -> u13 { def const x: i16 = -1; return u13(x); }",
          "def f() -> i13 { return i13(u13(4096)); }",
          "def f() -> i3 { def const x: i3 = i3(u64(4)); return x; }",
          "def f(x: u13) -> u16 { return x; }",
          "def f(x: u13) -> u13 { def y: u16 = 1; return x + y; }"}) {
        SCOPED_TRACE(text);
        mlir::MLIRContext context;
        auto compiled = compile_source(text, "invalid.gloin", context);
        EXPECT_FALSE(compiled.success());
        EXPECT_TRUE(compiled.failed_stage == DiagnosticStage::Parsing ||
                    compiled.failed_stage == DiagnosticStage::Semantic)
            << errors(*compiled.diagnostics);
    }
}

TEST_F(PackedTest, CheckedConversionsAndExactWidthArithmeticTrapEvenWhenUnused) {
    const std::vector<std::string> programs = {
        "def f(x: u16) -> void { u13(x); } def main() -> i32 { f(8192); return 0; }",
        "def f(x: i16) -> void { u13(x); } def main() -> i32 { f(-1); return 0; }",
        "def f(x: u13) -> void { i13(x); } def main() -> i32 { f(4096); return 0; }",
        "def f(x: u64) -> void { i64(x); } def main() -> i32 { f(18446744073709551615); return 0; "
        "}",
        "def f(x: u13) -> void { x + 1; } def main() -> i32 { f(8191); return 0; }",
        "def f(x: i13) -> void { x - 1; } def main() -> i32 { f(-4096); return 0; }",
        "def f(x: u3) -> void { x * 2; } def main() -> i32 { f(4); return 0; }",
        "def f(x: i3) -> void { -x; } def main() -> i32 { f(-4); return 0; }",
        "def f(x: i1) -> void { x / -1; } def main() -> i32 { f(-1); return 0; }",
        "def f(x: i1) -> void { x * -1; } def main() -> i32 { f(-1); return 0; }",
        "def f(x: i1) -> void { x % -1; } def main() -> i32 { f(-1); return 0; }",
        "def f(x: u1) -> void { x + 1; } def main() -> i32 { f(1); return 0; }",
    };
    for (const auto &text : programs) {
        SCOPED_TRACE(text);
        auto path = source(text);
        for (const auto *optimization : {"-O0", "-O2"}) {
            SCOPED_TRACE(optimization);
            auto executable = directory + "/trap";
            auto built = invoke_raw({optimization, "-o", executable, path});
            ASSERT_EQ(built.status, 0) << built.err;
            EXPECT_LT(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 0);
        }
    }
}

TEST_F(PackedTest, NarrowValuesWorkThroughArithmeticArraysGenericsAndResults) {
    auto path = source(R"(
        def twice<T>(x: T) -> T { return x + x; }
        def narrow(x: i16) -> i13 { return i13(x); }
        def number(x: i13) -> result<i13> { return x; }
        def main() -> i32 {
            def mut values: [i13; 3] = {-1, -4096, 4095};
            values[0] = twice<i13>(narrow(-2));
            if values[0] != -4 || i64(values[1]) != -4096 || u16(values[2]) != 4095 { return 1; }
            def value: result<i13> = number(values[1]);
            if value.erroneous { return 2; }
            if value.value != -4096 { return 3; }
            def a: u1 = u1(u8(1));
            def b: i1 = i1(i8(-1));
            if u16(a) != 1 || i16(b) != -1 || !(b < 0) { return 4; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/narrow";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, NarrowSignedArithmeticCannotInheritErasedUnsignedRangeProofs) {
    std::string text = R"(
        def packed struct(msb_first) P {
            def mut prefix: u7, def mut wide: u64, def mut signed: i13,
            def mut flags: u4, def sequence: be_u32, def check: le_u16, def tail: u3,
        }
    )";
    // Lowering a proven unsigned operation frees its address. New exact-width
    // operations must not inherit its proof when the allocator reuses that
    // address. Multiple functions exercise reuse within a single compilation.
    for (unsigned i = 0; i < 64; ++i)
        text += "def changed" + std::to_string(i) + R"((p: &P) -> void {
            p.prefix = p.prefix % 119 + 3;
            p.wide = p.wide / 3 + 17;
            p.signed = p.signed / 2;
            p.flags = p.flags % 13 + 1;
        })";
    text += "def main() -> i32 {";
    for (unsigned i = 0; i < 64; ++i) {
        auto name = "p" + std::to_string(i);
        text += "def mut " + name + R"(: P = P {
            prefix: 0, wide: 9259542123273814144, signed: -4080,
            flags: 1, sequence: 16843009, check: 257, tail: 0
        }; changed)" +
                std::to_string(i) + "(&" + name + "); if " + name +
                ".signed != -2040 { return 1; }";
    }
    text += "return 42; }";
    mlir::MLIRContext context;
    auto compiled = compile_source(text, "proof-lifetime.gloin", context,
                                   CompilationMode::Executable, CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    unsigned signed_divisions = 0;
    compiled.module->walk([&](mlir::LLVM::SDivOp op) {
        if (op.getType().isInteger(13))
            ++signed_divisions;
    });
    EXPECT_EQ(signed_divisions, 64u);
    compiled.module->walk([&](mlir::LLVM::UDivOp op) { EXPECT_FALSE(op.getType().isInteger(13)); });
    auto path = source(text);
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/proof-lifetime";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedIrRetainsSchemaAndRejectsForgedOffsetsAndAddresses) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def packed struct(msb_first) Header {
            def mut length: u13, def mut flags: u3, def sequence: be_u32,
        }
        def read(p: &Header) -> u13 { return p.length; }
        def write(p: &Header, value: u3) -> void { p.flags = value; }
    )",
                                   "packed.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::StructDefinitionOp record;
    compiled.module->walk([&](gloin::StructDefinitionOp op) { record = op; });
    ASSERT_TRUE(record);
    auto offsets = record->getAttrOfType<mlir::DenseI64ArrayAttr>("packed_offsets");
    ASSERT_TRUE(offsets);
    EXPECT_EQ(offsets.asArrayRef(), (llvm::ArrayRef<int64_t>{0, 13, 16}));
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    record->setAttr("packed_offsets", mlir::DenseI64ArrayAttr::get(&context, {0, 12, 16}));
    EXPECT_TRUE(mlir::failed(mlir::verify(*compiled.module)));
    record->setAttr("packed_offsets", offsets);
    compiled.module->walk([&](gloin::PackedLoadOp load) {
        mlir::OpBuilder builder(load);
        auto pointer =
            gloin::GloinPointerType::get(&context, load.getValue().getType(), false, true);
        auto forged = builder.create<gloin::FieldAddressOp>(
            load.getLoc(), pointer, load.getAddress(), mlir::TypeAttr::get(record.getLayoutType()),
            builder.getI64IntegerAttr(0), load.getSourceTypeAttr(),
            mlir::TypeAttr::get(load.getValue().getType()));
        EXPECT_TRUE(mlir::failed(mlir::verify(*compiled.module)));
        forged.erase();
    });
    compiled.module->walk([&](gloin::PackedStoreOp op) {
        op.setIndex(2); // The sequence is immutable and has a different value type.
    });
    EXPECT_TRUE(mlir::failed(mlir::verify(*compiled.module)));
}

TEST_F(PackedTest, ForeignValuesRequireAdaptersWhilePackedPointersKeepTheirAbi) {
    for (const auto *type : {"Header", "u13", "&Header"}) {
        SCOPED_TRACE(type);
        mlir::MLIRContext context;
        auto compiled = compile_source(
            "def packed struct(msb_first) Header { def x: u13, } def foreign(value: " +
                std::string(type) + ") -> void {}",
            "foreign.gloin", context);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
        auto function = compiled.module->lookupSymbol<mlir::func::FuncOp>("foreign");
        ASSERT_TRUE(function);
        function.getBody().dropAllReferences();
        function.getBody().getBlocks().clear();
        function.setPrivate();
        auto lowered = lower_to_llvm(std::move(compiled.module), *compiled.diagnostics);
        if (std::string(type) == "&Header") {
            EXPECT_TRUE(lowered) << errors(*compiled.diagnostics);
        } else {
            EXPECT_FALSE(lowered);
            EXPECT_NE(errors(*compiled.diagnostics).find("foreign ABI"), std::string::npos);
        }
    }
}

TEST_F(PackedTest, CopiesStayBytewiseAndPackedAccessDoesNotDependOnTargetByteOrder) {
    for (bool little : {false, true}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(R"(
            def packed struct(msb_first) Header {
                def mut length: u13, def flags: u3, def mut sequence: be_u32,
            }
            def copy(dst: &Header, src: &const Header) -> void { *dst = *src; }
            def read(src: &const Header) -> be_u32 { return src.sequence; }
            def write(dst: &Header, value: u13) -> void { dst.length = value; }
        )",
                                       "packed.gloin", context);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
        auto layout = (*compiled.module)
                          ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                          .getValue()
                          .str();
        layout[0] = little ? 'e' : 'E';
        (*compiled.module)->setAttr("llvm.data_layout", mlir::StringAttr::get(&context, layout));
        auto lowered = lower_to_llvm(std::move(compiled.module), *compiled.diagnostics);
        ASSERT_TRUE(lowered) << errors(*compiled.diagnostics);
        unsigned copies = 0;
        lowered->walk([&](mlir::LLVM::MemmoveOp) { ++copies; });
        EXPECT_EQ(copies, 1u);
        auto copy = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("copy");
        copy.walk([&](mlir::LLVM::ByteSwapOp) { ADD_FAILURE() << "copies must preserve bytes"; });
        auto read = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("read");
        unsigned swaps = 0;
        read.walk([&](mlir::LLVM::ByteSwapOp) { ++swaps; });
        EXPECT_EQ(swaps, little ? 1u : 0u);
        read.walk([&](mlir::LLVM::LoadOp op) {
            EXPECT_TRUE(op.getType().isInteger(32));
            EXPECT_EQ(op.getAlignment(), 1u);
        });
        auto write = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>("write");
        write.walk([&](mlir::LLVM::LoadOp op) {
            EXPECT_TRUE(op.getType().isInteger(16));
            EXPECT_EQ(op.getAlignment(), 1u);
        });
        write.walk([&](mlir::LLVM::StoreOp op) {
            EXPECT_TRUE(op.getValue().getType().isInteger(16));
            EXPECT_EQ(op.getAlignment(), 1u);
        });
    }
}

TEST_F(PackedTest, NineByteFieldsUseBoundedChunksWithExplicitTargetByteOrder) {
    for (bool little : {false, true}) {
        for (bool msb : {false, true}) {
            SCOPED_TRACE(little);
            SCOPED_TRACE(msb);
            mlir::MLIRContext context;
            auto compiled =
                compile_source("def packed struct(" + std::string(msb ? "msb_first" : "lsb_first") +
                                   ") Wide { def prefix: u7, def mut value: u64, } "
                                   "def read(p: &const Wide) -> u64 { return p.value; } "
                                   "def write(p: &Wide, value: u64) -> void { p.value = value; }",
                               "wide.gloin", context);
            ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
            auto layout = (*compiled.module)
                              ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                              .getValue()
                              .str();
            layout[0] = little ? 'e' : 'E';
            (*compiled.module)
                ->setAttr("llvm.data_layout", mlir::StringAttr::get(&context, layout));
            auto lowered = lower_to_llvm(std::move(compiled.module), *compiled.diagnostics);
            ASSERT_TRUE(lowered) << errors(*compiled.diagnostics);
            for (const auto *name : {"read", "write"}) {
                auto function = lowered->lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
                std::vector<unsigned> loads, stores;
                unsigned swaps = 0;
                function.walk([&](mlir::LLVM::LoadOp op) {
                    loads.push_back(mlir::cast<mlir::IntegerType>(op.getType()).getWidth());
                    EXPECT_EQ(op.getAlignment(), 1u);
                });
                function.walk([&](mlir::LLVM::StoreOp op) {
                    stores.push_back(
                        mlir::cast<mlir::IntegerType>(op.getValue().getType()).getWidth());
                    EXPECT_EQ(op.getAlignment(), 1u);
                });
                function.walk([&](mlir::LLVM::ByteSwapOp) { ++swaps; });
                EXPECT_EQ(loads, (std::vector<unsigned>{64, 8}));
                bool writing = std::string(name) == "write";
                EXPECT_EQ(stores,
                          (writing ? std::vector<unsigned>{64, 8} : std::vector<unsigned>{}));
                EXPECT_EQ(swaps, msb == little ? (writing ? 2u : 1u) : 0u);
            }
        }
    }
}

TEST_F(PackedTest, FieldsAcrossBytesAndWordsStayInsideGuardPagesAndPreserveOtherBits) {
    std::string program;
    std::ostringstream harness;
    harness << R"(
        #include <stdint.h>
        #include <string.h>
        #include <sys/mman.h>
        #include <unistd.h>
        typedef uint64_t (*Update)(unsigned char *, uint64_t);
        struct Case { Update update; unsigned width, offset, msb, sign; };
    )";
    std::ostringstream cases;
    unsigned count = 0;
    for (unsigned width : {1, 3, 7, 8, 9, 13, 17, 25, 31, 32, 33, 41, 49, 63, 64}) {
        for (unsigned offset = 0; offset < 8; ++offset) {
            for (bool msb : {false, true}) {
                for (bool sign : {false, true}) {
                    auto id = std::to_string(count++);
                    auto name = "field_" + id;
                    auto type = std::string(sign ? "i" : "u") + std::to_string(width);
                    auto wide = sign ? "i64" : "u64";
                    program += "def packed struct(" + std::string(msb ? "msb_first" : "lsb_first") +
                               ") Record" + id + " { ";
                    if (offset)
                        program += "def prefix: u" + std::to_string(offset) + ", ";
                    program += "def mut value: " + type + ", }\n";
                    program += "def " + name + "(p: &Record" + id + ", value: " + wide + ") -> " +
                               wide + " { p.value = " + type + "(value); return " + wide +
                               "(p.value); }\n";
                    harness << "extern uint64_t " << name << "(unsigned char *, uint64_t);\n";
                    cases << "{" << name << "," << width << "," << offset << "," << msb << ","
                          << sign << "},\n";
                }
            }
        }
    }
    harness << "static struct Case cases[] = {\n" << cases.str() << "};\n";
    harness << R"(
        // The field-only module does not use the process-argument runtime.
        void *gloin_process_arguments_push(int64_t argc, void *argv) {
            static char scope; (void)argc; (void)argv; return &scope;
        }
        int32_t gloin_process_arguments_pop(void *scope) { (void)scope; return 0; }
        int guard_driver(void) {
            long page = sysconf(_SC_PAGESIZE);
            if (page <= 0) return 1;
            unsigned char *pages = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (pages == MAP_FAILED || mprotect(pages + page, page, PROT_READ | PROT_WRITE)) return 2;
            uint64_t random = UINT64_C(0x0123456789abcdef);
            for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
                struct Case t = cases[c];
                unsigned size = (t.offset + t.width + 7) / 8;
                uint64_t mask = t.width == 64 ? UINT64_MAX : (UINT64_C(1) << t.width) - 1;
                for (unsigned sample = 0; sample < 70; ++sample) {
                    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
                    uint64_t bits = (sample < 64 ? UINT64_C(1) << sample : random) & mask;
                    if (sample == 64) bits = mask;
                    if (sample == 65) bits = 0;
                    uint64_t value = bits;
                    if (t.sign && (bits & (UINT64_C(1) << (t.width - 1)))) value |= ~mask;
                    for (unsigned edge = 0; edge < 2; ++edge) {
                        unsigned char *p = edge ? pages + page * 2 - size : pages + page;
                        unsigned char expected[9];
                        for (unsigned i = 0; i < size; ++i) p[i] = expected[i] = (unsigned char)(random >> ((i % 8) * 8));
                        // Independent per-bit oracle; the compiler lowers byte-sized segments.
                        for (unsigned j = 0; j < t.width; ++j) {
                            unsigned stream = t.offset + j;
                            unsigned physical = t.msb ? 7 - stream % 8 : stream % 8;
                            unsigned numeric = t.msb ? t.width - 1 - j : j;
                            unsigned char bit = 1u << physical;
                            expected[stream / 8] = (expected[stream / 8] & ~bit) |
                                (((bits >> numeric) & 1) ? bit : 0);
                        }
                        if (t.update(p, value) != value) return 3;
                        if (memcmp(p, expected, size)) return 4;
                    }
                }
            }
            return munmap(pages, page * 3) ? 5 : 0;
        }
    )";
    auto harness_path = source(harness.str(), "guard.c");
    auto cc = llvm::sys::findProgramByName("cc");
    ASSERT_TRUE(static_cast<bool>(cc)) << cc.getError().message();
    for (auto optimization : {NativeOptimization::O0, NativeOptimization::O2}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "fields.gloin", context, CompilationMode::Module,
                                       CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
        // Add the entry after lowering, keeping every independently called
        // field function alive. The native emitter supplies its normal main ABI.
        mlir::OpBuilder builder(&context);
        builder.setInsertionPointToEnd(compiled.module->getBody());
        auto entry_type = mlir::LLVM::LLVMFunctionType::get(builder.getI32Type(), {});
        auto driver = builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(),
                                                             "guard_driver", entry_type);
        auto main =
            builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "main", entry_type);
        builder.setInsertionPointToStart(main.addEntryBlock(builder));
        auto result =
            builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), driver, mlir::ValueRange{});
        builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), result.getResults());
        auto object = directory + "/fields.o";
        ASSERT_TRUE(emit_native(*compiled.module, object, NativeOutput::Object, optimization,
                                gloin_test::gloinc, *compiled.diagnostics))
            << errors(*compiled.diagnostics);
        auto executable = directory + "/guard";
        ASSERT_EQ(llvm::sys::ExecuteAndWait(*cc, {*cc, harness_path, object, "-o", executable},
                                            std::nullopt, {}, 30),
                  0);
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 30), 0);
    }
}
