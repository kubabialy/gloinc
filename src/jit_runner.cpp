#include "jit_runner.h"

#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/LLVMCommon/LoweringOptions.h"

#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"

#include "llvm/Support/TargetSelect.h"

#include <iostream>

int JitRunner::run(mlir::ModuleOp module) {
    std::cout << "JitRunner::run start\n";
    // Initialize LLVM targets
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    std::cout << "Targets initialized\n";

    // Register the translation from MLIR to LLVM IR
    // mlir::registerLLVMDialectTranslation(*module.getContext());
    std::cout << "Translations registered\n";

    // Create a pass manager to lower dialects to LLVM
    mlir::PassManager pm(module.getContext());

    
    // Lower SCF to ControlFlow first
    pm.addPass(mlir::createSCFToControlFlowPass());
    
    // Lower ControlFlow to LLVM
    pm.addPass(mlir::createConvertControlFlowToLLVMPass());

    // Lower Arith to LLVM
    pm.addPass(mlir::createArithToLLVMConversionPass());
    
    // Lower Func to LLVM
    pm.addPass(mlir::createConvertFuncToLLVMPass());
    
    // Lower MemRef to LLVM
    pm.addPass(mlir::createFinalizeMemRefToLLVMConversionPass());
    
    // Clean up casts
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());

    if (mlir::failed(pm.run(module))) {
        std::cerr << "JIT Lowering failed\n";
        module.dump();
        return -1;
    }

    // Create ExecutionEngine
    mlir::ExecutionEngineOptions engineOptions;
    auto maybeEngine = mlir::ExecutionEngine::create(module, engineOptions);
    
    if (!maybeEngine) {
        std::cerr << "Failed to create ExecutionEngine: " << llvm::toString(maybeEngine.takeError()) << "\n";
        return -1;
    }
    
    auto engine = std::move(maybeEngine.get());
    
    // Invoke 'main'
    // Note: main in Gloin returns i32. In MLIR->LLVM it should be a function returning i32.
    // However, mlir::ExecutionEngine::invoke uses a specific convention.
    // Usually it expects a wrapper or arguments. 
    // If main has no args and returns i32, we might need to handle the return value carefully.
    // invoke() returns llvm::Error. The return value of the function is passed via arguments pointer?
    // Actually, invoke template wrapper is handy.
    
    // Let's try to invoke "main" directly.
    // Typically `main` in C returns int.
    // We need to pass a pointer to store the result?
    
    // engine->invoke("main");
    // But how to get the result?
    
    // Standard convention for ExecutionEngine invoke:
    // It calls the function. If we want return value, we usually wrap it or assume void.
    // Wait, for integer return, invoke<int> might work? No, invoke calls generic packed args.
    
    // Let's wrap main to store result in a global or pointer passed in?
    // Or just rely on the fact that standard main returns exit code.
    
    // For unit testing, it's better if we can get the result.
    
    // Let's look at `invokePacked`. 
    // Or we can define a wrapper function in MLIR that takes a pointer and stores the result of main there.
    
    // But let's assume `main` returns i32.
    
    typedef int (*MainFuncType)();
    
    auto result = engine->lookupPacked("main");
    if (!result) {
        std::cerr << "Could not find main function\n";
        return -1;
    }
    
    // Cast to function pointer
    // Note: This is hacky. `lookupPacked` returns a wrapper usually? 
    // `lookup` returns the raw address.
    
    auto rawResult = engine->lookup("main");
    if (!rawResult) {
        std::cerr << "Could not lookup main\n";
        return -1;
    }
    
    MainFuncType mainFn = reinterpret_cast<MainFuncType>(rawResult.get());
    return mainFn();
}
