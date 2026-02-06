## Gloinc Project Guidelines

This file provides guidelines for working with the Gloinc codebase.

### Build & Test Commands

- **Configure:** `cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug`
- **Build:** `cmake --build cmake-build-debug`
- **Run all tests:** `cd cmake-build-debug && ctest`
- **Run a single test:** `ctest -R TestSuiteName.TestName` (e.g., `ctest -R LexerTest.TestKeywords`)

### Code Style

- **Language:** C++23.
- **Formatting:** Follow existing code style. Use `snake_case` for functions/methods and `PascalCase` for classes/structs.
- **Includes:** Use `#include "path/to/header.h"`.
- **Types:** Use standard integer types (`int64_t`, etc.) and `std::string_view` where possible.
- **Error Handling:** Errors should be handled gracefully; avoid crashing the compiler.
- **Dependencies:** The project uses MLIR/LLVM for compilation and GoogleTest for testing.

### MLIR/LLVM Configuration

When working with MLIR and LLVM for JIT compilation, link against the following libraries:

*   `MLIRFuncToLLVM`
*   `MLIRControlFlowToLLVM`
*   `MLIRReconcileUnrealizedCasts`
*   `MLIRArithToLLVM`
*   `MLIRFinalizeMemRefToLLVM`
*   `MLIRTargetLLVMIRExport`
