#include "numeric.h"
#include "llvm/Support/Error.h"
#include <charconv>
#include <limits>

const llvm::fltSemantics &float_semantics(CoreType type) {
    return type == CoreType::F32 ? llvm::APFloat::IEEEsingle() : llvm::APFloat::IEEEdouble();
}

std::optional<ConstantValue> parse_numeric_literal(std::string_view spelling, bool floating,
                                                   CoreType type, bool negative,
                                                   std::string &error) {
    const auto &info = core_type_info(type);
    if (!floating) {
        if (!info.is_integer) {
            error = "Integer literal requires an integer type";
            return std::nullopt;
        }
        if (negative && !info.is_signed) {
            error = "Negative integer literal cannot have an unsigned type";
            return std::nullopt;
        }
        int base = 10;
        if (spelling.starts_with("0x") || spelling.starts_with("0X")) {
            base = 16;
            spelling.remove_prefix(2);
        } else if (spelling.starts_with("0b") || spelling.starts_with("0B")) {
            base = 2;
            spelling.remove_prefix(2);
        }
        if (spelling.empty()) {
            error = "Malformed integer literal";
            return std::nullopt;
        }
        uint64_t magnitude = 0;
        auto [end, status] =
            std::from_chars(spelling.data(), spelling.data() + spelling.size(), magnitude, base);
        if (spelling.empty() || status == std::errc::invalid_argument ||
            end != spelling.data() + spelling.size()) {
            error = "Malformed integer literal";
            return std::nullopt;
        }
        uint64_t limit = info.is_signed ? (uint64_t{1} << (info.bits - 1)) - (negative ? 0 : 1)
                                        : (info.bits == 64 ? std::numeric_limits<uint64_t>::max()
                                                           : (uint64_t{1} << info.bits) - 1);
        if (status == std::errc::result_out_of_range || magnitude > limit) {
            error = "Integer literal is out of range for " + std::string(info.name);
            return std::nullopt;
        }
        llvm::APInt value(info.bits, magnitude);
        if (negative)
            value = -value;
        return ConstantValue{type, std::move(value)};
    }
    if (type != CoreType::F32 && type != CoreType::F64) {
        error = "Floating literal requires f32 or f64";
        return std::nullopt;
    }
    // Validate the entire decimal spelling even for ASTs built without the lexer.
    size_t cursor = 0;
    bool nonzero = false;
    auto digits = [&](bool mantissa) {
        size_t start = cursor;
        while (cursor < spelling.size() && spelling[cursor] >= '0' && spelling[cursor] <= '9') {
            if (mantissa && spelling[cursor] != '0')
                nonzero = true;
            ++cursor;
        }
        return cursor != start;
    };
    bool valid = digits(true), has_float_part = false;
    if (cursor < spelling.size() && spelling[cursor] == '.') {
        ++cursor;
        has_float_part = true;
        valid &= digits(true);
    }
    if (cursor < spelling.size() && (spelling[cursor] == 'e' || spelling[cursor] == 'E')) {
        ++cursor;
        has_float_part = true;
        if (cursor < spelling.size() && (spelling[cursor] == '+' || spelling[cursor] == '-'))
            ++cursor;
        valid &= digits(false);
    }
    if (!valid || !has_float_part || cursor != spelling.size()) {
        error = "Malformed floating literal";
        return std::nullopt;
    }
    llvm::APFloat value(float_semantics(type));
    auto status =
        value.convertFromString(llvm::StringRef(spelling), llvm::APFloat::rmNearestTiesToEven);
    if (!status) {
        error = "Malformed floating literal: " + llvm::toString(status.takeError());
        return std::nullopt;
    }
    if (!value.isFinite() || (*status & llvm::APFloat::opOverflow)) {
        error = "Floating literal is out of range for " + std::string(info.name);
        return std::nullopt;
    }
    if (nonzero && value.isZero()) {
        error = "Nonzero floating literal underflows to zero in " + std::string(info.name);
        return std::nullopt;
    }
    if (negative)
        value.changeSign();
    return ConstantValue{type, std::move(value)};
}
