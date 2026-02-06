/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Enum Utility Declarations                                                  *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: GloinTypes.td                                                        *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

namespace gloin {
// Endianness of integer types
enum class Endianness : uint32_t {
  Little = 0,
  Big = 1,
  Native = 2,
};

::std::optional<Endianness> symbolizeEndianness(uint32_t);
::llvm::StringRef stringifyEndianness(Endianness);
::std::optional<Endianness> symbolizeEndianness(::llvm::StringRef);
inline constexpr unsigned getMaxEnumValForEndianness() {
  return 2;
}


inline ::llvm::StringRef stringifyEnum(Endianness enumValue) {
  return stringifyEndianness(enumValue);
}

template <typename EnumType>
::std::optional<EnumType> symbolizeEnum(::llvm::StringRef);

template <>
inline ::std::optional<Endianness> symbolizeEnum<Endianness>(::llvm::StringRef str) {
  return symbolizeEndianness(str);
}

class EndiannessAttr : public ::mlir::IntegerAttr {
public:
  using ValueType = Endianness;
  using ::mlir::IntegerAttr::IntegerAttr;
  static bool classof(::mlir::Attribute attr);
  static EndiannessAttr get(::mlir::MLIRContext *context, Endianness val);
  Endianness getValue() const;
};
} // namespace gloin

namespace mlir {
template <typename T, typename>
struct FieldParser;

template<>
struct FieldParser<::gloin::Endianness, ::gloin::Endianness> {
  template <typename ParserT>
  static FailureOr<::gloin::Endianness> parse(ParserT &parser) {
    // Parse the keyword/string containing the enum.
    std::string enumKeyword;
    auto loc = parser.getCurrentLocation();
    if (failed(parser.parseOptionalKeywordOrString(&enumKeyword)))
      return parser.emitError(loc, "expected keyword for Endianness of integer types");

    // Symbolize the keyword.
    if (::std::optional<::gloin::Endianness> attr = ::gloin::symbolizeEnum<::gloin::Endianness>(enumKeyword))
      return *attr;
    return parser.emitError(loc, "invalid Endianness of integer types specification: ") << enumKeyword;
  }
};
} // namespace mlir

namespace llvm {
inline ::llvm::raw_ostream &operator<<(::llvm::raw_ostream &p, ::gloin::Endianness value) {
  auto valueStr = stringifyEnum(value);
  return p << valueStr;
}
} // namespace llvm

namespace llvm {
template<> struct DenseMapInfo<::gloin::Endianness> {
  using StorageInfo = ::llvm::DenseMapInfo<uint32_t>;

  static inline ::gloin::Endianness getEmptyKey() {
    return static_cast<::gloin::Endianness>(StorageInfo::getEmptyKey());
  }

  static inline ::gloin::Endianness getTombstoneKey() {
    return static_cast<::gloin::Endianness>(StorageInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const ::gloin::Endianness &val) {
    return StorageInfo::getHashValue(static_cast<uint32_t>(val));
  }

  static bool isEqual(const ::gloin::Endianness &lhs, const ::gloin::Endianness &rhs) {
    return lhs == rhs;
  }
};
}

