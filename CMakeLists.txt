cmake_minimum_required(VERSION 3.28)
project(gloinc VERSION 0.1.0)

option(GLOIN_ENABLE_SANITIZERS "Instrument project C++ code with AddressSanitizer and UBSan" OFF)
if(GLOIN_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GLOIN_ENABLE_SANITIZERS requires Clang or AppleClang")
endif()

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

# Keep headers, generated dialect code, and shared libraries on the tested
# release for each platform. apt.llvm.org provides the shared 21.1.8 build on
# Linux; the pinned Homebrew formula provides 21.1.6 on macOS.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GLOIN_REQUIRED_LLVM_VERSION 21.1.8)
else()
    set(GLOIN_REQUIRED_LLVM_VERSION 21.1.6)
endif()
find_package(MLIR ${GLOIN_REQUIRED_LLVM_VERSION} EXACT REQUIRED CONFIG)
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR AND EXISTS "/opt/homebrew/opt/openssl@3")
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
elseif(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR AND EXISTS "/usr/local/opt/openssl@3")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@3")
endif()
find_package(OpenSSL 3 REQUIRED)

foreach(toolchain_target IN ITEMS MLIR LLVM MLIRExecutionEngineShared)
    if(NOT TARGET ${toolchain_target})
        message(FATAL_ERROR
            "Gloin requires the ${toolchain_target} shared target from LLVM/MLIR ${GLOIN_REQUIRED_LLVM_VERSION}. "
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

# LLVM-independent runtime. The JIT links it statically; external runners load
# the shared variant, which is also shipped for emitted LLVM programs.
add_library(gloin_runtime STATIC src/arena_runtime.cpp src/memory_runtime.cpp src/stdlib_runtime.cpp src/numeric_runtime.cpp src/io_runtime.cpp src/net_runtime.cpp src/tls_runtime.cpp src/context_runtime.cpp src/fs_replace_runtime.cpp src/process_runtime.cpp src/process_limits_runtime.cpp src/math_runtime.cpp src/time_runtime.cpp src/random_runtime.cpp)
add_library(gloin_runtime_shared SHARED src/arena_runtime.cpp src/memory_runtime.cpp src/stdlib_runtime.cpp src/numeric_runtime.cpp src/io_runtime.cpp src/net_runtime.cpp src/tls_runtime.cpp src/context_runtime.cpp src/fs_replace_runtime.cpp src/process_runtime.cpp src/process_limits_runtime.cpp src/math_runtime.cpp src/time_runtime.cpp src/random_runtime.cpp)
foreach(runtime_target gloin_runtime gloin_runtime_shared)
    target_include_directories(${runtime_target} SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fast_float/include")
    target_link_libraries(${runtime_target} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endforeach()
set_target_properties(gloin_runtime_shared PROPERTIES OUTPUT_NAME gloin_runtime)
target_include_directories(gloin_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(gloin_frontend STATIC
    src/ast_clone.cpp
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/sema_structs.cpp
    src/sema_pointers.cpp
    src/sema_methods.cpp
    src/sema_arena.cpp
    src/sema_memory.cpp
    src/sema_modules.cpp
    src/sema_standard.cpp
    src/numeric.cpp
    src/sema_numeric.cpp
    src/sema_constants.cpp
    src/sema_control_flow.cpp
)
target_include_directories(gloin_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(gloin_frontend PUBLIC cxx_std_23)
# Frontend numeric values use LLVM APInt/APFloat, with the same shared LLVM as the backend.
target_include_directories(gloin_frontend SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(gloin_frontend PUBLIC LLVM)

add_library(gloin_backend STATIC
    src/codegen.cpp
    src/codegen_operators.cpp
    src/codegen_control_flow.cpp
    src/codegen_defer.cpp
    src/codegen_standard.cpp
    src/codegen_arena.cpp
    src/codegen_memory.cpp
    src/compiler.cpp
    src/module_loader.cpp
    src/target_layout.cpp
    src/lowering.cpp
    src/dialect/GloinDialect.cpp
    src/jit_runner.cpp
    src/native_output.cpp
)
add_dependencies(gloin_backend GloinDialectIncGen GloinTypesIncGen GloinOpsIncGen)
target_compile_definitions(gloin_backend PRIVATE
    GLOIN_BUILD_STDLIB_DIR="${CMAKE_CURRENT_BINARY_DIR}/stdlib"
    GLOIN_OPENSSL_SSL_LIBRARY="${OPENSSL_SSL_LIBRARY}"
    GLOIN_OPENSSL_CRYPTO_LIBRARY="${OPENSSL_CRYPTO_LIBRARY}")
file(GLOB GLOIN_STANDARD_MODULES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stdlib/*.gloin")
foreach(module IN LISTS GLOIN_STANDARD_MODULES)
    get_filename_component(module_name "${module}" NAME)
    configure_file("${module}" "stdlib/${module_name}" COPYONLY)
endforeach()
target_include_directories(gloin_backend PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(gloin_backend SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
separate_arguments(GLOIN_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(gloin_backend PUBLIC ${GLOIN_LLVM_DEFINITIONS})
target_link_libraries(gloin_backend PUBLIC
    gloin_frontend
    gloin_runtime
    MLIRExecutionEngineShared
    MLIR
    LLVM
)

add_executable(gloinc main.cpp)
target_link_libraries(gloinc PRIVATE gloin_backend)
if((APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64") OR
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
    CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|x86_64)$"))
    if(GLOIN_ENABLE_SANITIZERS)
        # The runtime archive is instrumented by the build compiler. Its ASan
        # runtime must come from that same compiler when linking generated code.
        set(GLOIN_NATIVE_LINKER "${CMAKE_CXX_COMPILER}")
    else()
        find_program(GLOIN_NATIVE_LINKER NAMES clang++
            HINTS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    endif()
endif()
target_compile_definitions(gloinc PRIVATE
    GLOIN_VERSION="${PROJECT_VERSION}"
    GLOIN_LLVM_VERSION="${LLVM_PACKAGE_VERSION}"
)
target_compile_definitions(gloin_backend PRIVATE
    GLOIN_NATIVE_LINKER="${GLOIN_NATIVE_LINKER}")
set_source_files_properties(src/native_output.cpp PROPERTIES
    COMPILE_DEFINITIONS "GLOIN_NATIVE_SANITIZED=$<BOOL:${GLOIN_ENABLE_SANITIZERS}>")
set(GLOIN_TARGETS gloin_frontend gloin_backend gloinc gloin_runtime gloin_runtime_shared)

# Build the formatter from Gloin source with the compiler under test.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt"
    COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gloinfmt/main.gloin"
    DEPENDS gloinc tools/gloinfmt/main.gloin tools/gloinfmt/format.gloin
            tools/gloinfmt/style.gloin tools/gloinfmt/tokens.gloin tools/gloinfmt/git.gloin ${GLOIN_STANDARD_MODULES}
    VERBATIM)
add_custom_target(gloin_formatter ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt")

# Repository checks are ordinary Gloin programs, also available without GTest.
set(GLOIN_CHECK_SOURCES tools/checks/common.gloin tools/checks/entities.gloin
    tools/checks/html_names.gloin tools/checks/markup.gloin tools/checks/docs.gloin tools/checks/suite.gloin)
foreach(check IN ITEMS docs suite)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-check-${check}"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-check-${check}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/checks/${check}_main.gloin"
        DEPENDS gloinc tools/checks/${check}_main.gloin ${GLOIN_CHECK_SOURCES} ${GLOIN_STANDARD_MODULES}
        VERBATIM)
endforeach()
add_custom_target(gloin_checks ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-check-docs"
    "${CMAKE_CURRENT_BINARY_DIR}/gloin-check-suite")

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
    target_link_libraries(gloin_test_process PRIVATE gloin_runtime)
    target_include_directories(gloin_test_process PRIVATE src)
    # Exercise subprocess paths containing spaces in every harness regression test.
    set_target_properties(gloin_test_process PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test tools")
    list(APPEND GLOIN_TARGETS gloin_test_process)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>/tool_paths.h" CONTENT
"#pragma once
namespace gloin_test {
inline constexpr unsigned cli_timeout_seconds = $<IF:$<BOOL:${GLOIN_ENABLE_SANITIZERS}>,30,10>;
inline constexpr auto gloinc = R\"tool($<TARGET_FILE:gloinc>)tool\";
inline constexpr auto core_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/core_counter.gloin)tool\";
inline constexpr auto standard_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/standard_library.gloin)tool\";
inline constexpr auto strings_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/strings_lab.gloin)tool\";
inline constexpr auto strings_guide = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/docs/strings.md)tool\";
inline constexpr auto module_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/module_lab.gloin)tool\";
inline constexpr auto custom_arena_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/custom_arena.gloin)tool\";
inline constexpr auto core_fixtures = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/core)tool\";
inline constexpr auto mlir_opt = R\"tool(${GLOIN_MLIR_OPT})tool\";
inline constexpr auto mlir_runner = R\"tool(${GLOIN_MLIR_RUNNER})tool\";
inline constexpr auto arena_runtime = R\"tool($<TARGET_FILE:gloin_runtime_shared>)tool\";
inline constexpr auto process_fixture = R\"tool($<TARGET_FILE:gloin_test_process>)tool\";
}
")

    add_executable(gloinc_test
        tests/lexer_test.cpp
        tests/diagnostics_test.cpp
        tests/checked_program_test.cpp
        tests/parser_test.cpp
        tests/numeric_test.cpp
        tests/functions_test.cpp
        tests/operators_test.cpp
        tests/control_flow_test.cpp
        tests/for_unless_test.cpp
        tests/lowering_test.cpp
        tests/variables_test.cpp
        tests/scope_test.cpp
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
        tests/cli_test.cpp
        tests/standard_module_test.cpp
        tests/standard_library_test.cpp
        tests/string_library_test.cpp
        tests/text_library_test.cpp
        tests/numeric_library_test.cpp
        tests/io_library_test.cpp
        tests/network_library_test.cpp
        tests/context_library_test.cpp
        tests/math_library_test.cpp
        tests/time_random_library_test.cpp
        tests/integrated_examples_test.cpp
        tests/module_test.cpp
        tests/ordinary_struct_test.cpp
        tests/result_test.cpp
        tests/enum_test.cpp
        tests/pointer_test.cpp
        tests/method_test.cpp
        tests/defer_test.cpp
        tests/core_acceptance_test.cpp
        tests/codegen_generics_test.cpp
        tests/external_runner_test.cpp
        tests/support/external_runner.cpp
    )
    add_dependencies(gloinc_test gloin_test_process gloinc gloin_runtime_shared)
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
    # Source-file acceptance fixtures must each be named by a maintained test.
    file(GLOB_RECURSE GLOIN_CORE_FIXTURES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/core/*.gloin")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_acceptance_test.cpp" GLOIN_CORE_TESTS)
    foreach(fixture IN LISTS GLOIN_CORE_FIXTURES)
        string(FIND "${GLOIN_CORE_TESTS}" "\"${fixture}\"" fixture_position)
        if(fixture_position EQUAL -1)
            message(FATAL_ERROR "Core fixture ${fixture} is missing from CoreAcceptanceTest")
        endif()
    endforeach()
    target_link_libraries(gloinc_test PRIVATE gloin_backend GTest::gtest_main)
    list(APPEND GLOIN_TARGETS gloinc_test)

    include(GoogleTest)
    # Linux signal termination can take longer under a shared CI/VM host.
    # Instrumented Debug cases can invoke the compiler ten times in one test;
    # allow that aggregate work while retaining each invocation's deadline.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR GLOIN_ENABLE_SANITIZERS)
        set(GLOIN_COMPILER_TEST_TIMEOUT 120)
    else()
        set(GLOIN_COMPILER_TEST_TIMEOUT 30)
    endif()
    set(GLOIN_SIGNATURE_MATRIX_TEST "ContextLibraryTest.PublicSignaturesAndCanonicalPrimitivePrivacyAreChecked")
    gtest_discover_tests(gloinc_test TEST_FILTER "-NetworkLibraryTest.*:${GLOIN_SIGNATURE_MATRIX_TEST}"
        PROPERTIES TIMEOUT ${GLOIN_COMPILER_TEST_TIMEOUT})
    # This matrix launches 143 compiler processes. Instrumented startup alone
    # took 120 seconds locally; keep headroom for the four-worker sanitizer gate.
    set(GLOIN_SIGNATURE_MATRIX_TIMEOUT ${GLOIN_COMPILER_TEST_TIMEOUT})
    if(GLOIN_ENABLE_SANITIZERS)
        set(GLOIN_SIGNATURE_MATRIX_TIMEOUT 300)
    endif()
    gtest_discover_tests(gloinc_test TEST_FILTER "${GLOIN_SIGNATURE_MATRIX_TEST}"
        PROPERTIES TIMEOUT ${GLOIN_SIGNATURE_MATRIX_TIMEOUT})
    # The round-trip case can compile twice (JIT and native), then run the binary.
    gtest_discover_tests(gloinc_test TEST_FILTER "NetworkLibraryTest.*"
        PROPERTIES TIMEOUT 150)

    add_executable(gloin_arena_test tests/runtime/arena_test.cpp)
    target_link_libraries(gloin_arena_test PRIVATE gloin_runtime GTest::gtest_main)
    list(APPEND GLOIN_TARGETS gloin_arena_test)
    gtest_discover_tests(gloin_arena_test PROPERTIES TIMEOUT 30)

    add_executable(gloin_standard_test tests/runtime/standard_test.cpp tests/runtime/numeric_test.cpp tests/runtime/io_test.cpp tests/runtime/net_test.cpp tests/runtime/tls_test.cpp tests/runtime/context_test.cpp tests/runtime/math_test.cpp tests/runtime/time_random_test.cpp)
    target_link_libraries(gloin_standard_test PRIVATE gloin_runtime GTest::gtest_main)
    target_include_directories(gloin_standard_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>")
    add_dependencies(gloin_standard_test gloin_test_process)
    list(APPEND GLOIN_TARGETS gloin_standard_test)
    gtest_discover_tests(gloin_standard_test PROPERTIES TIMEOUT 30)

    find_program(GLOIN_OPENSSL_COMMAND NAMES openssl HINTS "${OPENSSL_ROOT_DIR}/bin" REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-tls-test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-tls-test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tls/main.gloin"
        DEPENDS gloinc tests/tls/main.gloin tests/support/tool_runner.gloin
                tools/checks/common.gloin ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_tls_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-tls-test")
    add_test(NAME TlsClientSmoke.LocalPeers
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-tls-test" "$<TARGET_FILE:gloinc>"
                "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${GLOIN_OPENSSL_COMMAND}")
    set_tests_properties(TlsClientSmoke.LocalPeers PROPERTIES TIMEOUT 360)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-http-test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-http-test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/http/main.gloin"
        DEPENDS gloinc tests/http/main.gloin tests/http/peer.gloin tests/http/wire.gloin
                tests/support/tool_runner.gloin tools/checks/common.gloin ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_http_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-http-test")
    add_test(NAME HttpClientSmoke.LocalPeers
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-http-test" client "$<TARGET_FILE:gloinc>"
                "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${GLOIN_OPENSSL_COMMAND}"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/http_client.gloin")
    set_tests_properties(HttpClientSmoke.LocalPeers PROPERTIES TIMEOUT 360)
    add_test(NAME HttpStreamSmoke.LocalPeers
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-http-test" stream "$<TARGET_FILE:gloinc>"
                "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" "${GLOIN_OPENSSL_COMMAND}"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/http_stream.gloin")
    set_tests_properties(HttpStreamSmoke.LocalPeers PROPERTIES TIMEOUT 360)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-json-test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-json-test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/json/main.gloin"
        DEPENDS gloinc tests/json/main.gloin tests/support/tool_runner.gloin
                tools/checks/common.gloin ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_json_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-json-test")
    add_test(NAME JsonSmoke.CodecAndState
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-json-test" "$<TARGET_FILE:gloinc>"
                "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(JsonSmoke.CodecAndState PROPERTIES TIMEOUT 360)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-stress-test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-stress-test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/integrated/main.gloin"
        DEPENDS gloinc tests/integrated/main.gloin tests/support/tool_runner.gloin
                tools/checks/common.gloin ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_stress_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-stress-test")
    add_test(NAME GloinIntegratedStress.MillionRecords
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-stress-test" "$<TARGET_FILE:gloinc>"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(GloinIntegratedStress.MillionRecords PROPERTIES TIMEOUT 360 LABELS "stress")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt_test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt_test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/main.gloin"
        DEPENDS gloinc tests/formatter/main.gloin tools/gloinfmt/tokens.gloin tests/formatter/runner.gloin
                tests/support/tool_runner.gloin
                tools/gloinfmt/format.gloin tools/gloinfmt/style.gloin ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_formatter_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt_test")
    add_test(NAME GloinFormatterSmoke.LayoutAndTraversal
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt_test" --run
                "$<TARGET_FILE:gloinc>" "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt"
                "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt_test"
                "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(GloinFormatterSmoke.LayoutAndTraversal PROPERTIES TIMEOUT 90)

    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gloin-tools-test"
        COMMAND "$<TARGET_FILE:gloinc>" -O2 -o "${CMAKE_CURRENT_BINARY_DIR}/gloin-tools-test"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/main.gloin"
        DEPENDS gloinc tests/tooling/main.gloin tests/support/tool_runner.gloin
                ${GLOIN_CHECK_SOURCES} ${GLOIN_STANDARD_MODULES}
        VERBATIM)
    add_custom_target(gloin_tooling_tests ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gloin-tools-test")
    set(GLOIN_TOOLING_TEST_ARGS "$<TARGET_FILE:gloinc>" "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt"
        "${CMAKE_CURRENT_BINARY_DIR}/gloin-check-docs" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME GloinToolingSmoke.NativeChecksAndWrites
        COMMAND "${CMAKE_CURRENT_BINARY_DIR}/gloin-tools-test" ${GLOIN_TOOLING_TEST_ARGS})
    add_test(NAME GloinToolingSmoke.JitChecksAndWrites
        COMMAND "$<TARGET_FILE:gloinc>" --jit "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/main.gloin"
                -- ${GLOIN_TOOLING_TEST_ARGS})
    set_tests_properties(GloinToolingSmoke.NativeChecksAndWrites GloinToolingSmoke.JitChecksAndWrites
        PROPERTIES TIMEOUT 90)

    # Required supported-language suites; the unfiltered suite still reports deferred failures.
    set(GLOIN_CORE_TEST_REGEX "^(LexerTest|DiagnosticsTest|CheckedProgramTest|ParserTest|ScopeTest|VariablesTest|NumericTest|FunctionsTest|OperatorsTest|ControlFlowTest|ForUnlessTest|LoweringTest|JitRunnerTest|CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|NetworkLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|EnumTest|CheckedGenericsTest|ResultTest|PointerTest|MethodTest|DeferTest|ArenaTest|ArenaRuntimeTest|StandardRuntimeTest|NumericRuntimeTest|IoRuntimeTest|NetRuntimeTest|TlsRuntimeTest|ContextRuntimeTest|MathRuntimeTest|TimeRandomRuntimeTest|CoreAcceptanceTest|E2ETest|ExternalRunnerTest|MLIRSetup|TlsClientSmoke|HttpClientSmoke|HttpStreamSmoke|JsonSmoke|GloinFormatterSmoke|GloinToolingSmoke|GloinIntegratedStress)\\.")
    add_custom_target(check-core
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_CURRENT_BINARY_DIR}"
            -j 4 --no-tests=error -R "${GLOIN_CORE_TEST_REGEX}" --output-on-failure
            --output-junit "${CMAKE_CURRENT_BINARY_DIR}/core.xml"
        DEPENDS gloinc_test gloin_arena_test gloin_standard_test gloin_formatter gloin_formatter_tests
                gloin_checks gloin_tooling_tests gloin_json_tests gloin_stress_tests gloin_tls_tests gloin_http_tests USES_TERMINAL VERBATIM)
endif()

set_target_properties(${GLOIN_TARGETS} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

if(GLOIN_ENABLE_SANITIZERS)
    foreach(target IN LISTS GLOIN_TARGETS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
    # The prebuilt LLVM/MLIR libraries share a bump allocator with generated
    # Gloin type storage. Instrumenting only our storage poisons slabs that the
    # uninstrumented libraries subsequently allocate from without unpoisoning.
    set_source_files_properties(src/dialect/GloinDialect.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-sanitize=address")
endif()

include(GNUInstallDirs)
set_target_properties(gloinc PROPERTIES
    INSTALL_RPATH "$<TARGET_FILE_DIR:LLVM>;$<TARGET_FILE_DIR:MLIR>;$<TARGET_FILE_DIR:MLIRExecutionEngineShared>")
install(TARGETS gloinc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/gloinfmt" DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS gloin_runtime gloin_runtime_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/arena_runtime.h src/memory_runtime.h src/stdlib_runtime.h src/io_runtime.h src/net_runtime.h src/context_runtime.h src/math_runtime.h src/time_runtime.h src/random_runtime.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gloin)
install(DIRECTORY stdlib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/stdlib
    FILES_MATCHING PATTERN "*.gloin")
install(FILES scripts/install-llvm.sh scripts/install-llvm-linux.sh
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/scripts)
install(FILES README.md CONTRIBUTING.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    PATTERN ".DS_Store" EXCLUDE)
# Preserve the source layout used by the guides' relative example/module links.
# These small source copies also retain example imports and fixture data when
# readers open a guide from an installed or relocated prefix.
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples
    FILES_MATCHING PATTERN "*.gloin" PATTERN "*.md" PATTERN "*.conf" PATTERN "*.txt")
install(DIRECTORY stdlib/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/stdlib
    FILES_MATCHING PATTERN "*.gloin" PATTERN "*.md")
install(FILES tests/fixtures/core/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/tests/fixtures/core)
install(FILES third_party/fast_float/LICENSE-MIT third_party/fast_float/README.md third_party/fast_float/SHA256SUMS
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party/fast_float)
install(FILES examples/http_echo_server.gloin examples/tls_server.gloin examples/child_process.gloin examples/child_capture.gloin examples/child_options.gloin examples/symlinks.gloin examples/temporary_workspace.gloin examples/core_counter.gloin examples/hello_world.gloin examples/fixed_arrays.gloin examples/pointer_offsets.gloin examples/arena_lab.gloin examples/custom_arena.gloin examples/result_handling.gloin examples/network_http.gloin examples/http_client.gloin examples/http_stream.gloin examples/json.gloin examples/directory_walk.gloin examples/module_discovery.gloin examples/module_lab.gloin examples/standard_library.gloin examples/strings_lab.gloin examples/text_lab.gloin examples/numbers_lab.gloin examples/io_copy.gloin examples/io_filter.gloin examples/strip_nuls.gloin examples/file_tool.gloin examples/math_lab.gloin examples/simulation_lab.gloin examples/config_reader.gloin examples/statistics_tool.gloin examples/generic_structs.gloin examples/generic_functions.gloin examples/generic_methods.gloin examples/enums.gloin examples/slices.gloin examples/vector.gloin examples/fixed_vector.gloin
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples)
install(DIRECTORY examples/data DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples)
install(DIRECTORY examples/modules DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples
    FILES_MATCHING PATTERN "*.gloin")
install(DIRECTORY examples/packages DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples
    FILES_MATCHING PATTERN "*.gloin")
install(DIRECTORY tests/fixtures/core/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/core-fixtures)

# Packages depend on the same LLVM/MLIR installation used at configure time.
# Sanitizer builds are diagnostic tools, never distribution artifacts.
if(NOT GLOIN_ENABLE_SANITIZERS AND APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(GLOIN_PACKAGE_PLATFORM "macos-arm64")
elseif(NOT GLOIN_ENABLE_SANITIZERS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|x86_64)$")
    set(GLOIN_PACKAGE_PLATFORM "linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(GLOIN_PACKAGE_PLATFORM)
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_NAME gloinc)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "gloinc-${PROJECT_VERSION}-${GLOIN_PACKAGE_PLATFORM}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gloin native compiler and JIT; requires LLVM/MLIR ${GLOIN_REQUIRED_LLVM_VERSION}")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    include(CPack)
endif()
