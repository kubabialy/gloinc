/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* TypeDef Declarations                                                       *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifdef GET_TYPEDEF_CLASSES
#undef GET_TYPEDEF_CLASSES


namespace mlir {
class AsmParser;
class AsmPrinter;
} // namespace mlir
namespace gloin {
class GloinIntegerType;
class GloinPointerType;
class GloinDeferredType;
class GloinSpawnType;
class GloinResultType;
class GloinStructType;
namespace detail {
struct GloinIntegerTypeStorage;
} // namespace detail
class GloinIntegerType : public ::mlir::Type::TypeBase<GloinIntegerType, ::mlir::Type, detail::GloinIntegerTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.int";
  static GloinIntegerType get(::mlir::MLIRContext *context, unsigned width, Endianness endianness);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"int"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  unsigned getWidth() const;
  Endianness getEndianness() const;
};
namespace detail {
struct GloinPointerTypeStorage;
} // namespace detail
class GloinPointerType : public ::mlir::Type::TypeBase<GloinPointerType, ::mlir::Type, detail::GloinPointerTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.ptr";
  static GloinPointerType get(::mlir::MLIRContext *context, Type pointee);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"ptr"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  Type getPointee() const;
};
namespace detail {
struct GloinDeferredTypeStorage;
} // namespace detail
class GloinDeferredType : public ::mlir::Type::TypeBase<GloinDeferredType, ::mlir::Type, detail::GloinDeferredTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.deferred";
  static GloinDeferredType get(::mlir::MLIRContext *context, Type valueType);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"deferred"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  Type getValueType() const;
};
namespace detail {
struct GloinSpawnTypeStorage;
} // namespace detail
class GloinSpawnType : public ::mlir::Type::TypeBase<GloinSpawnType, ::mlir::Type, detail::GloinSpawnTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.spawn";
  static GloinSpawnType get(::mlir::MLIRContext *context, Type valueType);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"spawn"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  Type getValueType() const;
};
namespace detail {
struct GloinResultTypeStorage;
} // namespace detail
class GloinResultType : public ::mlir::Type::TypeBase<GloinResultType, ::mlir::Type, detail::GloinResultTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.result";
  static GloinResultType get(::mlir::MLIRContext *context, Type okType, Type errType);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"result"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  Type getOkType() const;
  Type getErrType() const;
};
namespace detail {
struct GloinStructTypeStorage;
} // namespace detail
class GloinStructType : public ::mlir::Type::TypeBase<GloinStructType, ::mlir::Type, detail::GloinStructTypeStorage> {
public:
  using Base::Base;
  static constexpr ::llvm::StringLiteral name = "gloin.struct";
  static GloinStructType get(::mlir::MLIRContext *context, ::llvm::StringRef name);
  static constexpr ::llvm::StringLiteral getMnemonic() {
    return {"struct"};
  }

  static ::mlir::Type parse(::mlir::AsmParser &odsParser);
  void print(::mlir::AsmPrinter &odsPrinter) const;
  ::llvm::StringRef getName() const;
};
} // namespace gloin
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinIntegerType)
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinPointerType)
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinDeferredType)
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinSpawnType)
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinResultType)
MLIR_DECLARE_EXPLICIT_TYPE_ID(::gloin::GloinStructType)

#endif  // GET_TYPEDEF_CLASSES

