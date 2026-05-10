// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/break_continue.cpp
// Canonical owner for LLVM IR break and continue instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRBreak &brk) const
{
  llvm::Value *break_value_slot = emitter.CurrentLoopBreakValueSlot();
  analysis::TypeRef break_result_type = emitter.CurrentLoopBreakResultType();
  if (break_value_slot)
  {
    llvm::Type *target_ty = nullptr;
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(break_value_slot))
    {
      target_ty = alloca->getAllocatedType();
    }
    if (!target_ty && break_result_type)
    {
      target_ty = emitter.GetLLVMType(break_result_type);
    }
    if (target_ty && !target_ty->isVoidTy())
    {
      analysis::TypeRef source_type = analysis::MakeTypePrim("()");
      llvm::Value *value = nullptr;
      if (brk.value.has_value())
      {
        value = EvaluateOrDefault(*brk.value);
        if (const LowerCtx *ctx = emitter.GetCurrentCtx())
        {
          source_type = ctx->LookupValueType(*brk.value);
        }
        if (!source_type)
        {
          source_type = analysis::MakeTypePrim("()");
        }
      }
      else
      {
        if (llvm::Type *unit_ty = emitter.GetLLVMType(analysis::MakeTypePrim("()")))
        {
          value = llvm::Constant::getNullValue(unit_ty);
        }
      }

      if (!value)
      {
        value = llvm::Constant::getNullValue(target_ty);
      }

      if (break_result_type)
      {
        llvm::Value *coerced = CoerceToTyped(
            emitter,
            &builder,
            value,
            target_ty,
            source_type,
            break_result_type);
        value = coerced ? coerced : llvm::Constant::getNullValue(target_ty);
      }
      else if (value->getType() != target_ty)
      {
        llvm::Value *coerced = CoerceTo(&builder, value, target_ty);
        value = coerced ? coerced : llvm::Constant::getNullValue(target_ty);
      }

      builder.CreateStore(value, break_value_slot);
    }
  }

  if (llvm::BasicBlock *target = emitter.CurrentLoopBreakTarget())
  {
    builder.CreateBr(target);
  }
}

void IRInstructionVisitor::operator()(const IRContinue &) const
{
  if (llvm::BasicBlock *target = emitter.CurrentLoopContinueTarget())
  {
    builder.CreateBr(target);
  }
}

} // namespace cursive::codegen::emit_detail
