#pragma once
#include "gir_range_proofs.h"

// Version bounded loops over initialized, unescaped floating arrays. The fast
// copy carries a persistent validity flag and traps before leaving the loop.
void version_gloin_float_batches(mlir::ModuleOp module, GloinRangeProofs &proofs);
