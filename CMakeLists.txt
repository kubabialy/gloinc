cmake_minimum_required(VERSION 3.28)
project(gloinc)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MLIR/LLVM Setup
# Attempt to auto-detect Homebrew LLVM on macOS if not provided
if(NOT DEFINED LLVM_DIR AND NOT DEFINED MLIR_DIR)
    if(EXISTS "/opt/homebrew/opt/llvm")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/llvm")
    elseif(EXISTS "/usr/local/opt/llvm")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/llvm")
    endif()
endif()

# Keep headers, generated dialect code, and shared libraries on the tested release.
find_package(MLIR 21.1.6 EXACT REQUIRED CONFIG)

foreach(toolchain_target IN ITEMS MLIR LLVM MLIRExecutionEngineShared)
    if(NOT TARGET ${toolchain_target})
        message(FATAL_ERROR
            "Gloin requires the ${toolchain_target} shared target from LLVM/MLIR 21.1.6. "
            "Set LLVM_DIR and MLIR_DIR to a matching installation built with "
            "LLVM_BUILD_LLVM_DYLIB=ON, LLVM_LINK_LLVM_DYLIB=ON, "
            "MLIR_BUILD_MLIR_DYLIB=ON, MLIR_LINK_MLIR_DYLIB=ON, and MLIR_ENABLE_EXECUTION_ENGINE=ON. "
            "See docs/toolchain.md.")
    endif()
    get_target_property(toolchain_target_type ${toolchain_target} TYPE)
    if(NOT toolchain_target_type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR
            "Gloin requires ${toolchain_target} to be a shared library; found ${toolchain_target_type}. "
            "Use the shared LLVM/MLIR installation described in docs/toolchain.md.")
    endif()
endforeach()

message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)

# MLIR Dialect Generation
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/dialect")

set(GLOIN_TABLEGEN_INCLUDES
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dialect"
)

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinBase.td)
mlir_tablegen(src/dialect/GloinDialect.h.inc -gen-dialect-decls EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
mlir_tablegen(src/dialect/GloinDialect.cpp.inc -gen-dialect-defs EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
add_public_tablegen_target(GloinDialectIncGen)

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinTypes.td)
mlir_tablegen(src/dialect/GloinEnums.h.inc -gen-enum-decls EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
mlir_tablegen(src/dialect/GloinEnums.cpp.inc -gen-enum-defs EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
mlir_tablegen(src/dialect/GloinTypes.h.inc -gen-typedef-decls -typedefs-dialect=gloin EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
mlir_tablegen(src/dialect/GloinTypes.cpp.inc -gen-typedef-defs -typedefs-dialect=gloin EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
add_public_tablegen_target(GloinTypesIncGen)

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinOps.td)
mlir_tablegen(src/dialect/GloinOps.h.inc -gen-op-decls EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
mlir_tablegen(src/dialect/GloinOps.cpp.inc -gen-op-defs EXTRA_INCLUDES ${GLOIN_TABLEGEN_INCLUDES})
add_public_tablegen_target(GloinOpsIncGen)

add_library(gloin_frontend STATIC
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
)
target_include_directories(gloin_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(gloin_frontend PUBLIC cxx_std_23)

add_library(gloin_backend STATIC
    src/codegen.cpp
    src/dialect/GloinDialect.cpp
    src/jit_runner.cpp
)
add_dependencies(gloin_backend GloinDialectIncGen GloinTypesIncGen GloinOpsIncGen)
target_include_directories(gloin_backend PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(gloin_backend SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
separate_arguments(GLOIN_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(gloin_backend PUBLIC ${GLOIN_LLVM_DEFINITIONS})
target_link_libraries(gloin_backend PUBLIC
    gloin_frontend
    MLIRExecutionEngineShared
    MLIR
    LLVM
)

add_executable(gloinc main.cpp)
target_link_libraries(gloinc PRIVATE gloin_backend)
set(GLOIN_TARGETS gloin_frontend gloin_backend gloinc)

include(CTest)
if(BUILD_TESTING)
    find_program(GLOIN_MLIR_OPT NAMES mlir-opt HINTS "${LLVM_TOOLS_BINARY_DIR}"
        NO_DEFAULT_PATH REQUIRED)
    find_program(GLOIN_MLIR_RUNNER NAMES mlir-runner HINTS "${LLVM_TOOLS_BINARY_DIR}"
        NO_DEFAULT_PATH REQUIRED)

    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.16.0
        URL_HASH SHA256=78c676fc63881529bf97bf9d45948d905a66833fbfa5318ea2cd7478cb98f399
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest" FORCE)
    # Match the default shared MSVC runtime without affecting other platforms.
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)

    add_executable(gloin_test_process tests/support/process_fixture.cpp)
    # Exercise subprocess paths containing spaces in every harness regression test.
    set_target_properties(gloin_test_process PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test tools")
    list(APPEND GLOIN_TARGETS gloin_test_process)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>/tool_paths.h" CONTENT
"#pragma once
namespace gloin_test {
inline constexpr auto mlir_opt = R\"tool(${GLOIN_MLIR_OPT})tool\";
inline constexpr auto mlir_runner = R\"tool(${GLOIN_MLIR_RUNNER})tool\";
inline constexpr auto process_fixture = R\"tool($<TARGET_FILE:gloin_test_process>)tool\";
}
")

    add_executable(gloinc_test
        tests/lexer_test.cpp
        tests/parser_test.cpp
        tests/sema_test.cpp
        tests/mlir_test.cpp
        tests/codegen_test.cpp
        tests/struct_test.cpp
        tests/codegen_struct_test.cpp
        tests/spec_test.cpp
        tests/codegen_spec_test.cpp
        tests/arena_test.cpp
        tests/async_test.cpp
        tests/sema_async_test.cpp
        tests/e2e_test.cpp
        tests/basic_codegen_test.cpp
        tests/array_string_test.cpp
        tests/unless_test.cpp
        tests/jit_test.cpp
        tests/codegen_generics_test.cpp
        tests/external_runner_test.cpp
        tests/support/external_runner.cpp
    )
    add_dependencies(gloinc_test gloin_test_process)
    target_include_directories(gloinc_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>")
    # Keep the explicit source list, but fail configuration if a new suite is omitted.
    file(GLOB GLOIN_TEST_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    get_target_property(GLOIN_TEST_SOURCES gloinc_test SOURCES)
    foreach(test_file IN LISTS GLOIN_TEST_FILES)
        if(NOT test_file IN_LIST GLOIN_TEST_SOURCES)
            message(FATAL_ERROR "Test suite ${test_file} is missing from the gloinc_test target")
        endif()
    endforeach()
    target_link_libraries(gloinc_test PRIVATE gloin_backend GTest::gtest_main)
    list(APPEND GLOIN_TARGETS gloinc_test)

    include(GoogleTest)
    gtest_discover_tests(gloinc_test PROPERTIES TIMEOUT 30)
endif()

set_target_properties(${GLOIN_TARGETS} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
