#include "codegen.h"
#include "compiler.h"
#include "jit_runner.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include "target_layout.h"
#include "tool_paths.h"
#include <tuple>

namespace {
class PointerTest : public gloin_test::CliFixture {};
} // namespace

TEST_F(PointerTest, SpecificationReferenceExampleMutatesTheOriginalObject) {
    auto file = source(R"(
        import "@std";
        def main() -> i32 {
            def mut value: i32 = 42;
            def ptr: &i32 = &value;
            if *ptr != 42 { return 1; }
            *ptr = 100;
            std.println("Value updated through reference");
            return value;
        }
    )");
    expect_run(invoke({file}), 100, "Value updated through reference\n");
}

TEST_F(PointerTest, EveryScalarPointeeUsesItsOwnStorageType) {
    for (const auto &[type, before, after] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"i8", "-128", "127"},
             {"i16", "-32768", "32767"},
             {"i32", "-2147483648", "2147483647"},
             {"i64", "-9223372036854775808", "9223372036854775807"},
             {"u8", "0", "255"},
             {"u16", "0", "65535"},
             {"u32", "0", "4294967295"},
             {"u64", "0", "18446744073709551615"},
             {"f32", "1.25", "-2.5"},
             {"f64", "1.25", "-2.5"},
             {"bool", "false", "true"}}) {
        SCOPED_TRACE(type);
        auto file = source("def write(p: &" + type + ") -> void { *p = " + after +
                           "; } "
                           "def main() -> i32 { def mut value: " +
                           type + " = " + before +
                           "; "
                           "def p: &" +
                           type + " = &value; if *p != " + before +
                           " { return 1; } "
                           "write(p); if value == " +
                           after + " && *p == " + after + " { return 42; } return 2; }");
        expect_run(invoke({file}), 42);
    }
}

TEST_F(PointerTest, ReadOnlyLocalsAndParametersHaveStableAddressableStorage) {
    auto file = source(R"(
        def load(p: &const i64) -> i64 { return *p; }
        def parameter(value: i64) -> i64 { return load(&value); }
        def main() -> i32 {
            def value: i64 = 42;
            def p: &const i64 = &value;
            if p != &value { return 1; }
            if load(p) == 42 && parameter(42) == 42 { return 42; }
            return 2;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, PointerBindingMutabilityIsSeparateFromPointeeMutability) {
    auto file = source(R"(
        def main() -> i32 {
            def mut a: i32 = 1;
            def mut b: i32 = 2;
            def mut p: *i32 = &a;
            *p = 3;
            p = &b;
            *p = 39;
            def q: &i32 = &b;
            *q = *q + a;
            return b;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, CapabilityWeakeningWorksInArgumentsReturnsFieldsAndAssignments) {
    auto file = source(R"(
        def struct View { def mut pointer: *const i32, }
        def readonly(p: &const i32) -> *const i32 { return p; }
        def main() -> i32 {
            def mut value: i32 = 42;
            def r: &i32 = &value;
            def c: &const i32 = r;
            def mut v: View = View { pointer: r };
            v.pointer = c;
            def p: *const i32 = readonly(r);
            if p == v.pointer && p != null { return *p; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, NestedPointersPreservePointeeIdentityAndQualifiers) {
    auto file = source(R"(
        def main() -> i32 {
            def mut a: i32 = 1;
            def mut b: i32 = 2;
            def mut r: &i32 = &a;
            def rr: &&i32 = &r;
            *rr = &b;
            **rr = 42;
            def mut p: *i32 = &b;
            def pp: **i32 = &p;
            def readslot: &const *i32 = &p;
            if *readslot == &b && **pp == 42 && a == 1 { return b; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, RecursiveStructPointersAndNestedFieldAddressesWork) {
    auto file = source(R"(
        def struct Node { def mut value: i64, def mut next: *Node, }
        def main() -> i32 {
            def mut tail: Node = Node { value: 2, next: null };
            def mut head: Node = Node { value: 10, next: &tail };
            def root: &Node = &head;
            root.next.value = 32;
            def v: &i64 = &root.next.value;
            if v != &tail.value { return 1; }
            def mut cursor: *Node = root;
            def mut total: i64 = 0;
            while cursor != null { total = total + cursor.value; cursor = cursor.next; }
            if total == 42 { return 42; }
            return 2;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, ReferencesToStringsAndWholeStructsUseAggregateLoadsAndStores) {
    auto file = source(R"(
        import "@std";
        def struct Value { def small: u8, def text: string, def large: u64, }
        def replace(p: &Value) -> void { *p = Value { small: 255, text: "ok", large: 18446744073709551615 }; }
        def main() -> i32 {
            def mut value: Value = Value { small: 0, text: "before", large: 0 };
            replace(&value);
            def mut text: string = "old";
            def p: &string = &text;
            *p = value.text;
            std.println(*p);
            if value.small == 255 && value.large == 18446744073709551615 { return 42; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42, "ok\n");
}

TEST_F(PointerTest, NullChecksAndShortCircuitingDoNotDereferenceThePointer) {
    auto file = source(R"(
        def empty() -> *i32 { return null; }
        def accepts(p: *const i32) -> bool { return null == p; }
        def main() -> i32 {
            def mut p: *i32;
            p = null;
            if p != null && *p == 42 { return 1; }
            if p == null || *p == 42 {
                if accepts(null) && empty() == null { return 42; }
            }
            return 2;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, NullableDereferencesTrapBeforeAnyLoadStoreOrReferenceCreation) {
    for (const std::string body :
         {"def p: *i32 = null; *p;", "def p: *i32 = null; *p = 42;",
          "def p: *i32 = null; def r: &i32 = &*p;", "def p: *Point = null; p.x;",
          "def p: *Point = null; p.x = 42;", "def p: *Point = null; def r: &i32 = &p.x;"}) {
        SCOPED_TRACE(body);
        auto file = source("def struct Point { def mut x: i32, } def main() -> i32 { " + body +
                           " return 0; }");
        expect_success(invoke({"--check", file}), "");
        auto result = invoke({file});
        EXPECT_LT(result.status, 0) << result.err;
        EXPECT_TRUE(result.out.empty());
        EXPECT_TRUE(result.err.empty()) << result.err;
        EXPECT_TRUE(result.message.find("Trace") != std::string::npos ||
                    result.message.find("Illegal instruction") != std::string::npos)
            << result.message;
    }
}

TEST_F(PointerTest, AddressesAndIndirectAssignmentOperandsEvaluateOnceInSourceOrder) {
    auto file = source(R"(
        import "@std";
        def select(p: &i32) -> *i32 { std.print("L"); return p; }
        def value() -> i32 { std.print("R"); return 42; }
        def main() -> i32 {
            def mut x: i32 = 0;
            *select(&x) = value();
            def r: &i32 = &*select(&x);
            return *r;
        }
    )");
    expect_run(invoke({file}), 42, "LRL");
}

TEST_F(PointerTest, TemporariesFunctionsConstantsAndUninitializedValuesCannotBeReferenced) {
    for (const std::string body :
         {"def r: &const i32 = &42;", "def r: &const i32 = &(1 + 2);",
          "def r: &const i32 = &make();", "def const X: i32 = 1; def r: &const i32 = &X;",
          "def mut x: i32; def r: &i32 = &x;", "def r: &const i32 = &make;",
          "def r: &const i32 = &(Point { x: 1 }).x;", "def mut p: *i32; *p = 42;",
          "def mut p: *i32; &*p;", "*42;"}) {
        SCOPED_TRACE(body);
        auto file = source("def struct Point { def x: i32, } def make() -> i32 { return 1; } "
                           "def main() -> i32 { " +
                           body + " return 0; }");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(PointerTest, ReadOnlyStorageCannotAcquireWritableAliases) {
    for (const std::string body :
         {"def x: i32 = 1; def p: &i32 = &x;", "def x: i32 = 1; def p: *i32 = &x;",
          "def x: i32 = 1; def p: &const i32 = &x; *p = 2;",
          "def x: i32 = 1; def p: *const i32 = &x; def r: &i32 = &*p;",
          "def x: i32 = 1; def mut p: *const i32 = &x; *p = 2;",
          "def mut p: Point = Point { x: 1 }; def r: &i32 = &p.x;",
          "def mut p: Point = Point { x: 1 }; def r: &Point = &p; r.x = 2;",
          "def mut p: Mutable = Mutable { x: 1 }; def r: &const Mutable = &p; r.x = 2;",
          "def mut x: i32 = 1; def mut p: *i32 = &x; def pp: **const i32 = &p;",
          "def mut x: i32 = 1; def mut p: &i32 = &x; def pp: &*i32 = &p;"}) {
        SCOPED_TRACE(body);
        auto file =
            source("def struct Point { def x: i32, } def struct Mutable { def mut x: i32, } "
                   "def main() -> i32 { " +
                   body + " return 0; }");
        expect_error(invoke({file}), 1, "error:");
    }
}

TEST_F(PointerTest, PointeeMismatchesNullReferencesAndInvalidPointerOperatorsAreRejected) {
    for (const std::string body :
         {"def p: &i32 = null;", "def p: &const i32 = null;", "def p: *void = null;",
          "def p: &void;", "def p: *Missing = null;", "def p: *i32 = 0;",
          "def mut x: i64 = 1; def p: *i32 = &x;", "def p: *i32 = null; def r: &i32 = p;",
          "def p: *i32 = null; p < p;", "def p: *i32 = null; p - 1;",
          "def p: *i32 = null; 1 + p;", "def p: *i32 = null; p + true;",
          "def mut x: i32 = 1; def p: &i32 = &x; p + 1;",
          "def p: *i32 = null; def q: *u32 = null; p == q;", "null == null;",
          "def p: *i32 = null; if p { return 1; }", "def p: *i32 = null; *p = true;",
          "def const p: *i32 = null;", "def p: *i32 = null; consume(p);",
          "def p: *i32 = null; def r: *u32 = p;"}) {
        SCOPED_TRACE(body);
        auto file =
            source("def consume(p: &i32) -> void {} def main() -> i32 { " + body + " return 0; }");
        expect_error(invoke({file}), 1, "error:");
    }
    for (const std::string function :
         {"def bad() -> &i32 { return null; }", "def bad(p: &const i32) -> &i32 { return p; }"}) {
        auto file = source(function + " def main() -> i32 { return 0; }");
        expect_error(invoke({file}), 1, "error:");
    }
}

TEST_F(PointerTest, NullablePointerOffsetsAdvanceByElementsAndPreserveReadOnlyCapability) {
    const auto file = source(R"(
        def main() -> i32 {
            def mut data: [i32; 3] = {10, 20, 12};
            def first: *i32 = &data[0];
            def second: *i32 = first + 1;
            def last: *const i32 = second + 1;
            def before_last: *const i32 = last + -1;
            if *before_last == 20 && *last == 12 { return *second + *last; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 32);
    expect_success(invoke({"--check", file}), "");
}

TEST_F(PointerTest, NullPointerOffsetTrapsBeforeAddressCalculation) {
    const auto file = source("def main() -> i32 { def p: *i32 = null; def q: *i32 = p + 0; return 1; }");
    expect_success(invoke({"--check", file}), "");
    auto result = invoke({file});
    EXPECT_LT(result.status, 0) << result.err;
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST_F(PointerTest, LiveAliasesAndReturnedCallerReferencesNeedNoBorrowChecker) {
    auto file = source(R"(
        def same(p: &i32) -> &i32 { return p; }
        def add(a: &i32, b: &i32) -> void { *a = *a + 1; *b = *b + 1; }
        def main() -> i32 {
            def mut x: i32 = 40;
            def a: &i32 = &x;
            def b: &const i32 = &x;
            add(a, same(&x));
            return *b;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(PointerTest, ModulePointersRespectFieldVisibilityAndNominalIdentity) {
    source(R"(
        def pub struct Data { def pub mut value: i32, def secret: i32, }
        def pub make() -> Data { return Data { value: 42, secret: 7 }; }
        def pub view(p: &Data) -> &const Data { return p; }
    )",
           "records.gloin");
    auto file =
        source("import \"@records\"; def main() -> i32 { def mut d: records.Data = records.make(); "
               "def p: &const records.Data = records.view(&d); return p.value; }");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
    for (const std::string operation :
         {"p.secret;", "p.secret = 3;", "def r: &const i32 = &p.secret;", "p.value = 3;"}) {
        file = source(
            "import \"@records\"; def main() -> i32 { def mut d: records.Data = records.make(); "
            "def p: &const records.Data = records.view(&d); " +
            operation + " return 0; }");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "error:");
    }
}

TEST_F(PointerTest, LoopAddressesUseEntryStorageAndDoNotGrowTheStack) {
    const std::string code = R"(
        def main() -> i32 {
            def mut total: i32 = 0;
            while total < 1000000 {
                def item: i64 = 42;
                def p: &const i64 = &item;
                if *p != 42 { return 1; }
                total = total + 1;
            }
            return 42;
        }
    )";
    expect_run(invoke({source(code)}), 42);
    mlir::MLIRContext context;
    auto compiled = compile_source(code, "loop-pointer.gloin", context);
    ASSERT_TRUE(compiled.success());
    for (auto function : compiled.module->getOps<mlir::func::FuncOp>()) {
        size_t allocations = 0;
        function.walk([&](mlir::LLVM::AllocaOp slot) {
            ++allocations;
            EXPECT_EQ(slot->getBlock(), &function.getBody().front());
        });
        EXPECT_EQ(allocations, 2u);
    }
}

TEST_F(PointerTest, PointerFieldsUseNativeLayoutAndExternalExecution) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def struct Record { def small: u8, def p: *i64, def r: &const f64, def tail: u16, }
        def layout(value: Record) -> Record { return value; }
        def update(p: &i64) -> void { *p = 42; }
        def main() -> i32 {
            def mut x: i64 = 1;
            def f: f64 = 1.25;
            def r: Record = layout(Record { small: 255, p: &x, r: &f, tail: 65535 });
            update(&*r.p);
            if *r.p == 42 && *r.r == 1.25 && r.small == 255 && r.tail == 65535 { return 42; }
            return 1;
        }
    )",
                                   "pointer-layout.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    auto function = compiled.module->lookupSymbol<mlir::func::FuncOp>("layout");
    auto data = (*compiled.module)->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
    auto layout =
        measure_type_layout(function.getArgumentTypes()[0], llvm::DataLayout(data.getValue()));
    ASSERT_TRUE(static_cast<bool>(layout)) << llvm::toString(layout.takeError());
    EXPECT_EQ(layout->size, 32u);
    EXPECT_EQ(layout->alignment, 8u);
    EXPECT_EQ(layout->field_offsets, (std::vector<uint64_t>{0, 8, 16, 24}));
    auto result = gloin_test::run_external_module(*compiled.module, {gloin_test::mlir_opt, {}},
                                                  {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
