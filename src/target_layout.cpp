#include "target_layout.h"
#include "mlir/Target/LLVMIR/TypeToLLVM.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

llvm::Expected<TargetLayout> native_target_layout() {
    struct Cached {
        std::optional<TargetLayout> value;
        std::string error;
        Cached() {
            if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter()) {
                error = "Cannot initialize native target";
                return;
            }
            auto builder = llvm::orc::JITTargetMachineBuilder::detectHost();
            if (!builder) {
                error = llvm::toString(builder.takeError());
                return;
            }
            auto machine = builder->createTargetMachine();
            if (!machine) {
                error = llvm::toString(machine.takeError());
                return;
            }
            value = TargetLayout{(*machine)->getTargetTriple().str(),
                                 (*machine)->createDataLayout().getStringRepresentation()};
        }
    };
    static const Cached target;
    if (!target.value)
        return llvm::createStringError(target.error);
    return *target.value;
}

llvm::Expected<TypeLayout> measure_type_layout(mlir::Type type, const llvm::DataLayout &layout) {
    llvm::LLVMContext context;
    mlir::LLVM::TypeToLLVMIRTranslator translator(context);
    auto *translated = translator.translateType(type);
    if (!translated || !translated->isSized())
        return llvm::createStringError("Cannot measure an unsized type");
    auto size = layout.getTypeAllocSize(translated);
    if (size.isScalable())
        return llvm::createStringError("Scalable types are unsupported");
    TypeLayout result{size.getFixedValue(), layout.getABITypeAlign(translated).value(), {}};
    if (auto *structure = llvm::dyn_cast<llvm::StructType>(translated)) {
        auto *record = layout.getStructLayout(structure);
        for (unsigned i = 0; i < structure->getNumElements(); ++i)
            result.field_offsets.push_back(record->getElementOffset(i));
    }
    return result;
}
