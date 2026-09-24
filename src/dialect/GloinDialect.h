#ifndef GLOIN_DIALECT_H
#define GLOIN_DIALECT_H

#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Bytecode/BytecodeOpInterface.h" // Corrected path

// Include generated Enums
#include "src/dialect/GloinEnums.h.inc"

// Include generated Dialect decls
#include "src/dialect/GloinDialect.h.inc"

// Include generated Type decls
#define GET_TYPEDEF_CLASSES
#include "src/dialect/GloinTypes.h.inc"

// Include generated Op decls
#define GET_OP_CLASSES
#include "src/dialect/GloinOps.h.inc"

#endif // GLOIN_DIALECT_H
