// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/race_return.cpp
// Canonical owner for LLVM IR race-return instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRRaceReturn &r) const
{
  if (r.arms.empty())
  {
    emitter.SetTempValue(r.result, DefaultFor(r.result));
    return;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);

  llvm::Type *expected = ExpectedLLVMType(r.result);
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupValueType(r.result) : nullptr;
  if (!expected && target_type)
  {
    expected = emitter.GetLLVMType(target_type);
  }
  if (!expected || expected->isVoidTy())
  {
    emitter.SetTempValue(r.result, DefaultFor(r.result));
    return;
  }

  llvm::Function *func =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!func)
  {
    emitter.SetTempValue(r.result, DefaultFor(r.result));
    return;
  }

  llvm::IRBuilder<> entry_builder(
      &func->getEntryBlock(),
      func->getEntryBlock().begin());

  struct RaceArmEval
  {
    const IRRaceArm *arm = nullptr;
    llvm::Value *async_value = nullptr;
    llvm::StructType *async_struct = nullptr;
    llvm::AllocaInst *async_slot = nullptr;
    analysis::TypeRef async_type = nullptr;
    analysis::TypeRef result_type = nullptr;
    analysis::TypeRef error_type = nullptr;
    AsyncStateDiscs discs{};
  };

  std::vector<RaceArmEval> evaluated;
  evaluated.reserve(r.arms.size());
  for (const IRRaceArm &arm : r.arms)
  {
    emitter.EmitIR(arm.async_ir);
    RaceArmEval entry;
    entry.arm = &arm;
    entry.async_value = EvaluateOrDefault(arm.async_value);
    entry.async_struct = llvm::dyn_cast_or_null<llvm::StructType>(
        entry.async_value ? entry.async_value->getType() : nullptr);
    if (active_ctx)
    {
      entry.async_type = active_ctx->LookupValueType(arm.async_value);
      if (const auto sig = analysis::GetAsyncSig(entry.async_type))
      {
        entry.result_type = sig->result;
        entry.error_type = sig->err;
      }
    }
    if (entry.async_type)
    {
      entry.discs = LoweredAsyncStateDiscs(scope, entry.async_type);
    }
    evaluated.push_back(std::move(entry));
  }

  llvm::AllocaInst *result_slot = entry_builder.CreateAlloca(expected);
  builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);

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
    if (llvm::Value *copied = materialize_as_type(out, expected))
    {
      return copied;
    }
    return llvm::Constant::getNullValue(expected);
  };

  auto extract_async_payload = [&](llvm::AllocaInst *async_slot,
                                   llvm::StructType *async_struct,
                                   const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    if (!async_slot || !async_struct || !payload_type ||
        IsUnitTypeRef(payload_type) ||
        IsNeverTypeRef(payload_type))
    {
      return nullptr;
    }
    if (async_struct->getNumElements() < 1 ||
        !async_struct->getElementType(0)->isIntegerTy())
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
        async_slot,
        ::cursive::analysis::layout::kPtrAlign);
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

  for (RaceArmEval &eval : evaluated)
  {
    if (!eval.async_struct ||
        eval.async_struct->getNumElements() < 1 ||
        !eval.async_struct->getElementType(0)->isIntegerTy())
    {
      llvm::Value *out = coerce_to_result(eval.async_value, eval.async_type);
      emitter.SetTempValue(r.result, out ? out : llvm::Constant::getNullValue(expected));
      return;
    }
    eval.async_slot = entry_builder.CreateAlloca(eval.async_struct);
    builder.CreateStore(eval.async_value, eval.async_slot);
  }

  auto emit_completed_arm = [&](const RaceArmEval &arm_eval)
  {
    llvm::Value *match_payload =
        extract_async_payload(arm_eval.async_slot,
                              arm_eval.async_struct,
                              arm_eval.result_type);
    if (!match_payload)
    {
      match_payload = DefaultFor(arm_eval.arm->match_value);
    }
    emitter.SetTempValue(arm_eval.arm->match_value, match_payload);
    emitter.EmitIR(arm_eval.arm->handler_ir);

    analysis::TypeRef source_type = r.result_type;
    if (!source_type && active_ctx)
    {
      source_type = active_ctx->LookupValueType(arm_eval.arm->handler_result);
    }
    llvm::Value *handler_out =
        coerce_to_result(EvaluateOrDefault(arm_eval.arm->handler_result), source_type);
    builder.CreateStore(handler_out, result_slot);
  };

  auto emit_failed_arm = [&](const RaceArmEval &arm_eval)
  {
    llvm::Value *error_payload =
        extract_async_payload(arm_eval.async_slot,
                              arm_eval.async_struct,
                              arm_eval.error_type);
    llvm::Value *out = coerce_to_result(error_payload, arm_eval.error_type);
    builder.CreateStore(out, result_slot);
  };

  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
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
      llvm::BasicBlock::Create(emitter.GetContext(), "race.return.loop", func);
  llvm::BasicBlock *fallback_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "race.return.fallback", func);
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "race.return.panic", func);
  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "race.return.merge", func);

  std::vector<llvm::BasicBlock *> completed_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> completed_hit(evaluated.size(), nullptr);
  std::vector<llvm::BasicBlock *> failed_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> failed_hit(evaluated.size(), nullptr);
  std::vector<llvm::BasicBlock *> resume_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> resume_hit(evaluated.size(), nullptr);

  for (std::size_t i = 0; i <= evaluated.size(); ++i)
  {
    completed_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.chk.completed." + std::to_string(i),
        func);
    failed_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.chk.failed." + std::to_string(i),
        func);
    resume_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.chk.resume." + std::to_string(i),
        func);
  }
  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    completed_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.completed." + std::to_string(i),
        func);
    failed_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.failed." + std::to_string(i),
        func);
    resume_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.return.resume." + std::to_string(i),
        func);
  }

  builder.CreateBr(loop_bb);

  builder.SetInsertPoint(loop_bb);
  builder.CreateBr(completed_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(completed_check[i]);
    const RaceArmEval &arm_eval = evaluated[i];
    llvm::Value *current_async =
        builder.CreateLoad(arm_eval.async_struct, arm_eval.async_slot);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    llvm::Value *is_completed = EmitTypedEq(
        &builder,
        disc,
        llvm::ConstantInt::get(disc->getType(), arm_eval.discs.completed));
    builder.CreateCondBr(
        AsBool(&builder, is_completed),
        completed_hit[i],
        completed_check[i + 1]);

    builder.SetInsertPoint(completed_hit[i]);
    emit_completed_arm(arm_eval);
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(completed_check[evaluated.size()]);
  builder.CreateBr(failed_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(failed_check[i]);
    const RaceArmEval &arm_eval = evaluated[i];
    llvm::Value *current_async =
        builder.CreateLoad(arm_eval.async_struct, arm_eval.async_slot);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    if (arm_eval.discs.failed.has_value())
    {
      llvm::Value *is_failed = EmitTypedEq(
          &builder,
          disc,
          llvm::ConstantInt::get(disc->getType(), *arm_eval.discs.failed));
      builder.CreateCondBr(
          AsBool(&builder, is_failed),
          failed_hit[i],
          failed_check[i + 1]);
    }
    else
    {
      builder.CreateBr(failed_check[i + 1]);
    }

    builder.SetInsertPoint(failed_hit[i]);
    emit_failed_arm(arm_eval);
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(failed_check[evaluated.size()]);
  builder.CreateBr(resume_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(resume_check[i]);
    const RaceArmEval &arm_eval = evaluated[i];
    llvm::Value *current_async =
        builder.CreateLoad(arm_eval.async_struct, arm_eval.async_slot);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    llvm::Value *is_suspended = EmitTypedEq(
        &builder,
        disc,
        llvm::ConstantInt::get(disc->getType(), arm_eval.discs.suspended));
    builder.CreateCondBr(
        AsBool(&builder, is_suspended),
        resume_hit[i],
        resume_check[i + 1]);

    builder.SetInsertPoint(resume_hit[i]);
    llvm::Value *suspended_ptr = builder.CreateBitCast(arm_eval.async_slot, opaque_ptr_ty);
    llvm::Value *unit_input = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
    llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
        emitter,
        &builder,
        suspended_ptr,
        unit_input,
        panic_ptr);
    llvm::Value *resumed_async = materialize_as_type(resume_call, arm_eval.async_struct);
    if (!resumed_async)
    {
      resumed_async = llvm::Constant::getNullValue(arm_eval.async_struct);
    }
    builder.CreateStore(resumed_async, arm_eval.async_slot);
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
  }

  builder.SetInsertPoint(resume_check[evaluated.size()]);
  builder.CreateBr(fallback_bb);

  builder.SetInsertPoint(fallback_bb);
  builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(panic_bb);
  builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(merge_bb);
  llvm::Value *out = builder.CreateLoad(expected, result_slot);
  if (!out)
  {
    out = llvm::Constant::getNullValue(expected);
  }
  emitter.SetTempValue(r.result, out);
}

} // namespace cursive::codegen::emit_detail
