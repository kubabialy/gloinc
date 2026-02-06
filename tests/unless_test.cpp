#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"

TEST(UnlessTest, BasicUnlessStatement) {
    std::string src = R"(
        unless (1 > 2) {
            return 1;
        }
    )";
    Lexer lexer(src);
    GloinParser parser(lexer);
    auto program = parser.parse_program();
    ASSERT_FALSE(program.empty());
}
