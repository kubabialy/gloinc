#include "compiler.h"
#include "context_runtime.h"
#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#include <fstream>
#include <tuple>
#include <sys/resource.h>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
namespace {
std::string literal(const std::string &value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\0')
            out += "\\0";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\"')
            out += "\\\"";
        else if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out + '"';
}
struct CwdGuard {
    std::filesystem::path original = std::filesystem::current_path();
    explicit CwdGuard(const std::string &path) { std::filesystem::current_path(path); }
    ~CwdGuard() { std::filesystem::current_path(original); }
};
struct EnvGuard {
    std::string name, old;
    bool existed;
    explicit EnvGuard(std::string key)
        : name(std::move(key)), existed(std::getenv(name.c_str()) != nullptr) {
        if (existed)
            old = std::getenv(name.c_str());
    }
    ~EnvGuard() {
        if (existed)
            setenv(name.c_str(), old.c_str(), 1);
        else
            unsetenv(name.c_str());
    }
};
class ContextLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source("import \"@fs\"; import \"@process\"; import \"@strings\"; import "
                      "\"@status\"; import \"@arena\"; import \"@io\"; import \"@std\"; "
                      "def same(r: fs.PathResult, s: string) -> bool { return r.status == "
                      "status.OK && strings.equal(r.value,s); } "
                      "def main() -> i32 { def mut memory: arena.GeneralArena = "
                      "arena.GeneralArena.create(); defer memory.free(); " +
                      body + " }");
    }
    std::filesystem::path library() {
        const auto *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
    void copy_modules() {
        for (auto name : {"arena.gloin", "std.gloin", "strings.gloin", "status.gloin", "io.gloin",
                          "fs.gloin", "process.gloin", "time.gloin"})
            source(read((library() / name).string()), name);
    }
    static void replace(std::string &s, const std::string &from, const std::string &to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};
} // namespace
TEST_F(ContextLibraryTest, LexicalBasenameAndDirnameCoverRootsDotsSeparatorsAndBytes) {
    std::string body;
    for (const auto &[path, base, dir] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"", ".", "."},
             {"/", "/", "/"},
             {"///", "/", "/"},
             {"a", "a", "."},
             {"a/", "a", "."},
             {"/a", "a", "/"},
             {"//a", "a", "/"},
             {"a//b///", "b", "a"},
             {"a//b/c", "c", "a//b"},
             {"a/../b", "b", "a/.."},
             {"..", "..", "."},
             {"/a/..", "..", "/a"},
             {"C:\\work\\a", "C:\\work\\a", "."},
             {"/é/λ", "λ", "/é"}}) {
        body += "if !same(fs.basename(" + literal(path) + ")," + literal(base) +
                ") || !same(fs.dirname(" + literal(path) + ")," + literal(dir) + ") { return 1; }";
    }
    body +=
        R"(if fs.basename("a\0b").status != status.INVALID || fs.dirname("a\0b").status != status.INVALID { return 2; } return 0;)";
    expect_run(invoke({program(body)}), 0);
}
TEST_F(ContextLibraryTest, JoinBoundsAbsoluteChildrenAndRetainedResults) {
    std::string body;
    for (const auto &[base, child, result] :
         std::vector<std::tuple<std::string, std::string, std::string>>{{"", "", ""},
                                                                        {"", "x", "x"},
                                                                        {"base///", "", "base///"},
                                                                        {"/", "x", "/x"},
                                                                        {"///", "x", "/x"},
                                                                        {"/a///", "b", "/a/b"},
                                                                        {"a", "/x", "/x"},
                                                                        {"a/../b", "c", "a/../b/c"},
                                                                        {"a//b", "c", "a//b/c"},
                                                                        {"é", "λ", "é/λ"}}) {
        auto args = "&memory," + literal(base) + "," + literal(child) + ",";
        body += "if !same(fs.join(" + args + std::to_string(result.size()) + ")," +
                literal(result) + ") { return 1; }";
        if (!result.empty())
            body += "if fs.join(" + args + std::to_string(result.size() - 1) +
                    ").status != status.TOO_LONG { return 2; }";
    }
    body += R"(def retained: fs.PathResult = fs.join(&memory,"a","b",3);
        def later: fs.PathResult = fs.join(&memory,"c","d",3); if !same(retained,"a/b") { return 3; }
        memory.free();
        if fs.join(&memory,"bad\0","/x",10).status != status.INVALID || fs.join(&memory,"a","b",2).status != status.TOO_LONG { return 4; }
        if !same(fs.join(&memory,"","",0),"") { return 5; } return 0;)";
    expect_run(invoke({program(body)}), 0);
}
TEST_F(ContextLibraryTest, BothJoinAllocationsFailRecoverably) {
    for (unsigned failure : {1u, 2u}) {
        copy_modules();
        auto arena = read((library() / "arena.gloin").string());
        replace(arena, "def mut state: *u8,", "def mut state: *u8, def mut requests: u64,");
        replace(arena, "GeneralArena {\n            state: state\n        }",
                "GeneralArena {\n            state: state,\n            requests: 0\n        }");
        replace(
            arena, "def bytes: *u8 = __arena_general_alloc(self.state, size, 1);",
            "self.requests = self.requests + 1; if self.requests == " + std::to_string(failure) +
                " { return null; } def bytes: *u8 = __arena_general_alloc(self.state, size, 1);");
        source(arena, "arena.gloin");
        expect_run(
            invoke(
                {"--stdlib-dir", directory,
                 program(
                     R"(def r: fs.PathResult = fs.join(&memory,"base","child",10); if r.status != status.NO_MEMORY || !strings.is_empty(r.value) { return 1; } return 0;)")}),
            0);
    }
}
TEST_F(ContextLibraryTest, FilesystemQueriesAndMutationsPreserveDefinedEffects) {
    auto src = source("source", "a"), dst = source("old", "b"), child = directory + "/sub";
    expect_run(invoke({program("def a: string = " + literal(src) + "; def b: string = " +
                               literal(dst) + "; def dir: string = " + literal(child) + R"(;
        def m: fs.MetadataResult = fs.metadata(a); if m.status != status.OK || m.os_error != 0 || m.kind != fs.FILE || m.size != 6 { return 1; }
        if fs.mkdir(dir).status != status.OK || fs.mkdir(dir).status != status.ALREADY_EXISTS { return 2; }
        if fs.metadata(dir).kind != fs.DIRECTORY || fs.metadata(dir).size != 0 { return 3; }
        if fs.remove_file(dir).status == status.OK { return 4; }
        if fs.rename_replace(a,b).status != status.OK || fs.metadata(a).status != status.NOT_FOUND || fs.metadata(b).size != 6 { return 5; }
        if fs.remove_file(b).status != status.OK || fs.remove_file(b).status != status.NOT_FOUND { return 6; }
        def bad: fs.MetadataResult = fs.metadata("a\0b");
        if bad.status != status.INVALID || bad.os_error != 0 || bad.kind != 0 || bad.size != 0 { return 7; }
        return 0;)")}),
               0);
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_FALSE(std::filesystem::exists(dst));
    EXPECT_TRUE(std::filesystem::is_directory(child));
}
TEST_F(ContextLibraryTest, DirectoryIterationCopiesNamesAndCloseInvalidatesAliases) {
    const auto entries = directory + "/entries";
    ASSERT_TRUE(std::filesystem::create_directory(entries));
    { std::ofstream(entries + "/dir-alpha") << "a"; }
    { std::ofstream(entries + "/dir-beta") << "b"; }
    const auto body = "def opened: fs.DirectoryResult = fs.Directory.open(&memory," +
                      literal(entries) + R"();
        if opened.status != status.OK || opened.os_error != 0 { return 1; }
        def mut cursor: fs.Directory = opened.value;
        def mut alias: fs.Directory = cursor;
        def one: fs.DirectoryEntryResult = cursor.next(&memory);
        def two: fs.DirectoryEntryResult = alias.next(&memory);
        if one.status != status.OK || two.status != status.OK || strings.equal(one.name,two.name) { return 2; }
        def alpha: bool = strings.equal(one.name,"dir-alpha") || strings.equal(two.name,"dir-alpha");
        def beta: bool = strings.equal(one.name,"dir-beta") || strings.equal(two.name,"dir-beta");
        if !alpha || !beta || cursor.next(&memory).status != status.END { return 3; }
        if alias.close().status != status.OK || cursor.is_open() { return 4; }
        if cursor.next(&memory).status != status.CLOSED || cursor.close().status != status.CLOSED { return 5; }
        if fs.Directory.open(&memory,"").status != status.INVALID ||
           fs.Directory.open(&memory,"a\0b").status != status.INVALID { return 6; }
        return 0;)";
    expect_run(invoke({program(body)}), 0);
}
TEST_F(ContextLibraryTest, CliForwardsExactBytesOnlyAfterFileDelimiter) {
    const auto file = program(R"(
        if process.arg_count() != 5 { return 1; }
        if !strings.equal(process.arg(&memory,1).value,"--check") || !strings.equal(process.arg(&memory,2).value,"") { return 2; }
        if !strings.equal(process.arg(&memory,3).value,"é path") || !strings.equal(process.arg(&memory,4).value,"--") { return 3; }
        def bad: process.TextResult = process.arg(&memory,5);
        if bad.status != status.OUT_OF_RANGE || bad.os_error != 0 || !strings.is_empty(bad.value) { return 4; }
        std.println(process.arg(&memory,0).value); return 0;)");
    expect_run(invoke({"--run", file, "--", "--check", "", "é path", "--"}), 0, file + "\n");
    expect_run(invoke({"--", file, "--", "--check", "", "é path", "--"}), 0, file + "\n");
    for (const std::string mode : {"--check", "--emit-ir", "--emit-llvm"})
        expect_error(invoke({mode, file, "--", "arg"}), 2, "program arguments require run mode");
    expect_error(invoke({file, "bare"}), 2, "expected exactly one input file");
}
TEST_F(ContextLibraryTest, DashFilenameEscapeAndEmptyForwardingRemainExplicit) {
    auto file = source("import \"@process\"; import \"@arena\"; import \"@std\"; def main() -> i32 "
                       "{ def mut m: arena.GeneralArena = arena.GeneralArena.create(); defer "
                       "m.free(); std.println(process.arg(&m,0).value); return 0; }",
                       "-program.gloin");
    CwdGuard cwd(directory);
    expect_run(invoke({"--", "-program.gloin"}), 0, "-program.gloin\n");
    expect_run(invoke({"--", "-program.gloin", "--"}), 0, "-program.gloin\n");
    expect_error(invoke({"-program.gloin"}), 2, "unknown or misplaced option");
}
TEST_F(ContextLibraryTest, EnvironmentMissingEmptyOwnedValuesAndMalformedNames) {
    EnvGuard env("GLOIN_SPEC030E_LIBRARY");
    auto file =
        program(R"(def text: process.TextResult = process.env(&memory,"GLOIN_SPEC030E_LIBRARY");
        def expected: process.TextResult = process.arg(&memory,1);
        if strings.equal(expected.value,"missing") { if text.status != status.NOT_FOUND || !strings.is_empty(text.value) { return 1; } }
        else { if text.status != status.OK || text.os_error != 0 || !strings.equal(text.value,expected.value) { return 2; } }
        if process.env(&memory,"").status != status.INVALID || process.env(&memory,"a=b").status != status.INVALID || process.env(&memory,"a\0b").status != status.INVALID { return 3; }
        return 0;)");
    unsetenv(env.name.c_str());
    expect_run(invoke({file, "--", "missing"}), 0);
    setenv(env.name.c_str(), "", 1);
    expect_run(invoke({file, "--", ""}), 0);
    setenv(env.name.c_str(), "value with spaces", 1);
    expect_run(invoke({file, "--", "value with spaces"}), 0);
}
TEST_F(ContextLibraryTest, ArgumentEnvironmentAndCwdAllocationFailuresAreRecoverable) {
    EnvGuard env("GLOIN_SPEC030E_ALLOC");
    setenv(env.name.c_str(), "value", 1);
    copy_modules();
    auto arena = read((library() / "arena.gloin").string());
    replace(arena, "def bytes: *u8 = __arena_general_alloc(self.state, size, 1);",
            "def bytes: *u8 = null;");
    source(arena, "arena.gloin");
    expect_run(invoke({"--stdlib-dir", directory, program(R"(
        if process.arg(&memory,1).status != status.NO_MEMORY || process.env(&memory,"GLOIN_SPEC030E_ALLOC").status != status.NO_MEMORY || process.cwd(&memory,1024).status != status.NO_MEMORY { return 1; }
        memory.free();
        if process.arg(&memory,2).status != status.OK || !strings.is_empty(process.arg(&memory,2).value) { return 2; }
        if process.arg(&memory,18446744073709551615).status != status.OUT_OF_RANGE || process.cwd(&memory,18446744073709551615).status != status.NO_MEMORY { return 3; }
        return 0;)"),
                       "--", "value", ""}),
               0);
}
TEST_F(ContextLibraryTest, CwdUsesHostWorkingDirectoryAndReportsTooSmallBounds) {
    auto file = program(R"(def outcome: process.TextResult = process.cwd(&memory,4096);
        if outcome.status != status.OK || outcome.os_error != 0 { return 1; } std.println(outcome.value);
        def small: process.TextResult = process.cwd(&memory,0);
        if small.status != status.TOO_LONG || small.os_error == 0 || !strings.is_empty(small.value) { return 2; } return 0;)");
    CwdGuard cwd(directory);
    expect_run(invoke({file}), 0, std::filesystem::current_path().string() + "\n");
}
TEST_F(ContextLibraryTest, EmbeddingArgumentsAreInvocationOwnedAndNeverLeak) {
    mlir::MLIRContext context;
    auto compiled = compile_source(R"(import "@process"; import "@strings"; import "@arena";
        def main() -> i32 { if process.arg_count() == 0 { return 0; }
        def mut memory: arena.GeneralArena = arena.GeneralArena.create(); defer memory.free();
        def arg: process.TextResult = process.arg(&memory,0); if strings.equal(arg.value,"first") { return 11; }
        if strings.equal(arg.value,"second") { return 22; } return 99; })",
                                   "embed.gloin", context, CompilationMode::Executable,
                                   CompilationOutput::HighLevel, library().string());
    ASSERT_TRUE(compiled.success());
    const char *outer_args[] = {"outer"};
    auto *outer = gloin_process_arguments_push(1, outer_args);
    ASSERT_NE(outer, nullptr);
    for (const auto &[args, expected] : std::vector<std::pair<std::vector<std::string>, int>>{
             {{"first"}, 11}, {{"second", "extra"}, 22}, {{}, 0}}) {
        auto result = JitRunner::run(*compiled.module, args);
        EXPECT_TRUE(result.success());
        EXPECT_EQ(result.value, expected);
        EXPECT_EQ(gloin_process_arg_count(), 1u);
    }
    auto bad = JitRunner::run(*compiled.module, {std::string("x\0y", 3)});
    EXPECT_FALSE(bad.success());
    EXPECT_EQ(gloin_process_arg_count(), 1u);
    uint64_t n;
    int32_t status;
    EXPECT_STREQ(gloin_process_arg(0, &n, &status), "outer");
    EXPECT_EQ(gloin_process_arguments_pop(outer), 0);
    EXPECT_EQ(gloin_process_arg_count(), 0u);
}
TEST_F(ContextLibraryTest, PublicSignaturesAndCanonicalPrimitivePrivacyAreChecked) {
    for (const std::string body :
         {"fs.basename(1);", "fs.join(&memory,\"a\",\"b\",-1);", "fs.metadata(true);",
          "fs.symlink(1,\"x\");", "fs.read_link(&memory,\"x\",-1);",
          "fs.symlink(\"x\",\"y\");", "fs.read_link(&memory,\"x\",10);",
          "__fs_symlink();", "__fs_read_link();",
          "__fs_canonical_path();", "__fs_temp_dir();", "__fs_remove_dir();", "__fs_replace_file();",
          "fs.replace_file(1,\"old\",\"new\");", "fs.replace_file(\"path\",\"old\",\"new\");",
          "fs.canonical_path(&memory,\"x\",-1);", "fs.temp_dir(&memory,\"x\",1,10);", "fs.remove_dir(1);",
          "fs.canonical_path(&memory,\"x\",10);", "fs.temp_dir(&memory,\"x\",\"p\",10);", "fs.remove_dir(\"x\");", "fs.rename_replace(\"a\");", "process.arg(&memory,-1);", "process.env(&memory,1);",
          "process.cwd(&memory,true);", "__process_arg_count();", "__fs_mkdir(\"x\");",
          "process.start(&memory,\"/bin/echo\",false);", "__process_start();", "__process_pipe_wait();", "__process_start_options();", "__process_group_signal();",
          "process.start_piped(&memory,\"/bin/cat\",false);",
          "def mut options: process.Options = process.Options.defaults(); options.stack_limit_bytes = -1;",
          "def args: [string;0] = {}; process.start(&memory,\"/bin/echo\",args[..]);"}) {
        SCOPED_TRACE(body);
        auto file = program(body + "return 0;");
        for (std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"}) {
            SCOPED_TRACE(mode);
            expect_error(invoke({mode, file}), 1, "error:");
        }
    }
    const auto file =
        source("import \"@process\"; def main() -> i32 { process.probe(); return 0; }");
    for (std::string body : {"__process_arg_count(1);",
                             "def mut n: u64 = 0; def mut s: i32 = 0; __process_arg(true,&n,&s);",
                             "__fs_mkdir(\"x\");"}) {
        SCOPED_TRACE(body);
        source("def pub probe() -> void {" + body + "}", "process.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "error:");
    }
}
TEST_F(ContextLibraryTest, SymlinksRunThroughGloinIRInJitAndNativeModes) {
    source("abc", "target");
    const auto fixture = (std::filesystem::path(__FILE__).parent_path() /
                          "fixtures/filesystem/symlinks.gloin").string();
    expect_success(invoke({fixture, "--", directory}), "");
    const auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *symbol : {"gloin_fs_symlink", "gloin_fs_read_link"}) {
        const auto position = ir.out.find(std::string("callee = @") + symbol);
        ASSERT_NE(position, std::string::npos) << symbol;
        const auto start = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
        EXPECT_EQ(ir.out.find(std::string("llvm.call @") + symbol), std::string::npos);
    }
    EXPECT_NE(ir.out.find("!gloin.result<"), std::string::npos);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/symlinks " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/symlinks.out", err = directory + "/symlinks.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable, directory}, std::nullopt, redirects, 10), 0);
        EXPECT_EQ(read(out), "");
        EXPECT_EQ(read(err), "");
    }
}
TEST_F(ContextLibraryTest, ReadLinkAllocationFailureAndValidationAreRecoverable) {
    copy_modules();
    auto arena = read((library() / "arena.gloin").string());
    const std::string allocation = "def bytes: *u8 = __arena_general_alloc(self.state, size, 1);";
    ASSERT_NE(arena.find(allocation), std::string::npos);
    replace(arena, allocation, "def bytes: *u8 = null;");
    source(arena, "arena.gloin");
    const auto file = program(R"(
        def failed: result<string> = fs.read_link(&memory,"missing",10);
        if !failed.erroneous { return 1; }
        if !strings.equal(failed.error.message,"symlink target allocation failed") { return 2; }
        def invalid: result<string> = fs.read_link(&memory,"",10);
        if !invalid.erroneous { return 3; }
        if !strings.equal(invalid.error.message,"symlink path must be nonempty and contain no NUL") { return 4; }
        def huge: result<string> = fs.read_link(&memory,"missing",18446744073709551615);
        if !huge.erroneous { return 5; }
        if !strings.equal(huge.error.message,"symlink target limit is too large") { return 6; }
        return 0;)");
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}
TEST_F(ContextLibraryTest, FilesystemWorkspaceRunsThroughGloinIRInJitAndNativeModes) {
    const auto fixture = (std::filesystem::path(__FILE__).parent_path() /
                          "fixtures/filesystem/workspace.gloin").string();
    const auto canonical = std::filesystem::canonical(directory).string();
    expect_success(invoke({fixture, "--", directory, canonical}), "");
    const auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *symbol : {"gloin_fs_canonical_path", "gloin_fs_temp_dir", "gloin_fs_remove_dir", "gloin_fs_replace_file"}) {
        const auto position = ir.out.find(std::string("callee = @") + symbol);
        ASSERT_NE(position, std::string::npos) << symbol;
        const auto start = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
        EXPECT_EQ(ir.out.find(std::string("llvm.call @") + symbol), std::string::npos);
    }
    EXPECT_NE(ir.out.find("!gloin.result<"), std::string::npos);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/workspace " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/workspace.out", err = directory + "/workspace.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable, directory, canonical},
                                          std::nullopt, redirects, 10), 0);
        EXPECT_EQ(read(out), "");
        EXPECT_EQ(read(err), "");
    }
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        EXPECT_FALSE(entry.path().filename().string().starts_with("case XXX-"));
}
TEST_F(ContextLibraryTest, EveryTemporaryDirectoryArenaFailurePrecedesCreation) {
    for (unsigned failure = 1; failure <= 5; ++failure) {
        copy_modules();
        auto arena = read((library() / "arena.gloin").string());
        const std::string allocation = "def bytes: *u8 = __arena_general_alloc(self.state, size, 1);";
        ASSERT_NE(arena.find(allocation), std::string::npos);
        ASSERT_NE(arena.find("GeneralArena {\n            state: state\n        }"), std::string::npos);
        replace(arena, "def mut state: *u8,", "def mut state: *u8, def mut requests: u64,");
        replace(arena, "GeneralArena {\n            state: state\n        }",
                "GeneralArena {\n            state: state,\n            requests: 0\n        }");
        replace(arena, allocation, "self.requests = self.requests + 1; if self.requests == " +
                                   std::to_string(failure) + " { return null; } " + allocation);
        source(arena, "arena.gloin");
        const auto file = program("def made: result<string> = fs.temp_dir(&memory," +
                                  literal(directory) + R"(,"failed-allocation",65536);
            if !made.erroneous { return 1; }
            if !strings.contains(made.error.message,"allocation failed") { return 2; }
            return 0;)");
        expect_run(invoke({"--stdlib-dir", directory, file}), 0);
        for (const auto &entry : std::filesystem::directory_iterator(directory))
            EXPECT_FALSE(entry.path().filename().string().starts_with("failed-allocation-"));
    }
}
TEST_F(ContextLibraryTest, ChildLifecycleRunsThroughGloinIRInJitAndNativeModes) {
    const auto fixture = (std::filesystem::path(__FILE__).parent_path() /
                          "fixtures/process/lifecycle.gloin").string();
    const std::string expected = " a b $(literal);* café\n";
    expect_success(invoke({fixture}), expected);
    auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *symbol : {"gloin_process_start", "gloin_process_wait", "gloin_process_signal"}) {
        const auto position = ir.out.find(std::string("callee = @") + symbol);
        ASSERT_NE(position, std::string::npos) << ir.out;
        const auto start = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
        EXPECT_EQ(ir.out.find(std::string("llvm.call @") + symbol), std::string::npos);
    }
    EXPECT_NE(ir.out.find("!gloin.result<"), std::string::npos);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/child lifecycle " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/child.out";
        const auto err = directory + "/child.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10), 0);
        EXPECT_EQ(read(out), expected);
        EXPECT_EQ(read(err), "");
    }
}
TEST_F(ContextLibraryTest, ChildPipesCaptureBinaryOutputAndEnforceLimitsThroughGloinIR) {
    const auto root = std::filesystem::path(__FILE__).parent_path() / "fixtures/process";
    const auto fixture = (root / "pipes.gloin").string();
    const auto peer = directory + "/pipe peer";
    expect_success(invoke_raw({"-O2", "-o", peer, (root / "pipe_peer.gloin").string()}), "");
    expect_success(invoke({fixture, "--", peer}), "");
    auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *symbol : {"gloin_process_start_piped", "gloin_process_pipe_read",
                              "gloin_process_pipe_write", "gloin_process_pipe_close",
                              "gloin_process_pipe_wait"}) {
        const auto position = ir.out.find(std::string("callee = @") + symbol);
        ASSERT_NE(position, std::string::npos) << symbol;
        const auto start = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
        EXPECT_EQ(ir.out.find(std::string("llvm.call @") + symbol), std::string::npos);
    }
    EXPECT_NE(ir.out.find("!gloin.result<"), std::string::npos);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/child pipes " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/pipes.out";
        const auto err = directory + "/pipes.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable, peer}, std::nullopt, redirects, 15), 0);
        EXPECT_EQ(read(out), "");
        EXPECT_EQ(read(err), "");
    }
}
TEST_F(ContextLibraryTest, ChildOptionsAndGroupCleanupRunThroughGloinIRInEveryMode) {
    EnvGuard environment("GLOIN_PARENT_ONLY");
    ASSERT_EQ(setenv(environment.name.c_str(), "parent", 1), 0);
    const auto root = std::filesystem::path(__FILE__).parent_path() / "fixtures/process";
    const auto fixture = (root / "options.gloin").string();
    const auto cwd = std::filesystem::canonical(directory).string();
    const auto peer = cwd + "/options-peer";
    expect_success(invoke_raw({"-O2", "-o", peer, (root / "options_peer.gloin").string()}), "");
    expect_success(invoke({fixture, "--", cwd}), "");
    auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *symbol : {"gloin_process_start_options", "gloin_process_observe", "gloin_process_group_signal"}) {
        const auto position = ir.out.find(std::string("callee = @") + symbol);
        ASSERT_NE(position, std::string::npos) << symbol;
        const auto start = ir.out.rfind('\n', position);
        EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
        EXPECT_EQ(ir.out.find(std::string("llvm.call @") + symbol), std::string::npos);
    }
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/child options " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/options.out";
        const auto err = directory + "/options.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable, cwd}, std::nullopt, redirects, 15), 0);
        EXPECT_EQ(read(out), "");
        EXPECT_EQ(read(err), "");
    }
}
TEST_F(ContextLibraryTest, ChildStackLimitReachesTheOsThroughGloinIRInEveryMode) {
    struct rlimit limits {};
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &limits), 0);
    const auto hard = std::to_string(limits.rlim_max);
    const auto fixture = (std::filesystem::path(__FILE__).parent_path() /
                          "fixtures/process/stack_limit.gloin").string();
    expect_success(invoke({fixture, "--", gloin_test::process_fixture, hard}), "");
    const auto ir = invoke({"--emit-ir", fixture});
    ASSERT_EQ(ir.status, 0) << ir.err;
    const auto position = ir.out.find("callee = @gloin_process_start_options");
    ASSERT_NE(position, std::string::npos);
    const auto start = ir.out.rfind('\n', position);
    EXPECT_NE(ir.out.substr(start + 1, position - start).find("gloin.abi_call"), std::string::npos);
    EXPECT_EQ(ir.out.find("llvm.call @gloin_process_start_options"), std::string::npos);
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/stack limit " + optimization;
        expect_success(invoke_raw({optimization, "-o", executable, fixture}), "");
        const auto out = directory + "/limit.out", err = directory + "/limit.err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable,
            {executable, gloin_test::process_fixture, hard}, std::nullopt, redirects, 15), 0);
        EXPECT_EQ(read(out), ""); EXPECT_EQ(read(err), "");
    }
    struct rlimit after {};
    ASSERT_EQ(getrlimit(RLIMIT_STACK, &after), 0);
    EXPECT_EQ(after.rlim_cur, limits.rlim_cur); EXPECT_EQ(after.rlim_max, limits.rlim_max);
}
TEST_F(ContextLibraryTest, ChildInheritsEnvironmentCwdAndBothOutputStreams) {
    EnvGuard environment("GLOIN_CHILD_TEST");
    ASSERT_EQ(setenv("GLOIN_CHILD_TEST", "inherited value", 1), 0);
    const auto file = program(R"(
        def args: [string;3] = {"-c", "printf '%s' \"$GLOIN_CHILD_TEST\"; pwd -P >&2", "fixture"};
        def started: result<process.Child> = process.start(&memory,"/bin/sh",args[..]);
        if started.erroneous { return 1; }
        def mut child: process.Child = started.value;
        def waited: result<process.ExitStatus> = child.wait();
        if waited.erroneous { return 2; }
        def closed: result<void> = child.close();
        if closed.erroneous { return 3; }
        return waited.value.code;)" );
    CwdGuard cwd(directory);
    auto result = invoke({file});
    EXPECT_EQ(result.status, 0);
    EXPECT_EQ(result.out, "inherited value");
    EXPECT_EQ(result.err, std::filesystem::canonical(directory).string() + "\n");
}
TEST_F(ContextLibraryTest, ChildMetadataAllocationFailureStartsNothing) {
    copy_modules();
    auto arena = read((library() / "arena.gloin").string());
    replace(arena, "return __arena_general_alloc(self.state, size, alignment);", "return null;");
    source(arena, "arena.gloin");
    const auto file = program(R"(
        def args: [string;0] = {};
        def started: result<process.Child> = process.start(&memory,"/bin/echo",args[..]);
        if !started.erroneous { return 1; }
        if !strings.equal(started.error.message,"child metadata allocation failed") { return 2; }
        return 0;)" );
    expect_run(invoke({"--stdlib-dir", directory, file}), 0);
}
TEST_F(ContextLibraryTest, RuntimeCollisionsAndMalformedAbisAreRejectedCorrectly) {
    expect_run(
        invoke({source(R"(import "@process"; def gloin_process_arg_count() -> i32 { return 42; }
        def main() -> i32 { if process.arg_count() != 1 { return 1; } return gloin_process_arg_count(); })")}),
        42);
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (std::string decl :
         {"llvm.func @gloin_process_arg_count() -> i32",
          "llvm.func @gloin_process_arg(i32, !llvm.ptr, !llvm.ptr) -> !llvm.ptr",
          "llvm.func @gloin_fs_metadata(!llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_symlink(!llvm.ptr, i64, !llvm.ptr, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_read_link(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_canonical_path(!llvm.ptr, i64, !llvm.ptr, i32, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_temp_dir(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_remove_dir(!llvm.ptr, i64, !llvm.ptr) -> i64",
          "llvm.func @gloin_fs_replace_file(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_start(!llvm.ptr, i64, !llvm.ptr, i32, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_wait(i32, i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_signal(i64, i32, !llvm.ptr) -> i64",
          "llvm.func @gloin_process_start_piped(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_pipe_read(i32, !llvm.ptr, i32, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_pipe_write(i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_pipe_close(i32, !llvm.ptr) -> i64",
          "llvm.func @gloin_process_start_options(!llvm.ptr, i64) -> i32",
          "llvm.func @gloin_process_start_options(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, i64, i32, i32, i32, !llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_observe(i32, i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_process_group_signal(i64, i32, !llvm.ptr) -> i64",
          "llvm.func @gloin_process_pipe_wait(i32, i32, i32, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_fs_rename_replace(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, ...) -> "
          "i32"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + decl +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}
TEST_F(ContextLibraryTest, ExternalRuntimeHasNoImplicitHostArgumentsAndSupportsFilesystemCalls) {
    const auto path = source("bytes", "external.txt");
    auto ir =
        invoke({"--emit-llvm",
                program("if process.arg_count() != 0 { return 1; } def info: fs.MetadataResult = "
                        "fs.metadata(" +
                        literal(path) +
                        "); if info.status != status.OK || info.size != 5 { return 2; } if "
                        "process.cwd(&memory,4096).status != status.OK { return 3; } return 42;")});
    ASSERT_EQ(ir.status, 0) << ir.err;
    const char *runtime = std::getenv("GLOIN_TEST_ARENA_RUNTIME");
    gloin_test::ToolCommand runner{
        gloin_test::mlir_runner,
        {std::string("--shared-libs=") + (runtime ? runtime : gloin_test::arena_runtime)}};
#ifdef __APPLE__
    if (auto *asan = dlsym(RTLD_DEFAULT, "__asan_init")) {
        Dl_info info{};
        ASSERT_NE(dladdr(asan, &info), 0);
        std::string preload = info.dli_fname;
        if (const char *p = std::getenv("DYLD_INSERT_LIBRARIES"); p && *p)
            preload += std::string(":") + p;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
}
TEST_F(ContextLibraryTest, PackagedToolRunsFromAnotherDirectoryWithPathsLabelsAndBadOptions) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "file_tool.gloin";
    auto guide =
        std::filesystem::path(gloin_test::strings_guide).parent_path() / "filesystem-process.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI")) {
        auto root = std::filesystem::path(binary).parent_path();
        example = root / "../share/gloinc/examples/file_tool.gloin";
        guide = root / "../share/doc/gloinc/docs/filesystem-process.md";
    }
    EnvGuard label("GLOIN_COPY_LABEL");
    unsetenv(label.name.c_str());
    std::string bytes;
    for (unsigned i = 0; i < 20000; ++i)
        bytes += char(i % 256);
    source(bytes, "source file.bin");
    CwdGuard cwd(directory);
    expect_run(invoke({example.string(), "--", "--copy", "source file.bin", "new copy.bin"}), 0,
               "copied new copy.bin: 20000\n");
    EXPECT_EQ(read(directory + "/new copy.bin"), bytes);
    setenv(label.name.c_str(), "", 1);
    expect_run(invoke({example.string(), "--", "--copy", "source file.bin", "empty label.bin"}), 0,
               "empty label.bin: 20000\n");
    setenv(label.name.c_str(), "saved", 1);
    expect_run(
        invoke({example.string(), "--", "--copy", directory + "/source file.bin", "label.bin"}), 0,
        "saved label.bin: 20000\n");
    auto bad = invoke({example.string(), "--", "--unknown", "source file.bin", "untouched"});
    EXPECT_EQ(bad.status, 2);
    EXPECT_EQ(bad.err, "unknown option\n");
    EXPECT_FALSE(std::filesystem::exists(directory + "/untouched"));
    expect_run(invoke({example.string(), "--", "--help"}), 0,
               "usage: --copy SOURCE NEW_DESTINATION\n");
    const auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        expect_run(invoke({source(doc.substr(pos, end - pos))}), 0,
                   blocks == 0 ? "report.txt\n/work\n" : "arguments ready\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}
