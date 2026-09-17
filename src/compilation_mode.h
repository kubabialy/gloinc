#ifndef GLOINC_COMPILATION_MODE_H
#define GLOINC_COMPILATION_MODE_H

// Module compilation permits helper-only source; executables require main() -> i32.
enum class CompilationMode { Module, Executable };

#endif
