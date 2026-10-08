#include "lowering.h"
#include "dialect/GloinDialect.h"
#include "target_layout.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "llvm/ADT/APFloat.h"
#include <iterator>
#include <limits>

namespace {
// The front end keeps checked source types in function signatures.  This pass
// resolves their layout only once, immediately before the existing core pass.
struct LowerGloinSignaturesPass
    : mlir::PassWrapper<LowerGloinSignaturesPass, mlir::OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerGloinSignaturesPass)
    void runOnOperation() override {
        auto module = getOperation();
        bool failed = false;
        module.walk([&](mlir::func::FuncOp function) {
            auto inputs = function->getAttrOfType<mlir::ArrayAttr>("gloin.layout_inputs");
            auto results = function->getAttrOfType<mlir::ArrayAttr>("gloin.layout_results");
            if (!inputs && !results)
                return;
            if (!inputs || !results ||
                inputs.size() != function.getFunctionType().getNumInputs() ||
                results.size() != function.getFunctionType().getNumResults()) {
                function.emitOpError("has an invalid checked layout signature");
                failed = true;
                return;
            }
            llvm::SmallVector<mlir::Type> input_types;
            llvm::SmallVector<mlir::Type> result_types;
            for (auto attr : inputs) {
                auto type = mlir::dyn_cast<mlir::TypeAttr>(attr);
                if (!type) { failed = true; break; }
                input_types.push_back(type.getValue());
            }
            for (auto attr : results) {
                auto type = mlir::dyn_cast<mlir::TypeAttr>(attr);
                if (!type) { failed = true; break; }
                result_types.push_back(type.getValue());
            }
            if (failed) {
                function.emitOpError("has a non-type checked layout entry");
                return;
            }
            function.setType(mlir::FunctionType::get(module.getContext(), input_types,
                                                     result_types));
            if (!function.isExternal())
                for (auto [argument, type] : llvm::zip(function.getBody().front().getArguments(),
                                                       input_types))
                    argument.setType(type);
            function->removeAttr("gloin.layout_inputs");
            function->removeAttr("gloin.layout_results");
        });
        if (failed) { signalPassFailure(); return; }
        module.walk([&](mlir::func::CallOp call) {
            auto callee = module.lookupSymbol<mlir::func::FuncOp>(call.getCallee());
            if (!callee)
                return;
            for (auto [result, type] : llvm::zip(call.getResults(),
                                                  callee.getFunctionType().getResults()))
                result.setType(type);
        });
        module.walk([](gloin::EnumConstantOp constant) {
            constant.getValue().setType(constant.getLayoutType());
        });
        module.walk([](gloin::ArrayLiteralOp literal) {
            literal.getValue().setType(literal.getLayoutType());
        });
        module.walk([](gloin::RepeatArrayOp repeat) {
            repeat.getValue().setType(repeat.getLayoutType());
        });
        module.walk([](gloin::StructLiteralOp literal) {
            literal.getValue().setType(literal.getLayoutType());
        });
        module.walk([](gloin::ZeroedArrayOp zeroed) {
            zeroed.getValue().setType(zeroed.getLayoutType());
        });
        module.walk([](gloin::StringLiteralOp literal) {
            literal.getValue().setType(literal.getLayoutType());
        });
        module.walk([](gloin::ErrorLiteralOp literal) {
            literal.getValue().setType(literal.getLayoutType());
        });
        module.walk([](gloin::ErrorMessageOp message) {
            message.getMessage().setType(message.getLayoutType());
        });
        module.walk([](gloin::ResultSuccessOp success) {
            success.getValue().setType(success.getLayoutType());
        });
        module.walk([](gloin::ResultFailureOp failure) {
            failure.getValue().setType(failure.getLayoutType());
        });
        module.walk([](gloin::ResultValueOp value) {
            value.getValue().setType(value.getLayoutType());
        });
        module.walk([](gloin::ResultErrorOp error) {
            error.getError().setType(error.getLayoutType());
        });
        module.walk([](gloin::SliceFromArrayOp slice) {
            slice.getValue().setType(slice.getLayoutType());
        });
        module.walk([](gloin::SliceFromPointerOp slice) {
            slice.getValue().setType(slice.getLayoutType());
        });
        module.walk([](gloin::SliceSubrangeOp slice) {
            slice.getValue().setType(slice.getLayoutType());
        });
        module.walk([](gloin::ExtractFieldOp field) {
            auto record = mlir::cast<mlir::LLVM::LLVMStructType>(field.getLayoutType());
            field.getValue().setType(record.getBody()[field.getIndex()]);
        });
        auto pointer_layout = mlir::LLVM::LLVMPointerType::get(module.getContext());
        module.walk([&](gloin::NullOp null_value) {
            null_value.getValue().setType(pointer_layout);
        });
        module.walk([&](gloin::ArenaTypedPointerOp allocation) {
            allocation.getPointer().setType(pointer_layout);
        });
        module.walk([&](gloin::RawPlaceOp placement) {
            placement.getPointer().setType(pointer_layout);
        });
        module.walk([&](gloin::PointerOffsetOp offset) {
            offset.getAddress().setType(pointer_layout);
        });
        module.walk([&](gloin::RequireNonNullOp check) {
            check.getCheckedPointer().setType(pointer_layout);
        });
        module.walk([&](gloin::StackAllocOp allocation) {
            allocation.getAddress().setType(pointer_layout);
        });
        module.walk([&](gloin::FieldAddressOp field) {
            field.getAddress().setType(pointer_layout);
        });
        module.walk([&](gloin::ArrayElementAddressOp element) {
            element.getAddress().setType(pointer_layout);
        });
        module.walk([&](gloin::SliceElementAddressOp element) {
            element.getAddress().setType(pointer_layout);
        });
        module.walk([](gloin::LoadOp load) {
            load.getValue().setType(load.getLayoutType());
        });
        llvm::SmallVector<mlir::Operation *> bridges;
        module.walk([&](mlir::Operation *op) {
            if (mlir::isa<gloin::FromLayoutOp, gloin::ToLayoutOp>(op))
                bridges.push_back(op);
        });
        for (auto *op : bridges) {
            op->getResult(0).replaceAllUsesWith(op->getOperand(0));
            op->erase();
        }
        llvm::SmallVector<gloin::StructDefinitionOp> definitions;
        module.walk([&](gloin::StructDefinitionOp definition) {
            definitions.push_back(definition);
        });
        for (auto definition : definitions)
            definition.erase();
        module->removeAttr("gloin.checked");
    }
};

bool enum_storage(mlir::Type type) {
    auto record = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(type);
    return record && !record.isOpaque() && record.getBody().size() == 1 &&
           record.getBody().front().isInteger(32);
}

struct LowerAbiCall : mlir::OpRewritePattern<gloin::AbiCallOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::AbiCallOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto module = op->getParentOfType<mlir::ModuleOp>();
        auto function = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(op.getCallee());
        if (!function)
            return mlir::failure();
        auto call = rewriter.create<mlir::LLVM::CallOp>(op.getLoc(), function, op.getArgs());
        rewriter.replaceOp(op, call.getResults());
        return mlir::success();
    }
};

struct LowerErrorLiteral : mlir::OpRewritePattern<gloin::ErrorLiteralOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ErrorLiteralOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOp(op, op.getMessage());
        return mlir::success();
    }
};

struct LowerErrorMessage : mlir::OpRewritePattern<gloin::ErrorMessageOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ErrorMessageOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOp(op, op.getError());
        return mlir::success();
    }
};

struct LowerResultSuccess : mlir::OpRewritePattern<gloin::ResultSuccessOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ResultSuccessOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto zero = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(), op.getLayoutType());
        if (op.getPayload().empty()) {
            rewriter.replaceOp(op, zero.getResult());
            return mlir::success();
        }
        auto value = rewriter.create<mlir::LLVM::InsertValueOp>(
            op.getLoc(), zero.getResult(), op.getPayload()[0], llvm::ArrayRef<int64_t>{1});
        rewriter.replaceOp(op, value.getResult());
        return mlir::success();
    }
};

struct LowerResultFailure : mlir::OpRewritePattern<gloin::ResultFailureOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ResultFailureOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto zero = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(), op.getLayoutType());
        auto tag = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 1, 1);
        auto tagged = rewriter.create<mlir::LLVM::InsertValueOp>(
            op.getLoc(), zero.getResult(), tag.getResult(), llvm::ArrayRef<int64_t>{0});
        auto layout = mlir::cast<mlir::LLVM::LLVMStructType>(op.getLayoutType());
        auto value = rewriter.create<mlir::LLVM::InsertValueOp>(
            op.getLoc(), tagged.getResult(), op.getError(),
            llvm::ArrayRef<int64_t>{static_cast<int64_t>(layout.getBody().size() - 1)});
        rewriter.replaceOp(op, value.getResult());
        return mlir::success();
    }
};

struct LowerResultIsError : mlir::OpRewritePattern<gloin::ResultIsErrorOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ResultIsErrorOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
            op, op.getValue(), llvm::ArrayRef<int64_t>{0});
        return mlir::success();
    }
};

struct LowerResultValue : mlir::OpRewritePattern<gloin::ResultValueOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ResultValueOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto tag = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getOutcome(), llvm::ArrayRef<int64_t>{0});
        auto false_value = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 1);
        auto success = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::eq, tag, false_value);
        rewriter.create<gloin::AssertOp>(op.getLoc(), success);
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
            op, op.getOutcome(), llvm::ArrayRef<int64_t>{1});
        return mlir::success();
    }
};

struct LowerResultError : mlir::OpRewritePattern<gloin::ResultErrorOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ResultErrorOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto layout = mlir::cast<mlir::LLVM::LLVMStructType>(op.getOutcome().getType());
        auto tag = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getOutcome(), llvm::ArrayRef<int64_t>{0});
        rewriter.create<gloin::AssertOp>(op.getLoc(), tag);
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
            op, op.getOutcome(),
            llvm::ArrayRef<int64_t>{static_cast<int64_t>(layout.getBody().size() - 1)});
        return mlir::success();
    }
};

struct LowerEnumConstant : mlir::OpRewritePattern<gloin::EnumConstantOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::EnumConstantOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        if (!enum_storage(op.getValue().getType()))
            return mlir::failure();
        auto zero = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(), op.getValue().getType());
        auto tag = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), op.getTagAttr().getInt(), 32);
        auto value = rewriter.create<mlir::LLVM::InsertValueOp>(
            op.getLoc(), zero, tag, llvm::ArrayRef<int64_t>{0});
        rewriter.replaceOp(op, value);
        return mlir::success();
    }
};

struct LowerGloinConstant : mlir::OpRewritePattern<gloin::ConstantOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ConstantOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto value = mlir::dyn_cast<mlir::TypedAttr>(op.getValueAttr());
        if (!value || !mlir::isa<mlir::IntegerAttr, mlir::FloatAttr>(value) ||
            value.getType() != op.getResult().getType())
            return mlir::failure();
        rewriter.replaceOpWithNewOp<mlir::arith::ConstantOp>(
            op, op.getResult().getType(), value);
        return mlir::success();
    }
};

struct LowerGloinAssert : mlir::OpRewritePattern<gloin::AssertOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::AssertOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto *block = op->getBlock();
        auto *success = rewriter.splitBlock(block, std::next(op->getIterator()));
        auto *failure = rewriter.createBlock(block->getParent(), success->getIterator());
        rewriter.setInsertionPointToEnd(block);
        rewriter.create<mlir::cf::CondBranchOp>(
            op.getLoc(), op.getCondition(), success, mlir::ValueRange{}, failure,
            mlir::ValueRange{});
        rewriter.setInsertionPointToStart(failure);
        rewriter.create<mlir::LLVM::Trap>(op.getLoc());
        rewriter.create<mlir::LLVM::UnreachableOp>(op.getLoc());
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct LowerCheckedIntegerBinary : mlir::OpRewritePattern<gloin::CheckedIntegerBinaryOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::CheckedIntegerBinaryOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto loc = op.getLoc();
        auto lhs = op.getLhs();
        auto rhs = op.getRhs();
        auto type = mlir::cast<mlir::IntegerType>(lhs.getType());
        auto kind = op.getKind();
        auto constant = [&](llvm::APInt value) -> mlir::Value {
            return rewriter.create<mlir::arith::ConstantOp>(
                loc, type, rewriter.getIntegerAttr(type, value));
        };
        auto assert_condition = [&](mlir::Value condition) {
            rewriter.create<gloin::AssertOp>(loc, condition);
        };

        if (kind == "/" || kind == "%") {
            auto zero = constant(llvm::APInt(type.getWidth(), 0));
            assert_condition(rewriter.create<mlir::arith::CmpIOp>(
                loc, mlir::arith::CmpIPredicate::ne, rhs, zero));
            if (op.getIsSigned()) {
                auto minimum = constant(llvm::APInt::getSignedMinValue(type.getWidth()));
                auto minus_one = constant(llvm::APInt::getAllOnes(type.getWidth()));
                auto not_minimum = rewriter.create<mlir::arith::CmpIOp>(
                    loc, mlir::arith::CmpIPredicate::ne, lhs, minimum);
                auto not_minus_one = rewriter.create<mlir::arith::CmpIOp>(
                    loc, mlir::arith::CmpIPredicate::ne, rhs, minus_one);
                assert_condition(rewriter.create<mlir::arith::OrIOp>(
                    loc, not_minimum, not_minus_one));
            }
            mlir::Value result;
            if (kind == "/")
                result = op.getIsSigned()
                             ? mlir::Value(rewriter.create<mlir::arith::DivSIOp>(loc, lhs, rhs))
                             : mlir::Value(rewriter.create<mlir::arith::DivUIOp>(loc, lhs, rhs));
            else
                result = op.getIsSigned()
                             ? mlir::Value(rewriter.create<mlir::arith::RemSIOp>(loc, lhs, rhs))
                             : mlir::Value(rewriter.create<mlir::arith::RemUIOp>(loc, lhs, rhs));
            rewriter.replaceOp(op, result);
            return mlir::success();
        }

        // Twice the source width contains every mathematical sum/product.
        auto wide = rewriter.getIntegerType(type.getWidth() * 2);
        auto extend = [&](mlir::Value value) -> mlir::Value {
            return op.getIsSigned()
                       ? mlir::Value(rewriter.create<mlir::arith::ExtSIOp>(loc, wide, value))
                       : mlir::Value(rewriter.create<mlir::arith::ExtUIOp>(loc, wide, value));
        };
        auto wide_lhs = extend(lhs);
        auto wide_rhs = extend(rhs);
        mlir::Value wide_result;
        if (kind == "+")
            wide_result = rewriter.create<mlir::arith::AddIOp>(loc, wide_lhs, wide_rhs);
        else if (kind == "-")
            wide_result = rewriter.create<mlir::arith::SubIOp>(loc, wide_lhs, wide_rhs);
        else
            wide_result = rewriter.create<mlir::arith::MulIOp>(loc, wide_lhs, wide_rhs);
        auto result = rewriter.create<mlir::arith::TruncIOp>(loc, type, wide_result);
        auto restored = extend(result);
        assert_condition(rewriter.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::eq, wide_result, restored));
        rewriter.replaceOp(op, result);
        return mlir::success();
    }
};

struct LowerCheckedFloatBinary : mlir::OpRewritePattern<gloin::CheckedFloatBinaryOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::CheckedFloatBinaryOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto loc = op.getLoc();
        auto lhs = op.getLhs();
        auto rhs = op.getRhs();
        auto type = lhs.getType();
        auto kind = op.getKind();
        const auto &semantics = type.isF32() ? llvm::APFloat::IEEEsingle()
                                              : llvm::APFloat::IEEEdouble();
        auto constant = [&](llvm::APFloat value) -> mlir::Value {
            return rewriter.create<mlir::arith::ConstantOp>(
                loc, type, rewriter.getFloatAttr(type, value));
        };
        if (kind == "/") {
            auto zero = constant(llvm::APFloat::getZero(semantics));
            auto nonzero = rewriter.create<mlir::arith::CmpFOp>(
                loc, mlir::arith::CmpFPredicate::ONE, rhs, zero);
            rewriter.create<gloin::AssertOp>(loc, nonzero);
        }
        mlir::Value result;
        if (kind == "+")
            result = rewriter.create<mlir::arith::AddFOp>(loc, lhs, rhs);
        else if (kind == "-")
            result = rewriter.create<mlir::arith::SubFOp>(loc, lhs, rhs);
        else if (kind == "*")
            result = rewriter.create<mlir::arith::MulFOp>(loc, lhs, rhs);
        else
            result = rewriter.create<mlir::arith::DivFOp>(loc, lhs, rhs);
        auto largest = llvm::APFloat::getLargest(semantics);
        auto upper = constant(largest);
        largest.changeSign();
        auto lower = constant(largest);
        auto above_lower = rewriter.create<mlir::arith::CmpFOp>(
            loc, mlir::arith::CmpFPredicate::OGE, result, lower);
        auto below_upper = rewriter.create<mlir::arith::CmpFOp>(
            loc, mlir::arith::CmpFPredicate::OLE, result, upper);
        auto finite = rewriter.create<mlir::arith::AndIOp>(
            loc, above_lower, below_upper);
        rewriter.create<gloin::AssertOp>(loc, finite);
        rewriter.replaceOp(op, result);
        return mlir::success();
    }
};

struct LowerCheckedIntegerCompare
    : mlir::OpRewritePattern<gloin::CheckedIntegerCompareOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(
        gloin::CheckedIntegerCompareOp op,
        mlir::PatternRewriter &rewriter) const override {
        using Predicate = mlir::arith::CmpIPredicate;
        const auto kind = op.getKind();
        Predicate predicate = Predicate::eq;
        if (kind == "!=")
            predicate = Predicate::ne;
        else if (kind == "<")
            predicate = op.getIsSigned() ? Predicate::slt : Predicate::ult;
        else if (kind == "<=")
            predicate = op.getIsSigned() ? Predicate::sle : Predicate::ule;
        else if (kind == ">")
            predicate = op.getIsSigned() ? Predicate::sgt : Predicate::ugt;
        else if (kind == ">=")
            predicate = op.getIsSigned() ? Predicate::sge : Predicate::uge;
        rewriter.replaceOpWithNewOp<mlir::arith::CmpIOp>(
            op, predicate, op.getLhs(), op.getRhs());
        return mlir::success();
    }
};

struct LowerCheckedFloatCompare
    : mlir::OpRewritePattern<gloin::CheckedFloatCompareOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(
        gloin::CheckedFloatCompareOp op,
        mlir::PatternRewriter &rewriter) const override {
        using Predicate = mlir::arith::CmpFPredicate;
        const auto kind = op.getKind();
        Predicate predicate = Predicate::OEQ;
        if (kind == "!=")
            predicate = Predicate::UNE;
        else if (kind == "<")
            predicate = Predicate::OLT;
        else if (kind == "<=")
            predicate = Predicate::OLE;
        else if (kind == ">")
            predicate = Predicate::OGT;
        else if (kind == ">=")
            predicate = Predicate::OGE;
        rewriter.replaceOpWithNewOp<mlir::arith::CmpFOp>(
            op, predicate, op.getLhs(), op.getRhs());
        return mlir::success();
    }
};

struct LowerArrayLiteral : mlir::OpRewritePattern<gloin::ArrayLiteralOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ArrayLiteralOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        mlir::Value value = rewriter.create<mlir::LLVM::ZeroOp>(
            op.getLoc(), op.getValue().getType());
        for (auto [index, element] : llvm::enumerate(op.getElements()))
            value = rewriter.create<mlir::LLVM::InsertValueOp>(
                op.getLoc(), value, element,
                llvm::ArrayRef<int64_t>{static_cast<int64_t>(index)});
        rewriter.replaceOp(op, value);
        return mlir::success();
    }
};

struct LowerRepeatArray : mlir::OpRewritePattern<gloin::RepeatArrayOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::RepeatArrayOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(op.getLayoutType());
        auto one = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 1, 64);
        auto zero = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 64);
        auto count = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), array.getNumElements(), 64);
        auto function = op->getParentOfType<mlir::func::FuncOp>();
        if (!function)
            return mlir::failure();
        mlir::Value address;
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(&function.getBody().front());
            auto one_entry = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 1, 64);
            address = rewriter.create<mlir::LLVM::AllocaOp>(
                op.getLoc(), mlir::LLVM::LLVMPointerType::get(op.getContext()),
                op.getLayoutType(), one_entry, 0);
        }
        auto loop = rewriter.create<mlir::scf::ForOp>(op.getLoc(), zero, count, one);
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(loop.getBody());
            auto slot = rewriter.create<mlir::LLVM::GEPOp>(
                op.getLoc(), address.getType(), op.getLayoutType(), address,
                llvm::ArrayRef<mlir::LLVM::GEPArg>{0, loop.getInductionVar()});
            rewriter.create<mlir::LLVM::StoreOp>(op.getLoc(), op.getElement(), slot);
        }
        rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(
            op, op.getLayoutType(), address);
        return mlir::success();
    }
};

struct LowerStructLiteral : mlir::OpRewritePattern<gloin::StructLiteralOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::StructLiteralOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        mlir::Value value = rewriter.create<mlir::LLVM::ZeroOp>(
            op.getLoc(), op.getValue().getType());
        for (auto [field, index] : llvm::zip(op.getFields(), op.getIndicesAttr().asArrayRef()))
            value = rewriter.create<mlir::LLVM::InsertValueOp>(
                op.getLoc(), value, field, llvm::ArrayRef<int64_t>{index});
        rewriter.replaceOp(op, value);
        return mlir::success();
    }
};

struct LowerZeroedArray : mlir::OpRewritePattern<gloin::ZeroedArrayOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ZeroedArrayOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ZeroOp>(
            op, op.getValue().getType());
        return mlir::success();
    }
};

struct LowerStringLiteral : mlir::OpRewritePattern<gloin::StringLiteralOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::StringLiteralOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto undef = rewriter.create<mlir::LLVM::UndefOp>(op.getLoc(), op.getLayoutType());
        auto data = rewriter.create<mlir::LLVM::InsertValueOp>(
            op.getLoc(), undef, op.getData(), llvm::ArrayRef<int64_t>{0});
        rewriter.replaceOpWithNewOp<mlir::LLVM::InsertValueOp>(
            op, data, op.getLength(), llvm::ArrayRef<int64_t>{1});
        return mlir::success();
    }
};

struct LowerNull : mlir::OpRewritePattern<gloin::NullOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::NullOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ZeroOp>(
            op, op.getValue().getType());
        return mlir::success();
    }
};

struct LowerArenaTypedPointer : mlir::OpRewritePattern<gloin::ArenaTypedPointerOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ArenaTypedPointerOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto target = mlir::cast<gloin::GloinPointerType>(op.getSourceType());
        if (!target.getNullable()) {
            auto zero = rewriter.create<mlir::LLVM::ZeroOp>(
                op.getLoc(), op.getStorage().getType());
            auto nonnull = rewriter.create<mlir::LLVM::ICmpOp>(
                op.getLoc(), mlir::LLVM::ICmpPredicate::ne, op.getStorage(), zero);
            rewriter.create<gloin::AssertOp>(op.getLoc(), nonnull);
        }
        rewriter.replaceOp(op, op.getStorage());
        return mlir::success();
    }
};

struct LowerRawPlace : mlir::OpRewritePattern<gloin::RawPlaceOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::RawPlaceOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto target = native_target_layout();
        if (!target) {
            op.emitError(llvm::toString(target.takeError()));
            return mlir::failure();
        }
        auto layout = measure_type_layout(op.getElementType(),
                                          llvm::DataLayout(target->data_layout));
        if (!layout || layout->size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            if (!layout)
                op.emitError(llvm::toString(layout.takeError()));
            else
                op.emitError("raw element size exceeds the signed range");
            return mlir::failure();
        }
        auto null = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(),
                                                       op.getStorage().getType());
        auto nonnull = rewriter.create<mlir::LLVM::ICmpOp>(
            op.getLoc(), mlir::LLVM::ICmpPredicate::ne, op.getStorage(), null);
        auto bytes = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), layout->size, 64);
        auto enough = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::sge, op.getAvailable(), bytes);
        auto address = rewriter.create<mlir::LLVM::PtrToIntOp>(
            op.getLoc(), rewriter.getI64Type(), op.getStorage());
        auto alignment = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), layout->alignment, 64);
        auto remainder = rewriter.create<mlir::arith::RemUIOp>(
            op.getLoc(), address, alignment);
        auto zero = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 64);
        auto aligned = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::eq, remainder, zero);
        auto valid = rewriter.create<mlir::arith::AndIOp>(
            op.getLoc(), rewriter.create<mlir::arith::AndIOp>(op.getLoc(), nonnull, enough),
            aligned);
        auto selected = rewriter.create<mlir::arith::SelectOp>(
            op.getLoc(), valid, op.getStorage(), null);
        auto conditional = rewriter.create<mlir::scf::IfOp>(op.getLoc(), valid, false);
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(conditional.thenBlock());
            rewriter.create<mlir::LLVM::StoreOp>(op.getLoc(), op.getInitial(), selected);
        }
        rewriter.replaceOp(op, selected);
        return mlir::success();
    }
};

struct LowerRawPadding : mlir::OpRewritePattern<gloin::RawPaddingOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::RawPaddingOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto target = native_target_layout();
        if (!target) {
            op.emitError(llvm::toString(target.takeError()));
            return mlir::failure();
        }
        auto layout = measure_type_layout(op.getElementType(),
                                          llvm::DataLayout(target->data_layout));
        if (!layout) {
            op.emitError(llvm::toString(layout.takeError()));
            return mlir::failure();
        }
        auto alignment = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), layout->alignment, 64);
        auto address = rewriter.create<mlir::LLVM::PtrToIntOp>(
            op.getLoc(), rewriter.getI64Type(), op.getStorage());
        auto remainder = rewriter.create<mlir::arith::RemUIOp>(
            op.getLoc(), address, alignment);
        auto difference = rewriter.create<mlir::arith::SubIOp>(
            op.getLoc(), alignment, remainder);
        rewriter.replaceOpWithNewOp<mlir::arith::RemUIOp>(
            op, difference, alignment);
        return mlir::success();
    }
};

struct LowerPointerOffset : mlir::OpRewritePattern<gloin::PointerOffsetOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::PointerOffsetOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto null = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(), op.getBase().getType());
        auto nonnull = rewriter.create<mlir::LLVM::ICmpOp>(
            op.getLoc(), mlir::LLVM::ICmpPredicate::ne, op.getBase(), null);
        rewriter.create<gloin::AssertOp>(op.getLoc(), nonnull);
        rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
            op, op.getAddress().getType(), op.getElementType(), op.getBase(),
            llvm::ArrayRef<mlir::LLVM::GEPArg>{op.getOffset()});
        return mlir::success();
    }
};

struct LowerRequireNonNull : mlir::OpRewritePattern<gloin::RequireNonNullOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::RequireNonNullOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto null = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(), op.getPointer().getType());
        auto nonnull = rewriter.create<mlir::LLVM::ICmpOp>(
            op.getLoc(), mlir::LLVM::ICmpPredicate::ne, op.getPointer(), null);
        rewriter.create<gloin::AssertOp>(op.getLoc(), nonnull);
        rewriter.replaceOp(op, op.getPointer());
        return mlir::success();
    }
};

struct LowerPointerCompare : mlir::OpRewritePattern<gloin::PointerCompareOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::PointerCompareOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ICmpOp>(
            op, op.getNotEqual() ? mlir::LLVM::ICmpPredicate::ne
                                : mlir::LLVM::ICmpPredicate::eq,
            op.getLhs(), op.getRhs());
        return mlir::success();
    }
};

struct LowerArrayElementAddress : mlir::OpRewritePattern<gloin::ArrayElementAddressOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ArrayElementAddressOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(op.getArrayType());
        auto length = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), array.getNumElements(), 64);
        auto in_bounds = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::ult, op.getIndex(), length);
        rewriter.create<gloin::AssertOp>(op.getLoc(), in_bounds);
        rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
            op, op.getAddress().getType(), op.getArrayType(), op.getBase(),
            llvm::ArrayRef<mlir::LLVM::GEPArg>{0, op.getIndex()});
        return mlir::success();
    }
};

mlir::Value checked_slice_range(mlir::PatternRewriter &rewriter, mlir::Location location,
                                mlir::Value start, mlir::Value end, mlir::Value length) {
    auto ordered = rewriter.create<mlir::arith::CmpIOp>(
        location, mlir::arith::CmpIPredicate::ule, start, end);
    auto bounded = rewriter.create<mlir::arith::CmpIOp>(
        location, mlir::arith::CmpIPredicate::ule, end, length);
    rewriter.create<gloin::AssertOp>(location, ordered);
    rewriter.create<gloin::AssertOp>(location, bounded);
    return rewriter.create<mlir::arith::SubIOp>(location, end, start);
}

mlir::Value slice_descriptor(mlir::PatternRewriter &rewriter, mlir::Location location,
                             mlir::Type type, mlir::Value pointer, mlir::Value length) {
    auto zero = rewriter.create<mlir::LLVM::ZeroOp>(location, type);
    auto with_pointer = rewriter.create<mlir::LLVM::InsertValueOp>(
        location, zero, pointer, llvm::ArrayRef<int64_t>{0});
    return rewriter.create<mlir::LLVM::InsertValueOp>(
        location, with_pointer, length, llvm::ArrayRef<int64_t>{1});
}

struct LowerSliceFromArray : mlir::OpRewritePattern<gloin::SliceFromArrayOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SliceFromArrayOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto array = mlir::cast<mlir::LLVM::LLVMArrayType>(op.getArrayType());
        auto capacity = rewriter.create<mlir::arith::ConstantIntOp>(
            op.getLoc(), array.getNumElements(), 64);
        auto length = checked_slice_range(rewriter, op.getLoc(), op.getStart(), op.getEnd(),
                                          capacity);
        auto pointer = rewriter.create<mlir::LLVM::GEPOp>(
            op.getLoc(), mlir::LLVM::LLVMPointerType::get(op.getContext()), op.getArrayType(),
            op.getBase(), llvm::ArrayRef<mlir::LLVM::GEPArg>{0, op.getStart()});
        rewriter.replaceOp(op, slice_descriptor(rewriter, op.getLoc(), op.getLayoutType(),
                                                pointer, length));
        return mlir::success();
    }
};

struct LowerSliceSubrange : mlir::OpRewritePattern<gloin::SliceSubrangeOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SliceSubrangeOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto pointer = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getBase(), llvm::ArrayRef<int64_t>{0});
        auto capacity = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getBase(), llvm::ArrayRef<int64_t>{1});
        auto length = checked_slice_range(rewriter, op.getLoc(), op.getStart(), op.getEnd(),
                                          capacity);
        auto first = rewriter.create<mlir::LLVM::GEPOp>(
            op.getLoc(), pointer.getType(), op.getElementType(), pointer,
            llvm::ArrayRef<mlir::LLVM::GEPArg>{op.getStart()});
        rewriter.replaceOp(op, slice_descriptor(rewriter, op.getLoc(), op.getLayoutType(),
                                                first, length));
        return mlir::success();
    }
};

struct LowerSliceFromPointer : mlir::OpRewritePattern<gloin::SliceFromPointerOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SliceFromPointerOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto zero = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 64);
        auto nonnegative = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::sge, op.getStart(), zero);
        rewriter.create<gloin::AssertOp>(op.getLoc(), nonnegative);
        auto end_nonnegative = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::sge, op.getEnd(), zero);
        rewriter.create<gloin::AssertOp>(op.getLoc(), end_nonnegative);
        auto ordered = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::ule, op.getStart(), op.getEnd());
        rewriter.create<gloin::AssertOp>(op.getLoc(), ordered);
        auto length = rewriter.create<mlir::arith::SubIOp>(
            op.getLoc(), op.getEnd(), op.getStart());
        auto null_pointer = rewriter.create<mlir::LLVM::ZeroOp>(op.getLoc(),
                                                               op.getBase().getType());
        auto nonnull = rewriter.create<mlir::LLVM::ICmpOp>(
            op.getLoc(), mlir::LLVM::ICmpPredicate::ne, op.getBase(), null_pointer);
        auto empty = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::eq, length, zero);
        rewriter.create<gloin::AssertOp>(
            op.getLoc(), rewriter.create<mlir::arith::OrIOp>(op.getLoc(), nonnull, empty));
        auto first = rewriter.create<mlir::LLVM::GEPOp>(
            op.getLoc(), op.getBase().getType(), op.getElementType(), op.getBase(),
            llvm::ArrayRef<mlir::LLVM::GEPArg>{op.getStart()});
        rewriter.replaceOp(op, slice_descriptor(rewriter, op.getLoc(), op.getLayoutType(),
                                                first, length));
        return mlir::success();
    }
};

struct LowerSliceElementAddress : mlir::OpRewritePattern<gloin::SliceElementAddressOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SliceElementAddressOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto pointer = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getBase(), llvm::ArrayRef<int64_t>{0});
        auto length = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getBase(), llvm::ArrayRef<int64_t>{1});
        auto in_bounds = rewriter.create<mlir::arith::CmpIOp>(
            op.getLoc(), mlir::arith::CmpIPredicate::ult, op.getIndex(), length);
        rewriter.create<gloin::AssertOp>(op.getLoc(), in_bounds);
        rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
            op, op.getAddress().getType(), op.getElementType(), pointer,
            llvm::ArrayRef<mlir::LLVM::GEPArg>{op.getIndex()});
        return mlir::success();
    }
};

struct LowerSliceLength : mlir::OpRewritePattern<gloin::SliceLengthOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SliceLengthOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
            op, op.getBase(), llvm::ArrayRef<int64_t>{1});
        return mlir::success();
    }
};

struct LowerArenaFill : mlir::OpRewritePattern<gloin::ArenaFillOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ArenaFillOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto zero = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 64);
        auto one = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 1, 64);
        auto loop = rewriter.create<mlir::scf::ForOp>(op.getLoc(), zero, op.getCount(), one);
        {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointToStart(loop.getBody());
            auto address = rewriter.create<mlir::LLVM::GEPOp>(
                op.getLoc(), op.getBase().getType(), op.getElementType(), op.getBase(),
                llvm::ArrayRef<mlir::LLVM::GEPArg>{loop.getInductionVar()});
            rewriter.create<mlir::LLVM::StoreOp>(op.getLoc(), op.getFill(), address);
        }
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct LowerStackAlloc : mlir::OpRewritePattern<gloin::StackAllocOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::StackAllocOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        auto one = rewriter.create<mlir::LLVM::ConstantOp>(
            op.getLoc(), rewriter.getI64Type(), rewriter.getI64IntegerAttr(1));
        rewriter.replaceOpWithNewOp<mlir::LLVM::AllocaOp>(
            op, op.getAddress().getType(), op.getElementType(), one, 0);
        return mlir::success();
    }
};

struct LowerLoad : mlir::OpRewritePattern<gloin::LoadOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::LoadOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(
            op, op.getValue().getType(), op.getAddress());
        return mlir::success();
    }
};

struct LowerStore : mlir::OpRewritePattern<gloin::StoreOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::StoreOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        // Do not ask SelectionDAG to split large zero aggregates into thousands
        // of scalar stores. Zeroed arrays have all-zero physical representations
        // (including null pointers and empty counted strings) on our targets.
        auto value = op.getValue();
        if (mlir::isa<mlir::LLVM::LLVMArrayType>(value.getType()) &&
            value.getDefiningOp() &&
            mlir::isa<gloin::ZeroedArrayOp, mlir::LLVM::ZeroOp>(value.getDefiningOp())) {
            auto target = native_target_layout();
            if (!target) {
                op.emitError(llvm::toString(target.takeError()));
                return mlir::failure();
            }
            auto layout = measure_type_layout(value.getType(), llvm::DataLayout(target->data_layout));
            if (!layout) {
                op.emitError(llvm::toString(layout.takeError()));
                return mlir::failure();
            }
            auto zero = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), 0, 8);
            auto bytes = rewriter.create<mlir::arith::ConstantIntOp>(op.getLoc(), layout->size, 64);
            rewriter.create<mlir::LLVM::MemsetOp>(op.getLoc(), op.getAddress(), zero, bytes, false);
            rewriter.eraseOp(op);
            return mlir::success();
        }
        rewriter.create<mlir::LLVM::StoreOp>(op.getLoc(), op.getValue(), op.getAddress());
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct LowerFieldAddress : mlir::OpRewritePattern<gloin::FieldAddressOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::FieldAddressOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(
            op, op.getAddress().getType(), op.getStructType(), op.getBase(),
            llvm::ArrayRef<mlir::LLVM::GEPArg>{0, static_cast<int32_t>(op.getIndex())});
        return mlir::success();
    }
};

struct LowerExtractField : mlir::OpRewritePattern<gloin::ExtractFieldOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::ExtractFieldOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        rewriter.replaceOpWithNewOp<mlir::LLVM::ExtractValueOp>(
            op, op.getAggregate(),
            llvm::ArrayRef<int64_t>{static_cast<int64_t>(op.getIndex())});
        return mlir::success();
    }
};

struct LowerEnumCompare : mlir::OpRewritePattern<gloin::EnumCompareOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::EnumCompareOp op,
                                         mlir::PatternRewriter &rewriter) const override {
        if (op.getLhs().getType() != op.getRhs().getType() ||
            !enum_storage(op.getLhs().getType()))
            return mlir::failure();
        auto left = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getLhs(), llvm::ArrayRef<int64_t>{0});
        auto right = rewriter.create<mlir::LLVM::ExtractValueOp>(
            op.getLoc(), op.getRhs(), llvm::ArrayRef<int64_t>{0});
        auto predicate = op.getNotEqual() ? mlir::arith::CmpIPredicate::ne
                                         : mlir::arith::CmpIPredicate::eq;
        rewriter.replaceOpWithNewOp<mlir::arith::CmpIOp>(op, predicate, left, right);
        return mlir::success();
    }
};

struct LowerGloinCorePass
    : mlir::PassWrapper<LowerGloinCorePass, mlir::OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerGloinCorePass)
    void runOnOperation() override {
        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<LowerAbiCall, LowerErrorLiteral, LowerErrorMessage,
                     LowerResultSuccess, LowerResultFailure, LowerResultIsError,
                     LowerResultValue, LowerResultError,
                     LowerGloinConstant, LowerGloinAssert, LowerCheckedIntegerBinary,
                     LowerCheckedFloatBinary, LowerCheckedIntegerCompare,
                     LowerCheckedFloatCompare, LowerArrayLiteral, LowerRepeatArray,
                     LowerStructLiteral,
                     LowerZeroedArray, LowerStringLiteral, LowerNull,
                     LowerArenaTypedPointer, LowerRawPlace, LowerRawPadding,
                     LowerPointerOffset,
                     LowerRequireNonNull, LowerPointerCompare,
                     LowerArrayElementAddress, LowerSliceFromArray, LowerSliceFromPointer,
                     LowerSliceSubrange,
                     LowerSliceElementAddress, LowerSliceLength, LowerArenaFill,
                     LowerStackAlloc, LowerLoad, LowerStore,
                     LowerFieldAddress, LowerExtractField, LowerEnumConstant,
                     LowerEnumCompare>(&getContext());
        if (mlir::failed(mlir::applyPatternsGreedily(getOperation(), std::move(patterns))))
            signalPassFailure();
    }
};

void record_error(Diagnostics &diagnostics, DiagnosticStage stage, mlir::Location location,
                  std::string message, const std::shared_ptr<const SourceFile> &source) {
    SourceSpan span;
    std::optional<DiagnosticPosition> position;
    if (auto file = location->findInstanceOf<mlir::FileLineColLoc>()) {
        position = DiagnosticPosition{file.getFilename().str(), file.getLine(), file.getColumn()};
        if (source && source->name == position->filename && position->line > 0 &&
            position->line <= source->line_starts.size() && position->column > 0) {
            auto offset = source->line_starts[position->line - 1] + position->column - 1;
            if (offset <= source->text.size())
                span = {source, offset, offset};
        }
    }
    diagnostics.error(stage, std::move(span), std::move(message), std::move(position));
}

bool check_legality(mlir::ModuleOp module, bool final) {
    bool valid = true;
    const bool checked_module = !final && module->hasAttr("gloin.checked");
    if (!final)
        module.walk([&](mlir::func::FuncOp function) {
            auto inputs = function->getAttrOfType<mlir::ArrayAttr>("gloin.layout_inputs");
            auto results = function->getAttrOfType<mlir::ArrayAttr>("gloin.layout_results");
            const auto signature = function.getFunctionType();
            bool has_source_types = false;
            for (auto type : signature.getInputs())
                has_source_types |= type.getDialect().getNamespace() == "gloin";
            for (auto type : signature.getResults())
                has_source_types |= type.getDialect().getNamespace() == "gloin";
            if (!inputs && !results && !has_source_types)
                return;
            if (!inputs || !results || inputs.size() != signature.getNumInputs() ||
                results.size() != signature.getNumResults()) {
                function.emitOpError("checked source types require a complete layout signature");
                valid = false;
                return;
            }
            auto check_entry = [&](mlir::Type source, mlir::Attribute attribute) {
                auto layout = mlir::dyn_cast<mlir::TypeAttr>(attribute);
                if (!layout || !gloin::matchesSourceStorage(source, layout.getValue()) ||
                    !mlir::LLVM::isCompatibleType(layout.getValue())) {
                    function.emitOpError("source type disagrees with its checked layout signature");
                    valid = false;
                }
            };
            for (auto [source, layout] : llvm::zip(signature.getInputs(), inputs))
                check_entry(source, layout);
            for (auto [source, layout] : llvm::zip(signature.getResults(), results))
                check_entry(source, layout);
        });
    module.walk([&](mlir::Operation *op) {
        const auto dialect = op->getName().getDialectNamespace();
        if (checked_module && mlir::isa<mlir::LLVM::CallOp>(op)) {
            op->emitError("checked source must call runtime functions through gloin.abi_call");
            valid = false;
        }
        if (checked_module) {
            if (auto constant = mlir::dyn_cast<gloin::EnumConstantOp>(op);
                constant && constant.getValue().getType() != constant.getSourceType()) {
                op->emitError("checked enum construction must produce a nominal SSA value");
                valid = false;
            }
            if (auto compare = mlir::dyn_cast<gloin::EnumCompareOp>(op);
                compare && (compare.getLhs().getType() != compare.getSourceType() ||
                            compare.getRhs().getType() != compare.getSourceType())) {
                op->emitError("checked enum comparison requires nominal SSA operands");
                valid = false;
            }
            if (auto literal = mlir::dyn_cast<gloin::ArrayLiteralOp>(op);
                literal && literal.getValue().getType() != literal.getSourceType()) {
                op->emitError("checked array literal requires a source-typed result");
                valid = false;
            }
            if (auto repeat = mlir::dyn_cast<gloin::RepeatArrayOp>(op);
                repeat && repeat.getValue().getType() != repeat.getSourceType()) {
                op->emitError("checked repeated array requires a source-typed result");
                valid = false;
            }
            if (auto literal = mlir::dyn_cast<gloin::StructLiteralOp>(op);
                literal && literal.getValue().getType() != literal.getSourceType()) {
                op->emitError("checked struct literal requires a nominal SSA result");
                valid = false;
            }
            if (auto zeroed = mlir::dyn_cast<gloin::ZeroedArrayOp>(op);
                zeroed && zeroed.getValue().getType() != zeroed.getSourceType()) {
                op->emitError("checked zeroed array requires a source-typed result");
                valid = false;
            }
            if (auto literal = mlir::dyn_cast<gloin::StringLiteralOp>(op);
                literal && literal.getValue().getType() != literal.getSourceType()) {
                op->emitError("checked string literal requires a source-typed result");
                valid = false;
            }
            if (auto field = mlir::dyn_cast<gloin::ExtractFieldOp>(op);
                field && (field.getAggregate().getType() != field.getSourceType() ||
                          field.getValue().getType() != field.getFieldSourceType())) {
                op->emitError("checked field extraction requires source-typed operands and result");
                valid = false;
            }
            if (auto null_value = mlir::dyn_cast<gloin::NullOp>(op);
                null_value && null_value.getValue().getType() != null_value.getSourceType()) {
                op->emitError("checked null construction requires a source pointer result");
                valid = false;
            }
            if (auto allocation = mlir::dyn_cast<gloin::ArenaTypedPointerOp>(op);
                allocation &&
                    (allocation.getPointer().getType() != allocation.getSourceType() ||
                     !mlir::isa<gloin::GloinPointerType>(
                         allocation.getStorage().getType()))) {
                op->emitError("checked arena allocation requires typed raw storage and result");
                valid = false;
            }
            if (auto placement = mlir::dyn_cast<gloin::RawPlaceOp>(op);
                placement &&
                    (placement.getPointer().getType() != placement.getSourceType() ||
                     !mlir::isa<gloin::GloinPointerType>(
                         placement.getStorage().getType()))) {
                op->emitError("checked raw placement requires typed storage and result");
                valid = false;
            }
            if (auto padding = mlir::dyn_cast<gloin::RawPaddingOp>(op);
                padding && !mlir::isa<gloin::GloinPointerType>(
                               padding.getStorage().getType())) {
                op->emitError("checked raw padding requires source-typed byte storage");
                valid = false;
            }
            if (auto offset = mlir::dyn_cast<gloin::PointerOffsetOp>(op);
                offset && (offset.getBase().getType() != offset.getSourceType() ||
                           offset.getAddress().getType() != offset.getSourceType())) {
                op->emitError("checked pointer offset requires source-typed operands and result");
                valid = false;
            }
            if (auto check = mlir::dyn_cast<gloin::RequireNonNullOp>(op);
                check && (check.getPointer().getType() != check.getSourceType() ||
                          !mlir::isa<gloin::GloinPointerType>(
                              check.getCheckedPointer().getType()))) {
                op->emitError("checked non-null guard requires source-typed input and result");
                valid = false;
            }
            if (auto compare = mlir::dyn_cast<gloin::PointerCompareOp>(op);
                compare && (compare.getLhs().getType() != compare.getSourceType() ||
                            compare.getRhs().getType() != compare.getSourceType())) {
                op->emitError("checked pointer comparison requires matching source operands");
                valid = false;
            }
            if (auto allocation = mlir::dyn_cast<gloin::StackAllocOp>(op);
                allocation && !mlir::isa<gloin::GloinPointerType>(
                                  allocation.getAddress().getType())) {
                op->emitError("checked stack allocation requires a source-typed address");
                valid = false;
            }
            if (auto field = mlir::dyn_cast<gloin::FieldAddressOp>(op);
                field && (!mlir::isa<gloin::GloinPointerType>(field.getBase().getType()) ||
                          !mlir::isa<gloin::GloinPointerType>(field.getAddress().getType()))) {
                op->emitError("checked field address requires source-typed base and result");
                valid = false;
            }
            if (auto element = mlir::dyn_cast<gloin::ArrayElementAddressOp>(op);
                element && (!mlir::isa<gloin::GloinPointerType>(element.getBase().getType()) ||
                            !mlir::isa<gloin::GloinPointerType>(
                                element.getAddress().getType()))) {
                op->emitError("checked array element address requires source-typed base and result");
                valid = false;
            }
            if (auto slice = mlir::dyn_cast<gloin::SliceFromArrayOp>(op);
                slice && (!mlir::isa<gloin::GloinPointerType>(slice.getBase().getType()) ||
                          slice.getValue().getType() != slice.getSourceType())) {
                op->emitError("checked array slice requires a source-typed borrow and result");
                valid = false;
            }
            if (auto slice = mlir::dyn_cast<gloin::SliceSubrangeOp>(op);
                slice && (slice.getBase().getType() != slice.getSourceType() ||
                          slice.getValue().getType() != slice.getSourceType())) {
                op->emitError("checked subslice requires source-typed operands and result");
                valid = false;
            }
            if (auto slice = mlir::dyn_cast<gloin::SliceFromPointerOp>(op);
                slice && (!mlir::isa<gloin::GloinPointerType>(slice.getBase().getType()) ||
                          slice.getValue().getType() != slice.getSourceType())) {
                op->emitError("checked pointer slice requires source-typed operands and result");
                valid = false;
            }
            if (auto element = mlir::dyn_cast<gloin::SliceElementAddressOp>(op);
                element && (element.getBase().getType() != element.getSourceType() ||
                            !mlir::isa<gloin::GloinPointerType>(
                                element.getAddress().getType()))) {
                op->emitError("checked slice element requires a source-typed slice and address");
                valid = false;
            }
            if (auto length = mlir::dyn_cast<gloin::SliceLengthOp>(op);
                length && length.getBase().getType() != length.getSourceType()) {
                op->emitError("checked slice length requires a source-typed slice");
                valid = false;
            }
            if (auto fill = mlir::dyn_cast<gloin::ArenaFillOp>(op);
                fill && (fill.getBase().getType() != fill.getSourceType() ||
                         fill.getFill().getType() != fill.getElementSourceType())) {
                op->emitError("checked arena fill requires source-typed storage and value");
                valid = false;
            }
            if (auto load = mlir::dyn_cast<gloin::LoadOp>(op);
                load && (load.getValue().getType() != load.getSourceType() ||
                         !mlir::isa<gloin::GloinPointerType>(load.getAddress().getType()))) {
                op->emitError("checked load requires source-typed address and value");
                valid = false;
            }
            if (auto literal = mlir::dyn_cast<gloin::ErrorLiteralOp>(op);
                literal && (!mlir::isa<gloin::GloinStringType>(literal.getMessage().getType()) ||
                            !mlir::isa<gloin::GloinErrorType>(literal.getValue().getType()))) {
                op->emitError("checked error construction requires source-typed values");
                valid = false;
            }
            if (auto success = mlir::dyn_cast<gloin::ResultSuccessOp>(op);
                success && !mlir::isa<gloin::GloinResultType>(success.getValue().getType())) {
                op->emitError("checked result success requires a source-typed result");
                valid = false;
            }
            if (auto failure = mlir::dyn_cast<gloin::ResultFailureOp>(op);
                failure && (!mlir::isa<gloin::GloinResultType>(failure.getValue().getType()) ||
                            !mlir::isa<gloin::GloinErrorType>(failure.getError().getType()))) {
                op->emitError("checked result failure requires a source-typed error");
                valid = false;
            }
            if (auto inspect = mlir::dyn_cast<gloin::ResultIsErrorOp>(op);
                inspect && !mlir::isa<gloin::GloinResultType>(inspect.getValue().getType())) {
                op->emitError("checked result inspection requires a source-typed result");
                valid = false;
            }
            if (auto value = mlir::dyn_cast<gloin::ResultValueOp>(op);
                value && !mlir::isa<gloin::GloinResultType>(value.getOutcome().getType())) {
                op->emitError("checked result value access requires a source-typed result");
                valid = false;
            }
            if (auto error = mlir::dyn_cast<gloin::ResultErrorOp>(op);
                error && !mlir::isa<gloin::GloinResultType>(error.getOutcome().getType())) {
                op->emitError("checked result error access requires a source-typed result");
                valid = false;
            }
            if (auto store = mlir::dyn_cast<gloin::StoreOp>(op);
                store && (store.getValue().getType() != store.getSourceType() ||
                          !mlir::isa<gloin::GloinPointerType>(store.getAddress().getType()))) {
                op->emitError("checked store requires source-typed address and value");
                valid = false;
            }
        }
        bool allowed =
            op == module.getOperation() || (op->getName().isRegistered() && dialect == "llvm");
        if (!final)
            allowed |= mlir::isa<mlir::UnrealizedConversionCastOp>(op) ||
                       mlir::isa<gloin::StructDefinitionOp, gloin::FromLayoutOp,
                                 gloin::ToLayoutOp, gloin::AbiCallOp,
                                 gloin::ConstantOp, gloin::AssertOp,
                                 gloin::CheckedIntegerBinaryOp, gloin::CheckedFloatBinaryOp,
                                 gloin::CheckedIntegerCompareOp, gloin::CheckedFloatCompareOp,
                                 gloin::ArrayLiteralOp, gloin::RepeatArrayOp,
                                 gloin::StructLiteralOp,
                                 gloin::ZeroedArrayOp, gloin::StringLiteralOp, gloin::NullOp,
                                 gloin::ErrorLiteralOp, gloin::ErrorMessageOp,
                                 gloin::ResultSuccessOp, gloin::ResultFailureOp,
                                 gloin::ResultIsErrorOp, gloin::ResultValueOp,
                                 gloin::ResultErrorOp,
                                 gloin::ArenaTypedPointerOp, gloin::RawPlaceOp,
                                 gloin::RawPaddingOp,
                                 gloin::PointerOffsetOp, gloin::ArrayElementAddressOp,
                                 gloin::SliceFromArrayOp, gloin::SliceFromPointerOp,
                                 gloin::SliceSubrangeOp,
                                 gloin::SliceElementAddressOp, gloin::SliceLengthOp,
                                 gloin::ArenaFillOp,
                                 gloin::RequireNonNullOp, gloin::PointerCompareOp,
                                 gloin::StackAllocOp, gloin::LoadOp, gloin::StoreOp,
                                 gloin::FieldAddressOp, gloin::ExtractFieldOp,
                                 gloin::EnumConstantOp, gloin::EnumCompareOp>(op) ||
                       (op->getName().isRegistered() &&
                        (dialect == "func" || dialect == "arith" || dialect == "cf" ||
                         dialect == "scf" || dialect == "memref"));
        if (!allowed) {
            op->emitError(final ? "Operation is not legal at LLVM export: "
                                : "No supported lowering for operation: ")
                << op->getName();
            valid = false;
        }
        auto check_type = [&](mlir::Type type) {
            const auto ns = type.getDialect().getNamespace();
            const bool legal =
                final ? mlir::LLVM::isCompatibleOuterType(type)
                      : ns == "builtin" || ns == "llvm" ||
                            mlir::isa<gloin::GloinPointerType, gloin::GloinArrayType,
                                      gloin::GloinSliceType,
                                      gloin::GloinEnumType, gloin::GloinStructType,
                                      gloin::GloinStringType, gloin::GloinErrorType,
                                      gloin::GloinResultType>(type);
            if (!legal) {
                op->emitError(final ? "Type is not legal at LLVM export: "
                                    : "No supported lowering for type: ")
                    << type;
                valid = false;
            }
        };
        for (auto type : op->getOperandTypes())
            type.walk(check_type);
        for (auto type : op->getResultTypes())
            type.walk(check_type);
        for (auto &region : op->getRegions())
            for (auto &block : region)
                for (auto argument : block.getArguments())
                    argument.getType().walk(check_type);
        for (auto named : op->getAttrs()) {
            const auto name = named.getName().getValue();
            auto attr = named.getValue();
            if (!final && name == "source_type" &&
                mlir::isa<gloin::NullOp, gloin::PointerOffsetOp,
                          gloin::RequireNonNullOp, gloin::PointerCompareOp,
                          gloin::ArenaTypedPointerOp, gloin::RawPlaceOp,
                          gloin::EnumConstantOp, gloin::EnumCompareOp,
                          gloin::ArrayLiteralOp, gloin::RepeatArrayOp,
                          gloin::ZeroedArrayOp,
                          gloin::StringLiteralOp,
                          gloin::ArrayElementAddressOp, gloin::StructLiteralOp,
                          gloin::SliceFromArrayOp, gloin::SliceFromPointerOp,
                          gloin::SliceSubrangeOp,
                          gloin::SliceElementAddressOp, gloin::SliceLengthOp,
                          gloin::ArenaFillOp,
                          gloin::FieldAddressOp, gloin::ExtractFieldOp,
                          gloin::StackAllocOp, gloin::LoadOp, gloin::StoreOp>(op))
                continue;
            // LLVM's memref/index conversions retain index-typed integer
            // payloads in llvm.mlir.constant with a concrete integer result.
            if (final && name == "value" && mlir::isa<mlir::LLVM::ConstantOp>(op)) {
                if (auto integer = mlir::dyn_cast<mlir::IntegerAttr>(attr);
                    integer && integer.getType().isIndex())
                    continue;
            }
            attr.walk<mlir::WalkOrder::PreOrder>(
                [](mlir::Attribute) { return mlir::WalkResult::advance(); }, check_type);
        }
    });
    return valid;
}
} // namespace

bool verify_module(mlir::ModuleOp module, Diagnostics &diagnostics,
                   std::shared_ptr<const SourceFile> source) {
    if (diagnostics.has_errors())
        return false;
    if (!module) {
        diagnostics.error(DiagnosticStage::Verification, {}, "Cannot verify a null module");
        return false;
    }
    mlir::ScopedDiagnosticHandler handler(module.getContext(), [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            record_error(diagnostics, DiagnosticStage::Verification, diagnostic.getLocation(),
                         diagnostic.str(), source);
        return mlir::success();
    });
    const auto result = mlir::verify(module);
    if (mlir::failed(result) && !diagnostics.has_errors())
        record_error(diagnostics, DiagnosticStage::Verification, module.getLoc(),
                     "Module verification failed", source);
    return mlir::succeeded(result) && !diagnostics.has_errors();
}

mlir::OwningOpRef<mlir::ModuleOp> lower_to_llvm(mlir::OwningOpRef<mlir::ModuleOp> module,
                                                Diagnostics &diagnostics,
                                                std::shared_ptr<const SourceFile> source) {
    if (!verify_module(module.get(), diagnostics, source))
        return {};
    mlir::ScopedDiagnosticHandler handler(module->getContext(), [&](mlir::Diagnostic &diagnostic) {
        if (diagnostic.getSeverity() == mlir::DiagnosticSeverity::Error)
            record_error(diagnostics, DiagnosticStage::Lowering, diagnostic.getLocation(),
                         diagnostic.str(), source);
        return mlir::success();
    });
    if (!check_legality(*module, false))
        return {};
    mlir::PassManager passes(module->getContext());
    passes.enableVerifier(true);
    passes.addPass(std::make_unique<LowerGloinSignaturesPass>());
    passes.addPass(std::make_unique<LowerGloinCorePass>());
    passes.addPass(mlir::createSCFToControlFlowPass());
    passes.addPass(mlir::createConvertControlFlowToLLVMPass());
    passes.addPass(mlir::createArithToLLVMConversionPass());
    passes.addPass(mlir::createConvertFuncToLLVMPass());
    passes.addPass(mlir::createFinalizeMemRefToLLVMConversionPass());
    passes.addPass(mlir::createReconcileUnrealizedCastsPass());
    if (mlir::failed(passes.run(*module)) || !check_legality(*module, true) ||
        mlir::failed(mlir::verify(*module)) || diagnostics.has_errors()) {
        if (!diagnostics.has_errors())
            record_error(diagnostics, DiagnosticStage::Lowering, module->getLoc(),
                         "LLVM lowering failed", source);
        return {};
    }
    return module;
}
