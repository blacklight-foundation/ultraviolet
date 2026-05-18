// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/transmute.cpp
// Canonical owner for LLVM IR transmute instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRTransmute &transmute) const
{
  llvm::Value *src = EvaluateOrDefault(transmute.value);
  llvm::Type *target_ty = emitter.GetLLVMType(transmute.to);
  if (!target_ty)
  {
    target_ty = ExpectedLLVMType(transmute.result);
  }
  if (!src || !target_ty || target_ty->isVoidTy())
  {
    emitter.SetTempValue(transmute.result, DefaultFor(transmute.result));
    return;
  }

  llvm::Type *src_ty = src->getType();
  llvm::Value *out = nullptr;
  if (src_ty == target_ty)
  {
    out = src;
  }
  else if (src_ty->isPointerTy() && target_ty->isPointerTy())
  {
    out = builder.CreateBitCast(src, target_ty);
  }
  else if (src_ty->isPointerTy() && target_ty->isIntegerTy())
  {
    out = builder.CreatePtrToInt(src, target_ty);
  }
  else if (src_ty->isIntegerTy() && target_ty->isPointerTy())
  {
    out = builder.CreateIntToPtr(src, target_ty);
  }
  else
  {
    const llvm::DataLayout &layout = emitter.GetModule().getDataLayout();
    const std::uint64_t src_bits = layout.getTypeSizeInBits(src_ty);
    const std::uint64_t dst_bits = layout.getTypeSizeInBits(target_ty);
    if (src_bits == dst_bits &&
        src_ty->isFirstClassType() &&
        target_ty->isFirstClassType())
    {
      if ((src_ty->isIntegerTy() && target_ty->isFloatingPointTy()) ||
          (src_ty->isFloatingPointTy() && target_ty->isIntegerTy()) ||
          (src_ty->isIntegerTy() && target_ty->isIntegerTy()) ||
          (src_ty->isFloatingPointTy() && target_ty->isFloatingPointTy()) ||
          (src_ty->isVectorTy() && target_ty->isVectorTy()))
      {
        out = builder.CreateBitCast(src, target_ty);
      }
      else
      {
        // Fallback to memory reinterpretation for equal-sized first-class
        // values when direct bitcast is not legal between categories.
        llvm::AllocaInst *slot = builder.CreateAlloca(src_ty, nullptr, "transmute.tmp");
        builder.CreateStore(src, slot);
        llvm::Value *cast_ptr = builder.CreateBitCast(
            slot,
            llvm::PointerType::get(target_ty, 0));
        out = builder.CreateLoad(target_ty, cast_ptr);
      }
    }
  }

  if (!out)
  {
    out = llvm::Constant::getNullValue(target_ty);
  }
  emitter.SetTempValue(transmute.result, out);
}

} // namespace ultraviolet::codegen::emit_detail
