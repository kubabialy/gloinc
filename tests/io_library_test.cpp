#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <cerrno>
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
        return source(
            R"(import "@io"; import "@status"; import "@arena"; import "@strings"; import "@std";
            def passed(outcome: result<void>) -> bool {
                if outcome.erroneous { return false; } return true;
            }
            def code<T>(outcome: result<T>) -> i32 {
                if outcome.erroneous { return outcome.error.code; } return status.OK;
            }
            def void_code(outcome: result<void>) -> i32 {
                if outcome.erroneous { return outcome.error.code; } return status.OK;
            }
            def read_code(outcome: result<void>, progress: io.ReadState) -> i32 {
                if outcome.erroneous { return outcome.error.code; }
                if progress.end { return status.END; } return status.OK;
            }
            def same(outcome: result<void>, progress: io.ReadState, expected: string) -> bool {
                if outcome.erroneous { return false; }
                return !progress.end && strings.equal(progress.value,expected);
            }
            def cleanup(file: &io.File) -> void {
                if file.is_open() {
                    def closed: result<void> = file.close();
                    if closed.erroneous { std.println(closed.error.message); }
                }
            }
            def main() -> i32 {
                def mut owner: arena.GeneralArena = arena.GeneralArena.create(); defer owner.free();
                def mut scratch: arena.GeneralArena = arena.GeneralArena.create(); defer scratch.free();
                def mut reading: io.ReadState = io.ReadState.empty();
                def mut writing: io.WriteState = io.WriteState.empty();
        )" + body +
            " }");
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
        ASSERT_NE(text.find(from), std::string::npos) << from;
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
        def mut input: io.Stream = io.stdin(); def mut output: io.Stream = io.stdout();
        def mut errors: io.Stream = io.stderr();
        if !input.is_open() || !output.is_open() || !errors.is_open() { return 1; }
        def first: result<std.InputLine> = std.input(&scratch,16);
        if first.erroneous { return 2; }
        if first.value.end || !strings.equal(first.value.value,"first") { return 2; }
        if !same(input.read_line(&scratch,16,&reading),reading,"second") { return 3; }
        def third: result<std.InputLine> = std.input(&scratch,16);
        if third.erroneous { return 4; }
        if third.value.end || !strings.equal(third.value.value,"third") { return 4; }
        if !same(input.read_all(&scratch,16,&reading),reading,"tail") { return 5; }
        if !passed(output.write("out",&writing)) || writing.written != 3 { return 6; }
        if !passed(output.write_line("put",&writing)) || writing.written != 4 { return 6; }
        if !passed(errors.write_all("diagnostic\n",&writing)) || writing.written != 11 { return 7; }
        if !passed(output.flush()) || !passed(errors.flush()) { return 8; }
        return 42;)")},
                         source("first\nsecond\nthird\ntail", "input.txt"));
    EXPECT_EQ(result.status, 42) << result.err;
    EXPECT_EQ(result.out, "output\n");
    EXPECT_EQ(result.err, "diagnostic\n");
}
TEST_F(IoLibraryTest, FileModesPreserveExclusiveCreateAppendAndExplicitTruncation) {
    auto path = literal(directory + "/a file.bin");
    expect_run(invoke({program("def path: string = " + path + R"(;
        def made: result<io.File> = io.File.create_new(&owner,path);
        if made.erroneous { return 1; }
        def mut f: io.File = made.value; defer cleanup(&f);
        if !passed(f.write("a\0",&writing)) || writing.written != 2 { return 2; }
        if !passed(f.write_line("b",&writing)) || writing.written != 2 || !passed(f.flush()) { return 2; }
        if !passed(f.close()) { return 3; }
        def again: result<io.File> = io.File.create_new(&owner,path);
        if !again.erroneous { return 4; }
        if again.error.code != status.ALREADY_EXISTS || again.error.os_error == 0 { return 4; }
        def app: result<io.File> = io.File.open_append(&owner,path); if app.erroneous { return 5; }
        def mut a: io.File = app.value; defer cleanup(&a);
        if !passed(a.write_all("!",&writing)) || writing.written != 1 || !passed(a.close()) { return 5; }
        def opened: result<io.File> = io.File.open_read(&owner,path); if opened.erroneous { return 6; }
        def mut r: io.File = opened.value; defer cleanup(&r);
        if !same(r.read_all(&scratch,5,&reading),reading,"a\0b\n!") || !passed(r.close()) { return 6; }
        def trunc: result<io.File> = io.File.open_truncate(&owner,path); if trunc.erroneous { return 7; }
        def mut t: io.File = trunc.value; if !passed(t.close()) { return 7; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(directory + "/a file.bin"), "");
}
TEST_F(IoLibraryTest, DirectByteIOPreservesBinaryDataAndReportsEOF) {
    const auto input = source(std::string("a\0b\xff", 4), "direct-input.bin");
    const auto output = directory + "/direct-output.bin";
    expect_run(invoke({program("def input_path: string = " + literal(input) +
                               "; def output_path: string = " + literal(output) + R"(;
        def opened: result<io.File> = io.File.open_read(&owner,input_path); if opened.erroneous { return 1; }
        def mut reader: io.File = opened.value; defer cleanup(&reader);
        def mut progress: io.ByteReadState = io.ByteReadState.empty();
        def mut empty: [u8;0] = zeroed;
        if void_code(reader.read_into(empty[..],&progress)) != status.INVALID { return 2; }
        def mut bytes: [u8;4] = zeroed; def last: u8 = 255;
        if !passed(reader.read_into(bytes[..],&progress)) || progress.end || progress.read != 4 || bytes[3] != last { return 3; }
        if !passed(reader.read_into(bytes[..],&progress)) || !progress.end || progress.read != 0 { return 4; }
        def made: result<io.File> = io.File.create_new(&owner,output_path); if made.erroneous { return 5; }
        def mut writer: io.File = made.value; defer cleanup(&writer);
        if !passed(writer.write_all_bytes(empty[..],&writing)) || writing.written != 0 { return 6; }
        if !passed(writer.write_all_bytes(bytes[..],&writing)) || writing.written != 4 { return 7; }
        if !passed(writer.close()) || !passed(reader.close()) { return 8; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(output), std::string("a\0b\xff", 4));
}
TEST_F(IoLibraryTest, FileAndBorrowedAliasesSharePositionAndCloseState) {
    const auto path = source("abc\ndef\n", "data.txt");
    expect_run(invoke({program("def made: result<io.File> = io.File.open_read(&owner," +
                               literal(path) + R"();
        if made.erroneous { return 1; }
        def mut file: io.File = made.value; defer cleanup(&file);
        def mut alias: io.File = file; def mut stream: io.Stream = file.stream();
        if !same(file.read_line(&scratch,3,&reading),reading,"abc") || !same(stream.read_line(&scratch,3,&reading),reading,"def") { return 1; }
        if !passed(alias.close()) || file.is_open() || alias.is_open() || stream.is_open() { return 2; }
        if void_code(file.close()) != status.CLOSED || void_code(stream.read_chunk(&scratch,3,&reading)) != status.CLOSED { return 3; }
        if void_code(stream.write_all("x",&writing)) != status.CLOSED || void_code(stream.flush()) != status.CLOSED { return 4; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, ReadBoundsEndAndBinaryViewsSurviveLaterReads) {
    const auto path = source(std::string("a\0b\nlonger\nx", 12), "data.bin");
    expect_run(invoke({program("def opened: result<io.File> = io.File.open_read(&owner," +
                               literal(path) + R"();
        if opened.erroneous { return 1; }
        def mut file: io.File = opened.value; defer cleanup(&file);
        if void_code(file.read_chunk(&scratch,0,&reading)) != status.INVALID { return 1; }
        if !same(file.read_line(&scratch,3,&reading),reading,"a\0b") { return 2; }
        def retained: string = reading.value;
        if void_code(file.read_line(&scratch,2,&reading)) != status.TOO_LONG || !strings.is_empty(reading.value) || reading.end { return 2; }
        if !same(file.read_chunk(&scratch,3,&reading),reading,"x") || !strings.equal(retained,"a\0b") { return 3; }
        if read_code(file.read_chunk(&scratch,3,&reading),reading) != status.END || !same(file.read_all(&scratch,0,&reading),reading,"") { return 4; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, ReadAllReturnsBoundedPrefixAndKeepsExcessForNextCall) {
    const auto path = source("abcdef", "all.txt");
    expect_run(invoke({program("def opened: result<io.File> = io.File.open_read(&owner," +
                               literal(path) + R"();
        if opened.erroneous { return 1; }
        def mut f: io.File = opened.value; defer cleanup(&f);
        if void_code(f.read_all(&scratch,0,&reading)) != status.TOO_LONG || !strings.is_empty(reading.value) || reading.end { return 1; }
        def prefix: result<void> = f.read_all(&scratch,3,&reading);
        if !prefix.erroneous { return 2; }
        if prefix.error.code != status.TOO_LONG || prefix.error.os_error != 0 || !strings.equal(reading.value,"abc") || reading.end { return 2; }
        if !same(f.read_all(&scratch,3,&reading),reading,"def") { return 3; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, WrongDirectionAndInvalidPathsHaveDefinedFailures) {
    const auto existing = source("keep", "keep.bin");
    expect_run(invoke({program("def path: string = " + literal(existing) + R"(;
        def mut input: io.Stream = io.stdin(); def mut output: io.Stream = io.stdout();
        if void_code(input.write_all("",&writing)) != status.INVALID || void_code(input.flush()) != status.INVALID || void_code(output.read_chunk(&scratch,1,&reading)) != status.INVALID { return 1; }
        def bad: result<io.File> = io.File.open_truncate(&owner,(match strings.concat(&scratch,path,"\0tail",4096) { case value(v) => v; case error(e) => ""; }));
        if !bad.erroneous { return 2; }
        if bad.error.code != status.INVALID || bad.error.os_error != 0 { return 2; }
        if code<io.File>(io.File.open_read(&owner,"")) != status.INVALID { return 4; }
        scratch.free();
        if void_code(input.read_chunk(&scratch,0,&reading)) != status.INVALID || void_code(input.read_all(&scratch,18446744073709551615,&reading)) != status.NO_MEMORY { return 5; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(existing), "keep");
}
TEST_F(IoLibraryTest, MissingFileReportsOsErrorAndMessageOwnership) {
    expect_run(invoke({program("def missing: result<io.File> = io.File.open_read(&owner," +
                               literal(directory + "/missing") + R"();
        if !missing.erroneous { return 1; }
        if missing.error.code != status.NOT_FOUND || missing.error.os_error != )" +
                               std::to_string(ENOENT) + R"( { return 1; }
        def text: result<string> = io.error_message(&scratch,missing.error.os_error);
        if text.erroneous { return 2; }
        if strings.is_empty(text.value) { return 2; }
        def saved: result<string> = strings.copy(&owner,text.value);
        if saved.erroneous { return 97; }
        scratch.reset();
        if saved.erroneous || strings.is_empty(saved.value) { return 3; }
        scratch.free();
        if code<string>(io.error_message(&scratch,-1)) != status.INVALID { return 4; }
        def zero: result<string> = io.error_message(&scratch,0); if zero.erroneous { return 4; }
        if !strings.equal(zero.value,"no OS error") { return 4; }
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
                program("if code<io.File>(io.File.open_truncate(&owner," + literal(existing) +
                        ")) != status.NO_MEMORY { return 1; }"
                        "if code<io.File>(io.File.create_new(&owner," +
                        literal(absent) + ")) != status.NO_MEMORY { return 2; } return 0;")}),
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
                       program("def made: result<io.File> = io.File.open_read(&owner," +
                               literal(path) + R"();
        if made.erroneous { return 1; }
        def mut file: io.File = made.value; defer cleanup(&file);
        reading.value = "stale"; reading.end = true;
        def failed: result<void> = file.read_all(&scratch,3,&reading);
        if !failed.erroneous { return 1; }
        if failed.error.code != status.NO_MEMORY || failed.error.os_error != 0 || !strings.is_empty(reading.value) || reading.end { return 1; }
        if !same(file.read_chunk(&scratch,2,&reading),reading,"ab") { return 2; }
        scratch.reset();
        if !same(file.read_line(&scratch,2,&reading),reading,"c") { return 3; }
        return 0;)")}),
               0);
}
TEST_F(IoLibraryTest, FailedCloseInvalidatesAliasesAndWriteLineReportsSuffixFailure) {
    copy_modules();
    auto io = read((library() / "io.gloin").string());
    replace(
        io, "def code: i32             = __io_close(handle, &failure_code);",
        "__io_close(handle, &failure_code); failure_code = 123; def code: i32 = status.IO_ERROR;");
    const std::string signature = "def write_mode(self: &Stream, text: string, all: i32, progress: "
                                  "&WriteState) -> result<void> {";
    replace(io, signature,
            signature + " if strings.equal(text, \"\\n\") { *progress = WriteState.empty(); return "
                        "error(\"injected write failure\",status.IO_ERROR,124); }");
    source(io, "io.gloin");
    const auto path = directory + "/out";
    expect_run(invoke({"--stdlib-dir", directory,
                       program("def made: result<io.File> = io.File.create_new(&owner," +
                               literal(path) + R"();
        if made.erroneous { return 1; }
        def mut f: io.File = made.value; def mut alias: io.File = f; def stream: io.Stream = f.stream();
        def written: result<void> = f.write_line("abc",&writing);
        if !written.erroneous { return 1; }
        if written.error.code != status.IO_ERROR || written.error.os_error != 124 || writing.written != 3 { return 1; }
        def closed: result<void> = f.close();
        if !closed.erroneous { return 2; }
        if closed.error.code != status.IO_ERROR || closed.error.os_error != 123 || alias.is_open() || stream.is_open() { return 2; }
        if void_code(alias.close()) != status.CLOSED { return 3; }
        return 0;)")}),
               0);
    EXPECT_EQ(read(path), "abc");
}
TEST_F(IoLibraryTest, MethodsEnforceMutabilityPrivacyAndBorrowedStreamsCannotClose) {
    for (const std::string body :
         {"def s: io.Stream = io.stdout(); def written: result<void> = s.write(\"x\",&writing); if "
          "written.erroneous { return 1; }",
          "def mut s: io.Stream = io.stdout(); s.close();",
          "def mut s: io.Stream = io.stdin(); s.standard;",
          "def mut s: io.Stream = io.stdout(); s.handle();",
          "def mut s: io.Stream = io.stdin(); def n: i64 = 1; def read: result<void> = "
          "s.read_line(&scratch,n,&reading); if read.erroneous { return 1; }",
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
        def made: result<io.File> = io.File.create_new(&owner,path); if made.erroneous { return 1; }
        def mut f: io.File = made.value;
        if !passed(f.write_all("a\0b",&writing)) || writing.written != 3 || !passed(f.close()) { return 1; }
        def opened: result<io.File> = io.File.open_read(&owner,path); if opened.erroneous { return 2; }
        def mut r: io.File = opened.value; defer cleanup(&r);
        if !same(r.read_all(&scratch,3,&reading),reading,"a\0b") { return 2; } return 42;)")});
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

TEST_F(IoLibraryTest, PartialTransfersRetainStateOnFailureInJitAndNativeModes) {
    copy_modules();
    auto io = read((library() / "io.gloin").string());
    // Perform real transfers, then inject a failure. The state must retain exactly
    // the bytes actually transferred; the next call must reset it before validation.
    replace(io,
            "def code: i32             = __io_write(handle, text, all, &written, &failure_code);",
            "def sliced: result<string> = strings.slice_bytes(text,0,2); if sliced.erroneous { "
            "return sliced.error; } def limited: string = sliced.value; "
            "__io_write(handle,limited,all,&written,&failure_code); "
            "failure_code = 123; def code: i32 = status.IO_ERROR;");
    replace(io,
            "def code: i32             = __io_read(handle, &*bytes, max_bytes, mode, &length, "
            "&failure_code);",
            "__io_read(handle,&*bytes,max_bytes,mode,&length,&failure_code); "
            "failure_code = 124; def code: i32 = status.IO_ERROR;");
    replace(io,
            "def code: i32             = __io_read_bytes(handle, &destination[0], destination.len, "
            "&received, "
            "&failure_code);",
            "__io_read_bytes(handle,&destination[0],destination.len,&received,&failure_code); "
            "failure_code = 125; def code: i32 = status.IO_ERROR;");
    replace(io,
            "def code: i32             = __io_write_bytes(handle, &bytes[0], bytes.len, all, "
            "&written, &failure_code);",
            "__io_write_bytes(handle,&bytes[0],2,all,&written,&failure_code); "
            "failure_code = 126; def code: i32 = status.IO_ERROR;");
    source(io, "io.gloin");
    const auto input = source("abcd", "partial-input");
    const auto output = directory + "/partial-output";
    const auto file = program("def input_path: string = " + literal(input) +
                              "; def output_path: string = " + literal(output) + R"(;
        def opened: result<io.File> = io.File.open_read(&owner,input_path);
        if opened.erroneous { return 1; }
        def mut reader: io.File = opened.value; defer cleanup(&reader);
        def failed: result<void> = reader.read_chunk(&scratch,2,&reading);
        if !failed.erroneous { return 2; }
        if failed.error.code != status.IO_ERROR || failed.error.os_error != 124 || !strings.equal(reading.value,"ab") || reading.end { return 3; }
        def mut bytes: [u8;4] = zeroed;
        def mut direct: io.ByteReadState = io.ByteReadState.empty();
        def next: result<void> = reader.read_into(bytes[..],&direct);
        if !next.erroneous { return 4; }
        if next.error.os_error != 125 || direct.read != 2 || direct.end { return 5; }
        def c: u8 = 99; def d: u8 = 100;
        if bytes[0] != c || bytes[1] != d { return 6; }
        def made: result<io.File> = io.File.open_truncate(&owner,output_path);
        if made.erroneous { return 7; }
        def mut writer: io.File = made.value; defer cleanup(&writer);
        def written: result<void> = writer.write_line("wxyz",&writing);
        if !written.erroneous { return 8; }
        if written.error.code != status.IO_ERROR || written.error.os_error != 123 || writing.written != 2 { return 9; }
        def binary: [u8;4] = {49,0,50,51};
        def binary_write: result<void> = writer.write_all_bytes(binary[..],&writing);
        if !binary_write.erroneous { return 15; }
        if binary_write.error.code != status.IO_ERROR || binary_write.error.os_error != 126 || writing.written != 2 { return 16; }
        if !passed(writer.close()) || !passed(reader.close()) { return 10; }

        if void_code(writer.write_all("later",&writing)) != status.CLOSED || writing.written != 0 { return 11; }
        if void_code(reader.read_chunk(&scratch,4,&reading)) != status.CLOSED || !strings.is_empty(reading.value) || reading.end { return 12; }
        if void_code(reader.read_into(bytes[..],&direct)) != status.CLOSED || direct.read != 0 || direct.end { return 13; }
        if bytes[0] != c || bytes[1] != d { return 14; }
        return 42;)");
    auto ir = invoke({"--stdlib-dir", directory, "--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    for (const auto *operation : {"!gloin.result<!gloin.struct<", "gloin.result_failure",
                                  "gloin.error_code", "gloin.error_os_error", "gloin.abi_call"})
        EXPECT_NE(ir.out.find(operation), std::string::npos) << operation;
    expect_run(invoke({"--stdlib-dir", directory, "--jit", file}), 42);
    EXPECT_EQ(read(output), std::string("wx1\0", 4));
    for (const auto *optimization : {"-O0", "-O2"}) {
        const auto executable = directory + "/partial-transfer" + optimization;
        expect_success(
            invoke_raw({"--stdlib-dir", directory, optimization, "-o", executable, file}), "");
        const auto err = executable + ".err";
        const std::optional<llvm::StringRef> redirects[] = {std::nullopt, std::nullopt, err};
        EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, 10),
                  42)
            << read(err);
        EXPECT_EQ(read(output), std::string("wx1\0", 4));
    }
}
TEST_F(IoLibraryTest, ResultsRequireHandlingAndCheckedPayloads) {
    for (const auto *body :
         {"io.File.open_read(&owner,\"x\");", "io.error_message(&scratch,0);",
          "def mut out: io.Stream = io.stdout(); out.write_all(\"x\",&writing);",
          "def mut out: io.Stream = io.stdout(); out.flush();",
          "def mut input: io.Stream = io.stdin(); input.read_line(&scratch,4,&reading);",
          "def opened: result<io.File> = io.File.open_read(&owner,\"x\"); if opened.erroneous { "
          "return 1; } def mut file: io.File = opened.value; file.close();"})
        expect_error(invoke({"--check", program(std::string(body) + " return 0;")}), 1,
                     "A result cannot be discarded");
    for (const auto *body : {"def opened: result<io.File> = io.File.open_read(&owner,\"x\"); def "
                             "file: io.File = opened.value;",
                             "def message: result<string> = io.error_message(&scratch,0); "
                             "std.println(message.value);"})
        expect_error(invoke({"--check", program(std::string(body) + " return 0;")}), 1,
                     "requires a proven non-erroneous path");
}
