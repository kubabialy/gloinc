#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include "target_layout.h"
#include "tool_paths.h"

namespace {
class OrdinaryStructTest : public gloin_test::CliFixture {};
} // namespace

TEST_F(OrdinaryStructTest, NestedValuesCallsReturnsCopiesAndFieldWrites) {
    auto file = source(R"(
        def main() -> i32 {
            def mut p: Pair = make();
            def copy: Pair = p;
            p.inner.value = 31;
            p.inner = Inner { value: 32 };
            p.tag = 9;
            if copy.inner.value != 10 { return 1; }
            return take(p) + copy.inner.value;
        }
        def take(p: Pair) -> i32 { return p.inner.value; }
        def make() -> Pair { return Pair { inner: Inner { value: 10 }, tag: 7 }; }
        def struct Pair { def mut tag: u8, def mut inner: Inner, }
        def struct Inner { def mut value: i32, }
    )");
    expect_run(invoke({file}), 42);
    expect_success(invoke({"--check", file}), "");
    auto lowered = invoke({"--emit-llvm", file});
    EXPECT_EQ(lowered.status, 0) << lowered.err;
    EXPECT_NE(lowered.out.find("llvm.getelementptr"), std::string::npos);
    EXPECT_NE(lowered.out.find("llvm.data_layout"), std::string::npos);
}

TEST_F(OrdinaryStructTest, MixedWidthSignednessAndFloatFieldsKeepTheirTypes) {
    auto file = source(R"(
        def struct Mixed { def byte: u8, def number: i64, def flag: bool, def f: f64, }
        def main() -> i32 {
            def m: Mixed = Mixed { f: 1.25, flag: true, number: -9223372036854775808, byte: 255 };
            if m.byte > 254 && 254 < m.byte && m.number < -1 && m.flag && m.f == 1.25 { return 42; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(OrdinaryStructTest, LiteralFieldEvaluationFollowsSourceOrderAndTemporaryBaseRunsOnce) {
    auto file = source(R"(
        import "@std";
        def struct Pair { def a: i32, def b: i32, }
        def first() -> i32 { std.print("first"); return 1; }
        def second() -> i32 { std.print("second"); return 2; }
        def make() -> Pair { std.print("make"); return Pair { b: first(), a: second() }; }
        def main() -> i32 { return make().a; }
    )");
    expect_run(invoke({file}), 2, "makefirstsecond");
}

TEST_F(OrdinaryStructTest, StringsAndEmptyStructsAreOrdinaryFields) {
    auto file = source(R"(
        import "@std";
        def struct Empty {}
        def struct Message { def mut text: string, def empty: Empty, }
        def echo(e: Empty) -> Empty { return e; }
        def main() -> i32 {
            def mut m: Message = Message { empty: echo(Empty {}), text: "hello" };
            std.print(m.text);
            m.text = " world";
            std.println(m.text);
            return 17;
        }
    )");
    expect_run(invoke({file}), 17, "hello world\n");
}

TEST_F(OrdinaryStructTest, WholeValueInitializationAndLoopStorage) {
    auto file = source(R"(
        def struct Point { def mut x: i32, }
        def main() -> i32 {
            def p: Point;
            if true { p = Point { x: 42 }; } else { p = Point { x: 10 }; }
            def mut i: i32 = 0;
            while i < 100000 {
                def mut q: Point = p;
                q.x = i;
                i = i + 1;
            }
            return p.x;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(OrdinaryStructTest, InvalidDeclarationsFailWithSourceLocations) {
    for (const std::string declaration :
         {"def struct P { def x: i32, def x: i64, }", "def struct P { def x: void, }",
          "def struct P { def x: Missing, }", "def struct P {} def struct P {}",
          "def struct P {} def P() -> void {}", "def struct i32 {}", "def struct P { def x: P, }",
          "def struct P { def q: Q, } def struct Q { def p: P, }",
          "def struct P { def x: i32 = 1, }", "def packed struct(u32) P { def x: u8, }",
          "def struct P { def method() -> void {} }",
          "def f() -> void { def struct Local {} }"}) {
        SCOPED_TRACE(declaration);
        auto file = source(declaration + "\ndef main() -> i32 { return 0; }");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, file + ":");
    }
}

TEST_F(OrdinaryStructTest, InvalidLiteralsAndUnknownFieldsAreRejectedBeforeExecution) {
    for (const std::string statement :
         {"def p: P = P { x: 1 };", "def p: P = P { x: 1, x: 2, y: 3 };",
          "def p: P = P { x: 1, z: 2 };", "def p: P = P { x: true, y: 2 };",
          "def p: P = P { x: 256, y: 2 };", "def p: P = P { x: 1, y: 2 }; p.missing;",
          "def mut p: P = P { x: 1, y: 2 }; p.missing = 42;",
          "def mut p: P = P { x: 1, y: 2 }; p.y = true;", "def p: P = Missing { x: 1, y: 2 };",
          "def p: P = i32 { x: 1 };"}) {
        SCOPED_TRACE(statement);
        auto file = source("def struct P { def x: u8, def mut y: i32, } "
                           "def main() -> i32 { " +
                           statement + " return 0; }");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(OrdinaryStructTest, NominalTypesDoNotCoerceEvenWithIdenticalLayouts) {
    for (const std::string body :
         {"def a: A = B { x: 1 }; return 0;",
          "def mut a: A = A { x: 1 }; a = B { x: 2 }; return 0;", "take(B { x: 1 }); return 0;",
          "if (A { x: 1 }) == (B { x: 1 }) { return 1; } return 0;",
          "def const a: A = A { x: 1 }; return 0;"}) {
        auto file = source("def struct A { def x: i32, } def struct B { def x: i32, } "
                           "def take(a: A) -> void {} def main() -> i32 { " +
                           body + " }");
        expect_error(invoke({file}), 1, "error:");
    }
    auto file = source("def struct A {} def struct B {} def wrong() -> A { return B {}; } "
                       "def main() -> i32 { return 0; }");
    expect_error(invoke({file}), 1, "Return type mismatch");
}

TEST_F(OrdinaryStructTest, MutabilityAppliesToRootAndEveryFieldInThePath) {
    for (const std::string statement :
         {"def p: P = P { inner: Inner { x: 1 }, fixed: 2 }; p.inner.x = 3;",
          "def mut p: P = P { inner: Inner { x: 1 }, fixed: 2 }; p.fixed = 3;",
          "make().inner.x = 3;", "def p: P; p.inner.x = 3;", "def mut p: P; p.inner.x;",
          "def p: P; if false { p = make(); } p.inner.x;",
          "def mut q: Frozen = Frozen { inner: Inner { x: 1 } }; q.inner.x = 3;"}) {
        auto file = source("def struct Inner { def mut x: i32, } "
                           "def struct P { def mut inner: Inner, def fixed: i32, } "
                           "def struct Frozen { def inner: Inner, } "
                           "def make() -> P { return P { inner: Inner { x: 1 }, fixed: 2 }; } "
                           "def main() -> i32 { " +
                           statement + " return 0; }");
        expect_error(invoke({file}), 1, "error:");
    }
    auto file = source("def struct P { def mut x: i32, } "
                       "def f(p: P) -> void { p.x = 3; } def main() -> i32 { return 0; }");
    expect_error(invoke({file}), 1, "mutable local");
}

TEST_F(OrdinaryStructTest, ModuleTypesAndFieldVisibility) {
    source(R"(
        def pub struct Pair { def pub mut x: i32, def secret: i32, }
        def struct Hidden { def value: i32, }
        def pub make() -> Pair { return Pair { secret: 7, x: 35 }; }
        def pub sum(p: Pair) -> i32 { return p.x + p.secret; }
    )",
           "records.gloin");
    auto file = source(R"(
        import "@records";
        def struct Pair { def x: i32, }
        def main() -> i32 {
            def mut p: records.Pair = records.make();
            p.x = 35;
            return records.sum(p);
        }
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
    for (const std::string statement :
         {"records.make().secret;", "def p: records.Pair = records.Pair { x: 1, secret: 2 };",
          "def p: records.Hidden;", "def p: Pair = records.make();",
          "def mut p: records.Pair = records.make(); p.secret = 2;"}) {
        file = source("import \"@records\"; def struct Pair { def x: i32, } "
                      "def main() -> i32 { " +
                      statement + " return 0; }");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "error:");
    }
    source("def pub struct Open { def pub x: i32, }", "records.gloin");
    file = source("import \"@records\"; def main() -> i32 { return records.Open { x: 42 }.x; }");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
}

TEST_F(OrdinaryStructTest, SameFilePrivateFieldsAndLowercaseTypeNamesWork) {
    auto file =
        source("def priv struct point { def priv mut x: i32, } "
               "def main() -> i32 { def mut p: point = point { x: 1 }; p.x = 42; return p.x; }");
    expect_run(invoke({file}), 42);
}

TEST_F(OrdinaryStructTest, LayoutUsesPaddingAndTargetPointerSize) {
    mlir::MLIRContext context;
    auto compiled = compile_source("def struct Mixed { def a: u8, def b: u64, def c: u16, } "
                                   "def identity(m: Mixed) -> Mixed { return m; }",
                                   "layout.gloin", context);
    ASSERT_TRUE(compiled.success());
    auto function = compiled.module->lookupSymbol<mlir::func::FuncOp>("identity");
    auto data = (*compiled.module)->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
    ASSERT_TRUE(data);
    auto layout =
        measure_type_layout(function.getArgumentTypes()[0], llvm::DataLayout(data.getValue()));
    ASSERT_TRUE(static_cast<bool>(layout)) << llvm::toString(layout.takeError());
    EXPECT_EQ(layout->size, 24u);
    EXPECT_EQ(layout->alignment, 8u);
    EXPECT_EQ(layout->field_offsets, (std::vector<uint64_t>{0, 8, 16}));
    auto pointer_record = mlir::LLVM::LLVMStructType::getLiteral(
        &context, {mlir::IntegerType::get(&context, 8), mlir::LLVM::LLVMPointerType::get(&context),
                   mlir::IntegerType::get(&context, 8)});
    auto narrow = measure_type_layout(pointer_record, llvm::DataLayout("e-p:32:32"));
    auto wide = measure_type_layout(pointer_record, llvm::DataLayout("e-p:64:64"));
    ASSERT_TRUE(static_cast<bool>(narrow)) << llvm::toString(narrow.takeError());
    ASSERT_TRUE(static_cast<bool>(wide)) << llvm::toString(wide.takeError());
    EXPECT_EQ(narrow->size, 12u);
    EXPECT_EQ(narrow->alignment, 4u);
    EXPECT_EQ(narrow->field_offsets, (std::vector<uint64_t>{0, 4, 8}));
    EXPECT_EQ(wide->size, 24u);
    EXPECT_EQ(wide->alignment, 8u);
    EXPECT_EQ(wide->field_offsets, (std::vector<uint64_t>{0, 8, 16}));
}

TEST_F(OrdinaryStructTest, StructTypesDoNotLeakAcrossCompilationsSharingAContext) {
    mlir::MLIRContext context;
    for (const std::string type : {"u8", "i64", "f64"}) {
        SCOPED_TRACE(type);
        const std::string literal = type == "f64" ? "42.0" : "42";
        auto compiled =
            compile_source("def struct P { def x: " + type +
                               ", } "
                               "def main() -> i32 { def p: P = P { x: " +
                               literal + " }; if p.x == " + literal + " { return 42; } return 0; }",
                           "reuse.gloin", context, CompilationMode::Executable);
        ASSERT_TRUE(compiled.success());
        auto result = JitRunner::run(*compiled.module);
        ASSERT_TRUE(result.success());
        EXPECT_EQ(result.value, 42);
    }
}

TEST_F(OrdinaryStructTest, AggregateCallsAlsoExecuteThroughExternalLLVMRunner) {
    mlir::MLIRContext context;
    auto compiled = compile_source(
        "def struct P { def a: u8, def mut x: i64, def b: u16, } "
        "def identity(p: P) -> P { return p; } "
        "def main() -> i32 { def mut p: P = identity(P { a: 255, x: 41, b: 65535 }); "
        "p.x = p.x + 1; if p.a == 255 && p.x == 42 && p.b == 65535 { return 42; } return 1; }",
        "external-struct.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    auto result = gloin_test::run_external_module(*compiled.module, {gloin_test::mlir_opt, {}},
                                                  {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
