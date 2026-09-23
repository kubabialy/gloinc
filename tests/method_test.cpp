#include "codegen.h"
#include "compiler.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"

namespace {
class MethodTest : public gloin_test::CliFixture {};
} // namespace

TEST_F(MethodTest, PersonGreetsAndConstructsThroughStaticMethod) {
    auto file = source(R"(
        import "@std";
        def main() -> i32 {
            def person: Person = Person.create("Alice", 25);
            person.greet();
            if person.is_adult() { return 42; }
            return 1;
        }
        def struct Person {
            def name: string, def age: i32,
            def pub static create(name: string, age: i32) -> Person {
                return Person { age: age, name: name };
            }
            def pub greet(self: &const Person) -> void {
                std.print("Hello, I'm "); std.println(self.name);
            }
            def pub is_adult(self: *const Person) -> bool { return self.age >= 18; }
        }
    )");
    expect_run(invoke({file}), 42, "Hello, I'm Alice\n");
    expect_success(invoke({"--check", file}), "");
    for (const std::string mode : {"--emit-ir", "--emit-llvm"}) {
        auto result = invoke({mode, file});
        EXPECT_EQ(result.status, 0) << result.err;
        EXPECT_NE(result.out.find("gloin.method."), std::string::npos);
    }
}

TEST_F(MethodTest, MutatesOriginalThroughValuesPointersReferencesAndNestedFields) {
    auto file = source(R"(
        def struct Counter {
            def mut value: i64,
            def add(self: *Counter, amount: i64) -> void { self.value = self.value + amount; }
            def read(self: &const Counter) -> i64 { return self.value; }
        }
        def struct Box { def mut counter: Counter, }
        def main() -> i32 {
            def mut box: Box = Box { counter: Counter { value: 1 } };
            box.counter.add(2);
            def ref: &Counter = &box.counter;
            ref.add(3);
            def ptr: *Counter = ref;
            ptr.add(4);
            (*ptr).add(5);
            if (&*ptr).read() != 15 { return 2; }
            def view: &const Counter = ref;
            if view.read() == 15 && box.counter.value == 15 { return 42; }
            return 1;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, ReadOnlyParametersAndLocalsHaveAddressableReceiverStorage) {
    auto file = source(R"(
        def struct P { def value: i32, def get(self: &const P) -> i32 { return self.value; } }
        def read(p: P) -> i32 { return p.get(); }
        def main() -> i32 { def p: P = P { value: 21 }; return p.get() + read(p); }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, ReceiverIsEvaluatedOnceBeforeArguments) {
    auto file = source(R"(
        import "@std";
        def struct P {
            def mut value: i32,
            def add(self: &P, a: i32, b: i32) -> i32 {
                std.print("M"); self.value = self.value + a + b; return self.value;
            }
        }
        def receiver(p: &P) -> &P { std.print("R"); return p; }
        def arg(text: string, n: i32) -> i32 { std.print(text); return n; }
        def main() -> i32 {
            def mut p: P = P { value: 1 };
            return receiver(&p).add(arg("A", 20), arg("B", 21));
        }
    )");
    expect_run(invoke({file}), 42, "RABM");
}

TEST_F(MethodTest, ForwardRecursiveAndMutuallyRecursiveMethods) {
    auto file = source(R"(
        def main() -> i32 { def p: P = P {}; if p.even(20) { return P.fact(5) - 78; } return 1; }
        def struct P {
            def even(self: &const P, n: i32) -> bool {
                if n == 0 { return true; } return self.odd(n - 1);
            }
            def odd(self: &const P, n: i32) -> bool {
                if n == 0 { return false; } return self.even(n - 1);
            }
            def static fact(n: i32) -> i32 { if n <= 1 { return 1; } return n * P.fact(n - 1); }
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, AggregateAndPointerResultsKeepTypeContext) {
    auto file = source(R"(
        import "@std";
        def struct P {
            def mut n: u8, def text: string,
            def static make() -> P { return P { text: "ok", n: 255 }; }
            def copy(self: &const P) -> P { return *self; }
            def address(self: &P) -> &u8 { return &self.n; }
            def identity(self: &P) -> &P { return self; }
            def read(self: &const P) -> u8 { return self.n; }
        }
        def main() -> i32 {
            def mut p: P = P.make();
            def copy: P = p.copy();
            if p.address() == null || p.address() != &p.n { return 1; }
            if p.identity().read() != 255 || 255 != copy.read() { return 2; }
            *p.address() = 42;
            std.println(copy.text);
            if copy.n == 255 && p.n == 42 { return 42; }
            return 3;
        }
    )");
    expect_run(invoke({file}), 42, "ok\n");
}

TEST_F(MethodTest, MethodSignaturesContainExactlyOneSelfAndStaticContainsNone) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def struct P {
            def x: i32,
            def read(self: *const P, extra: i64) -> i64 { return extra; }
            def static make(x: i32) -> P { return P { x: x }; }
        }
    )",
                                   "signature.gloin", context);
    ASSERT_TRUE(compiled.success());
    auto instance = compiled.module->lookupSymbol<mlir::func::FuncOp>("gloin.method.0.read");
    auto stat = compiled.module->lookupSymbol<mlir::func::FuncOp>("gloin.method.0.make");
    ASSERT_TRUE(instance);
    ASSERT_TRUE(stat);
    ASSERT_EQ(instance.getNumArguments(), 2u);
    EXPECT_TRUE(llvm::isa<mlir::LLVM::LLVMPointerType>(instance.getArgumentTypes()[0]));
    EXPECT_TRUE(instance.getArgumentTypes()[1].isInteger(64));
    ASSERT_EQ(stat.getNumArguments(), 1u);
    EXPECT_TRUE(stat.getArgumentTypes()[0].isInteger(32));
}

TEST_F(MethodTest, NullableSelfMayCheckNullButDereferencingItTraps) {
    auto file = source(R"(
        def struct P {
            def x: i32,
            def empty(self: *const P) -> bool { return self == null; }
            def read(self: *const P) -> i32 { return self.x; }
        }
        def main() -> i32 { def p: *const P = null; if p.empty() { return 42; } return 1; }
    )");
    expect_run(invoke({file}), 42);
    auto program = read(file);
    const std::string body = "if p.empty() { return 42; } return 1;";
    program.replace(program.find(body), body.size(), "return p.read();");
    file = source(program, "trap.gloin");
    expect_success(invoke({"--check", file}), "");
    auto result = invoke({file});
    EXPECT_LT(result.status, 0) << result.err;
    EXPECT_TRUE(result.out.empty());
    EXPECT_TRUE(result.err.empty());
}

TEST_F(MethodTest, NamesAreIsolatedFromOtherTypesAndGlobalFunctions) {
    auto file = source(R"(
        def struct A { def static get() -> i32 { return 10; } }
        def struct B { def static get() -> i32 { return 20; } }
        def A_get() -> i32 { return 5; }
        def get() -> i32 { return 7; }
        def main() -> i32 { return A.get() + B.get() + A_get() + get(); }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, ModulePublicMethodsCanUsePrivateFieldsAndPrivateHelpers) {
    source(R"(
        def pub struct Counter {
            def mut value: i32,
            def pub static make(value: i32) -> Counter { return Counter { value: value }; }
            def helper(self: &const Counter) -> i32 { return self.value; }
            def pub get(self: &const Counter) -> i32 { return self.helper(); }
            def pub add(self: &Counter, n: i32) -> void { self.value = self.value + n; }
        }
    )",
           "counter.gloin");
    auto file = source(R"(
        import "@counter";
        def main() -> i32 {
            def mut c: counter.Counter = counter.Counter.make(40);
            c.add(2); return c.get();
        }
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
    for (const std::string expression : {"c.helper()", "c.value", "counter.Counter.helper()"}) {
        file = source("import \"@counter\"; def main() -> i32 { "
                      "def c: counter.Counter = counter.Counter.make(42); return " +
                      expression + "; }");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "error:");
    }
}

TEST_F(MethodTest, PrivateStaticMethodsAndModuleTypesCannotBeCalledFromOutside) {
    source(R"(
        def pub struct P { def static make() -> i32 { return 42; } }
        def struct Secret { def pub static make() -> i32 { return 42; } }
    )",
           "hidden.gloin");
    for (const std::string name : {"P", "Secret"}) {
        auto file =
            source("import \"@hidden\"; def main() -> i32 { return hidden." + name + ".make(); }");
        expect_error(invoke({"--stdlib-dir", directory, file}), 1, "error:");
    }
}

TEST_F(MethodTest, InvalidMethodSignaturesFailInSema) {
    for (const std::string method :
         {"def f() -> void {}", "def f(x: *P) -> void {}", "def f(self: P) -> void {}",
          "def f(self: **P) -> void {}", "def f(self: *Q) -> void {}",
          "def f(self: i32) -> void {}", "def static f(self: *P) -> void {}",
          "def f(x: i32, self: *P) -> void {}", "def f(self: *P, self: *P) -> void {}",
          "def f(self: *P) -> void {} def f(self: *P) -> void {}",
          "def f: i32, def f(self: *P) -> void {}"}) {
        SCOPED_TRACE(method);
        mlir::MLIRContext context;
        auto compiled = compile_source("def struct Q {} def struct P { " + method + " }",
                                       "invalid-method.gloin", context);
        EXPECT_FALSE(compiled.success());
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic);
    }
}

TEST_F(MethodTest, BodiesAreCheckedEvenWhenMethodsAreNeverCalled) {
    for (const std::string method :
         {"def f(self: &const P) -> i32 {}", "def f(self: &P) -> i32 { return true; }",
          "def f(self: &const P) -> void { self.x = 42; }",
          "def f(self: &P) -> void { self = self; }", "def f(self: &P) -> void { self.missing; }",
          "def static f() -> i32 { return self.x; }"}) {
        SCOPED_TRACE(method);
        mlir::MLIRContext context;
        auto compiled = compile_source("def struct P { def mut x: i32, " + method + " }",
                                       "bad-body.gloin", context);
        EXPECT_FALSE(compiled.success());
        EXPECT_EQ(compiled.failed_stage, DiagnosticStage::Semantic);
    }
}

TEST_F(MethodTest, RejectsWrongCallFormArityTypesAndFirstClassMethods) {
    for (const std::string statement :
         {"P.read();", "p.make();", "p.read(1);", "P.make(true);", "P.missing();", "p.missing();",
          "p.read;", "P.make;", "def f: i32 = p.read;", "def n: bool = p.read();", "p.read(&p);"}) {
        SCOPED_TRACE(statement);
        auto file = source("def struct P { def x: i32, "
                           "def read(self: &const P) -> i32 { return self.x; } "
                           "def static make(x: i32) -> P { return P { x: x }; } } "
                           "def main() -> i32 { def p: P = P { x: 42 }; " +
                           statement + " return 0; }");
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}

TEST_F(MethodTest, RejectsReadonlyUninitializedTemporaryAndNullableReferenceReceivers) {
    for (const std::string statement :
         {"def p: P = P { x: 1 }; p.set();", "def mut p: P; p.set();",
          "def mut p: P = P { x: 1 }; def r: &const P = &p; r.set();",
          "def mut p: P = P { x: 1 }; def r: *const P = &p; r.set();", "def p: *P = null; p.set();",
          "P.make().read();", "(P { x: 1 }).read();",
          "def b: Box = Box { p: P { x: 1 } }; b.p.set();"}) {
        SCOPED_TRACE(statement);
        auto file = source("def struct P { def mut x: i32, "
                           "def set(self: &P) -> void { self.x = 42; } "
                           "def read(self: &const P) -> i32 { return self.x; } "
                           "def static make() -> P { return P { x: 1 }; } } "
                           "def struct Box { def mut p: P, } "
                           "def main() -> i32 { " +
                           statement + " return 0; }");
        expect_error(invoke({file}), 1, "error:");
    }
}

TEST_F(MethodTest, LoopReceiversUseEntryStorageAndPreserveAliases) {
    auto file = source(R"(
        def struct P {
            def mut x: i32,
            def bump(self: &P) -> &P { self.x = self.x + 1; return self; }
            def read(self: &const P) -> i32 { return self.x; }
        }
        def main() -> i32 {
            def mut total: i32 = 0;
            for def mut i: i32 = 0; i < 1000000; i = i + 1 {
                def mut p: P = P { x: 0 };
                if p.bump() != &p { return 1; }
                total = total + p.read();
            }
            if total == 1000000 { return 42; } return 2;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, PointerFieldsRetainTheirOwnCapabilityAndReceiverPrecedesArgumentRebinding) {
    auto file = source(R"(
        def struct P {
            def mut x: i32,
            def set(self: *P, n: i32) -> void { self.x = n; }
        }
        def struct View { def p: &P, }
        def rebind(slot: &*P, other: &P) -> i32 { *slot = other; return 42; }
        def main() -> i32 {
            def mut a: P = P { x: 1 }; def mut b: P = P { x: 2 };
            def view: View = View { p: &a };
            view.p.set(3);
            if a.x != 3 { return 1; }
            def mut p: *P = &a;
            p.set(rebind(&p, &b));
            if a.x == 42 && b.x == 2 && p == &b { return 42; } return 2;
        }
    )");
    expect_run(invoke({file}), 42);
}

TEST_F(MethodTest, SameNamedModuleTypesHaveIndependentMethodsAndNominalArguments) {
    for (const std::string module : {"first", "second"})
        source("def pub struct P { def pub x: i32, "
               "def pub static make() -> P { return P { x: 21 }; } "
               "def pub read(self: &const P) -> i32 { return self.x; } "
               "def pub same(self: &const P, other: &const P) -> bool { return self == other; } }",
               module + ".gloin");
    auto file = source(R"(
        import "@first"; import "@second";
        def main() -> i32 {
            def a: first.P = first.P.make(); def b: second.P = second.P.make();
            return a.read() + b.read();
        }
    )");
    expect_run(invoke({"--stdlib-dir", directory, file}), 42);
    file = source(R"(
        import "@first"; import "@second";
        def main() -> i32 {
            def a: first.P = first.P.make(); def b: second.P = second.P.make();
            a.same(&b); return 0;
        }
    )");
    expect_error(invoke({"--stdlib-dir", directory, file}), 1, "type mismatch");
}

TEST_F(MethodTest, MethodsAlsoExecuteThroughExternalLLVMRunner) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(
        def struct P {
            def mut value: i64,
            def static make() -> P { return P { value: 40 }; }
            def add(self: &P, n: i64) -> i64 { self.value = self.value + n; return self.value; }
        }
        def main() -> i32 { def mut p: P = P.make(); if p.add(2) == 42 { return 42; } return 1; }
    )",
                                   "external-method.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    auto result = gloin_test::run_external_module(*compiled.module, {gloin_test::mlir_opt, {}},
                                                  {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
