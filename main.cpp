#include <iostream>

#include "src/lexer.h"

int main(int argc, char *argv[]) {
    const std::string input = "def main() -> int { return 0 }";

    auto lexer = Lexer(input);
    while (true) {
        const auto token = lexer.next_token();
        if (token.type == GLOIN_TOKEN_EOF) break;

        print_debug_token(token);
    }
    return 0;
}
