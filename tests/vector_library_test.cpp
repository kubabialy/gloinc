#include "support/cli_fixture.h"
#include <filesystem>

namespace {
class VectorLibraryTest : public gloin_test::CliFixture {
  protected:
    std::filesystem::path library() {
        const char *binary = std::getenv("GLOIN_TEST_CLI");
        auto root = std::filesystem::path(binary ? binary : gloin_test::gloinc).parent_path();
        return std::filesystem::exists(root / "stdlib") ? root / "stdlib"
                                                        : root / "../share/gloinc/stdlib";
    }
    std::string program(const std::string &body) {
        return source(R"(
            import "@arena"; import "@vector"; import "@status"; import "@memory";
            def code(outcome: result<void>) -> i32 {
                if outcome.erroneous { return outcome.error.code; }
                return status.OK;
            }
            def marked(count: &i32) -> i32 { *count = *count + 1; return 33; }
            def packed struct(msb_first) Record { def value: u3, }
            def struct Empty {}
            def main() -> i32 {
                def mut owner: arena.GeneralArena = arena.GeneralArena.create();
                defer owner.free();
        )" + body + "}");
    }
    void modes(const std::string &file, bool replacement = false) {
        std::vector<std::string> options;
        if (replacement)
            options = {"--stdlib-dir", directory};
        auto arguments = options;
        arguments.push_back(file);
        expect_run(invoke(arguments), 42);
        for (const auto *mode : {"-O0", "-O2"}) {
            const auto executable = directory + "/vector";
            arguments = options;
            arguments.insert(arguments.end(), {mode, "-o", executable, file});
            const auto built = invoke_raw(arguments);
            ASSERT_EQ(built.status, 0) << built.err;
            EXPECT_EQ(llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10),
                      42);
        }
    }
    void fail_allocation(unsigned number) {
        for (const auto *name : {"vector", "arena", "memory", "status"})
            source(read((library() / (std::string(name) + ".gloin")).string()),
                   std::string(name) + ".gloin");
        auto arena = read((library() / "arena.gloin").string());
        const auto replace = [&](const std::string &from, const std::string &to) {
            size_t pos = 0, count = 0;
            while ((pos = arena.find(from, pos)) != std::string::npos) {
                arena.replace(pos, from.size(), to);
                pos += to.size();
                ++count;
            }
            EXPECT_GT(count, 0u) << from;
        };
        replace("def mut state: *u8,", "def mut state: *u8, def mut requests: u64,");
        replace("state: state\n", "state: state, requests: 0\n");
        replace("__arena_general_alloc(self.state, size, alignment)",
                "self.test_allocate(size, alignment)");
        const auto end = arena.rfind('}');
        ASSERT_NE(end, std::string::npos);
        arena.insert(end,
                     "def test_allocate(self: &GeneralArena, size: u64, alignment: u64) -> *u8 { "
                     "self.requests = self.requests + 1; if self.requests == " +
                         std::to_string(number) +
                         " { return null; } "
                         "return __arena_general_alloc(self.state, size, alignment); }");
        source(arena, "arena.gloin");
    }
};
} // namespace

TEST_F(VectorLibraryTest, FallibleGrowthPreservesValuesAndOverlappingAppendSnapshots) {
    modes(program(R"(
        def made: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 0);
        if made.erroneous { return 1; }
        def mut values: vector.Vector<i32> = made.value;
        if values.len() != 0 || values.cap() != 0 { return 2; }
        if code(values.try_push(1)) != status.OK || code(values.try_push(3)) != status.OK
            || code(values.try_insert(1, 2)) != status.OK { return 3; }
        if code(values.try_append_from(values.as_const_slice())) != status.OK { return 4; }
        def view: [const i32] = values.as_const_slice();
        for def mut i: u64 = 0; i < 6; i = i + 1 {
            if view[i] != view[i % 3] { return 5; }
        }
        if view[0] != 1 || view[1] != 2 || view[2] != 3 { return 6; }
        // Separate handles have independent lengths but share the buffer.
        // Append overlaps both ways without growth; compare independent values.
        if code(values.try_reserve(12)) != status.OK { return 7; }
        def mut shorter: vector.Vector<i32> = values;
        if !shorter.truncate(1) { return 8; }
        if code(shorter.try_append_from(values.as_const_slice()[..3])) != status.OK { return 9; }
        def backward: [const i32] = shorter.as_const_slice();
        if backward[0] != 1 || backward[1] != 1 || backward[2] != 2 || backward[3] != 3 { return 10; }
        def mut empty: vector.Vector<i32> = values;
        empty.clear();
        if code(empty.try_append_from(shorter.as_const_slice()[1..4])) != status.OK { return 11; }
        def forward: [const i32] = empty.as_const_slice();
        if forward[0] != 1 || forward[1] != 2 || forward[2] != 3 { return 12; }
        def raw: [u8; 1] = {255};
        def decoded: result<Record> = memory.read_packed<Record>(raw[..]);
        if decoded.erroneous { return 13; }
        def records: result<vector.Vector<Record>> = vector.Vector<Record>.try_empty(&owner, 0);
        if records.erroneous { return 14; }
        def mut packets: vector.Vector<Record> = records.value;
        if code(packets.try_push(decoded.value)) != status.OK
            || code(packets.try_insert(0, decoded.value)) != status.OK { return 15; }
        def mut output: [u8; 1] = {0};
        if code(memory.write_packed<Record>(output[..], *packets.get(1))) != status.OK
            || output[0] != 255 { return 16; }
        def zero: result<vector.Vector<Empty>> = vector.Vector<Empty>.try_empty(&owner, 9223372036854775807);
        if zero.erroneous { return 17; }
        def mut zero_values: vector.Vector<Empty> = zero.value;
        if code(zero_values.try_push(Empty {})) != status.OK || zero_values.len() != 1 { return 18; }
        def huge_source: result<[Empty]> = owner.alloc_slice<[Empty]>(18446744073709551615, Empty {}, 1);
        if huge_source.erroneous { return 19; }
        def source_view: [Empty] = huge_source.value;
        if code(zero_values.try_append_from(source_view)) != status.OVERFLOW { return 20; }
        if code(zero_values.try_append_from(source_view[..9223372036854775807])) != status.OVERFLOW
            || zero_values.len() != 1 { return 21; }
        return 42;
    )"));
}

TEST_F(VectorLibraryTest, AllocationFailuresPreserveStorageAliasesAndRetryWithCapturedValues) {
    fail_allocation(1);
    modes(program(R"(
        def made: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 0);
        if !made.erroneous { return 1; }
        if made.error.code != status.NO_MEMORY || made.error.os_error != 0 { return 2; }
        def retry: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 2);
        if retry.erroneous { return 3; }
        return 42;
    )"),
          true);
    fail_allocation(2);
    for (const auto *operation : {"values.try_reserve(4)", "values.try_push(marked(&effects))",
                                  "values.try_insert(0, marked(&effects))",
                                  "values.try_append_from(values.as_const_slice())"}) {
        SCOPED_TRACE(operation);
        modes(program(R"(
            def made: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 2);
            if made.erroneous { return 1; }
            def mut values: vector.Vector<i32> = made.value;
            if !values.push(11) || !values.push(22) { return 2; }
            def alias: vector.Vector<i32> = values;
            def view: [const i32] = values.as_const_slice();
            def pointer: *const i32 = values.get(0);
            def mut effects: i32 = 0;
            def failed: result<void> = )" +
                      std::string(operation) + R"(;
            if !failed.erroneous { return 3; }
            if failed.error.code != status.NO_MEMORY || failed.error.os_error != 0 { return 4; }
            if values.len() != 2 || values.cap() != 2 || values.get(0) != pointer
                || alias.len() != 2 || alias.cap() != 2 || view[0] != 11 || view[1] != 22 { return 5; }
        )" +
                      (std::string(operation).find("marked") != std::string::npos
                           ? "if effects != 1 { return 6; }"
                           : "if effects != 0 { return 6; }") +
                      "if code(" + operation + ") != status.OK { return 7; } return 42;"),
              true);
    }
}

TEST_F(VectorLibraryTest, InvalidRequestsAndNoopCapacityNeverConsumeAnAllocation) {
    fail_allocation(2);
    modes(program(R"(
        def invalid: result<vector.Vector<i64>> = vector.Vector<i64>.try_empty(&owner, -1);
        if !invalid.erroneous { return 1; }
        if invalid.error.code != status.INVALID { return 2; }
        def huge: result<vector.Vector<i64>> = vector.Vector<i64>.try_empty(&owner, 1152921504606846976);
        if !huge.erroneous { return 3; }
        if huge.error.code != status.OVERFLOW { return 4; }
        def made: result<vector.Vector<i64>> = vector.Vector<i64>.try_empty(&owner, 2);
        if made.erroneous { return 5; }
        def mut values: vector.Vector<i64> = made.value;
        if code(values.try_reserve(-1)) != status.INVALID
            || code(values.try_reserve(1152921504606846976)) != status.OVERFLOW
            || code(values.try_insert(-1, 0)) != status.OUT_OF_RANGE
            || code(values.try_insert(1, 0)) != status.OUT_OF_RANGE { return 6; }
        if code(values.try_reserve(0)) != status.OK || code(values.try_reserve(2)) != status.OK
            || code(values.try_append_from(values.as_const_slice())) != status.OK
            || code(values.try_push(42)) != status.OK { return 7; }
        if code(values.try_reserve(1152921504606846975)) != status.NO_MEMORY { return 8; }
        if values.len() != 1 || values.cap() != 2 || *values.get(0) != 42 { return 9; }
        return 42;
    )"),
          true);
}

TEST_F(VectorLibraryTest, FallibleResultsAndPrivateReservationRemainChecked) {
    for (const auto *body :
         {"vector.Vector<i32>.try_empty(&owner, 0);",
          "def made: result<vector.Vector<i32>> = vector.Vector<i32>.try_empty(&owner, 0); "
          "made.value;",
          "def mut values: vector.Vector<i32> = vector.Vector<i32>.empty(&owner, 0); "
          "values.try_push(1);",
          "def values: vector.Vector<i32> = vector.Vector<i32>.empty(&owner, 0); "
          "code(values.try_reserve(1));",
          "vector.try_storage<i32>(&owner, 1);", "owner.try_reserve_typed<i32>(1);"}) {
        SCOPED_TRACE(body);
        expect_error(invoke({"--check", program(std::string(body) + "return 42;")}), 1, "error:");
    }
}
