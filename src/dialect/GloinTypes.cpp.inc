/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* TypeDef Definitions                                                        *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifdef GET_TYPEDEF_LIST
#undef GET_TYPEDEF_LIST

::gloin::GloinIntegerType,
::gloin::GloinPointerType,
::gloin::GloinDeferredType,
::gloin::GloinSpawnType,
::gloin::GloinResultType,
::gloin::GloinStructType

#endif  // GET_TYPEDEF_LIST

#ifdef GET_TYPEDEF_CLASSES
#undef GET_TYPEDEF_CLASSES

static ::mlir::OptionalParseResult generatedTypeParser(::mlir::AsmParser &parser, ::llvm::StringRef *mnemonic, ::mlir::Type &value) {
  return ::mlir::AsmParser::KeywordSwitch<::mlir::OptionalParseResult>(parser)
    .Case(::gloin::GloinIntegerType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinIntegerType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Case(::gloin::GloinPointerType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinPointerType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Case(::gloin::GloinDeferredType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinDeferredType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Case(::gloin::GloinSpawnType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinSpawnType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Case(::gloin::GloinResultType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinResultType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Case(::gloin::GloinStructType::getMnemonic(), [&](llvm::StringRef, llvm::SMLoc) {
      value = ::gloin::GloinStructType::parse(parser);
      return ::mlir::success(!!value);
    })
    .Default([&](llvm::StringRef keyword, llvm::SMLoc) {
      *mnemonic = keyword;
      return std::nullopt;
    });
}

static ::mlir::LogicalResult generatedTypePrinter(::mlir::Type def, ::mlir::AsmPrinter &printer) {
  return ::llvm::TypeSwitch<::mlir::Type, ::mlir::LogicalResult>(def)    .Case<::gloin::GloinIntegerType>([&](auto t) {
      printer << ::gloin::GloinIntegerType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Case<::gloin::GloinPointerType>([&](auto t) {
      printer << ::gloin::GloinPointerType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Case<::gloin::GloinDeferredType>([&](auto t) {
      printer << ::gloin::GloinDeferredType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Case<::gloin::GloinSpawnType>([&](auto t) {
      printer << ::gloin::GloinSpawnType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Case<::gloin::GloinResultType>([&](auto t) {
      printer << ::gloin::GloinResultType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Case<::gloin::GloinStructType>([&](auto t) {
      printer << ::gloin::GloinStructType::getMnemonic();
t.print(printer);
      return ::mlir::success();
    })
    .Default([](auto) { return ::mlir::failure(); });
}

namespace gloin {
namespace detail {
struct GloinIntegerTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<unsigned, Endianness>;
  GloinIntegerTypeStorage(unsigned width, Endianness endianness) : width(std::move(width)), endianness(std::move(endianness)) {}

  KeyTy getAsKey() const {
    return KeyTy(width, endianness);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (width == std::get<0>(tblgenKey)) && (endianness == std::get<1>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey), std::get<1>(tblgenKey));
  }

  static GloinIntegerTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto width = std::move(std::get<0>(tblgenKey));
    auto endianness = std::move(std::get<1>(tblgenKey));
    return new (allocator.allocate<GloinIntegerTypeStorage>()) GloinIntegerTypeStorage(std::move(width), std::move(endianness));
  }

  unsigned width;
  Endianness endianness;
};
} // namespace detail
GloinIntegerType GloinIntegerType::get(::mlir::MLIRContext *context, unsigned width, Endianness endianness) {
  return Base::get(context, std::move(width), std::move(endianness));
}

::mlir::Type GloinIntegerType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<unsigned> _result_width;
  ::mlir::FailureOr<Endianness> _result_endianness;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'width'
  _result_width = ::mlir::FieldParser<unsigned>::parse(odsParser);
  if (::mlir::failed(_result_width)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinInteger parameter 'width' which is to be a `unsigned`");
    return {};
  }
  // Parse literal ','
  if (odsParser.parseComma()) return {};

  // Parse variable 'endianness'
  _result_endianness = ::mlir::FieldParser<Endianness>::parse(odsParser);
  if (::mlir::failed(_result_endianness)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinInteger parameter 'endianness' which is to be a `Endianness`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_width));
  assert(::mlir::succeeded(_result_endianness));
  return GloinIntegerType::get(odsParser.getContext(),
      unsigned((*_result_width)),
      Endianness((*_result_endianness)));
}

void GloinIntegerType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printStrippedAttrOrType(getWidth());
  odsPrinter << ",";
  odsPrinter << ' ';
  odsPrinter.printStrippedAttrOrType(getEndianness());
  odsPrinter << ">";
}

unsigned GloinIntegerType::getWidth() const {
  return getImpl()->width;
}

Endianness GloinIntegerType::getEndianness() const {
  return getImpl()->endianness;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinIntegerType)
namespace gloin {
namespace detail {
struct GloinPointerTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<Type>;
  GloinPointerTypeStorage(Type pointee) : pointee(std::move(pointee)) {}

  KeyTy getAsKey() const {
    return KeyTy(pointee);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (pointee == std::get<0>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey));
  }

  static GloinPointerTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto pointee = std::move(std::get<0>(tblgenKey));
    return new (allocator.allocate<GloinPointerTypeStorage>()) GloinPointerTypeStorage(std::move(pointee));
  }

  Type pointee;
};
} // namespace detail
GloinPointerType GloinPointerType::get(::mlir::MLIRContext *context, Type pointee) {
  return Base::get(context, std::move(pointee));
}

::mlir::Type GloinPointerType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<Type> _result_pointee;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'pointee'
  _result_pointee = ::mlir::FieldParser<Type>::parse(odsParser);
  if (::mlir::failed(_result_pointee)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinPointer parameter 'pointee' which is to be a `Type`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_pointee));
  return GloinPointerType::get(odsParser.getContext(),
      Type((*_result_pointee)));
}

void GloinPointerType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printStrippedAttrOrType(getPointee());
  odsPrinter << ">";
}

Type GloinPointerType::getPointee() const {
  return getImpl()->pointee;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinPointerType)
namespace gloin {
namespace detail {
struct GloinDeferredTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<Type>;
  GloinDeferredTypeStorage(Type valueType) : valueType(std::move(valueType)) {}

  KeyTy getAsKey() const {
    return KeyTy(valueType);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (valueType == std::get<0>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey));
  }

  static GloinDeferredTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto valueType = std::move(std::get<0>(tblgenKey));
    return new (allocator.allocate<GloinDeferredTypeStorage>()) GloinDeferredTypeStorage(std::move(valueType));
  }

  Type valueType;
};
} // namespace detail
GloinDeferredType GloinDeferredType::get(::mlir::MLIRContext *context, Type valueType) {
  return Base::get(context, std::move(valueType));
}

::mlir::Type GloinDeferredType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<Type> _result_valueType;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'valueType'
  _result_valueType = ::mlir::FieldParser<Type>::parse(odsParser);
  if (::mlir::failed(_result_valueType)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinDeferred parameter 'valueType' which is to be a `Type`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_valueType));
  return GloinDeferredType::get(odsParser.getContext(),
      Type((*_result_valueType)));
}

void GloinDeferredType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printStrippedAttrOrType(getValueType());
  odsPrinter << ">";
}

Type GloinDeferredType::getValueType() const {
  return getImpl()->valueType;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinDeferredType)
namespace gloin {
namespace detail {
struct GloinSpawnTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<Type>;
  GloinSpawnTypeStorage(Type valueType) : valueType(std::move(valueType)) {}

  KeyTy getAsKey() const {
    return KeyTy(valueType);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (valueType == std::get<0>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey));
  }

  static GloinSpawnTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto valueType = std::move(std::get<0>(tblgenKey));
    return new (allocator.allocate<GloinSpawnTypeStorage>()) GloinSpawnTypeStorage(std::move(valueType));
  }

  Type valueType;
};
} // namespace detail
GloinSpawnType GloinSpawnType::get(::mlir::MLIRContext *context, Type valueType) {
  return Base::get(context, std::move(valueType));
}

::mlir::Type GloinSpawnType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<Type> _result_valueType;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'valueType'
  _result_valueType = ::mlir::FieldParser<Type>::parse(odsParser);
  if (::mlir::failed(_result_valueType)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinSpawn parameter 'valueType' which is to be a `Type`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_valueType));
  return GloinSpawnType::get(odsParser.getContext(),
      Type((*_result_valueType)));
}

void GloinSpawnType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printStrippedAttrOrType(getValueType());
  odsPrinter << ">";
}

Type GloinSpawnType::getValueType() const {
  return getImpl()->valueType;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinSpawnType)
namespace gloin {
namespace detail {
struct GloinResultTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<Type, Type>;
  GloinResultTypeStorage(Type okType, Type errType) : okType(std::move(okType)), errType(std::move(errType)) {}

  KeyTy getAsKey() const {
    return KeyTy(okType, errType);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (okType == std::get<0>(tblgenKey)) && (errType == std::get<1>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey), std::get<1>(tblgenKey));
  }

  static GloinResultTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto okType = std::move(std::get<0>(tblgenKey));
    auto errType = std::move(std::get<1>(tblgenKey));
    return new (allocator.allocate<GloinResultTypeStorage>()) GloinResultTypeStorage(std::move(okType), std::move(errType));
  }

  Type okType;
  Type errType;
};
} // namespace detail
GloinResultType GloinResultType::get(::mlir::MLIRContext *context, Type okType, Type errType) {
  return Base::get(context, std::move(okType), std::move(errType));
}

::mlir::Type GloinResultType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<Type> _result_okType;
  ::mlir::FailureOr<Type> _result_errType;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'okType'
  _result_okType = ::mlir::FieldParser<Type>::parse(odsParser);
  if (::mlir::failed(_result_okType)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinResult parameter 'okType' which is to be a `Type`");
    return {};
  }
  // Parse literal ','
  if (odsParser.parseComma()) return {};

  // Parse variable 'errType'
  _result_errType = ::mlir::FieldParser<Type>::parse(odsParser);
  if (::mlir::failed(_result_errType)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinResult parameter 'errType' which is to be a `Type`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_okType));
  assert(::mlir::succeeded(_result_errType));
  return GloinResultType::get(odsParser.getContext(),
      Type((*_result_okType)),
      Type((*_result_errType)));
}

void GloinResultType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printStrippedAttrOrType(getOkType());
  odsPrinter << ",";
  odsPrinter << ' ';
  odsPrinter.printStrippedAttrOrType(getErrType());
  odsPrinter << ">";
}

Type GloinResultType::getOkType() const {
  return getImpl()->okType;
}

Type GloinResultType::getErrType() const {
  return getImpl()->errType;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinResultType)
namespace gloin {
namespace detail {
struct GloinStructTypeStorage : public ::mlir::TypeStorage {
  using KeyTy = std::tuple<::llvm::StringRef>;
  GloinStructTypeStorage(::llvm::StringRef name) : name(std::move(name)) {}

  KeyTy getAsKey() const {
    return KeyTy(name);
  }

  bool operator==(const KeyTy &tblgenKey) const {
    return (name == std::get<0>(tblgenKey));
  }

  static ::llvm::hash_code hashKey(const KeyTy &tblgenKey) {
    return ::llvm::hash_combine(std::get<0>(tblgenKey));
  }

  static GloinStructTypeStorage *construct(::mlir::TypeStorageAllocator &allocator, KeyTy &&tblgenKey) {
    auto name = std::move(std::get<0>(tblgenKey));
    name = allocator.copyInto(name);
    return new (allocator.allocate<GloinStructTypeStorage>()) GloinStructTypeStorage(std::move(name));
  }

  ::llvm::StringRef name;
};
} // namespace detail
GloinStructType GloinStructType::get(::mlir::MLIRContext *context, ::llvm::StringRef name) {
  return Base::get(context, std::move(name));
}

::mlir::Type GloinStructType::parse(::mlir::AsmParser &odsParser) {
  ::mlir::Builder odsBuilder(odsParser.getContext());
  ::llvm::SMLoc odsLoc = odsParser.getCurrentLocation();
  (void) odsLoc;
  ::mlir::FailureOr<std::string> _result_name;
  // Parse literal '<'
  if (odsParser.parseLess()) return {};

  // Parse variable 'name'
  _result_name = ::mlir::FieldParser<std::string>::parse(odsParser);
  if (::mlir::failed(_result_name)) {
    odsParser.emitError(odsParser.getCurrentLocation(), "failed to parse GloinStruct parameter 'name' which is to be a `::llvm::StringRef`");
    return {};
  }
  // Parse literal '>'
  if (odsParser.parseGreater()) return {};
  assert(::mlir::succeeded(_result_name));
  return GloinStructType::get(odsParser.getContext(),
      ::llvm::StringRef((*_result_name)));
}

void GloinStructType::print(::mlir::AsmPrinter &odsPrinter) const {
  ::mlir::Builder odsBuilder(getContext());
  odsPrinter << "<";
  odsPrinter.printString(getName());;
  odsPrinter << ">";
}

::llvm::StringRef GloinStructType::getName() const {
  return getImpl()->name;
}

} // namespace gloin
MLIR_DEFINE_EXPLICIT_TYPE_ID(::gloin::GloinStructType)
namespace gloin {

/// Parse a type registered to this dialect.
::mlir::Type GloinDialect::parseType(::mlir::DialectAsmParser &parser) const {
  ::llvm::SMLoc typeLoc = parser.getCurrentLocation();
  ::llvm::StringRef mnemonic;
  ::mlir::Type genType;
  auto parseResult = generatedTypeParser(parser, &mnemonic, genType);
  if (parseResult.has_value())
    return genType;
  
  parser.emitError(typeLoc) << "unknown  type `"
      << mnemonic << "` in dialect `" << getNamespace() << "`";
  return {};
}
/// Print a type registered to this dialect.
void GloinDialect::printType(::mlir::Type type,
                    ::mlir::DialectAsmPrinter &printer) const {
  if (::mlir::succeeded(generatedTypePrinter(type, printer)))
    return;
  
}
} // namespace gloin

#endif  // GET_TYPEDEF_CLASSES

