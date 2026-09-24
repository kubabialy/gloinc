#include "codegen.h"
#include "stdlib_lowering.h"

mlir::Value CodeGen::emit_standard_primitive(StandardPrimitive kind, mlir::ValueRange arguments) {
    if (kind == StandardPrimitive::StringStore || kind == StandardPrimitive::StringWrite) {
        auto null = builder.create<mlir::LLVM::ZeroOp>(location(), arguments[0].getType());
        auto nonnull = builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne,
                                                          arguments[0], null);
        auto zero = builder.create<mlir::arith::ConstantIntOp>(location(), 0, 64);
        auto empty = builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::eq,
                                                         arguments[1], zero);
        require_runtime(builder.create<mlir::arith::OrIOp>(location(), nonnull, empty));
        require_runtime(builder.create<mlir::arith::CmpIOp>(location(),
                                                            kind == StandardPrimitive::StringStore
                                                                ? mlir::arith::CmpIPredicate::ult
                                                                : mlir::arith::CmpIPredicate::ule,
                                                            arguments[2], arguments[1]));
        if (kind == StandardPrimitive::StringWrite) {
            auto size = builder.create<mlir::LLVM::ExtractValueOp>(location(), arguments[3],
                                                                   llvm::ArrayRef<int64_t>{1});
            auto remaining =
                builder.create<mlir::arith::SubIOp>(location(), arguments[1], arguments[2]);
            require_runtime(builder.create<mlir::arith::CmpIOp>(
                location(), mlir::arith::CmpIPredicate::ule, size, remaining));
        }
        auto address = builder.create<mlir::LLVM::GEPOp>(
            location(), arguments[0].getType(), builder.getI8Type(), arguments[0],
            llvm::ArrayRef<mlir::LLVM::GEPArg>{arguments[2]});
        if (kind == StandardPrimitive::StringStore)
            builder.create<mlir::LLVM::StoreOp>(location(), arguments[3], address);
        else
            emit_standard_primitive(StandardPrimitive::StringCopy,
                                    mlir::ValueRange{arguments[3], address});
        return {};
    }
    if (kind == StandardPrimitive::StringLength || kind == StandardPrimitive::StringByte ||
        kind == StandardPrimitive::StringSlice) {
        auto size = builder.create<mlir::LLVM::ExtractValueOp>(location(), arguments[0],
                                                               llvm::ArrayRef<int64_t>{1});
        if (kind == StandardPrimitive::StringLength)
            return size;
        // Guard before pointer arithmetic/loading, even in replacement standard files.
        require_runtime(builder.create<mlir::arith::CmpIOp>(location(),
                                                            kind == StandardPrimitive::StringByte
                                                                ? mlir::arith::CmpIPredicate::ult
                                                                : mlir::arith::CmpIPredicate::ule,
                                                            arguments[1], size));
        if (kind == StandardPrimitive::StringSlice) {
            auto remaining = builder.create<mlir::arith::SubIOp>(location(), size, arguments[1]);
            require_runtime(builder.create<mlir::arith::CmpIOp>(
                location(), mlir::arith::CmpIPredicate::ule, arguments[2], remaining));
        }
        auto base = builder.create<mlir::LLVM::ExtractValueOp>(location(), arguments[0],
                                                               llvm::ArrayRef<int64_t>{0});
        auto address =
            builder.create<mlir::LLVM::GEPOp>(location(), base.getType(), builder.getI8Type(), base,
                                              llvm::ArrayRef<mlir::LLVM::GEPArg>{arguments[1]});
        if (kind == StandardPrimitive::StringByte)
            return builder.create<mlir::LLVM::LoadOp>(location(), builder.getI8Type(), address);
        return emit_standard_primitive(StandardPrimitive::StringView,
                                       mlir::ValueRange{address, arguments[2]});
    }
    if (kind == StandardPrimitive::StringView || kind == StandardPrimitive::StringBufferView ||
        kind == StandardPrimitive::IoStringView || kind == StandardPrimitive::ProcessStringView) {
        auto null = builder.create<mlir::LLVM::ZeroOp>(location(), arguments[0].getType());
        auto nonnull = builder.create<mlir::LLVM::ICmpOp>(location(), mlir::LLVM::ICmpPredicate::ne,
                                                          arguments[0], null);
        auto zero = builder.create<mlir::arith::ConstantIntOp>(location(), 0, 64);
        auto empty = builder.create<mlir::arith::CmpIOp>(location(), mlir::arith::CmpIPredicate::eq,
                                                         arguments[1], zero);
        require_runtime(builder.create<mlir::arith::OrIOp>(location(), nonnull, empty));
        auto type = string_type();
        mlir::Value result = builder.create<mlir::LLVM::UndefOp>(location(), type);
        result = builder.create<mlir::LLVM::InsertValueOp>(location(), result, arguments[0],
                                                           llvm::ArrayRef<int64_t>{0});
        return builder.create<mlir::LLVM::InsertValueOp>(location(), result, arguments[1],
                                                         llvm::ArrayRef<int64_t>{1});
    }
    std::vector<mlir::Value> native_arguments;
    const auto numeric = numeric_signature(kind);
    if (kind == StandardPrimitive::FsMetadata || kind == StandardPrimitive::FsMkdir ||
        kind == StandardPrimitive::FsRemoveFile || kind == StandardPrimitive::FsRenameReplace ||
        kind == StandardPrimitive::ProcessEnv) {
        const size_t strings = kind == StandardPrimitive::FsRenameReplace ? 2 : 1;
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (i < strings) {
                native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
                    location(), arguments[i], llvm::ArrayRef<int64_t>{0}));
                native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
                    location(), arguments[i], llvm::ArrayRef<int64_t>{1}));
            } else
                native_arguments.push_back(arguments[i]);
        }
    } else if (kind == StandardPrimitive::IoOpen || kind == StandardPrimitive::IoWrite) {
        const size_t index = kind == StandardPrimitive::IoOpen ? 0 : 1;
        if (index)
            native_arguments.push_back(arguments[0]);
        native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
            location(), arguments[index], llvm::ArrayRef<int64_t>{0}));
        native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
            location(), arguments[index], llvm::ArrayRef<int64_t>{1}));
        for (size_t i = index + 1; i < arguments.size(); ++i)
            native_arguments.push_back(arguments[i]);
    } else if (kind == StandardPrimitive::ParseI32 || kind == StandardPrimitive::StringCopy ||
               (numeric && numeric->group == NumericPrimitiveGroup::Parse)) {
        native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
            location(), arguments[0], llvm::ArrayRef<int64_t>{0}));
        native_arguments.push_back(builder.create<mlir::LLVM::ExtractValueOp>(
            location(), arguments[0], llvm::ArrayRef<int64_t>{1}));
        native_arguments.push_back(arguments[1]);
    } else {
        native_arguments.assign(arguments.begin(), arguments.end());
    }
    const auto name = standard_runtime_names.at(static_cast<size_t>(kind));
    auto function = theModule.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name);
    if (!function) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(theModule.getBody());
        function = builder.create<mlir::LLVM::LLVMFuncOp>(location(), name,
                                                          standard_runtime_type(context, kind));
    }
    auto call = builder.create<mlir::LLVM::CallOp>(location(), function, native_arguments);
    if (kind == StandardPrimitive::StringCopy)
        return emit_standard_primitive(StandardPrimitive::StringView,
                                       mlir::ValueRange{arguments[1], native_arguments[1]});
    return call.getNumResults() ? call.getResult() : mlir::Value{};
}
