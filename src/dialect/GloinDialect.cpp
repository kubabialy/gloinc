#include "GloinDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h" // Added for DialectAsmParser/Printer
#include "llvm/ADT/TypeSwitch.h"

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
