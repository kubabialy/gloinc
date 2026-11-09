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
    if (name == "__memory_copy_packed") {
        auto slice = std::dynamic_pointer_cast<SliceType>(type);
        if (!slice || slice->read_only || slice->bit_order == PackedBitOrder::None ||
            call->arguments.size() != 2) {
            log_error("Packed copy requires a writable packed destination and matching source");
            return nullptr;
        }
        const std::vector<std::shared_ptr<Type>> parameters{
            type, std::make_shared<SliceType>(slice->element, true, slice->bit_order)};
        for (size_t i = 0; i < parameters.size(); ++i) {
            auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
            if (!actual || !actual->equals(*parameters[i])) {
                log_error("Packed copy argument encoding or access capability mismatch");
                return nullptr;
            }
        }
        recording->memory_intrinsics.emplace(
            call, std::make_pair(MemoryIntrinsic::PackedCopy, *value_type(type)));
        return get_builtin_type("void");
    }
    if (name == "__memory_packed_bit_offset") {
        auto slice = std::dynamic_pointer_cast<SliceType>(type);
        if (!slice || slice->bit_order == PackedBitOrder::None || call->arguments.size() != 1) {
            log_error("Packed bit offset requires one matching packed slice");
            return nullptr;
        }
        auto actual = check_typed_expression(call->arguments.front().get(), type);
        if (!actual || !actual->equals(*type)) {
            log_error("Packed bit offset slice type mismatch");
            return nullptr;
        }
        recording->memory_intrinsics.emplace(
            call, std::make_pair(MemoryIntrinsic::PackedBitOffset, *value_type(type)));
        return get_builtin_type("u64");
    }
    if (name == "__memory_initialize_slice") {
        auto slice = std::dynamic_pointer_cast<SliceType>(type);
        if (!slice || slice->read_only || call->arguments.size() != 4) {
            log_error(
                "Slice initialization requires a writable slice, storage, extent, count and fill");
            return nullptr;
        }
        const std::vector<std::shared_ptr<Type>> parameters{
            std::make_shared<PointerType>(get_builtin_type("u8"), true, false),
            get_builtin_type("u64"), get_builtin_type("u64"), slice->element};
        for (size_t i = 0; i < parameters.size(); ++i) {
            auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
            if (!actual || !actual->equals(*parameters[i])) {
                log_error("Slice initialization argument type mismatch");
                return nullptr;
            }
        }
        recording->memory_intrinsics.emplace(
            call, std::make_pair(MemoryIntrinsic::InitializeSlice, *value_type(type)));
        return type;
    }
    if (name == "__memory_view_packed") {
        auto slice = std::dynamic_pointer_cast<SliceType>(type);
        if (!slice || slice->bit_order == PackedBitOrder::None || call->arguments.size() != 6) {
            log_error(
                "Packed view construction requires a packed slice type, bytes, bounds and errors");
            return nullptr;
        }
        const std::vector<std::shared_ptr<Type>> parameters{
            std::make_shared<SliceType>(get_builtin_type("u8"), slice->read_only),
            get_builtin_type("u64"),
            get_builtin_type("u64"),
            std::make_shared<ErrorType>(),
            std::make_shared<ErrorType>(),
            std::make_shared<ErrorType>()};
        for (size_t i = 0; i < parameters.size(); ++i) {
            auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
            if (!actual || !actual->equals(*parameters[i])) {
                log_error("Packed view argument type or byte access capability mismatch");
                return nullptr;
            }
        }
        recording->memory_intrinsics.emplace(
            call, std::make_pair(MemoryIntrinsic::PackedView, *value_type(type)));
        return std::make_shared<ResultType>(type);
    }
    if (name == "__memory_slice_unit" || name == "__memory_slice_alignment" ||
        name == "__memory_slice_packed") {
        if (!dynamic_cast<SliceType *>(type.get()) || !call->arguments.empty()) {
            log_error("Slice layout queries require a slice type and no value arguments");
            return nullptr;
        }
        const auto kind = name == "__memory_slice_unit"        ? MemoryIntrinsic::SliceUnit
                          : name == "__memory_slice_alignment" ? MemoryIntrinsic::SliceAlignment
                                                               : MemoryIntrinsic::SlicePacked;
        recording->memory_intrinsics.emplace(call, std::make_pair(kind, *value_type(type)));
        return get_builtin_type(kind == MemoryIntrinsic::SlicePacked ? "bool" : "u64");
    }
    if (name == "__memory_packed_pointer" || name == "__memory_packed_mut_pointer") {
        const auto *record = dynamic_cast<const StructType *>(type.get());
        if (!record || !record->is_packed || record->fields.empty()) {
            log_error("Packed byte access requires a nonempty packed struct type");
            return nullptr;
        }
        const bool read_only = name == "__memory_packed_pointer";
        auto raw = std::make_shared<PointerType>(get_builtin_type("u8"), true, read_only);
        if (call->arguments.size() != 1) {
            log_error("Packed byte access expects one storage pointer");
            return nullptr;
        }
        auto actual = check_typed_expression(call->arguments.front().get(), raw);
        if (!actual || !actual->equals(*raw)) {
            log_error("Packed byte access storage pointer type mismatch");
            return nullptr;
        }
        recording->memory_intrinsics.emplace(
            call, std::make_pair(read_only ? MemoryIntrinsic::PackedPointer
                                           : MemoryIntrinsic::PackedMutPointer,
                                 *value_type(type)));
        return std::make_shared<PointerType>(type, true, read_only);
    }
    const auto kind = name == "__memory_is_aligned" ? MemoryIntrinsic::IsAligned
                      : name == "__memory_size_of"  ? MemoryIntrinsic::SizeOf
                      : name == "__memory_align_of" ? MemoryIntrinsic::AlignOf
                      : name == "__memory_padding"  ? MemoryIntrinsic::Padding
                      : name == "__memory_reserve"  ? MemoryIntrinsic::Reserve
                                                    : MemoryIntrinsic::Place;
    const auto expected = kind == MemoryIntrinsic::IsAligned                                   ? 2u
                          : kind == MemoryIntrinsic::Place || kind == MemoryIntrinsic::Reserve ? 3u
                          : kind == MemoryIntrinsic::Padding                                   ? 1u
                                                                                               : 0u;
    if (call->arguments.size() != expected) {
        log_error("Incorrect number of raw memory intrinsic arguments");
        return nullptr;
    }
    if (kind == MemoryIntrinsic::Place || kind == MemoryIntrinsic::Reserve ||
        kind == MemoryIntrinsic::Padding || kind == MemoryIntrinsic::IsAligned) {
        const std::vector<std::shared_ptr<Type>> parameters{
            std::make_shared<PointerType>(get_builtin_type("u8"), true, false),
            get_builtin_type(kind == MemoryIntrinsic::IsAligned ? "u64" : "i64"),
            kind == MemoryIntrinsic::Reserve ? get_builtin_type("i64") : type};
        for (size_t i = 0; i < expected; ++i) {
            auto actual = check_typed_expression(call->arguments[i].get(), parameters[i]);
            if (!actual || !actual->equals(*parameters[i])) {
                log_error("Raw memory placement argument type mismatch");
                return nullptr;
            }
        }
    }
    recording->memory_intrinsics.emplace(call, std::make_pair(kind, *value_type(type)));
    if (kind == MemoryIntrinsic::IsAligned)
        return get_builtin_type("bool");
    return kind == MemoryIntrinsic::Place || kind == MemoryIntrinsic::Reserve
               ? std::shared_ptr<Type>(std::make_shared<PointerType>(type, true, false))
               : get_builtin_type("i64");
}
