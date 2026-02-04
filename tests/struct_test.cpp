#include <gtest/gtest.h>
#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/AST.h"

TEST(ParserTest, ParseStructDefinition) {
    std::string input = R"(
        def struct Person {
            def pub name: string,
            def age: i32,
            
            pub def greet(self) -> void {
                return;
            }
        }
    )";
    
    Lexer lexer(input);
    GloinParser parser(lexer);
    
    auto stmt = parser.parse_statement();
    ASSERT_NE(stmt, nullptr);
    
    auto* structDef = dynamic_cast<StructDefinition*>(stmt.get());
    ASSERT_NE(structDef, nullptr);
    EXPECT_EQ(structDef->name->value, "Person");
    EXPECT_EQ(structDef->fields.size(), 2);
    EXPECT_EQ(structDef->methods.size(), 1);
    
    EXPECT_EQ(structDef->fields[0].name->value, "name");
    EXPECT_TRUE(structDef->fields[0].is_public);
    EXPECT_EQ(structDef->fields[1].name->value, "age");
    EXPECT_FALSE(structDef->fields[1].is_public);
    
    EXPECT_EQ(structDef->methods[0]->name->value, "greet");
}

TEST(ParserTest, ParsePackedStruct) {
    std::string input = R"(
        def packed struct Header {
            def version: u4,
            def type: u4
        }
    )";
    
    Lexer lexer(input);
    GloinParser parser(lexer);
    
    auto stmt = parser.parse_statement();
    auto* structDef = dynamic_cast<StructDefinition*>(stmt.get());
    
    ASSERT_NE(structDef, nullptr);
    EXPECT_TRUE(structDef->is_packed);
    EXPECT_EQ(structDef->fields.size(), 2);
}
