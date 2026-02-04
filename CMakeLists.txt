cmake_minimum_required(VERSION 3.28)
project(gloinc)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)

# MLIR/LLVM Setup
# Attempt to auto-detect Homebrew LLVM on macOS if not provided
if(NOT DEFINED LLVM_DIR AND NOT DEFINED MLIR_DIR)
    if(EXISTS "/opt/homebrew/opt/llvm")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/llvm")
    elseif(EXISTS "/usr/local/opt/llvm")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/llvm")
    endif()
endif()

find_package(MLIR REQUIRED CONFIG)

message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR}) # For generated headers
include_directories(src) 

add_definitions(${LLVM_DEFINITIONS})

# MLIR Dialect Generation
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/dialect")

set(GLOIN_DIALECT_INCLUDE "-I${CMAKE_SOURCE_DIR}/src/dialect")

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinBase.td)
mlir_tablegen(src/dialect/GloinDialect.h.inc -gen-dialect-decls ${GLOIN_DIALECT_INCLUDE})
mlir_tablegen(src/dialect/GloinDialect.cpp.inc -gen-dialect-defs ${GLOIN_DIALECT_INCLUDE})
add_public_tablegen_target(GloinDialectIncGen)

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinTypes.td)
mlir_tablegen(src/dialect/GloinEnums.h.inc -gen-enum-decls ${GLOIN_DIALECT_INCLUDE})
mlir_tablegen(src/dialect/GloinEnums.cpp.inc -gen-enum-defs ${GLOIN_DIALECT_INCLUDE})
mlir_tablegen(src/dialect/GloinTypes.h.inc -gen-typedef-decls -typedefs-dialect=gloin ${GLOIN_DIALECT_INCLUDE})
mlir_tablegen(src/dialect/GloinTypes.cpp.inc -gen-typedef-defs -typedefs-dialect=gloin ${GLOIN_DIALECT_INCLUDE})
add_public_tablegen_target(GloinTypesIncGen)

set(LLVM_TARGET_DEFINITIONS src/dialect/GloinOps.td)
mlir_tablegen(src/dialect/GloinOps.h.inc -gen-op-decls ${GLOIN_DIALECT_INCLUDE})
mlir_tablegen(src/dialect/GloinOps.cpp.inc -gen-op-defs ${GLOIN_DIALECT_INCLUDE})
add_public_tablegen_target(GloinOpsIncGen)

add_executable(gloinc main.cpp
        src/lexer.h
        src/lexer.cpp
        src/parser.cpp
        src/parser.h
        src/AST.h
        src/sema.cpp
        src/sema.h
        src/dialect/GloinDialect.cpp
        src/dialect/GloinDialect.h)

add_dependencies(gloinc GloinDialectIncGen GloinTypesIncGen GloinOpsIncGen)

target_link_libraries(gloinc PRIVATE
        MLIRIR
        MLIRSupport
        MLIRParser
        MLIRSideEffectInterfaces
        MLIRControlFlowInterfaces
        MLIRCallInterfaces
        MLIRArithDialect
        MLIRFuncDialect
        MLIRControlFlowDialect
        MLIRMemRefDialect
        MLIRSCFDialect
)


include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

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
    src/codegen.cpp
    src/codegen.h
    src/lexer.cpp 
    src/lexer.h 
    src/parser.cpp 
    src/parser.h 
    src/AST.h 
    src/sema.cpp 
    src/sema.h
    src/dialect/GloinDialect.cpp
    src/dialect/GloinDialect.h
)
add_dependencies(gloinc_test GloinDialectIncGen GloinTypesIncGen GloinOpsIncGen)
target_link_libraries(gloinc_test 
    GTest::gtest_main
    MLIR
    LLVMSupport
    LLVMCore
)

include(GoogleTest)
gtest_discover_tests(gloinc_test)
