// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/yield.cpp
// Canonical owner for LLVM IR yield instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRYield &y) const
{
  AsyncEmitState *async_state = emitter.GetAsyncState();
  if (!async_state || !async_state->info)
  {
    emitter.SetTempValue(y.result, EvaluateOrDefault(y.result));
    return;
  }

  const LowerCtx::AsyncProcInfo &info = *async_state->info;
  llvm::Function *func =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!func)
  {
    emitter.SetTempValue(y.result, DefaultFor(y.result));
    return;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  llvm::BasicBlock *cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield.cont", func);

  auto load_resume_input = [&](llvm::IRBuilder<> &b) -> llvm::Value *
  {
    analysis::TypeRef input_type = info.in_type;
    analysis::TypeRef target_type =
        active_ctx ? active_ctx->LookupValueType(y.result) : nullptr;
    llvm::Type *expected = ExpectedLLVMType(y.result);

    if (!input_type || IsUnitTypeRef(input_type) || IsNeverTypeRef(input_type))
    {
      if (expected && !expected->isVoidTy())
      {
        return llvm::Constant::getNullValue(expected);
      }
      return DefaultFor(y.result);
    }

    llvm::Type *input_ll = emitter.GetLLVMType(input_type);
    if (!input_ll || input_ll->isVoidTy())
    {
      if (expected && !expected->isVoidTy())
      {
        return llvm::Constant::getNullValue(expected);
      }
      return DefaultFor(y.result);
    }

    llvm::Value *input_ptr = async_state->input_ptr;
    if (!input_ptr)
    {
      llvm::Value *fallback = llvm::Constant::getNullValue(input_ll);
      if (expected)
      {
        if (target_type)
        {
          if (llvm::Value *coerced = CoerceToTyped(
                  emitter,
                  &b,
                  fallback,
                  expected,
                  input_type,
                  target_type))
          {
            return coerced;
          }
        }
        if (llvm::Value *coerced = CoerceTo(&b, fallback, expected))
        {
          return coerced;
        }
        return llvm::Constant::getNullValue(expected);
      }
      return fallback;
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Value *input_i8 = input_ptr;
    if (input_i8->getType() != llvm::PointerType::get(i8_ty, 0))
    {
      input_i8 = b.CreateBitCast(input_i8, llvm::PointerType::get(i8_ty, 0));
    }
    llvm::Value *typed_input_ptr =
        b.CreateBitCast(input_i8, llvm::PointerType::get(input_ll, 0));
    llvm::LoadInst *loaded = b.CreateLoad(input_ll, typed_input_ptr);
    loaded->setAlignment(llvm::Align(1));
    llvm::Value *input_value = loaded;

    if (expected)
    {
      if (target_type)
      {
        if (llvm::Value *coerced = CoerceToTyped(
                emitter,
                &b,
                input_value,
                expected,
                input_type,
                target_type))
        {
          input_value = coerced;
        }
        else if (llvm::Value *coerced_plain =
                     CoerceTo(&b, input_value, expected))
        {
          input_value = coerced_plain;
        }
        else
        {
          input_value = llvm::Constant::getNullValue(expected);
        }
      }
      else if (llvm::Value *coerced = CoerceTo(&b, input_value, expected))
      {
        input_value = coerced;
      }
      else
      {
        input_value = llvm::Constant::getNullValue(expected);
      }
    }

    return input_value;
  };

  if (info.is_resume && async_state->resume_switch)
  {
    if (!async_state->resume_blocks.contains(y.state_index))
    {
      llvm::BasicBlock *resume_bb = llvm::BasicBlock::Create(
          emitter.GetContext(),
          "yield.resume." + std::to_string(y.state_index),
          func);
      async_state->resume_blocks[y.state_index] = resume_bb;
      if (auto *disc_ty = llvm::dyn_cast<llvm::IntegerType>(
              async_state->resume_switch->getCondition()->getType()))
      {
        async_state->resume_switch->addCase(
            llvm::ConstantInt::get(disc_ty, y.state_index),
            resume_bb);
      }

      llvm::IRBuilder<> resume_builder(resume_bb);
      resume_builder.CreateBr(cont_bb);
    }
  }

  auto ensure_async_frame = [&]() -> llvm::Value *
  {
    if (async_state->frame_ptr)
    {
      return async_state->frame_ptr;
    }
    if (!info.is_wrapper)
    {
      return nullptr;
    }

    const std::string alloc_sym = BuiltinSymAsyncAllocFrame();
    llvm::Function *alloc_fn = emitter.GetModule().getFunction(alloc_sym);
    if (!alloc_fn)
    {
      llvm::FunctionType *alloc_ty = llvm::FunctionType::get(
          emitter.GetOpaquePtr(),
          {llvm::Type::getInt64Ty(emitter.GetContext()),
           llvm::Type::getInt64Ty(emitter.GetContext())},
          false);
      alloc_fn = llvm::Function::Create(
          alloc_ty,
          llvm::GlobalValue::ExternalLinkage,
          alloc_sym,
          &emitter.GetModule());
    }
    if (!alloc_fn)
    {
      return nullptr;
    }

    llvm::Value *frame_raw = builder.CreateCall(
        alloc_fn,
        {llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                info.frame_size),
         llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                std::max<std::uint64_t>(1, info.frame_align))});
    async_state->frame_ptr = CoerceTo(&builder, frame_raw, emitter.GetOpaquePtr());
    if (!async_state->frame_ptr)
    {
      async_state->frame_ptr = frame_raw;
    }
    if (!async_state->frame_ptr)
    {
      return nullptr;
    }

    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *resume_state_ptr = AsyncFrameTypedPtr(
        emitter,
        &builder,
        async_state->frame_ptr,
        kAsyncFrameResumeStateOffset,
        i64_ty);
    if (resume_state_ptr)
    {
      builder.CreateStore(llvm::ConstantInt::get(i64_ty, 0), resume_state_ptr);
    }

    llvm::Value *resume_fn_ptr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(emitter.GetOpaquePtr()));
    if (llvm::Function *resume_fn = emitter.GetFunction(info.resume_symbol))
    {
      if (llvm::Value *coerced = CoerceTo(&builder, resume_fn, emitter.GetOpaquePtr()))
      {
        resume_fn_ptr = coerced;
      }
    }
    llvm::Value *resume_fn_slot = AsyncFrameTypedPtr(
        emitter,
        &builder,
        async_state->frame_ptr,
        kAsyncFrameResumeFnOffset,
        emitter.GetOpaquePtr());
    if (resume_fn_slot)
    {
      builder.CreateStore(resume_fn_ptr, resume_fn_slot);
    }

    StoreAsyncFrameHostedEnv(
        emitter,
        &builder,
        async_state->frame_ptr,
        emitter.GetHostedCurrentEnvPtr());
    StoreAsyncFrameKeySnapshot(
        emitter,
        &builder,
        async_state->frame_ptr,
        NullOpaquePtr(emitter));

    return async_state->frame_ptr;
  };

  auto snapshot_async_slots = [&]()
  {
    if (!async_state->frame_ptr)
    {
      return;
    }
    for (const auto &slot_name : info.slot_order)
    {
      const auto slot_it = info.slots.find(slot_name);
      if (slot_it == info.slots.end())
      {
        continue;
      }
      const auto &slot = slot_it->second;
      llvm::Type *slot_ty = emitter.GetLLVMType(slot.type);
      if (!slot_ty || slot_ty->isVoidTy())
      {
        continue;
      }

      llvm::Value *local_value = LoadLocalValue(emitter, &builder, slot_name);
      if (!local_value)
      {
        continue;
      }

      analysis::TypeRef source_type = emitter.LookupLocalType(slot_name);
      llvm::Value *stored_value = local_value;
      if (stored_value->getType() != slot_ty)
      {
        if (llvm::Value *coerced = CoerceToTyped(
                emitter,
                &builder,
                stored_value,
                slot_ty,
                source_type,
                slot.type))
        {
          stored_value = coerced;
        }
        else if (llvm::Value *coerced_plain =
                     CoerceTo(&builder, stored_value, slot_ty))
        {
          stored_value = coerced_plain;
        }
        else
        {
          stored_value = llvm::Constant::getNullValue(slot_ty);
        }
      }

      llvm::Value *frame_slot_ptr = AsyncFrameTypedPtr(
          emitter,
          &builder,
          async_state->frame_ptr,
          slot.offset,
          slot_ty);
      if (!frame_slot_ptr)
      {
        continue;
      }
      llvm::StoreInst *st = builder.CreateStore(stored_value, frame_slot_ptr);
      st->setAlignment(llvm::Align(std::max<std::uint64_t>(1, slot.align)));
    }
  };

  llvm::Value *yielded_value = EvaluateOrDefault(y.value);
  llvm::Value *frame_ptr = ensure_async_frame();
  if (y.release && frame_ptr)
  {
    llvm::Value *released = EmitKeyReleaseAll(emitter, &builder);
    StoreAsyncFrameKeySnapshot(emitter, &builder, frame_ptr, released);
  }
  if (frame_ptr)
  {
    snapshot_async_slots();
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *state_ptr = AsyncFrameTypedPtr(
        emitter,
        &builder,
        frame_ptr,
        kAsyncFrameResumeStateOffset,
        i64_ty);
    if (state_ptr)
    {
      builder.CreateStore(
          llvm::ConstantInt::get(i64_ty, y.state_index),
          state_ptr);
    }
  }

  analysis::TypeRef async_type = info.async_type;
  llvm::Type *async_layout_ty = async_type ? emitter.GetLLVMType(async_type) : nullptr;
  auto *async_struct = llvm::dyn_cast_or_null<llvm::StructType>(async_layout_ty);
  llvm::Value *suspended_value = nullptr;
  if (async_struct && async_struct->getNumElements() >= 1 &&
      async_struct->getElementType(0)->isIntegerTy())
  {
    llvm::IRBuilder<> entry_builder(
        &func->getEntryBlock(),
        func->getEntryBlock().begin());
    llvm::AllocaInst *async_slot = entry_builder.CreateAlloca(async_struct);
    builder.CreateStore(llvm::Constant::getNullValue(async_struct), async_slot);

    llvm::Type *disc_ty = async_struct->getElementType(0);
    const AsyncStateDiscs async_discs =
        LoweredAsyncStateDiscs(scope, async_type);
    const std::uint64_t suspended_disc = async_discs.suspended;
    llvm::Value *disc_ptr = builder.CreateStructGEP(async_struct, async_slot, 0);
    builder.CreateStore(
        llvm::ConstantInt::get(disc_ty, suspended_disc),
        disc_ptr);

    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        async_struct,
        async_slot,
        ::ultraviolet::analysis::layout::kPtrAlign);

    if (payload_i8 &&
        info.out_type &&
        !IsUnitTypeRef(info.out_type) &&
        !IsNeverTypeRef(info.out_type))
    {
      llvm::Type *out_ll = emitter.GetLLVMType(info.out_type);
      if (out_ll && !out_ll->isVoidTy())
      {
        llvm::Value *out_value = yielded_value;
        if (out_value->getType() != out_ll)
        {
          if (llvm::Value *coerced = CoerceToTyped(
                  emitter,
                  &builder,
                  out_value,
                  out_ll,
                  active_ctx ? active_ctx->LookupValueType(y.value) : nullptr,
                  info.out_type))
          {
            out_value = coerced;
          }
          else if (llvm::Value *coerced_plain =
                       CoerceTo(&builder, out_value, out_ll))
          {
            out_value = coerced_plain;
          }
          else
          {
            out_value = llvm::Constant::getNullValue(out_ll);
          }
        }

        llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(out_ll);
        builder.CreateStore(out_value, src_slot);
        llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
        llvm::Value *src_i8 = builder.CreateBitCast(
            src_slot,
            llvm::PointerType::get(i8_ty, 0));
        const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
        const std::uint64_t copy_size =
            static_cast<std::uint64_t>(dl.getTypeAllocSize(out_ll));
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

    if (payload_i8 && frame_ptr)
    {
      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *frame_slot_i8 = builder.CreateGEP(
          i8_ty,
          payload_i8,
          llvm::ConstantInt::get(i64_ty, kAsyncPayloadFramePtrOffset));
      llvm::Value *frame_slot = builder.CreateBitCast(
          frame_slot_i8,
          llvm::PointerType::get(emitter.GetOpaquePtr(), 0));
      llvm::Value *frame_store = CoerceTo(&builder, frame_ptr, emitter.GetOpaquePtr());
      if (!frame_store)
      {
        frame_store = builder.CreateBitCast(frame_ptr, emitter.GetOpaquePtr());
      }
      if (!frame_store)
      {
        frame_store = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(emitter.GetOpaquePtr()));
      }
      builder.CreateStore(frame_store, frame_slot);
    }

    suspended_value = builder.CreateLoad(async_struct, async_slot);
  }

  if (!suspended_value)
  {
    suspended_value = llvm::Constant::getNullValue(
        async_layout_ty ? async_layout_ty
                        : llvm::Type::getInt64Ty(emitter.GetContext()));
  }

  llvm::Type *ret_ty = func->getReturnType();
  const std::string sym = std::string(func->getName());
  const LowerCtx::ProcSigInfo *sig =
      active_ctx ? active_ctx->LookupProcSig(sym) : nullptr;

  if (ret_ty->isVoidTy())
  {
    (void)StoreProcedureOutValue(
        emitter,
        &builder,
        func,
        sym,
        sig,
        suspended_value,
        info.async_type);
    builder.CreateRetVoid();
  }
  else
  {
    llvm::Value *out = CoerceToTyped(
        emitter,
        &builder,
        suspended_value,
        ret_ty,
        info.async_type,
        sig ? sig->ret : nullptr);
    if (!out)
    {
      out = CoerceTo(&builder, suspended_value, ret_ty);
    }
    if (!out)
    {
      out = llvm::Constant::getNullValue(ret_ty);
    }
    builder.CreateRet(out);
  }

  builder.SetInsertPoint(cont_bb);
  if (info.is_resume && async_state->resume_switch)
  {
    async_state->emitting_resume_prelude = false;
    if (y.release && async_state->frame_ptr)
    {
      llvm::Value *released =
          LoadAsyncFrameKeySnapshot(emitter, &builder, async_state->frame_ptr);
      EmitKeyReacquire(emitter, &builder, released);
      StoreAsyncFrameKeySnapshot(
          emitter,
          &builder,
          async_state->frame_ptr,
          NullOpaquePtr(emitter));
    }
    llvm::Value *resume_input = load_resume_input(builder);
    if (!resume_input)
    {
      resume_input = DefaultFor(y.result);
    }
    llvm::Type *resume_type = resume_input ? resume_input->getType() : nullptr;
    if (resume_type && !resume_type->isVoidTy())
    {
      llvm::IRBuilder<> entry_builder(
          &func->getEntryBlock(),
          func->getEntryBlock().begin());
      llvm::AllocaInst *resume_slot =
          entry_builder.CreateAlloca(resume_type, nullptr, "yield_input");
      builder.CreateStore(resume_input, resume_slot);
      emitter.SetTempStorage(y.result, resume_slot);
    }
    else
    {
      emitter.SetTempValue(y.result, resume_input);
    }
  }
  if (!emitter.GetTempValue(y.result) && !emitter.GetTempStorage(y.result))
  {
    emitter.SetTempValue(y.result, DefaultFor(y.result));
  }
}

} // namespace ultraviolet::codegen::emit_detail
