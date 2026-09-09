#include "../src/lexer.h"
#include "../src/parser.h"
#include <iostream>

int main() {
    // Test that import statements are recognized
    std::string input = "import @std/io; import ./local_module; import #external_package; def "
                        "test_imports() { print(\"Hello from imports test!\"); return 42; }";

    Lexer lexer(input);
    GloinParser parser(lexer);

    try {
        auto ast = parser.parse_program();
        if (ast.size() > 0) {
            std::cout << "✅ Parsing successful with " << ast.size() << " statements" << std::endl;
        } else {
            std::cout << "❌ Parsing failed" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cout << "❌ Parser error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}