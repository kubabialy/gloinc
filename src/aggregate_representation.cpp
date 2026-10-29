#include "aggregate_representation.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "target_layout.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <functional>

mlir::LogicalResult preserve_aggregate_bytes(mlir::ModuleOp module) {
    auto attribute = module->getAttrOfType<mlir::StringAttr>("llvm.data_layout");
    std::string layout_text;
    if (attribute) {
        layout_text = attribute.getValue().str();
    } else {
        auto target = native_target_layout();
        if (!target)
            return module.emitError(llvm::toString(target.takeError()));
        layout_text = target->data_layout;
    }
    auto layout = llvm::DataLayout::parse(layout_text);
    if (!layout)
        return module.emitError(llvm::toString(layout.takeError()));

    struct Representation {
        mlir::Type type;
        llvm::SmallVector<int64_t> indices;
    };
    llvm::DenseMap<mlir::Type, Representation> representations;
    bool failed = false;
    std::function<mlir::Type(mlir::Type)> convert = [&](mlir::Type type) -> mlir::Type {
        if (auto found = representations.find(type); found != representations.end())
            return found->second.type;
        if (auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(type)) {
            auto result = mlir::LLVM::LLVMArrayType::get(convert(array.getElementType()),
                                                         array.getNumElements());
            representations[type] = {result, {}};
            return result;
        }
        auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type);
        if (!record || record.isOpaque())
            return type;
        auto measured = measure_type_layout(record, *layout);
        if (!measured) {
            module.emitError(llvm::toString(measured.takeError()));
            failed = true;
            return type;
        }
        llvm::SmallVector<mlir::Type> fields;
        llvm::SmallVector<int64_t> indices;
        uint64_t end = 0;
        auto padding = [&](uint64_t next) {
            if (next > end)
                fields.push_back(mlir::LLVM::LLVMArrayType::get(
                    mlir::IntegerType::get(module.getContext(), 8), next - end));
        };
        for (auto [index, field] : llvm::enumerate(record.getBody())) {
            padding(measured->field_offsets[index]);
            indices.push_back(fields.size());
            fields.push_back(convert(field));
            auto field_layout = measure_type_layout(field, *layout);
            if (!field_layout) {
                module.emitError(llvm::toString(field_layout.takeError()));
                failed = true;
                return type;
            }
            end = measured->field_offsets[index] + field_layout->size;
        }
        padding(measured->size);
        mlir::Type result = type;
        if (llvm::ArrayRef(fields) != record.getBody())
            result = mlir::LLVM::LLVMStructType::getLiteral(module.getContext(), fields,
                                                            record.isPacked());
        auto padded = measure_type_layout(result, *layout);
        if (!padded) {
            module.emitError(llvm::toString(padded.takeError()));
            failed = true;
            return type;
        }
        if (padded->size != measured->size || padded->alignment != measured->alignment) {
            module.emitError("explicit padding changed object size or alignment: ")
                << record << " -> " << result << " (" << measured->size << ", "
                << measured->alignment << ") -> (" << padded->size << ", " << padded->alignment
                << ")";
            failed = true;
        }
        for (auto [index, physical] : llvm::enumerate(indices))
            if (padded->field_offsets[physical] != measured->field_offsets[index]) {
                module.emitError("explicit padding changed a source field offset: ")
                    << record << " field " << index << " from " << measured->field_offsets[index]
                    << " to " << padded->field_offsets[physical];
                failed = true;
            }
        representations[type] = {result, std::move(indices)};
        return result;
    };

    // A foreign ABI is not the Gloin aggregate calling convention. Current
    // runtime interfaces use scalars, pointers, and padding-free descriptors.
    // A future aggregate FFI requires a declared adapter, not silent retyping.
    for (auto function : module.getOps<mlir::LLVM::LLVMFuncOp>()) {
        if (!function.isExternal())
            continue;
        function.getFunctionType().walk([&](mlir::Type type) {
            if (convert(type) != type) {
                function.emitError(
                    "padding-bearing aggregate at a foreign ABI requires an explicit adapter");
                failed = true;
            }
        });
    }
    if (failed)
        return mlir::failure();

    // Intrinsic result tuples (e.g. {integer, overflow}) have LLVM-defined
    // signatures and are not Gloin objects. The compiler only extracts them.
    llvm::SmallPtrSet<mlir::Operation *, 16> intrinsic_tuples;
    module.walk([&](mlir::Operation *op) {
        if (!op->getName().getStringRef().starts_with("llvm.intr."))
            return;
        for (auto result : op->getResults()) {
            if (!mlir::isa<mlir::LLVM::LLVMStructType>(result.getType()))
                continue;
            intrinsic_tuples.insert(op);
            for (auto *user : result.getUsers())
                if (!mlir::isa<mlir::LLVM::ExtractValueOp>(user)) {
                    op->emitError("LLVM intrinsic tuple cannot cross a Gloin object boundary");
                    failed = true;
                }
        }
    });
    if (failed)
        return mlir::failure();

    auto position = [&](mlir::Type type, llvm::ArrayRef<int64_t> old) {
        llvm::SmallVector<int64_t> result;
        for (auto index : old) {
            if (auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type)) {
                (void)convert(record);
                result.push_back(representations.at(record).indices[index]);
                type = record.getBody()[index];
            } else {
                result.push_back(index);
                type = mlir::cast<mlir::LLVM::LLVMArrayType>(type).getElementType();
            }
        }
        return result;
    };
    // Remap indices against the original schemas before changing any types.
    module.walk([&](mlir::Operation *op) {
        if (auto extract = mlir::dyn_cast<mlir::LLVM::ExtractValueOp>(op)) {
            if (!intrinsic_tuples.contains(extract.getContainer().getDefiningOp()))
                extract.setPosition(
                    position(extract.getContainer().getType(), extract.getPosition()));
        } else if (auto insert = mlir::dyn_cast<mlir::LLVM::InsertValueOp>(op)) {
            insert.setPosition(position(insert.getContainer().getType(), insert.getPosition()));
        } else if (auto gep = mlir::dyn_cast<mlir::LLVM::GEPOp>(op)) {
            auto type = gep.getElemType();
            llvm::SmallVector<int32_t> indices(gep.getRawConstantIndices());
            // The first GEP index steps through whole objects, not their fields.
            for (size_t i = 1; i < indices.size(); ++i) {
                if (auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type)) {
                    (void)convert(record);
                    auto original = indices[i];
                    indices[i] = representations.at(record).indices[original];
                    type = record.getBody()[original];
                } else if (auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(type)) {
                    type = array.getElementType();
                }
            }
            gep.setRawConstantIndices(indices);
        }
    });
    if (failed)
        return mlir::failure();
    mlir::AttrTypeReplacer replacer;
    replacer.addReplacement([&](mlir::Type type) -> std::optional<mlir::Type> {
        auto replacement = convert(type);
        return replacement == type ? std::nullopt : std::optional<mlir::Type>(replacement);
    });
    module.walk([&](mlir::Operation *op) {
        if (!intrinsic_tuples.contains(op))
            replacer.replaceElementsIn(op, true, false, true);
    });
    return mlir::failure(failed);
}
