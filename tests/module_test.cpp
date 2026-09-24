#include "codegen.h"
#include "compiler.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>

namespace {
namespace fs = std::filesystem;
class ModuleTest : public gloin_test::CliFixture {
  protected:
    std::string file(const std::string &text, const std::string &name) {
        fs::create_directories(fs::path(directory) / fs::path(name).parent_path());
        return source(text, name);
    }
    void rejects(const std::string &path, const std::string &reason) {
        for (const std::string mode : {"--check", "--run", "--emit-ir", "--emit-llvm"})
            expect_error(invoke({mode, path}), 1, reason);
    }
};
} // namespace

TEST_F(ModuleTest, SpecificationExampleResolvesRelativeToSourceNotWorkingDirectory) {
    file("def pub calculate(x: i32, y: i32) -> i32 { return x * y + 10; }", "app/utils.gloin");
    auto main = file("import \"./utils\"; def main() -> i32 { return utils.calculate(5, 3); }",
                     "app/main.gloin");
    EXPECT_NE(fs::current_path(), fs::path(main).parent_path());
    expect_run(invoke({main}), 25);
    expect_success(invoke({"--check", main}), "");
}

TEST_F(ModuleTest, NestedParentPathsExplicitExtensionsAndSpacesResolve) {
    file("def pub answer() -> i32 { return 42; }", "tree/shared.gloin");
    file("import \"../shared.gloin\"; def pub answer() -> i32 { return shared.answer(); }",
         "tree/a folder/worker.gloin");
    auto main = file("import \"./a folder/worker\"; def main() -> i32 { return worker.answer(); }",
                     "tree/main.gloin");
    expect_run(invoke({main}), 42);
}

TEST_F(ModuleTest, DiamondGraphSharesNominalTypesAndEmitsEachDefinitionOnce) {
    file(R"(def pub struct Item { def pub value: i32, }
        def pub create(value: i32) -> Item { return Item { value: value }; })",
         "common.gloin");
    file("import \"./common\"; def pub make() -> common.Item { return common.create(21); }",
         "left.gloin");
    file("import \"./common.gloin\"; def pub read(x: common.Item) -> i32 { return x.value; }",
         "right.gloin");
    auto main = source(R"(import "./left"; import "./right"; import "./common";
        def main() -> i32 { def x: common.Item = left.make(); return right.read(x) * 2; })");
    expect_run(invoke({main}), 42);
    auto ir = invoke({"--emit-llvm", main});
    ASSERT_EQ(ir.status, 0) << ir.err;
    // One emitted create definition despite three import edges to common.
    size_t definitions = 0, at = 0;
    while ((at = ir.out.find("llvm.func @gloin.module.local.", at)) != std::string::npos) {
        auto end = ir.out.find('\n', at);
        if (ir.out.substr(at, end - at).find(".common.create(") != std::string::npos)
            ++definitions;
        ++at;
    }
    EXPECT_EQ(definitions, 1u);
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}},
                                                {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

TEST_F(ModuleTest, IdenticalBasenamesKeepSeparateTypesAndLinkage) {
    for (const std::string dir : {"left", "right"}) {
        file(std::string(
                 "def pub struct Item { def pub n: i32, } def pub answer() -> i32 { return ") +
                 (dir == "left" ? "20" : "22") + "; }",
             dir + "/common.gloin");
        file("import \"./" + dir + "/common\"; def pub answer() -> i32 { return common.answer(); }",
             dir + ".gloin");
    }
    auto main = source("import \"./left\"; import \"./right\"; def main() -> i32 { return "
                       "left.answer() + right.answer(); }");
    expect_run(invoke({main}), 42);
    file("import \"./left/common\"; def pub make() -> common.Item { return common.Item { n: 1 }; }",
         "left.gloin");
    file("import \"./right/common\"; def pub read(value: common.Item) -> i32 { return value.n; }",
         "right.gloin");
    rejects(source("import \"./left\"; import \"./right\"; def main() -> i32 { return "
                   "right.read(left.make()); }"),
            "type mismatch");
}

TEST_F(ModuleTest, PublicConstantsComposeAcrossModulesAndAtRuntime) {
    file("def pub const BASE: u64 = 18446744073709551614; def pub const TEXT: string = \"from "
         "module\"; def const SECRET: i32 = 9;",
         "base.gloin");
    file("import \"./base\"; def pub const LIMIT: u64 = base.BASE + 1;", "limits.gloin");
    auto main = source(R"(import "./limits"; import "./base"; import "@std";
        def const EXPECTED: u64 = limits.LIMIT;
        def main() -> i32 {
            std.println(base.TEXT);
            if EXPECTED == 18446744073709551615 && limits.LIMIT == EXPECTED { return 42; }
            return 1;
        })");
    expect_run(invoke({main}), 42, "from module\n");
    rejects(
        source(
            "import \"./base\"; def const X: i32 = base.SECRET; def main() -> i32 { return X; }"),
        "Unknown or private member");
    rejects(
        source(
            "import \"./base\"; def main() -> i32 { def p: &const u64 = &base.BASE; return 0; }"),
        "not a constant");
    rejects(source("import \"./base\"; def main() -> i32 { base.BASE = 0; return 0; }"),
            "not a constant");
}

TEST_F(ModuleTest, PrivateFunctionsStructsFieldsAndMethodsStayInTheirFile) {
    file(R"(def hidden() -> i32 { return 41; }
        def struct Private { def n: i32, }
        def pub struct Public {
            def mut n: i32,
            def secret(self: &Public) -> i32 { return self.n; }
            def pub static create() -> Public { return Public { n: hidden() }; }
            def pub read(self: &Public) -> i32 { return self.secret() + 1; }
        })",
         "vault.gloin");
    expect_run(invoke({source(R"(import "./vault"; def main() -> i32 {
        def mut value: vault.Public = vault.Public.create(); return value.read(); })")}),
               42);
    for (const std::string body :
         {"return vault.hidden();", "def x: vault.Private; return 0;",
          "def mut x: vault.Public = vault.Public.create(); return x.n;",
          "def mut x: vault.Public = vault.Public.create(); return x.secret();",
          "def x: vault.Public = vault.Public { n: 1 }; return 0;"})
        rejects(source("import \"./vault\"; def main() -> i32 { " + body + " }"), "error:");
}

TEST_F(ModuleTest, DependenciesHaveTheirOwnImportsAndCannotSeeCallerNames) {
    file("def pub answer() -> i32 { return 42; }", "leaf.gloin");
    file("import \"./leaf\"; def pub answer() -> i32 { return leaf.answer(); }", "middle.gloin");
    expect_run(
        invoke({source("import \"./middle\"; def main() -> i32 { return middle.answer(); }")}), 42);
    for (const std::string expression : {"leaf.answer()", "middle.leaf.answer()", "answer()"})
        rejects(source("import \"./middle\"; def main() -> i32 { return " + expression + "; }"),
                "error:");
    file("def pub answer() -> i32 { return caller(); }", "middle.gloin");
    rejects(source("import \"./middle\"; def caller() -> i32 { return 42; } def main() -> i32 { "
                   "return middle.answer(); }"),
            "Undefined variable");
}

TEST_F(ModuleTest, ImportedMainIsNotTheRootEntryAndUnusedModulesAreChecked) {
    file("def pub main(n: i32) -> i32 { return n; }", "worker.gloin");
    auto main = source("import \"./worker\";");
    expect_success(invoke({"--check", main}), "");
    expect_error(invoke({main}), 1, "Executable requires");
    file("def private_bad() -> i32 { return true; }", "worker.gloin");
    rejects(source("import \"./worker\"; def main() -> i32 { return 0; }"), "Return type mismatch");
}

TEST_F(ModuleTest, GlobalsAndExecutableInitializationAreRejectedInsideDependencies) {
    for (const std::string text :
         {"def value: i32 = 1;", "def mut value: i32 = 1;", "def pub value: i32 = 1;",
          "def pub const VALUE: i32 = later(); def later() -> i32 { return 1; }"}) {
        file(text, "globals.gloin");
        rejects(source("import \"./globals\"; def main() -> i32 { return 0; }"), "error:");
    }
}

TEST_F(ModuleTest, ConstantForwardReferencesAndOverflowAreStillRejected) {
    for (const std::string text : {"def pub const X: i32 = Y; def const Y: i32 = 1;",
                                   "def pub const X: i32 = 2147483647 + 1;"}) {
        file(text, "constants.gloin");
        rejects(source("import \"./constants\"; def main() -> i32 { return 0; }"), "error:");
    }
    file("def pub const X: i32 = 2147483647;", "constants.gloin");
    rejects(source("import \"./constants\"; def const Y: i32 = constants.X + 1; def main() -> i32 "
                   "{ return Y; }"),
            "overflow");
}

TEST_F(ModuleTest, DuplicateFilesNamespacesAndFileDeclarationsAreRejected) {
    file("", "common.gloin");
    file("", "elsewhere/common.gloin");
    for (const std::string text : {"import \"./common\"; import \"./common.gloin\";",
                                   "import \"./common\"; import \"./elsewhere/common\";",
                                   "import \"./common\"; def common() -> void {}",
                                   "import \"./common\"; def const common: i32 = 0;",
                                   "import \"./common\"; def struct common {}"})
        rejects(source(text + " def main() -> i32 { return 0; }"), "error:");
    file("import \"./common\"; def common() -> void {}", "worker.gloin");
    rejects(source("import \"./worker\"; def main() -> i32 { return 0; }"),
            "conflicts with imported module");
}

TEST_F(ModuleTest, LocalShadowingAndImportOrderFollowFileScopeRules) {
    file("def pub answer() -> i32 { return 42; }", "helper.gloin");
    auto main = source(R"(def main() -> i32 {
        { def helper: i32 = 7; if helper != 7 { return 1; } }
        return helper.answer();
    } import "./helper";)");
    expect_run(invoke({main}), 42);
    rejects(source("import \"./helper\"; def main() -> i32 { def helper: i32 = 7; return "
                   "helper.answer(); }"),
            "error:");
}

TEST_F(ModuleTest, SymlinkAliasesShareIdentityAcrossImportersAndRejectDuplicates) {
    file("def pub struct Item { def pub n: i32, }", "real/common.gloin");
    fs::create_symlink(fs::path(directory) / "real/common.gloin",
                       fs::path(directory) / "alias.gloin");
    file(
        "import \"./real/common\"; def pub make() -> common.Item { return common.Item { n: 42 }; }",
        "left.gloin");
    file("import \"./alias\"; def pub read(x: alias.Item) -> i32 { return x.n; }", "right.gloin");
    expect_run(invoke({source("import \"./left\"; import \"./right\"; def main() -> i32 { return "
                              "right.read(left.make()); }")}),
               42);
    rejects(source("import \"./real/common\"; import \"./alias\"; def main() -> i32 { return 0; }"),
            "Duplicate import");
}

TEST_F(ModuleTest, SymlinkedModulesResolveChildrenFromCanonicalDirectory) {
    file("def pub answer() -> i32 { return 42; }", "real/child.gloin");
    file("import \"./child\"; def pub answer() -> i32 { return child.answer(); }",
         "real/parent.gloin");
    fs::create_symlink(fs::path(directory) / "real/parent.gloin",
                       fs::path(directory) / "alias.gloin");
    expect_run(invoke({source("import \"./alias\"; def main() -> i32 { return alias.answer(); }")}),
               42);
}

TEST_F(ModuleTest, CyclesIncludeRootSelfAndIndirectCanonicalChains) {
    auto main = source("import \"./program\"; def main() -> i32 { return 0; }");
    rejects(main, "Import cycle:");
    file("import \"./b\";", "a.gloin");
    file("import \"./a\";", "b.gloin");
    main = source("import \"./a\"; def main() -> i32 { return 0; }");
    rejects(main, "Import cycle:");
    auto result = invoke({"--check", main});
    EXPECT_NE(result.err.find("a.gloin ->"), std::string::npos);
    EXPECT_NE(result.err.find("b.gloin ->"), std::string::npos);
    file("import \"./program\";", "a.gloin");
    rejects(main, "Import cycle:");
    fs::create_symlink(main, fs::path(directory) / "root_alias.gloin");
    file("import \"./root_alias\";", "a.gloin");
    rejects(main, "Import cycle:");
}

TEST_F(ModuleTest, ExcessiveImportDepthProducesDiagnostic) {
    for (int i = 0; i < 130; ++i)
        file("import \"./unit" + std::to_string(i + 1) + "\";",
             "unit" + std::to_string(i) + ".gloin");
    rejects(source("import \"./unit0\"; def main() -> i32 { return 0; }"), "depth exceeds 128");
}

TEST_F(ModuleTest, MissingFilesMalformedPathsAndInvalidNamespacesAreDiagnosed) {
    for (const std::string path :
         {"./missing", "../missing", "./missing.txt", "missing.gloin", "/absolute/module.gloin",
          "./bad-name", "./if", "./i32", "./bad\\0name"})
        rejects(source("import \"" + path + "\"; def main() -> i32 { return 0; }"), "error:");
    fs::create_directory(fs::path(directory) / "folder.gloin");
    rejects(source("import \"./folder.gloin\"; def main() -> i32 { return 0; }"),
            "not a regular file");
}

TEST_F(ModuleTest, DependencyDiagnosticsPreserveSourceLocationsAndReloadEachCompilation) {
    auto bad = file("def pub answer() -> i32 { return true; }", "dependency.gloin");
    auto main =
        source("import \"./dependency\"; def main() -> i32 { return dependency.answer(); }");
    rejects(main, bad + ":1:");
    file("def pub answer() -> i32 { return 42; }", "dependency.gloin");
    expect_run(invoke({main}), 42);
    file("def pub answer() -> i32 { return 17; }", "dependency.gloin");
    expect_run(invoke({main}), 17);
}

TEST_F(ModuleTest, StandardModulesCanImportDependenciesAndExportConstants) {
    file("def pub const OFFSET: i32 = 2;", "helper.gloin");
    file("import \"./helper\"; def pub answer() -> i32 { return helper.OFFSET + 40; }",
         "math.gloin");
    file("import \"@math\"; def pub answer() -> i32 { return math.answer(); }", "std.gloin");
    auto main = source("import \"@std\"; def main() -> i32 { return std.answer(); }");
    expect_run(invoke({"--stdlib-dir", directory, main}), 42);
}

TEST_F(ModuleTest, LocalArenaBasenameDoesNotGrantNativePrimitivesOrTypedBridge) {
    file(R"(def pub struct GeneralArena {
        def pub static create() -> GeneralArena { return GeneralArena {}; }
        def pub alloc(self: &GeneralArena, value: i32) -> i32 { return value; }
    })",
         "arena.gloin");
    expect_run(invoke({source(R"(import "./arena"; def main() -> i32 {
        def mut a: arena.GeneralArena = arena.GeneralArena.create(); return a.alloc(42); })")}),
               42);
    for (const std::string body : {"__write_stdout(\"bad\");", "__arena_general_create();"}) {
        file("def pub bad() -> void { " + body + " }", "arena.gloin");
        rejects(source("import \"./arena\"; def main() -> i32 { return 0; }"),
                "Undefined variable");
    }
}

TEST_F(ModuleTest, InMemoryCompilerUsesFilenameAsImportBase) {
    file("def pub answer() -> i32 { return 42; }", "api/helper.gloin");
    mlir::MLIRContext context;
    auto compiled =
        compile_source("import \"./helper\"; def main() -> i32 { return helper.answer(); }",
                       directory + "/api/virtual.gloin", context, CompilationMode::Executable);
    ASSERT_TRUE(compiled.success());
    auto result = gloin_test::run_external_module(*compiled.module, {gloin_test::mlir_opt, {}},
                                                  {gloin_test::mlir_runner, {}});
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}

TEST_F(ModuleTest, QualifiedExportsCannotBecomeFunctionValuesOrWritableStorage) {
    file("def pub answer() -> i32 { return 42; } def pub const VALUE: i32 = 42;", "helper.gloin");
    for (const std::string body :
         {"def x: i32 = helper.answer;", "def p: &const i32 = &helper.answer;",
          "helper.answer = 0;", "helper.VALUE();",
          "def const X: i32 = helper.answer; def y: i32 = X;"})
        rejects(source("import \"./helper\"; def main() -> i32 { " + body + " return 0; }"),
                "error:");
}

TEST_F(ModuleTest, QualifiedDeferredCallsCaptureValuesAcrossModules) {
    file(R"(import "@std"; def pub show(value: string) -> void { std.print(value); })",
         "output.gloin");
    expect_run(invoke({source(R"(import "./output"; def main() -> i32 {
        def mut text: string = "A"; defer output.show(text);
        text = "B"; defer output.show(text); return 42;
    })")}),
               42, "BA");
}

TEST_F(ModuleTest, StandardAndLocalEdgesShareIdentityWithoutAmbiguousStandardAliases) {
    file(R"(def pub struct Item { def pub n: i32, }
        def pub make() -> Item { __write_stdout("ok"); return Item { n: 42 }; })",
         "utility.gloin");
    fs::create_symlink(fs::path(directory) / "utility.gloin", fs::path(directory) / "alias.gloin");
    file(R"(import "./alias"; def pub read(x: alias.Item) -> i32 { return x.n; })", "local.gloin");
    auto main = source(R"(import "./local"; import "@utility";
        def main() -> i32 { return local.read(utility.make()); })");
    expect_run(invoke({"--stdlib-dir", directory, main}), 42, "ok");
    main = source(R"(import "@utility"; import "./local";
        def main() -> i32 { return local.read(utility.make()); })");
    expect_run(invoke({"--stdlib-dir", directory, main}), 42, "ok");
    main = source(R"(import "./local"; import "@alias"; import "@utility";
        def main() -> i32 { return 0; })");
    expect_error(invoke({"--stdlib-dir", directory, main}), 1, "Duplicate import");
    file(R"(import "@alias";)", "local.gloin");
    main = source(R"(import "./local"; import "@utility"; def main() -> i32 { return 0; })");
    expect_error(invoke({"--stdlib-dir", directory, main}), 1, "Distinct standard module names");
}

TEST_F(ModuleTest, ExampleGraphWorksAcrossArenaMethodsConstantsAndReset) {
    auto example = std::string(gloin_test::module_example);
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example =
            (fs::path(binary).parent_path() / "../share/gloinc/examples/module_lab.gloin").string();
    expect_run(invoke({example}), 0, "module lab: ok\n");
}
