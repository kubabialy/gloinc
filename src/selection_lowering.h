#ifndef GLOINC_SELECTION_LOWERING_H
#define GLOINC_SELECTION_LOWERING_H
#include "mlir/IR/PatternMatch.h"
void populate_selection_lowering(mlir::RewritePatternSet &patterns);
#endif
