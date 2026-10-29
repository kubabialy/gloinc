#include "selection_lowering.h"
#include "dialect/GloinDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

namespace {
void dispatch(mlir::PatternRewriter &rewriter, mlir::Location loc, mlir::Value subject,
              mlir::TypeAttr source, mlir::ArrayAttr keys, llvm::ArrayRef<mlir::Block *> arms,
              mlir::Block *fallback) {
    for (size_t i = 0; i < keys.size(); ++i) {
        if (mlir::isa<mlir::UnitAttr>(keys[i])) {
            rewriter.create<mlir::cf::BranchOp>(loc, arms[i]);
            return;
        }
        auto test =
            rewriter.create<gloin::CaseTestOp>(loc, rewriter.getI1Type(), subject, keys[i], source);
        auto *region = rewriter.getInsertionBlock()->getParent();
        auto *next = i + 1 == keys.size() ? fallback : new mlir::Block();
        if (next != fallback)
            region->push_back(next);
        rewriter.create<mlir::cf::CondBranchOp>(loc, test, arms[i], mlir::ValueRange{}, next,
                                                mlir::ValueRange{});
        if (next == fallback)
            return;
        rewriter.setInsertionPointToStart(next);
    }
    rewriter.create<mlir::cf::BranchOp>(loc, fallback);
}

struct LowerSwitch : mlir::OpRewritePattern<gloin::SwitchOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::SwitchOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        llvm::SmallVector<mlir::Block *> destinations(op.getDestinations().begin(),
                                                      op.getDestinations().end());
        dispatch(rewriter, op.getLoc(), op.getSubject(), op.getSourceTypeAttr(), op.getKeys(),
                 llvm::ArrayRef(destinations).drop_back(), destinations.back());
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct LowerMatch : mlir::OpRewritePattern<gloin::MatchOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::MatchOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        if (op.getValue().getType() != op.getLayoutType())
            return mlir::failure();
        auto *before = op->getBlock();
        auto *after = rewriter.splitBlock(before, mlir::Block::iterator(op));
        auto value = after->addArgument(op.getValue().getType(), op.getLoc());
        llvm::SmallVector<mlir::Block *> arms;
        for (auto &region : op.getArms()) {
            arms.push_back(&region.front());
            for (auto &block : region) {
                if (auto yield = mlir::dyn_cast<gloin::MatchYieldOp>(block.getTerminator())) {
                    rewriter.setInsertionPoint(yield);
                    rewriter.replaceOpWithNewOp<mlir::cf::BranchOp>(
                        yield, after, mlir::ValueRange{yield.getValue()});
                }
            }
            rewriter.inlineRegionBefore(region, *after->getParent(), after->getIterator());
        }
        auto *fallback = rewriter.createBlock(after->getParent(), after->getIterator());
        rewriter.create<mlir::LLVM::Trap>(op.getLoc());
        rewriter.create<mlir::LLVM::UnreachableOp>(op.getLoc());
        rewriter.setInsertionPointToEnd(before);
        dispatch(rewriter, op.getLoc(), op.getSubject(), op.getSourceTypeAttr(), op.getKeys(), arms,
                 fallback);
        rewriter.replaceOp(op, value);
        return mlir::success();
    }
};

struct LowerCaseTest : mlir::OpRewritePattern<gloin::CaseTestOp> {
    using OpRewritePattern::OpRewritePattern;
    mlir::LogicalResult matchAndRewrite(gloin::CaseTestOp op,
                                        mlir::PatternRewriter &rewriter) const override {
        auto loc = op.getLoc();
        auto subject = op.getSubject();
        if (mlir::isa<gloin::GloinStringType>(op.getSourceType())) {
            if (!mlir::isa<mlir::LLVM::LLVMStructType>(subject.getType()))
                return mlir::failure();
            auto text = mlir::cast<mlir::StringAttr>(op.getKey()).getValue();
            auto length = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, subject,
                                                                      llvm::ArrayRef<int64_t>{1});
            auto size = rewriter.create<mlir::arith::ConstantIntOp>(loc, text.size(), 64);
            auto length_equal = rewriter.create<mlir::arith::CmpIOp>(
                loc, mlir::arith::CmpIPredicate::eq, length, size);
            if (text.empty()) {
                rewriter.replaceOp(op, length_equal);
                return mlir::success();
            }
            auto module = op->getParentOfType<mlir::ModuleOp>();
            mlir::LLVM::GlobalOp global;
            {
                mlir::OpBuilder::InsertionGuard guard(rewriter);
                rewriter.setInsertionPointToStart(module.getBody());
                unsigned suffix = 0;
                std::string name;
                do {
                    name = "__gloin_case_bytes_" + std::to_string(suffix++);
                } while (module.lookupSymbol(name));
                auto type = mlir::LLVM::LLVMArrayType::get(rewriter.getI8Type(), text.size());
                global = rewriter.create<mlir::LLVM::GlobalOp>(loc, type, true,
                                                               mlir::LLVM::Linkage::Internal, name,
                                                               rewriter.getStringAttr(text));
            }
            auto expected = rewriter.create<mlir::LLVM::AddressOfOp>(loc, global);
            auto actual = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, subject,
                                                                      llvm::ArrayRef<int64_t>{0});
            auto zero = rewriter.create<mlir::arith::ConstantIntOp>(loc, 0, 64);
            auto one = rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 64);
            auto yes = rewriter.create<mlir::arith::ConstantIntOp>(loc, 1, 1);
            auto no = rewriter.create<mlir::arith::ConstantIntOp>(loc, 0, 1);
            auto *before = op->getBlock();
            auto *after = rewriter.splitBlock(before, mlir::Block::iterator(op));
            auto answer = after->addArgument(rewriter.getI1Type(), loc);
            auto *loop = rewriter.createBlock(after->getParent(), after->getIterator(),
                                              {rewriter.getI64Type()}, {loc});
            auto index = loop->getArgument(0);
            auto pointer = mlir::LLVM::LLVMPointerType::get(rewriter.getContext());
            auto a = rewriter.create<mlir::LLVM::GEPOp>(loc, pointer, rewriter.getI8Type(), actual,
                                                        mlir::ValueRange{index});
            auto b = rewriter.create<mlir::LLVM::GEPOp>(loc, pointer, rewriter.getI8Type(),
                                                        expected, mlir::ValueRange{index});
            auto left = rewriter.create<mlir::LLVM::LoadOp>(loc, rewriter.getI8Type(), a);
            auto right = rewriter.create<mlir::LLVM::LoadOp>(loc, rewriter.getI8Type(), b);
            auto equal = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq,
                                                              left, right);
            auto next = rewriter.create<mlir::arith::AddIOp>(loc, index, one);
            auto *advance = rewriter.createBlock(after->getParent(), after->getIterator());
            auto more = rewriter.create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ult,
                                                             next, size);
            rewriter.create<mlir::cf::CondBranchOp>(loc, more, loop, mlir::ValueRange{next}, after,
                                                    mlir::ValueRange{yes});
            rewriter.setInsertionPointToEnd(loop);
            rewriter.create<mlir::cf::CondBranchOp>(loc, equal, advance, mlir::ValueRange{}, after,
                                                    mlir::ValueRange{no});
            rewriter.setInsertionPointToEnd(before);
            rewriter.create<mlir::cf::CondBranchOp>(loc, length_equal, loop, mlir::ValueRange{zero},
                                                    after, mlir::ValueRange{no});
            rewriter.replaceOp(op, answer);
            return mlir::success();
        }
        if (mlir::isa<gloin::GloinEnumType, gloin::GloinResultType>(op.getSourceType())) {
            if (!mlir::isa<mlir::LLVM::LLVMStructType>(subject.getType()))
                return mlir::failure();
            subject = rewriter.create<mlir::LLVM::ExtractValueOp>(loc, subject,
                                                                  llvm::ArrayRef<int64_t>{0});
        }
        auto key = mlir::cast<mlir::IntegerAttr>(op.getKey());
        if (auto integer = mlir::dyn_cast<gloin::GloinIntegerType>(op.getSourceType())) {
            auto width = mlir::cast<mlir::IntegerType>(subject.getType()).getWidth();
            auto value = integer.getSignedness() == gloin::IntegerSignedness::Signed
                             ? key.getValue().sextOrTrunc(width)
                             : key.getValue().zextOrTrunc(width);
            key = rewriter.getIntegerAttr(subject.getType(), value);
        }
        auto constant = rewriter.create<mlir::arith::ConstantOp>(loc, key);
        rewriter.replaceOpWithNewOp<mlir::arith::CmpIOp>(op, mlir::arith::CmpIPredicate::eq,
                                                         subject, constant);
        return mlir::success();
    }
};
} // namespace

void populate_selection_lowering(mlir::RewritePatternSet &patterns) {
    patterns.add<LowerSwitch, LowerMatch, LowerCaseTest>(patterns.getContext());
}
