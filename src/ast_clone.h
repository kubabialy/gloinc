#ifndef GLOINC_AST_CLONE_H
#define GLOINC_AST_CLONE_H

#include "AST.h"

// Checked specializations need distinct AST node identities for every concrete
// function or method body, because semantic bindings are keyed by node address.
std::unique_ptr<FunctionDefinition> clone_function(const FunctionDefinition &function);

#endif
