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
        for (const auto *name : {"vector", "arena", "memory", "status", "slices"})
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

TEST_F(VectorLibraryTest, ClonesOwnTheirBufferAndCountersButCopyElementsShallowly) {
    modes(program(R"(
        def mut destination: arena.GeneralArena = arena.GeneralArena.create();
        defer destination.free();
        def mut values: vector.Vector<i32> = vector.Vector<i32>.empty(&owner, 8);
        if !values.push(11) || !values.push(22) { return 1; }
        def frozen: vector.Vector<i32> = values;
        def copied: result<vector.Vector<i32>> = frozen.try_clone(&destination);
        if copied.erroneous { return 2; }
        def mut cloned: vector.Vector<i32> = copied.value;
        if cloned.len() != 2 || cloned.cap() != 2 || cloned.get(0) == values.get(0) { return 3; }
        *values.get_mut(0) = 99;
        *cloned.get_mut(1) = 33;
        if *cloned.get(0) != 11 || *values.get(1) != 22 { return 4; }
        cloned.clear();
        if values.len() != 2 || !cloned.push(44) { return 5; }
        def same: result<vector.Vector<i32>> = values.try_clone(&owner);
        if same.erroneous { return 6; }
        def same_owner: vector.Vector<i32> = same.value;
        if same_owner.get(0) == values.get(0) || *same_owner.get(0) != 99 { return 7; }
        owner.reset();
        if cloned.len() != 1 || *cloned.get(0) != 44 { return 8; }
        def mut referent: i32 = 7;
        def mut pointers: vector.Vector<*i32> = vector.Vector<*i32>.empty(&owner, 3);
        if !pointers.push(&referent) { return 9; }
        def pointer_copy: result<vector.Vector<*i32>> = pointers.try_clone(&destination);
        if pointer_copy.erroneous { return 10; }
        def pointer_clone: vector.Vector<*i32> = pointer_copy.value;
        if pointer_clone.get(0) == pointers.get(0) || *pointer_clone.get(0) != &referent { return 11; }
        owner.reset();
        referent = 42;
        if **pointer_clone.get(0) != 42 { return 12; }
        return 42;
    )"));
}

TEST_F(VectorLibraryTest, CloneFailureAndEmptyClonePreserveSourceAndAllowRetry) {
    fail_allocation(2);
    for (const bool empty : {false, true}) {
        modes(program(R"(
            def mut destination: arena.GeneralArena = arena.GeneralArena.create();
            defer destination.free();
            def marker: &i32 = destination.alloc(123);
            def mut values: vector.Vector<i32> = vector.Vector<i32>.empty(&owner, 8);
        )" + std::string(empty ? "" : "if !values.push(42) { return 1; }") +
                      R"(
            def length: i64 = values.len();
            def pointer: *const i32 = values.get(0);
            def failed: result<vector.Vector<i32>> = values.try_clone(&destination);
            if !failed.erroneous { return 2; }
            if failed.error.code != status.NO_MEMORY || failed.error.os_error != 0 { return 3; }
            if values.len() != length || values.cap() != 8 || values.get(0) != pointer || *marker != 123 { return 4; }
            def retry: result<vector.Vector<i32>> = values.try_clone(&destination);
            if retry.erroneous { return 5; }
            def mut cloned: vector.Vector<i32> = retry.value;
            if cloned.len() != length || cloned.cap() != length { return 6; }
            owner.reset();
            if length == 0 {
                if code(cloned.try_push(42)) != status.OK { return 7; }
            }
            if *cloned.get(0) != 42 { return 8; }
            return 42;
        )"),
              true);
    }
}

TEST_F(VectorLibraryTest, AppendPreservesSourcesPlacedBetweenElementBoundaries) {
    modes(source(R"(
        import "@arena"; import "@vector"; import "@memory";
        def struct Bytes { def mut bytes: [u8; 3], }
        def main() -> i32 {
            def mut owner: arena.GeneralArena = arena.GeneralArena.create();
            defer owner.free();
            def offsets: [i64; 4] = {1, 2, 7, 8};
            for def mut case_index: u64 = 0; case_index < 4; case_index = case_index + 1 {
                def mut values: vector.Vector<Bytes> = vector.Vector<Bytes>.empty(&owner, 6);
                for def mut index: i64 = 0; index < 6; index = index + 1 {
                    if !values.push(Bytes { bytes: {11, 22, 33} }) { return 1; }
                }
                def alias: vector.Vector<Bytes> = values;
                def base: *u8 = &values.get_mut(0).bytes[0];
                def offset: i64 = offsets[case_index];
                def seed: [Bytes; 3] = {
                    Bytes { bytes: {4, 5, 6} }, Bytes { bytes: {7, 8, 9} }, Bytes { bytes: {10, 11, 12} }
                };
                def placed: *[Bytes; 3] = memory.place<[Bytes; 3]>(base + offset, 18 - offset, seed);
                if placed == null { return 2; }
                def snapshot: [Bytes; 3] = *placed;
                def mut expected: [Bytes; 6] = {Bytes { bytes: {0, 0, 0} }; 6};
                for def mut index: i64 = 0; index < 6; index = index + 1 {
                    expected[index] = *alias.get(index);
                }
                for def mut index: u64 = 0; index < 3; index = index + 1 {
                    expected[2 + index] = snapshot[index];
                }
                if !values.truncate(2) { return 3; }
                def appended: result<void> = values.try_append_from((*placed)[..]);
                if appended.erroneous { return 4; }
                if values.len() != 5 || values.cap() != 6 || values.get(0) != alias.get(0) { return 5; }
                for def mut index: i64 = 0; index < 6; index = index + 1 {
                    for def mut byte_index: u64 = 0; byte_index < 3; byte_index = byte_index + 1 {
                        if alias.get(index).bytes[byte_index] != expected[index].bytes[byte_index] { return 6; }
                    }
                }
            }
            return 42;
        }
    )"));
}
