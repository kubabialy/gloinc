#ifndef GLOINC_PACKED_IR_H
#define GLOINC_PACKED_IR_H

#include "dialect/GloinDialect.h"
#include "mlir/IR/PatternMatch.h"
#include <optional>

std::optional<uint64_t> packed_integer_width(mlir::Type type);
std::optional<uint64_t> packed_element_width(mlir::Type type, mlir::Operation *owner);
mlir::Type packed_array_layout(gloin::GloinPackedArrayType type, mlir::Operation *owner);
mlir::Type packed_slot_layout(mlir::MLIRContext *context);
mlir::Type packed_slice_layout(mlir::MLIRContext *context);

mlir::LogicalResult verify_packed_definition(gloin::StructDefinitionOp definition);
struct GloinRangeProofs;
void populate_packed_lowering(mlir::RewritePatternSet &patterns, bool little_endian,
                              const GloinRangeProofs &proofs);

#endif
