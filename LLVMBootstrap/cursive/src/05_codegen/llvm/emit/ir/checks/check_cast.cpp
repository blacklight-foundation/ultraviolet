// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_cast.cpp
// Canonical owner for LLVM IR cast check instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckCast &check) const
{
  llvm::Type *target_ty = emitter.GetLLVMType(check.target);
  llvm::Value *value = EvaluateOrDefault(check.value);
  if (!target_ty || !value || !target_ty->isIntegerTy() || !value->getType()->isIntegerTy())
  {
    return;
  }
  const unsigned src_bits = value->getType()->getIntegerBitWidth();
  const unsigned dst_bits = target_ty->getIntegerBitWidth();
  if (dst_bits >= src_bits)
  {
    return;
  }

  bool signed_src = false;
  if (analysis::TypeRef src_type = LookupValueType(check.value))
  {
    signed_src = IsSignedIntegerType(src_type);
  }
  llvm::Value *narrowed = builder.CreateIntCast(value, target_ty, signed_src);
  llvm::Value *widened = builder.CreateIntCast(narrowed, value->getType(), signed_src);
  llvm::Value *ok = builder.CreateICmpEQ(value, widened);
  EmitPanicReturnIfFalse(emitter, &builder, ok, PanicCode(PanicReason::Cast));
}

} // namespace cursive::codegen::emit_detail
