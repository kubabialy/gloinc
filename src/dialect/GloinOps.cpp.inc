/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Op Definitions                                                             *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: GloinOps.td                                                          *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifdef GET_OP_LIST
#undef GET_OP_LIST

::gloin::AsyncCallOp,
::gloin::AwaitOp,
::gloin::ConstantOp,
::gloin::DeferOp,
::gloin::IfOp,
::gloin::JoinOp,
::gloin::SpawnOp,
::gloin::StructDefOp,
::gloin::UnlessOp,
::gloin::YieldOp
#endif  // GET_OP_LIST

#ifdef GET_OP_CLASSES
#undef GET_OP_CLASSES


//===----------------------------------------------------------------------===//
// Local Utility Method Definitions
//===----------------------------------------------------------------------===//

namespace gloin {

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps0(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((true))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be variadic of any type, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps1(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((::llvm::isa<::gloin::GloinDeferredType>(type)))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be Handle for async operations, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps2(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((::llvm::isa<::gloin::GloinResultType>(type)))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be Result type for operations that can fail, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps3(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((true))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be any type, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps4(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((type.isSignlessInteger(1)))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be 1-bit signless integer, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_type_constraint_GloinOps5(
    ::mlir::Operation *op, ::mlir::Type type, ::llvm::StringRef valueKind,
    unsigned valueIndex) {
  if (!((::llvm::isa<::gloin::GloinSpawnType>(type)))) {
    return op->emitOpError(valueKind) << " #" << valueIndex
        << " must be Handle for spawned threads, but got " << type;
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps0(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !((::llvm::isa<::mlir::FlatSymbolRefAttr>(attr))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: flat symbol reference attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps0(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps0(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps1(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !((true)))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: any attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps1(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps1(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps2(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !((::llvm::isa<::mlir::StringAttr>(attr))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: string attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps2(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps2(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps3(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !((::llvm::isa<::mlir::BoolAttr>(attr))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: bool attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps3(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps3(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps4(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !(((::llvm::isa<::mlir::ArrayAttr>(attr))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(attr), [&](::mlir::Attribute attr) { return attr && (((::llvm::isa<::mlir::TypeAttr>(attr))) && ((::llvm::isa<::mlir::Type>(::llvm::cast<::mlir::TypeAttr>(attr).getValue()))) && ((true))); }))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: type array attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps4(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps4(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps5(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !(((::llvm::isa<::mlir::ArrayAttr>(attr))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(attr), [&](::mlir::Attribute attr) { return attr && ((::llvm::isa<::mlir::StringAttr>(attr))); }))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: string array attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps5(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps5(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps6(
    ::mlir::Attribute attr, ::llvm::StringRef attrName, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  if (attr && !(((::llvm::isa<::mlir::ArrayAttr>(attr))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(attr), [&](::mlir::Attribute attr) { return attr && (((::llvm::isa<::mlir::IntegerAttr>(attr))) && ((::llvm::cast<::mlir::IntegerAttr>(attr).getType().isSignlessInteger(64)))); }))))
    return emitError() << "attribute '" << attrName
        << "' failed to satisfy constraint: 64-bit integer array attribute";
  return ::mlir::success();
}
static ::mlir::LogicalResult __mlir_ods_local_attr_constraint_GloinOps6(
    ::mlir::Operation *op, ::mlir::Attribute attr, ::llvm::StringRef attrName) {
  return __mlir_ods_local_attr_constraint_GloinOps6(attr, attrName, [op]() {
    return op->emitOpError();
  });
}

static ::mlir::LogicalResult __mlir_ods_local_region_constraint_GloinOps0(
    ::mlir::Operation *op, ::mlir::Region &region, ::llvm::StringRef regionName,
    unsigned regionIndex) {
  if (!((::llvm::hasNItems(region, 1)))) {
    return op->emitOpError("region #") << regionIndex
        << (regionName.empty() ? " " : " ('" + regionName + "') ")
        << "failed to verify constraint: region with 1 blocks";
  }
  return ::mlir::success();
}

static ::mlir::LogicalResult __mlir_ods_local_region_constraint_GloinOps1(
    ::mlir::Operation *op, ::mlir::Region &region, ::llvm::StringRef regionName,
    unsigned regionIndex) {
  if (!((true))) {
    return op->emitOpError("region #") << regionIndex
        << (regionName.empty() ? " " : " ('" + regionName + "') ")
        << "failed to verify constraint: any region";
  }
  return ::mlir::success();
}
} // namespace gloin
namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::AsyncCallOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
AsyncCallOpGenericAdaptorBase::AsyncCallOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const Properties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), properties(properties), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.async_call", odsAttrs.getContext());
}

AsyncCallOpGenericAdaptorBase::AsyncCallOpGenericAdaptorBase(AsyncCallOp op) : AsyncCallOpGenericAdaptorBase(op->getDiscardableAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> AsyncCallOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (odsOperandsSize - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::DictionaryAttr AsyncCallOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::FlatSymbolRefAttr AsyncCallOpGenericAdaptorBase::getCalleeAttr() {
  auto attr = ::llvm::cast<::mlir::FlatSymbolRefAttr>(getProperties().callee);
  return attr;
}

::llvm::StringRef AsyncCallOpGenericAdaptorBase::getCallee() {
  auto attr = getCalleeAttr();
  return attr.getValue();
}

} // namespace detail
AsyncCallOpAdaptor::AsyncCallOpAdaptor(AsyncCallOp op) : AsyncCallOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult AsyncCallOpAdaptor::verify(::mlir::Location loc) {
  auto tblgen_callee = getProperties().callee; (void)tblgen_callee;
  if (!tblgen_callee) return emitError(loc, "'gloin.async_call' op ""requires attribute 'callee'");

  if (tblgen_callee && !((::llvm::isa<::mlir::FlatSymbolRefAttr>(tblgen_callee))))
    return emitError(loc, "'gloin.async_call' op ""attribute 'callee' failed to satisfy constraint: flat symbol reference attribute");
  return ::mlir::success();
}

std::pair<unsigned, unsigned> AsyncCallOp::getODSOperandIndexAndLength(unsigned index) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (getOperation()->getNumOperands() - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::Operation::operand_range AsyncCallOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::Operation::operand_range AsyncCallOp::getOperands() {
  return getODSOperands(0);
}

::mlir::MutableOperandRange AsyncCallOp::getOperandsMutable() {
  auto range = getODSOperandIndexAndLength(0);
  auto mutableRange = ::mlir::MutableOperandRange(getOperation(), range.first, range.second);
  return mutableRange;
}

std::pair<unsigned, unsigned> AsyncCallOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range AsyncCallOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinDeferredType> AsyncCallOp::getHandle() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinDeferredType>>(*getODSResults(0).begin());
}

::mlir::LogicalResult AsyncCallOp::setPropertiesFromAttr(Properties &prop, ::mlir::Attribute attr, ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  ::mlir::DictionaryAttr dict = ::llvm::dyn_cast<::mlir::DictionaryAttr>(attr);
  if (!dict) {
    emitError() << "expected DictionaryAttr to set properties";
    return ::mlir::failure();
  }

  {
    auto &propStorage = prop.callee;
       auto attr = dict.get("callee");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for callee in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `callee` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }
  return ::mlir::success();
}

::mlir::Attribute AsyncCallOp::getPropertiesAsAttr(::mlir::MLIRContext *ctx, const Properties &prop) {
    ::mlir::SmallVector<::mlir::NamedAttribute> attrs;
    ::mlir::Builder odsBuilder{ctx};

    {
      const auto &propStorage = prop.callee;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("callee",
                                       propStorage));
    }

  if (!attrs.empty())
    return odsBuilder.getDictionaryAttr(attrs);
  return {};
}

llvm::hash_code AsyncCallOp::computePropertiesHash(const Properties &prop) {
  return llvm::hash_combine(
    llvm::hash_value(prop.callee.getAsOpaquePointer()));
}

std::optional<mlir::Attribute> AsyncCallOp::getInherentAttr(::mlir::MLIRContext *ctx, const Properties &prop, llvm::StringRef name) {
    if (name == "callee")
      return prop.callee;
  return std::nullopt;
}

void AsyncCallOp::setInherentAttr(Properties &prop, llvm::StringRef name, mlir::Attribute value) {
    if (name == "callee") {
       prop.callee = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.callee)>>(value);
       return;
    }
}

void AsyncCallOp::populateInherentAttrs(::mlir::MLIRContext *ctx, const Properties &prop, ::mlir::NamedAttrList &attrs) {
    if (prop.callee) attrs.append("callee", prop.callee);
}

::mlir::LogicalResult AsyncCallOp::verifyInherentAttrs(::mlir::OperationName opName, ::mlir::NamedAttrList &attrs, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
    {
      ::mlir::Attribute attr = attrs.get(getCalleeAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps0(attr, "callee", emitError)))
        return ::mlir::failure();
    }
    return ::mlir::success();
}

::mlir::LogicalResult AsyncCallOp::readProperties(::mlir::DialectBytecodeReader &reader, ::mlir::OperationState &state) {
  auto &prop = state.getOrAddProperties<Properties>(); (void)prop;
  if (::mlir::failed(reader.readAttribute(prop.callee)))
    return ::mlir::failure();
  return ::mlir::success();
}

void AsyncCallOp::writeProperties(::mlir::DialectBytecodeWriter &writer) {
  auto &prop = getProperties(); (void)prop;
  writer.writeAttribute(prop.callee);
}

::mlir::FlatSymbolRefAttr AsyncCallOp::getCalleeAttr() {
  return ::llvm::cast<::mlir::FlatSymbolRefAttr>(getProperties().callee);
}

::llvm::StringRef AsyncCallOp::getCallee() {
  auto attr = getCalleeAttr();
  return attr.getValue();
}

void AsyncCallOp::setCalleeAttr(::mlir::FlatSymbolRefAttr attr) {
  (*this)->setAttr(getCalleeAttrName(), attr);
}

void AsyncCallOp::setCallee(::llvm::StringRef attrValue) {
  (*this)->setAttr(getCalleeAttrName(), ::mlir::SymbolRefAttr::get(::mlir::Builder((*this)->getContext()).getContext(), attrValue));
}

void AsyncCallOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type handle, ::mlir::FlatSymbolRefAttr callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = callee;
  odsState.addTypes(handle);
}

void AsyncCallOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::FlatSymbolRefAttr callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = callee;
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void AsyncCallOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type handle, ::llvm::StringRef callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = ::mlir::SymbolRefAttr::get(odsBuilder.getContext(), callee);
  odsState.addTypes(handle);
}

void AsyncCallOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::llvm::StringRef callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = ::mlir::SymbolRefAttr::get(odsBuilder.getContext(), callee);
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void AsyncCallOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 1u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult AsyncCallOp::verifyInvariantsImpl() {
  auto tblgen_callee = getProperties().callee; (void)tblgen_callee;
  if (!tblgen_callee) return emitOpError("requires attribute 'callee'");

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps0(*this, tblgen_callee, "callee")))
    return ::mlir::failure();
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps0(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSResults(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps1(*this, v.getType(), "result", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult AsyncCallOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::AsyncCallOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::AwaitOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
AwaitOpGenericAdaptorBase::AwaitOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.await", odsAttrs.getContext());
}

AwaitOpGenericAdaptorBase::AwaitOpGenericAdaptorBase(AwaitOp op) : AwaitOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> AwaitOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr AwaitOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

} // namespace detail
AwaitOpAdaptor::AwaitOpAdaptor(AwaitOp op) : AwaitOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult AwaitOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> AwaitOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range AwaitOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinDeferredType> AwaitOp::getHandle() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinDeferredType>>(*getODSOperands(0).begin());
}

::mlir::OpOperand &AwaitOp::getHandleMutable() {
  auto range = getODSOperandIndexAndLength(0);
  return getOperation()->getOpOperand(range.first);
}

std::pair<unsigned, unsigned> AwaitOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range AwaitOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinResultType> AwaitOp::getResult() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinResultType>>(*getODSResults(0).begin());
}

void AwaitOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type result, ::mlir::Value handle) {
  odsState.addOperands(handle);
  odsState.addTypes(result);
}

void AwaitOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::Value handle) {
  odsState.addOperands(handle);
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void AwaitOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 1u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 1u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult AwaitOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps1(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSResults(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps2(*this, v.getType(), "result", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult AwaitOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::AwaitOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::ConstantOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
ConstantOpGenericAdaptorBase::ConstantOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const Properties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), properties(properties), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.constant", odsAttrs.getContext());
}

ConstantOpGenericAdaptorBase::ConstantOpGenericAdaptorBase(ConstantOp op) : ConstantOpGenericAdaptorBase(op->getDiscardableAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> ConstantOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr ConstantOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::Attribute ConstantOpGenericAdaptorBase::getValueAttr() {
  auto attr = ::llvm::cast<::mlir::Attribute>(getProperties().value);
  return attr;
}

::mlir::Attribute ConstantOpGenericAdaptorBase::getValue() {
  auto attr = getValueAttr();
  return attr;
}

} // namespace detail
ConstantOpAdaptor::ConstantOpAdaptor(ConstantOp op) : ConstantOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult ConstantOpAdaptor::verify(::mlir::Location loc) {
  auto tblgen_value = getProperties().value; (void)tblgen_value;
  if (!tblgen_value) return emitError(loc, "'gloin.constant' op ""requires attribute 'value'");

  if (tblgen_value && !((true)))
    return emitError(loc, "'gloin.constant' op ""attribute 'value' failed to satisfy constraint: any attribute");
  return ::mlir::success();
}

std::pair<unsigned, unsigned> ConstantOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range ConstantOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

std::pair<unsigned, unsigned> ConstantOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range ConstantOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::Value ConstantOp::getResult() {
  return ::llvm::cast<::mlir::Value>(*getODSResults(0).begin());
}

::mlir::LogicalResult ConstantOp::setPropertiesFromAttr(Properties &prop, ::mlir::Attribute attr, ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  ::mlir::DictionaryAttr dict = ::llvm::dyn_cast<::mlir::DictionaryAttr>(attr);
  if (!dict) {
    emitError() << "expected DictionaryAttr to set properties";
    return ::mlir::failure();
  }

  {
    auto &propStorage = prop.value;
       auto attr = dict.get("value");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for value in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `value` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }
  return ::mlir::success();
}

::mlir::Attribute ConstantOp::getPropertiesAsAttr(::mlir::MLIRContext *ctx, const Properties &prop) {
    ::mlir::SmallVector<::mlir::NamedAttribute> attrs;
    ::mlir::Builder odsBuilder{ctx};

    {
      const auto &propStorage = prop.value;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("value",
                                       propStorage));
    }

  if (!attrs.empty())
    return odsBuilder.getDictionaryAttr(attrs);
  return {};
}

llvm::hash_code ConstantOp::computePropertiesHash(const Properties &prop) {
  return llvm::hash_combine(
    llvm::hash_value(prop.value.getAsOpaquePointer()));
}

std::optional<mlir::Attribute> ConstantOp::getInherentAttr(::mlir::MLIRContext *ctx, const Properties &prop, llvm::StringRef name) {
    if (name == "value")
      return prop.value;
  return std::nullopt;
}

void ConstantOp::setInherentAttr(Properties &prop, llvm::StringRef name, mlir::Attribute value) {
    if (name == "value") {
       prop.value = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.value)>>(value);
       return;
    }
}

void ConstantOp::populateInherentAttrs(::mlir::MLIRContext *ctx, const Properties &prop, ::mlir::NamedAttrList &attrs) {
    if (prop.value) attrs.append("value", prop.value);
}

::mlir::LogicalResult ConstantOp::verifyInherentAttrs(::mlir::OperationName opName, ::mlir::NamedAttrList &attrs, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
    {
      ::mlir::Attribute attr = attrs.get(getValueAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps1(attr, "value", emitError)))
        return ::mlir::failure();
    }
    return ::mlir::success();
}

::mlir::LogicalResult ConstantOp::readProperties(::mlir::DialectBytecodeReader &reader, ::mlir::OperationState &state) {
  auto &prop = state.getOrAddProperties<Properties>(); (void)prop;
  if (::mlir::failed(reader.readAttribute(prop.value)))
    return ::mlir::failure();
  return ::mlir::success();
}

void ConstantOp::writeProperties(::mlir::DialectBytecodeWriter &writer) {
  auto &prop = getProperties(); (void)prop;
  writer.writeAttribute(prop.value);
}

::mlir::Attribute ConstantOp::getValueAttr() {
  return ::llvm::cast<::mlir::Attribute>(getProperties().value);
}

::mlir::Attribute ConstantOp::getValue() {
  auto attr = getValueAttr();
  return attr;
}

void ConstantOp::setValueAttr(::mlir::Attribute attr) {
  (*this)->setAttr(getValueAttrName(), attr);
}

void ConstantOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type result, ::mlir::Attribute value) {
  odsState.getOrAddProperties<Properties>().value = value;
  odsState.addTypes(result);
}

void ConstantOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::Attribute value) {
  odsState.getOrAddProperties<Properties>().value = value;
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void ConstantOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 0u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 1u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult ConstantOp::verifyInvariantsImpl() {
  auto tblgen_value = getProperties().value; (void)tblgen_value;
  if (!tblgen_value) return emitOpError("requires attribute 'value'");

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps1(*this, tblgen_value, "value")))
    return ::mlir::failure();
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSResults(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps3(*this, v.getType(), "result", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult ConstantOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

void ConstantOp::getEffects(::llvm::SmallVectorImpl<::mlir::SideEffects::EffectInstance<::mlir::MemoryEffects::Effect>> &effects) {
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::ConstantOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::DeferOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
DeferOpGenericAdaptorBase::DeferOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.defer", odsAttrs.getContext());
}

DeferOpGenericAdaptorBase::DeferOpGenericAdaptorBase(DeferOp op) : DeferOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> DeferOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr DeferOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::Region &DeferOpGenericAdaptorBase::getBodyRegion() {
  return *odsRegions[0];
}

::mlir::RegionRange DeferOpGenericAdaptorBase::getRegions() {
  return odsRegions;
}

} // namespace detail
DeferOpAdaptor::DeferOpAdaptor(DeferOp op) : DeferOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult DeferOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> DeferOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range DeferOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

std::pair<unsigned, unsigned> DeferOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range DeferOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::Region &DeferOp::getBodyRegion() {
  return (*this)->getRegion(0);
}

void DeferOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState) {
  (void)odsState.addRegion();
}

void DeferOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes) {
  (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void DeferOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 0u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  for (unsigned i = 0; i != 1; ++i)
    (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult DeferOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;

    for (auto &region : ::llvm::MutableArrayRef((*this)->getRegion(0)))
      if (::mlir::failed(__mlir_ods_local_region_constraint_GloinOps0(*this, region, "bodyRegion", index++)))
        return ::mlir::failure();
  }
  return ::mlir::success();
}

::mlir::LogicalResult DeferOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::DeferOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::IfOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
IfOpGenericAdaptorBase::IfOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.if", odsAttrs.getContext());
}

IfOpGenericAdaptorBase::IfOpGenericAdaptorBase(IfOp op) : IfOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> IfOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr IfOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::Region &IfOpGenericAdaptorBase::getThenRegion() {
  return *odsRegions[0];
}

::mlir::Region &IfOpGenericAdaptorBase::getElseRegion() {
  return *odsRegions[1];
}

::mlir::RegionRange IfOpGenericAdaptorBase::getRegions() {
  return odsRegions;
}

} // namespace detail
IfOpAdaptor::IfOpAdaptor(IfOp op) : IfOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult IfOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> IfOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range IfOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::mlir::IntegerType> IfOp::getCondition() {
  return ::llvm::cast<::mlir::TypedValue<::mlir::IntegerType>>(*getODSOperands(0).begin());
}

::mlir::OpOperand &IfOp::getConditionMutable() {
  auto range = getODSOperandIndexAndLength(0);
  return getOperation()->getOpOperand(range.first);
}

std::pair<unsigned, unsigned> IfOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range IfOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::Region &IfOp::getThenRegion() {
  return (*this)->getRegion(0);
}

::mlir::Region &IfOp::getElseRegion() {
  return (*this)->getRegion(1);
}

void IfOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Value condition) {
  odsState.addOperands(condition);
  (void)odsState.addRegion();
  (void)odsState.addRegion();
}

void IfOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::Value condition) {
  odsState.addOperands(condition);
  (void)odsState.addRegion();
  (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void IfOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 1u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  for (unsigned i = 0; i != 2; ++i)
    (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult IfOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps4(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;

    for (auto &region : ::llvm::MutableArrayRef((*this)->getRegion(0)))
      if (::mlir::failed(__mlir_ods_local_region_constraint_GloinOps0(*this, region, "thenRegion", index++)))
        return ::mlir::failure();

    for (auto &region : ::llvm::MutableArrayRef((*this)->getRegion(1)))
      if (::mlir::failed(__mlir_ods_local_region_constraint_GloinOps1(*this, region, "elseRegion", index++)))
        return ::mlir::failure();
  }
  return ::mlir::success();
}

::mlir::LogicalResult IfOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::IfOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::JoinOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
JoinOpGenericAdaptorBase::JoinOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.join", odsAttrs.getContext());
}

JoinOpGenericAdaptorBase::JoinOpGenericAdaptorBase(JoinOp op) : JoinOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> JoinOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr JoinOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

} // namespace detail
JoinOpAdaptor::JoinOpAdaptor(JoinOp op) : JoinOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult JoinOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> JoinOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range JoinOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinSpawnType> JoinOp::getHandle() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinSpawnType>>(*getODSOperands(0).begin());
}

::mlir::OpOperand &JoinOp::getHandleMutable() {
  auto range = getODSOperandIndexAndLength(0);
  return getOperation()->getOpOperand(range.first);
}

std::pair<unsigned, unsigned> JoinOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range JoinOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinResultType> JoinOp::getResult() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinResultType>>(*getODSResults(0).begin());
}

void JoinOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type result, ::mlir::Value handle) {
  odsState.addOperands(handle);
  odsState.addTypes(result);
}

void JoinOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::Value handle) {
  odsState.addOperands(handle);
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void JoinOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 1u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 1u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult JoinOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps5(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSResults(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps2(*this, v.getType(), "result", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult JoinOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::JoinOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::SpawnOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
SpawnOpGenericAdaptorBase::SpawnOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const Properties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), properties(properties), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.spawn", odsAttrs.getContext());
}

SpawnOpGenericAdaptorBase::SpawnOpGenericAdaptorBase(SpawnOp op) : SpawnOpGenericAdaptorBase(op->getDiscardableAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> SpawnOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (odsOperandsSize - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::DictionaryAttr SpawnOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::FlatSymbolRefAttr SpawnOpGenericAdaptorBase::getCalleeAttr() {
  auto attr = ::llvm::cast<::mlir::FlatSymbolRefAttr>(getProperties().callee);
  return attr;
}

::llvm::StringRef SpawnOpGenericAdaptorBase::getCallee() {
  auto attr = getCalleeAttr();
  return attr.getValue();
}

} // namespace detail
SpawnOpAdaptor::SpawnOpAdaptor(SpawnOp op) : SpawnOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult SpawnOpAdaptor::verify(::mlir::Location loc) {
  auto tblgen_callee = getProperties().callee; (void)tblgen_callee;
  if (!tblgen_callee) return emitError(loc, "'gloin.spawn' op ""requires attribute 'callee'");

  if (tblgen_callee && !((::llvm::isa<::mlir::FlatSymbolRefAttr>(tblgen_callee))))
    return emitError(loc, "'gloin.spawn' op ""attribute 'callee' failed to satisfy constraint: flat symbol reference attribute");
  return ::mlir::success();
}

std::pair<unsigned, unsigned> SpawnOp::getODSOperandIndexAndLength(unsigned index) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (getOperation()->getNumOperands() - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::Operation::operand_range SpawnOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::Operation::operand_range SpawnOp::getOperands() {
  return getODSOperands(0);
}

::mlir::MutableOperandRange SpawnOp::getOperandsMutable() {
  auto range = getODSOperandIndexAndLength(0);
  auto mutableRange = ::mlir::MutableOperandRange(getOperation(), range.first, range.second);
  return mutableRange;
}

std::pair<unsigned, unsigned> SpawnOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range SpawnOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::gloin::GloinSpawnType> SpawnOp::getHandle() {
  return ::llvm::cast<::mlir::TypedValue<::gloin::GloinSpawnType>>(*getODSResults(0).begin());
}

::mlir::LogicalResult SpawnOp::setPropertiesFromAttr(Properties &prop, ::mlir::Attribute attr, ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  ::mlir::DictionaryAttr dict = ::llvm::dyn_cast<::mlir::DictionaryAttr>(attr);
  if (!dict) {
    emitError() << "expected DictionaryAttr to set properties";
    return ::mlir::failure();
  }

  {
    auto &propStorage = prop.callee;
       auto attr = dict.get("callee");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for callee in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `callee` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }
  return ::mlir::success();
}

::mlir::Attribute SpawnOp::getPropertiesAsAttr(::mlir::MLIRContext *ctx, const Properties &prop) {
    ::mlir::SmallVector<::mlir::NamedAttribute> attrs;
    ::mlir::Builder odsBuilder{ctx};

    {
      const auto &propStorage = prop.callee;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("callee",
                                       propStorage));
    }

  if (!attrs.empty())
    return odsBuilder.getDictionaryAttr(attrs);
  return {};
}

llvm::hash_code SpawnOp::computePropertiesHash(const Properties &prop) {
  return llvm::hash_combine(
    llvm::hash_value(prop.callee.getAsOpaquePointer()));
}

std::optional<mlir::Attribute> SpawnOp::getInherentAttr(::mlir::MLIRContext *ctx, const Properties &prop, llvm::StringRef name) {
    if (name == "callee")
      return prop.callee;
  return std::nullopt;
}

void SpawnOp::setInherentAttr(Properties &prop, llvm::StringRef name, mlir::Attribute value) {
    if (name == "callee") {
       prop.callee = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.callee)>>(value);
       return;
    }
}

void SpawnOp::populateInherentAttrs(::mlir::MLIRContext *ctx, const Properties &prop, ::mlir::NamedAttrList &attrs) {
    if (prop.callee) attrs.append("callee", prop.callee);
}

::mlir::LogicalResult SpawnOp::verifyInherentAttrs(::mlir::OperationName opName, ::mlir::NamedAttrList &attrs, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
    {
      ::mlir::Attribute attr = attrs.get(getCalleeAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps0(attr, "callee", emitError)))
        return ::mlir::failure();
    }
    return ::mlir::success();
}

::mlir::LogicalResult SpawnOp::readProperties(::mlir::DialectBytecodeReader &reader, ::mlir::OperationState &state) {
  auto &prop = state.getOrAddProperties<Properties>(); (void)prop;
  if (::mlir::failed(reader.readAttribute(prop.callee)))
    return ::mlir::failure();
  return ::mlir::success();
}

void SpawnOp::writeProperties(::mlir::DialectBytecodeWriter &writer) {
  auto &prop = getProperties(); (void)prop;
  writer.writeAttribute(prop.callee);
}

::mlir::FlatSymbolRefAttr SpawnOp::getCalleeAttr() {
  return ::llvm::cast<::mlir::FlatSymbolRefAttr>(getProperties().callee);
}

::llvm::StringRef SpawnOp::getCallee() {
  auto attr = getCalleeAttr();
  return attr.getValue();
}

void SpawnOp::setCalleeAttr(::mlir::FlatSymbolRefAttr attr) {
  (*this)->setAttr(getCalleeAttrName(), attr);
}

void SpawnOp::setCallee(::llvm::StringRef attrValue) {
  (*this)->setAttr(getCalleeAttrName(), ::mlir::SymbolRefAttr::get(::mlir::Builder((*this)->getContext()).getContext(), attrValue));
}

void SpawnOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type handle, ::mlir::FlatSymbolRefAttr callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = callee;
  odsState.addTypes(handle);
}

void SpawnOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::FlatSymbolRefAttr callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = callee;
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void SpawnOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Type handle, ::llvm::StringRef callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = ::mlir::SymbolRefAttr::get(odsBuilder.getContext(), callee);
  odsState.addTypes(handle);
}

void SpawnOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::llvm::StringRef callee, ::mlir::ValueRange operands) {
  odsState.addOperands(operands);
  odsState.getOrAddProperties<Properties>().callee = ::mlir::SymbolRefAttr::get(odsBuilder.getContext(), callee);
  assert(resultTypes.size() == 1u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void SpawnOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 1u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult SpawnOp::verifyInvariantsImpl() {
  auto tblgen_callee = getProperties().callee; (void)tblgen_callee;
  if (!tblgen_callee) return emitOpError("requires attribute 'callee'");

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps0(*this, tblgen_callee, "callee")))
    return ::mlir::failure();
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps0(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSResults(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps5(*this, v.getType(), "result", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult SpawnOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::SpawnOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::StructDefOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
StructDefOpGenericAdaptorBase::StructDefOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const Properties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), properties(properties), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.struct_def", odsAttrs.getContext());
}

StructDefOpGenericAdaptorBase::StructDefOpGenericAdaptorBase(StructDefOp op) : StructDefOpGenericAdaptorBase(op->getDiscardableAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> StructDefOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr StructDefOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::StringAttr StructDefOpGenericAdaptorBase::getSymNameAttr() {
  auto attr = ::llvm::cast<::mlir::StringAttr>(getProperties().sym_name);
  return attr;
}

::llvm::StringRef StructDefOpGenericAdaptorBase::getSymName() {
  auto attr = getSymNameAttr();
  return attr.getValue();
}

::mlir::BoolAttr StructDefOpGenericAdaptorBase::getIsPackedAttr() {
  auto attr = ::llvm::cast<::mlir::BoolAttr>(getProperties().is_packed);
  return attr;
}

bool StructDefOpGenericAdaptorBase::getIsPacked() {
  auto attr = getIsPackedAttr();
  return attr.getValue();
}

::mlir::ArrayAttr StructDefOpGenericAdaptorBase::getFieldTypesAttr() {
  auto attr = ::llvm::cast<::mlir::ArrayAttr>(getProperties().field_types);
  return attr;
}

::mlir::ArrayAttr StructDefOpGenericAdaptorBase::getFieldTypes() {
  auto attr = getFieldTypesAttr();
  return attr;
}

::mlir::ArrayAttr StructDefOpGenericAdaptorBase::getFieldNamesAttr() {
  auto attr = ::llvm::cast<::mlir::ArrayAttr>(getProperties().field_names);
  return attr;
}

::mlir::ArrayAttr StructDefOpGenericAdaptorBase::getFieldNames() {
  auto attr = getFieldNamesAttr();
  return attr;
}

::mlir::ArrayAttr StructDefOpGenericAdaptorBase::getBitOffsetsAttr() {
  auto attr = ::llvm::dyn_cast_or_null<::mlir::ArrayAttr>(getProperties().bit_offsets);
  return attr;
}

::std::optional< ::mlir::ArrayAttr > StructDefOpGenericAdaptorBase::getBitOffsets() {
  auto attr = getBitOffsetsAttr();
  return attr ? ::std::optional< ::mlir::ArrayAttr >(attr) : (::std::nullopt);
}

::mlir::Region &StructDefOpGenericAdaptorBase::getBody() {
  return *odsRegions[0];
}

::mlir::RegionRange StructDefOpGenericAdaptorBase::getRegions() {
  return odsRegions;
}

} // namespace detail
StructDefOpAdaptor::StructDefOpAdaptor(StructDefOp op) : StructDefOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult StructDefOpAdaptor::verify(::mlir::Location loc) {
  auto tblgen_bit_offsets = getProperties().bit_offsets; (void)tblgen_bit_offsets;
  auto tblgen_field_names = getProperties().field_names; (void)tblgen_field_names;
  if (!tblgen_field_names) return emitError(loc, "'gloin.struct_def' op ""requires attribute 'field_names'");
  auto tblgen_field_types = getProperties().field_types; (void)tblgen_field_types;
  if (!tblgen_field_types) return emitError(loc, "'gloin.struct_def' op ""requires attribute 'field_types'");
  auto tblgen_is_packed = getProperties().is_packed; (void)tblgen_is_packed;
  if (!tblgen_is_packed) return emitError(loc, "'gloin.struct_def' op ""requires attribute 'is_packed'");
  auto tblgen_sym_name = getProperties().sym_name; (void)tblgen_sym_name;
  if (!tblgen_sym_name) return emitError(loc, "'gloin.struct_def' op ""requires attribute 'sym_name'");

  if (tblgen_sym_name && !((::llvm::isa<::mlir::StringAttr>(tblgen_sym_name))))
    return emitError(loc, "'gloin.struct_def' op ""attribute 'sym_name' failed to satisfy constraint: string attribute");

  if (tblgen_is_packed && !((::llvm::isa<::mlir::BoolAttr>(tblgen_is_packed))))
    return emitError(loc, "'gloin.struct_def' op ""attribute 'is_packed' failed to satisfy constraint: bool attribute");

  if (tblgen_field_types && !(((::llvm::isa<::mlir::ArrayAttr>(tblgen_field_types))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(tblgen_field_types), [&](::mlir::Attribute attr) { return attr && (((::llvm::isa<::mlir::TypeAttr>(attr))) && ((::llvm::isa<::mlir::Type>(::llvm::cast<::mlir::TypeAttr>(attr).getValue()))) && ((true))); }))))
    return emitError(loc, "'gloin.struct_def' op ""attribute 'field_types' failed to satisfy constraint: type array attribute");

  if (tblgen_field_names && !(((::llvm::isa<::mlir::ArrayAttr>(tblgen_field_names))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(tblgen_field_names), [&](::mlir::Attribute attr) { return attr && ((::llvm::isa<::mlir::StringAttr>(attr))); }))))
    return emitError(loc, "'gloin.struct_def' op ""attribute 'field_names' failed to satisfy constraint: string array attribute");

  if (tblgen_bit_offsets && !(((::llvm::isa<::mlir::ArrayAttr>(tblgen_bit_offsets))) && (::llvm::all_of(::llvm::cast<::mlir::ArrayAttr>(tblgen_bit_offsets), [&](::mlir::Attribute attr) { return attr && (((::llvm::isa<::mlir::IntegerAttr>(attr))) && ((::llvm::cast<::mlir::IntegerAttr>(attr).getType().isSignlessInteger(64)))); }))))
    return emitError(loc, "'gloin.struct_def' op ""attribute 'bit_offsets' failed to satisfy constraint: 64-bit integer array attribute");
  return ::mlir::success();
}

std::pair<unsigned, unsigned> StructDefOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range StructDefOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

std::pair<unsigned, unsigned> StructDefOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range StructDefOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::Region &StructDefOp::getBody() {
  return (*this)->getRegion(0);
}

::mlir::LogicalResult StructDefOp::setPropertiesFromAttr(Properties &prop, ::mlir::Attribute attr, ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
  ::mlir::DictionaryAttr dict = ::llvm::dyn_cast<::mlir::DictionaryAttr>(attr);
  if (!dict) {
    emitError() << "expected DictionaryAttr to set properties";
    return ::mlir::failure();
  }

  {
    auto &propStorage = prop.bit_offsets;
       auto attr = dict.get("bit_offsets");
    if (attr || /*isRequired=*/false) {
      if (!attr) {
        emitError() << "expected key entry for bit_offsets in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `bit_offsets` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }

  {
    auto &propStorage = prop.field_names;
       auto attr = dict.get("field_names");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for field_names in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `field_names` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }

  {
    auto &propStorage = prop.field_types;
       auto attr = dict.get("field_types");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for field_types in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `field_types` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }

  {
    auto &propStorage = prop.is_packed;
       auto attr = dict.get("is_packed");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for is_packed in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `is_packed` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }

  {
    auto &propStorage = prop.sym_name;
       auto attr = dict.get("sym_name");
    if (attr || /*isRequired=*/true) {
      if (!attr) {
        emitError() << "expected key entry for sym_name in DictionaryAttr to set "
                   "Properties.";
        return ::mlir::failure();
      }
      auto convertedAttr = ::llvm::dyn_cast<std::remove_reference_t<decltype(propStorage)>>(attr);
      if (convertedAttr) {
        propStorage = convertedAttr;
      } else {
        emitError() << "Invalid attribute `sym_name` in property conversion: " << attr;
        return ::mlir::failure();
      }
    }
  }
  return ::mlir::success();
}

::mlir::Attribute StructDefOp::getPropertiesAsAttr(::mlir::MLIRContext *ctx, const Properties &prop) {
    ::mlir::SmallVector<::mlir::NamedAttribute> attrs;
    ::mlir::Builder odsBuilder{ctx};

    {
      const auto &propStorage = prop.bit_offsets;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("bit_offsets",
                                       propStorage));
    }

    {
      const auto &propStorage = prop.field_names;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("field_names",
                                       propStorage));
    }

    {
      const auto &propStorage = prop.field_types;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("field_types",
                                       propStorage));
    }

    {
      const auto &propStorage = prop.is_packed;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("is_packed",
                                       propStorage));
    }

    {
      const auto &propStorage = prop.sym_name;
      if (propStorage)
        attrs.push_back(odsBuilder.getNamedAttr("sym_name",
                                       propStorage));
    }

  if (!attrs.empty())
    return odsBuilder.getDictionaryAttr(attrs);
  return {};
}

llvm::hash_code StructDefOp::computePropertiesHash(const Properties &prop) {
  return llvm::hash_combine(
    llvm::hash_value(prop.bit_offsets.getAsOpaquePointer()), 
    llvm::hash_value(prop.field_names.getAsOpaquePointer()), 
    llvm::hash_value(prop.field_types.getAsOpaquePointer()), 
    llvm::hash_value(prop.is_packed.getAsOpaquePointer()), 
    llvm::hash_value(prop.sym_name.getAsOpaquePointer()));
}

std::optional<mlir::Attribute> StructDefOp::getInherentAttr(::mlir::MLIRContext *ctx, const Properties &prop, llvm::StringRef name) {
    if (name == "bit_offsets")
      return prop.bit_offsets;

    if (name == "field_names")
      return prop.field_names;

    if (name == "field_types")
      return prop.field_types;

    if (name == "is_packed")
      return prop.is_packed;

    if (name == "sym_name")
      return prop.sym_name;
  return std::nullopt;
}

void StructDefOp::setInherentAttr(Properties &prop, llvm::StringRef name, mlir::Attribute value) {
    if (name == "bit_offsets") {
       prop.bit_offsets = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.bit_offsets)>>(value);
       return;
    }

    if (name == "field_names") {
       prop.field_names = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.field_names)>>(value);
       return;
    }

    if (name == "field_types") {
       prop.field_types = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.field_types)>>(value);
       return;
    }

    if (name == "is_packed") {
       prop.is_packed = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.is_packed)>>(value);
       return;
    }

    if (name == "sym_name") {
       prop.sym_name = ::llvm::dyn_cast_or_null<std::remove_reference_t<decltype(prop.sym_name)>>(value);
       return;
    }
}

void StructDefOp::populateInherentAttrs(::mlir::MLIRContext *ctx, const Properties &prop, ::mlir::NamedAttrList &attrs) {
    if (prop.bit_offsets) attrs.append("bit_offsets", prop.bit_offsets);

    if (prop.field_names) attrs.append("field_names", prop.field_names);

    if (prop.field_types) attrs.append("field_types", prop.field_types);

    if (prop.is_packed) attrs.append("is_packed", prop.is_packed);

    if (prop.sym_name) attrs.append("sym_name", prop.sym_name);
}

::mlir::LogicalResult StructDefOp::verifyInherentAttrs(::mlir::OperationName opName, ::mlir::NamedAttrList &attrs, llvm::function_ref<::mlir::InFlightDiagnostic()> emitError) {
    {
      ::mlir::Attribute attr = attrs.get(getBitOffsetsAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps6(attr, "bit_offsets", emitError)))
        return ::mlir::failure();
    }

    {
      ::mlir::Attribute attr = attrs.get(getFieldNamesAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps5(attr, "field_names", emitError)))
        return ::mlir::failure();
    }

    {
      ::mlir::Attribute attr = attrs.get(getFieldTypesAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps4(attr, "field_types", emitError)))
        return ::mlir::failure();
    }

    {
      ::mlir::Attribute attr = attrs.get(getIsPackedAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps3(attr, "is_packed", emitError)))
        return ::mlir::failure();
    }

    {
      ::mlir::Attribute attr = attrs.get(getSymNameAttrName(opName));
      if (attr && ::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps2(attr, "sym_name", emitError)))
        return ::mlir::failure();
    }
    return ::mlir::success();
}

::mlir::LogicalResult StructDefOp::readProperties(::mlir::DialectBytecodeReader &reader, ::mlir::OperationState &state) {
  auto &prop = state.getOrAddProperties<Properties>(); (void)prop;
  if (::mlir::failed(reader.readOptionalAttribute(prop.bit_offsets)))
    return ::mlir::failure();

  if (::mlir::failed(reader.readAttribute(prop.field_names)))
    return ::mlir::failure();

  if (::mlir::failed(reader.readAttribute(prop.field_types)))
    return ::mlir::failure();

  if (::mlir::failed(reader.readAttribute(prop.is_packed)))
    return ::mlir::failure();

  if (::mlir::failed(reader.readAttribute(prop.sym_name)))
    return ::mlir::failure();
  return ::mlir::success();
}

void StructDefOp::writeProperties(::mlir::DialectBytecodeWriter &writer) {
  auto &prop = getProperties(); (void)prop;

  writer.writeOptionalAttribute(prop.bit_offsets);
  writer.writeAttribute(prop.field_names);
  writer.writeAttribute(prop.field_types);
  writer.writeAttribute(prop.is_packed);
  writer.writeAttribute(prop.sym_name);
}

::mlir::StringAttr StructDefOp::getSymNameAttr() {
  return ::llvm::cast<::mlir::StringAttr>(getProperties().sym_name);
}

::llvm::StringRef StructDefOp::getSymName() {
  auto attr = getSymNameAttr();
  return attr.getValue();
}

::mlir::BoolAttr StructDefOp::getIsPackedAttr() {
  return ::llvm::cast<::mlir::BoolAttr>(getProperties().is_packed);
}

bool StructDefOp::getIsPacked() {
  auto attr = getIsPackedAttr();
  return attr.getValue();
}

::mlir::ArrayAttr StructDefOp::getFieldTypesAttr() {
  return ::llvm::cast<::mlir::ArrayAttr>(getProperties().field_types);
}

::mlir::ArrayAttr StructDefOp::getFieldTypes() {
  auto attr = getFieldTypesAttr();
  return attr;
}

::mlir::ArrayAttr StructDefOp::getFieldNamesAttr() {
  return ::llvm::cast<::mlir::ArrayAttr>(getProperties().field_names);
}

::mlir::ArrayAttr StructDefOp::getFieldNames() {
  auto attr = getFieldNamesAttr();
  return attr;
}

::mlir::ArrayAttr StructDefOp::getBitOffsetsAttr() {
  return ::llvm::dyn_cast_or_null<::mlir::ArrayAttr>(getProperties().bit_offsets);
}

::std::optional< ::mlir::ArrayAttr > StructDefOp::getBitOffsets() {
  auto attr = getBitOffsetsAttr();
  return attr ? ::std::optional< ::mlir::ArrayAttr >(attr) : (::std::nullopt);
}

void StructDefOp::setSymNameAttr(::mlir::StringAttr attr) {
  (*this)->setAttr(getSymNameAttrName(), attr);
}

void StructDefOp::setSymName(::llvm::StringRef attrValue) {
  (*this)->setAttr(getSymNameAttrName(), ::mlir::Builder((*this)->getContext()).getStringAttr(attrValue));
}

void StructDefOp::setIsPackedAttr(::mlir::BoolAttr attr) {
  (*this)->setAttr(getIsPackedAttrName(), attr);
}

void StructDefOp::setIsPacked(bool attrValue) {
  (*this)->setAttr(getIsPackedAttrName(), ::mlir::Builder((*this)->getContext()).getBoolAttr(attrValue));
}

void StructDefOp::setFieldTypesAttr(::mlir::ArrayAttr attr) {
  (*this)->setAttr(getFieldTypesAttrName(), attr);
}

void StructDefOp::setFieldNamesAttr(::mlir::ArrayAttr attr) {
  (*this)->setAttr(getFieldNamesAttrName(), attr);
}

void StructDefOp::setBitOffsetsAttr(::mlir::ArrayAttr attr) {
  (*this)->setAttr(getBitOffsetsAttrName(), attr);
}

::mlir::Attribute StructDefOp::removeBitOffsetsAttr() {
    auto &attr = getProperties().bit_offsets;
    attr = {};
    return attr;
}

void StructDefOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::StringAttr sym_name, ::mlir::BoolAttr is_packed, ::mlir::ArrayAttr field_types, ::mlir::ArrayAttr field_names, /*optional*/::mlir::ArrayAttr bit_offsets) {
  odsState.getOrAddProperties<Properties>().sym_name = sym_name;
  odsState.getOrAddProperties<Properties>().is_packed = is_packed;
  odsState.getOrAddProperties<Properties>().field_types = field_types;
  odsState.getOrAddProperties<Properties>().field_names = field_names;
  if (bit_offsets) {
    odsState.getOrAddProperties<Properties>().bit_offsets = bit_offsets;
  }
  (void)odsState.addRegion();
}

void StructDefOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::StringAttr sym_name, ::mlir::BoolAttr is_packed, ::mlir::ArrayAttr field_types, ::mlir::ArrayAttr field_names, /*optional*/::mlir::ArrayAttr bit_offsets) {
  odsState.getOrAddProperties<Properties>().sym_name = sym_name;
  odsState.getOrAddProperties<Properties>().is_packed = is_packed;
  odsState.getOrAddProperties<Properties>().field_types = field_types;
  odsState.getOrAddProperties<Properties>().field_names = field_names;
  if (bit_offsets) {
    odsState.getOrAddProperties<Properties>().bit_offsets = bit_offsets;
  }
  (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void StructDefOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::llvm::StringRef sym_name, bool is_packed, ::mlir::ArrayAttr field_types, ::mlir::ArrayAttr field_names, /*optional*/::mlir::ArrayAttr bit_offsets) {
  odsState.getOrAddProperties<Properties>().sym_name = odsBuilder.getStringAttr(sym_name);
  odsState.getOrAddProperties<Properties>().is_packed = odsBuilder.getBoolAttr(is_packed);
  odsState.getOrAddProperties<Properties>().field_types = field_types;
  odsState.getOrAddProperties<Properties>().field_names = field_names;
  if (bit_offsets) {
    odsState.getOrAddProperties<Properties>().bit_offsets = bit_offsets;
  }
  (void)odsState.addRegion();
}

void StructDefOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::llvm::StringRef sym_name, bool is_packed, ::mlir::ArrayAttr field_types, ::mlir::ArrayAttr field_names, /*optional*/::mlir::ArrayAttr bit_offsets) {
  odsState.getOrAddProperties<Properties>().sym_name = odsBuilder.getStringAttr(sym_name);
  odsState.getOrAddProperties<Properties>().is_packed = odsBuilder.getBoolAttr(is_packed);
  odsState.getOrAddProperties<Properties>().field_types = field_types;
  odsState.getOrAddProperties<Properties>().field_names = field_names;
  if (bit_offsets) {
    odsState.getOrAddProperties<Properties>().bit_offsets = bit_offsets;
  }
  (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void StructDefOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 0u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  for (unsigned i = 0; i != 1; ++i)
    (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult StructDefOp::verifyInvariantsImpl() {
  auto tblgen_bit_offsets = getProperties().bit_offsets; (void)tblgen_bit_offsets;
  auto tblgen_field_names = getProperties().field_names; (void)tblgen_field_names;
  if (!tblgen_field_names) return emitOpError("requires attribute 'field_names'");
  auto tblgen_field_types = getProperties().field_types; (void)tblgen_field_types;
  if (!tblgen_field_types) return emitOpError("requires attribute 'field_types'");
  auto tblgen_is_packed = getProperties().is_packed; (void)tblgen_is_packed;
  if (!tblgen_is_packed) return emitOpError("requires attribute 'is_packed'");
  auto tblgen_sym_name = getProperties().sym_name; (void)tblgen_sym_name;
  if (!tblgen_sym_name) return emitOpError("requires attribute 'sym_name'");

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps2(*this, tblgen_sym_name, "sym_name")))
    return ::mlir::failure();

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps3(*this, tblgen_is_packed, "is_packed")))
    return ::mlir::failure();

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps4(*this, tblgen_field_types, "field_types")))
    return ::mlir::failure();

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps5(*this, tblgen_field_names, "field_names")))
    return ::mlir::failure();

  if (::mlir::failed(__mlir_ods_local_attr_constraint_GloinOps6(*this, tblgen_bit_offsets, "bit_offsets")))
    return ::mlir::failure();
  {
    unsigned index = 0; (void)index;

    for (auto &region : ::llvm::MutableArrayRef((*this)->getRegion(0)))
      if (::mlir::failed(__mlir_ods_local_region_constraint_GloinOps1(*this, region, "body", index++)))
        return ::mlir::failure();
  }
  return ::mlir::success();
}

::mlir::LogicalResult StructDefOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::StructDefOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::UnlessOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
UnlessOpGenericAdaptorBase::UnlessOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.unless", odsAttrs.getContext());
}

UnlessOpGenericAdaptorBase::UnlessOpGenericAdaptorBase(UnlessOp op) : UnlessOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> UnlessOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  return {index, 1};
}

::mlir::DictionaryAttr UnlessOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

::mlir::Region &UnlessOpGenericAdaptorBase::getBodyRegion() {
  return *odsRegions[0];
}

::mlir::RegionRange UnlessOpGenericAdaptorBase::getRegions() {
  return odsRegions;
}

} // namespace detail
UnlessOpAdaptor::UnlessOpAdaptor(UnlessOp op) : UnlessOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult UnlessOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> UnlessOp::getODSOperandIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::operand_range UnlessOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::TypedValue<::mlir::IntegerType> UnlessOp::getCondition() {
  return ::llvm::cast<::mlir::TypedValue<::mlir::IntegerType>>(*getODSOperands(0).begin());
}

::mlir::OpOperand &UnlessOp::getConditionMutable() {
  auto range = getODSOperandIndexAndLength(0);
  return getOperation()->getOpOperand(range.first);
}

std::pair<unsigned, unsigned> UnlessOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range UnlessOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

::mlir::Region &UnlessOp::getBodyRegion() {
  return (*this)->getRegion(0);
}

void UnlessOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::Value condition) {
  odsState.addOperands(condition);
  (void)odsState.addRegion();
}

void UnlessOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::Value condition) {
  odsState.addOperands(condition);
  (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of results");
  odsState.addTypes(resultTypes);
}

void UnlessOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  assert(operands.size() == 1u && "mismatched number of parameters");
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  for (unsigned i = 0; i != 1; ++i)
    (void)odsState.addRegion();
  assert(resultTypes.size() == 0u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult UnlessOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps4(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  {
    unsigned index = 0; (void)index;

    for (auto &region : ::llvm::MutableArrayRef((*this)->getRegion(0)))
      if (::mlir::failed(__mlir_ods_local_region_constraint_GloinOps0(*this, region, "bodyRegion", index++)))
        return ::mlir::failure();
  }
  return ::mlir::success();
}

::mlir::LogicalResult UnlessOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::UnlessOp)

namespace gloin {

//===----------------------------------------------------------------------===//
// ::gloin::YieldOp definitions
//===----------------------------------------------------------------------===//

namespace detail {
YieldOpGenericAdaptorBase::YieldOpGenericAdaptorBase(::mlir::DictionaryAttr attrs, const ::mlir::EmptyProperties &properties, ::mlir::RegionRange regions) : odsAttrs(attrs), odsRegions(regions) {  if (odsAttrs)
    odsOpName.emplace("gloin.yield", odsAttrs.getContext());
}

YieldOpGenericAdaptorBase::YieldOpGenericAdaptorBase(YieldOp op) : YieldOpGenericAdaptorBase(op->getAttrDictionary(), op.getProperties(), op->getRegions()) {}

std::pair<unsigned, unsigned> YieldOpGenericAdaptorBase::getODSOperandIndexAndLength(unsigned index, unsigned odsOperandsSize) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (odsOperandsSize - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::DictionaryAttr YieldOpGenericAdaptorBase::getAttributes() {
  return odsAttrs;
}

} // namespace detail
YieldOpAdaptor::YieldOpAdaptor(YieldOp op) : YieldOpGenericAdaptor(op->getOperands(), op) {}

::mlir::LogicalResult YieldOpAdaptor::verify(::mlir::Location loc) {
  return ::mlir::success();
}

std::pair<unsigned, unsigned> YieldOp::getODSOperandIndexAndLength(unsigned index) {
  bool isVariadic[] = {true};
  int prevVariadicCount = 0;
  for (unsigned i = 0; i < index; ++i)
    if (isVariadic[i]) ++prevVariadicCount;

  // Calculate how many dynamic values a static variadic operand corresponds to.
  // This assumes all static variadic operands have the same dynamic value count.
  int variadicSize = (getOperation()->getNumOperands() - 0) / 1;
  // `index` passed in as the parameter is the static index which counts each
  // operand (variadic or not) as size 1. So here for each previous static variadic
  // operand, we need to offset by (variadicSize - 1) to get where the dynamic
  // value pack for this static operand starts.
  int start = index + (variadicSize - 1) * prevVariadicCount;
  int size = isVariadic[index] ? variadicSize : 1;
  return {start, size};
}

::mlir::Operation::operand_range YieldOp::getODSOperands(unsigned index) {
  auto valueRange = getODSOperandIndexAndLength(index);
  return {std::next(getOperation()->operand_begin(), valueRange.first),
           std::next(getOperation()->operand_begin(), valueRange.first + valueRange.second)};
}

::mlir::Operation::operand_range YieldOp::getResults() {
  return getODSOperands(0);
}

::mlir::MutableOperandRange YieldOp::getResultsMutable() {
  auto range = getODSOperandIndexAndLength(0);
  auto mutableRange = ::mlir::MutableOperandRange(getOperation(), range.first, range.second);
  return mutableRange;
}

std::pair<unsigned, unsigned> YieldOp::getODSResultIndexAndLength(unsigned index) {
  return {index, 1};
}

::mlir::Operation::result_range YieldOp::getODSResults(unsigned index) {
  auto valueRange = getODSResultIndexAndLength(index);
  return {std::next(getOperation()->result_begin(), valueRange.first),
           std::next(getOperation()->result_begin(), valueRange.first + valueRange.second)};
}

void YieldOp::build(::mlir::OpBuilder &odsBuilder, ::mlir::OperationState &odsState, ::mlir::ValueRange results) {
  odsState.addOperands(results);
}

void YieldOp::build(::mlir::OpBuilder &, ::mlir::OperationState &odsState, ::mlir::TypeRange resultTypes, ::mlir::ValueRange operands, ::llvm::ArrayRef<::mlir::NamedAttribute> attributes) {
  odsState.addOperands(operands);
  odsState.addAttributes(attributes);
  assert(resultTypes.size() == 0u && "mismatched number of return types");
  odsState.addTypes(resultTypes);
}

::mlir::LogicalResult YieldOp::verifyInvariantsImpl() {
  {
    unsigned index = 0; (void)index;
    auto valueGroup0 = getODSOperands(0);

    for (auto v : valueGroup0) {
      if (::mlir::failed(__mlir_ods_local_type_constraint_GloinOps0(*this, v.getType(), "operand", index++)))
        return ::mlir::failure();
    }
  }
  return ::mlir::success();
}

::mlir::LogicalResult YieldOp::verifyInvariants() {
  return verifyInvariantsImpl();
}

void YieldOp::getEffects(::llvm::SmallVectorImpl<::mlir::SideEffects::EffectInstance<::mlir::MemoryEffects::Effect>> &effects) {
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::YieldOp)


#endif  // GET_OP_CLASSES

