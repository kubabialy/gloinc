#ifndef GLOINC_COMPILATION_MODE_H
#define GLOINC_COMPILATION_MODE_H

// Module permits helper-only source. Object and Executable require main() -> i32;
// Object preserves functions for external callers, Executable is rooted at main.
enum class CompilationMode { Module, Executable, Object };

#endif
