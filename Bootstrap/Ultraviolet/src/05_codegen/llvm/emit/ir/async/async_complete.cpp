// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/async_complete.cpp
// Canonical owner for LLVM IR async completion instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRAsyncComplete &async_complete) const
{
  llvm::Value *wrapped_value = EvaluateOrDefault(async_complete.value);
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  analysis::TypeRef async_type = async_complete.async_type;
  if (!async_type && active_ctx)
  {
    async_type = active_ctx->LookupValueType(async_complete.result);
  }
  analysis::TypeRef source_type =
      active_ctx ? active_ctx->LookupValueType(async_complete.value) : nullptr;
  analysis::TypeRef completed_type = async_complete.result_type;
  if (!completed_type)
  {
    if (const auto sig = analysis::GetAsyncSig(async_type))
    {
      completed_type = sig->result;
    }
  }

  llvm::Type *async_layout_ty = async_type ? emitter.GetLLVMType(async_type) : nullptr;
  llvm::Type *expected_async_ty = ExpectedLLVMType(async_complete.result);
  if (!expected_async_ty)
  {
    expected_async_ty = async_layout_ty;
  }

  llvm::Type *pack_target_ty = async_layout_ty ? async_layout_ty : expected_async_ty;
  if (auto *async_struct = llvm::dyn_cast_or_null<llvm::StructType>(pack_target_ty);
      async_struct && async_struct->getNumElements() >= 1 &&
      async_struct->getElementType(0)->isIntegerTy())
  {
    llvm::Function *current_fn =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (current_fn)
    {
      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *async_slot = entry_builder.CreateAlloca(async_struct);
      builder.CreateStore(llvm::Constant::getNullValue(async_struct), async_slot);

      llvm::Type *disc_ty = async_struct->getElementType(0);
      const AsyncStateDiscs async_discs =
          LoweredAsyncStateDiscs(scope, async_type);
      const std::uint64_t completed_disc = async_discs.completed;
      llvm::Value *disc_ptr = builder.CreateStructGEP(async_struct, async_slot, 0);
      llvm::Value *disc_val = llvm::ConstantInt::get(disc_ty, completed_disc);
      builder.CreateStore(disc_val, disc_ptr);

      if (completed_type &&
          !IsUnitTypeRef(completed_type) &&
          !IsNeverTypeRef(completed_type))
      {
        llvm::Type *completed_ll = emitter.GetLLVMType(completed_type);
        if (completed_ll && !completed_ll->isVoidTy())
        {
          llvm::Value *payload_value = wrapped_value;
          if (!payload_value)
          {
            payload_value = llvm::Constant::getNullValue(completed_ll);
          }
          else if (payload_value->getType() != completed_ll)
          {
            if (llvm::Value *coerced = CoerceToTyped(
                    emitter,
                    &builder,
                    payload_value,
                    completed_ll,
                    source_type,
                    completed_type))
            {
              payload_value = coerced;
            }
            else if (llvm::Value *coerced_plain =
                         CoerceTo(&builder, payload_value, completed_ll))
            {
              payload_value = coerced_plain;
            }
            else
            {
              payload_value = llvm::Constant::getNullValue(completed_ll);
            }
          }

          llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
              emitter,
              &builder,
              async_struct,
              async_slot,
              ::ultraviolet::analysis::layout::kPtrAlign);
          if (payload_i8)
          {
            llvm::AllocaInst *src_slot =
                entry_builder.CreateAlloca(payload_value->getType());
            builder.CreateStore(payload_value, src_slot);

            llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
            llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
            llvm::Value *src_i8 = builder.CreateBitCast(
                src_slot,
                llvm::PointerType::get(i8_ty, 0));
            const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
            const std::uint64_t copy_size = static_cast<std::uint64_t>(
                dl.getTypeAllocSize(payload_value->getType()));
            if (copy_size > 0)
            {
              builder.CreateMemCpy(
                  payload_i8,
                  llvm::Align(1),
                  src_i8,
                  llvm::Align(1),
                  llvm::ConstantInt::get(i64_ty, copy_size));
            }
          }
        }
      }

      llvm::Value *packed = builder.CreateLoad(async_struct, async_slot);
      if (expected_async_ty && packed->getType() != expected_async_ty)
      {
        if (llvm::Value *coerced = CoerceTo(&builder, packed, expected_async_ty))
        {
          packed = coerced;
        }
      }
      emitter.SetTempValue(async_complete.result, packed);
      return;
    }
  }

  if (expected_async_ty)
  {
    if (llvm::Value *coerced = CoerceTo(&builder, wrapped_value, expected_async_ty))
    {
      wrapped_value = coerced;
    }
  }
  if (!wrapped_value)
  {
    wrapped_value = DefaultFor(async_complete.result);
  }
  emitter.SetTempValue(async_complete.result, wrapped_value);
}

} // namespace ultraviolet::codegen::emit_detail
