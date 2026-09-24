#ifndef GLOINC_OPERATORS_H
#define GLOINC_OPERATORS_H

#include "numeric.h"

// Shared by constant and runtime checking. Both binary operands must already
// have the same canonical type before consulting this table.
inline std::optional<CoreType> unary_operator_type(std::string_view op, CoreType operand) {
    if ((op == "!" && operand == CoreType::Bool) ||
        (op == "-" && core_type_info(operand).is_signed))
        return operand;
    return std::nullopt;
}

inline std::optional<CoreType> binary_operator_type(std::string_view op, CoreType operand) {
    bool boolean = operand == CoreType::Bool;
    bool integer = core_type_info(operand).is_integer;
    bool numeric = integer || operand == CoreType::F32 || operand == CoreType::F64;
    if (((boolean || numeric) && (op == "==" || op == "!=")) ||
        (boolean && (op == "&&" || op == "||")) ||
        (numeric && (op == "<" || op == "<=" || op == ">" || op == ">=")))
        return CoreType::Bool;
    if ((numeric && (op == "+" || op == "-" || op == "*" || op == "/")) || (integer && op == "%"))
        return operand;
    return std::nullopt;
}

#endif
