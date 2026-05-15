// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/loop.cpp
// Canonical owner for LLVM IR loop instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRLoop &loop) const
{
  llvm::Function *func = builder.GetInsertBlock()->getParent();
  if (!func)
  {
    emitter.SetTempValue(loop.result, DefaultFor(loop.result));
    return;
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef loop_result_type =
      active_ctx ? active_ctx->LookupValueType(loop.result) : nullptr;
  llvm::Type *loop_result_ty = ExpectedLLVMType(loop.result);
  if (!loop_result_ty && loop_result_type)
  {
    loop_result_ty = emitter.GetLLVMType(loop_result_type);
  }

  llvm::AllocaInst *loop_result_slot = nullptr;
  if (loop_result_ty && !loop_result_ty->isVoidTy())
  {
    loop_result_slot =
        CreateEntryAlloca(func, loop_result_ty, "loop.result.slot");

    llvm::Value *init_value = llvm::Constant::getNullValue(loop_result_ty);
    if (loop.kind == IRLoopKind::Conditional || loop.kind == IRLoopKind::Iter)
    {
      const analysis::TypeRef unit_type = analysis::MakeTypePrim("()");
      llvm::Type *unit_ty = emitter.GetLLVMType(unit_type);
      llvm::Value *unit_value = unit_ty
                                    ? llvm::Constant::getNullValue(unit_ty)
                                    : llvm::Constant::getNullValue(loop_result_ty);

      if (loop_result_type)
      {
        llvm::Value *coerced = CoerceToTyped(
            emitter,
            &builder,
            unit_value,
            loop_result_ty,
            unit_type,
            loop_result_type);
        if (coerced)
        {
          init_value = coerced;
        }
      }
      else if (unit_value->getType() != loop_result_ty)
      {
        llvm::Value *coerced = CoerceTo(&builder, unit_value, loop_result_ty);
        if (coerced)
        {
          init_value = coerced;
        }
      }
      else
      {
        init_value = unit_value;
      }
    }
    builder.CreateStore(init_value, loop_result_slot);
  }

  auto *loop_end = llvm::BasicBlock::Create(emitter.GetContext(), "loop.end", func);

  if (loop.kind == IRLoopKind::Infinite)
  {
    auto *loop_head = llvm::BasicBlock::Create(emitter.GetContext(), "loop.head", func);
    auto *loop_body = llvm::BasicBlock::Create(emitter.GetContext(), "loop.body", func);
    builder.CreateBr(loop_head);

    builder.SetInsertPoint(loop_head);
    builder.CreateBr(loop_body);

    builder.SetInsertPoint(loop_body);
    emitter.PushLoopTargets(loop_end, loop_head, loop_result_slot, loop_result_type);
    emitter.EmitIR(loop.body_ir);
    emitter.PopLoopTargets();
    if (!builder.GetInsertBlock()->getTerminator())
    {
      builder.CreateBr(loop_head);
    }
  }
  else if (loop.kind == IRLoopKind::Conditional)
  {
    auto *loop_cond = llvm::BasicBlock::Create(emitter.GetContext(), "loop.cond", func);
    auto *loop_body = llvm::BasicBlock::Create(emitter.GetContext(), "loop.body", func);
    builder.CreateBr(loop_cond);

    builder.SetInsertPoint(loop_cond);
    emitter.EmitIR(loop.cond_ir);
    llvm::Value *cond = llvm::ConstantInt::getFalse(emitter.GetContext());
    if (loop.cond_value.has_value())
    {
      cond = AsBool(&builder, EvaluateOrDefault(*loop.cond_value));
    }
    builder.CreateCondBr(cond, loop_body, loop_end);

    builder.SetInsertPoint(loop_body);
    emitter.PushLoopTargets(loop_end, loop_cond, loop_result_slot, loop_result_type);
    emitter.EmitIR(loop.body_ir);
    emitter.PopLoopTargets();
    if (!builder.GetInsertBlock()->getTerminator())
    {
      builder.CreateBr(loop_cond);
    }
  }
  else
  {
    emitter.EmitIR(loop.iter_ir);
    if (builder.GetInsertBlock()->getTerminator())
    {
      // Iterator evaluation already terminated this path (panic/return).
    }
    else
    {
      const bool debug_loop_iter = core::IsDebugEnabled("loop");
      bool handled_async_iter = false;

      if (loop.iter_value.has_value() && active_ctx)
      {
        analysis::TypeRef iter_type =
            analysis::StripPerm(active_ctx->LookupValueType(*loop.iter_value));
        if (const auto async_sig = analysis::GetAsyncSig(iter_type))
        {
          handled_async_iter = true;
          llvm::Value *iter_async = EvaluateOrDefault(*loop.iter_value);
          auto *iter_async_ty = llvm::dyn_cast<llvm::StructType>(
              iter_async ? iter_async->getType() : nullptr);
          if (!iter_async || !iter_async_ty || iter_async_ty->getNumElements() < 1 ||
              !iter_async_ty->getElementType(0)->isIntegerTy())
          {
            builder.CreateBr(loop_end);
          }
          else
          {
            llvm::IRBuilder<> entry_builder(
                &func->getEntryBlock(),
                func->getEntryBlock().begin());
            llvm::AllocaInst *iter_async_slot = entry_builder.CreateAlloca(
                iter_async_ty,
                nullptr,
                "iter.async.slot");
            builder.CreateStore(iter_async, iter_async_slot);

            llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
            llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
            llvm::Type *opaque_ptr_ty = emitter.GetOpaquePtr();
            auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);

            auto materialize_as_type =
                [&](llvm::Value *value, llvm::Type *dst_ty) -> llvm::Value *
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

              llvm::Value *dst_i8 = builder.CreateBitCast(
                  dst_slot, llvm::PointerType::get(i8_ty, 0));
              llvm::Value *src_i8 = builder.CreateBitCast(
                  src_slot, llvm::PointerType::get(i8_ty, 0));
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

            auto extract_async_payload = [&](llvm::Value *async_value,
                                             const analysis::TypeRef &payload_type)
                -> llvm::Value *
            {
              if (!async_value || !payload_type || IsUnitTypeRef(payload_type) ||
                  IsNeverTypeRef(payload_type))
              {
                return nullptr;
              }
              llvm::Type *payload_ty = emitter.GetLLVMType(payload_type);
              if (!payload_ty || payload_ty->isVoidTy())
              {
                return nullptr;
              }
              llvm::AllocaInst *payload_async_slot = entry_builder.CreateAlloca(iter_async_ty);
              builder.CreateStore(async_value, payload_async_slot);
              llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
                  emitter,
                  &builder,
                  iter_async_ty,
                  payload_async_slot,
                  ::cursive::analysis::layout::kPtrAlign);
              if (!payload_i8)
              {
                return nullptr;
              }
              llvm::Value *payload_ptr = builder.CreateBitCast(
                  payload_i8,
                  llvm::PointerType::get(payload_ty, 0));
              llvm::LoadInst *loaded = builder.CreateLoad(payload_ty, payload_ptr);
              loaded->setAlignment(llvm::Align(1));
              return loaded;
            };

            auto bind_async_iter_identifier =
                [&](std::string_view name, llvm::Value *elem_value)
            {
              if (name.empty() || !elem_value)
              {
                return;
              }
              llvm::Value *local = emitter.GetLocal(std::string(name));
              llvm::AllocaInst *slot = llvm::dyn_cast_or_null<llvm::AllocaInst>(local);
              if (!slot)
              {
                llvm::IRBuilder<> entry_builder_local(
                    &func->getEntryBlock(),
                    func->getEntryBlock().begin());
                std::string slot_name = std::string(name) + ".iter";
                slot = entry_builder_local.CreateAlloca(
                    elem_value->getType(),
                    nullptr,
                    slot_name);
                emitter.RegisterLocalBindStorage(std::string(name), slot);
                if (async_sig->out)
                {
                  emitter.SetLocalType(std::string(name), async_sig->out);
                }
              }

              llvm::Type *dst_ty = slot->getAllocatedType();
              llvm::Value *stored = elem_value;
              const analysis::TypeRef dst_type =
                  emitter.LookupLocalType(std::string(name));
              if (dst_type && async_sig->out)
              {
                llvm::Value *coerced = CoerceToTyped(
                    emitter,
                    &builder,
                    stored,
                    dst_ty,
                    async_sig->out,
                    dst_type);
                stored = coerced ? coerced : llvm::Constant::getNullValue(dst_ty);
              }
              else if (stored->getType() != dst_ty)
              {
                llvm::Value *coerced = CoerceTo(&builder, stored, dst_ty);
                stored = coerced ? coerced : llvm::Constant::getNullValue(dst_ty);
              }
              builder.CreateStore(stored, slot);
            };

            auto bind_async_iter_pattern = [&](llvm::Value *elem_value)
            {
              if (!loop.pattern || !elem_value)
              {
                return;
              }
              std::visit(
                  [&](const auto &pat)
                  {
                    using P = std::decay_t<decltype(pat)>;
                    if constexpr (std::is_same_v<P, IRIdentifierPattern>)
                    {
                      bind_async_iter_identifier(pat.name, elem_value);
                    }
                    else if constexpr (std::is_same_v<P, IRTypedPattern>)
                    {
                      bind_async_iter_identifier(pat.name, elem_value);
                    }
                    else if constexpr (std::is_same_v<P, IRWildcardPattern>)
                    {
                      return;
                    }
                  },
                  loop.pattern->node);
            };

            const analysis::ScopeContext &scope = BuildScope(active_ctx);
            const AsyncStateDiscs iter_discs =
                LoweredAsyncStateDiscs(scope, *async_sig);
            const std::uint64_t suspended_disc = iter_discs.suspended;
            const std::uint64_t completed_disc = iter_discs.completed;
            const std::optional<std::uint64_t> failed_disc =
                iter_discs.failed;
            const std::string fn_sym = std::string(func->getName());
            const LowerCtx::ProcSigInfo *proc_sig =
                active_ctx ? active_ctx->LookupProcSig(fn_sym) : nullptr;

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
                    fn_sym,
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

            llvm::Value *panic_ptr =
                LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
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

            auto *loop_cond =
                llvm::BasicBlock::Create(emitter.GetContext(), "loop.cond", func);
            auto *loop_body =
                llvm::BasicBlock::Create(emitter.GetContext(), "loop.body", func);
            auto *loop_resume =
                llvm::BasicBlock::Create(emitter.GetContext(), "loop.resume", func);
            auto *loop_failed = failed_disc.has_value()
                                    ? llvm::BasicBlock::Create(
                                          emitter.GetContext(),
                                          "loop.failed",
                                          func)
                                    : nullptr;
            builder.CreateBr(loop_cond);

            builder.SetInsertPoint(loop_cond);
            llvm::Value *current_async = builder.CreateLoad(iter_async_ty, iter_async_slot);
            llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
            auto *disc_ty = llvm::cast<llvm::IntegerType>(disc->getType());
            llvm::SwitchInst *state_sw = builder.CreateSwitch(
                disc, loop_end, failed_disc.has_value() ? 3 : 2);
            state_sw->addCase(
                llvm::ConstantInt::get(disc_ty, suspended_disc), loop_body);
            state_sw->addCase(
                llvm::ConstantInt::get(disc_ty, completed_disc), loop_end);
            if (failed_disc.has_value())
            {
              state_sw->addCase(
                  llvm::ConstantInt::get(disc_ty, *failed_disc), loop_failed);
            }

            builder.SetInsertPoint(loop_body);
            bind_async_iter_pattern(extract_async_payload(current_async, async_sig->out));
            emitter.PushLoopTargets(loop_end, loop_resume, loop_result_slot, loop_result_type);
            emitter.EmitIR(loop.body_ir);
            emitter.PopLoopTargets();
            if (!builder.GetInsertBlock()->getTerminator())
            {
              builder.CreateBr(loop_resume);
            }

            builder.SetInsertPoint(loop_resume);
            llvm::Value *suspended_ptr =
                builder.CreateBitCast(iter_async_slot, opaque_ptr_ty);
            llvm::Value *unit_input =
                llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
            llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
                emitter,
                &builder,
                suspended_ptr,
                unit_input,
                panic_ptr);
            llvm::Value *resumed_async =
                materialize_as_type(resume_call, iter_async_ty);
            if (!resumed_async)
            {
              resumed_async = llvm::Constant::getNullValue(iter_async_ty);
            }
            builder.CreateStore(resumed_async, iter_async_slot);
            if (has_panic_ptr)
            {
              llvm::Value *panic_i8 = panic_ptr;
              if (panic_i8->getType() != llvm::PointerType::get(i8_ty, 0))
              {
                panic_i8 = builder.CreateBitCast(
                    panic_i8, llvm::PointerType::get(i8_ty, 0));
              }
              llvm::LoadInst *panic_flag = builder.CreateLoad(i8_ty, panic_i8);
              llvm::Value *has_panic = builder.CreateICmpNE(
                  panic_flag,
                  llvm::ConstantInt::get(i8_ty, 0));
              builder.CreateCondBr(has_panic, loop_end, loop_cond);
            }
            else
            {
              builder.CreateBr(loop_cond);
            }

            if (loop_failed)
            {
              builder.SetInsertPoint(loop_failed);
              bool propagated_failure = false;
              const auto outer_sig =
                  proc_sig ? analysis::GetAsyncSig(proc_sig->ret) : std::nullopt;
              const std::optional<std::uint64_t> outer_failed_disc =
                  outer_sig ? LoweredAsyncStateDiscs(scope, *outer_sig).failed
                            : std::nullopt;
              if (outer_sig && outer_failed_disc.has_value() && async_sig->err)
              {
                llvm::Value *failed_async =
                    builder.CreateLoad(iter_async_ty, iter_async_slot);
                llvm::Value *failed_payload =
                    extract_async_payload(failed_async, async_sig->err);
                if (!failed_payload &&
                    !IsUnitTypeRef(async_sig->err) &&
                    !IsNeverTypeRef(async_sig->err))
                {
                  if (llvm::Type *err_ty = emitter.GetLLVMType(async_sig->err))
                  {
                    if (!err_ty->isVoidTy())
                    {
                      failed_payload = llvm::Constant::getNullValue(err_ty);
                    }
                  }
                }

                llvm::Value *propagated_value = failed_payload;
                analysis::TypeRef propagated_type = async_sig->err;
                llvm::Type *outer_async_ty = emitter.GetLLVMType(proc_sig->ret);
                auto *outer_async_struct =
                    llvm::dyn_cast_or_null<llvm::StructType>(outer_async_ty);
                if (outer_async_struct &&
                    outer_async_struct->getNumElements() >= 1 &&
                    outer_async_struct->getElementType(0)->isIntegerTy())
                {
                  llvm::AllocaInst *outer_async_slot =
                      entry_builder.CreateAlloca(outer_async_struct);
                  builder.CreateStore(
                      llvm::Constant::getNullValue(outer_async_struct),
                      outer_async_slot);

                  llvm::Type *outer_disc_ty = outer_async_struct->getElementType(0);
                  llvm::Value *outer_disc_ptr =
                      builder.CreateStructGEP(outer_async_struct, outer_async_slot, 0);
                  builder.CreateStore(
                      llvm::ConstantInt::get(outer_disc_ty, *outer_failed_disc),
                      outer_disc_ptr);

                  llvm::Value *outer_payload_i8 = CreateTaggedPayloadI8Ptr(
                      emitter,
                      &builder,
                      outer_async_struct,
                      outer_async_slot,
                      ::cursive::analysis::layout::kPtrAlign);
                  if (outer_payload_i8 &&
                      outer_sig->err &&
                      !IsUnitTypeRef(outer_sig->err) &&
                      !IsNeverTypeRef(outer_sig->err))
                  {
                    llvm::Type *outer_err_ty = emitter.GetLLVMType(outer_sig->err);
                    if (outer_err_ty && !outer_err_ty->isVoidTy())
                    {
                      llvm::Value *outer_err_value = failed_payload;
                      if (!outer_err_value)
                      {
                        outer_err_value = llvm::Constant::getNullValue(outer_err_ty);
                      }
                      else if (outer_err_value->getType() != outer_err_ty)
                      {
                        if (llvm::Value *coerced = CoerceToTyped(
                                emitter,
                                &builder,
                                outer_err_value,
                                outer_err_ty,
                                async_sig->err,
                                outer_sig->err))
                        {
                          outer_err_value = coerced;
                        }
                        else if (llvm::Value *coerced_plain =
                                     CoerceTo(&builder, outer_err_value, outer_err_ty))
                        {
                          outer_err_value = coerced_plain;
                        }
                        else
                        {
                          outer_err_value = llvm::Constant::getNullValue(outer_err_ty);
                        }
                      }

                      llvm::AllocaInst *src_slot =
                          entry_builder.CreateAlloca(outer_err_ty);
                      builder.CreateStore(outer_err_value, src_slot);
                      llvm::Value *src_i8 = builder.CreateBitCast(
                          src_slot, llvm::PointerType::get(i8_ty, 0));
                      const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
                      const std::uint64_t copy_size = static_cast<std::uint64_t>(
                          dl.getTypeAllocSize(outer_err_ty));
                      if (copy_size > 0)
                      {
                        builder.CreateMemCpy(
                            outer_payload_i8,
                            llvm::Align(1),
                            src_i8,
                            llvm::Align(1),
                            llvm::ConstantInt::get(i64_ty, copy_size));
                      }
                    }
                  }

                  propagated_value =
                      builder.CreateLoad(outer_async_struct, outer_async_slot);
                  propagated_type = proc_sig->ret;
                }

                if (propagated_value)
                {
                  emit_async_return(propagated_value, propagated_type);
                  propagated_failure =
                      builder.GetInsertBlock()->getTerminator() != nullptr;
                }
              }

              if (!propagated_failure || !builder.GetInsertBlock()->getTerminator())
              {
                builder.CreateBr(loop_end);
              }
            }
          }
        }
      }

      if (!handled_async_iter)
      {
        std::optional<std::uint64_t> trip_count;
        analysis::TypeRef iter_elem_type = nullptr;
        bool iter_is_range_type = false;
        bool range_iter = false;
        IRRangeKind range_iter_kind = IRRangeKind::Exclusive;
        llvm::Type *range_bound_ty = nullptr;
        llvm::Value *range_lo = nullptr;
        llvm::Value *range_hi = nullptr;
        if (loop.iter_value.has_value())
        {
          trip_count = StaticLengthOf(*loop.iter_value);
          if (active_ctx)
          {
            analysis::TypeRef iter_type =
                active_ctx->LookupValueType(*loop.iter_value);
            if (!iter_type &&
                loop.iter_value->kind == IRValue::Kind::Local)
            {
              iter_type = emitter.LookupLocalType(loop.iter_value->name);
            }
            if (analysis::TypeRef stripped = analysis::StripPerm(iter_type))
            {
              iter_type = stripped;
            }
            if (iter_type)
            {
              if (const auto *arr = std::get_if<analysis::TypeArray>(&iter_type->node))
              {
                if (!trip_count.has_value())
                {
                  trip_count = arr->length;
                }
                iter_elem_type = arr->element;
              }
              else if (const auto *slice =
                           std::get_if<analysis::TypeSlice>(&iter_type->node))
              {
                iter_elem_type = slice->element;
              }
              else if (const auto *range =
                           std::get_if<analysis::TypeRange>(&iter_type->node))
              {
                iter_is_range_type = true;
                iter_elem_type = range->base;
                range_iter = true;
                range_iter_kind = IRRangeKind::Exclusive;
              }
              else if (const auto *range =
                           std::get_if<analysis::TypeRangeInclusive>(&iter_type->node))
              {
                iter_is_range_type = true;
                iter_elem_type = range->base;
                range_iter = true;
                range_iter_kind = IRRangeKind::Inclusive;
              }
              else if (const auto *range =
                           std::get_if<analysis::TypeRangeFrom>(&iter_type->node))
              {
                iter_is_range_type = true;
                iter_elem_type = range->base;
                range_iter = true;
                range_iter_kind = IRRangeKind::From;
              }
            }
          }
          if (range_iter)
          {
            if (!iter_elem_type || !analysis::BuiltinStepType(iter_elem_type))
            {
              if (active_ctx)
              {
                const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
              }
              range_iter = false;
            }
          }
          if (range_iter)
          {
            range_bound_ty =
                iter_elem_type ? emitter.GetLLVMType(iter_elem_type) : nullptr;
            if (!range_bound_ty || !range_bound_ty->isIntegerTy())
            {
              if (active_ctx)
              {
                const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
              }
              range_iter = false;
            }
          }
          if (range_iter)
          {
            if (auto range_value = ResolveRangeValue(
                    *loop.iter_value,
                    range_bound_ty,
                    std::optional<IRRangeKind>(range_iter_kind));
                range_value.has_value())
            {
              range_iter_kind = range_value->kind;
              range_lo = range_value->lo;
              range_hi = range_value->hi;
            }

            if (!range_lo)
            {
              if (active_ctx)
              {
                const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
              }
              range_iter = false;
            }
            const bool finite_range = range_iter_kind != IRRangeKind::From;
            if (finite_range && !range_hi)
            {
              if (active_ctx)
              {
                const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
              }
              range_iter = false;
            }
          }
        }

        if (debug_loop_iter)
        {
          const std::string fn_name = func ? func->getName().str() : std::string("<no-func>");
          std::cerr << "[loop-iter-debug] fn=" << fn_name
                    << " trip_count="
                    << (trip_count.has_value() ? std::to_string(*trip_count) : std::string("<none>"))
                    << " iter_elem_type="
                    << (iter_elem_type ? analysis::TypeToString(iter_elem_type) : std::string("<null>"))
                    << " range_iter=" << (range_iter ? "yes" : "no")
                    << "\n";
        }

        llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
        llvm::IRBuilder<> entry_builder(
            &func->getEntryBlock(),
            func->getEntryBlock().begin());
        llvm::AllocaInst *range_cur_slot = nullptr;
        llvm::AllocaInst *range_done_slot = nullptr;
        std::optional<IndexedSequenceLoweredIterState> indexed_iter;
        if (range_iter)
        {
          range_cur_slot = entry_builder.CreateAlloca(
              range_bound_ty, nullptr, "iter.range.cur");
          range_done_slot = entry_builder.CreateAlloca(
              llvm::Type::getInt1Ty(emitter.GetContext()),
              nullptr,
              "iter.range.done");
          builder.CreateStore(range_lo, range_cur_slot);
          builder.CreateStore(
              llvm::ConstantInt::getFalse(emitter.GetContext()),
              range_done_slot);
        }
        else
        {
          if (loop.iter_value.has_value() && active_ctx)
          {
            llvm::Value *iter_value = EvaluateOrDefault(*loop.iter_value);
            analysis::TypeRef iter_type =
                active_ctx->LookupValueType(*loop.iter_value);
            if (!iter_type &&
                loop.iter_value->kind == IRValue::Kind::Local)
            {
              iter_type = emitter.LookupLocalType(loop.iter_value->name);
            }
            if (analysis::TypeRef stripped = analysis::StripPerm(iter_type))
            {
              iter_type = stripped;
            }
            if (iter_value &&
                EmitSeqIterInit(
                    emitter,
                    entry_builder,
                    builder,
                    iter_type,
                    iter_value,
                    indexed_iter.emplace()))
            {
            }
            else
            {
              indexed_iter.reset();
            }
          }
        }
        if (debug_loop_iter)
        {
          const std::string fn_name = func ? func->getName().str() : std::string("<no-func>");
          std::cerr << "[loop-iter-debug] fn=" << fn_name
                    << " indexed_iter=" << (indexed_iter.has_value() ? "yes" : "no")
                    << " range_iter=" << (range_iter ? "yes" : "no")
                    << "\n";
        }

        if ((iter_is_range_type && !range_iter) ||
            (!range_iter && !indexed_iter.has_value()) ||
            (range_iter && trip_count.has_value() && *trip_count == 0))
        {
          builder.CreateBr(loop_end);
        }
        else
        {

          auto bind_iter_identifier =
              [&](std::string_view name, llvm::Value *elem_value)
          {
            if (name.empty() || !elem_value)
            {
              return;
            }
            llvm::Value *local = emitter.GetLocal(std::string(name));
            llvm::AllocaInst *slot = llvm::dyn_cast_or_null<llvm::AllocaInst>(local);
            if (!slot)
            {
              llvm::IRBuilder<> entry_builder(
                  &func->getEntryBlock(),
                  func->getEntryBlock().begin());
              std::string slot_name = std::string(name) + ".iter";
              slot = entry_builder.CreateAlloca(
                  elem_value->getType(),
                  nullptr,
                  slot_name);
              emitter.RegisterLocalBindStorage(std::string(name), slot);
              if (iter_elem_type)
              {
                emitter.SetLocalType(std::string(name), iter_elem_type);
              }
              if (debug_loop_iter)
              {
                std::cerr << "[loop-iter-debug] bind-alloc name=" << name
                          << " llvm_ty=" << (elem_value->getType()->isIntegerTy() ? "int" : elem_value->getType()->isFloatingPointTy() ? "float"
                                                                                        : elem_value->getType()->isPointerTy()         ? "ptr"
                                                                                        : elem_value->getType()->isStructTy()          ? "struct"
                                                                                                                                       : "other")
                          << "\n";
              }
            }

            llvm::Type *dst_ty = slot->getAllocatedType();
            llvm::Value *stored = elem_value;
            const analysis::TypeRef dst_type = emitter.LookupLocalType(std::string(name));
            if (dst_type && iter_elem_type)
            {
              llvm::Value *coerced = CoerceToTyped(
                  emitter,
                  &builder,
                  stored,
                  dst_ty,
                  iter_elem_type,
                  dst_type);
              stored = coerced ? coerced : llvm::Constant::getNullValue(dst_ty);
            }
            else if (stored->getType() != dst_ty)
            {
              llvm::Value *coerced = CoerceTo(&builder, stored, dst_ty);
              stored = coerced ? coerced : llvm::Constant::getNullValue(dst_ty);
            }
            builder.CreateStore(stored, slot);
            if (debug_loop_iter)
            {
              std::cerr << "[loop-iter-debug] bind-ok name=" << name << "\n";
            }
          };

          auto bind_iter_pattern_value = [&](llvm::Value *elem_value)
          {
            if (!loop.pattern || !elem_value)
            {
              return;
            }
            std::visit(
                [&](const auto &pat)
                {
                  using P = std::decay_t<decltype(pat)>;
                  if constexpr (std::is_same_v<P, IRIdentifierPattern>)
                  {
                    bind_iter_identifier(pat.name, elem_value);
                  }
                  else if constexpr (std::is_same_v<P, IRTypedPattern>)
                  {
                    bind_iter_identifier(pat.name, elem_value);
                  }
                  else if constexpr (std::is_same_v<P, IRWildcardPattern>)
                  {
                    return;
                  }
                },
                loop.pattern->node);
          };

          auto *loop_cond = llvm::BasicBlock::Create(emitter.GetContext(), "loop.cond", func);
          auto *loop_body = llvm::BasicBlock::Create(emitter.GetContext(), "loop.body", func);
          auto *loop_inc = llvm::BasicBlock::Create(emitter.GetContext(), "loop.inc", func);
          builder.CreateBr(loop_cond);

          builder.SetInsertPoint(loop_cond);
          llvm::Value *in_range = nullptr;
          if (range_iter)
          {
            llvm::Value *done = builder.CreateLoad(
                llvm::Type::getInt1Ty(emitter.GetContext()),
                range_done_slot);
            llvm::Value *cur = builder.CreateLoad(range_bound_ty, range_cur_slot);
            switch (range_iter_kind)
            {
            case IRRangeKind::Exclusive:
            {
              llvm::Value *at_hi =
                  EmitBuiltinEqCall(builder, iter_elem_type, cur, range_hi);
              if (!at_hi)
              {
                if (active_ctx)
                {
                  const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
                }
                in_range = llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              else
              {
                in_range = builder.CreateAnd(
                    builder.CreateNot(done),
                    builder.CreateNot(at_hi));
              }
              break;
            }
            case IRRangeKind::Inclusive:
              in_range = builder.CreateNot(done);
              break;
            case IRRangeKind::From:
              in_range = builder.CreateNot(done);
              break;
            case IRRangeKind::To:
            case IRRangeKind::ToInclusive:
            case IRRangeKind::Full:
              in_range = llvm::ConstantInt::getFalse(emitter.GetContext());
              break;
            }
          }
          else
          {
            if (indexed_iter.has_value())
            {
              in_range = EmitSeqIterNext(emitter, builder, *indexed_iter);
            }
            else
            {
              in_range = llvm::ConstantInt::getFalse(emitter.GetContext());
            }
          }
          builder.CreateCondBr(in_range, loop_body, loop_end);

          builder.SetInsertPoint(loop_body);
          if (range_iter)
          {
            bind_iter_pattern_value(builder.CreateLoad(range_bound_ty, range_cur_slot));
          }
          else if (indexed_iter.has_value())
          {
            if (llvm::Value *elem = LoadSeqIterElem(builder, *indexed_iter))
            {
              bind_iter_pattern_value(elem);
            }
          }
          emitter.PushLoopTargets(loop_end, loop_inc, loop_result_slot, loop_result_type);
          emitter.EmitIR(loop.body_ir);
          emitter.PopLoopTargets();
          if (!builder.GetInsertBlock()->getTerminator())
          {
            builder.CreateBr(loop_inc);
          }

          builder.SetInsertPoint(loop_inc);
          if (range_iter)
          {
            llvm::Value *cur = builder.CreateLoad(range_bound_ty, range_cur_slot);
            const auto successor =
                EmitBuiltinSuccessor(builder, iter_elem_type, cur);
            if (!successor.has_value())
            {
              if (active_ctx)
              {
                const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
              }
              builder.CreateStore(
                  llvm::ConstantInt::getTrue(emitter.GetContext()),
                  range_done_slot);
            }
            else
            {
              llvm::Value *next_or_cur = builder.CreateSelect(
                  successor->has_next,
                  successor->next,
                  cur);
              switch (range_iter_kind)
              {
              case IRRangeKind::Exclusive:
                builder.CreateStore(
                    builder.CreateNot(successor->has_next),
                    range_done_slot);
                builder.CreateStore(next_or_cur, range_cur_slot);
                break;
              case IRRangeKind::Inclusive:
              {
                llvm::Value *at_hi =
                    EmitBuiltinEqCall(builder, iter_elem_type, cur, range_hi);
                if (!at_hi)
                {
                  if (active_ctx)
                  {
                    const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
                  }
                  builder.CreateStore(
                      llvm::ConstantInt::getTrue(emitter.GetContext()),
                      range_done_slot);
                }
                else
                {
                  llvm::Value *done_next = builder.CreateOr(
                      at_hi,
                      builder.CreateNot(successor->has_next));
                  builder.CreateStore(done_next, range_done_slot);
                  builder.CreateStore(next_or_cur, range_cur_slot);
                }
                break;
              }
              case IRRangeKind::From:
                builder.CreateStore(
                    builder.CreateNot(successor->has_next),
                    range_done_slot);
                builder.CreateStore(next_or_cur, range_cur_slot);
                break;
              case IRRangeKind::To:
              case IRRangeKind::ToInclusive:
              case IRRangeKind::Full:
                builder.CreateStore(
                    llvm::ConstantInt::getTrue(emitter.GetContext()),
                    range_done_slot);
                break;
              }
            }
          }
          else
          {
          }
          builder.CreateBr(loop_cond);
        }
      }
    }
  }

  builder.SetInsertPoint(loop_end);
  if (loop_result_slot && loop_result_ty && !loop_result_ty->isVoidTy())
  {
    if (loop.result.kind == IRValue::Kind::Opaque &&
        IsAddressBackedAggregateType(loop_result_ty))
    {
      llvm::Value *typed_ptr = loop_result_slot;
      llvm::Type *expected_ptr_ty = llvm::PointerType::get(loop_result_ty, 0);
      if (typed_ptr->getType() != expected_ptr_ty)
      {
        typed_ptr = builder.CreateBitCast(typed_ptr, expected_ptr_ty);
      }
      emitter.ForgetTempStorage(loop.result);
      emitter.SetTempStorage(loop.result, typed_ptr);
    }
    else
    {
      emitter.SetTempValue(loop.result, builder.CreateLoad(loop_result_ty, loop_result_slot));
    }
  }
  else
  {
    emitter.SetTempValue(loop.result, DefaultFor(loop.result));
  }
}

} // namespace cursive::codegen::emit_detail
