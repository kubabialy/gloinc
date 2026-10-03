#include "sema.h"

std::shared_ptr<Type> Sema::check_memory_primitive(const CallExpression *call,
                                                    MemoryPrimitive kind) {
    auto pointer = std::make_shared<PointerType>(get_builtin_type("u8"), true, false);
    std::vector<std::shared_ptr<Type>> parameters;
    if (kind == MemoryPrimitive::Allocate)
        parameters = {get_builtin_type("i64"), get_builtin_type("i64")};
    else
        parameters = {pointer};
    if (call->arguments.size() != parameters.size()) {
        log_error("Incorrect number of raw memory primitive arguments");
        return nullptr;
    }
    for (size_t i = 0; i < parameters.size(); ++i) {
        auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
        if (!actual || !actual->equals(*parameters[i])) {
            log_error("Raw memory primitive argument type mismatch");
            return nullptr;
        }
    }
    recording->memory_runtime_calls.emplace(call, kind);
    return kind == MemoryPrimitive::Allocate ? std::shared_ptr<Type>(pointer)
                                             : get_builtin_type("void");
}

std::shared_ptr<Type> Sema::check_memory_intrinsic(const CallExpression *call,
                                                   const std::string &name,
                                                   const std::vector<std::string> &types) {
    if (types.size() != 1) {
        log_error("Raw memory intrinsic expects exactly one type argument");
        return nullptr;
    }
    auto type = resolve_type_from_string(types.front());
    if (!type || !value_type(type) || dynamic_cast<VoidType *>(type.get())) {
        log_error("Invalid raw memory intrinsic type argument");
        return nullptr;
    }
    const auto kind = name == "__memory_size_of" ? MemoryIntrinsic::SizeOf :
                      name == "__memory_align_of" ? MemoryIntrinsic::AlignOf :
                      name == "__memory_padding" ? MemoryIntrinsic::Padding :
                                                   MemoryIntrinsic::Place;
    const auto expected = kind == MemoryIntrinsic::Place ? 3u :
                          kind == MemoryIntrinsic::Padding ? 1u : 0u;
    if (call->arguments.size() != expected) {
        log_error("Incorrect number of raw memory intrinsic arguments");
        return nullptr;
    }
    if (kind == MemoryIntrinsic::Place || kind == MemoryIntrinsic::Padding) {
        const std::vector<std::shared_ptr<Type>> parameters{
            std::make_shared<PointerType>(get_builtin_type("u8"), true, false),
            get_builtin_type("i64"), type};
        for (size_t i = 0; i < expected; ++i) {
            auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
            if (!actual || !actual->equals(*parameters[i])) {
                log_error("Raw memory placement argument type mismatch");
                return nullptr;
            }
        }
    }
    recording->memory_intrinsics.emplace(call, std::make_pair(kind, *value_type(type)));
    return kind == MemoryIntrinsic::Place
               ? std::shared_ptr<Type>(std::make_shared<PointerType>(type, true, false))
               : get_builtin_type("i64");
}
