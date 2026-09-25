#include "../src/parser.h"
#include <gtest/gtest.h>

TEST(ParserTest, IntegerLiteral) {
    Lexer l("123");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "123");
}

TEST(ParserTest, FloatLiteral) {
    Lexer l("123.456");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "123.456");
}

TEST(ParserTest, Identifier) {
    Lexer l("myVar");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "myVar");
}

TEST(ParserTest, PrefixExpressionMinus) {
    Lexer l("-5");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(-5)");
}

TEST(ParserTest, PrefixExpressionBang) {
    Lexer l("!true");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(!true)");
}

TEST(ParserTest, InfixExpression) {
    Lexer l("1 + 2");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(1 + 2)");
}

TEST(ParserTest, OperatorPrecedence) {
    Lexer l("1 + 2 * 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be (1 + (2 * 3)) because * has higher precedence
    EXPECT_EQ(expr->to_string(), "(1 + (2 * 3))");
}

TEST(ParserTest, OperatorPrecedence2) {
    Lexer l("1 * 2 + 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be ((1 * 2) + 3)
    EXPECT_EQ(expr->to_string(), "((1 * 2) + 3)");
}

TEST(ParserTest, GroupedExpression) {
    Lexer l("(1 + 2) * 3");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // Should be ((1 + 2) * 3)
    EXPECT_EQ(expr->to_string(), "((1 + 2) * 3)");
}

TEST(ParserTest, FunctionCall) {
    Lexer l("add(1, 2 * 3)");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "add(1, (2 * 3))");
}

TEST(ParserTest, FunctionCallNoArgs) {
    Lexer l("foo()");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "foo()");
}

TEST(ParserTest, MemberAccess) {
    Lexer l("person.name");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(person.name)");
}

TEST(ParserTest, ComplexMemberAccess) {
    Lexer l("a.b.c");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // ((a.b).c) - left associative
    EXPECT_EQ(expr->to_string(), "((a.b).c)");
}

TEST(ParserTest, IndexExpression) {
    Lexer l("arr[1]");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(arr[1])");
}

TEST(ParserTest, IndexExpressionComplex) {
    Lexer l("arr[i + 1]");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(arr[(i + 1)])");
}

TEST(ParserTest, ChainedCallsAndAccess) {
    Lexer l("obj.method(1).field");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    // ((obj.method)(1)).field -> ((obj.method)(1).field)
    EXPECT_EQ(expr->to_string(), "((obj.method)(1).field)");
}

TEST(ParserTest, BooleanLiteral) {
    Lexer l("true != false");
    GloinParser p(l);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "(true != false)");
}

TEST(ParserTest, StringLiteral) {
    Lexer l("\"hello\"");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto expr = p.parse_expression(0);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->to_string(), "\"hello\"");
}

TEST(ParserTest, VariableDeclaration) {
    Lexer l("def x: i32 = 5;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def x: i32 = 5;");
}

TEST(ParserTest, MutableVariableDeclaration) {
    Lexer l("def mut y: i32 = 10;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "def mut y: i32 = 10;");
}

TEST(ParserTest, ReturnStatement) {
    Lexer l("return 0;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "return 0;");
}

TEST(ParserTest, ReturnVoidStatement) {
    Lexer l("return;");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "return;");
}

TEST(ParserTest, BlockStatement) {
    Lexer l("{ def x: i32 = 5; return x; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "{ def x: i32 = 5; return x; }");
}

TEST(ParserTest, IfStatement) {
    Lexer l("if x > 5 { return true; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "if (x > 5) { return true; }");
}

TEST(ParserTest, IfElseStatement) {
    Lexer l("if x > 5 { return true; } else { return false; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "if (x > 5) { return true; } else { return false; }");
}

TEST(ParserTest, WhileStatement) {
    Lexer l("while i < 10 { i = i + 1; }");
    GloinParser p(l);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "while (i < 10) { (i = (i + 1)); }");
}

TEST(ParserTest, DeferStatement) {
    Lexer l("defer x.free();");
    GloinParser p(l, ParseMode::SyntaxOnly);
    auto stmt = p.parse_statement();
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->to_string(), "defer (x.free)();");
}

namespace {
ParseResult checked(const std::string &source, ParseMode mode = ParseMode::Core) {
    GloinParser parser(Lexer(source, "parser.gloin"), mode);
    auto result = parser.parse_checked_program();
    if (!result.success) {
        std::ostringstream errors;
        parser.diagnostics()->render(errors);
        ADD_FAILURE() << errors.str() << source;
    }
    return result;
}

void rejected(const std::string &source, ParseMode mode = ParseMode::Core) {
    SCOPED_TRACE(source);
    GloinParser parser(Lexer(source, "invalid.gloin"), mode);
    auto result = parser.parse_checked_program();
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.program.empty());
    ASSERT_TRUE(parser.has_error());
    for (const auto &diagnostic : parser.diagnostics()->all()) {
        EXPECT_EQ(diagnostic.stage, DiagnosticStage::Parsing);
        ASSERT_NE(diagnostic.span.source, nullptr);
        EXPECT_LE(diagnostic.span.begin, diagnostic.span.end);
        EXPECT_LE(diagnostic.span.end, source.size());
    }
    // An error is terminal: repeated entry cannot return a later partial program.
    EXPECT_FALSE(parser.parse_checked_program().success);
    EXPECT_EQ(parser.parse_statement(), nullptr);
}
} // namespace

TEST(ParserTest, CanonicalCorePrograms) {
    for (const std::string source : {"def main() -> i32 { return 42; }",
                                     R"(def const OFFSET: int = 2;
           def main() -> i32 { def input: i32 = 40; return add(input, OFFSET); }
           def add(left: i32, right: i32) -> i32 { return left + right; })",
                                     R"(def main() -> i32 {
            def mut count: i32 = 0;
            def enabled: bool = true;
            if enabled {
                for def mut i: i32 = 0; i < 3; i = i + 1 { count = count + 1; }
            } else { return -1; }
            unless count == 3 { return -1; }
            while count > 3 { count = count - 1; }
            return count;
        })"}) {
        auto result = checked(source);
        EXPECT_TRUE(result.success);
    }
}

TEST(ParserTest, IdentifierConditionsAlwaysIntroduceBodies) {
    for (const std::string expression :
         {"ready", "Ready", "(ready)", "ready && other", "check(ready)", "!check(ready)"}) {
        for (const std::string keyword : {"if", "unless", "while"}) {
            auto source = "def main(ready: bool, other: bool) -> void { " + keyword + " " +
                          expression + " { work(); } }";
            for (auto mode : {ParseMode::Core, ParseMode::SyntaxOnly}) {
                auto result = checked(source, mode);
                ASSERT_TRUE(result.success);
                auto *fn = dynamic_cast<FunctionDefinition *>(result.program[0].get());
                ASSERT_NE(fn, nullptr);
                EXPECT_EQ(fn->body->statements.size(), 1u);
            }
        }
    }
}

TEST(ParserTest, CompleteOperatorPrecedenceAndAssociativity) {
    for (const auto &[source, expected] : std::vector<std::pair<std::string, std::string>>{
             {"-f(1) * 2", "((-f(1)) * 2)"},
             {"!f() || a && b", "((!f()) || (a && b))"},
             {"A + B * C", "(A + (B * C))"},
             {"A < B == C >= D", "((A < B) == (C >= D))"},
             {"a <= b != c > d", "((a <= b) != (c > d))"},
             {"a - b - c", "((a - b) - c)"},
             {"a / b % c * d", "(((a / b) % c) * d)"},
             {"f(1)(2) + 3", "(f(1)(2) + 3)"}}) {
        GloinParser parser{Lexer(source)};
        auto expression = parser.parse_expression(0);
        ASSERT_NE(expression, nullptr) << source;
        EXPECT_EQ(expression->to_string(), expected);
        EXPECT_TRUE(parser.at_end());
        EXPECT_FALSE(parser.has_error());
    }
}

TEST(ParserTest, NewlinesAreTriviaAtEveryTokenBoundary) {
    std::string source = "def main(a: i32, b: i32,) -> i32 { def mut x: i32 = f(a, b,); "
                         "if x <= 2 { x = x + 1; } else if x >= 3 { return -f(x); } return x; }";
    auto expected = checked(source);
    ASSERT_TRUE(expected.success);
    for (const std::string trivia : {"\n", "\r\n", " // comment\r\n"}) {
        std::string multiline;
        for (auto token : Lexer(source).tokenize()) {
            if (token.type != GLOIN_TOKEN_EOF)
                multiline += std::string(token.literal) + trivia;
        }
        auto result = checked(multiline);
        ASSERT_TRUE(result.success);
        EXPECT_EQ(result.program[0]->to_string(), expected.program[0]->to_string());
    }
}

TEST(ParserTest, StrictParameterAndArgumentLists) {
    for (const std::string source :
         {"def f(,x: i32) -> void {}", "def f(x: i32,, y: i32) -> void {}",
          "def f(x: i32 y: i32) -> void {}", "def f(x: i32,", "def f(x: i32; y: i32) -> void {}",
          "def f(x) -> void {}", "def f(self) -> void {}", "def f(mut x: i32) -> void {}",
          "def f(pub x: i32) -> void {}"})
        rejected(source);
    for (const std::string expression : {"f(,1)", "f(1,,2)", "f(1 2)", "f(1;2)", "f(1", "f() 2"})
        rejected("def main() -> void { " + expression + "; }");
    EXPECT_TRUE(checked("def f(a: i32,) -> void { f(1,); f(); }").success);
}

TEST(ParserTest, RejectsMissingAnnotationsAndConflictingModifiers) {
    for (const std::string source :
         {"const X: i32 = 1;", "pub def f() -> void {}", "def pub priv f() -> void {}",
          "def const mut X: i32 = 1;", "def mut const X: i32 = 1;", "def const X: i32;",
          "def const X = 1;", "def f() {}", "def f() -> {}", "def mut f() -> void {}",
          "def const f() -> void {}", "def const pub X: i32 = 1;", "def pub x: i32 = 1;",
          "def main() -> void { def x = 1; }", "def main() -> void { def x; }",
          "def main() -> void { def pub x: i32 = 1; }",
          "def main() -> void { def priv const X: i32 = 1; }"})
        rejected(source);
}

TEST(ParserTest, PreservesConstantAndVisibilityMetadata) {
    auto result = checked("def pub const X: i32 = 1; def priv const Y: i32 = 2; "
                          "def pub f() -> void { def const Z: i32 = 3; } def priv g() -> void {}");
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.program.size(), 4u);
    auto *x = dynamic_cast<VariableDeclaration *>(result.program[0].get());
    auto *y = dynamic_cast<VariableDeclaration *>(result.program[1].get());
    auto *f = dynamic_cast<FunctionDefinition *>(result.program[2].get());
    auto *g = dynamic_cast<FunctionDefinition *>(result.program[3].get());
    ASSERT_TRUE(x && y && f && g);
    EXPECT_TRUE(x->is_const && x->is_public);
    EXPECT_TRUE(y->is_const && !y->is_public);
    EXPECT_FALSE(x->is_mutable);
    EXPECT_TRUE(f->is_public);
    EXPECT_FALSE(g->is_public);
    auto *z = dynamic_cast<VariableDeclaration *>(f->body->statements[0].get());
    ASSERT_NE(z, nullptr);
    EXPECT_TRUE(z->is_const);
    EXPECT_FALSE(z->is_public);
    EXPECT_EQ(x->to_string(), "def pub const X: i32 = 1;");
}

TEST(ParserTest, RejectsWrongDeclarationScopes) {
    for (const std::string source :
         {"def global: i32 = 1;", "def mut global: i32;", "return 0;", "work();", "{}",
          "if true {}", "def main() -> void { def nested() -> void {} }"})
        rejected(source);
}

TEST(ParserTest, RequiresEveryStatementTerminator) {
    for (const std::string body : {"def x: i32 = 1", "def x: i32", "return", "return 1", "f()",
                                   "x = 1", ";", "if true {};", "while true {};", "{};"})
        rejected("def main() -> void { " + body + " }");
    rejected("def main() -> void {}; ");
    rejected("def const X: i32 = 1");
    rejected("def main() -> void { def x: i32 = 1\ndef y: i32 = 2; }");
}

TEST(ParserTest, AssignmentsAreOnlyStatementsOrLoopUpdates) {
    for (const std::string body :
         {"a = b = 1;", "return a = 1;", "f(a = 1);", "def x: i32 = a = 1;", "if a = true {}",
          "while (a = true) {}", "(a = 1);", "a + (b = 1);", "1 = a;", "a + b = 1;", "f() = 1;"})
        rejected("def main() -> void { " + body + " }");
    auto result = checked("def main() -> void { def mut a: i32 = 0; a = f(); "
                          "for def mut i: i32 = 0; i < 3; i = i + 1 { a = a + i; } }");
    ASSERT_TRUE(result.success);
    auto *fn = dynamic_cast<FunctionDefinition *>(result.program[0].get());
    auto *assignment = dynamic_cast<ExpressionStatement *>(fn->body->statements[1].get());
    ASSERT_NE(assignment, nullptr);
    EXPECT_NE(dynamic_cast<AssignmentExpression *>(assignment->expression.get()), nullptr);
    auto *loop = dynamic_cast<ForStatement *>(fn->body->statements[2].get());
    ASSERT_NE(loop, nullptr);
    EXPECT_NE(dynamic_cast<AssignmentExpression *>(loop->increment.get()), nullptr);
}

TEST(ParserTest, RequiresForSeparatorsAndBracedBodies) {
    for (const std::string body :
         {"for def mut i: i32 = 0; i < 3 i = i + 1 {}",
          "for def mut i: i32 = 0 i < 3; i = i + 1 {}",
          "for def mut i: i32 = 0; i < 3; i = i + 1; {}", "for if true {} true; f() {}",
          "if true return;", "while true return;", "unless true return;",
          "if true {} else return;"})
        rejected("def main() -> void { " + body + " }");
}

TEST(ParserTest, RejectsTruncatedConstructsAndTrailingTokens) {
    for (const std::string source :
         {"def", "def f(", "def f() -> i32 {", "def f() -> i32 { return",
          "def f() -> i32 { return (1 + 2; }", "def f() -> i32 { return f(1, 2; }",
          "def f() -> i32 { return 1 +; }", "def f() -> i32 { return !; }",
          "def f() -> i32 { return (); }", "def f() -> i32 { return 1 2; }",
          "def f() -> void { if true { }", "def f() -> void { if true {} else }",
          "def f() -> void {} garbage", "def f() -> void {} }"})
        rejected(source);
}

TEST(ParserTest, RejectsUnsupportedCoreSyntax) {
    for (const std::string source :
         {"fn f() {}", "extern def f() -> void;", "struct X {}", "def packed struct(u32) X {}",
          "def deferred f() -> void {}", "def spawnable f() -> void {}",
          "def f<T>(x: T) -> T { return x; }"})
        rejected(source);
    for (const std::string expression :
         {"spawn f()", "await f()", "run f()", "[1, 2]", "X<i32> { x: 1 }",
          "+a",        "~a",
          "a & b",     "a | b",     "a ^ b",   "a << b", "a >> b",    "a += 1",
          "a ? b",     "0..3",      "a => b"})
        rejected("def main() -> void { " + expression + "; }");
    for (const std::string statement :
         {"break;", "continue;", "switch x {}", "match x {}", "for x in 0..3 {}"})
        rejected("def main() -> void { " + statement + " }");
}

TEST(ParserTest, MultilineDeferredStructSyntaxUsesCanonicalMembers) {
    auto result = checked(R"(
        def pub struct box<T,> {
            def pub mut value: T,
            def priv other: i32,
            def pub get(self: *box<T>,) -> T { return self.value; }
        }
        def main() -> i32 {
            def x: box<i32> = box<i32> { value: f(1,), other: 2 + 3, };
            return x.other;
        }
    )",
                          ParseMode::SyntaxOnly);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.program.size(), 2u);
    auto *definition = dynamic_cast<StructDefinition *>(result.program[0].get());
    ASSERT_NE(definition, nullptr);
    EXPECT_TRUE(definition->is_public);
    ASSERT_EQ(definition->fields.size(), 2u);
    EXPECT_TRUE(definition->fields[0].is_public && definition->fields[0].is_mutable);
    ASSERT_EQ(definition->methods.size(), 1u);
    EXPECT_TRUE(definition->methods[0]->is_public);
    EXPECT_EQ(definition->methods[0]->parameters[0].type->value, "*box<T>");
}

TEST(ParserTest, DeferredListsRejectMissingCommasTypesAndDelimiters) {
    for (const std::string source :
         {"def struct X { def x: i32 def y: i32 }", "def struct X { def x }",
          "def struct X { def x: i32; }", "def struct X { pub def f(self: *X) -> void {} }",
          "def struct X { def f(self) -> void {} }", "def struct X<T {}", "def struct X<> {}",
          "def struct X<T U> {}", "def struct X {", "def main() -> void { def x: X<i32; }",
          "def main() -> void { def x: X<>; }"})
        rejected(source, ParseMode::SyntaxOnly);
    for (const std::string expression :
         {"X { x: 1 y: 2 }", "X { x: f() y: 2 }", "X { x: a y: 2 }", "X { x: 1", "[1 2]", "[1,",
          "a[1", "a.", "f([1], X { x: 2 }"})
        rejected("def main() -> void { " + expression + "; }", ParseMode::SyntaxOnly);
}

TEST(ParserTest, DeferredExpressionsComposeWithoutSkippingTokens) {
    auto result = checked(R"(def main() -> void {
        def a: [i32; 2] = [f(1), 2 + 3,];
        f([1, 2], X { x: f(3), y: a[0], },);
        def nested: box<box<i32>> = box<box<i32>> { value: box<i32> { value: 1 } };
        a[0] = f().value;
        return;
    })",
                          ParseMode::SyntaxOnly);
    ASSERT_TRUE(result.success);
    auto *fn = dynamic_cast<FunctionDefinition *>(result.program[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->body->statements.size(), 5u);
}

TEST(ParserTest, NestedTypeClosersPreserveSourceSpans) {
    GloinParser parser(Lexer("box<box<i32>> tail", "type.gloin"), ParseMode::SyntaxOnly);
    auto type = parser.parse_type();
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->value, "box<box<i32>>");
    EXPECT_EQ(type->span.begin, 0u);
    EXPECT_EQ(type->span.end, 13u);
    auto tail = parser.parse_expression(0);
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(tail->to_string(), "tail");
    EXPECT_EQ(tail->span.begin, 14u);
    EXPECT_EQ(tail->span.end, 18u);
}

TEST(ParserTest, ExpressionAndStatementSpansExcludeFollowingTrivia) {
    auto result = checked("def main() -> i32 {\n  return -f(1 + 2); // trailing\n}");
    ASSERT_TRUE(result.success);
    auto *fn = dynamic_cast<FunctionDefinition *>(result.program[0].get());
    auto *ret = dynamic_cast<ReturnStatement *>(fn->body->statements[0].get());
    ASSERT_NE(ret, nullptr);
    const auto spelling = [](const Node *node) {
        return node->span.source->text.substr(node->span.begin, node->span.end - node->span.begin);
    };
    EXPECT_EQ(spelling(ret), "return -f(1 + 2);");
    auto *prefix = dynamic_cast<PrefixExpression *>(ret->return_value.get());
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(spelling(prefix), "-f(1 + 2)");
    auto *call = dynamic_cast<CallExpression *>(prefix->right.get());
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(spelling(call), "f(1 + 2)");
    EXPECT_EQ(spelling(call->function.get()), "f");
    EXPECT_EQ(spelling(call->arguments[0].get()), "1 + 2");
}

TEST(ParserTest, FirstErrorHasExactPositionAndDiscardsPriorDeclarations) {
    GloinParser parser{
        Lexer("def good() -> void {}\r\ndef bad(x: i32 y: i32) -> void {}", "error.gloin")};
    auto result = parser.parse_checked_program();
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.program.empty());
    ASSERT_EQ(parser.diagnostics()->all().size(), 1u);
    std::ostringstream errors;
    parser.diagnostics()->render(errors);
    EXPECT_EQ(errors.str(), "error.gloin:2:16: error: Expected ',' or ')' after parameter\n");
}

TEST(ParserTest, ExcessiveNestingFailsWithDiagnostic) {
    rejected("def main() -> i32 { return " + std::string(600, '(') + "1" + std::string(600, ')') +
             "; }");
}
