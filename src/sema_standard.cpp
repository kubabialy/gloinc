#include "sema.h"

std::shared_ptr<Type> Sema::check_standard_primitive(const CallExpression *call,
                                                     StandardPrimitive kind) {
    auto reference = [&](const char *name) {
        return std::make_shared<PointerType>(get_builtin_type(name), false, false);
    };
    std::vector<std::shared_ptr<Type>> parameters;
    auto byte_pointer = std::make_shared<PointerType>(get_builtin_type("u8"), true, false);
    if (kind == StandardPrimitive::TimeMonotonic)
        parameters = {reference("u64"), reference("i32")};
    else if (kind == StandardPrimitive::RandomSplitMix64)
        parameters = {reference("u64")};
    else if (kind >= StandardPrimitive::MathUnaryF32 && kind <= StandardPrimitive::MathBinaryF64) {
        const char *scalar =
            kind == StandardPrimitive::MathUnaryF32 || kind == StandardPrimitive::MathBinaryF32
                ? "f32"
                : "f64";
        parameters = {get_builtin_type("i32"), get_builtin_type(scalar)};
        if (kind == StandardPrimitive::MathBinaryF32 || kind == StandardPrimitive::MathBinaryF64)
            parameters.push_back(get_builtin_type(scalar));
        parameters.push_back(reference(scalar));
    } else if (kind == StandardPrimitive::FsMetadata)
        parameters = {get_builtin_type("string"), reference("i32"), reference("u64"),
                      reference("i32")};
    else if (kind == StandardPrimitive::FsMkdir || kind == StandardPrimitive::FsRemoveFile)
        parameters = {get_builtin_type("string"), reference("i32")};
    else if (kind == StandardPrimitive::FsRenameReplace)
        parameters = {get_builtin_type("string"), get_builtin_type("string"), reference("i32")};
    else if (kind == StandardPrimitive::ProcessCount)
        parameters = {};
    else if (kind == StandardPrimitive::ProcessArg || kind == StandardPrimitive::ProcessEnv)
        parameters = {get_builtin_type(kind == StandardPrimitive::ProcessArg ? "u64" : "string"),
                      reference("u64"), reference("i32")};
    else if (kind == StandardPrimitive::ProcessCwd)
        parameters = {reference("u8"), get_builtin_type("u64"), reference("u64"), reference("i32")};
    else if (kind == StandardPrimitive::IoStandard)
        parameters = {get_builtin_type("i32")};
    else if (kind == StandardPrimitive::IoOpen)
        parameters = {get_builtin_type("string"), get_builtin_type("i32"), reference("i32"),
                      reference("i32")};
    else if (kind == StandardPrimitive::IoRead)
        parameters = {
            byte_pointer,     reference("u8"), get_builtin_type("u64"), get_builtin_type("i32"),
            reference("u64"), reference("i32")};
    else if (kind == StandardPrimitive::IoWrite)
        parameters = {byte_pointer, get_builtin_type("string"), get_builtin_type("i32"),
                      reference("u64"), reference("i32")};
    else if (kind == StandardPrimitive::IoFlush || kind == StandardPrimitive::IoClose)
        parameters = {byte_pointer, reference("i32")};
    else if (kind == StandardPrimitive::IoErrorMessage)
        parameters = {get_builtin_type("i32"), reference("u8"), reference("u64")};
    else if (auto spec = numeric_signature(kind)) {
        if (spec->group == NumericPrimitiveGroup::Parse)
            parameters = {get_builtin_type("string"), reference(spec->output)};
        else {
            parameters = {get_builtin_type(spec->input)};
            if (spec->group == NumericPrimitiveGroup::Fixed)
                parameters.push_back(get_builtin_type("u32"));
            if (spec->mode)
                parameters.push_back(get_builtin_type("i32"));
            if (spec->group != NumericPrimitiveGroup::Convert)
                parameters.push_back(reference("u8"));
            parameters.push_back(reference(spec->output));
        }
    } else if (kind == StandardPrimitive::ParseI32)
        parameters = {get_builtin_type("string"), reference("i32")};
    else if (kind == StandardPrimitive::FormatI32)
        parameters = {get_builtin_type("i32"), reference("u8"), reference("u64")};
    else if (kind == StandardPrimitive::Input)
        parameters = {reference("u8"), get_builtin_type("u64"), reference("u64")};
    else if (kind == StandardPrimitive::StringCopy)
        parameters = {get_builtin_type("string"), reference("u8")};
    else if (kind == StandardPrimitive::StringLength)
        parameters = {get_builtin_type("string")};
    else if (kind == StandardPrimitive::StringByte)
        parameters = {get_builtin_type("string"), get_builtin_type("u64")};
    else if (kind == StandardPrimitive::StringSlice)
        parameters = {get_builtin_type("string"), get_builtin_type("u64"), get_builtin_type("u64")};
    else if (kind == StandardPrimitive::StringStore || kind == StandardPrimitive::StringWrite)
        parameters = {std::make_shared<PointerType>(get_builtin_type("u8"), true, false),
                      get_builtin_type("u64"), get_builtin_type("u64"),
                      get_builtin_type(kind == StandardPrimitive::StringStore ? "u8" : "string")};
    else
        parameters = {std::make_shared<PointerType>(get_builtin_type("u8"), true, true),
                      get_builtin_type("u64")};
    if (call->arguments.size() != parameters.size()) {
        log_error("Incorrect number of standard-library primitive arguments");
        return nullptr;
    }
    for (size_t i = 0; i < parameters.size(); ++i) {
        auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
        if (!actual)
            return nullptr;
        if (!actual->equals(*parameters[i])) {
            log_error("Standard-library primitive argument type mismatch");
            return nullptr;
        }
    }
    recording->standard_calls.emplace(call, kind);
    if (kind == StandardPrimitive::IoStandard || kind == StandardPrimitive::IoOpen ||
        kind == StandardPrimitive::ProcessArg || kind == StandardPrimitive::ProcessEnv)
        return byte_pointer;
    return get_builtin_type(
        (kind == StandardPrimitive::StringLength || kind == StandardPrimitive::ProcessCount ||
         kind == StandardPrimitive::RandomSplitMix64)
            ? "u64"
        : kind == StandardPrimitive::StringByte ? "u8"
        : kind == StandardPrimitive::StringCopy || kind == StandardPrimitive::StringSlice ||
                kind == StandardPrimitive::StringView ||
                kind == StandardPrimitive::StringBufferView ||
                kind == StandardPrimitive::IoStringView ||
                kind == StandardPrimitive::ProcessStringView
            ? "string"
        : kind == StandardPrimitive::FormatI32 || kind == StandardPrimitive::StringStore ||
                kind == StandardPrimitive::StringWrite
            ? "void"
            : "i32");
}
