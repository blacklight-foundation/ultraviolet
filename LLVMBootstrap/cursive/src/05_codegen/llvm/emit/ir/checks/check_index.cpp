// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_index.cpp
// Canonical owner for LLVM IR index check instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckIndex &check) const
{
  auto len_opt = StaticLengthOf(check.base);
  llvm::Value *dynamic_len = nullptr;
  if (!len_opt.has_value())
  {
    llvm::Value *base = EvaluateOrDefault(check.base);
    if (base)
    {
      if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
      {
        len_opt = arr_ty->getNumElements();
      }
      else
      {
        dynamic_len = DynamicLengthOf(check.base);
      }
    }
  }
  if (!len_opt.has_value() && dynamic_len == nullptr)
  {
    return;
  }
  llvm::Value *idx = EvaluateOrDefault(check.index);
  if (!idx || !idx->getType()->isIntegerTy())
  {
    return;
  }
  llvm::Value *idx64 = idx;
  if (idx64->getType()->getIntegerBitWidth() != 64)
  {
    idx64 = builder.CreateIntCast(idx64, llvm::Type::getInt64Ty(emitter.GetContext()), false);
  }
  llvm::Value *len64 = nullptr;
  if (len_opt.has_value())
  {
    len64 = llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()), *len_opt);
  }
  else
  {
    len64 = dynamic_len;
    if (!len64 || !len64->getType()->isIntegerTy())
    {
      return;
    }
    if (len64->getType()->getIntegerBitWidth() != 64)
    {
      len64 = builder.CreateIntCast(
          len64, llvm::Type::getInt64Ty(emitter.GetContext()), false);
    }
  }
  llvm::Value *ok = builder.CreateICmpULT(
      idx64,
      len64);
  EmitPanicReturnIfFalse(emitter, &builder, ok, PanicCode(PanicReason::Bounds));
}

} // namespace cursive::codegen::emit_detail
