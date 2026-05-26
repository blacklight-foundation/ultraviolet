// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_op.cpp
// Canonical owner for LLVM IR checked operation instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckOp &check) const
{
  llvm::Value *ok = llvm::ConstantInt::getTrue(emitter.GetContext());
  const std::uint16_t code = PanicCodeFromString(check.reason);

  if ((check.op == "/" || check.op == "%") && check.rhs.has_value())
  {
    llvm::Value *rhs = EvaluateOrDefault(*check.rhs);
    if (rhs && rhs->getType()->isIntegerTy())
    {
      llvm::Value *zero = llvm::ConstantInt::get(rhs->getType(), 0);
      ok = builder.CreateICmpNE(rhs, zero);
    }
  }
  else if ((check.op == "<<" || check.op == ">>") && check.rhs.has_value())
  {
    llvm::Value *lhs = EvaluateOrDefault(check.lhs);
    llvm::Value *rhs = EvaluateOrDefault(*check.rhs);
    if (rhs && rhs->getType()->isIntegerTy())
    {
      llvm::Value *rhs64 = rhs;
      if (rhs64->getType()->getIntegerBitWidth() != 64)
      {
        rhs64 = builder.CreateIntCast(rhs64, llvm::Type::getInt64Ty(emitter.GetContext()), false);
      }
      std::uint64_t width = 64;
      if (lhs && lhs->getType()->isIntegerTy())
      {
        width = lhs->getType()->getIntegerBitWidth();
      }
      ok = builder.CreateICmpULT(
          rhs64,
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()), width));
    }
  }
  else if (check.op == "-" && !check.rhs.has_value())
  {
    llvm::Value *lhs = EvaluateOrDefault(check.lhs);
    if (lhs && lhs->getType()->isIntegerTy())
    {
      bool signed_ty = true;
      if (const LowerCtx *ctx = emitter.GetCurrentCtx())
      {
        analysis::TypeRef lhs_type = ctx->LookupValueType(check.lhs);
        if (lhs_type)
        {
          signed_ty = IsSignedIntegerType(lhs_type);
        }
      }
      if (signed_ty)
      {
        const unsigned bits = lhs->getType()->getIntegerBitWidth();
        llvm::Value *minv = llvm::ConstantInt::get(
            lhs->getType(),
            llvm::APInt::getSignedMinValue(bits));
        ok = builder.CreateICmpNE(lhs, minv);
      }
    }
  }
  else if (check.op == "nonnull")
  {
    llvm::Value *lhs = EvaluateOrDefault(check.lhs);
    if (lhs)
    {
      if (lhs->getType()->isPointerTy())
      {
        llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(lhs->getType()));
        ok = builder.CreateICmpNE(lhs, null_ptr);
      }
      else if (lhs->getType()->isIntegerTy())
      {
        llvm::Value *zero = llvm::ConstantInt::get(lhs->getType(), 0);
        ok = builder.CreateICmpNE(lhs, zero);
      }
      else
      {
        ok = AsBool(&builder, lhs);
      }
    }
  }
  else if (check.op == "addr_active")
  {
    llvm::Value *lhs = EvaluateOrDefault(check.lhs);
    if (lhs)
    {
      ok = AsBool(&builder, lhs);
    }
  }

  EmitPanicIfFalse(emitter, &builder, ok, code);
}

} // namespace ultraviolet::codegen::emit_detail
