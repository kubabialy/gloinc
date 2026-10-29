#ifndef GLOINC_PACKED_IR_H
#define GLOINC_PACKED_IR_H

#include "dialect/GloinDialect.h"
#include "mlir/IR/PatternMatch.h"

mlir::LogicalResult verify_packed_definition(gloin::StructDefinitionOp definition);
void populate_packed_lowering(mlir::RewritePatternSet &patterns);

#endif
