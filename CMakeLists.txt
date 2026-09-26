cmake_minimum_required(VERSION 3.28)
project(gloinc VERSION 0.0.3)

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

# LLVM-independent runtime. The JIT links it statically; external runners load
# the shared variant, which is also shipped for emitted LLVM programs.
add_library(gloin_runtime STATIC src/arena_runtime.cpp src/stdlib_runtime.cpp src/numeric_runtime.cpp src/io_runtime.cpp src/context_runtime.cpp src/math_runtime.cpp src/time_runtime.cpp src/random_runtime.cpp)
add_library(gloin_runtime_shared SHARED src/arena_runtime.cpp src/stdlib_runtime.cpp src/numeric_runtime.cpp src/io_runtime.cpp src/context_runtime.cpp src/math_runtime.cpp src/time_runtime.cpp src/random_runtime.cpp)
foreach(runtime_target gloin_runtime gloin_runtime_shared)
    target_include_directories(${runtime_target} SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fast_float/include")
endforeach()
set_target_properties(gloin_runtime_shared PROPERTIES OUTPUT_NAME gloin_runtime)
target_include_directories(gloin_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(gloin_frontend STATIC
    src/lexer.cpp
    src/parser.cpp
    src/sema.cpp
    src/sema_structs.cpp
    src/sema_pointers.cpp
    src/sema_methods.cpp
    src/sema_arena.cpp
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
    GLOIN_BUILD_STDLIB_DIR="${CMAKE_CURRENT_BINARY_DIR}/stdlib")
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
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
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
inline constexpr auto gloinc = R\"tool($<TARGET_FILE:gloinc>)tool\";
inline constexpr auto core_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/core_counter.gloin)tool\";
inline constexpr auto standard_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/standard_library.gloin)tool\";
inline constexpr auto strings_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/strings_lab.gloin)tool\";
inline constexpr auto strings_guide = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/docs/strings.md)tool\";
inline constexpr auto module_example = R\"tool(${CMAKE_CURRENT_SOURCE_DIR}/examples/module_lab.gloin)tool\";
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
        tests/context_library_test.cpp
        tests/math_library_test.cpp
        tests/time_random_library_test.cpp
        tests/integrated_examples_test.cpp
        tests/module_test.cpp
        tests/ordinary_struct_test.cpp
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
    gtest_discover_tests(gloinc_test PROPERTIES TIMEOUT 30)

    add_executable(gloin_arena_test tests/runtime/arena_test.cpp)
    target_link_libraries(gloin_arena_test PRIVATE gloin_runtime GTest::gtest_main)
    list(APPEND GLOIN_TARGETS gloin_arena_test)
    gtest_discover_tests(gloin_arena_test PROPERTIES TIMEOUT 30)

    add_executable(gloin_standard_test tests/runtime/standard_test.cpp tests/runtime/numeric_test.cpp tests/runtime/io_test.cpp tests/runtime/context_test.cpp tests/runtime/math_test.cpp tests/runtime/time_random_test.cpp)
    target_link_libraries(gloin_standard_test PRIVATE gloin_runtime GTest::gtest_main)
    list(APPEND GLOIN_TARGETS gloin_standard_test)
    gtest_discover_tests(gloin_standard_test PROPERTIES TIMEOUT 30)

    # Required scalar-core suites; the unfiltered suite still reports deferred failures.
    set(GLOIN_CORE_TEST_REGEX "^(LexerTest|DiagnosticsTest|CheckedProgramTest|ParserTest|ScopeTest|VariablesTest|NumericTest|FunctionsTest|OperatorsTest|ControlFlowTest|ForUnlessTest|LoweringTest|JitRunnerTest|CliTest|StandardModuleTest|StandardLibraryTest|StringLibraryTest|TextLibraryTest|NumericLibraryTest|IoLibraryTest|ContextLibraryTest|MathLibraryTest|TimeRandomLibraryTest|IntegratedExamplesTest|ModuleTest|OrdinaryStructTest|PointerTest|MethodTest|DeferTest|ArenaTest|ArenaRuntimeTest|StandardRuntimeTest|NumericRuntimeTest|IoRuntimeTest|ContextRuntimeTest|MathRuntimeTest|TimeRandomRuntimeTest|CoreAcceptanceTest|E2ETest|ExternalRunnerTest|MLIRSetup)\\.")
    add_custom_target(check-core
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_CURRENT_BINARY_DIR}"
            -j 4 --no-tests=error -R "${GLOIN_CORE_TEST_REGEX}" --output-on-failure
            --output-junit "${CMAKE_CURRENT_BINARY_DIR}/core.xml"
        DEPENDS gloinc_test gloin_arena_test gloin_standard_test USES_TERMINAL VERBATIM)
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
endif()

include(GNUInstallDirs)
set_target_properties(gloinc PROPERTIES
    INSTALL_RPATH "$<TARGET_FILE_DIR:LLVM>;$<TARGET_FILE_DIR:MLIR>;$<TARGET_FILE_DIR:MLIRExecutionEngineShared>")
install(TARGETS gloinc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS gloin_runtime gloin_runtime_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/arena_runtime.h src/stdlib_runtime.h src/io_runtime.h src/context_runtime.h src/math_runtime.h src/time_runtime.h src/random_runtime.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gloin)
install(DIRECTORY stdlib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/stdlib
    FILES_MATCHING PATTERN "*.gloin")
install(FILES scripts/install-llvm.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/scripts)
install(FILES README.md CONTRIBUTING.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    PATTERN ".DS_Store" EXCLUDE)
install(FILES examples/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
install(FILES tests/fixtures/core/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/tests/fixtures/core)
install(FILES third_party/fast_float/LICENSE-MIT third_party/fast_float/README.md third_party/fast_float/SHA256SUMS
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party/fast_float)
install(FILES examples/core_counter.gloin examples/hello_world.gloin examples/fixed_arrays.gloin examples/pointer_offsets.gloin examples/arena_lab.gloin examples/module_lab.gloin examples/standard_library.gloin examples/strings_lab.gloin examples/text_lab.gloin examples/numbers_lab.gloin examples/io_copy.gloin examples/io_filter.gloin examples/file_tool.gloin examples/math_lab.gloin examples/simulation_lab.gloin examples/config_reader.gloin examples/statistics_tool.gloin
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples)
install(DIRECTORY examples/data DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples)
install(DIRECTORY examples/modules DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/examples
    FILES_MATCHING PATTERN "*.gloin")
install(DIRECTORY tests/fixtures/core/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gloinc/core-fixtures)

# Packages depend on the same LLVM/MLIR installation used at configure time.
# Sanitizer builds are diagnostic tools, never distribution artifacts.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" AND NOT GLOIN_ENABLE_SANITIZERS)
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_NAME gloinc)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "gloinc-${PROJECT_VERSION}-macos-arm64")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gloin native compiler and JIT; requires LLVM/MLIR 21.1.6")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    include(CPack)
endif()
