#include <gtest/gtest.h>
#include "../src/parser.h"
#include "../src/sema.h"

class SemaAsyncTest : public ::testing::Test {
protected:
    void Check(const std::string& input, bool expect_error, const std::string& error_substr = "") {
        Lexer l(input);
        GloinParser p(l);
        auto program = p.parse_program();
        
        Sema sema;
        sema.check_program(program);
        
        if (expect_error) {
            EXPECT_TRUE(sema.has_error()) << "Expected error for input: " << input;
            if (sema.has_error() && !error_substr.empty()) {
                bool found = false;
                for (const auto& err : sema.get_errors()) {
                    if (err.find(error_substr) != std::string::npos) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                     std::string all_errors;
                     for(const auto& e : sema.get_errors()) all_errors += e + "\n";
                     EXPECT_TRUE(found) << "Expected error containing '" << error_substr << "', got:\n" << all_errors;
                }
            }
        } else {
            std::string all_errors;
            for(const auto& e : sema.get_errors()) all_errors += e + "\n";
            EXPECT_FALSE(sema.has_error()) << "Expected no error, got:\n" << all_errors;
        }
    }
};

TEST_F(SemaAsyncTest, BasicTypes) {
    Check("def x: i32 = 10;", false);
    Check("def x: i32 = \"hello\";", true, "Type mismatch");
}

TEST_F(SemaAsyncTest, AsyncTypes) {
    // Normal function
    Check(R"(
        def foo(x: i32) -> i32 { return x; }
        def main() -> void {
            def res: i32 = foo(10);
        }
    )", false);

    // Spawn function -> Deferred
    Check(R"(
        def foo(x: i32) -> i32 { return x; }
        def main() -> void {
            def res: Deferred<i32> = spawn foo(10);
        }
    )", false);

    // Spawn function type mismatch
    Check(R"(
        def foo(x: i32) -> i32 { return x; }
        def main() -> void {
            def res: i32 = spawn foo(10); // Error: Deferred<i32> != i32
        }
    )", true, "Type mismatch"); 

    // Await Deferred
    Check(R"(
        def foo(x: i32) -> i32 { return x; }
        def main() -> void {
            def d: Deferred<i32> = spawn foo(10);
            def res: i32 = await d;
        }
    )", false);
    
    // Await Non-Deferred
    Check(R"(
        def main() -> void {
            def x: i32 = 10;
            def res: i32 = await x;
        }
    )", true, "applied to non-deferred type");
}

TEST_F(SemaAsyncTest, DeferredFunction) {
    // Correct deferred function
    Check(R"(
        def deferred fetch() -> Deferred<string> {
            return "data"; 
        }
    )", false);
    
    // Incorrect deferred function return type
    Check(R"(
        def deferred fetch() -> string { 
        }
    )", true, "must return Deferred<T>");
}

TEST_F(SemaAsyncTest, ArgumentValidation) {
    Check(R"(
        def foo(a: i32, b: bool) -> void {}
        def main() -> void {
            foo(1, true);
        }
    )", false);
    
    Check(R"(
        def foo(a: i32) -> void {}
        def main() -> void {
            foo(1, 2);
        }
    )", true, "Incorrect number of arguments");
    
    Check(R"(
        def foo(a: i32) -> void {}
        def main() -> void {
            foo(true);
        }
    )", true, "type mismatch");
}
