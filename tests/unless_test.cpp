#include "lexer.h"
#include "parser.h"
#include <gtest/gtest.h>

TEST(UnlessTest, BasicUnlessStatement) {
    std::string src = R"(
        unless (1 > 2) {
            return 1;
        }
    )";
    Lexer lexer(src);
    GloinParser parser(lexer);
    auto statement = parser.parse_statement();
    ASSERT_NE(statement, nullptr);
    ASSERT_FALSE(parser.has_error());
    EXPECT_TRUE(parser.at_end());
    auto *unless = dynamic_cast<UnlessStatement *>(statement.get());
    ASSERT_NE(unless, nullptr);
    EXPECT_EQ(unless->condition->to_string(), "(1 > 2)");
    ASSERT_EQ(unless->consequence->statements.size(), 1u);
    EXPECT_EQ(unless->consequence->statements[0]->to_string(), "return 1;");
}
