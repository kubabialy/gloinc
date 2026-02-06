/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Enum Utility Definitions                                                   *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: GloinTypes.td                                                        *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

namespace gloin {
::llvm::StringRef stringifyEndianness(Endianness val) {
  switch (val) {
    case Endianness::Little: return "Little";
    case Endianness::Big: return "Big";
    case Endianness::Native: return "Native";
  }
  return "";
}

::std::optional<Endianness> symbolizeEndianness(::llvm::StringRef str) {
  return ::llvm::StringSwitch<::std::optional<Endianness>>(str)
      .Case("Little", Endianness::Little)
      .Case("Big", Endianness::Big)
      .Case("Native", Endianness::Native)
      .Default(::std::nullopt);
}
::std::optional<Endianness> symbolizeEndianness(uint32_t value) {
  switch (value) {
  case 0: return Endianness::Little;
  case 1: return Endianness::Big;
  case 2: return Endianness::Native;
  default: return ::std::nullopt;
  }
}

bool EndiannessAttr::classof(::mlir::Attribute attr) {
  return (((::llvm::isa<::mlir::IntegerAttr>(attr))) && ((::llvm::cast<::mlir::IntegerAttr>(attr).getType().isSignlessInteger(32)))) && (((::llvm::cast<::mlir::IntegerAttr>(attr).getInt() == 0)) || ((::llvm::cast<::mlir::IntegerAttr>(attr).getInt() == 1)) || ((::llvm::cast<::mlir::IntegerAttr>(attr).getInt() == 2)));
}
EndiannessAttr EndiannessAttr::get(::mlir::MLIRContext *context, Endianness val) {
  ::mlir::IntegerType intType = ::mlir::IntegerType::get(context, 32);
  ::mlir::IntegerAttr baseAttr = ::mlir::IntegerAttr::get(intType, static_cast<uint32_t>(val));
  return ::llvm::cast<EndiannessAttr>(baseAttr);
}
Endianness EndiannessAttr::getValue() const {
  return static_cast<Endianness>(::mlir::IntegerAttr::getInt());
}
} // namespace gloin

