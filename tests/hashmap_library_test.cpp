#include "hashmap_runtime.h"
#include "jit_runner.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Parser/Parser.h"
#include "support/cli_fixture.h"
#include "support/external_runner.h"
#include <filesystem>
#include <tuple>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {
class HashmapLibraryTest : public gloin_test::CliFixture {
  protected:
    std::string program(const std::string &body) {
        return source(R"(
            import "@arena"; import "@hashmap"; import "@status"; import "@strings";
            def inserted(outcome: result<bool>, expected: bool) -> bool {
                if outcome.erroneous { return false; }
                return outcome.value == expected;
            }
            def code(outcome: result<void>) -> i32 {
                if outcome.erroneous { return outcome.error.code; }
                return status.OK;
            }
            def main() -> i32 {
                def mut memory: arena.GeneralArena = arena.GeneralArena.create();
                defer memory.free();
        )" + body + "}");
    }
    std::filesystem::path library() {
        const char *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
    void execution_modes(const std::string &file, int value = 0,
                         const std::vector<std::string> &options = {}) {
        auto arguments = options;
        arguments.push_back(file);
        expect_run(invoke(arguments), value);
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto executable = directory + "/hashmap";
            arguments = options;
            arguments.insert(arguments.end(), {mode, "-o", executable, file});
            expect_success(invoke_raw(arguments), "");
            EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                      value);
        }
    }
    void fail_allocation(unsigned number) {
        for (const auto *name : {"hashmap", "arena", "memory", "strings", "status"})
            source(read((library() / (std::string(name) + ".gloin")).string()),
                   std::string(name) + ".gloin");
        auto arena = read((library() / "arena.gloin").string());
        auto replace = [&](const std::string &from, const std::string &to) {
            size_t pos = 0;
            while ((pos = arena.find(from, pos)) != std::string::npos) {
                arena.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replace("def mut state: *u8,", "def mut state: *u8, def mut requests: u64,");
        replace("state: state\n", "state: state, requests: 0\n");
        replace("__arena_general_alloc(self.state, size, alignment)",
                "self.test_allocate(size, alignment)");
        const auto end = arena.rfind('}');
        ASSERT_NE(end, std::string::npos);
        arena.insert(
            end,
            "def test_allocate(self: &GeneralArena, size: u64, alignment: u64) -> *u8 { "
            "self.requests = self.requests + 1; if self.requests == " +
                std::to_string(number) +
                " { return null; } return __arena_general_alloc(self.state, size, alignment); }");
        source(arena, "arena.gloin");
    }
};
} // namespace

TEST_F(HashmapLibraryTest, GrowthRemovalAliasesAndIterationInJitAndNative) {
    auto file = program(R"(
        def made: result<hashmap.HashMap<i64, i64>> = hashmap.HashMap<i64, i64>.create(&memory, 0);
        if made.erroneous { return 1; }
        def mut map: hashmap.HashMap<i64, i64> = made.value;
        def mut alias: hashmap.HashMap<i64, i64> = map;
        if map.len() != 0 || map.cap() != 0 || map.get(0) != null || map.get_mut(0) != null
            || map.contains(0) || map.remove(0) { return 2; }
        def mut empty: hashmap.Cursor<i64, i64> = map.entries();
        if empty.next() != null || empty.next() != null { return 3; }
        if code(map.reserve(-1)) != status.INVALID || code(map.reserve(9223372036854775807)) != status.OVERFLOW
            || code(map.reserve(2305843009213693952)) != status.OVERFLOW
            || map.cap() != 0 || code(map.reserve(0)) != status.OK { return 4; }
        def invalid: result<hashmap.HashMap<i64, i64>> = hashmap.HashMap<i64, i64>.create(&memory, -1);
        if !invalid.erroneous { return 4; }
        if invalid.error.code != status.INVALID { return 4; }
        def mut i: i64 = 0;
        while i < 2000 {
            if !inserted(map.insert(i, i * 3), true) { return 5; }
            i = i + 1;
        }
        if alias.len() != 2000 || map.cap() < 2000 { return 6; }
        i = 0;
        while i < 2000 {
            def value: *const i64 = alias.get(i);
            if value == null { return 7; }
            if *value != i * 3 { return 8; }
            if !inserted(alias.insert(i, i * 5), false) { return 9; }
            i = i + 1;
        }
        if map.len() != 2000 { return 10; }
        i = 0;
        while i < 2000 {
            if !alias.remove(i) || map.contains(i) || map.remove(i) { return 11; }
            i = i + 2;
        }
        if map.len() != 1000 { return 12; }
        i = 0;
        while i < 2000 {
            if !inserted(map.insert(i, i * 5), true) { return 13; }
            i = i + 2;
        }
        if code(alias.reserve(10000)) != status.OK || map.cap() < 10000 { return 14; }
        def mut cursor: hashmap.Cursor<i64, i64> = map.entries();
        def mut entry: *const hashmap.Entry<i64, i64> = cursor.next();
        def mut count: i64 = 0;
        def mut sum: i64 = 0;
        while entry != null {
            if entry.value != entry.key * 5 { return 15; }
            count = count + 1;
            sum = sum + entry.key;
            entry = cursor.next();
        }
        if count != 2000 || sum != 1999000 || cursor.next() != null { return 16; }
        def value: *i64 = alias.get_mut(10);
        if value == null { return 17; }
        *value = 42;
        def read: *const i64 = map.get(10);
        if read == null { return 18; }
        if *read != 42 { return 19; }
        def capacity: i64 = map.cap();
        alias.clear();
        if map.len() != 0 || map.cap() != capacity || map.get(10) != null { return 20; }
        if !inserted(map.insert(-1, 42), true) || !alias.contains(-1) { return 21; }
        return 0;
    )");
    auto ir = invoke({"--emit-ir", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    EXPECT_NE(ir.out.find("gloin.abi_call"), std::string::npos);
    EXPECT_NE(ir.out.find("gloin.arena_typed_pointer"), std::string::npos);
    execution_modes(file);
}

TEST_F(HashmapLibraryTest, CollidingKeysWrapProbeChainsAndReuseDeletedSlots) {
    std::vector<int64_t> keys;
    for (int64_t key = 0; keys.size() < 7; ++key)
        if (gloin_hashmap_word(key) % 8 == 7)
            keys.push_back(key);
    std::string body = R"(
        def made: result<hashmap.HashMap<i64, i32>> = hashmap.HashMap<i64, i32>.create(&memory, 6);
        if made.erroneous { return 1; }
        def mut map: hashmap.HashMap<i64, i32> = made.value;
    )";
    for (size_t i = 0; i < 5; ++i)
        body += "if !inserted(map.insert(" + std::to_string(keys[i]) + ", 42), true) { return 2; }";
    body += "if !map.remove(" + std::to_string(keys[0]) + ") { return 3; }";
    for (size_t i = 1; i < 5; ++i)
        body += "if !map.contains(" + std::to_string(keys[i]) + ") { return 4; }";
    body += "if !inserted(map.insert(" + std::to_string(keys[4]) +
            ", 9), false) || map.len() != 4 { return 5; }";
    body += "if !inserted(map.insert(" + std::to_string(keys[5]) +
            ", 9), true) || map.cap() != 6 { return 6; }";
    body += "if map.contains(" + std::to_string(keys[6]) +
            ") || map.len() != 5 { return 7; } return 0;";
    execution_modes(program(body));
}

TEST_F(HashmapLibraryTest, EveryIntegerWidthBooleanAndAliasesHaveExactKeyIdentity) {
    std::string body;
    const std::vector<std::tuple<std::string, std::string, std::string>> types{
        {"i8", "-128", "127"},
        {"i16", "-32768", "32767"},
        {"i32", "-2147483648", "2147483647"},
        {"i64", "-9223372036854775808", "9223372036854775807"},
        {"u8", "0", "255"},
        {"u16", "0", "65535"},
        {"u32", "0", "4294967295"},
        {"u64", "0", "18446744073709551615"},
        {"bool", "false", "true"},
        {"int", "-1", "1"},
        {"usize", "0", "18446744073709551615"}};
    for (const auto &[type, low, high] : types) {
        body += "{ def made: result<hashmap.HashMap<" + type + ", i32>> = hashmap.HashMap<" + type +
                ", i32>.create(&memory, 0);"
                "if made.erroneous { return 1; } def mut map: hashmap.HashMap<" +
                type +
                ", i32> = made.value;"
                "if !inserted(map.insert(" +
                low + ", 10), true) || !inserted(map.insert(" + high +
                ", 20), true) { return 2; }"
                "def a: *const i32 = map.get(" +
                low + "); def b: *const i32 = map.get(" + high +
                ");"
                "if a == null || b == null { return 3; } if *a != 10 || *b != 20 || map.len() != 2 "
                "{ return 4; } }";
    }
    execution_modes(program(body + "return 0;"));
}

TEST_F(HashmapLibraryTest, StringsCompareCountedBytesAndValuesNeedNoDefaultInitializer) {
    auto file = program(R"(
        def made: result<hashmap.HashMap<string, [i32; 2]>> = hashmap.HashMap<string, [i32; 2]>.create(&memory, 0);
        if made.erroneous { return 1; }
        def mut map: hashmap.HashMap<string, [i32; 2]> = made.value;
        if !inserted(map.insert("", {1, 2}), true)
            || !inserted(map.insert("a\0b", {3, 4}), true)
            || !inserted(map.insert("a", {5, 6}), true)
            || !inserted(map.insert("café", {7, 8}), true) { return 2; }
        def copied: result<string> = strings.copy(&memory, "a\0b");
        if copied.erroneous { return 3; }
        if !inserted(map.insert(copied.value, {9, 10}), false) || map.len() != 4 { return 4; }
        def read: *const [i32; 2] = map.get("a\0b");
        if read == null { return 5; }
        def pair: [i32; 2] = *read;
        if pair[0] != 9 || pair[1] != 10 || map.contains("a\0") { return 6; }
        def mut bytes: [u8; 3] = {255, 0, 128};
        def key: string = strings.view_bytes(bytes[0..3]);
        if !inserted(map.insert(key, {11, 12}), true) || !map.contains(key) { return 7; }
        def other: result<hashmap.HashMap<i32, *i32>> = hashmap.HashMap<i32, *i32>.create(&memory, 0);
        if other.erroneous { return 8; }
        def mut pointers: hashmap.HashMap<i32, *i32> = other.value;
        if !inserted(pointers.insert(1, null), true) { return 9; }
        def pointer: *const *i32 = pointers.get(1);
        if pointer == null || pointers.get(2) != null { return 10; }
        if *pointer != null { return 11; }
        return 0;
    )");
    execution_modes(file);
    auto ir = invoke({"--emit-llvm", file});
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
    EXPECT_EQ(*result, 0);
}

TEST_F(HashmapLibraryTest, AllocationFailuresLeaveCreationAndGrowthRecoverable) {
    for (unsigned failure = 1; failure <= 5; ++failure) {
        SCOPED_TRACE(failure);
        fail_allocation(failure);
        std::string body = R"(
            def made: result<hashmap.HashMap<i32, i32>> = hashmap.HashMap<i32, i32>.create(&memory, 6);
        )";
        if (failure <= 3) {
            body += R"(
                if !made.erroneous { return 1; }
                if made.error.code != status.NO_MEMORY || made.error.os_error != 0 { return 2; }
                def retry: result<hashmap.HashMap<i32, i32>> = hashmap.HashMap<i32, i32>.create(&memory, 6);
                if retry.erroneous { return 3; }
                def mut map: hashmap.HashMap<i32, i32> = retry.value;
                if !inserted(map.insert(7, 42), true) { return 4; }
                return 0;
            )";
            expect_run(invoke({"--stdlib-dir", directory, program(body)}), 0);
        } else {
            for (const bool after_removal : {false, true}) {
                auto mutation =
                    after_removal
                        ? "if !map.remove(0) || !inserted(map.insert(-1, 42), true) { return 4; }"
                        : "";
                auto file = program(body + R"(
                    if made.erroneous { return 1; }
                    def mut map: hashmap.HashMap<i32, i32> = made.value;
                    def mut i: i32 = 0;
                    while i < 6 {
                        if !inserted(map.insert(i, 42), true) { return 2; }
                        i = i + 1;
                    }
                )" + mutation + R"(
                    def length: i64 = map.len();
                    def failed: result<bool> = map.insert(99, 10);
                    if !failed.erroneous { return 5; }
                    if failed.error.code != status.NO_MEMORY || failed.error.os_error != 0
                        || map.len() != length || map.cap() != 6 || map.contains(99) { return 6; }
                    i = 1;
                    while i < 6 {
                        def value: *const i32 = map.get(i);
                        if value == null { return 7; }
                        if *value != 42 { return 8; }
                        i = i + 1;
                    }
                    if !inserted(map.insert(99, 10), true) || map.len() != length + 1 { return 9; }
                    return 0;
                )");
                execution_modes(file, 0, {"--stdlib-dir", directory});
            }
        }
    }
}

TEST_F(HashmapLibraryTest, ReplacingAndChurningDistinctKeysWithinCapacityNeverAllocate) {
    fail_allocation(4);
    auto file = program(R"(
        def made: result<hashmap.HashMap<i32, i32>> = hashmap.HashMap<i32, i32>.create(&memory, 6);
        if made.erroneous { return 1; }
        def mut map: hashmap.HashMap<i32, i32> = made.value;
        def mut i: i32 = 0;
        while i < 6 {
            if !inserted(map.insert(i, 0), true) { return 2; }
            i = i + 1;
        }
        i = 0;
        while i < 2000 {
            if !inserted(map.insert(i, i), false) || !map.remove(i)
                || map.remove(i) || !inserted(map.insert(i + 6, i + 6), true) { return 3; }
            def mut key: i32 = i + 1;
            while key <= i + 6 {
                def value: *const i32 = map.get(key);
                if value == null { return 4; }
                if key >= 6 && *value != key { return 5; }
                key = key + 1;
            }
            i = i + 1;
        }
        if map.len() != 6 || map.cap() != 6 { return 6; }
        // The next growth must encounter the still-pending fourth allocation.
        def growth: result<bool> = map.insert(10000, 42);
        if !growth.erroneous { return 7; }
        if growth.error.code != status.NO_MEMORY || map.len() != 6
            || map.cap() != 6 || map.contains(10000) { return 8; }
        return 0;
    )");
    execution_modes(file, 0, {"--stdlib-dir", directory});
}

TEST_F(HashmapLibraryTest, RemovalSkipsEntriesAtHomeAndRepairsWrappedProbeChains) {
    // A home-bucket entry between the hole and a displaced entry must not
    // terminate the scan. Exercise holes both before and after wraparound.
    for (int64_t home : {6, 7}) {
        std::vector<int64_t> displaced;
        for (int64_t key = 0; displaced.size() < 3; ++key)
            if (gloin_hashmap_word(key) % 8 == home)
                displaced.push_back(key);
        int64_t at_home = 0;
        while (gloin_hashmap_word(at_home) % 8 != (home + 2) % 8)
            ++at_home;
        std::string body = R"(
            def made: result<hashmap.HashMap<i64, i64>> = hashmap.HashMap<i64, i64>.create(&memory, 6);
            if made.erroneous { return 1; }
            def mut map: hashmap.HashMap<i64, i64> = made.value;
            def mut alias: hashmap.HashMap<i64, i64> = map;
        )";
        for (auto key : {displaced[0], displaced[1], at_home, displaced[2]})
            body += "if !inserted(map.insert(" + std::to_string(key) + ", " +
                    std::to_string(key + 10) + "), true) { return 2; }";
        for (auto removed : displaced) {
            body += "if !alias.remove(" + std::to_string(removed) + ") || map.contains(" +
                    std::to_string(removed) + ") { return 3; }";
            for (auto key : {displaced[0], displaced[1], at_home, displaced[2]}) {
                if (key == removed)
                    continue;
                body +=
                    "{ def value: *const i64 = map.get(" + std::to_string(key) +
                    "); if value == null { return 4; } if *value != " + std::to_string(key + 10) +
                    " { return 5; } }";
            }
            body += "if !inserted(alias.insert(" + std::to_string(removed) + ", " +
                    std::to_string(removed + 10) + "), true) || map.len() != 4 { return 6; }";
        }
        execution_modes(program(body + "return 0;"));
    }
}

TEST_F(HashmapLibraryTest, MixedMutationsMatchAnIndependentArrayModel) {
    execution_modes(program(R"(
        def made: result<hashmap.HashMap<i64, i64>> = hashmap.HashMap<i64, i64>.create(&memory, 0);
        if made.erroneous { return 1; }
        def mut map: hashmap.HashMap<i64, i64> = made.value;
        def mut alias: hashmap.HashMap<i64, i64> = map;
        def mut present: [bool; 128] = {false; 128};
        def mut values: [i64; 128] = {0; 128};
        def mut seed: i64 = 12345;
        def mut step: i64 = 0;
        while step < 12000 {
            seed = (seed * 1664525 + 1013904223) % 2147483648;
            def index: i64 = seed / 256 % 128;
            def key: i64 = index - 64;
            if seed % 3 == 0 {
                if alias.remove(key) != present[index] { return 2; }
                present[index] = false;
            } else {
                if !inserted(alias.insert(key, step), !present[index]) { return 3; }
                present[index] = true;
                values[index] = step;
            }
            if step % 997 == 0 {
                alias.clear();
                present = {false; 128};
            }
            if step % 233 == 0 {
                if code(map.reserve(step / 233 + 128)) != status.OK { return 4; }
            }
            def mut expected: i64 = 0;
            def mut i: i64 = 0;
            while i < 128 {
                def value: *const i64 = map.get(i - 64);
                if present[i] {
                    if value == null { return 5; }
                    if *value != values[i] { return 6; }
                    expected = expected + 1;
                } else {
                    if value != null { return 7; }
                }
                i = i + 1;
            }
            if map.len() != expected || alias.len() != expected { return 8; }
            if step % 64 == 0 {
                def mut cursor: hashmap.Cursor<i64, i64> = map.entries();
                def mut entry: *const hashmap.Entry<i64, i64> = cursor.next();
                def mut seen: [bool; 128] = {false; 128};
                def mut count: i64 = 0;
                while entry != null {
                    def index: i64 = entry.key + 64;
                    if index < 0 || index >= 128 { return 9; }
                    if seen[index] || !present[index] || entry.value != values[index] { return 10; }
                    seen[index] = true;
                    count = count + 1;
                    entry = cursor.next();
                }
                if count != expected { return 11; }
            }
            step = step + 1;
        }
        return 0;
    )"));
}

TEST_F(HashmapLibraryTest, InvalidKeysMutabilityPrivateStorageAndUncheckedResultsAreRejected) {
    for (const auto *key : {"f32", "f64", "[i32; 2]", "*i32"}) {
        auto file = program("def made: result<hashmap.HashMap<" + std::string(key) +
                            ", i32>> = "
                            "hashmap.HashMap<" +
                            key +
                            ", i32>.create(&memory, 0); "
                            "if made.erroneous { return 1; } return 0;");
        expect_error(invoke({"--check", file}), 1,
                     "HashMap keys must be integers, bool, or string");
    }
    for (const auto *body :
         {"map.insert(1, 2);", "map.reserve(10);", "map.state;", "map.insert(true, 2);",
          "map.get(\"1\");", "def frozen: hashmap.HashMap<i32, i32> = map; frozen.clear();",
          "def frozen: hashmap.HashMap<i32, i32> = map; frozen.get_mut(1);",
          "def value: *const i32 = map.get(1); if value != null { *value = 9; }",
          "def mut cursor: hashmap.Cursor<i32, i32> = map.entries(); "
          "def entry: *const hashmap.Entry<i32, i32> = cursor.next(); if entry != null { entry.key "
          "= 7; }",
          "__hashmap_hash(1);", "__hashmap_equal(1, 1);", "__hashmap_mask(1, 7);",
          "memory.try_reserve_typed<i32>(8);"}) {
        SCOPED_TRACE(body);
        auto file = program(R"(
            def made: result<hashmap.HashMap<i32, i32>> = hashmap.HashMap<i32, i32>.create(&memory, 0);
            if made.erroneous { return 1; }
            def mut map: hashmap.HashMap<i32, i32> = made.value;
        )" + std::string(body) +
                            "return 0;");
        expect_error(invoke({"--check", file}), 1, "error:");
    }
}

TEST_F(HashmapLibraryTest, PrimitivePrivacySignaturesReplacementAndRuntimeAbiAreChecked) {
    auto root = source("import \"@hashmap\"; def main() -> i32 { return hashmap.probe(); }");
    for (const auto *body :
         {"__hashmap_hash();", "__hashmap_hash(1, 2);", "__hashmap_equal(1, true);",
          "__hashmap_equal(1);", "__hashmap_mask(1);", "__hashmap_mask(1, true);",
          "__hashmap_mask(\"a\", 7);", "__hashmap_hash_string(1);",
          "__hashmap_string_equal(1, 1);"}) {
        source("def pub probe() -> i32 { " + std::string(body) + " return 0; }", "hashmap.gloin");
        expect_error(invoke({"--stdlib-dir", directory, "--check", root}), 1, "error:");
    }
    source("def pub probe() -> i32 { def hash: i64 = __hashmap_hash(\"\"); "
           "if __hashmap_equal(\"a\\0b\", \"a\\0b\") "
           "&& hash >= 0 { return 42; } return 1; }",
           "hashmap.gloin");
    expect_run(invoke({"--stdlib-dir", directory, root}), 42);
    root = source("import \"./hashmap\"; def main() -> i32 { return hashmap.probe(); }");
    expect_error(invoke({"--check", root}), 1, "error:");
    mlir::MLIRContext context;
    context.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
    for (const auto *decl :
         {"llvm.func @gloin_hashmap_word(i32) -> i64",
          "llvm.func @gloin_hashmap_string(!llvm.ptr) -> i64",
          "llvm.func @gloin_hashmap_string_equal(!llvm.ptr, i64, !llvm.ptr, i64) -> i1"}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string("module { ") + decl +
                " llvm.func @main() -> i32 { "
                "%v = llvm.mlir.constant(0 : i32) : i32 llvm.return %v : i32 } }",
            &context);
        ASSERT_TRUE(module);
        EXPECT_FALSE(JitRunner::run(*module).success());
    }
}

TEST_F(HashmapLibraryTest, BucketMaskMatchesRemainderAcrossPowerOfTwoCapacities) {
    source(R"(
        def pub probe(hash: i64, mask: i64) -> i64 {
            return __hashmap_mask(hash, mask);
        }
        def pub check() -> i32 {
            def mut buckets: i64 = 8;
            while buckets <= 1048576 {
                def mut key: i64 = -1024;
                while key <= 1024 {
                    def hash: i64 = __hashmap_hash(key);
                    if probe(hash, buckets - 1) != hash % buckets { return 1; }
                    key = key + 1;
                }
                if probe(9223372036854775807, buckets - 1) != buckets - 1 { return 2; }
                buckets = buckets * 2;
            }
            return 0;
        }
    )",
           "hashmap.gloin");
    auto file = source("import \"@hashmap\"; def main() -> i32 { return hashmap.check(); }");
    auto ir = invoke({"--stdlib-dir", directory, "--emit-llvm", file});
    ASSERT_EQ(ir.status, 0) << ir.err;
    EXPECT_NE(ir.out.find("llvm.and"), std::string::npos);
    execution_modes(file, 0, {"--stdlib-dir", directory});
}

TEST_F(HashmapLibraryTest, PackagedExampleExecutes) {
    auto example =
        std::filesystem::path(gloin_test::strings_example).parent_path() / "hashmap.gloin";
    if (const char *binary = std::getenv("GLOIN_TEST_CLI"))
        example =
            std::filesystem::path(binary).parent_path() / "../share/gloinc/examples/hashmap.gloin";
    expect_run(invoke({example.string()}), 0, "apple=2\n");
}
