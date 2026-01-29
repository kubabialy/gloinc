cmake_minimum_required(VERSION 3.28)
project(gloinc)

set(CMAKE_CXX_STANDARD 23)

add_executable(gloinc main.cpp
        src/lexer.h
        src/lexer.cpp)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(gloinc_test tests/lexer_test.cpp src/lexer.cpp src/lexer.h)
target_link_libraries(gloinc_test GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(gloinc_test)
