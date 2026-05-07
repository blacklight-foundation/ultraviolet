// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/async_fail.cpp
// Canonical owner for LLVM IR async failure instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRAsyncFail &async_fail) const
{
  llvm::Value *wrapped_value = EvaluateOrDefault(async_fail.value);
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  analysis::TypeRef async_type = async_fail.async_type;
  if (!async_type && active_ctx)
  {
    async_type = active_ctx->LookupValueType(async_fail.result);
  }
  analysis::TypeRef source_type =
      active_ctx ? active_ctx->LookupValueType(async_fail.value) : nullptr;
  analysis::TypeRef error_type = async_fail.error_type;
  if (!error_type)
  {
    if (const auto sig = analysis::GetAsyncSig(async_type))
    {
      error_type = sig->err;
    }
  }

  llvm::Type *async_layout_ty = async_type ? emitter.GetLLVMType(async_type) : nullptr;
  llvm::Type *expected_async_ty = ExpectedLLVMType(async_fail.result);
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
      const auto lowered_async = ::cursive::analysis::layout::LowerAsyncType(async_type);
      const AsyncStateDiscs async_discs =
          LoweredAsyncStateDiscs(scope, lowered_async);
      if (!async_discs.failed.has_value())
      {
        // Infallible asyncs have no concrete Failed arm. This path should
        // already be unreachable after typing, so preserve the zero
        // initialized value instead of fabricating a failed discriminator.
        emitter.SetTempValue(async_fail.result,
                             llvm::Constant::getNullValue(async_struct));
        return;
      }
      const std::uint64_t failed_disc = *async_discs.failed;
      llvm::Value *disc_ptr = builder.CreateStructGEP(async_struct, async_slot, 0);
      llvm::Value *disc_val = llvm::ConstantInt::get(disc_ty, failed_disc);
      builder.CreateStore(disc_val, disc_ptr);

      if (error_type &&
          !IsUnitTypeRef(error_type) &&
          !IsNeverTypeRef(error_type))
      {
        llvm::Type *error_ll = emitter.GetLLVMType(error_type);
        if (error_ll && !error_ll->isVoidTy())
        {
          llvm::Value *payload_value = wrapped_value;
          if (!payload_value)
          {
            payload_value = llvm::Constant::getNullValue(error_ll);
          }
          else if (payload_value->getType() != error_ll)
          {
            if (llvm::Value *coerced = CoerceToTyped(
                    emitter,
                    &builder,
                    payload_value,
                    error_ll,
                    source_type,
                    error_type))
            {
              payload_value = coerced;
            }
            else if (llvm::Value *coerced_plain =
                         CoerceTo(&builder, payload_value, error_ll))
            {
              payload_value = coerced_plain;
            }
            else
            {
              payload_value = llvm::Constant::getNullValue(error_ll);
            }
          }

          llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
              emitter,
              &builder,
              async_struct,
              async_slot,
              ::cursive::analysis::layout::kPtrAlign);
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
      emitter.SetTempValue(async_fail.result, packed);
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
    wrapped_value = DefaultFor(async_fail.result);
  }
  emitter.SetTempValue(async_fail.result, wrapped_value);
}

} // namespace cursive::codegen::emit_detail
