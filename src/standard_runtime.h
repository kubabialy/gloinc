#ifndef GLOINC_STANDARD_RUNTIME_H
#define GLOINC_STANDARD_RUNTIME_H

// Reserved backend names cannot be spelled as source identifiers.
// Runtime ABI: void(const char *bytes, uint64_t length).
inline constexpr char standard_output_symbol[] = "gloin.runtime.output";

#endif
