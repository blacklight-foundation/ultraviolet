// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/sync.cpp
// Canonical owner for LLVM IR sync instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRSync &s) const
{
  llvm::Value *initial_async = EvaluateOrDefault(s.async_value);
  if (!initial_async)
  {
    emitter.SetTempValue(s.result, DefaultFor(s.result));
    return;
  }

  llvm::Type *expected = ExpectedLLVMType(s.result);
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupValueType(s.result) : nullptr;
  if (!expected && target_type)
  {
    expected = emitter.GetLLVMType(target_type);
  }
  if (!expected && s.result_type)
  {
    expected = emitter.GetLLVMType(s.result_type);
  }

  auto *async_struct = llvm::dyn_cast<llvm::StructType>(initial_async->getType());
  if (!async_struct || async_struct->getNumElements() < 1 ||
      !async_struct->getElementType(0)->isIntegerTy())
  {
    llvm::Value *fallback = initial_async;
    if (expected)
    {
      if (target_type)
      {
        if (llvm::Value *coerced = CoerceToTyped(
                emitter,
                &builder,
                fallback,
                expected,
                s.async_type,
                target_type))
        {
          fallback = coerced;
        }
        else if (llvm::Value *plain = CoerceTo(&builder, fallback, expected))
        {
          fallback = plain;
        }
        else
        {
          fallback = llvm::Constant::getNullValue(expected);
        }
      }
      else if (llvm::Value *plain = CoerceTo(&builder, fallback, expected))
      {
        fallback = plain;
      }
      else
      {
        fallback = llvm::Constant::getNullValue(expected);
      }
    }
    if (!fallback)
    {
      fallback = DefaultFor(s.result);
    }
    emitter.SetTempValue(s.result, fallback);
    return;
  }

  llvm::Function *func =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!func)
  {
    emitter.SetTempValue(s.result, DefaultFor(s.result));
    return;
  }

  llvm::IRBuilder<> entry_builder(
      &func->getEntryBlock(),
      func->getEntryBlock().begin());
  llvm::AllocaInst *async_slot = entry_builder.CreateAlloca(async_struct);
  builder.CreateStore(initial_async, async_slot);

  llvm::AllocaInst *result_slot = nullptr;
  if (expected && !expected->isVoidTy())
  {
    result_slot = entry_builder.CreateAlloca(expected);
    builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  }

  auto materialize_as_type = [&](llvm::Value *value, llvm::Type *dst_ty) -> llvm::Value *
  {
    if (!value || !dst_ty)
    {
      return nullptr;
    }
    if (value->getType() == dst_ty)
    {
      return value;
    }
    if (llvm::Value *coerced = CoerceTo(&builder, value, dst_ty))
    {
      return coerced;
    }

    llvm::AllocaInst *dst_slot = entry_builder.CreateAlloca(dst_ty);
    builder.CreateStore(llvm::Constant::getNullValue(dst_ty), dst_slot);
    llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(value->getType());
    builder.CreateStore(value, src_slot);

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *dst_i8 = builder.CreateBitCast(dst_slot, llvm::PointerType::get(i8_ty, 0));
    llvm::Value *src_i8 = builder.CreateBitCast(src_slot, llvm::PointerType::get(i8_ty, 0));
    const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
    const std::uint64_t src_size =
        static_cast<std::uint64_t>(dl.getTypeAllocSize(value->getType()));
    const std::uint64_t dst_size =
        static_cast<std::uint64_t>(dl.getTypeAllocSize(dst_ty));
    const std::uint64_t copy_size = std::min(src_size, dst_size);
    if (copy_size > 0)
    {
      builder.CreateMemCpy(
          dst_i8,
          llvm::Align(1),
          src_i8,
          llvm::Align(1),
          llvm::ConstantInt::get(i64_ty, copy_size));
    }
    return builder.CreateLoad(dst_ty, dst_slot);
  };

  auto coerce_to_result = [&](llvm::Value *value,
                              const analysis::TypeRef &source_type) -> llvm::Value *
  {
    if (!expected || expected->isVoidTy())
    {
      return nullptr;
    }
    llvm::Value *out = value;
    if (!out)
    {
      return llvm::Constant::getNullValue(expected);
    }
    if (target_type)
    {
      if (llvm::Value *coerced = CoerceToTyped(
              emitter,
              &builder,
              out,
              expected,
              source_type,
              target_type))
      {
        return coerced;
      }
    }
    if (llvm::Value *coerced = CoerceTo(&builder, out, expected))
    {
      return coerced;
    }
    return materialize_as_type(out, expected);
  };

  auto extract_async_payload = [&](llvm::Value *async_value,
                                   const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    if (!async_value || !payload_type ||
        IsUnitTypeRef(payload_type) ||
        IsNeverTypeRef(payload_type))
    {
      return nullptr;
    }
    llvm::Type *payload_ll = emitter.GetLLVMType(payload_type);
    if (!payload_ll || payload_ll->isVoidTy())
    {
      return nullptr;
    }
    llvm::AllocaInst *payload_async_slot = entry_builder.CreateAlloca(async_struct);
    builder.CreateStore(async_value, payload_async_slot);
    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        async_struct,
        payload_async_slot,
        ::ultraviolet::analysis::layout::kPtrAlign);
    if (!payload_i8)
    {
      return nullptr;
    }
    llvm::Value *payload_ptr = builder.CreateBitCast(
        payload_i8,
        llvm::PointerType::get(payload_ll, 0));
    llvm::LoadInst *loaded = builder.CreateLoad(payload_ll, payload_ptr);
    loaded->setAlignment(llvm::Align(1));
    return loaded;
  };

  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  const AsyncStateDiscs async_discs =
      LoweredAsyncStateDiscs(scope, s.async_type);
  const std::uint64_t suspended_disc = async_discs.suspended;
  const std::uint64_t completed_disc = async_discs.completed;
  const std::optional<std::uint64_t> failed_disc = async_discs.failed;

  analysis::TypeRef completed_type = s.result_type;
  analysis::TypeRef error_type = s.error_type;
  if (const auto sig = analysis::AsyncSigOf(scope, s.async_type))
  {
    if (!completed_type)
    {
      completed_type = sig->result;
    }
    if (!error_type)
    {
      error_type = sig->err;
    }
  }

  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Type *opaque_ptr_ty = emitter.GetOpaquePtr();
  auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);

  llvm::Value *panic_ptr = LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
  bool has_panic_ptr = panic_ptr != nullptr;
  if (panic_ptr)
  {
    if (llvm::Value *coerced = CoerceTo(&builder, panic_ptr, opaque_ptr_ty))
    {
      panic_ptr = coerced;
    }
    else if (panic_ptr->getType()->isPointerTy())
    {
      panic_ptr = builder.CreateBitCast(panic_ptr, opaque_ptr_ty);
    }
    else
    {
      panic_ptr = nullptr;
      has_panic_ptr = false;
    }
  }
  if (!panic_ptr)
  {
    panic_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
  }

  llvm::BasicBlock *loop_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.loop", func);
  llvm::BasicBlock *suspended_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.suspended", func);
  llvm::BasicBlock *completed_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.completed", func);
  llvm::BasicBlock *failed_bb = failed_disc.has_value()
                                    ? llvm::BasicBlock::Create(
                                          emitter.GetContext(),
                                          "sync.failed",
                                          func)
                                    : nullptr;
  llvm::BasicBlock *fallback_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.fallback", func);
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.panic", func);
  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "sync.merge", func);

  builder.CreateBr(loop_bb);

  builder.SetInsertPoint(loop_bb);
  llvm::Value *current_async = builder.CreateLoad(async_struct, async_slot);
  llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
  auto *disc_ty = llvm::cast<llvm::IntegerType>(disc->getType());
  llvm::SwitchInst *state_sw = builder.CreateSwitch(
      disc, fallback_bb, failed_disc.has_value() ? 3 : 2);
  state_sw->addCase(llvm::ConstantInt::get(disc_ty, suspended_disc), suspended_bb);
  state_sw->addCase(llvm::ConstantInt::get(disc_ty, completed_disc), completed_bb);
  if (failed_disc.has_value())
  {
    state_sw->addCase(llvm::ConstantInt::get(disc_ty, *failed_disc), failed_bb);
  }

  builder.SetInsertPoint(suspended_bb);
  llvm::Value *suspended_ptr = builder.CreateBitCast(async_slot, opaque_ptr_ty);
  llvm::Value *unit_input = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
  llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
      emitter,
      &builder,
      suspended_ptr,
      unit_input,
      panic_ptr);
  llvm::Value *resumed_async = materialize_as_type(resume_call, async_struct);
  if (!resumed_async)
  {
    resumed_async = llvm::Constant::getNullValue(async_struct);
  }
  builder.CreateStore(resumed_async, async_slot);
  if (has_panic_ptr)
  {
    llvm::Value *panic_i8 = panic_ptr;
    if (panic_i8->getType() != llvm::PointerType::get(i8_ty, 0))
    {
      panic_i8 = builder.CreateBitCast(panic_i8, llvm::PointerType::get(i8_ty, 0));
    }
    llvm::LoadInst *panic_flag = builder.CreateLoad(i8_ty, panic_i8);
    llvm::Value *has_panic = builder.CreateICmpNE(
        panic_flag,
        llvm::ConstantInt::get(i8_ty, 0));
    builder.CreateCondBr(has_panic, panic_bb, loop_bb);
  }
  else
  {
    builder.CreateBr(loop_bb);
  }

  builder.SetInsertPoint(completed_bb);
  llvm::Value *completed_async = builder.CreateLoad(async_struct, async_slot);
  llvm::Value *completed_payload =
      extract_async_payload(completed_async, completed_type);
  llvm::Value *completed_result =
      coerce_to_result(completed_payload, completed_type);
  if (result_slot && !completed_result)
  {
    completed_result = llvm::Constant::getNullValue(expected);
  }
  if (result_slot && completed_result)
  {
    builder.CreateStore(completed_result, result_slot);
  }
  builder.CreateBr(merge_bb);

  if (failed_bb)
  {
    builder.SetInsertPoint(failed_bb);
    llvm::Value *failed_async = builder.CreateLoad(async_struct, async_slot);
    llvm::Value *failed_payload = extract_async_payload(failed_async, error_type);
    llvm::Value *failed_result = coerce_to_result(failed_payload, error_type);
    if (result_slot && !failed_result)
    {
      failed_result = llvm::Constant::getNullValue(expected);
    }
    if (result_slot && failed_result)
    {
      builder.CreateStore(failed_result, result_slot);
    }
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(fallback_bb);
  llvm::Value *fallback_async = builder.CreateLoad(async_struct, async_slot);
  llvm::Value *fallback_result = coerce_to_result(fallback_async, s.async_type);
  if (result_slot && !fallback_result)
  {
    fallback_result = llvm::Constant::getNullValue(expected);
  }
  if (result_slot && fallback_result)
  {
    builder.CreateStore(fallback_result, result_slot);
  }
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(panic_bb);
  if (result_slot)
  {
    builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  }
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(merge_bb);
  llvm::Value *out = nullptr;
  if (result_slot && expected && !expected->isVoidTy())
  {
    out = builder.CreateLoad(expected, result_slot);
  }
  if (!out)
  {
    out = DefaultFor(s.result);
  }
  emitter.SetTempValue(s.result, out);
}

} // namespace ultraviolet::codegen::emit_detail
