#include "support/cli_fixture.h"
#include "llvm/Support/Program.h"

namespace {
class EnumTest : public gloin_test::CliFixture {};
}

TEST_F(EnumTest, ValuesCopyThroughFunctionsStructsAndArrays) {
    auto file = source(R"(
        def enum Direction { North, East, South, West, }
        def struct Compass { def mut direction: Direction, }
        def turn(direction: Direction) -> Direction {
            if direction == Direction.North { return Direction.East; }
            if direction == Direction.East { return Direction.South; }
            return Direction.West;
        }
        def main() -> i32 {
            def mut compass: Compass = Compass { direction: Direction.North };
            compass.direction = turn(compass.direction);
            def path: [Direction; 2] = {compass.direction, turn(compass.direction)};
            if path[0] == Direction.East && path[1] != Direction.East { return 42; }
            return 1;
        }
    )");
    expect_success(invoke({"--check", file}), "");
    auto high_level = invoke({"--emit-ir", file});
    EXPECT_EQ(high_level.status, 0) << high_level.err;
    EXPECT_NE(high_level.out.find("gloin.enum_constant"), std::string::npos);
    EXPECT_NE(high_level.out.find("gloin.enum_compare"), std::string::npos);
    EXPECT_NE(high_level.out.find("!gloin.enum"), std::string::npos);
    EXPECT_NE(high_level.out.find("gloin.struct_literal"), std::string::npos);
    EXPECT_NE(high_level.out.find("gloin.array_literal"), std::string::npos);
    auto lowered = invoke({"--emit-llvm", file});
    EXPECT_EQ(lowered.status, 0) << lowered.err;
    EXPECT_EQ(lowered.out.find("gloin.enum_"), std::string::npos);
    expect_run(invoke({"--jit", file}), 42);
    auto executable = directory + "/enum-native";
    expect_success(invoke_raw({"-O2", "-o", executable, file}), "");
    std::string message;
    bool launch_failed = false;
    int status = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, {}, 10, 0,
                                           &message, &launch_failed);
    EXPECT_FALSE(launch_failed) << message;
    EXPECT_EQ(status, 42) << message;
}

TEST_F(EnumTest, PublicModuleTypesAndPrivateVariants) {
    source("def pub enum Status { Ready, Busy } def enum Hidden { Secret } "
           "def pub ready() -> Status { return Status.Ready; }", "states.gloin");
    auto file = source(R"(
        import "./states";
        def main() -> i32 {
            def first: states.Status = states.ready();
            if first == states.Status.Ready && first != states.Status.Busy { return 42; }
            return 1;
        }
    )", "main.gloin");
    expect_success(invoke({"--check", file}), "");
    expect_run(invoke({"--jit", file}), 42);
    file = source("import \"./states\"; def main() -> i32 { "
                  "if states.Hidden.Secret == states.Hidden.Secret { return 1; } return 0; }",
                  "private.gloin");
    expect_error(invoke({"--check", file}), 1, "Private enum type");
    file = source("import \"./states\"; def main() -> i32 { "
                  "def secret: states.Hidden; return 0; }", "private-type.gloin");
    expect_error(invoke({"--check", file}), 1, "Unknown type 'states.Hidden'");
}

TEST_F(EnumTest, InvalidConstructionAndOperationsAreRejected) {
    for (const auto &[source_text, error] : {
             std::pair{"def enum E {}", "at least one variant"},
             std::pair{"def enum E { A, A }", "Duplicate enum variant"},
             std::pair{"def enum E { A(i32) }", "Expected ',' after enum variant"},
             std::pair{"def enum E { A } def main() -> i32 { def e: E = E.Missing; return 0; }",
                       "Unknown variant"},
             std::pair{"def enum E { A } def main() -> i32 { def e: E = E {}; return 0; }",
                       "constructed with Type.Variant"},
             std::pair{"def enum E { A } def main() -> i32 { def e: E = E.A; e.__tag; return 0; }",
                       "no directly accessible fields"},
             std::pair{"def enum E { A } def main() -> i32 { if E.A < E.A { return 1; } return 0; }",
                       "Only equality comparisons"},
             std::pair{"def enum E { A } def enum F { A } "
                       "def main() -> i32 { if E.A == F.A { return 1; } return 0; }",
                       "Type mismatch in binary expression"}}) {
        SCOPED_TRACE(source_text);
        auto file = source(source_text);
        expect_error(invoke({"--check", file}), 1, error);
    }
}
