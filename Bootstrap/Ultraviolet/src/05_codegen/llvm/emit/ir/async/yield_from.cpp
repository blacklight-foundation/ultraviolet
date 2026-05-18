// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/yield_from.cpp
// Canonical owner for LLVM IR yield-from instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRYieldFrom &y) const
{
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef async_type = y.source_type;
  if (!async_type && active_ctx)
  {
    async_type = active_ctx->LookupValueType(y.source);
  }
  const analysis::ScopeContext &scope = BuildScope(active_ctx);

  llvm::Type *expected = ExpectedLLVMType(y.result);
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupValueType(y.result) : nullptr;
  if (!expected && target_type)
  {
    expected = emitter.GetLLVMType(target_type);
  }

  auto fallback_result = [&](llvm::Value *value,
                             const analysis::TypeRef &source_type) -> llvm::Value *
  {
    llvm::Value *out = value;
    if (!out && expected && !expected->isVoidTy())
    {
      out = llvm::Constant::getNullValue(expected);
    }
    if (expected)
    {
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
          out = coerced;
        }
        else if (llvm::Value *plain = CoerceTo(&builder, out, expected))
        {
          out = plain;
        }
        else
        {
          out = llvm::Constant::getNullValue(expected);
        }
      }
      else if (llvm::Value *plain = CoerceTo(&builder, out, expected))
      {
        out = plain;
      }
      else
      {
        out = llvm::Constant::getNullValue(expected);
      }
    }
    if (!out)
    {
      out = DefaultFor(y.result);
    }
    return out;
  };

  const auto source_sig = analysis::AsyncSigOf(scope, async_type);
  if (!source_sig)
  {
    emitter.SetTempValue(
        y.result,
        fallback_result(EvaluateOrDefault(y.source), async_type));
    return;
  }

  // The yield-from expression result is the delegated async result type.
  // If value-type metadata for the synthetic temp is missing, recover it
  // from the source async signature instead of defaulting to zero/null.
  if (!target_type)
  {
    target_type = source_sig->result;
  }
  if (!expected && target_type)
  {
    expected = emitter.GetLLVMType(target_type);
  }

  AsyncEmitState *async_state = emitter.GetAsyncState();
  if (!async_state || !async_state->info)
  {
    emitter.SetTempValue(
        y.result,
        fallback_result(EvaluateOrDefault(y.source), async_type));
    return;
  }

  llvm::Function *func =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!func)
  {
    emitter.SetTempValue(y.result, DefaultFor(y.result));
    return;
  }

  llvm::Type *async_layout_ty = emitter.GetLLVMType(async_type);
  auto *async_struct = llvm::dyn_cast_or_null<llvm::StructType>(async_layout_ty);
  if (!async_struct || async_struct->getNumElements() < 1 ||
      !async_struct->getElementType(0)->isIntegerTy())
  {
    emitter.SetTempValue(
        y.result,
        fallback_result(EvaluateOrDefault(y.source), async_type));
    return;
  }

  const LowerCtx::AsyncProcInfo &info = *async_state->info;
  const AsyncStateDiscs source_discs =
      LoweredAsyncStateDiscs(scope, *source_sig);
  const std::uint64_t suspended_disc = source_discs.suspended;
  const std::uint64_t completed_disc = source_discs.completed;
  const std::optional<std::uint64_t> failed_disc = source_discs.failed;
  const AsyncStateDiscs outer_discs =
      LoweredAsyncStateDiscs(scope, info.async_type);

  const std::string sym = std::string(func->getName());
  const LowerCtx::ProcSigInfo *proc_sig =
      active_ctx ? active_ctx->LookupProcSig(sym) : nullptr;

  llvm::IRBuilder<> entry_builder(
      &func->getEntryBlock(),
      func->getEntryBlock().begin());

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
    llvm::Type *dst_ptr_ty = llvm::PointerType::get(dst_ty, 0);
    if (value->getType() == dst_ptr_ty)
    {
      return builder.CreateLoad(dst_ty, value);
    }
    if (value->getType()->isPointerTy())
    {
      llvm::Value *typed_ptr = CoerceTo(&builder, value, dst_ptr_ty);
      if (!typed_ptr)
      {
        typed_ptr = builder.CreateBitCast(value, dst_ptr_ty);
      }
      if (typed_ptr)
      {
        return builder.CreateLoad(dst_ty, typed_ptr);
      }
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

  std::string source_slot_name;
  if (y.source.kind == IRValue::Kind::Local)
  {
    source_slot_name = y.source.name;
    if (!source_slot_name.empty() &&
        !info.slots.contains(source_slot_name))
    {
      std::string best_match;
      for (const auto &[slot_name, slot_info] : info.slots)
      {
        (void)slot_info;
        if (slot_name.empty())
        {
          continue;
        }
        const bool prefix_match =
            source_slot_name.rfind(slot_name, 0) == 0 ||
            slot_name.rfind(source_slot_name, 0) == 0;
        if (!prefix_match)
        {
          continue;
        }
        if (slot_name.size() > best_match.size())
        {
          best_match = slot_name;
        }
      }
      if (!best_match.empty())
      {
        source_slot_name = best_match;
      }
    }
  }

  llvm::Value *source_slot = nullptr;
  if (!source_slot_name.empty())
  {
    source_slot = emitter.GetLocal(source_slot_name);
  }
  if (!source_slot || !source_slot->getType()->isPointerTy())
  {
    source_slot = entry_builder.CreateAlloca(async_struct);
  }
  llvm::Type *async_ptr_ty = llvm::PointerType::get(async_struct, 0);
  if (source_slot->getType() != async_ptr_ty)
  {
    source_slot = builder.CreateBitCast(source_slot, async_ptr_ty);
  }

  auto extract_async_payload = [&](const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    if (!payload_type ||
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
    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        async_struct,
        source_slot,
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

  auto emit_async_return = [&](llvm::Value *value,
                               const analysis::TypeRef &source_type)
  {
    llvm::Type *ret_ty = func->getReturnType();
    if (ret_ty->isVoidTy())
    {
      (void)StoreProcedureOutValue(
          emitter,
          &builder,
          func,
          sym,
          proc_sig,
          value,
          source_type);
      builder.CreateRetVoid();
      return;
    }

    llvm::Value *out = CoerceToTyped(
        emitter,
        &builder,
        value,
        ret_ty,
        source_type,
        proc_sig ? proc_sig->ret : nullptr);
    if (!out)
    {
      out = CoerceTo(&builder, value, ret_ty);
    }
    if (!out)
    {
      out = llvm::Constant::getNullValue(ret_ty);
    }
    builder.CreateRet(out);
  };

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

  auto emit_outer_suspended = [&](llvm::Value *yielded_value,
                                  const analysis::TypeRef &yielded_type)
  {
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

    analysis::TypeRef outer_async_type = info.async_type;
    llvm::Type *outer_layout_ty =
        outer_async_type ? emitter.GetLLVMType(outer_async_type) : nullptr;
    auto *outer_struct = llvm::dyn_cast_or_null<llvm::StructType>(outer_layout_ty);
    llvm::Value *suspended_value = nullptr;
    if (outer_struct && outer_struct->getNumElements() >= 1 &&
        outer_struct->getElementType(0)->isIntegerTy())
    {
      llvm::AllocaInst *outer_slot = entry_builder.CreateAlloca(outer_struct);
      builder.CreateStore(llvm::Constant::getNullValue(outer_struct), outer_slot);

      llvm::Type *disc_ty = outer_struct->getElementType(0);
      llvm::Value *disc_ptr = builder.CreateStructGEP(outer_struct, outer_slot, 0);
      builder.CreateStore(
          llvm::ConstantInt::get(disc_ty, outer_discs.suspended),
          disc_ptr);

      llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          &builder,
          outer_struct,
          outer_slot,
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
          if (!out_value)
          {
            out_value = llvm::Constant::getNullValue(out_ll);
          }
          else if (out_value->getType() != out_ll)
          {
            if (llvm::Value *coerced = CoerceToTyped(
                    emitter,
                    &builder,
                    out_value,
                    out_ll,
                    yielded_type,
                    info.out_type))
            {
              out_value = coerced;
            }
            else if (llvm::Value *plain = CoerceTo(&builder, out_value, out_ll))
            {
              out_value = plain;
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

      suspended_value = builder.CreateLoad(outer_struct, outer_slot);
    }

    if (!suspended_value)
    {
      suspended_value = llvm::Constant::getNullValue(
          outer_layout_ty ? outer_layout_ty
                          : llvm::Type::getInt64Ty(emitter.GetContext()));
    }
    emit_async_return(suspended_value, info.async_type);
  };

  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *opaque_ptr_ty = emitter.GetOpaquePtr();
  auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);

  llvm::BasicBlock *loop_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.loop", func);
  llvm::BasicBlock *suspended_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.suspended", func);
  llvm::BasicBlock *completed_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.completed", func);
  llvm::BasicBlock *failed_bb = failed_disc.has_value()
                                    ? llvm::BasicBlock::Create(
                                          emitter.GetContext(),
                                          "yield_from.failed",
                                          func)
                                    : nullptr;
  llvm::BasicBlock *fallback_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.fallback", func);
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.panic", func);
  llvm::BasicBlock *cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "yield_from.cont", func);
  llvm::AllocaInst *result_slot = nullptr;
  if (expected && !expected->isVoidTy())
  {
    result_slot = entry_builder.CreateAlloca(expected);
    builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  }

  llvm::BasicBlock *resume_entry_bb = nullptr;
  bool emit_resume_body = false;
  if (info.is_resume && async_state->resume_switch)
  {
    auto it = async_state->resume_blocks.find(y.state_index);
    if (it == async_state->resume_blocks.end())
    {
      resume_entry_bb = llvm::BasicBlock::Create(
          emitter.GetContext(),
          "yield_from.resume." + std::to_string(y.state_index),
          func);
      async_state->resume_blocks[y.state_index] = resume_entry_bb;
      if (auto *disc_ty = llvm::dyn_cast<llvm::IntegerType>(
              async_state->resume_switch->getCondition()->getType()))
      {
        async_state->resume_switch->addCase(
            llvm::ConstantInt::get(disc_ty, y.state_index),
            resume_entry_bb);
      }
      emit_resume_body = true;
    }
    else
    {
      resume_entry_bb = it->second;
    }
  }

  llvm::Value *delegated_async = EvaluateOrDefault(y.source);
  llvm::Value *initial_async = materialize_as_type(delegated_async, async_struct);
  if (!initial_async)
  {
    initial_async = llvm::Constant::getNullValue(async_struct);
  }
  builder.CreateStore(initial_async, source_slot);
  builder.CreateBr(loop_bb);

  if (emit_resume_body && resume_entry_bb)
  {
    builder.SetInsertPoint(resume_entry_bb);
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
    llvm::Value *suspended_ptr = builder.CreateBitCast(source_slot, opaque_ptr_ty);
    llvm::Value *resume_input_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
    if (source_sig->in &&
        !IsUnitTypeRef(source_sig->in) &&
        !IsNeverTypeRef(source_sig->in))
    {
      resume_input_ptr = async_state->input_ptr;
      if (resume_input_ptr)
      {
        if (llvm::Value *coerced = CoerceTo(&builder, resume_input_ptr, opaque_ptr_ty))
        {
          resume_input_ptr = coerced;
        }
        else if (resume_input_ptr->getType()->isPointerTy())
        {
          resume_input_ptr = builder.CreateBitCast(resume_input_ptr, opaque_ptr_ty);
        }
        else
        {
          resume_input_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
        }
      }
      else
      {
        resume_input_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }
    }

    llvm::Value *resume_panic_ptr =
        LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
    bool resume_has_panic_ptr = resume_panic_ptr != nullptr;
    if (resume_panic_ptr)
    {
      if (llvm::Value *coerced = CoerceTo(&builder, resume_panic_ptr, opaque_ptr_ty))
      {
        resume_panic_ptr = coerced;
      }
      else if (resume_panic_ptr->getType()->isPointerTy())
      {
        resume_panic_ptr = builder.CreateBitCast(resume_panic_ptr, opaque_ptr_ty);
      }
      else
      {
        resume_panic_ptr = nullptr;
        resume_has_panic_ptr = false;
      }
    }
    if (!resume_panic_ptr)
    {
      resume_panic_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
    }

    llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
        emitter,
        &builder,
        suspended_ptr,
        resume_input_ptr,
        resume_panic_ptr);
    llvm::Value *resumed_async = materialize_as_type(resume_call, async_struct);
    if (!resumed_async)
    {
      resumed_async = llvm::Constant::getNullValue(async_struct);
    }
    builder.CreateStore(resumed_async, source_slot);
    if (resume_has_panic_ptr)
    {
      llvm::Value *panic_i8 = resume_panic_ptr;
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
  }

  builder.SetInsertPoint(loop_bb);
  llvm::Value *current_async = builder.CreateLoad(async_struct, source_slot);
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
  llvm::Value *yielded_value = extract_async_payload(source_sig->out);
  emit_outer_suspended(yielded_value, source_sig->out);
  if (!builder.GetInsertBlock()->getTerminator())
  {
    builder.CreateBr(cont_bb);
  }

  builder.SetInsertPoint(completed_bb);
  llvm::Value *completed_payload = extract_async_payload(source_sig->result);
  llvm::Value *completed_value = coerce_to_result(completed_payload, source_sig->result);
  if (!completed_value)
  {
    completed_value = DefaultFor(y.result);
  }
  if (result_slot)
  {
    if (completed_value->getType() != expected)
    {
      if (llvm::Value *coerced = CoerceTo(&builder, completed_value, expected))
      {
        completed_value = coerced;
      }
      else
      {
        completed_value = llvm::Constant::getNullValue(expected);
      }
    }
    builder.CreateStore(completed_value, result_slot);
  }
  builder.CreateBr(cont_bb);

  if (failed_bb)
  {
    builder.SetInsertPoint(failed_bb);
    llvm::Value *failed_payload = extract_async_payload(source_sig->err);
    if (!failed_payload &&
        source_sig->err &&
        !IsUnitTypeRef(source_sig->err) &&
        !IsNeverTypeRef(source_sig->err))
    {
      if (llvm::Type *err_ty = emitter.GetLLVMType(source_sig->err))
      {
        if (!err_ty->isVoidTy())
        {
          failed_payload = llvm::Constant::getNullValue(err_ty);
        }
      }
    }
    emit_async_return(failed_payload, source_sig->err);
    if (!builder.GetInsertBlock()->getTerminator())
    {
      builder.CreateBr(cont_bb);
    }
  }

  builder.SetInsertPoint(fallback_bb);
  llvm::Value *fallback_async = builder.CreateLoad(async_struct, source_slot);
  llvm::Value *fallback_value = coerce_to_result(fallback_async, async_type);
  if (!fallback_value)
  {
    fallback_value = DefaultFor(y.result);
  }
  if (result_slot)
  {
    if (fallback_value->getType() != expected)
    {
      if (llvm::Value *coerced = CoerceTo(&builder, fallback_value, expected))
      {
        fallback_value = coerced;
      }
      else
      {
        fallback_value = llvm::Constant::getNullValue(expected);
      }
    }
    builder.CreateStore(fallback_value, result_slot);
  }
  builder.CreateBr(cont_bb);

  builder.SetInsertPoint(panic_bb);
  if (result_slot)
  {
    builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  }
  builder.CreateBr(cont_bb);

  builder.SetInsertPoint(cont_bb);
  if (info.is_resume && async_state->resume_switch)
  {
    async_state->emitting_resume_prelude = false;
  }
  llvm::Value *result_value = nullptr;
  if (result_slot && expected && !expected->isVoidTy())
  {
    result_value = builder.CreateLoad(expected, result_slot);
  }
  if (!result_value)
  {
    result_value = DefaultFor(y.result);
  }
  emitter.SetTempValue(y.result, result_value);
}

} // namespace ultraviolet::codegen::emit_detail
