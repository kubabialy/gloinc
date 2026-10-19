#pragma once

#include "mlir/IR/BuiltinOps.h"

// On verified GIR, let an immediately following checked addition cover its
// single-use product's finite check. The addition remains effectful and checked.
void combine_gloin_float_checks(mlir::ModuleOp module);
