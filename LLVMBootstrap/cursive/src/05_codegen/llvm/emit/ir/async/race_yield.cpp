// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/race_yield.cpp
// Canonical owner for LLVM IR race-yield instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRRaceYield &r) const
{
  if (r.arms.empty())
  {
    emitter.SetTempValue(r.result, DefaultFor(r.result));
    return;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);

  const auto stream_sig = analysis::GetAsyncSig(r.stream_type);
  llvm::Type *expected = ExpectedLLVMType(r.result);
  if (!expected && r.stream_type)
  {
    expected = emitter.GetLLVMType(r.stream_type);
  }
  auto *stream_struct = llvm::dyn_cast_or_null<llvm::StructType>(expected);
  if (!stream_sig || !expected || !stream_struct)
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
  const AsyncStateDiscs stream_discs =
      LoweredAsyncStateDiscs(scope, r.stream_type);
  const std::uint64_t suspended_disc = stream_discs.suspended;

  auto extract_async_payload = [&](llvm::Value *async_value,
                                   llvm::StructType *async_struct,
                                   const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    if (!async_value || !async_struct || !payload_type ||
        IsUnitTypeRef(payload_type) || IsNeverTypeRef(payload_type))
    {
      return nullptr;
    }
    llvm::Type *payload_ll = emitter.GetLLVMType(payload_type);
    if (!payload_ll || payload_ll->isVoidTy())
    {
      return nullptr;
    }
    llvm::AllocaInst *async_slot = entry_builder.CreateAlloca(async_struct);
    builder.CreateStore(async_value, async_slot);
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

  auto make_async_complete = [&](llvm::Value *payload_value,
                                 const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    static std::uint64_t temp_index = 0;
    IRValue payload_ir;
    payload_ir.kind = IRValue::Kind::Opaque;
    payload_ir.name = r.result.name + ".race_yield.complete.payload." +
                      std::to_string(temp_index++);
    emitter.SetTempValue(payload_ir, payload_value ? payload_value : DefaultFor(payload_ir));

    IRAsyncComplete complete;
    complete.value = payload_ir;
    complete.result.kind = IRValue::Kind::Opaque;
    complete.result.name = r.result.name + ".race_yield.complete." +
                           std::to_string(temp_index++);
    complete.async_type = r.stream_type;
    complete.result_type = payload_type;
    (*this)(complete);
    return emitter.EvaluateIRValue(complete.result);
  };

  auto make_async_fail = [&](llvm::Value *payload_value,
                             const analysis::TypeRef &payload_type) -> llvm::Value *
  {
    static std::uint64_t temp_index = 0;
    IRValue payload_ir;
    payload_ir.kind = IRValue::Kind::Opaque;
    payload_ir.name = r.result.name + ".race_yield.fail.payload." +
                      std::to_string(temp_index++);
    emitter.SetTempValue(payload_ir, payload_value ? payload_value : DefaultFor(payload_ir));

    IRAsyncFail fail;
    fail.value = payload_ir;
    fail.result.kind = IRValue::Kind::Opaque;
    fail.result.name = r.result.name + ".race_yield.fail." +
                       std::to_string(temp_index++);
    fail.async_type = r.stream_type;
    fail.error_type = payload_type;
    (*this)(fail);
    return emitter.EvaluateIRValue(fail.result);
  };

  auto make_async_suspended = [&](llvm::Value *out_value,
                                  const analysis::TypeRef &out_type) -> llvm::Value *
  {
    if (stream_struct->getNumElements() < 1 ||
        !stream_struct->getElementType(0)->isIntegerTy())
    {
      return nullptr;
    }
    llvm::AllocaInst *stream_slot = entry_builder.CreateAlloca(stream_struct);
    builder.CreateStore(llvm::Constant::getNullValue(stream_struct), stream_slot);
    llvm::Type *disc_ty = stream_struct->getElementType(0);
    llvm::Value *disc_ptr = builder.CreateStructGEP(stream_struct, stream_slot, 0);
    builder.CreateStore(
        llvm::ConstantInt::get(disc_ty, suspended_disc),
        disc_ptr);

    if (out_type && !IsUnitTypeRef(out_type) && !IsNeverTypeRef(out_type))
    {
      llvm::Type *out_ll = emitter.GetLLVMType(out_type);
      if (out_ll && !out_ll->isVoidTy())
      {
        llvm::Value *payload = out_value;
        if (!payload)
        {
          payload = llvm::Constant::getNullValue(out_ll);
        }
        else if (payload->getType() != out_ll)
        {
          if (llvm::Value *coerced = CoerceTo(&builder, payload, out_ll))
          {
            payload = coerced;
          }
          else
          {
            payload = llvm::Constant::getNullValue(out_ll);
          }
        }

        llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
            emitter,
            &builder,
            stream_struct,
            stream_slot,
            ::cursive::analysis::layout::kPtrAlign);
        if (payload_i8)
        {
          llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(out_ll);
          builder.CreateStore(payload, src_slot);

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
    }

    return builder.CreateLoad(stream_struct, stream_slot);
  };

  struct YieldArmEval
  {
    const IRRaceArm *arm = nullptr;
    llvm::Value *async_value = nullptr;
    llvm::StructType *async_struct = nullptr;
    llvm::AllocaInst *async_slot = nullptr;
    analysis::TypeRef out_type = nullptr;
    analysis::TypeRef err_type = nullptr;
    AsyncStateDiscs discs{};
  };

  std::vector<YieldArmEval> evaluated;
  evaluated.reserve(r.arms.size());
  for (const IRRaceArm &arm : r.arms)
  {
    emitter.EmitIR(arm.async_ir);
    YieldArmEval eval;
    eval.arm = &arm;
    eval.async_value = EvaluateOrDefault(arm.async_value);
    eval.async_struct = llvm::dyn_cast_or_null<llvm::StructType>(
        eval.async_value ? eval.async_value->getType() : nullptr);
    if (active_ctx)
    {
      if (const auto sig = analysis::GetAsyncSig(active_ctx->LookupValueType(arm.async_value)))
      {
        eval.out_type = sig->out;
        eval.err_type = sig->err;
        eval.discs = LoweredAsyncStateDiscs(scope, *sig);
      }
    }
    evaluated.push_back(std::move(eval));
  }

  llvm::AllocaInst *result_slot = entry_builder.CreateAlloca(stream_struct);
  builder.CreateStore(llvm::Constant::getNullValue(stream_struct), result_slot);

  for (YieldArmEval &eval : evaluated)
  {
    if (!eval.async_struct || eval.async_struct->getNumElements() < 1 ||
        !eval.async_struct->getElementType(0)->isIntegerTy())
    {
      emitter.SetTempValue(r.result, llvm::Constant::getNullValue(expected));
      return;
    }
    eval.async_slot = entry_builder.CreateAlloca(eval.async_struct);
    builder.CreateStore(eval.async_value, eval.async_slot);
  }

  auto current_async_value = [&](const YieldArmEval &eval) -> llvm::Value *
  {
    if (!eval.async_slot || !eval.async_struct)
    {
      return nullptr;
    }
    return builder.CreateLoad(eval.async_struct, eval.async_slot);
  };

  auto emit_suspended_arm = [&](const YieldArmEval &eval)
  {
    llvm::Value *current_async = current_async_value(eval);
    llvm::Value *out_payload =
        extract_async_payload(current_async, eval.async_struct, eval.out_type);
    if (!out_payload)
    {
      out_payload = DefaultFor(eval.arm->match_value);
    }
    emitter.SetTempValue(eval.arm->match_value, out_payload);
    emitter.EmitIR(eval.arm->handler_ir);
    llvm::Value *handler_value = EvaluateOrDefault(eval.arm->handler_result);
    llvm::Value *suspended_stream = make_async_suspended(handler_value, stream_sig->out);
    if (!suspended_stream)
    {
      suspended_stream = llvm::Constant::getNullValue(expected);
    }
    builder.CreateStore(suspended_stream, result_slot);
  };

  auto emit_failed_arm = [&](const YieldArmEval &eval)
  {
    llvm::Value *current_async = current_async_value(eval);
    llvm::Value *err_payload =
        extract_async_payload(current_async, eval.async_struct, eval.err_type);
    llvm::Value *failed_stream = make_async_fail(err_payload, eval.err_type);
    if (!failed_stream)
    {
      failed_stream = llvm::Constant::getNullValue(expected);
    }
    builder.CreateStore(failed_stream, result_slot);
  };

  auto emit_completed_stream = [&]()
  {
    llvm::Value *completed_stream = make_async_complete(nullptr, stream_sig->result);
    if (!completed_stream)
    {
      completed_stream = llvm::Constant::getNullValue(expected);
    }
    builder.CreateStore(completed_stream, result_slot);
  };

  std::vector<llvm::BasicBlock *> suspended_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> suspended_hit(evaluated.size(), nullptr);
  std::vector<llvm::BasicBlock *> failed_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> failed_hit(evaluated.size(), nullptr);

  for (std::size_t i = 0; i <= evaluated.size(); ++i)
  {
    suspended_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.yield.chk.suspended." + std::to_string(i),
        func);
    failed_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.yield.chk.failed." + std::to_string(i),
        func);
  }
  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    suspended_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.yield.suspended." + std::to_string(i),
        func);
    failed_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "race.yield.failed." + std::to_string(i),
        func);
  }

  llvm::BasicBlock *complete_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "race.yield.completed", func);
  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "race.yield.merge", func);

  builder.CreateBr(suspended_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(suspended_check[i]);
    const YieldArmEval &eval = evaluated[i];
    llvm::Value *current_async = current_async_value(eval);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    llvm::Value *is_suspended = EmitTypedEq(
        &builder,
        disc,
        llvm::ConstantInt::get(disc->getType(), eval.discs.suspended));
    builder.CreateCondBr(
        AsBool(&builder, is_suspended),
        suspended_hit[i],
        suspended_check[i + 1]);

    builder.SetInsertPoint(suspended_hit[i]);
    emit_suspended_arm(eval);
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(suspended_check[evaluated.size()]);
  builder.CreateBr(failed_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(failed_check[i]);
    const YieldArmEval &eval = evaluated[i];
    llvm::Value *current_async = current_async_value(eval);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    if (eval.discs.failed.has_value())
    {
      llvm::Value *is_failed = EmitTypedEq(
          &builder,
          disc,
          llvm::ConstantInt::get(disc->getType(), *eval.discs.failed));
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
    emit_failed_arm(eval);
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(failed_check[evaluated.size()]);
  builder.CreateBr(complete_bb);

  builder.SetInsertPoint(complete_bb);
  emit_completed_stream();
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(merge_bb);
  llvm::Value *out = builder.CreateLoad(stream_struct, result_slot);
  if (!out)
  {
    out = llvm::Constant::getNullValue(expected);
  }
  emitter.SetTempValue(r.result, out);
}

} // namespace cursive::codegen::emit_detail
