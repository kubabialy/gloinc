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

TEST_F(PackedTest, PackedProofsRetainTrapsAfterAliasedIndexDescriptorAndValueMutation) {
    for (const auto *body :
         {"def mut index: u64 = 0; if index < view.len { change(&index, view.len); view[index]; }",
          "def mut borrowed: packed(msb_first) [const u3] = view; if borrowed.len > 0 { "
          "shrink(&borrowed); borrowed[0]; }",
          "def readonly: packed(msb_first) [const u3] = view; if readonly[0] < 2 { view[0] = 7; "
          "readonly[0] * 2; }"}) {
        auto file = source(std::string(R"(
            def change(index: &u64, value: u64) -> void { *index = value; }
            def shrink(view: &packed(msb_first) [const u3]) -> void { *view = (*view)[..0]; }
            def main() -> i32 {
                def mut array: packed(msb_first) [u3; 8] = {1; 8};
                def view: packed(msb_first) [u3] = array[..];
        )") + body + " return 42; }");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        for (const auto *mode : {"-O0", "-O2"}) {
            auto executable = directory + "/packed-alias";
            auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err;
            EXPECT_LT(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 0);
        }
    }
}

TEST_F(PackedTest, PackedCopyPreservesOverlapRecordTailsAndFailureAtomicity) {
    auto path = source(R"(
        import "@memory";
        import "@arena";
        import "@status";
        def packed struct(msb_first) Item { def x: u3, }
        def packed struct(msb_first) Empty {}
        def main() -> i32 {
            def mut values: packed(msb_first) [u3; 8] = {0, 1, 2, 3, 4, 5, 6, 7};
            def view: packed(msb_first) [u3] = values[..];
            def copied: result<void> = memory.copy_packed<packed(msb_first) [u3]>(view[1..], view[..7]);
            if copied.erroneous { return 1; }
            if values[0] != 0 || values[1] != 0 || values[2] != 1 || values[7] != 6 { return 2; }
            def reverse: result<void> = memory.copy_packed<packed(msb_first) [u3]>(view[..7], view[1..]);
            if reverse.erroneous { return 3; }
            if values[0] != 0 || values[1] != 1 || values[6] != 6 || values[7] != 6 { return 4; }
            def mismatch: result<void> = memory.copy_packed<packed(msb_first) [u3]>(view[5..6], view[..2]);
            if !mismatch.erroneous { return 5; }
            if mismatch.error.code != status.OUT_OF_RANGE || mismatch.error.os_error != 0 || values[5] != 5 { return 6; }
            def mut bytes: [u8; 6] = {165, 167, 95, 225, 127, 90};
            def r: result<packed(lsb_first) [Item]> = memory.view_packed<packed(lsb_first) [Item]>(bytes[1..5], 0, 4);
            if r.erroneous { return 7; }
            def records: packed(lsb_first) [Item] = r.value;
            def moved: result<void> = memory.copy_packed<packed(lsb_first) [Item]>(records[1..], records[..3]);
            if moved.erroneous { return 8; }
            if bytes[0] != 165 || bytes[1] != 167 || bytes[2] != 167 || bytes[3] != 95 || bytes[4] != 225 || bytes[5] != 90 { return 9; }
            def same: result<void> = memory.copy_packed<packed(lsb_first) [Item]>(records, records);
            if same.erroneous { return 10; }
            def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
            def e: result<packed(msb_first) [Empty]> = a.alloc_slice<packed(msb_first) [Empty]>(18446744073709551615, Empty {}, 1);
            if e.erroneous { return 11; }
            def empty: packed(msb_first) [Empty] = e.value;
            def zero_copy: result<void> = memory.copy_packed<packed(msb_first) [Empty]>(empty, empty);
            if zero_copy.erroneous { return 12; }
            def no_elements: result<void> = memory.copy_packed<packed(msb_first) [u3]>(view[0..0], view[1..1]);
            if no_elements.erroneous { return 13; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/packed-copy";
        auto built = invoke_raw({mode, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedCopyMatchesIndependentBitOracleAtGuardPages) {
    std::string program = "import \"@memory\";\n";
    std::ostringstream harness, cases;
    harness << R"(
        #include <stdint.h>
        #include <stdlib.h>
        #include <string.h>
        #include <sys/mman.h>
        #include <unistd.h>
        typedef int32_t (*Copy)(unsigned char *, uint64_t, uint64_t, uint64_t);
        struct Case { Copy copy; unsigned width, msb, aligned; };
    )";
    unsigned id = 0;
    for (const auto *element :
         {"u1", "u3", "i13", "u17", "u33", "u49", "u56", "u64", "be_u32", "le_u16"}) {
        auto info = core_type_info(*resolve_core_type(element));
        for (bool msb : {false, true}) {
            auto name = "copy_" + std::to_string(id++);
            auto type =
                std::string("packed(") + (msb ? "msb_first" : "lsb_first") + ") [" + element + "]";
            program += "def " + name +
                       "(p: &[u8; 64], destination: u64, source: u64, count: u64) -> i32 { "
                       "def d: result<" +
                       type + "> = memory.view_packed<" + type +
                       ">((*p)[..], destination, count); "
                       "if d.erroneous { return 1; } def output: " +
                       type +
                       " = d.value; "
                       "def s: result<" +
                       type + "> = memory.view_packed<" + type +
                       ">((*p)[..], source, count); "
                       "if s.erroneous { return 2; } def input: " +
                       type +
                       " = s.value; "
                       "def copied: result<void> = memory.copy_packed<" +
                       type +
                       ">(output, input); "
                       "if copied.erroneous { return 3; } return 0; }\n";
            harness << "extern int32_t " << name
                    << "(unsigned char *, uint64_t, uint64_t, uint64_t);\n";
            cases << "{" << name << "," << info.bits << "," << msb << ","
                  << (info.byte_order != ByteOrder::Native) << "},\n";
        }
    }
    harness << "static struct Case cases[] = {\n" << cases.str() << "};\n";
    harness << R"(
        void *gloin_process_arguments_push(int64_t argc, void *argv) {
            static char scope; (void)argc; (void)argv; return &scope;
        }
        int32_t gloin_process_arguments_pop(void *scope) { (void)scope; return 0; }
        // Imported but unused allocation wrappers must never run during a copy.
        void *gloin_memory_alloc(int64_t bytes, int64_t alignment) { (void)bytes; (void)alignment; abort(); }
        void gloin_memory_free(void *pointer) { (void)pointer; abort(); }
        int guard_driver(void) {
            long page = sysconf(_SC_PAGESIZE);
            if (page < 64) return 1;
            unsigned char *pages = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (pages == MAP_FAILED || mprotect(pages + page, page, PROT_READ | PROT_WRITE)) return 2;
            uint64_t random = UINT64_C(0x123456789abcdef);
            for (unsigned c = 0; c < sizeof(cases)/sizeof(cases[0]); ++c) {
                struct Case test = cases[c];
                for (unsigned end = 0; end != 2; ++end) {
                    unsigned char *data = pages + page + (end ? page - 64 : 0);
                    for (unsigned count = 0; count <= 7; count += count < 1 ? 1 : 2) {
                        for (unsigned d = 0; d != 8; ++d) for (unsigned s = 0; s != 8; ++s) {
                            unsigned dst[] = {d, 8 + d, 512 - count * test.width - d};
                            unsigned src[] = {s, 8 + s, 512 - count * test.width - s};
                            for (unsigned di = 0; di != 3; ++di) for (unsigned si = 0; si != 3; ++si) {
                                unsigned destination = dst[di], source = src[si];
                                if (test.aligned && ((destination | source) % 8)) continue;
                                unsigned char original[64], expected[64];
                                for (unsigned i = 0; i != 64; ++i) {
                                    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
                                    data[i] = original[i] = expected[i] = (unsigned char)random;
                                }
                                for (unsigned bit = 0; bit < count * test.width; ++bit) {
                                    unsigned input = source + bit, output = destination + bit;
                                    unsigned shift_in = test.msb ? 7 - input % 8 : input % 8;
                                    unsigned shift_out = test.msb ? 7 - output % 8 : output % 8;
                                    unsigned value = (original[input / 8] >> shift_in) & 1;
                                    expected[output / 8] = (expected[output / 8] & ~(1u << shift_out)) | (value << shift_out);
                                }
                                if (test.copy(data, destination, source, count)) return 3;
                                if (memcmp(data, expected, sizeof(expected))) return 4;
                            }
                        }
                    }
                }
            }
            return munmap(pages, page * 3) ? 5 : 0;
        }
    )";
    auto harness_path = source(harness.str(), "copy-guard.c");
    auto cc = llvm::sys::findProgramByName("cc");
    ASSERT_TRUE(static_cast<bool>(cc)) << cc.getError().message();
    for (auto optimization : {NativeOptimization::O0, NativeOptimization::O2}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "copy-guard.gloin", context,
                                       CompilationMode::Module, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
        mlir::OpBuilder builder(&context);
        builder.setInsertionPointToEnd(compiled.module->getBody());
        auto entry = mlir::LLVM::LLVMFunctionType::get(builder.getI32Type(), {});
        auto driver =
            builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "guard_driver", entry);
        auto main = builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "main", entry);
        builder.setInsertionPointToStart(main.addEntryBlock(builder));
        auto result =
            builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), driver, mlir::ValueRange{});
        builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), result.getResults());
        auto object = directory + "/copy.o";
        ASSERT_TRUE(emit_native(*compiled.module, object, NativeOutput::Object, optimization,
                                gloin_test::gloinc, *compiled.diagnostics))
            << errors(*compiled.diagnostics);
        auto executable = directory + "/copy-guard";
        ASSERT_EQ(llvm::sys::ExecuteAndWait(*cc, {*cc, harness_path, object, "-o", executable},
                                            std::nullopt, {}, 30),
                  0);
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(PackedTest, PackedCopyRejectsEncodingChangesReadonlyDestinationsAndForgedGIR) {
    for (const auto *program :
         {"def f(a: packed(msb_first) [const u3]) -> result<void> { def copied: result<void> = "
          "memory.copy_packed<packed(msb_first) [const u3]>(a, a); if copied.erroneous { return "
          "copied.error; } return; }",
          "def f(a: packed(msb_first) [u3], b: packed(lsb_first) [u3]) -> result<void> { def "
          "copied: result<void> = memory.copy_packed<packed(msb_first) [u3]>(a, b); if "
          "copied.erroneous { return copied.error; } return; }",
          "def f(a: packed(msb_first) [be_u16], b: packed(msb_first) [le_u16]) -> result<void> { "
          "def copied: result<void> = memory.copy_packed<packed(msb_first) [be_u16]>(a, b); if "
          "copied.erroneous { return copied.error; } return; }",
          "def f(a: [u8]) -> result<void> { def copied: result<void> = memory.copy_packed<[u8]>(a, "
          "a); if copied.erroneous { return copied.error; } return; }",
          "def f(a: packed(msb_first) [u3]) -> void { __memory_copy_packed<packed(msb_first) "
          "[u3]>(a, a); }"}) {
        auto file = source(std::string("import \"@memory\"; ") + program);
        auto result = invoke({"--check", file});
        EXPECT_EQ(result.status, 1) << result.err;
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        import "@memory";
        def f(a: packed(msb_first) [u3], b: packed(msb_first) [const u3]) -> result<void> {
            def copied: result<void> = memory.copy_packed<packed(msb_first) [u3]>(a, b);
            if copied.erroneous { return copied.error; }
            return;
        }
    )",
                                   "copy-ir.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedSliceCopyOp copy;
    compiled.module->walk([&](gloin::PackedSliceCopyOp op) { copy = op; });
    ASSERT_TRUE(copy);
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    auto destination = mlir::cast<gloin::GloinPackedSliceType>(copy.getSourceType());
    auto source = copy.getSource().getType();
    auto readonly =
        gloin::GloinPackedSliceType::get(&context, destination.getElement(), true, true);
    copy.setSourceType(readonly);
    EXPECT_TRUE(mlir::failed(copy.verify()));
    copy.setSourceType(destination);
    copy.getDestination().setType(readonly);
    EXPECT_TRUE(mlir::failed(copy.verify()));
    copy.getDestination().setType(destination);
    copy.getSource().setType(
        gloin::GloinPackedSliceType::get(&context, destination.getElement(), false, true));
    EXPECT_TRUE(mlir::failed(copy.verify()));
    copy.getSource().setType(source);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
}

TEST_F(PackedTest, PackedRecordElementsPreserveBytesReferencesAndIndependentOrders) {
    auto path = source(R"(
        import "@memory";
        import "@arena";
        import "@status";
        def packed struct(msb_first) Item { def mut tag: be_u16, def mut flag: u3, }
        def packed struct(lsb_first) Container { def items: packed(lsb_first) [Item; 2], def tail: u3, }
        def pass(value: packed(lsb_first) [Item; 2]) -> packed(lsb_first) [Item; 2] { return value; }
        def main() -> i32 {
            def mut bytes: [u8; 8] = {165, 18, 52, 167, 86, 120, 95, 90};
            def r: result<packed(lsb_first) [Item]> = memory.view_packed<packed(lsb_first) [Item]>(bytes[1..7], 0, 2);
            if r.erroneous { return 1; }
            def view: packed(lsb_first) [Item] = r.value;
            def readonly: packed(lsb_first) [const Item] = view;
            def ref: &Item = &view[1];
            ref.flag = 6;
            if bytes[6] != 223 || bytes[3] != 167 || readonly[1].flag != 6 { return 2; }
            def seed: Item = view[0];
            def mut values: packed(lsb_first) [Item; 2] = {seed; 2};
            values[1] = view[1];
            def copied: packed(lsb_first) [Item; 2] = pass(values);
            def mut out: [u8; 8] = {165; 8};
            def written: result<&packed(lsb_first) [Item; 2]> = memory.place_at<packed(lsb_first) [Item; 2]>(out[..], 1, copied, 1);
            if written.erroneous { return 3; }
            if out[0] != 165 || out[1] != 18 || out[2] != 52 || out[3] != 167 || out[4] != 86 || out[5] != 120 || out[6] != 223 || out[7] != 165 { return 4; }
            def elem: &Item = &values[0];
            elem.flag = 1;
            def sub: packed(lsb_first) [Item] = values[1..];
            sub[0].flag = 2;
            if values[0].flag != 1 || values[1].flag != 2 { return 5; }
            def container: Container = Container { items: copied, tail: 5 };
            if memory.size_of<Container>() != 7 || container.items[1].flag != 6 { return 6; }
            def zero: packed(msb_first) [Item; 2] = zeroed;
            if zero[1].flag != 0 || zero[0].tag != 0 { return 7; }
            def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
            def allocated: result<packed(msb_first) [Item]> = a.alloc_slice<packed(msb_first) [Item]>(3, seed, 64);
            if allocated.erroneous { return 8; }
            def allocated_view: packed(msb_first) [Item] = allocated.value;
            def bytes_written: result<void> = memory.write_packed<Item>(out[1..4], allocated_view[2]);
            if bytes_written.erroneous { return 9; }
            if out[1] != 18 || out[2] != 52 || out[3] != 167 { return 10; }
            def split: result<memory.PackedSplit<packed(lsb_first) [Item]>> = memory.split_packed<packed(lsb_first) [Item]>(view, 1);
            if split.erroneous { return 11; }
            def left: packed(lsb_first) [Item] = split.value.left;
            left[0].flag = 3;
            if bytes[3] != 103 || bytes[0] != 165 || bytes[7] != 90 { return 12; }
            def invalid: result<packed(lsb_first) [Item]> = memory.view_packed<packed(lsb_first) [Item]>(bytes[..], 1, 0);
            if !invalid.erroneous { return 13; }
            if invalid.error.code != status.INVALID { return 14; }
            def short: result<packed(lsb_first) [Item]> = memory.view_packed<packed(lsb_first) [Item]>(bytes[1..6], 0, 2);
            if !short.erroneous { return 15; }
            if short.error.code != status.OUT_OF_RANGE { return 16; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/record-elements";
        auto built = invoke_raw({mode, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedEmptyRecordElementsKeepFullCountsAndBackedReferences) {
    auto path = source(R"(
        import "@memory";
        import "@arena";
        import "@status";
        def packed struct(msb_first) Empty {}
        def fill(calls: &u64) -> Empty { *calls = *calls + 1; return Empty {}; }
        def main() -> i32 {
            def mut calls: u64 = 0;
            def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
            def r: result<packed(msb_first) [Empty]> = a.alloc_slice<packed(msb_first) [Empty]>(18446744073709551615, fill(&calls), 8);
            if r.erroneous { return 1; }
            def view: packed(msb_first) [Empty] = r.value;
            def last: &Empty = &view[18446744073709551614];
            def tail: packed(msb_first) [Empty] = view[18446744073709551614..];
            if last != &tail[0] || tail.len != 1 || calls != 1 { return 2; }
            def parts: result<memory.PackedSplit<packed(msb_first) [Empty]>> = memory.split_packed<packed(msb_first) [Empty]>(view, 18446744073709551614);
            if parts.erroneous { return 3; }
            if parts.value.right.len != 1 { return 4; }
            def fixed: packed(lsb_first) [Empty; 1048576] = {fill(&calls); 1048576};
            if memory.size_of<packed(lsb_first) [Empty; 1048576]>() != 0 || calls != 2 { return 5; }
            def mut bytes: [u8; 1] = {165};
            def raw: result<packed(msb_first) [Empty]> = memory.view_packed<packed(msb_first) [Empty]>(bytes[..], 8, 18446744073709551615);
            if raw.erroneous { return 6; }
            def raw_view: packed(msb_first) [Empty] = raw.value;
            raw_view[18446744073709551614] = Empty {};
            def p: &Empty = &raw_view[0];
            if p != &raw_view[18446744073709551614] || bytes[0] != 165 { return 7; }
            def unbacked: result<packed(msb_first) [Empty]> = memory.view_packed<packed(msb_first) [Empty]>(bytes[0..0], 0, 1);
            if !unbacked.erroneous { return 8; }
            if unbacked.error.code != status.OUT_OF_RANGE { return 9; }
            def empty: result<packed(msb_first) [Empty]> = memory.view_packed<packed(msb_first) [Empty]>(bytes[0..0], 0, 0);
            if empty.erroneous { return 10; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/record-elements";
        auto built = invoke_raw({mode, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedRecordElementsRejectInvalidLayoutsAndCapabilities) {
    const std::string record = "def packed struct(msb_first) P { def mut x: u3, } ";
    for (const auto *program :
         {"def f(v: packed(msb_first) [const P]) -> &P { return &v[0]; }",
          "def f(v: packed(msb_first) [const P]) -> void { v[0].x = 1; }",
          "def f(v: packed(msb_first) [P]) -> &u3 { return &v[0].x; }",
          "def made() -> packed(msb_first) [P; 1] { return {P { x: 1 }}; } def f() -> &const P { "
          "return &made()[0]; }",
          "def struct Ordinary { def x: u8, } def f(v: packed(msb_first) [Ordinary]) -> void {}",
          "def packed struct(msb_first) Cycle { def next: packed(msb_first) [Cycle; 1], }"}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(record + program, "record-reject.gloin", context);
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic)
            << errors(*compiled.diagnostics);
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(record + R"(
        def f(v: packed(msb_first) [const P]) -> &const P { return &v[0]; }
        def make() -> packed(msb_first) [P; 2] { return {P { x: 1 }; 2}; }
    )",
                                   "record-ir.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedSlotAddressOp address;
    gloin::PackedArrayRepeatOp repeat;
    compiled.module->walk([&](gloin::PackedSlotAddressOp op) { address = op; });
    compiled.module->walk([&](gloin::PackedArrayRepeatOp op) { repeat = op; });
    ASSERT_TRUE(address);
    ASSERT_TRUE(repeat);
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    auto pointer = mlir::cast<gloin::GloinPointerType>(address.getAddress().getType());
    address.getAddress().setType(
        gloin::GloinPointerType::get(&context, pointer.getPointee(), false, false));
    EXPECT_TRUE(mlir::failed(address.verify()));
    address.getAddress().setType(pointer);
    auto layout = repeat.getLayoutType();
    auto bytes = mlir::LLVM::LLVMArrayType::get(mlir::IntegerType::get(&context, 8), 1);
    repeat.setLayoutType(mlir::LLVM::LLVMStructType::getLiteral(&context, {bytes}));
    EXPECT_TRUE(mlir::failed(repeat.verify()));
    repeat.setLayoutType(layout);
    gloin::StructDefinitionOp definition;
    compiled.module->walk([&](gloin::StructDefinitionOp op) { definition = op; });
    ASSERT_TRUE(definition);
    auto order = definition->getAttr("packed_order");
    definition->removeAttr("packed_order");
    EXPECT_TRUE(mlir::failed(address.verify()));
    EXPECT_TRUE(mlir::failed(repeat.verify()));
    definition->setAttr("packed_order", order);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
}

TEST_F(PackedTest, PackedArrayIdentityRejectsImplicitRepresentationChanges) {
    for (const auto &types : std::vector<std::pair<std::string, std::string>>{
             {"packed(msb_first) [u3; 3]", "packed(lsb_first) [u3; 3]"},
             {"packed(msb_first) [u3; 3]", "[u3; 3]"},
             {"packed(msb_first) [be_u16; 2]", "packed(msb_first) [le_u16; 2]"},
             {"packed(msb_first) [u3; 3]", "packed(msb_first) [u3; 4]"}}) {
        mlir::MLIRContext context;
        auto compiled =
            compile_source("def f(x: " + types.first + ") -> " + types.second + " { return x; }",
                           "identity.gloin", context);
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic);
    }
    for (const auto *element : {"bool", "f32", "*u8", "[u8; 2]"}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(std::string("def f(x: packed(msb_first) [") + element +
                                           "; 2]) -> void {}",
                                       "element.gloin", context);
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic);
    }
}

TEST_F(PackedTest, PackedArrayLiteralVerifierRejectsForgedExtentAndEncoding) {
    mlir::MLIRContext context;
    auto compiled = compile_source("def f() -> packed(msb_first) [u13; 2] { return {1, 2}; }",
                                   "array.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedArrayLiteralOp literal;
    compiled.module->walk([&](gloin::PackedArrayLiteralOp op) { literal = op; });
    ASSERT_TRUE(literal);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    auto original = literal.getLayoutType();
    auto bytes = mlir::LLVM::LLVMArrayType::get(mlir::IntegerType::get(&context, 8), 3);
    literal.setLayoutType(mlir::LLVM::LLVMStructType::getLiteral(&context, {bytes}));
    EXPECT_TRUE(mlir::failed(literal.verify()));
    literal.setLayoutType(original);
    auto source = mlir::cast<gloin::GloinPackedArrayType>(literal.getSourceType());
    literal.setSourceType(
        gloin::GloinPackedArrayType::get(&context, source.getElement(), 2, false));
    EXPECT_TRUE(mlir::failed(literal.verify()));
    literal.setSourceType(source);
    auto element = literal.getElements()[0];
    auto element_type = element.getType();
    element.setType(mlir::IntegerType::get(&context, 16));
    EXPECT_TRUE(mlir::failed(literal.verify()));
    element.setType(element_type);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
}

TEST_F(PackedTest, PackedArrayConstructionMatchesIndependentBitOracle) {
    std::ostringstream program;
    program << "import \"@memory\"; def main() -> i32 { "
               "def raw: *u8 = memory.alloc(32, 8); if raw == null { return 1; } "
               "defer memory.free(raw);\n";
    unsigned id = 0;
    for (unsigned width : {1, 3, 7, 8, 9, 13, 16, 24, 31, 32, 33, 63, 64}) {
        for (bool sign : {false, true}) {
            for (bool msb : {false, true}) {
                auto mask = llvm::APInt::getLowBitsSet(64, width).getZExtValue();
                std::vector<uint64_t> values{mask, uint64_t{1} << (width - 1),
                                             UINT64_C(0x123456789abcdef0) & mask};
                std::vector<unsigned> expected((width * 3 + 7) / 8, 0);
                for (unsigned i = 0; i < 3; ++i) {
                    for (unsigned j = 0; j < width; ++j) {
                        unsigned stream = i * width + j;
                        unsigned physical = msb ? 7 - stream % 8 : stream % 8;
                        unsigned numeric = msb ? width - 1 - j : j;
                        expected[stream / 8] |= ((values[i] >> numeric) & 1) << physical;
                    }
                }
                auto type = std::string("packed(") + (msb ? "msb_first" : "lsb_first") + ") [" +
                            (sign ? "i" : "u") + std::to_string(width) + "; 3]";
                program << "*raw = 165; *(raw + " << expected.size() + 1 << ") = 90;\n"
                        << "def p" << id << ": *" << type << " = memory.place<" << type
                        << ">(raw + 1, " << expected.size() << ", {";
                for (unsigned i = 0; i < 3; ++i) {
                    if (i)
                        program << ", ";
                    if (sign)
                        program << llvm::APInt(width, values[i]).getSExtValue();
                    else
                        program << values[i];
                }
                program << "}); if p" << id++ << " == null { return 2; }\n";
                for (unsigned i = 0; i < expected.size(); ++i)
                    program << "if *(raw + " << i + 1 << ") != " << expected[i]
                            << " { return 3; }\n";
                program << "if *raw != 165 || *(raw + " << expected.size() + 1
                        << ") != 90 { return 4; }\n";
            }
        }
    }
    program << "return 42; }";
    auto path = source(program.str());
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/array-oracle";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedArrayAccessPreservesEvaluationOrderAndNeighborBits) {
    auto path = source(R"(
        def fill(count: &u64) -> u3 { *count = *count + 1; return 5; }
        def base(order: &u64, p: &packed(msb_first) [u3; 17]) -> &packed(msb_first) [u3; 17] {
            *order = *order * 10 + 3; return p;
        }
        def index(order: &u64) -> u64 { *order = *order * 10 + 1; return 2; }
        def rhs(order: &u64) -> u3 { *order = *order * 10 + 2; return 3; }
        def made() -> packed(lsb_first) [i13; 3] { return {-4096, -1, 4095}; }
        def main() -> i32 {
            def mut count: u64 = 0;
            def empty: packed(msb_first) [u3; 0] = {fill(&count); 0};
            def mut values: packed(msb_first) [u3; 17] = {fill(&count); 17};
            if count != 2 { return 1; }
            def mut order: u64 = 0;
            (*base(&order, &values))[index(&order)] = rhs(&order);
            if order != 312 { return 2; }
            def mut i: u64 = 0;
            while i < 17 {
                if i == 2 { if values[i] != 3 { return 3; } }
                else { if values[i] != 5 { return 4; } }
                i = i + 1;
            }
            values[1] = values[2];
            if values[0] != 5 || values[1] != 3 || values[2] != 3 || values[3] != 5 { return 5; }
            if made()[0] != -4096 || made()[1] != -1 || made()[2] != 4095 { return 6; }
            def mut endian: packed(lsb_first) [be_u16; 3] = {0x1234; 3};
            endian[1] = 0x5678;
            if endian[0] != 0x1234 || endian[1] != 0x5678 || endian[2] != 0x1234 { return 7; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/access";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedArrayBoundsTrapBeforeAssignmentValue) {
    for (const auto *index : {"-1", "8", "18446744073709551615", "0"}) {
        auto index_type = std::string(index) == "-1" ? "i64" : "u64";
        auto path = source(std::string(R"(
            import "@std";
            def rhs() -> u3 { std.print("rhs"); return 1; }
            def main() -> i32 {
                def mut values: packed(lsb_first) [u3; 8] = zeroed;
                def index: )") +
                           index_type + " = " + index + "; values[index] = rhs(); return 0; }");
        if (std::string(index) == "0") {
            auto text = read(path);
            text.replace(text.find("[u3; 8]"), 7, "[u3; 0]");
            path = source(text, "empty.gloin");
        }
        auto result = invoke({path});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty());
        for (const auto *optimization : {"-O0", "-O2"}) {
            auto executable = directory + "/bounds";
            auto built = invoke_raw({optimization, "-o", executable, path});
            ASSERT_EQ(built.status, 0) << built.err;
            auto output = directory + "/output";
            const std::optional<llvm::StringRef> redirects[] = {std::nullopt, output, std::nullopt};
            EXPECT_LT(
                llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10),
                0);
            EXPECT_TRUE(read(output).empty());
        }
    }
}

TEST_F(PackedTest, PackedScalarElementsRejectAddressesAndReadonlyWrites) {
    for (const auto *program :
         {"def f(p: &packed(msb_first) [u3; 2]) -> &u3 { return &(*p)[0]; }",
          "def f(p: &const packed(msb_first) [u3; 2]) -> void { (*p)[0] = 1; }",
          "def f() -> void { def a: packed(msb_first) [u3; 2] = {1; 2}; a[0] = 2; }",
          "def take<T>(p: &T) -> void {} def f(p: &packed(msb_first) [u3; 2]) -> void { "
          "take<u3>(&(*p)[0]); }"}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "access.gloin", context);
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic)
            << errors(*compiled.diagnostics);
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def f(p: &const packed(msb_first) [u3; 2], i: u64) -> u3 { return (*p)[i]; }",
        "slot.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedArraySelectOp selection;
    compiled.module->walk([&](gloin::PackedArraySelectOp op) { selection = op; });
    ASSERT_TRUE(selection);
    auto type = mlir::cast<gloin::GloinPackedSlotType>(selection.getSlotType());
    EXPECT_TRUE(type.getReadOnly());
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    auto forged = gloin::GloinPackedSlotType::get(&context, type.getElement(), true, false);
    selection.setSlotType(forged);
    selection.getSlot().setType(forged);
    EXPECT_TRUE(mlir::failed(selection.verify()));
}

TEST_F(PackedTest, DynamicPackedArrayUpdatesMatchBitOracleAtGuardPages) {
    std::string program;
    std::ostringstream harness, cases;
    harness << R"(
        #include <stdint.h>
        #include <string.h>
        #include <sys/mman.h>
        #include <unistd.h>
        typedef uint64_t (*Update)(unsigned char *, uint64_t, uint64_t);
        struct Case { Update update; unsigned width, msb, sign; };
    )";
    unsigned id = 0;
    for (unsigned width : {1, 3, 7, 8, 9, 13, 16, 24, 31, 32, 33, 63, 64}) {
        for (bool msb : {false, true}) {
            for (bool sign : {false, true}) {
                auto name = "array_" + std::to_string(id++);
                auto scalar = std::string(sign ? "i" : "u") + std::to_string(width);
                auto wide = sign ? "i64" : "u64";
                auto array = std::string("packed(") + (msb ? "msb_first" : "lsb_first") + ") [" +
                             scalar + "; 9]";
                program +=
                    "def " + name + "(p: &" + array + ", index: u64, value: " + wide + ") -> " +
                    wide + " { def narrowed: result<" + scalar + "> = " + scalar +
                    "(value); if narrowed.erroneous { return 0; } (*p)[index] = narrowed.value; "
                    "def widened: result<" +
                    wide + "> = " + wide +
                    "((*p)[index]); if widened.erroneous { return 0; } return widened.value; }\n";
                harness << "extern uint64_t " << name << "(unsigned char *, uint64_t, uint64_t);\n";
                cases << "{" << name << "," << width << "," << msb << "," << sign << "},\n";
            }
        }
    }
    harness << "static struct Case cases[] = {\n" << cases.str() << "};\n";
    harness << R"(
        void *gloin_process_arguments_push(int64_t argc, void *argv) {
            static char scope; (void)argc; (void)argv; return &scope;
        }
        int32_t gloin_process_arguments_pop(void *scope) { (void)scope; return 0; }
        int guard_driver(void) {
            long page = sysconf(_SC_PAGESIZE);
            if (page <= 0) return 1;
            unsigned char *pages = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (pages == MAP_FAILED || mprotect(pages + page, page, PROT_READ | PROT_WRITE)) return 2;
            uint64_t random = UINT64_C(0x123456789abcdef);
            for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
                struct Case t = cases[c];
                unsigned size = (9 * t.width + 7) / 8;
                uint64_t mask = t.width == 64 ? UINT64_MAX : (UINT64_C(1) << t.width) - 1;
                for (unsigned index = 0; index < 9; ++index) {
                    for (unsigned sample = 0; sample < 70; ++sample) {
                        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
                        uint64_t bits = (sample < 64 ? UINT64_C(1) << sample : random) & mask;
                        if (sample == 64) bits = mask;
                        if (sample == 65) bits = 0;
                        uint64_t value = bits;
                        if (t.sign && (bits & (UINT64_C(1) << (t.width - 1)))) value |= ~mask;
                        for (unsigned edge = 0; edge < 2; ++edge) {
                            unsigned char *p = edge ? pages + page * 2 - size : pages + page;
                            unsigned char expected[72];
                            for (unsigned i = 0; i < size; ++i)
                                p[i] = expected[i] = (unsigned char)(random >> ((i % 8) * 8));
                            for (unsigned j = 0; j < t.width; ++j) {
                                unsigned stream = index * t.width + j;
                                unsigned physical = t.msb ? 7 - stream % 8 : stream % 8;
                                unsigned numeric = t.msb ? t.width - 1 - j : j;
                                unsigned char bit = 1u << physical;
                                expected[stream / 8] = (expected[stream / 8] & ~bit) |
                                    (((bits >> numeric) & 1) ? bit : 0);
                            }
                            if (t.update(p, index, value) != value) return 3;
                            if (memcmp(p, expected, size)) return 4;
                        }
                    }
                }
            }
            return munmap(pages, page * 3) ? 5 : 0;
        }
    )";
    auto harness_path = source(harness.str(), "array-guard.c");
    auto cc = llvm::sys::findProgramByName("cc");
    ASSERT_TRUE(static_cast<bool>(cc)) << cc.getError().message();
    for (auto optimization : {NativeOptimization::O0, NativeOptimization::O2}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "array-guard.gloin", context,
                                       CompilationMode::Module, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
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
        auto object = directory + "/arrays.o";
        ASSERT_TRUE(emit_native(*compiled.module, object, NativeOutput::Object, optimization,
                                gloin_test::gloinc, *compiled.diagnostics))
            << errors(*compiled.diagnostics);
        auto executable = directory + "/array-guard";
        ASSERT_EQ(llvm::sys::ExecuteAndWait(*cc, {*cc, harness_path, object, "-o", executable},
                                            std::nullopt, {}, 30),
                  0);
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 30), 0);
    }
}

TEST_F(PackedTest, PackedSlicesPreserveBitPositionsAliasesAndCapabilities) {
    auto path = source(R"(
        def identity<T>(value: T) -> T { return value; }
        def read(view: packed(msb_first) [const u3], i: u64) -> u3 { return view[i]; }
        def base(order: &u64, view: packed(msb_first) [u3]) -> packed(msb_first) [u3] {
            *order = *order * 10 + 1; return view;
        }
        def bound(order: &u64, digit: u64, value: u64) -> u64 {
            *order = *order * 10 + digit; return value;
        }
        def main() -> i32 {
            def mut values: packed(msb_first) [u3; 17] = {5; 17};
            def all: packed(msb_first) [u3] = values[..];
            def readonly: packed(msb_first) [const u3] = all;
            def mut first: u64 = 0;
            while first < 8 {
                def middle: packed(msb_first) [u3] = all[first..16];
                def nested: packed(msb_first) [u3] = middle[1..3];
                if nested.len != 2 { return 1; }
                nested[0] = 2;
                if read(readonly, first + 1) != 2 || values[first] != 5 || values[first + 2] != 5 { return 2; }
                values[first + 1] = 5;
                first = first + 1;
            }
            def copied: packed(msb_first) [u3] = identity<packed(msb_first) [u3]>(all[2..4]);
            copied[1] = 7;
            if readonly[3] != 7 { return 3; }
            all[16] = 1;
            if readonly[16] != 1 || values[15] != 5 { return 9; }
            def empty: packed(msb_first) [u3] = all[17..17];
            if empty[..].len != 0 { return 4; }
            def mut order: u64 = 0;
            def chosen: packed(msb_first) [u3] = base(&order, all)[bound(&order, 2, 2)..bound(&order, 3, 4)];
            if order != 123 || chosen.len != 2 || chosen[1] != 7 { return 5; }
            def mut signed: packed(lsb_first) [i13; 4] = {-4096, -1, 4095, 0};
            def signed_view: packed(lsb_first) [i13] = signed[1..];
            signed_view[1] = -4096;
            if signed_view[0] != -1 || signed[2] != -4096 || signed[3] != 0 { return 6; }
            def mut words: packed(lsb_first) [be_u16; 3] = {0x1234; 3};
            def word_view: packed(lsb_first) [be_u16] = words[1..2];
            word_view[0] = 0x5678;
            if words[0] != 0x1234 || words[1] != 0x5678 || words[2] != 0x1234 { return 7; }
            def mut nested_arrays: [packed(msb_first) [u3; 2]; 1] = {{3, 4}};
            def native_view: [packed(msb_first) [u3; 2]] = nested_arrays[..];
            if native_view[0][1] != 4 { return 8; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/packed-slices";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedSlicesRejectEncodingChangesTemporaryBorrowsAndWritesThroughConst) {
    for (const auto *program :
         {"def f(v: packed(msb_first) [u3]) -> packed(lsb_first) [u3] { return v; }",
          "def f(v: packed(msb_first) [u3]) -> [u3] { return v; }",
          "def f(v: [u3]) -> packed(msb_first) [u3] { return v; }",
          "def f(v: packed(msb_first) [const u3]) -> packed(msb_first) [u3] { return v[..]; }",
          "def f(v: packed(msb_first) [const u3]) -> void { v[0] = 1; }",
          "def f(v: packed(msb_first) [u3]) -> &u3 { return &v[0]; }",
          "def made() -> packed(msb_first) [u3; 2] { return {1, 2}; } def f() -> packed(msb_first) "
          "[const u3] { return made()[..]; }"}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "slice.gloin", context);
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic)
            << errors(*compiled.diagnostics);
    }
    mlir::MLIRContext context;
    auto compiled = compile_source("def f(v: packed(msb_first) [const u3]) -> packed(msb_first) "
                                   "[const u3] { return v[1..2]; }",
                                   "slice-ir.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedSliceSubrangeOp range;
    compiled.module->walk([&](gloin::PackedSliceSubrangeOp op) { range = op; });
    ASSERT_TRUE(range);
    auto type = mlir::cast<gloin::GloinPackedSliceType>(range.getSourceType());
    auto writable = gloin::GloinPackedSliceType::get(&context, type.getElement(), true, false);
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    range.getValue().setType(writable);
    EXPECT_TRUE(mlir::failed(range.verify()));
}

TEST_F(PackedTest, PackedSliceRangesAndIndicesTrapInEveryExecutionMode) {
    for (const auto *operation : {"all[0..9]", "all[3..2]", "all[negative..3]", "all[0..negative]",
                                  "all[8..8][0]", "all[negative]", "all[8]"}) {
        auto path = source(
            std::string("def main() -> i32 { def mut a: packed(msb_first) [u3; 8] = zeroed; ") +
            "def all: packed(msb_first) [u3] = a[..]; def negative: i64 = -1; " + operation +
            "; return 0; }");
        auto result = invoke({path});
        EXPECT_LT(result.status, 0) << result.err;
        for (const auto *optimization : {"-O0", "-O2"}) {
            auto executable = directory + "/slice-bounds";
            auto built = invoke_raw({optimization, "-o", executable, path});
            ASSERT_EQ(built.status, 0) << built.err;
            EXPECT_LT(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 0);
        }
    }
}

TEST_F(PackedTest, SliceStorageSizesUseBitOrNativeStrideAndCheckUnsignedBoundaries) {
    auto path = source(R"(
        import "@memory";
        import "@status";
        def struct Empty {}
        def main() -> i32 {
            def compact: result<u64> = memory.storage_bytes<packed(msb_first) [u13]>(8);
            if compact.erroneous { return 1; }
            if compact.value != 13 { return 2; }
            def native: result<u64> = memory.storage_bytes<[u13]>(8);
            if native.erroneous { return 3; }
            if native.value != 16 { return 4; }
            def tail: result<u64> = memory.storage_bytes<packed(lsb_first) [const i13]>(3);
            if tail.erroneous { return 5; }
            if tail.value != 5 { return 6; }
            def huge: result<u64> = memory.storage_bytes<packed(msb_first) [u3]>(18446744073709551615);
            if huge.erroneous { return 7; }
            if huge.value != 6917529027641081856 { return 8; }
            def largest: result<u64> = memory.storage_bytes<packed(msb_first) [u13]>(5675921253449092804);
            if largest.erroneous { return 9; }
            if largest.value != 9223372036854775807 { return 10; }
            def overflow: result<u64> = memory.storage_bytes<packed(msb_first) [u13]>(5675921253449092805);
            if overflow.erroneous {
                if overflow.error.code != status.OVERFLOW || overflow.error.os_error != 0 { return 11; }
            } else { return 12; }
            def native_overflow: result<u64> = memory.storage_bytes<[u64]>(1152921504606846976);
            if native_overflow.erroneous {
                if native_overflow.error.code != status.OVERFLOW { return 13; }
            } else { return 14; }
            def empty: result<u64> = memory.storage_bytes<[Empty]>(18446744073709551615);
            if empty.erroneous { return 15; }
            if empty.value != 0 { return 16; }
            def zero: result<u64> = memory.storage_bytes<packed(msb_first) [be_u64]>(0);
            if zero.erroneous { return 17; }
            if zero.value != 0 { return 18; }
            def aligned: result<void> = memory.check_slice_alignment<packed(lsb_first) [be_u64]>(1);
            if aligned.erroneous { return 19; }
            def weak: result<void> = memory.check_slice_alignment<[u64]>(1);
            if weak.erroneous { if weak.error.code != status.INVALID { return 20; } }
            else { return 21; }
            def bad: result<void> = memory.check_slice_alignment<packed(msb_first) [u3]>(3);
            if bad.erroneous { if bad.error.code != status.INVALID { return 22; } }
            else { return 23; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/storage-size";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, SliceStorageQueriesRequireSliceTypesAndKeepIntrinsicsPrivate) {
    for (const auto *program :
         {"import \"@memory\"; def main() -> i32 { def r: result<u64> = "
          "memory.storage_bytes<u13>(8); if r.erroneous { return 1; } return 0; }",
          "import \"@memory\"; def main() -> i32 { def r: result<u64> = "
          "memory.storage_bytes<packed(msb_first) [u13; 8]>(8); if r.erroneous { return 1; } "
          "return 0; }",
          "def f() -> u64 { return __memory_slice_unit<packed(msb_first) [u13]>(); }"}) {
        auto path = source(program);
        auto result = invoke({"--check", path});
        EXPECT_EQ(result.status, 1) << result.err;
        EXPECT_NE(result.err.find("error:"), std::string::npos);
    }
}

TEST_F(PackedTest, BoundedPackedViewsPreserveBytesAndHandleCapacityAlignmentAndOverflow) {
    auto path = source(R"(
        import "@memory";
        import "@status";
        def main() -> i32 {
            def mut bytes: [u8; 4] = {0x11, 0xA5, 0x5A, 0x22};
            def created: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[1..3], 3, 3);
            if created.erroneous { return 1; }
            def view: packed(msb_first) [u3] = created.value;
            if view.len != 3 || view[0] != 1 || view[1] != 2 || view[2] != 5 { return 2; }
            def borrowed: result<packed(msb_first) [const u3]> = memory.view_packed<packed(msb_first) [const u3]>(bytes[1..3], 3, 3);
            if borrowed.erroneous { return 3; }
            def readonly: packed(msb_first) [const u3] = borrowed.value;
            view[1] = 7;
            if readonly[1] != 7 || bytes[1] != 0xA7 || bytes[2] != 0xDA { return 4; }
            def nested: packed(msb_first) [u3] = view[1..];
            nested[1] = 1;
            if readonly[2] != 1 || bytes[2] != 0x9A || bytes[0] != 0x11 || bytes[3] != 0x22 { return 5; }
            def short: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[1..2], 3, 3);
            if short.erroneous { if short.error.code != status.OUT_OF_RANGE { return 6; } }
            else { return 7; }
            def overflow: result<packed(msb_first) [u13]> = memory.view_packed<packed(msb_first) [u13]>(bytes[..], 0, 18446744073709551615);
            if overflow.erroneous { if overflow.error.code != status.OVERFLOW || overflow.error.os_error != 0 { return 8; } }
            else { return 9; }
            def huge: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[..], 0, 18446744073709551615);
            if huge.erroneous { if huge.error.code != status.OUT_OF_RANGE { return 10; } }
            else { return 11; }
            def invalid: result<packed(msb_first) [be_u16]> = memory.view_packed<packed(msb_first) [be_u16]>(bytes[..], 1, 18446744073709551615);
            if invalid.erroneous { if invalid.error.code != status.INVALID { return 12; } }
            else { return 13; }
            def empty: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[0..0], 0, 0);
            if empty.erroneous { return 14; }
            def none: packed(msb_first) [u3] = empty.value;
            if none.len != 0 { return 15; }
            def past: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[..], 32, 0);
            if past.erroneous { return 16; }
            def outside: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[..], 33, 0);
            if outside.erroneous { if outside.error.code != status.OUT_OF_RANGE { return 17; } }
            else { return 18; }
            def remote: result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes[..], 18446744073709551615, 1);
            if remote.erroneous { if remote.error.code != status.OUT_OF_RANGE { return 19; } }
            else { return 20; }
            if bytes[0] != 0x11 || bytes[1] != 0xA7 || bytes[2] != 0x9A || bytes[3] != 0x22 { return 21; }
            def mut wide: [u8; 9] = {255; 9};
            def full: result<packed(msb_first) [u64]> = memory.view_packed<packed(msb_first) [u64]>(wide[..], 7, 1);
            if full.erroneous { return 22; }
            def number: packed(msb_first) [u64] = full.value;
            if number[0] != 18446744073709551615 { return 23; }
            number[0] = 0;
            if wide[0] != 254 || wide[8] != 1 { return 24; }
            def mut i: u64 = 1;
            while i < 8 { if wide[i] != 0 { return 25; } i = i + 1; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *optimization : {"-O0", "-O2"}) {
        auto executable = directory + "/packed-byte-view";
        auto built = invoke_raw({optimization, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(PackedTest, PackedByteViewsRejectCapabilityAndTypeForgeries) {
    for (const auto *program :
         {"import \"@memory\"; def f(bytes: [const u8]) -> result<packed(msb_first) [u3]> { def r: "
          "result<packed(msb_first) [u3]> = memory.view_packed<packed(msb_first) [u3]>(bytes, 0, "
          "1); if r.erroneous { return r.error; } return r.value; }",
          "import \"@memory\"; def f(bytes: [u8]) -> result<[u16]> { def r: result<[u16]> = "
          "memory.view_packed<[u16]>(bytes, 0, 1); if r.erroneous { return r.error; } return "
          "r.value; }",
          "def f(bytes: __memory_slice_bytes<packed(msb_first) [u3]>) -> void {}"}) {
        auto path = source(program);
        auto result = invoke({"--check", path});
        EXPECT_EQ(result.status, 1) << result.err;
        EXPECT_NE(result.err.find("error:"), std::string::npos);
    }
    auto path = source(R"(
        import "@memory";
        def main() -> i32 {
            def bytes: [u8; 1] = {0};
            def r: result<packed(msb_first) [const u3]> = memory.view_packed<packed(msb_first) [const u3]>(bytes[..], 0, 1);
            if r.erroneous { return 1; } return 0;
        }
    )");
    mlir::MLIRContext context;
    auto compiled = compile_source(read(path), path, context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::CheckedPackedViewOp view;
    compiled.module->walk([&](gloin::CheckedPackedViewOp op) { view = op; });
    ASSERT_TRUE(view);
    auto type = mlir::cast<gloin::GloinPackedSliceType>(view.getSourceType());
    EXPECT_TRUE(type.getReadOnly());
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    auto writable =
        gloin::GloinPackedSliceType::get(&context, type.getElement(), type.getMsbFirst(), false);
    view.setSourceType(writable);
    view.getValue().setType(gloin::GloinResultType::get(&context, writable));
    EXPECT_TRUE(mlir::failed(view.verify()));
}

TEST_F(PackedTest, CheckedPackedSplitsMatchPhysicalByteBoundariesAndPreserveViews) {
    for (const auto *order : {"msb_first", "lsb_first"}) {
        for (unsigned width : {3, 8, 13}) {
            auto scalar = "u" + std::to_string(width);
            auto type = std::string("packed(") + order + ") [" + scalar + "]";
            auto readonly = std::string("packed(") + order + ") [const " + scalar + "]";
            auto path = source(
                "import \"@memory\"; import \"@status\"; def main() -> i32 { "
                "def mut bytes: [u8; 32] = {0xA5; 32}; "
                "def mut offset: u64 = 0; while offset < 8 { "
                "def r: result<" +
                type + "> = memory.view_packed<" + type +
                ">(bytes[..], offset, 16); "
                "if r.erroneous { return 1; } def view: " +
                type +
                " = r.value; "
                "def mut split_index: u64 = 0; while split_index <= 16 { "
                "def boundary: u64 = offset + split_index * " +
                std::to_string(width) +
                "; "
                "def disjoint: bool = split_index == 0 || split_index == 16 || (boundary + 7) / 8 "
                "== boundary / 8; "
                "def split: result<memory.PackedSplit<" +
                type + ">> = memory.split_packed<" + type +
                ">(view, split_index); "
                "if split.erroneous { if disjoint || split.error.code != status.INVALID || "
                "split.error.os_error != 0 { return 2; } } "
                "else { if !disjoint { return 3; } def pair: memory.PackedSplit<" +
                type +
                "> = split.value; "
                "if pair.left.len != split_index || pair.right.len != 16 - split_index { return 4; "
                "} "
                "if split_index > 0 { if pair.left[split_index - 1] != view[split_index - 1] { "
                "return 5; } } "
                "if split_index < 16 { if pair.right[0] != view[split_index] { return 6; } } } "
                "split_index = split_index + 1; } "
                "def out: result<memory.PackedSplit<" +
                type + ">> = memory.split_packed<" + type +
                ">(view, 18446744073709551615); "
                "if out.erroneous { if out.error.code != status.OUT_OF_RANGE { return 7; } } else "
                "{ return 8; } "
                "def empty: " +
                readonly +
                " = view[0..0]; "
                "def none: result<memory.PackedSplit<" +
                readonly + ">> = memory.split_packed<" + readonly +
                ">(empty, 0); "
                "if none.erroneous { return 9; } offset = offset + 1; } "
                "def mut i: u64 = 0; while i < 32 { if bytes[i] != 0xA5 { return 10; } i = i + 1; "
                "} return 42; }");
            expect_run(invoke({path}), 42);
            for (const auto *mode : {"-O0", "-O2"}) {
                auto executable = directory + "/packed-splits";
                auto built = invoke_raw({mode, "-o", executable, path});
                ASSERT_EQ(built.status, 0) << built.err;
                EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                          42);
            }
        }
    }
}

TEST_F(PackedTest, PackedSplitAliasesRetainCapabilitiesAndRejectOrdinarySlices) {
    auto path = source(R"(
        import "@memory";
        def main() -> i32 {
            def mut values: packed(msb_first) [u3; 16] = {5; 16};
            def r: result<memory.PackedSplit<packed(msb_first) [u3]>> = memory.split_packed<packed(msb_first) [u3]>(values[..], 8);
            if r.erroneous { return 1; }
            def pair: memory.PackedSplit<packed(msb_first) [u3]> = r.value;
            def readonly: packed(msb_first) [const u3] = values[..];
            pair.left[7] = 1; pair.right[0] = 2;
            if values[7] != 1 || readonly[8] != 2 || values[6] != 5 || values[9] != 5 { return 2; }
            def mut endian: packed(lsb_first) [be_u16; 2] = {0x1234; 2};
            def e: result<memory.PackedSplit<packed(lsb_first) [const be_u16]>> = memory.split_packed<packed(lsb_first) [const be_u16]>(endian[..], 1);
            if e.erroneous { return 3; }
            def words: memory.PackedSplit<packed(lsb_first) [const be_u16]> = e.value;
            if words.left[0] != 0x1234 || words.right[0] != 0x1234 { return 4; }
            return 42;
        }
    )");
    expect_run(invoke({path}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/split-aliases";
        auto built = invoke_raw({mode, "-o", executable, path});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(read(path), path, context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    gloin::PackedSliceBitOffsetOp offset;
    compiled.module->walk([&](gloin::PackedSliceBitOffsetOp op) { offset = op; });
    ASSERT_TRUE(offset);
    auto type = mlir::cast<gloin::GloinPackedSliceType>(offset.getSourceType());
    offset.setSourceType(
        gloin::GloinSliceType::get(&context, type.getElement(), type.getReadOnly()));
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    EXPECT_TRUE(mlir::failed(offset.verify()));
    for (const auto *text :
         {"import \"@memory\"; def f(s: [u8]) -> void { def r: result<memory.PackedSplit<[u8]>> = "
          "memory.split_packed<[u8]>(s, 0); if r.erroneous { return; } }",
          "def f(s: packed(msb_first) [u3]) -> u64 { return "
          "__memory_packed_bit_offset<packed(msb_first) [u3]>(s); }",
          "import \"@memory\"; def f(s: packed(msb_first) [const u3]) -> void { def r: "
          "result<memory.PackedSplit<packed(msb_first) [const u3]>> = "
          "memory.split_packed<packed(msb_first) [const u3]>(s, 0); if r.erroneous { return; } def "
          "p: memory.PackedSplit<packed(msb_first) [const u3]> = r.value; p.right[0] = 1; }"}) {
        EXPECT_EQ(invoke({"--check", source(text)}).status, 1);
    }
}

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

TEST_F(PackedTest, ConstantConversionHelperChecksEveryWidthSignAndByteOrderBoundaryPair) {
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

TEST_F(PackedTest, RejectsInvalidLayoutsReferencesAndUncheckedConversions) {
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

TEST_F(PackedTest, ExactWidthArithmeticTrapsEvenWhenUnused) {
    const std::vector<std::string> programs = {
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
        def narrow(x: i16) -> result<i13> {
            def r: result<i13> = i13(x);
            if r.erroneous { return r.error; }
            return r.value;
        }
        def number(x: i13) -> result<i13> { return x; }
        def main() -> i32 {
            def mut values: [i13; 3] = {-1, -4096, 4095};
            def narrowed: result<i13> = narrow(-2);
            if narrowed.erroneous { return 5; }
            values[0] = twice<i13>(narrowed.value);
            def converted_1: result<u16> = u16(values[2]);
            if converted_1.erroneous { return 90; }
            def converted_2: result<i64> = i64(values[1]);
            if converted_2.erroneous { return 90; }
            if values[0] != -4 || converted_2.value != -4096 || converted_1.value != 4095 { return 1; }
            def value: result<i13> = number(values[1]);
            if value.erroneous { return 2; }
            if value.value != -4096 { return 3; }
            def converted_3: result<u8> = u8(1);
            if converted_3.erroneous { return 90; }
            def converted_4: result<u1> = u1(converted_3.value);
            if converted_4.erroneous { return 90; }
            def a: u1 = converted_4.value;
            def converted_5: result<i8> = i8(-1);
            if converted_5.erroneous { return 90; }
            def converted_6: result<i1> = i1(converted_5.value);
            if converted_6.erroneous { return 90; }
            def b: i1 = converted_6.value;
            def converted_7: result<i16> = i16(b);
            if converted_7.erroneous { return 90; }
            def converted_8: result<u16> = u16(a);
            if converted_8.erroneous { return 90; }
            if converted_8.value != 1 || converted_7.value != -1 || !(b < 0) { return 4; }
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
                    program +=
                        "def " + name + "(p: &Record" + id + ", value: " + wide + ") -> " + wide +
                        " { def narrowed: result<" + type + "> = " + type +
                        "(value); if narrowed.erroneous { return 0; } p.value = narrowed.value; "
                        "def widened: result<" +
                        wide + "> = " + wide +
                        "(p.value); if widened.erroneous { return 0; } return widened.value; }\n";
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

TEST_F(PackedTest, RawPackedByteViewsVerifyNominalTypesAndConstness) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        import "@memory";
        def packed struct(msb_first) Header { def x: u13, def y: u3, def z: be_u32, }
        def struct Native { def x: u32, }
        def read(bytes: [const u8]) -> result<Header> {
            def value: result<Header> = memory.read_packed<Header>(bytes);
            if value.erroneous { return value.error; }
            return value.value;
        }
        def write(bytes: [u8], value: Header) -> result<void> {
            def done: result<void> = memory.write_packed<Header>(bytes, value);
            if done.erroneous { return done.error; }
            return;
        }
    )",
                                   "raw-packed.gloin", context);
    ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
    unsigned views = 0;
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    compiled.module->walk([&](gloin::RawPackedPointerOp view) {
        ++views;
        auto source = mlir::cast<gloin::GloinPointerType>(view.getSourceType());
        auto old_type = view.getPointer().getType();
        for (auto forged :
             {gloin::GloinPointerType::get(&context, source.getPointee(), true,
                                           !source.getReadOnly()),
              gloin::GloinPointerType::get(&context, mlir::IntegerType::get(&context, 32), true,
                                           source.getReadOnly()),
              gloin::GloinPointerType::get(&context, source.getPointee(), false,
                                           source.getReadOnly())}) {
            view.setSourceType(forged);
            view.getPointer().setType(forged);
            EXPECT_TRUE(mlir::failed(view.verify()));
        }
        view.setSourceType(source);
        view.getPointer().setType(old_type);
        EXPECT_TRUE(mlir::succeeded(view.verify()));
    });
    EXPECT_EQ(views, 2u);
    EXPECT_TRUE(mlir::succeeded(mlir::verify(*compiled.module)));
}

TEST_F(PackedTest, PackedByteCopiesAndShortBuffersStayInsideGuardPages) {
    std::string program = "import \"@memory\";\n";
    std::ostringstream harness;
    harness << R"(
        #include <stdint.h>
        #include <string.h>
        #include <stdlib.h>
        #include <sys/mman.h>
        #include <unistd.h>
        typedef int32_t (*Copy)(unsigned char *, uint64_t);
        struct Case { Copy copy; unsigned size; };
    )";
    std::string cases;
    for (unsigned size : {1, 2, 3, 6, 9, 20, 60, 257}) {
        const auto id = std::to_string(size);
        program += "def packed struct(msb_first) Bytes" + id + " { def data: [u8; " + id + "], }\n";
        program += "def copy_" + id + "(p: *[u8; " + id +
                   "], capacity: u64) -> i32 { "
                   "def read: result<Bytes" +
                   id + "> = memory.read_packed<Bytes" + id +
                   ">((*p)[..capacity]); "
                   "if read.erroneous { if capacity < " +
                   id +
                   " { return 0; } return 1; } "
                   "if capacity < " +
                   id +
                   " { return 2; } (*p)[0] = 0; "
                   "def written: result<void> = memory.write_packed<Bytes" +
                   id +
                   ">((*p)[..capacity], read.value); "
                   "if written.erroneous { return 3; } return 0; }\n";
        harness << "extern int32_t copy_" << id << "(unsigned char *, uint64_t);\n";
        cases += "{copy_" + id + "," + id + "},";
    }
    harness << "static struct Case cases[] = {" << cases << "};\n";
    harness << R"(
        // The imported memory module retains unused alloc/free wrappers in object output.
        // Any allocation in a packed copy is a test failure.
        void *gloin_memory_alloc(int64_t bytes, int64_t alignment) { abort(); }
        void gloin_memory_free(void *pointer) { abort(); }
        void *gloin_process_arguments_push(int64_t argc, void *argv) {
            static char scope; (void)argc; (void)argv; return &scope;
        }
        int32_t gloin_process_arguments_pop(void *scope) { (void)scope; return 0; }
        int guard_driver(void) {
            long page = sysconf(_SC_PAGESIZE);
            if (page <= 0) return 1;
            unsigned char *pages = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (pages == MAP_FAILED || mprotect(pages + page, page, PROT_READ | PROT_WRITE)) return 2;
            for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
                struct Case t = cases[c];
                for (unsigned edge = 0; edge < 2; ++edge) {
                    for (unsigned capacity = 0; capacity <= t.size; ++capacity) {
                        unsigned char *p = edge ? pages + 2 * page - capacity : pages + page;
                        unsigned char expected[257];
                        for (unsigned i = 0; i < capacity; ++i) p[i] = expected[i] = (i * 71 + 123) % 256;
                        if (t.copy(p, capacity)) return 3;
                        if (memcmp(p, expected, capacity)) return 4;
                    }
                }
            }
            return munmap(pages, page * 3) ? 5 : 0;
        }
    )";
    const auto harness_path = source(harness.str(), "packed-copy-guard.c");
    auto cc = llvm::sys::findProgramByName("cc");
    ASSERT_TRUE(static_cast<bool>(cc));
    for (auto optimization : {NativeOptimization::O0, NativeOptimization::O2}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(program, "byte-copy.gloin", context, CompilationMode::Module,
                                       CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success()) << errors(*compiled.diagnostics);
        mlir::OpBuilder builder(&context);
        builder.setInsertionPointToEnd(compiled.module->getBody());
        auto entry = mlir::LLVM::LLVMFunctionType::get(builder.getI32Type(), {});
        auto driver =
            builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "guard_driver", entry);
        auto main = builder.create<mlir::LLVM::LLVMFuncOp>(builder.getUnknownLoc(), "main", entry);
        builder.setInsertionPointToStart(main.addEntryBlock(builder));
        auto result =
            builder.create<mlir::LLVM::CallOp>(builder.getUnknownLoc(), driver, mlir::ValueRange{});
        builder.create<mlir::LLVM::ReturnOp>(builder.getUnknownLoc(), result.getResults());
        const auto object = directory + "/copy.o";
        ASSERT_TRUE(emit_native(*compiled.module, object, NativeOutput::Object, optimization,
                                gloin_test::gloinc, *compiled.diagnostics))
            << errors(*compiled.diagnostics);
        const auto executable = directory + "/guard";
        ASSERT_EQ(llvm::sys::ExecuteAndWait(*cc, {*cc, harness_path, object, "-o", executable},
                                            std::nullopt, {}, 30),
                  0);
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 30), 0);
    }
}
