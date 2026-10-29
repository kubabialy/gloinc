#include "packed_ir.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "numeric.h"
#include "llvm/ADT/DenseSet.h"
#include <bit>
#include <functional>

namespace {
gloin::StructDefinitionOp definition(mlir::Operation *op, mlir::Type type) {
    auto nominal = mlir::dyn_cast<gloin::GloinStructType>(type);
    auto module = op->getParentOfType<mlir::ModuleOp>();
    if (!nominal || !module)
        return {};
    return module.lookupSymbol<gloin::StructDefinitionOp>(nominal.getName());
}

std::optional<uint64_t> byte_size(mlir::Type layout) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(layout);
    if (!record || record.isOpaque() || record.isPacked() || record.getBody().size() != 1)
        return std::nullopt;
    auto bytes = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(record.getBody()[0]);
    if (!bytes || !bytes.getElementType().isInteger(8))
        return std::nullopt;
    return bytes.getNumElements();
}

struct Field {
    mlir::Type source;
    mlir::Type layout;
    uint64_t bits;
    uint64_t offset = 0;
    bool scalar;
    bool byte_aligned;
    bool msb = false;
    bool is_signed = false;
    bool writable = false;
};

std::optional<Field> field_type(mlir::Operation *op, mlir::Type source, bool array_element = false,
                                unsigned depth = 0) {
    if (depth > 64)
        return std::nullopt;
    if (auto integer = mlir::dyn_cast<gloin::GloinIntegerType>(source)) {
        if (array_element)
            return std::nullopt;
        auto width = integer.getWidth();
        return Field{source,
                     mlir::IntegerType::get(op->getContext(), integer_storage_bits(width)),
                     width,
                     0,
                     true,
                     integer.getEndianness() != gloin::Endianness::Native,
                     integer.getEndianness() == gloin::Endianness::Big,
                     integer.getSignedness() == gloin::IntegerSignedness::Signed};
    }
    if (auto integer = mlir::dyn_cast<mlir::IntegerType>(source)) {
        auto width = integer.getWidth();
        if (!integer.isSignless() || (width != 8 && width != 16 && width != 32 && width != 64) ||
            (array_element && width != 8))
            return std::nullopt;
        return Field{source, source, width, 0, true, false};
    }
    if (auto nested = definition(op, source)) {
        auto size = byte_size(nested.getLayoutType());
        if (!nested->hasAttr("packed_order") || !size || *size > (uint64_t{1} << 28))
            return std::nullopt;
        return Field{source, nested.getLayoutType(), *size * 8, 0, false, true};
    }
    if (auto array = mlir::dyn_cast<gloin::GloinArrayType>(source)) {
        auto element = field_type(op, array.getElement(), true, depth + 1);
        if (!element || (element->bits && array.getLength() > (uint64_t{1} << 31) / element->bits))
            return std::nullopt;
        return Field{source,
                     mlir::LLVM::LLVMArrayType::get(element->layout, array.getLength()),
                     element->bits * array.getLength(),
                     0,
                     false,
                     true};
    }
    return std::nullopt;
}

std::optional<Field> field_at(mlir::Operation *op, mlir::Type owner, int64_t index) {
    auto record = definition(op, owner);
    if (!record || !record->hasAttr("packed_order") || index < 0 ||
        static_cast<uint64_t>(index) >= record.getFieldSourceTypes().size())
        return std::nullopt;
    auto offsets = record->getAttrOfType<mlir::DenseI64ArrayAttr>("packed_offsets");
    auto mutable_fields = record->getAttrOfType<mlir::DenseBoolArrayAttr>("packed_mutable");
    auto order = record->getAttrOfType<mlir::StringAttr>("packed_order");
    auto type = mlir::dyn_cast<mlir::TypeAttr>(record.getFieldSourceTypes()[index]);
    if (!offsets || !mutable_fields || !order || !type ||
        offsets.size() != record.getFieldSourceTypes().size() ||
        mutable_fields.size() != offsets.size())
        return std::nullopt;
    auto field = field_type(op, type.getValue());
    if (!field)
        return std::nullopt;
    field->offset = offsets[index];
    field->writable = mutable_fields[index];
    if (!field->byte_aligned)
        field->msb = order.getValue() == "msb_first";
    return field;
}

bool is_checked(mlir::Operation *op) {
    auto module = op->getParentOfType<mlir::ModuleOp>();
    return module && module->hasAttr("gloin.checked");
}

bool matches_value(mlir::Operation *op, mlir::Value value, const Field &field) {
    return value.getType() == field.source || (!is_checked(op) && value.getType() == field.layout);
}

bool matches_address(mlir::Operation *op, mlir::Value address, mlir::Type owner, bool writing) {
    if (auto pointer = mlir::dyn_cast<gloin::GloinPointerType>(address.getType()))
        return pointer.getPointee() == owner && !pointer.getNullable() &&
               (!writing || !pointer.getReadOnly());
    return !is_checked(op) && mlir::isa<mlir::LLVM::LLVMPointerType>(address.getType());
}

// Every memory access below is confined to the bytes containing this field.
// Scalar access never fabricates a pointer with the scalar's natural alignment.
struct Access {
    mlir::PatternRewriter &rewriter;
    mlir::Location loc;
    mlir::Value base;
    bool little;
    bool combine_bytes = true;

    mlir::Value constant(mlir::Type type, uint64_t value) {
        return rewriter.create<mlir::arith::ConstantOp>(loc, rewriter.getIntegerAttr(type, value));
    }
    mlir::Value pointer(uint64_t byte) {
        return rewriter.create<mlir::LLVM::GEPOp>(
            loc, base.getType(), rewriter.getI8Type(), base,
            mlir::ValueRange{constant(rewriter.getI64Type(), byte)});
    }
    mlir::Value load(mlir::Type type, uint64_t byte) {
        return rewriter.create<mlir::LLVM::LoadOp>(loc, type, pointer(byte), 1);
    }
    void store(mlir::Value value, uint64_t byte) {
        rewriter.create<mlir::LLVM::StoreOp>(loc, value, pointer(byte), 1);
    }
    mlir::Value resize(mlir::Value value, mlir::Type type) {
        auto width = mlir::cast<mlir::IntegerType>(value.getType()).getWidth();
        auto target = mlir::cast<mlir::IntegerType>(type).getWidth();
        if (width == target)
            return value;
        if (width < target)
            return rewriter.create<mlir::arith::ExtUIOp>(loc, type, value);
        return rewriter.create<mlir::arith::TruncIOp>(loc, type, value);
    }
    static uint64_t low_mask(unsigned bits) {
        return llvm::APInt::getLowBitsSet(64, bits).getZExtValue();
    }
    // Consume a power-of-two number of bytes entirely inside the field's
    // containing bytes. A 64-bit field crossing nine bytes uses 8 + 1, never
    // an oversized access to a backing word. All accesses retain alignment one.
    unsigned chunk_bits(const Field &field, uint64_t done) const {
        if (!combine_bytes)
            return 8;
        auto remaining = ((field.offset + done) % 8 + field.bits - done + 7) / 8;
        return 8 * std::bit_floor(remaining);
    }
    mlir::Value order(mlir::Value value, bool msb) {
        if (!value.getType().isInteger(8) && msb == little)
            return rewriter.create<mlir::LLVM::ByteSwapOp>(loc, value);
        return value;
    }
    mlir::Value read(const Field &field) {
        if (!field.scalar)
            return load(field.layout, field.offset / 8);
        mlir::Value value = constant(field.layout, 0);
        for (uint64_t done = 0; done < field.bits;) {
            auto bit = field.offset + done;
            unsigned width = chunk_bits(field, done);
            unsigned count = std::min<uint64_t>(field.bits - done, width - bit % 8);
            unsigned physical = field.msb ? width - bit % 8 - count : bit % 8;
            unsigned numeric = field.msb ? field.bits - done - count : done;
            auto word = order(load(rewriter.getIntegerType(width), bit / 8), field.msb);
            mlir::Value part = rewriter.create<mlir::arith::ShRUIOp>(
                loc, word, constant(word.getType(), physical));
            part = rewriter.create<mlir::arith::AndIOp>(loc, part,
                                                        constant(word.getType(), low_mask(count)));
            part = resize(part, field.layout);
            part = rewriter.create<mlir::arith::ShLIOp>(loc, part, constant(field.layout, numeric));
            value = rewriter.create<mlir::arith::OrIOp>(loc, value, part);
            done += count;
        }
        auto width = mlir::cast<mlir::IntegerType>(field.layout).getWidth();
        if (field.is_signed && field.bits < width) {
            auto narrow = rewriter.create<mlir::arith::TruncIOp>(
                loc, rewriter.getIntegerType(field.bits), value);
            value = rewriter.create<mlir::arith::ExtSIOp>(loc, field.layout, narrow);
        }
        return value;
    }
    void write(const Field &field, mlir::Value value) {
        if (!field.scalar) {
            store(value, field.offset / 8);
            return;
        }
        for (uint64_t done = 0; done < field.bits;) {
            auto bit = field.offset + done;
            unsigned width = chunk_bits(field, done);
            unsigned count = std::min<uint64_t>(field.bits - done, width - bit % 8);
            unsigned physical = field.msb ? width - bit % 8 - count : bit % 8;
            unsigned numeric = field.msb ? field.bits - done - count : done;
            uint64_t mask = low_mask(count) << physical;
            mlir::Value part = rewriter.create<mlir::arith::ShRUIOp>(
                loc, value, constant(value.getType(), numeric));
            part = resize(part, rewriter.getIntegerType(width));
            part =
                rewriter.create<mlir::arith::ShLIOp>(loc, part, constant(part.getType(), physical));
            part = rewriter.create<mlir::arith::AndIOp>(loc, part, constant(part.getType(), mask));
            if (mask != low_mask(width)) {
                auto old = order(load(rewriter.getIntegerType(width), bit / 8), field.msb);
                auto kept = rewriter.create<mlir::arith::AndIOp>(
                    loc, old, constant(old.getType(), low_mask(width) ^ mask));
                part = rewriter.create<mlir::arith::OrIOp>(loc, kept, part);
            }
            store(order(part, field.msb), bit / 8);
            done += count;
        }
    }
};

mlir::Value temporary(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Type type) {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    auto *parent = rewriter.getInsertionBlock()->getParentOp();
    auto function = mlir::dyn_cast<mlir::func::FuncOp>(parent);
    if (!function)
        function = parent->getParentOfType<mlir::func::FuncOp>();
    rewriter.setInsertionPointToStart(&function.getBody().front());
    auto one = rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 64);
    return rewriter.create<mlir::LLVM::AllocaOp>(
        loc, mlir::LLVM::LLVMPointerType::get(rewriter.getContext()), type, one, 1);
}

template <typename Op> struct PackedPattern : mlir::OpRewritePattern<Op> {
    bool little;
    PackedPattern(mlir::MLIRContext *context, bool little)
        : mlir::OpRewritePattern<Op>(context), little(little) {}
};

struct LowerLiteral : PackedPattern<gloin::PackedLiteralOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedLiteralOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto storage = temporary(rewriter, op.getLoc(), op.getLayoutType());
        // Aggregate SSA storage is an array of bytes. LLVM SROA splits writes
        // to this temporary back into bytes; wide encoding here can leave
        // redundant swaps/extractions instead of the direct byte construction.
        Access access{rewriter, op.getLoc(), storage, little, false};
        auto count = access.constant(rewriter.getI64Type(), *byte_size(op.getLayoutType()));
        rewriter.create<mlir::LLVM::MemsetOp>(
            op.getLoc(), storage, access.constant(rewriter.getI8Type(), 0), count, false);
        for (auto [value, index] : llvm::zip(op.getFields(), op.getIndices()))
            access.write(*field_at(op, op.getSourceType(), index), value);
        rewriter.replaceOp(op, access.load(op.getLayoutType(), 0));
        return mlir::success();
    }
};
struct LowerExtract : PackedPattern<gloin::PackedExtractOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedExtractOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto storage = temporary(rewriter, op.getLoc(), op.getAggregate().getType());
        Access access{rewriter, op.getLoc(), storage, little};
        access.store(op.getAggregate(), 0);
        rewriter.replaceOp(op, access.read(*field_at(op, op.getSourceType(), op.getIndex())));
        return mlir::success();
    }
};
struct LowerLoad : PackedPattern<gloin::PackedLoadOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedLoadOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        Access access{rewriter, op.getLoc(), op.getAddress(), little};
        rewriter.replaceOp(op, access.read(*field_at(op, op.getSourceType(), op.getIndex())));
        return mlir::success();
    }
};
struct LowerStore : PackedPattern<gloin::PackedStoreOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedStoreOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        Access access{rewriter, op.getLoc(), op.getAddress(), little};
        access.write(*field_at(op, op.getSourceType(), op.getIndex()), op.getValue());
        rewriter.eraseOp(op);
        return mlir::success();
    }
};
struct LowerAddress : PackedPattern<gloin::PackedAddressOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedAddressOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        Access access{rewriter, op.getLoc(), op.getBase(), little};
        rewriter.replaceOp(
            op, access.pointer(field_at(op, op.getSourceType(), op.getIndex())->offset / 8));
        return mlir::success();
    }
};
} // namespace

mlir::LogicalResult verify_packed_definition(gloin::StructDefinitionOp op) {
    auto order = op->getAttrOfType<mlir::StringAttr>("packed_order");
    auto offsets = op->getAttrOfType<mlir::DenseI64ArrayAttr>("packed_offsets");
    auto mutable_fields = op->getAttrOfType<mlir::DenseBoolArrayAttr>("packed_mutable");
    auto bytes = byte_size(op.getLayoutType());
    if (!order || (order.getValue() != "msb_first" && order.getValue() != "lsb_first") ||
        !offsets || !mutable_fields || offsets.size() != op.getFieldSourceTypes().size() ||
        mutable_fields.size() != offsets.size() || !bytes)
        return op.emitOpError(
            "requires packed bit order, byte storage, and complete field metadata");
    uint64_t end = 0;
    for (auto [index, attr] : llvm::enumerate(op.getFieldSourceTypes())) {
        auto type = mlir::dyn_cast<mlir::TypeAttr>(attr);
        auto field = type ? field_type(op, type.getValue()) : std::nullopt;
        if (!field || offsets[index] < 0 || static_cast<uint64_t>(offsets[index]) != end ||
            (field->byte_aligned && end % 8) || field->bits > (uint64_t{1} << 31) - end)
            return op.emitOpError(
                "requires valid contiguous fields and byte-aligned byte representations");
        end += field->bits;
    }
    if (*bytes != (end + 7) / 8)
        return op.emitOpError("packed storage extent disagrees with its fields");
    llvm::SmallDenseSet<mlir::Type> active;
    std::function<bool(mlir::Type, unsigned)> acyclic = [&](mlir::Type source, unsigned depth) {
        if (depth > 64)
            return false;
        if (auto array = mlir::dyn_cast<gloin::GloinArrayType>(source))
            return acyclic(array.getElement(), depth + 1);
        auto nested = definition(op, source);
        if (!nested)
            return true;
        if (!active.insert(source).second)
            return false;
        for (auto attribute : nested.getFieldSourceTypes()) {
            auto field = mlir::dyn_cast<mlir::TypeAttr>(attribute);
            if (!field || !acyclic(field.getValue(), depth + 1))
                return false;
        }
        active.erase(source);
        return true;
    };
    if (!acyclic(gloin::GloinStructType::get(op.getContext(), op.getSymName()), 0))
        return op.emitOpError("recursive or excessively nested packed value layout");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedLiteralOp::verify() {
    auto record = definition(*this, getSourceType());
    if (!record || !record->hasAttr("packed_order") || record.getLayoutType() != getLayoutType() ||
        getFields().size() != record.getFieldSourceTypes().size() ||
        getIndices().size() != getFields().size() ||
        (getValue().getType() != getSourceType() &&
         (is_checked(*this) || getValue().getType() != getLayoutType())))
        return emitOpError("requires a complete nominal packed construction");
    llvm::SmallDenseSet<int64_t> seen;
    for (auto [value, index] : llvm::zip(getFields(), getIndices())) {
        auto field = field_at(*this, getSourceType(), index);
        if (!field || !seen.insert(index).second || !matches_value(*this, value, *field))
            return emitOpError("requires each packed field exactly once with its declared type");
    }
    return mlir::success();
}
mlir::LogicalResult gloin::PackedExtractOp::verify() {
    auto record = definition(*this, getSourceType());
    auto field = field_at(*this, getSourceType(), getIndex());
    if (!record || !field || field->layout != getLayoutType() ||
        !matches_value(*this, getValue(), *field) ||
        (getAggregate().getType() != getSourceType() &&
         (is_checked(*this) || getAggregate().getType() != record.getLayoutType())))
        return emitOpError("requires the declared packed aggregate and field type");
    return mlir::success();
}
mlir::LogicalResult gloin::PackedLoadOp::verify() {
    auto field = field_at(*this, getSourceType(), getIndex());
    if (!field || field->layout != getLayoutType() || !matches_value(*this, getValue(), *field) ||
        !matches_address(*this, getAddress(), getSourceType(), false))
        return emitOpError("requires a readable packed record address and matching field result");
    return mlir::success();
}
mlir::LogicalResult gloin::PackedStoreOp::verify() {
    auto field = field_at(*this, getSourceType(), getIndex());
    if (!field || !field->writable || !matches_value(*this, getValue(), *field) ||
        !matches_address(*this, getAddress(), getSourceType(), true))
        return emitOpError("requires a writable packed record, mutable field, and matching value");
    return mlir::success();
}
mlir::LogicalResult gloin::PackedAddressOp::verify() {
    auto field = field_at(*this, getSourceType(), getIndex());
    if (!field || field->scalar || field->offset % 8 ||
        !matches_address(*this, getBase(), getSourceType(), false))
        return emitOpError("only byte-aligned packed aggregate fields have ordinary addresses");
    auto base = mlir::dyn_cast<GloinPointerType>(getBase().getType());
    auto result = mlir::dyn_cast<GloinPointerType>(getAddress().getType());
    if (base && result && result.getPointee() == field->source && !result.getNullable() &&
        ((!base.getReadOnly() && field->writable) || result.getReadOnly()))
        return mlir::success();
    if (!is_checked(*this) && mlir::isa<mlir::LLVM::LLVMPointerType>(getBase().getType()) &&
        getBase().getType() == getAddress().getType())
        return mlir::success();
    return emitOpError("requires a matching aggregate pointer without strengthening access");
}

void populate_packed_lowering(mlir::RewritePatternSet &patterns, bool little_endian) {
    patterns.add<LowerLiteral, LowerExtract, LowerLoad, LowerStore, LowerAddress>(
        patterns.getContext(), little_endian);
}
