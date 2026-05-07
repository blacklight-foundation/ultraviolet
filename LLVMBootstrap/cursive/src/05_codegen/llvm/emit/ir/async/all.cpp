// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/all.cpp
// Canonical owner for LLVM IR all instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRAll &all) const
{
  if (all.async_values.empty())
  {
    emitter.SetTempValue(all.result, DefaultFor(all.result));
    return;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(active_ctx);

  std::vector<analysis::TypeRef> tuple_elem_types;
  if (all.tuple_type)
  {
    if (const auto *tuple = std::get_if<analysis::TypeTuple>(&all.tuple_type->node))
    {
      tuple_elem_types = tuple->elements;
    }
  }

  struct AllEval
  {
    llvm::Value *async_value = nullptr;
    llvm::StructType *async_struct = nullptr;
    llvm::AllocaInst *async_slot = nullptr;
    analysis::TypeRef async_type = nullptr;
    analysis::TypeRef result_type = nullptr;
    analysis::TypeRef error_type = nullptr;
    AsyncStateDiscs discs{};
  };

  std::vector<AllEval> evaluated;
  evaluated.reserve(all.async_values.size());
  for (std::size_t i = 0; i < all.async_values.size(); ++i)
  {
    if (i < all.async_irs.size())
    {
      emitter.EmitIR(all.async_irs[i]);
    }

    AllEval entry;
    entry.async_value = EvaluateOrDefault(all.async_values[i]);
    entry.async_struct = llvm::dyn_cast_or_null<llvm::StructType>(
        entry.async_value ? entry.async_value->getType() : nullptr);
    if (active_ctx)
    {
      entry.async_type = active_ctx->LookupValueType(all.async_values[i]);
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
    if (!entry.result_type && i < tuple_elem_types.size())
    {
      entry.result_type = tuple_elem_types[i];
    }
    if (!entry.error_type && i < all.error_types.size())
    {
      entry.error_type = all.error_types[i];
    }

    evaluated.push_back(std::move(entry));
  }

  llvm::Type *expected = ExpectedLLVMType(all.result);
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupValueType(all.result) : nullptr;
  if (!expected && target_type)
  {
    expected = emitter.GetLLVMType(target_type);
  }
  if (!expected || expected->isVoidTy())
  {
    emitter.SetTempValue(all.result, DefaultFor(all.result));
    return;
  }

  llvm::Function *func =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!func)
  {
    emitter.SetTempValue(all.result, DefaultFor(all.result));
    return;
  }

  llvm::IRBuilder<> entry_builder(
      &func->getEntryBlock(),
      func->getEntryBlock().begin());
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
    if (!async_slot || !payload_type ||
        IsUnitTypeRef(payload_type) ||
        IsNeverTypeRef(payload_type))
    {
      return nullptr;
    }
    if (!async_struct || async_struct->getNumElements() < 1 ||
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

  for (AllEval &eval : evaluated)
  {
    if (!eval.async_struct ||
        eval.async_struct->getNumElements() < 1 ||
        !eval.async_struct->getElementType(0)->isIntegerTy())
    {
      llvm::Value *out = coerce_to_result(eval.async_value, eval.async_type);
      builder.CreateStore(out, result_slot);
      emitter.SetTempValue(all.result, builder.CreateLoad(expected, result_slot));
      return;
    }
    eval.async_slot = entry_builder.CreateAlloca(eval.async_struct);
    builder.CreateStore(eval.async_value, eval.async_slot);
  }

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
      llvm::BasicBlock::Create(emitter.GetContext(), "all.loop", func);
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "all.panic", func);
  llvm::BasicBlock *fallback_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "all.fallback", func);
  llvm::BasicBlock *success_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "all.success", func);
  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "all.merge", func);
  std::vector<llvm::BasicBlock *> failed_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> failed_hit(evaluated.size(), nullptr);
  std::vector<llvm::BasicBlock *> complete_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> resume_check(evaluated.size() + 1, nullptr);
  std::vector<llvm::BasicBlock *> resume_hit(evaluated.size(), nullptr);

  for (std::size_t i = 0; i <= evaluated.size(); ++i)
  {
    failed_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "all.chk.failed." + std::to_string(i),
        func);
    complete_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "all.chk.complete." + std::to_string(i),
        func);
    resume_check[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "all.chk.resume." + std::to_string(i),
        func);
  }
  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    failed_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "all.failed." + std::to_string(i),
        func);
    resume_hit[i] = llvm::BasicBlock::Create(
        emitter.GetContext(),
        "all.resume." + std::to_string(i),
        func);
  }

  builder.CreateBr(loop_bb);

  builder.SetInsertPoint(loop_bb);
  builder.CreateBr(failed_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(failed_check[i]);
    const AllEval &eval = evaluated[i];
    llvm::Value *current_async = builder.CreateLoad(eval.async_struct, eval.async_slot);
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
    llvm::Value *error_payload =
        extract_async_payload(eval.async_slot, eval.async_struct, eval.error_type);
    llvm::Value *out = coerce_to_result(error_payload, eval.error_type);
    builder.CreateStore(out, result_slot);
    builder.CreateBr(merge_bb);
  }

  builder.SetInsertPoint(failed_check[evaluated.size()]);
  builder.CreateBr(complete_check[0]);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(complete_check[i]);
    const AllEval &eval = evaluated[i];
    llvm::Value *current_async = builder.CreateLoad(eval.async_struct, eval.async_slot);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    llvm::Value *is_completed = EmitTypedEq(
        &builder,
        disc,
        llvm::ConstantInt::get(disc->getType(), eval.discs.completed));
    builder.CreateCondBr(
        AsBool(&builder, is_completed),
        complete_check[i + 1],
        resume_check[0]);
  }

  builder.SetInsertPoint(complete_check[evaluated.size()]);
  builder.CreateBr(success_bb);

  for (std::size_t i = 0; i < evaluated.size(); ++i)
  {
    builder.SetInsertPoint(resume_check[i]);
    const AllEval &eval = evaluated[i];
    llvm::Value *current_async = builder.CreateLoad(eval.async_struct, eval.async_slot);
    llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
    llvm::Value *is_suspended = EmitTypedEq(
        &builder,
        disc,
        llvm::ConstantInt::get(disc->getType(), eval.discs.suspended));
    builder.CreateCondBr(
        AsBool(&builder, is_suspended),
        resume_hit[i],
        resume_check[i + 1]);

    builder.SetInsertPoint(resume_hit[i]);
    llvm::Value *suspended_ptr = builder.CreateBitCast(eval.async_slot, opaque_ptr_ty);
    llvm::Value *unit_input = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
    llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
        emitter,
        &builder,
        suspended_ptr,
        unit_input,
        panic_ptr);
    llvm::Value *resumed_async = materialize_as_type(resume_call, eval.async_struct);
    if (!resumed_async)
    {
      resumed_async = llvm::Constant::getNullValue(eval.async_struct);
    }
    builder.CreateStore(resumed_async, eval.async_slot);
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

  builder.SetInsertPoint(success_bb);
  llvm::Value *tuple_value = nullptr;
  llvm::Type *tuple_llvm_ty = all.tuple_type ? emitter.GetLLVMType(all.tuple_type) : nullptr;
  if (tuple_llvm_ty && (tuple_llvm_ty->isStructTy() || tuple_llvm_ty->isArrayTy()))
  {
    llvm::Value *agg = llvm::Constant::getNullValue(tuple_llvm_ty);
    for (std::size_t i = 0; i < evaluated.size(); ++i)
    {
      llvm::Type *elem_ty = nullptr;
      if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(tuple_llvm_ty))
      {
        elem_ty = arr_ty->getElementType();
      }
      else if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(tuple_llvm_ty))
      {
        if (i < struct_ty->getNumElements())
        {
          elem_ty = struct_ty->getElementType(static_cast<unsigned>(i));
        }
      }
      if (!elem_ty || elem_ty->isVoidTy())
      {
        continue;
      }

      const AllEval &eval = evaluated[i];
      llvm::Value *completed_payload =
          extract_async_payload(eval.async_slot, eval.async_struct, eval.result_type);
      completed_payload = materialize_as_type(completed_payload, elem_ty);
      if (!completed_payload)
      {
        completed_payload = llvm::Constant::getNullValue(elem_ty);
      }
      agg = builder.CreateInsertValue(agg, completed_payload, {static_cast<unsigned>(i)});
    }
    tuple_value = agg;
  }

  llvm::Value *success_out = coerce_to_result(tuple_value, all.tuple_type);
  builder.CreateStore(success_out, result_slot);
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(panic_bb);
  builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(fallback_bb);
  builder.CreateStore(llvm::Constant::getNullValue(expected), result_slot);
  builder.CreateBr(merge_bb);

  builder.SetInsertPoint(merge_bb);
  llvm::Value *out = builder.CreateLoad(expected, result_slot);
  if (!out)
  {
    out = llvm::Constant::getNullValue(expected);
  }
  emitter.SetTempValue(all.result, out);
}

} // namespace cursive::codegen::emit_detail
