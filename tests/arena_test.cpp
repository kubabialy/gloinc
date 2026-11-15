#include "arena_abi.h"
#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#include <tuple>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
class ArenaTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body, const std::string &extra = "") {
        return source("import \"@arena\"; " + extra +
                      " def main() -> i32 { "
                      "def mut a: arena.GeneralArena = arena.GeneralArena.create(); "
                      "defer a.free(); " +
                      body + " }");
    }
    std::string library() {
        const char *override_path = std::getenv("GLOIN_TEST_CLI");
        auto binary = std::filesystem::path(override_path ? override_path : gloin_test::gloinc);
        auto root = binary.parent_path();
        auto path = root / "stdlib/arena.gloin";
        if (!std::filesystem::exists(path))
            path = root / "../share/gloinc/stdlib/arena.gloin";
        // Override fixtures replace @arena but retain its checked dependencies.
        for (const auto *dependency : {"memory.gloin", "status.gloin"})
            source(read((path.parent_path() / dependency).string()), dependency);
        return read(path.string());
    }
    void expect_trap(const std::string &file) {
        expect_success(invoke({"--check", file}), "");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err << result.message;
        EXPECT_TRUE(result.err.empty()) << result.err;
        EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                    result.message.find("Illegal instruction") != std::string::npos)
            << result.message;
    }
};

void fail_runtime_call(mlir::ModuleOp module, ArenaPrimitive kind) {
    const auto name = arena_runtime_names[static_cast<size_t>(kind)];
    mlir::OpBuilder builder(module.getContext());
    auto function = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
    ASSERT_TRUE(function);
    // Leave the actual external declaration intact for JIT ABI validation.
    const std::string replacement = "gloin.test.fail_arena";
    module.walk([&](mlir::LLVM::CallOp call) {
        if (call.getCallee() == llvm::StringRef(name))
            call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(module.getContext(), replacement));
    });
    builder.setInsertionPointToEnd(module.getBody());
    auto stub = builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), replacement,
                                                       function.getFunctionType());
    builder.setInsertionPointToStart(stub.addEntryBlock(builder));
    auto zero = builder.create<mlir::LLVM::ZeroOp>(
        module.getLoc(), mlir::LLVM::LLVMPointerType::get(module.getContext()));
    builder.create<mlir::LLVM::ReturnOp>(module.getLoc(), mlir::ValueRange{zero});
}
} // namespace

TEST_F(ArenaTest, EveryScalarIsCopiedIntoWritableTypedStorage) {
    for (const auto &[type, before, after] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"i8", "-128", "127"},
             {"u8", "0", "255"},
             {"i16", "-32768", "32767"},
             {"u16", "0", "65535"},
             {"i32", "-2147483648", "2147483647"},
             {"u32", "0", "4294967295"},
             {"i64", "-9223372036854775808", "9223372036854775807"},
             {"u64", "0", "18446744073709551615"},
             {"f32", "1.25", "-2.5"},
             {"f64", "1.25", "-2.5"},
             {"bool", "false", "true"}}) {
        SCOPED_TRACE(type);
        auto file =
            program("def value: " + type + " = " + before + "; def p: &" + type +
                    " = a.alloc(value); def q: *" + type +
                    " = a.try_alloc(value); "
                    "if q == null { return 1; } *p = " +
                    after + "; *q = " + after + "; if value == " + before + " && *p == " + after +
                    " && *q == " + after + " { return 42; } return 2;");
        expect_run(invoke({file}), 42);
    }
}

TEST_F(ArenaTest, NestedStructsStringsAndMethodsPreserveNativeLayout) {
    auto file = program(R"(
        def p: &Record = a.alloc(Record { tag: 255, inner: Inner { value: 40 }, text: "alive" });
        p.inner.add(2);
        def q: *Record = a.try_alloc(*p);
        if q == null { return 1; }
        q.inner.add(1);
        std.println(p.text);
        if p.tag == 255 && p.inner.value == 42 && q.inner.value == 43 { return 42; }
        return 2;
    )",
                        R"(import "@std";
        def struct Inner { def mut value: i64,
            def add(self: &Inner, n: i64) -> void { self.value = self.value + n; } }
        def struct Record { def tag: u8, def mut inner: Inner, def text: string, }
    )");
    expect_run(invoke({file}), 42, "alive\n");
    expect_success(invoke({"--check", file}), "");
    for (const std::string mode : {"--emit-ir", "--emit-llvm"}) {
        auto result = invoke({mode, file});
        EXPECT_EQ(result.status, 0) << result.err;
        EXPECT_NE(result.out.find("gloin_arena_general_alloc"), std::string::npos);
    }
}

TEST_F(ArenaTest, CopiesAreShallowAndPointerQualifiersArePreserved) {
    auto file = program(R"(
        def mut value: i32 = 1;
        def p: &Box = a.alloc(Box { item: &value });
        *p.item = 42;
        def reference: &i32 = &value;
        def rr: &&i32 = a.alloc(reference);
        def view: &const i32 = &value;
        def readonly: &&const i32 = a.alloc(view);
        def empty: *i32 = null;
        def slot: &*i32 = a.alloc(empty);
        if *slot == null && **rr == 42 && **readonly == 42 && value == 42 { return 42; }
        return 1;
    )",
                        "def struct Box { def item: &i32, }");
    expect_run(invoke({file}), 42);
}

TEST_F(ArenaTest, EmptyRecordsHaveDistinctNonNullAddresses) {
    auto file = program(R"(
        def p: &Empty = a.alloc(Empty {});
        def q: &Empty = a.alloc(Empty {});
        def r: *Empty = a.try_alloc(Empty {});
        if p != q && r != null && r != p && r != q { return 42; } return 1;
    )",
                        "def struct Empty {}");
    expect_run(invoke({file}), 42);
}

TEST_F(ArenaTest, GrowingLinkedObjectsRemainReadableAndWritable) {
    auto file = program(R"(
        def mut head: *Node = null;
        for def mut i: i64 = 0; i < 100000; i = i + 1 {
            head = a.alloc(Node { value: i, next: head });
        }
        def mut sum: i64 = 0;
        while head != null { sum = sum + head.value; head.value = 0; head = head.next; }
        if sum == 4999950000 { return 42; } return 1;
    )",
                        "def struct Node { def mut value: i64, def next: *Node, }");
    expect_run(invoke({file}), 42);
}

TEST_F(ArenaTest, HandlesAliasResetStateAndClearedOwnerCanBeRecreated) {
    auto file = program(R"(
        def mut alias: arena.GeneralArena = a;
        def old: &i32 = alias.alloc(1);
        if *old != 1 { return 1; }
        a.reset();
        def fresh: &i32 = alias.alloc(42);
        if *fresh != 42 { return 2; }
        a.free(); a.free();
        a = arena.GeneralArena.create();
        return *a.alloc(42);
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(ArenaTest, OwnerCanBeReturnedAndHelpersUseReferences) {
    auto file = source(R"(
        import "@arena";
        def make() -> arena.GeneralArena { return arena.GeneralArena.create(); }
        def add(a: &arena.GeneralArena, n: i32) -> &i32 { return a.alloc(n); }
        def main() -> i32 {
            def mut a: arena.GeneralArena = make(); defer a.free();
            def mut b: arena.GeneralArena = make(); defer b.free();
            def p: &i32 = add(&a, 20);
            def q: &i32 = add(&b, 22);
            a.reset();
            return *add(&a, 20) + *q;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(ArenaTest, ReceiverAndInitializerEvaluateOnceInOrder) {
    auto file = program(R"(
        def p: &i32 = receiver(&a).alloc(value());
        def q: *i32 = receiver(&a).try_alloc(value());
        if q == null { return 1; } return *p + *q;
    )",
                        R"(import "@std";
        def receiver(a: &arena.GeneralArena) -> &arena.GeneralArena { std.print("R"); return a; }
        def value() -> i32 { std.print("V"); return 21; }
    )");
    expect_run(invoke({file}), 42, "RVRV");
}

TEST_F(ArenaTest, DeferredAllocationCapturesValueAndRunsBeforeFree) {
    // Instrument the source layout hook to expose the deferred invocation.
    auto module = library();
    const std::string needle = "__arena_require(storage);";
    auto position = module.find(needle);
    ASSERT_NE(position, std::string::npos);
    module.insert(position, "__write_stdout(\"A\"); ");
    source(module, "arena.gloin");
    auto file = program(R"(
        def mut n: i32 = 7;
        defer a.alloc(n);
        n = 9;
        defer a.try_alloc(n);
        return 42;
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42, "A");
}

TEST_F(ArenaTest, InvalidCallsFailInEveryCompilerMode) {
    for (const std::string body :
         {"a.alloc(); return 0;", "a.alloc(1, 8); return 0;", "a.alloc(i32); return 0;",
          "a.alloc(null); return 0;", "a.alloc(noop()); return 0;",
          "def n: i32; a.alloc(n); return 0;", "def p: &i64 = a.alloc(1); return 0;",
          "def p: &i32 = a.try_alloc(1); return 0;",
          "def frozen: arena.GeneralArena = a; frozen.alloc(1); return 0;",
          "def frozen: &const arena.GeneralArena = &a; frozen.try_alloc(1); return 0;",
          "arena.GeneralArena.create().alloc(1); return 0;", "a.state = null; return 0;",
          "__arena_general_create(); return 0;", "a.alloc; return 0;"}) {
        SCOPED_TRACE(body);
        auto file = program(body, "def noop() -> void {}");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(ArenaTest, ClearedHandlesTrapOnAllocationTryAllocationAndReset) {
    for (const std::string operation : {"a.alloc(1);", "a.try_alloc(1);", "a.reset();"})
        expect_trap(program("a.free(); " + operation + " return 0;"));
}

TEST_F(ArenaTest, NullReceiverTrapsAndReferenceConversionRemainsExplicit) {
    expect_trap(source(R"(
        import "@arena";
        def main() -> i32 {
            def a: *arena.GeneralArena = null;
            (&*a).alloc(1); return 0;
        }
    )"));
}

TEST_F(ArenaTest, SameNamedMethodsAndAdditionalModuleTypesRemainOrdinaryMethods) {
    source(library() + R"(
        def pub struct Fixed {
            def pub static create() -> Fixed { return Fixed {}; }
            def pub alloc(self: &Fixed, n: i32) -> i32 { return n + 1; }
        }
    )",
           "arena.gloin");
    auto file = program(R"(
        def mut fixed: arena.Fixed = arena.Fixed.create();
        def mut local: GeneralArena = GeneralArena {};
        return fixed.alloc(20) + local.alloc(20);
    )",
                        R"(def struct GeneralArena {
        def alloc(self: &GeneralArena, n: i32) -> i32 { return n + 1; }
    })");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
}

TEST_F(ArenaTest, LayoutBridgeReceivesNativeSizeAndAlignment) {
    // u8 followed by u64 must be padded to 16 bytes with 8-byte alignment.
    auto module = library();
    const std::string needle =
        "def pub alloc(self: &GeneralArena, size: u64, alignment: u64) -> *u8 {";
    auto position = module.find(needle);
    ASSERT_NE(position, std::string::npos);
    module.insert(position + needle.size(),
                  "if size != 16 || alignment != 8 { __arena_require(null); }");
    source(module, "arena.gloin");
    auto file = program("def p: &P = a.alloc(P { tag: 255, count: 42 }); "
                        "if p.tag == 255 && p.count == 42 { return 42; } return 1;",
                        "def struct P { def tag: u8, def count: u64, }");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
}

TEST_F(ArenaTest, MissingLibraryAndMalformedBridgeFailWithoutFallback) {
    auto file = program("return 42;");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Cannot load module '@arena'");
    source(R"(def pub struct GeneralArena {
        def pub alloc(self: &GeneralArena, value: i32) -> i32 { return value; }
    })",
           "arena.gloin");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "Arena allocation bridge");
}

TEST_F(ArenaTest, NativePrimitiveNamesAreReservedOnlyInsideArenaModule) {
    source("def __arena_general_create() -> i32 { return 42; }", "arena.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@arena\"; def main() -> i32 { return 0; }")}),
                 1, "Cannot redeclare");
    expect_run(invoke({source("def gloin_arena_general_alloc() -> i32 { return 42; } "
                              "def main() -> i32 { return gloin_arena_general_alloc(); }")}),
               42);
}

TEST_F(ArenaTest, ExternalLlvmRunnerLoadsTheNativeRuntime) {
    auto file = program("return *a.alloc(42);");
    auto emitted = invoke({"--emit-llvm", file});
    ASSERT_EQ(emitted.status, 0) << emitted.err;
    const char *runtime = std::getenv("GLOIN_TEST_ARENA_RUNTIME");
    gloin_test::ToolCommand runner{
        gloin_test::mlir_runner,
        {std::string("--shared-libs=") + (runtime ? runtime : gloin_test::arena_runtime)}};
#ifdef __APPLE__
    // An instrumented runtime loaded by mlir-runner needs ASan installed at
    // process startup, before dlopen. Use this test binary's actual ASan image;
    // do not hardcode an Xcode/compiler version or change the parent's environment.
    if (auto *asan = dlsym(RTLD_DEFAULT, "__asan_init")) {
        Dl_info info{};
        ASSERT_NE(dladdr(asan, &info), 0);
        std::string preload = info.dli_fname;
        if (const auto *existing = std::getenv("DYLD_INSERT_LIBRARIES"); existing && *existing)
            preload += std::string(":") + existing;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(emitted.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

TEST_F(ArenaTest, JitRejectsEveryMalformedRuntimeSignature) {
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const std::string declaration :
         {"llvm.func @gloin_arena_general_create(i64) -> !llvm.ptr",
          "llvm.func @gloin_arena_general_alloc(!llvm.ptr, i64) -> !llvm.ptr",
          "llvm.func @gloin_arena_general_alloc(!llvm.ptr, i64, i64, ...) -> !llvm.ptr",
          "llvm.func @gloin_arena_general_reset(!llvm.ptr) -> i32",
          "llvm.func @gloin_arena_general_destroy(i64)",
          "llvm.func @gloin_arena_owned_alloc(!llvm.ptr, i64) -> !llvm.ptr",
          "llvm.func @gloin_arena_owned_release(!llvm.ptr)",
          "llvm.func @gloin_arena_general_create() -> !llvm.ptr { %p = llvm.mlir.zero : !llvm.ptr "
          "llvm.return %p : !llvm.ptr }"}) {
        SCOPED_TRACE(declaration);
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + declaration +
                " llvm.func @main() -> i32 { "
                "%v = llvm.mlir.constant(42 : i32) : i32 llvm.return %v : i32 } }",
            &context);
        ASSERT_TRUE(module);
        auto result = JitRunner::run(*module);
        EXPECT_FALSE(result.success());
        EXPECT_EQ(result.failed_stage, DiagnosticStage::Execution);
    }
}

TEST_F(ArenaTest, TryAllocationFailureReturnsNullAfterInitializerSideEffects) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        import "@arena";
        def value(count: &i32) -> i32 { *count = *count + 1; return 7; }
        def main() -> i32 {
            def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
            def mut count: i32 = 0;
            def p: *i32 = a.try_alloc(value(&count));
            if p == null && count == 1 { return 42; } return 1;
        }
    )",
                                   "try-failure.gloin", context, CompilationMode::Executable,
                                   CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    fail_runtime_call(*compiled.module, ArenaPrimitive::Allocate);
    auto result = JitRunner::run(*compiled.module);
    ASSERT_TRUE(result.success());
    EXPECT_EQ(result.value, 42);
}

TEST_F(ArenaTest, AlignedAllocationFailureReturnsErrorAfterInitializerSideEffects) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        import "@arena";
        import "@status";
        def value(count: &i32) -> i32 { *count = *count + 1; return 7; }
        def main() -> i32 {
            def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
            def mut count: i32 = 0;
            def p: result<&i32> = a.alloc_aligned<i32>(value(&count), 64);
            if p.erroneous {
                if p.error.code == status.NO_MEMORY && count == 1 { return 42; }
                return 1;
            }
            return 2;
        }
    )",
                                   "aligned-failure.gloin", context, CompilationMode::Executable,
                                   CompilationOutput::LLVM);
    ASSERT_TRUE(compiled.success());
    fail_runtime_call(*compiled.module, ArenaPrimitive::Allocate);
    auto result = JitRunner::run(*compiled.module);
    ASSERT_TRUE(result.success());
    EXPECT_EQ(result.value, 42);
}

TEST_F(ArenaTest, SliceAllocationUsesExactStorageBytesAlignmentAndCapturedFill) {
    for (const auto &[type, fill, count, bytes] :
         std::vector<std::tuple<std::string, std::string, unsigned, std::vector<unsigned>>>{
             {"packed(msb_first) [u3]", "5", 3, {0xB6, 0x80}},
             {"packed(lsb_first) [u3]", "5", 3, {0x6D, 0x01}},
             {"packed(msb_first) [be_u16]", "0x1234", 2, {0x12, 0x34, 0x12, 0x34}},
             {"[le_u16]", "0x1234", 2, {0x34, 0x12, 0x34, 0x12}},
             {"packed(msb_first) [u3]", "5", 0, {}}}) {
        SCOPED_TRACE(type);
        auto library_text = library();
        const std::string marker =
            "return __memory_initialize_slice<S>(storage, size.value, count, fill);";
        auto at = library_text.find(marker);
        ASSERT_NE(at, std::string::npos);
        std::string check =
            "if size.value != " + std::to_string(bytes.size()) +
            " || alignment != 64 { __arena_require(null); } "
            "def extent: u64 = size.value; "
            "def raw: [u8] = __memory_initialize_slice<[u8]>(storage, extent, extent, 255); "
            "def initialized: S = __memory_initialize_slice<S>(storage, extent, count, fill); "
            "if extent != 0 { def empty: [u8; 0] = zeroed; "
            "def placed: result<&[u8; 0]> = memory.place_at<[u8; 0]>(raw, 0, empty, 64); "
            "if placed.erroneous { __arena_require(null); } } ";
        for (size_t i = 0; i < bytes.size(); ++i)
            check += "if raw[" + std::to_string(i) + "] != " + std::to_string(bytes[i]) +
                     " { __arena_require(null); } ";
        check += "return initialized;";
        library_text.replace(at, marker.size(), check);
        source(library_text, "arena.gloin");
        auto file = program("def r: result<" + type + "> = a.alloc_slice<" + type + ">(" +
                            std::to_string(count) + ", " + fill +
                            ", 64); "
                            "if r.erroneous { return 1; } def view: " +
                            type + " = r.value; if view.len != " + std::to_string(count) +
                            " { return 2; } return 42;");
        expect_run(invoke({"--stdlib-dir", directory, file}), 42);
        for (const auto *mode : {"-O0", "-O2"}) {
            auto executable = directory + "/slice-storage";
            auto built = invoke_raw({"--stdlib-dir", directory, mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err;
            EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                      42);
        }
    }
}

TEST_F(ArenaTest, SliceAllocationSupportsOrdinaryValuesPackedUpdatesAndHugeEmptyElements) {
    auto file = program(R"(
        def mut calls: i32 = 0;
        def r: result<packed(msb_first) [u13]> = a.alloc_slice<packed(msb_first) [u13]>(17, value(&calls), 64);
        if r.erroneous { return 1; }
        def view: packed(msb_first) [u13] = r.value;
        def readonly: packed(msb_first) [const u13] = view;
        def mut i: u64 = 0;
        while i < view.len { if view[i] != 8191 { return 2; } i = i + 1; }
        view[8] = 42;
        if calls != 1 || readonly[8] != 42 || view[7] != 8191 || view[9] != 8191 { return 3; }
        def empty: result<packed(lsb_first) [u13]> = a.alloc_slice<packed(lsb_first) [u13]>(0, value(&calls), 1);
        if empty.erroneous { return 4; }
        if calls != 2 { return 5; }
        def ordinary: result<[Box]> = a.alloc_slice<[Box]>(3, Box { value: 99 }, 64);
        if ordinary.erroneous { return 6; }
        def boxes: [Box] = ordinary.value;
        boxes[1].value = 42;
        if boxes[0].value != 99 || boxes[1].value != 42 || boxes[2].value != 99 { return 7; }
        def huge: result<[Empty]> = a.alloc_slice<[Empty]>(18446744073709551615, Empty {}, 1);
        if huge.erroneous { return 8; }
        def zero: [Empty] = huge.value;
        if zero.len != 18446744073709551615 { return 9; }
        def last: &Empty = &zero[18446744073709551614];
        def tail: [Empty] = zero[18446744073709551614..];
        if tail.len != 1 || last != &tail[0] { return 10; }
        def past: [Empty] = zero[18446744073709551615..];
        if past.len != 0 { return 11; }
        return 42;
    )",
                        R"(
        def value(calls: &i32) -> u13 { *calls = *calls + 1; return 8191; }
        def struct Box { def mut value: u64, }
        def struct Empty {}
    )");
    expect_run(invoke({file}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/allocated-slices";
        auto built = invoke_raw({mode, "-o", executable, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
}

TEST_F(ArenaTest, SliceAllocationErrorsPreserveInitializerEffectsAndPrivateCapabilities) {
    for (const auto *type : {"[u13]", "packed(msb_first) [u13]"}) {
        mlir::MLIRContext context;
        auto text = std::string(R"(
            import "@arena"; import "@status";
            def value(calls: &i32) -> u13 { *calls = *calls + 1; return 7; }
            def main() -> i32 {
                def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
                def mut calls: i32 = 0;
                def r: result<)") +
                    type + "> = a.alloc_slice<" + type + R"(>(3, value(&calls), 64);
                if r.erroneous {
                    if r.error.code == status.NO_MEMORY && r.error.os_error == 0 && calls == 1 { return 42; }
                    return 1;
                } return 2;
            })";
        auto compiled = compile_source(text, "slice-failure.gloin", context,
                                       CompilationMode::Executable, CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success());
        fail_runtime_call(*compiled.module, ArenaPrimitive::Allocate);
        auto result = JitRunner::run(*compiled.module);
        ASSERT_TRUE(result.success());
        EXPECT_EQ(result.value, 42);
    }
    for (const auto *body :
         {"def r: result<[const u8]> = a.alloc_slice<[const u8]>(3, 0, 1); if r.erroneous { return "
          "1; }",
          "def r: result<u8> = a.alloc_slice<u8>(3, 0, 1); if r.erroneous { return 1; }",
          "def r: [u8] = __memory_initialize_slice<[u8]>(null, 0, 0, 0);"}) {
        auto file = program(std::string(body) + "return 0;");
        EXPECT_EQ(invoke({"--check", file}).status, 1);
    }
}

TEST_F(ArenaTest, HugeZeroSizedSlicesRejectNegativeSignedIndicesAndBounds) {
    for (const auto *expression :
         {"def item: Empty = view[negative];", "view[negative] = effect();",
          "def part: [Empty] = view[negative..];", "def part: [Empty] = view[0..negative];"}) {
        auto file = program(std::string(R"(
            def r: result<[Empty]> = a.alloc_slice<[Empty]>(18446744073709551615, Empty {}, 1);
            if r.erroneous { return 1; }
            def view: [Empty] = r.value;
            def negative: i64 = -2;
        )") + expression + " return 0;",
                            R"(
            import "@std";
            def struct Empty {}
            def effect() -> Empty { std.println("unexpected RHS"); return Empty {}; }
        )");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty());
        for (const auto *mode : {"-O0", "-O2"}) {
            auto executable = directory + "/negative-empty-slice";
            auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err;
            const auto output = directory + "/trap.out";
            const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output,
                                                                llvm::StringRef("")};
            EXPECT_LT(
                llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10),
                0);
            EXPECT_TRUE(read(output).empty());
        }
    }
}

TEST_F(ArenaTest, HugePackedEmptyRecordsRejectNegativeSignedIndicesAndBounds) {
    for (const auto *expression :
         {"def item: Empty = view[negative];", "view[negative] = effect();",
          "def part: packed(msb_first) [Empty] = view[negative..];",
          "def part: packed(msb_first) [Empty] = view[0..negative];"}) {
        auto file = program(std::string(R"(
            def r: result<packed(msb_first) [Empty]> = a.alloc_slice<packed(msb_first) [Empty]>(18446744073709551615, Empty {}, 1);
            if r.erroneous { return 1; }
            def view: packed(msb_first) [Empty] = r.value;
            def negative: i64 = -2;
        )") + expression + " return 0;",
                            R"(
            import "@std";
            def packed struct(msb_first) Empty {}
            def effect() -> Empty { std.println("unexpected RHS"); return Empty {}; }
        )");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty());
        for (const auto *mode : {"-O0", "-O2"}) {
            auto executable = directory + "/negative-empty-slice";
            auto built = invoke_raw({mode, "-o", executable, file});
            ASSERT_EQ(built.status, 0) << built.err;
            const auto output = directory + "/trap.out";
            const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), output,
                                                                llvm::StringRef("")};
            EXPECT_LT(
                llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10),
                0);
            EXPECT_TRUE(read(output).empty());
        }
    }
}

TEST_F(ArenaTest, InvalidSliceRequestsNeverAllocateAndGIRRejectsForgedCapabilities) {
    auto library_text = library();
    const std::string allocation =
        "def storage: *u8 = __arena_general_alloc(self.state, size.value, alignment);";
    size_t position = 0;
    while ((position = library_text.find(allocation, position)) != std::string::npos) {
        library_text.insert(position, "__arena_require(null); ");
        position += allocation.size() + 23;
    }
    source(library_text, "arena.gloin");
    auto file = program(
        R"(
        def mut calls: i32 = 0;
        def invalid: result<packed(msb_first) [u13]> = a.alloc_slice<packed(msb_first) [u13]>(3, value(&calls), 3);
        if invalid.erroneous { if invalid.error.code != status.INVALID { return 1; } }
        else { return 2; }
        def overflow: result<packed(msb_first) [u13]> = a.alloc_slice<packed(msb_first) [u13]>(18446744073709551615, value(&calls), 1);
        if overflow.erroneous { if overflow.error.code != status.OVERFLOW { return 3; } }
        else { return 4; }
        if calls != 2 { return 5; }
        return 42;
    )",
        R"(import "@status"; def value(calls: &i32) -> u13 { *calls = *calls + 1; return 7; })");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
    for (const auto *mode : {"-O0", "-O2"}) {
        auto executable = directory + "/invalid-slice";
        auto built = invoke_raw({"--stdlib-dir", directory, mode, "-o", executable, file});
        ASSERT_EQ(built.status, 0) << built.err;
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10), 42);
    }
    mlir::MLIRContext context;
    auto compiled = compile_source(read(file), file, context);
    ASSERT_TRUE(compiled.success());
    gloin::InitializeSliceOp initialized;
    compiled.module->walk([&](gloin::InitializeSliceOp op) { initialized = op; });
    ASSERT_TRUE(initialized);
    auto type = mlir::cast<gloin::GloinPackedSliceType>(initialized.getSourceType());
    auto readonly =
        gloin::GloinPackedSliceType::get(&context, type.getElement(), type.getMsbFirst(), true);
    initialized.setSourceType(readonly);
    initialized.getValue().setType(readonly);
    mlir::ScopedDiagnosticHandler suppress(&context,
                                           [](mlir::Diagnostic &) { return mlir::success(); });
    EXPECT_TRUE(mlir::failed(initialized.verify()));
}

TEST_F(ArenaTest, CreationAndInfallibleAllocationFailureTrap) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    for (auto kind : {ArenaPrimitive::Create, ArenaPrimitive::Allocate}) {
        mlir::MLIRContext context;
        auto compiled = compile_source(R"(
            import "@arena";
            def main() -> i32 {
                def mut a: arena.GeneralArena = arena.GeneralArena.create(); defer a.free();
                def p: &i32 = a.alloc(42); return *p;
            }
        )",
                                       "alloc-failure.gloin", context, CompilationMode::Executable,
                                       CompilationOutput::LLVM);
        ASSERT_TRUE(compiled.success());
        fail_runtime_call(*compiled.module, kind);
        EXPECT_EXIT(
            {
                auto result = JitRunner::run(*compiled.module);
                std::_Exit(result.success() ? 0 : 1);
            },
            [](int status) {
                return WIFSIGNALED(status) &&
                       (WTERMSIG(status) == SIGTRAP || WTERMSIG(status) == SIGILL);
            },
            "");
    }
}

TEST_F(ArenaTest, AllocationResultsComposeWithComparisonsAndNestedCalls) {
    auto file = program(R"(
        def p: &i32 = a.alloc(40 + 2);
        def pp: &&i32 = a.alloc(a.alloc(42));
        if a.alloc(1) == a.try_alloc(2) { return 1; }
        if a.alloc(1.0) == a.alloc(2.0) { return 2; }
        if a.alloc(true) == a.alloc(false) { return 3; }
        if **pp == 42 && *p == 42 && *a.alloc(!false) { return 42; }
        return 4;
    )");
    expect_run(invoke({file}), 42);
}
