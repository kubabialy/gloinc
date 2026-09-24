#ifndef GLOINC_MODULE_LOADER_H
#define GLOINC_MODULE_LOADER_H

#include "AST.h"
#include "diagnostics.h"

bool load_modules(std::vector<std::unique_ptr<Statement>> &program, const std::string &filename,
                  const std::string &directory, const std::shared_ptr<Diagnostics> &diagnostics);

#endif
