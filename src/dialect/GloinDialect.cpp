#include "GloinDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h" // Added for DialectAsmParser/Printer
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/STLExtras.h"
#include <vector>

using namespace mlir;
using namespace gloin;

#include "src/dialect/GloinDialect.cpp.inc"

#include "src/dialect/GloinEnums.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "src/dialect/GloinTypes.cpp.inc"

#define GET_OP_CLASSES
#include "src/dialect/GloinOps.cpp.inc"

void GloinDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "src/dialect/GloinOps.cpp.inc"
      >();
      
  addTypes<
#define GET_TYPEDEF_LIST
#include "src/dialect/GloinTypes.cpp.inc"
      >();
}

namespace {
bool is_payloadless_enum_storage(mlir::Type type) {
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type);
  return record && !record.isOpaque() && record.getBody().size() == 1 &&
         record.getBody().front().isInteger(32);
}

bool matches_storage(mlir::Type source, mlir::Type storage) {
  if (mlir::isa<gloin::GloinPointerType>(source))
    return mlir::isa<mlir::LLVM::LLVMPointerType>(storage);
  if (auto array = mlir::dyn_cast<gloin::GloinArrayType>(source)) {
    auto layout = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(storage);
    return layout && array.getLength() == layout.getNumElements() &&
           matches_storage(array.getElement(), layout.getElementType());
  }
  if (mlir::isa<gloin::GloinSliceType>(source)) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(storage);
    return record && !record.isOpaque() && record.getBody().size() == 2 &&
           mlir::isa<mlir::LLVM::LLVMPointerType>(record.getBody()[0]) &&
           record.getBody()[1].isInteger(64);
  }
  if (mlir::isa<gloin::GloinEnumType>(source))
    return is_payloadless_enum_storage(storage);
  if (mlir::isa<gloin::GloinStructType>(source)) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(storage);
    return record && !record.isOpaque();
  }
  if (mlir::isa<gloin::GloinStringType>(source)) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(storage);
    return record && !record.isOpaque() && record.getBody().size() == 2 &&
           mlir::isa<mlir::LLVM::LLVMPointerType>(record.getBody()[0]) &&
           record.getBody()[1].isInteger(64);
  }
  if (mlir::isa<gloin::GloinErrorType>(source)) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(storage);
    return record && !record.isOpaque() && record.getBody().size() == 2 &&
           mlir::isa<mlir::LLVM::LLVMPointerType>(record.getBody()[0]) &&
           record.getBody()[1].isInteger(64);
  }
  if (auto result = mlir::dyn_cast<gloin::GloinResultType>(source)) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(storage);
    if (!record || record.isOpaque() ||
        record.getBody().size() != (mlir::isa<mlir::NoneType>(result.getValueType()) ? 2 : 3) ||
        !record.getBody()[0].isInteger(1) ||
        !matches_storage(gloin::GloinErrorType::get(source.getContext()),
                         record.getBody().back()))
      return false;
    return mlir::isa<mlir::NoneType>(result.getValueType()) ||
           matches_storage(result.getValueType(), record.getBody()[1]);
  }
  return source == storage;
}

gloin::StructDefinitionOp struct_definition(mlir::Operation *operation,
                                             mlir::Type source) {
  auto nominal = mlir::dyn_cast<gloin::GloinStructType>(source);
  auto module = operation->getParentOfType<mlir::ModuleOp>();
  if (!nominal || !module)
    return {};
  return module.lookupSymbol<gloin::StructDefinitionOp>(nominal.getName());
}

bool can_weaken_source_value(mlir::Type actual, mlir::Type requested) {
  if (actual == requested)
    return true;
  auto from = mlir::dyn_cast<gloin::GloinPointerType>(actual);
  auto to = mlir::dyn_cast<gloin::GloinPointerType>(requested);
  if (from && to)
    return from.getPointee() == to.getPointee() &&
           (!from.getNullable() || to.getNullable()) &&
           (!from.getReadOnly() || to.getReadOnly());
  auto from_slice = mlir::dyn_cast<gloin::GloinSliceType>(actual);
  auto to_slice = mlir::dyn_cast<gloin::GloinSliceType>(requested);
  return from_slice && to_slice && from_slice.getElement() == to_slice.getElement() &&
         (!from_slice.getReadOnly() || to_slice.getReadOnly());
}
} // namespace

bool gloin::matchesSourceStorage(mlir::Type source, mlir::Type storage) {
  return matches_storage(source, storage);
}

mlir::LogicalResult StructDefinitionOp::verify() {
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getLayoutType());
  if (!record || record.isOpaque() ||
      getFieldSourceTypes().size() != record.getBody().size())
    return emitOpError("requires one source field type per struct layout field");
  for (auto [source_field, layout_field] :
       llvm::zip(getFieldSourceTypes(), record.getBody())) {
    auto type = mlir::dyn_cast<mlir::TypeAttr>(source_field);
    if (!type || !matches_storage(type.getValue(), layout_field))
      return emitOpError("has a field source type incompatible with its layout");
  }
  return mlir::success();
}

mlir::LogicalResult FromLayoutOp::verify() {
  if (!matches_storage(getSourceValue().getType(), getValue().getType()))
    return emitOpError("requires a source result matching the input layout");
  if (auto bridge = getValue().getDefiningOp<gloin::ToLayoutOp>();
      bridge && !can_weaken_source_value(bridge.getSourceValue().getType(),
                                         getSourceValue().getType()))
    return emitOpError("cannot retype a known source value through its layout: ")
           << bridge.getSourceValue().getType() << " as " << getSourceValue().getType();
  return mlir::success();
}

mlir::LogicalResult ToLayoutOp::verify() {
  if (!matches_storage(getSourceValue().getType(), getValue().getType()))
    return emitOpError("requires a layout result matching the input source type");
  return mlir::success();
}

mlir::LogicalResult ErrorLiteralOp::verify() {
  auto source = mlir::isa<gloin::GloinErrorType>(getValue().getType());
  if ((!source && getValue().getType() != getLayoutType()) ||
      !matches_storage(gloin::GloinErrorType::get(getContext()), getLayoutType()) ||
      (source && !mlir::isa<gloin::GloinStringType>(getMessage().getType())) ||
      (!source && getMessage().getType() != getLayoutType()))
    return emitOpError("requires a checked string message and error layout");
  return mlir::success();
}

mlir::LogicalResult ErrorMessageOp::verify() {
  if (!matches_storage(gloin::GloinStringType::get(getContext()), getLayoutType()))
    return emitOpError("requires a string layout");
  if (mlir::isa<gloin::GloinErrorType>(getError().getType())) {
    if (!mlir::isa<gloin::GloinStringType>(getMessage().getType()))
      return emitOpError("requires a source string result");
  } else if (getError().getType() != getMessage().getType() ||
             getMessage().getType() != getLayoutType() ||
             !matches_storage(gloin::GloinErrorType::get(getContext()),
                              getError().getType())) {
    return emitOpError("requires matching error and string storage layouts");
  }
  return mlir::success();
}

mlir::LogicalResult ResultSuccessOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinResultType>(getValue().getType());
  if ((!source && getValue().getType() != getLayoutType()) ||
      (source && !matches_storage(source, getLayoutType())))
    return emitOpError("requires a result source type matching its layout");
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getLayoutType());
  if (!record || record.isOpaque() ||
      getPayload().size() != (record.getBody().size() == 2 ? 0u : 1u))
    return emitOpError("has the wrong success payload count");
  if (!getPayload().empty() && getPayload()[0].getType() !=
                                  (source ? source.getValueType() : record.getBody()[1]))
    return emitOpError("has the wrong success payload type");
  return mlir::success();
}

mlir::LogicalResult ResultFailureOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinResultType>(getValue().getType());
  auto layout = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getLayoutType());
  if (!layout || layout.isOpaque() ||
      (layout.getBody().size() != 2 && layout.getBody().size() != 3) ||
      !matches_storage(gloin::GloinErrorType::get(getContext()),
                       layout.getBody().back()) ||
      (!source && getValue().getType() != getLayoutType()) ||
      (source && !matches_storage(source, getLayoutType())) ||
      getError().getType() != (source ? mlir::Type(gloin::GloinErrorType::get(getContext()))
                                     : layout.getBody().back()))
    return emitOpError("requires an error payload and matching result layout");
  return mlir::success();
}

mlir::LogicalResult ResultIsErrorOp::verify() {
  if (!mlir::isa<gloin::GloinResultType>(getValue().getType())) {
    auto layout = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getValue().getType());
    if (!layout || layout.isOpaque() ||
        (layout.getBody().size() != 2 && layout.getBody().size() != 3) ||
        !layout.getBody()[0].isInteger(1))
      return emitOpError("requires a result value");
  }
  return mlir::success();
}

mlir::LogicalResult ResultValueOp::verify() {
  if (auto result = mlir::dyn_cast<gloin::GloinResultType>(getOutcome().getType())) {
    if (mlir::isa<mlir::NoneType>(result.getValueType()) ||
        getValue().getType() != result.getValueType() ||
        !matches_storage(result.getValueType(), getLayoutType()))
      return emitOpError("requires the result's non-void payload type");
  } else if (auto layout = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getOutcome().getType())) {
    if (layout.isOpaque() || layout.getBody().size() != 3 ||
        getValue().getType() != layout.getBody()[1] ||
        getLayoutType() != layout.getBody()[1])
      return emitOpError("requires the result payload storage type");
  } else {
    return emitOpError("requires a result operand");
  }
  return mlir::success();
}

mlir::LogicalResult ResultErrorOp::verify() {
  if (mlir::isa<gloin::GloinResultType>(getOutcome().getType())) {
    if (!mlir::isa<gloin::GloinErrorType>(getError().getType()) ||
        !matches_storage(getError().getType(), getLayoutType()))
      return emitOpError("requires a source error result");
  } else if (auto layout = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getOutcome().getType())) {
    if (layout.isOpaque() ||
        (layout.getBody().size() != 2 && layout.getBody().size() != 3) ||
        getError().getType() != layout.getBody().back() ||
        getLayoutType() != layout.getBody().back())
      return emitOpError("requires the error storage type");
  } else {
    return emitOpError("requires a result operand");
  }
  return mlir::success();
}

mlir::LogicalResult AbiCallOp::verify() {
  auto module = (*this)->getParentOfType<mlir::ModuleOp>();
  auto function = module ? module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(getCallee())
                         : mlir::LLVM::LLVMFuncOp{};
  if (!function || !function.isExternal())
    return emitOpError("requires a declared external LLVM runtime function");
  auto signature = function.getFunctionType();
  if (signature.isVarArg() || signature.getNumParams() != getArgs().size())
    return emitOpError("requires the runtime function's exact argument count");
  for (auto [index, argument] : llvm::enumerate(getArgs()))
    if (argument.getType() != signature.getParamType(index))
      return emitOpError("requires the runtime function's exact argument types");
  auto result_type = signature.getReturnType();
  if (mlir::isa<mlir::LLVM::LLVMVoidType>(result_type)) {
    if (!getResults().empty())
      return emitOpError("void runtime function cannot return a value");
  } else if (getResults().size() != 1 || getResults()[0].getType() != result_type) {
    return emitOpError("requires the runtime function's exact result type");
  }
  return mlir::success();
}

mlir::LogicalResult EnumConstantOp::verify() {
  if (!mlir::isa<gloin::GloinEnumType>(getSourceType()) ||
      !is_payloadless_enum_storage(getLayoutType()) ||
      (getValue().getType() != getSourceType() &&
       getValue().getType() != getLayoutType()))
    return emitOpError("requires a nominal enum result and one-field u32 storage layout");
  return mlir::success();
}

mlir::LogicalResult EnumCompareOp::verify() {
  if (getLhs().getType() != getRhs().getType() ||
      !mlir::isa<gloin::GloinEnumType>(getSourceType()) ||
      (getLhs().getType() != getSourceType() &&
       !is_payloadless_enum_storage(getLhs().getType())))
    return emitOpError("requires matching operands of the nominal enum type");
  return mlir::success();
}

mlir::LogicalResult CheckedIntegerBinaryOp::verify() {
  auto type = mlir::dyn_cast<mlir::IntegerType>(getLhs().getType());
  if (!type || type != getRhs().getType() || type != getResult().getType() ||
      (type.getWidth() != 8 && type.getWidth() != 16 && type.getWidth() != 32 &&
       type.getWidth() != 64))
    return emitOpError("requires matching supported integer operands and result");
  auto kind = getKind();
  if (kind != "+" && kind != "-" && kind != "*" && kind != "/" && kind != "%")
    return emitOpError("has an unsupported integer operator");
  return mlir::success();
}

mlir::LogicalResult CheckedFloatBinaryOp::verify() {
  auto type = getLhs().getType();
  if ((!type.isF32() && !type.isF64()) || type != getRhs().getType() ||
      type != getResult().getType())
    return emitOpError("requires matching f32 or f64 operands and result");
  auto kind = getKind();
  if (kind != "+" && kind != "-" && kind != "*" && kind != "/")
    return emitOpError("has an unsupported floating operator");
  return mlir::success();
}

mlir::LogicalResult CheckedIntegerCompareOp::verify() {
  auto type = mlir::dyn_cast<mlir::IntegerType>(getLhs().getType());
  if (!type || type != getRhs().getType() ||
      (type.getWidth() != 1 && type.getWidth() != 8 && type.getWidth() != 16 &&
       type.getWidth() != 32 && type.getWidth() != 64))
    return emitOpError("requires matching supported integer operands");
  auto kind = getKind();
  if (kind != "==" && kind != "!=" && kind != "<" && kind != "<=" &&
      kind != ">" && kind != ">=")
    return emitOpError("has an unsupported integer comparison");
  if (type.getWidth() == 1 && kind != "==" && kind != "!=")
    return emitOpError("allows only equality comparisons for bool");
  return mlir::success();
}

mlir::LogicalResult CheckedFloatCompareOp::verify() {
  auto type = getLhs().getType();
  if ((!type.isF32() && !type.isF64()) || type != getRhs().getType())
    return emitOpError("requires matching f32 or f64 operands");
  auto kind = getKind();
  if (kind != "==" && kind != "!=" && kind != "<" && kind != "<=" &&
      kind != ">" && kind != ">=")
    return emitOpError("has an unsupported floating comparison");
  return mlir::success();
}

mlir::LogicalResult ArrayLiteralOp::verify() {
  auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(getLayoutType());
  auto source = mlir::dyn_cast<gloin::GloinArrayType>(getSourceType());
  bool typed = getValue().getType() == getSourceType();
  bool layout = getValue().getType() == getLayoutType();
  if (!array || !source || source.getLength() != array.getNumElements() ||
      !matches_storage(source, array) || (!typed && !layout))
    return emitOpError("requires a matching Gloin fixed-array source type");
  if (getElements().size() != array.getNumElements())
    return emitOpError("requires one element per fixed-array slot");
  for (auto element : getElements())
    if (element.getType() != (typed ? source.getElement() : array.getElementType()))
      return emitOpError("has an element with the wrong type");
  return mlir::success();
}

mlir::LogicalResult RepeatArrayOp::verify() {
  auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(getLayoutType());
  auto source = mlir::dyn_cast<gloin::GloinArrayType>(getSourceType());
  bool typed = getValue().getType() == getSourceType();
  bool layout = getValue().getType() == getLayoutType();
  if (!array || !source || source.getLength() != array.getNumElements() ||
      !matches_storage(source, array) || (!typed && !layout) ||
      getElement().getType() != (typed ? source.getElement() : array.getElementType()))
    return emitOpError("requires one matching element and a fixed-array result");
  return mlir::success();
}

mlir::LogicalResult StructLiteralOp::verify() {
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getLayoutType());
  bool typed = getValue().getType() == getSourceType();
  bool layout = getValue().getType() == getLayoutType();
  if (!mlir::isa<gloin::GloinStructType>(getSourceType()) || !record ||
      record.isOpaque() || getFields().size() != record.getBody().size() ||
      getIndicesAttr().asArrayRef().size() != getFields().size() ||
      getFieldSourceTypes().size() != record.getBody().size() ||
      (!typed && !layout))
    return emitOpError("requires one initializer per struct field");
  if (typed) {
    auto definition = struct_definition(getOperation(), getSourceType());
    if (!definition || getLayoutType() != definition.getLayoutType() ||
        getFieldSourceTypes() != definition.getFieldSourceTypes())
      return emitOpError("disagrees with its nominal struct definition");
  }
  for (auto [source_field, layout_field] :
       llvm::zip(getFieldSourceTypes(), record.getBody())) {
    auto type = mlir::dyn_cast<mlir::TypeAttr>(source_field);
    if (!type || !matches_storage(type.getValue(), layout_field))
      return emitOpError("has a field source type incompatible with its layout");
  }
  std::vector<bool> seen(record.getBody().size());
  for (auto [field, index] : llvm::zip(getFields(), getIndicesAttr().asArrayRef())) {
    if (index < 0 || static_cast<size_t>(index) >= seen.size() || seen[index] ||
        field.getType() != (typed
                               ? mlir::cast<mlir::TypeAttr>(getFieldSourceTypes()[index])
                                     .getValue()
                               : record.getBody()[index]))
      return emitOpError("has an invalid, duplicate, or mistyped field index");
    seen[index] = true;
  }
  return mlir::success();
}

mlir::LogicalResult ZeroedArrayOp::verify() {
  auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(getLayoutType());
  auto source = mlir::dyn_cast<gloin::GloinArrayType>(getSourceType());
  if (!array || !source || source.getLength() != array.getNumElements() ||
      !matches_storage(source, array) ||
      (getValue().getType() != getSourceType() &&
       getValue().getType() != getLayoutType()))
    return emitOpError("requires a fixed-array result type");
  return mlir::success();
}

mlir::LogicalResult StringLiteralOp::verify() {
  if (!mlir::isa<gloin::GloinStringType>(getSourceType()) ||
      !matches_storage(getSourceType(), getLayoutType()) ||
      !mlir::isa<mlir::LLVM::LLVMPointerType>(getData().getType()) ||
      (getValue().getType() != getSourceType() &&
       getValue().getType() != getLayoutType()))
    return emitOpError("requires a Gloin string with pointer and length storage");
  return mlir::success();
}

mlir::LogicalResult NullOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  if (!source || !source.getNullable() ||
      (getValue().getType() != getSourceType() &&
       !mlir::isa<mlir::LLVM::LLVMPointerType>(getValue().getType())))
    return emitOpError("requires a nullable Gloin pointer type and pointer result");
  return mlir::success();
}

mlir::LogicalResult ArenaTypedPointerOp::verify() {
  auto target = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  auto raw = mlir::dyn_cast<gloin::GloinPointerType>(getStorage().getType());
  bool typed = raw && target && raw.getPointee().isInteger(8) &&
               !raw.getReadOnly() && !target.getReadOnly() &&
               getPointer().getType() == getSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getStorage().getType()) &&
                getPointer().getType() == getStorage().getType();
  if (!target || (!typed && !layout) ||
      !mlir::LLVM::isCompatibleType(getElementType()) ||
      !matches_storage(target.getPointee(), getElementType()))
    return emitOpError("requires raw byte storage and a typed pointer matching its element layout: ")
           << getStorage().getType() << ", " << getPointer().getType() << ", "
           << getSourceType() << ", " << getElementType();
  return mlir::success();
}

mlir::LogicalResult RawPlaceOp::verify() {
  auto target = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  auto raw = mlir::dyn_cast<gloin::GloinPointerType>(getStorage().getType());
  bool typed = raw && target && raw.getPointee().isInteger(8) &&
               raw.getNullable() && target.getNullable() &&
               !raw.getReadOnly() && !target.getReadOnly() &&
               getPointer().getType() == getSourceType() &&
               getInitial().getType() == getElementSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getStorage().getType()) &&
                getPointer().getType() == getStorage().getType() &&
                getInitial().getType() == getElementType();
  if (!target || !target.getNullable() || (!typed && !layout) ||
      !mlir::LLVM::isCompatibleType(getElementType()) ||
      !matches_storage(target.getPointee(), getElementType()) ||
      target.getPointee() != getElementSourceType())
    return emitOpError("requires writable raw byte storage and a matching nullable typed pointer");
  return mlir::success();
}

mlir::LogicalResult RawPaddingOp::verify() {
  auto raw = mlir::dyn_cast<gloin::GloinPointerType>(getStorage().getType());
  if (((raw && raw.getPointee().isInteger(8) && raw.getNullable() &&
       !raw.getReadOnly()) ||
      mlir::isa<mlir::LLVM::LLVMPointerType>(getStorage().getType())) &&
      mlir::LLVM::isCompatibleType(getElementType()))
    return mlir::success();
  return emitOpError("requires writable nullable raw byte storage");
}

mlir::LogicalResult PointerOffsetOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  bool typed = getBase().getType() == getSourceType() &&
               getAddress().getType() == getSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                getAddress().getType() == getBase().getType();
  if (!source || (!typed && !layout) || !getOffset().getType().isInteger(64) ||
      !mlir::LLVM::isCompatibleType(getElementType()) ||
      !matches_storage(source.getPointee(), getElementType()))
    return emitOpError("requires matching source pointer base/result, i64 offset, and pointee layout");
  return mlir::success();
}

mlir::LogicalResult RequireNonNullOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getPointer().getType()) &&
                getCheckedPointer().getType() == getPointer().getType();
  bool typed = source && getPointer().getType() == getSourceType() &&
               getCheckedPointer().getType() == gloin::GloinPointerType::get(
                   getContext(), source.getPointee(), false, source.getReadOnly());
  if (!source || !source.getNullable() || (!typed && !layout))
    return emitOpError("requires a nullable source pointer and non-null result with matching pointee and access");
  return mlir::success();
}

mlir::LogicalResult PointerCompareOp::verify() {
  bool typed = getLhs().getType() == getSourceType() &&
               getRhs().getType() == getSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getLhs().getType()) &&
                getLhs().getType() == getRhs().getType();
  if (!mlir::isa<gloin::GloinPointerType>(getSourceType()) || (!typed && !layout))
    return emitOpError("requires identical source pointer operand types");
  return mlir::success();
}

mlir::LogicalResult ArrayElementAddressOp::verify() {
  auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(getArrayType());
  auto source = mlir::dyn_cast<gloin::GloinArrayType>(getSourceType());
  auto base = mlir::dyn_cast<gloin::GloinPointerType>(getBase().getType());
  auto address = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool typed_address = base && address && source &&
                       base.getPointee() == getSourceType() &&
                       !base.getNullable() &&
                       address.getPointee() == source.getElement() &&
                       !address.getNullable() &&
                       address.getReadOnly() == base.getReadOnly();
  bool layout_address = mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                        getAddress().getType() == getBase().getType();
  if (!array || !source || source.getLength() != array.getNumElements() ||
      !matches_storage(source, array) ||
      (!typed_address && !layout_address))
    return emitOpError("requires a fixed-array type and matching pointer base/result");
  return mlir::success();
}

mlir::LogicalResult SliceFromArrayOp::verify() {
  auto array = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(getArrayType());
  auto source = mlir::dyn_cast<gloin::GloinSliceType>(getSourceType());
  auto base = mlir::dyn_cast<gloin::GloinPointerType>(getBase().getType());
  bool typed = base && array && source &&
               base.getPointee() == gloin::GloinArrayType::get(
                   getContext(), source.getElement(), array.getNumElements()) &&
               !base.getNullable() && source.getReadOnly() == base.getReadOnly() &&
               getValue().getType() == getSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                getValue().getType() == getLayoutType();
  if (!array || !source || !matches_storage(source, getLayoutType()) ||
      !matches_storage(source.getElement(), array.getElementType()) ||
      (!typed && !layout))
    return emitOpError("requires matching array storage, slice type, and borrow capability");
  return mlir::success();
}

mlir::LogicalResult SliceSubrangeOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinSliceType>(getSourceType());
  bool typed = getBase().getType() == getSourceType() &&
               getValue().getType() == getSourceType();
  bool layout = getBase().getType() == getLayoutType() &&
                getValue().getType() == getLayoutType();
  if (!source || !matches_storage(source, getLayoutType()) ||
      !matches_storage(source.getElement(), getElementType()) ||
      (!typed && !layout))
    return emitOpError("requires a matching source slice and element layout");
  return mlir::success();
}

mlir::LogicalResult SliceFromPointerOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinSliceType>(getSourceType());
  auto base = mlir::dyn_cast<gloin::GloinPointerType>(getBase().getType());
  bool typed = source && base && base.getPointee() == source.getElement() &&
               base.getReadOnly() == source.getReadOnly() &&
               getValue().getType() == getSourceType();
  bool layout = source && mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                getValue().getType() == getLayoutType();
  if (!source || !matches_storage(source, getLayoutType()) ||
      !matches_storage(source.getElement(), getElementType()) ||
      (!typed && !layout))
    return emitOpError("requires matching pointer storage and slice borrow capability");
  return mlir::success();
}

mlir::LogicalResult SliceElementAddressOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinSliceType>(getSourceType());
  auto address = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool typed = source && getBase().getType() == getSourceType() && address &&
               address.getPointee() == source.getElement() && !address.getNullable() &&
               address.getReadOnly() == source.getReadOnly();
  bool layout = getBase().getType() == getLayoutType() &&
                mlir::isa<mlir::LLVM::LLVMPointerType>(getAddress().getType());
  if (!source || !matches_storage(source, getLayoutType()) ||
      !matches_storage(source.getElement(), getElementType()) ||
      (!typed && !layout))
    return emitOpError("requires a slice and matching element address capability");
  return mlir::success();
}

mlir::LogicalResult SliceLengthOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinSliceType>(getSourceType());
  if (!source || !matches_storage(source, getLayoutType()) ||
      (getBase().getType() != getSourceType() &&
       getBase().getType() != getLayoutType()))
    return emitOpError("requires a matching source slice");
  return mlir::success();
}

mlir::LogicalResult ArenaFillOp::verify() {
  auto source = mlir::dyn_cast<gloin::GloinPointerType>(getSourceType());
  bool typed = getBase().getType() == getSourceType() &&
               getFill().getType() == getElementSourceType();
  bool layout = mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                getFill().getType() == getElementType();
  if (!source || source.getNullable() || source.getReadOnly() ||
      source.getPointee() != getElementSourceType() ||
      !matches_storage(getElementSourceType(), getElementType()) ||
      (!typed && !layout))
    return emitOpError("requires writable typed arena storage and matching initialized value");
  return mlir::success();
}

mlir::LogicalResult StackAllocOp::verify() {
  auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool source_address = pointer && pointer.getPointee() == getSourceType() &&
                        !pointer.getNullable() && !pointer.getReadOnly();
  if ((!source_address &&
       !mlir::isa<mlir::LLVM::LLVMPointerType>(getAddress().getType())) ||
      !mlir::LLVM::isCompatibleType(getElementType()) ||
      !matches_storage(getSourceType(), getElementType()))
    return emitOpError("requires a writable source address and matching storage layout");
  return mlir::success();
}

mlir::LogicalResult LoadOp::verify() {
  auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool source_address = pointer && pointer.getPointee() == getSourceType() &&
                        !pointer.getNullable();
  bool layout_address = mlir::isa<mlir::LLVM::LLVMPointerType>(getAddress().getType());
  if ((!source_address && !layout_address) ||
      !mlir::LLVM::isCompatibleType(getLayoutType()) ||
      !matches_storage(getSourceType(), getLayoutType()) ||
      (source_address && getValue().getType() != getSourceType()) ||
      (layout_address && getValue().getType() != getLayoutType()))
    return emitOpError("requires a source address with the loaded pointee type and matching layout");
  return mlir::success();
}

mlir::LogicalResult StoreOp::verify() {
  auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool source_address = pointer && pointer.getPointee() == getSourceType() &&
                        !pointer.getNullable() && !pointer.getReadOnly();
  bool layout_address = mlir::isa<mlir::LLVM::LLVMPointerType>(getAddress().getType());
  if ((!source_address && !layout_address) ||
      (source_address && getValue().getType() != getSourceType()) ||
      (layout_address && (!mlir::LLVM::isCompatibleType(getValue().getType()) ||
                          !matches_storage(getSourceType(), getValue().getType()))))
    return emitOpError("requires a writable source address and matching stored pointee type");
  return mlir::success();
}

mlir::LogicalResult FieldAddressOp::verify() {
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getStructType());
  auto base = mlir::dyn_cast<gloin::GloinPointerType>(getBase().getType());
  auto address = mlir::dyn_cast<gloin::GloinPointerType>(getAddress().getType());
  bool typed_address = base && address &&
                       base.getPointee() == getSourceType() &&
                       address.getPointee() == getFieldSourceType() &&
                       !base.getNullable() && !address.getNullable() &&
                       (!base.getReadOnly() || address.getReadOnly()) && record &&
                       !record.isOpaque() && getIndex() >= 0 &&
                       static_cast<size_t>(getIndex()) < record.getBody().size() &&
                       matches_storage(getFieldSourceType(), record.getBody()[getIndex()]);
  bool layout_address = mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
                        getAddress().getType() == getBase().getType();
  if (!mlir::isa<gloin::GloinStructType>(getSourceType()) || !record ||
      record.isOpaque() || getIndex() < 0 ||
      static_cast<size_t>(getIndex()) >= record.getBody().size() ||
      !matches_storage(getSourceType(), record) ||
      !matches_storage(getFieldSourceType(), record.getBody()[getIndex()]) ||
      (!typed_address && !layout_address))
    return emitOpError("requires a valid struct field index and matching pointer types");
  if (base || address) {
    auto definition = struct_definition(getOperation(), getSourceType());
    if (!definition)
      return emitOpError("has no nominal struct definition for ") << getSourceType();
    if (getStructType() != definition.getLayoutType())
      return emitOpError("disagrees with its nominal struct definition layout for ")
             << getSourceType();
    auto expected =
        mlir::cast<mlir::TypeAttr>(definition.getFieldSourceTypes()[getIndex()])
            .getValue();
    if (!can_weaken_source_value(expected, getFieldSourceType()))
      return emitOpError("disagrees with its nominal struct definition: field ")
             << getIndex() << " of " << getSourceType() << " is " << expected
             << ", but the address uses " << getFieldSourceType();
  }
  return mlir::success();
}

mlir::LogicalResult ExtractFieldOp::verify() {
  auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(getLayoutType());
  bool typed = getAggregate().getType() == getSourceType();
  bool layout = getAggregate().getType() == getLayoutType();
  if (!mlir::isa<gloin::GloinStructType>(getSourceType()) || !record ||
      record.isOpaque() || getIndex() < 0 ||
      static_cast<size_t>(getIndex()) >= record.getBody().size() ||
      !matches_storage(getFieldSourceType(), record.getBody()[getIndex()]) ||
      (!typed && !layout) ||
      getValue().getType() != (typed ? getFieldSourceType()
                                    : record.getBody()[getIndex()]))
    return emitOpError("requires a valid struct field index and matching result type");
  if (typed) {
    auto definition = struct_definition(getOperation(), getSourceType());
    if (!definition || getLayoutType() != definition.getLayoutType() ||
        !can_weaken_source_value(
            mlir::cast<mlir::TypeAttr>(definition.getFieldSourceTypes()[getIndex()])
                .getValue(),
            getFieldSourceType()))
      return emitOpError("disagrees with its nominal struct definition");
  }
  return mlir::success();
}

mlir::LogicalResult ConstantOp::verify() {
  auto value = mlir::dyn_cast<mlir::TypedAttr>(getValueAttr());
  if (!value || !mlir::isa<mlir::IntegerAttr, mlir::FloatAttr>(value) ||
      value.getType() != getResult().getType())
    return emitOpError("requires a matching integer or floating attribute");
  return mlir::success();
}
