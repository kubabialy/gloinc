#include "packed_ir.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "numeric.h"
#include "llvm/ADT/DenseSet.h"
#include <bit>
#include <functional>

std::optional<uint64_t> packed_integer_width(mlir::Type type) {
    if (auto integer = mlir::dyn_cast<gloin::GloinIntegerType>(type))
        return integer.getWidth();
    if (auto integer = mlir::dyn_cast<mlir::IntegerType>(type)) {
        const auto width = integer.getWidth();
        if (integer.isSignless() && (width == 8 || width == 16 || width == 32 || width == 64))
            return width;
    }
    return std::nullopt;
}

mlir::Type packed_array_layout(gloin::GloinPackedArrayType type) {
    const auto bits = packed_integer_width(type.getElement());
    if (!bits || type.getLength() > 1048576)
        return {};
    auto bytes = mlir::LLVM::LLVMArrayType::get(mlir::IntegerType::get(type.getContext(), 8),
                                                (*bits * type.getLength() + 7) / 8);
    return mlir::LLVM::LLVMStructType::getLiteral(type.getContext(), {bytes});
}

mlir::Type packed_slot_layout(mlir::MLIRContext *context) {
    return mlir::LLVM::LLVMStructType::getLiteral(
        context, {mlir::LLVM::LLVMPointerType::get(context), mlir::IntegerType::get(context, 64)});
}

mlir::Type packed_slice_layout(mlir::MLIRContext *context) {
    return mlir::LLVM::LLVMStructType::getLiteral(
        context, {mlir::LLVM::LLVMPointerType::get(context), mlir::IntegerType::get(context, 64),
                  mlir::IntegerType::get(context, 64)});
}

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
    if (auto array = mlir::dyn_cast<gloin::GloinPackedArrayType>(source)) {
        auto layout = packed_array_layout(array);
        auto size = layout ? byte_size(layout) : std::nullopt;
        if (!size)
            return std::nullopt;
        return Field{source, layout, *size * 8, 0, false, true};
    }
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
struct LowerArrayLiteral : PackedPattern<gloin::PackedArrayLiteralOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedArrayLiteralOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedArrayType>(op.getSourceType());
        auto field = *field_type(op, type.getElement());
        if (!field.byte_aligned)
            field.msb = type.getMsbFirst();
        auto storage = temporary(rewriter, op.getLoc(), op.getLayoutType());
        Access access{rewriter, op.getLoc(), storage, little, false};
        auto bytes = access.constant(rewriter.getI64Type(), *byte_size(op.getLayoutType()));
        rewriter.create<mlir::LLVM::MemsetOp>(
            op.getLoc(), storage, access.constant(rewriter.getI8Type(), 0), bytes, false);
        for (auto element : op.getElements()) {
            access.write(field, element);
            field.offset += field.bits;
        }
        rewriter.replaceOp(op, access.load(op.getLayoutType(), 0));
        return mlir::success();
    }
};

mlir::Value slot_position(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value base,
                          mlir::Value byte, mlir::Value offset) {
    auto pointer = rewriter.create<mlir::LLVM::GEPOp>(loc, base.getType(), rewriter.getI8Type(),
                                                      base, mlir::ValueRange{byte});
    mlir::Value slot =
        rewriter.create<mlir::LLVM::UndefOp>(loc, packed_slot_layout(rewriter.getContext()));
    slot =
        rewriter.create<mlir::LLVM::InsertValueOp>(loc, slot, pointer, llvm::ArrayRef<int64_t>{0});
    return rewriter.create<mlir::LLVM::InsertValueOp>(loc, slot, offset,
                                                      llvm::ArrayRef<int64_t>{1});
}

// Fixed arrays have bounded extents, so the full bit position is representable.
mlir::Value slot_at(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value base,
                    mlir::Value bit) {
    Access access{rewriter, loc, base, true};
    auto eight = access.constant(rewriter.getI64Type(), 8);
    auto byte = rewriter.create<mlir::arith::DivUIOp>(loc, bit, eight);
    auto offset = rewriter.create<mlir::arith::RemUIOp>(loc, bit, eight);
    return slot_position(rewriter, loc, base, byte, offset);
}

// Split the product before multiplying: a valid byte extent can exceed u64
// when expressed in bits. Descriptor construction and bounds prove byte safety.
mlir::Value advance_slice(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value base,
                          mlir::Value offset, mlir::Value index, uint64_t bits) {
    Access access{rewriter, loc, base, true};
    auto eight = access.constant(rewriter.getI64Type(), 8);
    auto width = access.constant(rewriter.getI64Type(), bits);
    auto whole = rewriter.create<mlir::arith::DivUIOp>(loc, index, eight);
    auto remainder = rewriter.create<mlir::arith::RemUIOp>(loc, index, eight);
    auto bytes = rewriter.create<mlir::arith::MulIOp>(loc, whole, width);
    auto tail = rewriter.create<mlir::arith::MulIOp>(loc, remainder, width);
    auto position = rewriter.create<mlir::arith::AddIOp>(loc, tail, offset);
    auto carry = rewriter.create<mlir::arith::DivUIOp>(loc, position, eight);
    auto bit = rewriter.create<mlir::arith::RemUIOp>(loc, position, eight);
    auto byte = rewriter.create<mlir::arith::AddIOp>(loc, bytes, carry);
    return slot_position(rewriter, loc, base, byte, bit);
}

mlir::Value slice_from_slot(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value slot,
                            mlir::Value length) {
    auto base = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, slot, llvm::ArrayRef<int64_t>{0});
    auto offset =
        rewriter.create<mlir::LLVM::ExtractValueOp>(loc, slot, llvm::ArrayRef<int64_t>{1});
    mlir::Value view =
        rewriter.create<mlir::LLVM::UndefOp>(loc, packed_slice_layout(rewriter.getContext()));
    view = rewriter.create<mlir::LLVM::InsertValueOp>(loc, view, base, llvm::ArrayRef<int64_t>{0});
    view =
        rewriter.create<mlir::LLVM::InsertValueOp>(loc, view, offset, llvm::ArrayRef<int64_t>{1});
    return rewriter.create<mlir::LLVM::InsertValueOp>(loc, view, length,
                                                      llvm::ArrayRef<int64_t>{2});
}

void nonnegative_bound(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value value,
                       bool is_signed) {
    if (!is_signed)
        return;
    auto zero = rewriter.create<mlir::arith::ConstantIntOp>(loc, 0, 64);
    auto valid =
        rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sge, value, zero);
    rewriter.create<gloin::AssertOp>(loc, valid);
}

mlir::Value packed_range(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value start,
                         mlir::Value end, mlir::Value length, bool start_signed, bool end_signed) {
    nonnegative_bound(rewriter, loc, start, start_signed);
    nonnegative_bound(rewriter, loc, end, end_signed);
    auto ordered =
        rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule, start, end);
    auto bounded =
        rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule, end, length);
    rewriter.create<gloin::AssertOp>(loc, ordered);
    rewriter.create<gloin::AssertOp>(loc, bounded);
    return rewriter.create<mlir::arith::SubIOp>(loc, end, start);
}

struct LowerCheckedView : PackedPattern<gloin::CheckedPackedViewOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::CheckedPackedViewOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto loc = op.getLoc();
        auto type = mlir::cast<gloin::GloinPackedSliceType>(op.getSourceType());
        auto field = *field_type(op, type.getElement());
        auto base = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, op.getBytes(),
                                                                llvm::ArrayRef<int64_t>{0});
        auto capacity = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, op.getBytes(),
                                                                    llvm::ArrayRef<int64_t>{1});
        Access access{rewriter, loc, base, little};
        auto zero = access.constant(rewriter.getI64Type(), 0);
        auto seven = access.constant(rewriter.getI64Type(), 7);
        auto eight = access.constant(rewriter.getI64Type(), 8);
        auto width = access.constant(rewriter.getI64Type(), field.bits);
        auto limit = access.constant(rewriter.getI64Type(), INT64_MAX);
        auto byte = rewriter.create<mlir::arith::DivUIOp>(loc, op.getBitOffset(), eight);
        auto bit = rewriter.create<mlir::arith::RemUIOp>(loc, op.getBitOffset(), eight);
        auto whole = rewriter.create<mlir::arith::DivUIOp>(loc, op.getCount(), eight);
        auto remainder = rewriter.create<mlir::arith::RemUIOp>(loc, op.getCount(), eight);
        auto tail_bits = rewriter.create<mlir::arith::MulIOp>(loc, remainder, width);
        auto with_start = rewriter.create<mlir::arith::AddIOp>(loc, tail_bits, bit);
        auto rounded = rewriter.create<mlir::arith::AddIOp>(loc, with_start, seven);
        auto rounded_bytes = rewriter.create<mlir::arith::DivUIOp>(loc, rounded, eight);
        auto empty = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq,
                                                          op.getCount(), zero);
        auto tail = rewriter.create<mlir::arith::SelectOp>(loc, empty, zero, rounded_bytes);
        auto remaining_limit = rewriter.create<mlir::arith::SubIOp>(loc, limit, tail);
        auto maximum = rewriter.create<mlir::arith::DivUIOp>(loc, remaining_limit, width);
        auto fits = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule,
                                                         whole, maximum);
        // Plain unsigned IR arithmetic may wrap on a failed request; the fits
        // guard prevents publishing its descriptor. No memory is accessed here.
        auto whole_bytes = rewriter.create<mlir::arith::MulIOp>(loc, whole, width);
        auto extent = rewriter.create<mlir::arith::AddIOp>(loc, whole_bytes, tail);
        auto remaining = rewriter.create<mlir::arith::SubIOp>(loc, capacity, byte);
        auto enough = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule,
                                                           extent, remaining);
        auto inside = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ult,
                                                           byte, capacity);
        auto at_end = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq,
                                                           byte, capacity);
        auto bit_zero =
            rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, bit, zero);
        auto end_boundary = rewriter.create<mlir::arith::AndIOp>(loc, at_end, bit_zero);
        auto position = rewriter.create<mlir::arith::OrIOp>(loc, inside, end_boundary);
        mlir::Value aligned =
            field.byte_aligned ? mlir::Value(bit_zero) : access.constant(rewriter.getI1Type(), 1);
        auto bounded = rewriter.create<mlir::arith::AndIOp>(loc, position, enough);
        auto representable = rewriter.create<mlir::arith::AndIOp>(loc, fits, aligned);
        auto valid = rewriter.create<mlir::arith::AndIOp>(loc, bounded, representable);
        auto outcome =
            rewriter.create<mlir::scf::IfOp>(loc, mlir::TypeRange{op.getLayoutType()}, valid, true);
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(&outcome.getThenRegion().front());
            auto slot = slot_position(rewriter, loc, base, byte, bit);
            auto view = slice_from_slot(rewriter, loc, slot, op.getCount());
            auto result = rewriter.create<gloin::ResultSuccessOp>(
                loc, op.getLayoutType(), mlir::ValueRange{view},
                mlir::TypeAttr::get(op.getLayoutType()));
            result->setAttr("gloin.payload_source", op.getSourceTypeAttr());
            rewriter.create<mlir::scf::YieldOp>(loc, result.getResult());
            rewriter.setInsertionPointToStart(&outcome.getElseRegion().front());
            auto choose = [&](mlir::Value condition, mlir::Value yes, mlir::Value no) {
                auto choice = rewriter.create<mlir::scf::IfOp>(loc, mlir::TypeRange{yes.getType()},
                                                               condition, true);
                mlir::OpBuilder::InsertionGuard branch(rewriter);
                rewriter.setInsertionPointToStart(&choice.getThenRegion().front());
                rewriter.create<mlir::scf::YieldOp>(loc, yes);
                rewriter.setInsertionPointToStart(&choice.getElseRegion().front());
                rewriter.create<mlir::scf::YieldOp>(loc, no);
                return choice.getResult(0);
            };
            auto error = choose(fits, op.getBoundsError(), op.getOverflowError());
            error = choose(aligned, error, op.getInvalidError());
            auto failure = rewriter.create<gloin::ResultFailureOp>(
                loc, op.getLayoutType(), error, mlir::TypeAttr::get(op.getLayoutType()));
            rewriter.create<mlir::scf::YieldOp>(loc, failure.getResult());
        }
        rewriter.replaceOp(op, outcome.getResults());
        return mlir::success();
    }
};

struct LowerSliceFromArray : PackedPattern<gloin::PackedSliceFromArrayOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedSliceFromArrayOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedArrayType>(op.getArrayType());
        Access access{rewriter, op.getLoc(), op.getBase(), little};
        auto capacity = access.constant(rewriter.getI64Type(), type.getLength());
        auto length = packed_range(rewriter, op.getLoc(), op.getStart(), op.getEnd(), capacity,
                                   op.getStartSigned(), op.getEndSigned());
        auto zero = access.constant(rewriter.getI64Type(), 0);
        auto slot = advance_slice(rewriter, op.getLoc(), op.getBase(), zero, op.getStart(),
                                  *packed_integer_width(type.getElement()));
        rewriter.replaceOp(op, slice_from_slot(rewriter, op.getLoc(), slot, length));
        return mlir::success();
    }
};

struct LowerSliceSubrange : PackedPattern<gloin::PackedSliceSubrangeOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedSliceSubrangeOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedSliceType>(op.getSourceType());
        auto base = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                llvm::ArrayRef<int64_t>{0});
        auto offset = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                  llvm::ArrayRef<int64_t>{1});
        auto capacity = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                    llvm::ArrayRef<int64_t>{2});
        auto length = packed_range(rewriter, op.getLoc(), op.getStart(), op.getEnd(), capacity,
                                   op.getStartSigned(), op.getEndSigned());
        auto slot = advance_slice(rewriter, op.getLoc(), base, offset, op.getStart(),
                                  *packed_integer_width(type.getElement()));
        rewriter.replaceOp(op, slice_from_slot(rewriter, op.getLoc(), slot, length));
        return mlir::success();
    }
};

struct LowerSliceBitOffset : PackedPattern<gloin::PackedSliceBitOffsetOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedSliceBitOffsetOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(op, op.getBase(),
                                                                llvm::ArrayRef<int64_t>{1});
        return mlir::success();
    }
};

struct LowerSliceSelect : PackedPattern<gloin::PackedSliceSelectOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedSliceSelectOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedSliceType>(op.getSourceType());
        auto base = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                llvm::ArrayRef<int64_t>{0});
        auto offset = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                  llvm::ArrayRef<int64_t>{1});
        auto length = rewriter.create<mlir::LLVM::ExtractValueOp>(op.getLoc(), op.getBase(),
                                                                  llvm::ArrayRef<int64_t>{2});
        nonnegative_bound(rewriter, op.getLoc(), op.getIndex(), op.getIndexSigned());
        auto bounded = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::ult, op.getIndex(), length);
        rewriter.create<gloin::AssertOp>(op.getLoc(), bounded);
        rewriter.replaceOp(op, advance_slice(rewriter, op.getLoc(), base, offset, op.getIndex(),
                                             *packed_integer_width(type.getElement())));
        return mlir::success();
    }
};

// Specialize the eight possible bit positions. Each arm reuses the verified
// bounded field accesses: no arm reads or writes outside its containing bytes.
mlir::Value access_slot(mlir::Operation *op, mlir::PatternRewriter &rewriter, mlir::Value slot,
                        gloin::GloinPackedSlotType type, bool little, mlir::Value stored = {}) {
    auto loc = op->getLoc();
    auto base = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, slot, llvm::ArrayRef<int64_t>{0});
    auto offset =
        rewriter.create<mlir::LLVM::ExtractValueOp>(loc, slot, llvm::ArrayRef<int64_t>{1});
    auto field = *field_type(op, type.getElement());
    if (!field.byte_aligned)
        field.msb = type.getMsbFirst();
    Access access{rewriter, loc, base, little};
    if (field.byte_aligned) {
        if (stored) {
            access.write(field, stored);
            return {};
        }
        return access.read(field);
    }
    auto index = rewriter.create<mlir::arith::IndexCastOp>(loc, rewriter.getIndexType(), offset);
    auto select = rewriter.create<mlir::scf::IndexSwitchOp>(
        loc, stored ? mlir::TypeRange{} : mlir::TypeRange{field.layout}, index,
        llvm::ArrayRef<int64_t>{1, 2, 3, 4, 5, 6, 7}, 7);
    {
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto &region = bit ? select.getCaseRegions()[bit - 1] : select.getDefaultRegion();
            rewriter.createBlock(&region);
            field.offset = bit;
            if (stored) {
                access.write(field, stored);
                rewriter.create<mlir::scf::YieldOp>(loc);
            } else {
                auto value = access.read(field);
                rewriter.create<mlir::scf::YieldOp>(loc, value);
            }
        }
    }
    return stored ? mlir::Value{} : select.getResult(0);
}

struct LowerArraySelect : PackedPattern<gloin::PackedArraySelectOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedArraySelectOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedArrayType>(op.getSourceType());
        Access access{rewriter, op.getLoc(), op.getBase(), little};
        auto length = access.constant(rewriter.getI64Type(), type.getLength());
        auto valid = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::ult, op.getIndex(), length);
        rewriter.create<gloin::AssertOp>(op.getLoc(), valid);
        auto width =
            access.constant(rewriter.getI64Type(), *packed_integer_width(type.getElement()));
        auto bit = rewriter.create<mlir::arith::MulIOp>(op.getLoc(), op.getIndex(), width);
        rewriter.replaceOp(op, slot_at(rewriter, op.getLoc(), op.getBase(), bit));
        return mlir::success();
    }
};

struct LowerElementLoad : PackedPattern<gloin::PackedElementLoadOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedElementLoadOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOp(op, access_slot(op, rewriter, op.getSlot(),
                                           mlir::cast<gloin::GloinPackedSlotType>(op.getSlotType()),
                                           little));
        return mlir::success();
    }
};

struct LowerElementStore : PackedPattern<gloin::PackedElementStoreOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedElementStoreOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        access_slot(op, rewriter, op.getSlot(),
                    mlir::cast<gloin::GloinPackedSlotType>(op.getSlotType()), little,
                    op.getValue());
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct LowerArrayRepeat : PackedPattern<gloin::PackedArrayRepeatOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::PackedArrayRepeatOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::cast<gloin::GloinPackedArrayType>(op.getSourceType());
        auto storage = temporary(rewriter, op.getLoc(), op.getLayoutType());
        Access access{rewriter, op.getLoc(), storage, little};
        auto bytes = access.constant(rewriter.getI64Type(), *byte_size(op.getLayoutType()));
        rewriter.create<mlir::LLVM::MemsetOp>(
            op.getLoc(), storage, access.constant(rewriter.getI8Type(), 0), bytes, false);
        auto zero = access.constant(rewriter.getI64Type(), 0);
        auto one = access.constant(rewriter.getI64Type(), 1);
        auto length = access.constant(rewriter.getI64Type(), type.getLength());
        auto width =
            access.constant(rewriter.getI64Type(), *packed_integer_width(type.getElement()));
        auto loop = rewriter.create<mlir::scf::ForOp>(op.getLoc(), zero, length, one);
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(loop.getBody());
            auto bit =
                rewriter.create<mlir::arith::MulIOp>(op.getLoc(), loop.getInductionVar(), width);
            auto slot = slot_at(rewriter, op.getLoc(), storage, bit);
            auto slot_type = gloin::GloinPackedSlotType::get(
                rewriter.getContext(), type.getElement(), type.getMsbFirst(), false);
            rewriter.create<gloin::PackedElementStoreOp>(op.getLoc(), slot, op.getElement(),
                                                         mlir::TypeAttr::get(slot_type));
        }
        rewriter.replaceOp(op, access.load(op.getLayoutType(), 0));
        return mlir::success();
    }
};

struct LowerInitializePackedSlice : PackedPattern<gloin::InitializeSliceOp> {
    using PackedPattern::PackedPattern;
    mlir::LogicalResult matchAndRewrite(gloin::InitializeSliceOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto type = mlir::dyn_cast<gloin::GloinPackedSliceType>(op.getSourceType());
        if (!type)
            return mlir::failure();
        auto field = *field_type(op, type.getElement());
        auto loc = op.getLoc();
        Access access{rewriter, loc, op.getStorage(), little};
        auto zero = access.constant(rewriter.getI64Type(), 0);
        auto one = access.constant(rewriter.getI64Type(), 1);
        auto eight = access.constant(rewriter.getI64Type(), 8);
        auto width = access.constant(rewriter.getI64Type(), field.bits);
        auto whole = rewriter.create<mlir::arith::DivUIOp>(loc, op.getCount(), eight);
        auto remainder = rewriter.create<mlir::arith::RemUIOp>(loc, op.getCount(), eight);
        auto tail_bits = rewriter.create<mlir::arith::MulIOp>(loc, remainder, width);
        auto rounded = rewriter.create<mlir::arith::AddIOp>(
            loc, tail_bits, access.constant(rewriter.getI64Type(), 7));
        auto tail = rewriter.create<mlir::arith::DivUIOp>(loc, rounded, eight);
        auto budget = rewriter.create<mlir::arith::SubIOp>(
            loc, access.constant(rewriter.getI64Type(), INT64_MAX), tail);
        auto maximum = rewriter.create<mlir::arith::DivUIOp>(loc, budget, width);
        rewriter.create<gloin::AssertOp>(
            loc, rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule, whole,
                                                      maximum));
        auto bytes = rewriter.create<mlir::arith::AddIOp>(
            loc, rewriter.create<mlir::arith::MulIOp>(loc, whole, width), tail);
        rewriter.create<gloin::AssertOp>(
            loc, rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sge,
                                                      op.getAvailable(), zero));
        rewriter.create<gloin::AssertOp>(
            loc, rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ule, bytes,
                                                      op.getAvailable()));
        auto present =
            rewriter.create<gloin::RawIsAlignedOp>(loc, rewriter.getI1Type(), op.getStorage(), one);
        rewriter.create<gloin::AssertOp>(loc, present);
        // Establish initialized boundary bytes before any masked element store.
        rewriter.create<mlir::LLVM::MemsetOp>(
            loc, op.getStorage(), access.constant(rewriter.getI8Type(), 0), bytes, false);
        auto loop = rewriter.create<mlir::scf::WhileOp>(loc, mlir::TypeRange{rewriter.getI64Type()},
                                                        mlir::ValueRange{zero});
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            auto *before =
                rewriter.createBlock(&loop.getBefore(), {}, {rewriter.getI64Type()}, {loc});
            auto more = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ult,
                                                             before->getArgument(0), op.getCount());
            rewriter.create<mlir::scf::ConditionOp>(loc, more, before->getArguments());
            auto *body = rewriter.createBlock(&loop.getAfter(), {}, {rewriter.getI64Type()}, {loc});
            auto slot = advance_slice(rewriter, loc, op.getStorage(), zero, body->getArgument(0),
                                      field.bits);
            auto slot_type = gloin::GloinPackedSlotType::get(op.getContext(), type.getElement(),
                                                             type.getMsbFirst(), false);
            rewriter.create<gloin::PackedElementStoreOp>(loc, slot, op.getFill(),
                                                         mlir::TypeAttr::get(slot_type));
            auto next = rewriter.create<mlir::arith::AddIOp>(loc, body->getArgument(0), one);
            rewriter.create<mlir::scf::YieldOp>(loc, next.getResult());
        }
        rewriter.replaceOp(
            op, slice_from_slot(rewriter, loc,
                                slot_position(rewriter, loc, op.getStorage(), zero, zero),
                                op.getCount()));
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

mlir::LogicalResult gloin::PackedArrayLiteralOp::verify() {
    auto type = mlir::dyn_cast<gloin::GloinPackedArrayType>(getSourceType());
    if (!type || packed_array_layout(type) != getLayoutType() ||
        getElements().size() != type.getLength() ||
        (getValue().getType() != getSourceType() &&
         (is_checked(*this) || getValue().getType() != getLayoutType())))
        return emitOpError("requires a complete packed array with its exact byte layout");
    auto field = field_type(*this, type.getElement());
    if (!field)
        return emitOpError("requires a supported packed element type");
    for (auto element : getElements())
        if (!matches_value(*this, element, *field))
            return emitOpError("requires each element with its declared source type");
    return mlir::success();
}

void populate_packed_lowering(mlir::RewritePatternSet &patterns, bool little_endian) {
    patterns.add<LowerSliceBitOffset, LowerInitializePackedSlice, LowerCheckedView,
                 LowerSliceFromArray, LowerSliceSubrange, LowerSliceSelect, LowerLiteral,
                 LowerArrayLiteral, LowerArrayRepeat, LowerArraySelect, LowerElementLoad,
                 LowerElementStore, LowerExtract, LowerLoad, LowerStore, LowerAddress>(
        patterns.getContext(), little_endian);
}

mlir::LogicalResult gloin::PackedArrayRepeatOp::verify() {
    auto type = mlir::dyn_cast<gloin::GloinPackedArrayType>(getSourceType());
    auto field = type ? field_type(*this, type.getElement()) : std::nullopt;
    if (!type || !field || packed_array_layout(type) != getLayoutType() ||
        !matches_value(*this, getElement(), *field) ||
        (getValue().getType() != type &&
         (is_checked(*this) || getValue().getType() != getLayoutType())))
        return emitOpError("requires a captured packed element and exact array layout");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedArraySelectOp::verify() {
    auto type = mlir::dyn_cast<gloin::GloinPackedArrayType>(getSourceType());
    auto slot = mlir::dyn_cast<gloin::GloinPackedSlotType>(getSlotType());
    if (!type || !slot || slot.getElement() != type.getElement() ||
        slot.getMsbFirst() != type.getMsbFirst() ||
        !matches_address(*this, getBase(), type, !slot.getReadOnly()) ||
        (getSlot().getType() != slot &&
         (is_checked(*this) || getSlot().getType() != packed_slot_layout(getContext()))))
        return emitOpError("requires an array pointer and matching packed element capability");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedElementLoadOp::verify() {
    auto slot = mlir::dyn_cast<gloin::GloinPackedSlotType>(getSlotType());
    auto field = slot ? field_type(*this, slot.getElement()) : std::nullopt;
    if (!field || field->layout != getLayoutType() || !matches_value(*this, getValue(), *field) ||
        (getSlot().getType() != slot &&
         (is_checked(*this) || getSlot().getType() != packed_slot_layout(getContext()))))
        return emitOpError("requires a matching checked packed element location and value");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedElementStoreOp::verify() {
    auto slot = mlir::dyn_cast<gloin::GloinPackedSlotType>(getSlotType());
    auto field = slot ? field_type(*this, slot.getElement()) : std::nullopt;
    if (!field || slot.getReadOnly() || !matches_value(*this, getValue(), *field) ||
        (getSlot().getType() != slot &&
         (is_checked(*this) || getSlot().getType() != packed_slot_layout(getContext()))))
        return emitOpError("requires a writable packed element location and matching value");
    return mlir::success();
}

namespace {
bool matches_slice(mlir::Operation *op, mlir::Value value, gloin::GloinPackedSliceType type) {
    return type && (value.getType() == type ||
                    (!is_checked(op) && value.getType() == packed_slice_layout(op->getContext())));
}
} // namespace

mlir::LogicalResult gloin::PackedSliceFromArrayOp::verify() {
    auto array = mlir::dyn_cast<gloin::GloinPackedArrayType>(getArrayType());
    auto slice = mlir::dyn_cast<gloin::GloinPackedSliceType>(getSourceType());
    if (!array || !slice || array.getElement() != slice.getElement() ||
        array.getMsbFirst() != slice.getMsbFirst() ||
        !matches_address(*this, getBase(), array, !slice.getReadOnly()) ||
        !matches_slice(*this, getValue(), slice))
        return emitOpError("requires a packed array and matching borrowed slice capability");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedSliceSubrangeOp::verify() {
    auto slice = mlir::dyn_cast<gloin::GloinPackedSliceType>(getSourceType());
    if (!matches_slice(*this, getBase(), slice) || !matches_slice(*this, getValue(), slice))
        return emitOpError("requires packed subranges to preserve element encoding and capability");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedSliceSelectOp::verify() {
    auto slice = mlir::dyn_cast<gloin::GloinPackedSliceType>(getSourceType());
    auto slot = mlir::dyn_cast<gloin::GloinPackedSlotType>(getSlotType());
    if (!matches_slice(*this, getBase(), slice) || !slot ||
        slice.getElement() != slot.getElement() || slice.getMsbFirst() != slot.getMsbFirst() ||
        (slice.getReadOnly() && !slot.getReadOnly()) ||
        (getSlot().getType() != slot &&
         (is_checked(*this) || getSlot().getType() != packed_slot_layout(getContext()))))
        return emitOpError("requires a packed slice and matching element location capability");
    return mlir::success();
}

mlir::LogicalResult gloin::CheckedPackedViewOp::verify() {
    auto slice = mlir::dyn_cast<gloin::GloinPackedSliceType>(getSourceType());
    if (!slice || !field_type(*this, slice.getElement()))
        return emitOpError("requires a supported packed slice encoding");
    auto result = gloin::GloinResultType::get(getContext(), slice);
    auto bytes = gloin::GloinSliceType::get(getContext(), mlir::IntegerType::get(getContext(), 8),
                                            slice.getReadOnly());
    auto error = gloin::GloinErrorType::get(getContext());
    auto matches = [&](mlir::Value value, mlir::Type source) {
        return value.getType() == source ||
               (!is_checked(*this) && gloin::matchesSourceStorage(source, value.getType()));
    };
    if (!gloin::matchesSourceStorage(result, getLayoutType()) || !matches(getValue(), result) ||
        !matches(getBytes(), bytes) || !matches(getInvalidError(), error) ||
        !matches(getOverflowError(), error) || !matches(getBoundsError(), error))
        return emitOpError(
            "requires matching byte access capability, checked result layout and errors");
    return mlir::success();
}

mlir::LogicalResult gloin::PackedSliceBitOffsetOp::verify() {
    auto slice = mlir::dyn_cast<gloin::GloinPackedSliceType>(getSourceType());
    if (!slice || (getBase().getType() != slice &&
                   (is_checked(*this) || getBase().getType() != packed_slice_layout(getContext()))))
        return emitOpError("requires a matching packed slice descriptor");
    return mlir::success();
}
