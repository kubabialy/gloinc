#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
std::string literal(const std::string &value) {
    std::string text = "\"";
    for (char c : value) {
        if (c == '\n')
            text += "\\n";
        else if (c == '\0')
            text += "\\0";
        else if (c == '\\')
            text += "\\\\";
        else if (c == '"')
            text += "\\\"";
        else
            text += c;
    }
    return text + '"';
}
class IoLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source("import \"@io\"; import \"@status\"; import \"@arena\"; import \"@strings\"; "
                      "import \"@std\"; "
                      "def same(r: io.ReadResult, s: string) -> bool { return r.status == "
                      "status.OK && r.os_error == 0 && strings.equal(r.value,s); } "
                      "def main() -> i32 { def mut owner: arena.GeneralArena = "
                      "arena.GeneralArena.create(); defer owner.free(); "
                      "def mut scratch: arena.GeneralArena = arena.GeneralArena.create(); defer "
                      "scratch.free(); " +
                      body + " }");
    }
    std::filesystem::path library() {
        const auto *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
    void copy_modules() {
        for (auto name : {"io.gloin", "std.gloin", "strings.gloin", "arena.gloin", "status.gloin"})
            source(read((library() / name).string()), name);
    }
    static void replace(std::string &text, const std::string &from, const std::string &to) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};
} // namespace
TEST_F(IoLibraryTest, StandardStreamsShareStdioPositionAndKeepOutputSeparate) {
    auto result = invoke({program(R"(
        def mut input: io.Stream = io.stdin();
        def mut output: io.Stream = io.stdout();
        def mut errors: io.Stream = io.stderr();
        if !input.is_open() || !output.is_open() || !errors.is_open() { return 1; }
        if !strings.equal(std.input(&scratch,16).value,"first") { return 2; }
        if !same(input.read_line(&scratch,16),"second") { return 3; }
        if !strings.equal(std.input(&scratch,16).value,"third") { return 4; }
        if !same(input.read_all(&scratch,16),"tail") { return 5; }
        if output.write("out").written != 3 || output.write_line("put").written != 4 { return 6; }
        if errors.write_all("diagnostic\n").written != 11 { return 7; }
        if output.flush().status != status.OK || errors.flush().status != status.OK { return 8; }
        return 42;)")},
                         source("first\nsecond\nthird\ntail", "input.txt"));
    EXPECT_EQ(result.status, 42) << result.err;
    EXPECT_EQ(result.out, "output\n");
    EXPECT_EQ(result.err, "diagnostic\n");
}
TEST_F(IoLibraryTest, FileModesPreserveExclusiveCreateAppendAndExplicitTruncation) {
    auto path = literal(directory + "/a file.bin");
    expect_run(invoke({program("def path: string = " + path + R"(;
        def made: io.FileResult = io.File.create_new(&owner,path);
        if made.status != status.OK || made.os_error != 0 { return 1; }
        def mut f: io.File = made.value; defer f.close();
        if f.write("a\0").written != 2 || f.write_line("b").written != 2 || f.flush().status != status.OK { return 2; }
        if f.close().status != status.OK { return 3; }
        def again: io.FileResult = io.File.create_new(&owner,path);
        if again.status != status.ALREADY_EXISTS || again.os_error == 0 || again.value.is_open() { return 4; }
        def app: io.FileResult = io.File.open_append(&owner,path); def mut a: io.File = app.value; defer a.close();
        if a.write_all("!").written != 1 || a.close().status != status.OK { return 5; }
        def opened: io.FileResult = io.File.open_read(&owner,path); def mut r: io.File = opened.value; defer r.close();
        if !same(r.read_all(&scratch,5),"a\0b\n!") || r.close().status != status.OK { return 6; }
        def trunc: io.FileResult = io.File.open_truncate(&owner,path); def mut t: io.File = trunc.value;
        if trunc.status != status.OK || t.close().status != status.OK { return 7; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(directory + "/a file.bin"), "");
}
TEST_F(IoLibraryTest, FileAndBorrowedAliasesSharePositionAndCloseState) {
    const auto path = source("abc\ndef\n", "data.txt");
    expect_run(invoke({program("def made: io.FileResult = io.File.open_read(&owner," +
                               literal(path) + R"();
        def mut file: io.File = made.value; defer file.close();
        def mut alias: io.File = file;
        def mut stream: io.Stream = file.stream();
        if !same(file.read_line(&scratch,3),"abc") || !same(stream.read_line(&scratch,3),"def") { return 1; }
        if alias.close().status != status.OK || file.is_open() || alias.is_open() || stream.is_open() { return 2; }
        if file.close().status != status.CLOSED || stream.read_chunk(&scratch,3).status != status.CLOSED { return 3; }
        if stream.write_all("x").status != status.CLOSED || stream.flush().status != status.CLOSED { return 4; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, ReadBoundsEndAndBinaryViewsSurviveLaterReads) {
    const auto path = source(std::string("a\0b\nlonger\nx", 12), "data.bin");
    expect_run(invoke({program("def opened: io.FileResult = io.File.open_read(&owner," +
                               literal(path) + R"();
        def mut file: io.File = opened.value; defer file.close();
        if file.read_chunk(&scratch,0).status != status.INVALID { return 1; }
        def first: io.ReadResult = file.read_line(&scratch,3);
        if !same(first,"a\0b") || file.read_line(&scratch,2).status != status.TOO_LONG { return 2; }
        if !same(file.read_chunk(&scratch,3),"x") || !same(first,"a\0b") { return 3; }
        if file.read_chunk(&scratch,3).status != status.END || !same(file.read_all(&scratch,0),"") { return 4; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, ReadAllReturnsBoundedPrefixAndKeepsExcessForNextCall) {
    auto path = source("abcdef", "all.txt");
    expect_run(invoke({program("def opened: io.FileResult = io.File.open_read(&owner," +
                               literal(path) + R"();
        def mut f: io.File = opened.value; defer f.close();
        def zero: io.ReadResult = f.read_all(&scratch,0);
        def prefix: io.ReadResult = f.read_all(&scratch,3);
        if zero.status != status.TOO_LONG || !strings.is_empty(zero.value) { return 1; }
        if prefix.status != status.TOO_LONG || prefix.os_error != 0 || !strings.equal(prefix.value,"abc") { return 2; }
        if !same(f.read_all(&scratch,3),"def") { return 3; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, WrongDirectionAndInvalidPathsHaveDefinedFailures) {
    auto existing = source("keep", "keep.bin");
    expect_run(invoke({program("def path: string = " + literal(existing) + R"(;
        def mut input: io.Stream = io.stdin(); def mut output: io.Stream = io.stdout();
        if input.write_all("").status != status.INVALID || input.flush().status != status.INVALID || output.read_chunk(&scratch,1).status != status.INVALID { return 1; }
        def bad: io.FileResult = io.File.open_truncate(&owner,strings.concat(&scratch,path,"\0tail",4096).value);
        if bad.status != status.INVALID || bad.os_error != 0 || bad.value.is_open() { return 2; }
        def mut closed: io.File = bad.value;
        if closed.close().status != status.CLOSED || closed.read_line(&scratch,1).status != status.CLOSED { return 3; }
        if io.File.open_read(&owner,"").status != status.INVALID { return 4; }
        scratch.free();
        if input.read_chunk(&scratch,0).status != status.INVALID || input.read_all(&scratch,18446744073709551615).status != status.NO_MEMORY { return 5; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(existing), "keep");
}
TEST_F(IoLibraryTest, MissingFileReportsOsErrorAndMessageOwnership) {
    expect_run(invoke({program("def missing: io.FileResult = io.File.open_read(&owner," +
                               literal(directory + "/missing") + R"();
        if missing.status != status.NOT_FOUND || missing.os_error == 0 { return 1; }
        def text: io.MessageResult = io.error_message(&scratch,missing.os_error);
        if text.status != status.OK || strings.is_empty(text.value) { return 2; }
        def saved: strings.StringResult = strings.copy(&owner,text.value);
        scratch.reset();
        if saved.status != status.OK || strings.is_empty(saved.value) { return 3; }
        scratch.free();
        if io.error_message(&scratch,-1).status != status.INVALID || !strings.equal(io.error_message(&scratch,0).value,"no OS error") { return 4; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, MetadataAllocationFailurePrecedesCreateOrTruncate) {
    copy_modules();
    auto arena = read((library() / "arena.gloin").string());
    replace(arena, "return __arena_general_alloc(self.state, size, alignment);", "return null;");
    source(arena, "arena.gloin");
    const auto existing = source("keep", "keep.bin"), absent = directory + "/absent";
    expect_run(
        invoke({"--stdlib-dir", directory,
                program("if io.File.open_truncate(&owner," + literal(existing) +
                        ").status != status.NO_MEMORY { return 1; }"
                        "if io.File.create_new(&owner," +
                        literal(absent) + ").status != status.NO_MEMORY { return 2; } return 0;")}),
        0);
    EXPECT_EQ(read(existing), "keep");
    EXPECT_FALSE(std::filesystem::exists(absent));
}
TEST_F(IoLibraryTest, ReadAllocationFailureConsumesNothingAndScratchCanBeReused) {
    copy_modules();
    auto arena = read((library() / "arena.gloin").string());
    replace(arena, "def mut state: *u8,", "def mut state: *u8, def mut byte_requests: u64,");
    replace(arena, "GeneralArena {\n            state: state\n        }",
            "GeneralArena {\n            state: state,\n            byte_requests: 0\n        }");
    replace(arena, "def bytes: *u8 = __arena_general_alloc(self.state, size, 1);",
            "self.byte_requests = self.byte_requests + 1; if self.byte_requests == 1 { return "
            "null; } def bytes: *u8 = __arena_general_alloc(self.state, size, 1);");
    source(arena, "arena.gloin");
    const auto path = source("abc", "data.txt");
    expect_run(invoke({"--stdlib-dir", directory,
                       program("def made: io.FileResult = io.File.open_read(&owner," +
                               literal(path) + R"();
        def mut file: io.File = made.value; defer file.close();
        def failed: io.ReadResult = file.read_all(&scratch,3);
        if failed.status != status.NO_MEMORY || failed.os_error != 0 || !strings.is_empty(failed.value) { return 1; }
        if !same(file.read_chunk(&scratch,2),"ab") { return 2; }
        scratch.reset();
        if !same(file.read_line(&scratch,2),"c") { return 3; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, FailedCloseInvalidatesAliasesAndWriteLineReportsSuffixFailure) {
    copy_modules();
    auto io = read((library() / "io.gloin").string());
    // Still consume the actual resource, then inject an observable close failure.
    replace(io, "def code: i32 = __io_close(handle, &error);",
            "__io_close(handle, &error); error = 123; def code: i32 = status.IO_ERROR;");
    replace(io, "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult {",
            "def write_mode(self: &Stream, text: string, all: i32) -> WriteResult { if "
            "strings.equal(text, \"\\n\") { return WriteResult { status: status.IO_ERROR, "
            "os_error: 124, written: 0 }; }");
    source(io, "io.gloin");
    const auto path = directory + "/out";
    expect_run(invoke({"--stdlib-dir", directory,
                       program("def made: io.FileResult = io.File.create_new(&owner," +
                               literal(path) + R"();
        def mut f: io.File = made.value; def mut alias: io.File = f; def stream: io.Stream = f.stream();
        def written: io.WriteResult = f.write_line("abc");
        if written.status != status.IO_ERROR || written.os_error != 124 || written.written != 3 { return 1; }
        def closed: io.IoResult = f.close();
        if closed.status != status.IO_ERROR || closed.os_error != 123 || alias.is_open() || stream.is_open() { return 2; }
        if alias.close().status != status.CLOSED { return 3; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(path), "abc");
}
TEST_F(IoLibraryTest, MethodsEnforceMutabilityPrivacyAndBorrowedStreamsCannotClose) {
    for (const std::string body :
         {"def s: io.Stream = io.stdout(); s.write(\"x\");",
          "def mut s: io.Stream = io.stdout(); s.close();",
          "def mut s: io.Stream = io.stdin(); s.standard;",
          "def mut s: io.Stream = io.stdout(); s.handle();",
          "def mut s: io.Stream = io.stdin(); def n: i64 = 1; s.read_line(&scratch,n);",
          "io.File.open_read(\"file\");",
          "def f: io.File = io.File.open_read(&owner,\"x\").value; f.close();",
          "io.Stream { state: null, standard: 2, readable: false };", "__io_standard(1);",
          "io.__io_standard(1);", "io.error_message(&scratch,true);"}) {
        const auto file = program(body + "return 0;");
        for (std::string mode : {"--check", "--emit-ir", "--emit-llvm", "--run"})
            expect_error(invoke({mode, file}), 1, "error:");
    }
}
TEST_F(IoLibraryTest, PrivateIoPrimitivesAreTypedAndRestrictedToCanonicalIo) {
    const auto file = source("import \"@io\"; def main() -> i32 { io.probe(); return 0; }");
    for (std::string body :
         {"__io_standard(true);", "def mut n: i32 = 0; __io_open(\"x\",0,&n);",
          "def n: i32 = 0; __io_close(null,&n);", "def mut n: i64 = 0; __io_flush(null,&n);",
          "def mut n: i32 = 0; def mut count: u64 = 0; __io_write(null,\"x\",true,&count,&n);"}) {
        source("def pub probe() -> void {" + body + "}", "io.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "error:");
    }
    source("def __io_read() -> void {}", "io.gloin");
    expect_error(invoke({"--stdlib-dir", directory, "--check", file}), 1, "Cannot redeclare");
    source("def pub probe() -> void { __io_standard(1); }", "std.gloin");
    expect_error(invoke({"--stdlib-dir", directory,
                         source("import \"@std\"; def main() -> i32 { std.probe(); return 0; }",
                                "other.gloin")}),
                 1, "Undefined variable");
}
TEST_F(IoLibraryTest, NativeNamesDoNotCollideAndJitRejectsWrongAbis) {
    expect_run(invoke({source(R"(import "@io";
        def gloin_io_standard() -> i32 { return 42; }
        def main() -> i32 { def output: io.Stream = io.stdout(); if !output.is_open() { return 1; } return gloin_io_standard(); })")}),
               42);
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (std::string decl :
         {"llvm.func @gloin_io_standard(i64) -> !llvm.ptr",
          "llvm.func @gloin_io_open(!llvm.ptr, i32, i32, !llvm.ptr, !llvm.ptr) -> !llvm.ptr",
          "llvm.func @gloin_io_read(!llvm.ptr, !llvm.ptr, i64, i32, !llvm.ptr) -> i32",
          "llvm.func @gloin_io_write(!llvm.ptr, !llvm.ptr, i64, i1, !llvm.ptr, !llvm.ptr) -> i32",
          "llvm.func @gloin_io_flush(!llvm.ptr, !llvm.ptr, ...) -> i32",
          "llvm.func @gloin_io_close(%a: !llvm.ptr, %b: !llvm.ptr) -> i32 { %v = "
          "llvm.mlir.constant(0 : i32) : i32 llvm.return %v : i32 }"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            "module { " + decl +
                " llvm.func @main() -> i32 { %v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v "
                ": i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}
TEST_F(IoLibraryTest, ExternalRuntimeExecutesFileLifecycleAndBinaryReads) {
    const auto path = directory + "/external.bin";
    auto ir = invoke({"--emit-llvm", program("def path: string = " + literal(path) + R"(;
        def made: io.FileResult = io.File.create_new(&owner,path); def mut f: io.File = made.value;
        if f.write_all("a\0b").written != 3 || f.close().status != status.OK { return 1; }
        def opened: io.FileResult = io.File.open_read(&owner,path); def mut r: io.File = opened.value; defer r.close();
        if !same(r.read_all(&scratch,3),"a\0b") { return 2; } return 42;)")});
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
        if (const char *existing = std::getenv("DYLD_INSERT_LIBRARIES"); existing && *existing)
            preload += std::string(":") + existing;
        runner.arguments.insert(runner.arguments.begin(), runner.path);
        runner.arguments.insert(runner.arguments.begin(), "DYLD_INSERT_LIBRARIES=" + preload);
        runner.path = "/usr/bin/env";
    }
#endif
    auto result = gloin_test::run_external_mlir(ir.out, {gloin_test::mlir_opt, {}}, runner);
    ASSERT_TRUE(static_cast<bool>(result)) << llvm::toString(result.takeError());
    EXPECT_EQ(*result, 42);
    EXPECT_EQ(read(path), std::string("a\0b", 3));
}
TEST_F(IoLibraryTest, PackagedCopierFilterAndGuideProgramsRunVerbatim) {
    auto examples = std::filesystem::path(gloin_test::strings_example).parent_path();
    auto guide = std::filesystem::path(gloin_test::strings_guide).parent_path() / "io.md";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI")) {
        auto root = std::filesystem::path(binary).parent_path();
        examples = root / "../share/gloinc/examples";
        guide = root / "../share/doc/gloinc/docs/io.md";
    }
    std::string binary;
    for (unsigned i = 0; i < 20000; ++i)
        binary += static_cast<char>(i % 256);
    const auto input = source(binary, "input.bin"), output = directory + "/copy.bin";
    auto paths = source(input + "\n" + output + "\n", "paths.txt");
    expect_run(invoke({(examples / "io_copy.gloin").string()}, paths), 0, "20000\n");
    EXPECT_EQ(read(output), binary);
    auto again = invoke({(examples / "io_copy.gloin").string()}, paths);
    EXPECT_EQ(again.status, 4);
    EXPECT_EQ(again.err, "cannot create new destination\n");
    EXPECT_EQ(read(output), binary);
    expect_run(invoke({(examples / "io_filter.gloin").string()},
                      source(" hello\r\n#skip\n\n world \n", "filter.txt")),
               0, "HELLO\nWORLD\n");
    auto bad = invoke({(examples / "io_filter.gloin").string()},
                      source(std::string(257, 'x') + "\n", "long.txt"));
    EXPECT_EQ(bad.status, 2);
    EXPECT_EQ(bad.err, "line read failed\n");
    const auto doc = read(guide.string());
    const std::string marker = "```gloin\n";
    size_t pos = 0;
    unsigned blocks = 0;
    while ((pos = doc.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        auto end = doc.find("```", pos);
        ASSERT_NE(end, std::string::npos);
        const auto stdin_file = source("hello\n", "guide-input.txt");
        expect_run(invoke({source(doc.substr(pos, end - pos))}, stdin_file), 0,
                   blocks == 0 ? "hello\n" : "no OS error\n");
        pos = end + 3;
        ++blocks;
    }
    EXPECT_EQ(blocks, 2u);
}
