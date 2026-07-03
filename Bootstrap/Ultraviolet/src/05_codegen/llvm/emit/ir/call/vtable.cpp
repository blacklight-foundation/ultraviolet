// =============================================================================
// File: 05_codegen/llvm/emit/ir/call/vtable.cpp
// Canonical owner for LLVM IR vtable call instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

void ReportCodegenFailure(LLVMEmitter &emitter)
{
  if (const LowerCtx *ctx = emitter.GetCurrentCtx())
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

void RecordDynamicReceiverAddressStateCheck()
{
  SPEC_RULE("rule.15.EvalRecvSigma-Ref-Dyn");
  SPEC_RULE("rule.15.EvalRecvSigma-Ref-Dyn-Expired");
  if (!core::Conformance::Enabled())
  {
    return;
  }

  const std::string payload =
      "source=EmitIRCallVTable;operation=EvalRecvSigma;receiver=Dyn;"
      "mode=ref;data=RawPtrImm;state_check=DynAddrState;"
      "valid=continue;expired=panic;panic=ExpiredDeref;"
      "runtime_check=region.addr_is_active;call_skipped_on_expired=true";
  core::Conformance::Record(
      "rule.15.EvalRecvSigma-Ref-Dyn", std::nullopt, payload);
  core::Conformance::Record(
      "rule.15.EvalRecvSigma-Ref-Dyn-Expired", std::nullopt, payload);
}

} // namespace

void IRInstructionVisitor::operator()(const IRCallVTable &call) const
{
  const bool debug_vtable_call = core::IsDebugEnabled("obj");
  // Evaluate the dense pointer (base): {data_ptr, vtable_ptr}
  llvm::Value *dense_ptr = EvaluateOrDefault(call.base);
  if (!dense_ptr)
  {
    if (debug_vtable_call)
    {
      std::fprintf(stderr, "[vtable-call] dense_ptr is null for slot=%zu\n",
                   call.slot);
    }
    emitter.SetTempValue(call.result, DefaultFor(call.result));
    return;
  }

  // Debug: print the type of the evaluated dense pointer
  if (debug_vtable_call)
  {
    std::string type_str;
    llvm::raw_string_ostream os(type_str);
    dense_ptr->getType()->print(os);
    os.flush();
    std::fprintf(stderr, "[vtable-call] slot=%zu base_kind=%d base_name=%s dense_ptr_type=%s\n",
                 call.slot,
                 static_cast<int>(call.base.kind),
                 call.base.name.c_str(),
                 type_str.c_str());
  }

  // The dense pointer should be a struct {ptr, ptr}.  If instead we got a
  // pointer (e.g. ByRef parameter), load through it first.
  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  if (dense_ptr->getType()->isPointerTy())
  {
    llvm::StructType *dyn_ty = GetDynamicType(emitter.GetContext());
    dense_ptr = builder.CreateLoad(dyn_ty, dense_ptr);
    if (debug_vtable_call)
    {
      std::fprintf(stderr,
                   "[vtable-call]   loaded dense_ptr through pointer\n");
    }
  }

  auto *dense_struct_ty = llvm::dyn_cast<llvm::StructType>(dense_ptr->getType());
  if (!dense_struct_ty || dense_struct_ty->getNumElements() < 2)
  {
    if (debug_vtable_call)
    {
      std::fprintf(
          stderr,
          "[vtable-call]   FAIL: dense_ptr is not a struct with >=2 elements\n");
    }
    emitter.SetTempValue(call.result, DefaultFor(call.result));
    return;
  }

  llvm::Value *data_ptr = builder.CreateExtractValue(dense_ptr, {0});
  llvm::Value *vtable_ptr = builder.CreateExtractValue(dense_ptr, {1});

  // Spec vtables carry a 3-word header:
  // [sizeof(T), alignof(T), DropGlueSym(T)] ++ method slots.
  // Dynamic dispatch therefore indexes method slot i at header_offset+i.
  llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  const std::size_t vtable_slot_index = call.slot + 3;
  llvm::Value *slot_ptr = builder.CreateGEP(
      ptr_ty,
      vtable_ptr,
      llvm::ConstantInt::get(i64_ty, vtable_slot_index));
  llvm::Value *fn_ptr = builder.CreateLoad(ptr_ty, slot_ptr);

  if (debug_vtable_call)
  {
    std::fprintf(stderr, "[vtable-call]   call_args count=%zu\n",
                 call.args.size() + 1);
  }

  // Build the function type: (ptr, arg_types...) -> ret_ty
  // Use the return type from the IRCallVTable (populated by LowerDynCall),
  // fall back to LookupValueType, then try the vtable global entries.
  llvm::Type *ret_ty = nullptr;
  if (call.ret_type)
  {
    ret_ty = emitter.GetLLVMType(call.ret_type);
  }
  if (!ret_ty)
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    if (active_ctx)
    {
      if (analysis::TypeRef result_type = active_ctx->LookupValueType(call.result))
      {
        ret_ty = emitter.GetLLVMType(result_type);
      }
    }
  }
  if (!ret_ty)
  {
    // Last resort: try to find the actual function at this vtable slot
    // by examining the vtable global's initializer.
    auto try_vtable_global = [&]() -> llvm::Type *
    {
      // The base is a DynLit whose DerivedValueInfo contains the vtable symbol.
      const LowerCtx *vtable_ctx = emitter.GetCurrentCtx();
      if (!vtable_ctx)
      {
        return nullptr;
      }
      const DerivedValueInfo *derived = vtable_ctx->LookupDerivedValue(call.base);
      if (!derived || derived->vtable_sym.empty())
      {
        return nullptr;
      }
      auto *gv = emitter.GetModule().getNamedGlobal(derived->vtable_sym);
      if (!gv || !gv->hasInitializer())
      {
        return nullptr;
      }
      auto *init = gv->getInitializer();
      auto *arr = llvm::dyn_cast<llvm::ConstantArray>(init);
      llvm::Value *slot_val = nullptr;
      const std::size_t vtable_slot_index = call.slot + 3;
      if (auto *st = llvm::dyn_cast<llvm::ConstantStruct>(init))
      {
        if (vtable_slot_index >= st->getNumOperands())
        {
          return nullptr;
        }
        slot_val = st->getOperand(static_cast<unsigned>(vtable_slot_index));
      }
      else if (auto *arr = llvm::dyn_cast<llvm::ConstantArray>(init))
      {
        if (vtable_slot_index >= arr->getNumOperands())
        {
          return nullptr;
        }
        slot_val = arr->getOperand(static_cast<unsigned>(vtable_slot_index));
      }
      if (!slot_val)
      {
        return nullptr;
      }
      slot_val = slot_val->stripPointerCasts();
      if (auto *fn = llvm::dyn_cast<llvm::Function>(slot_val))
      {
        return fn->getReturnType();
      }
      return nullptr;
    };
    ret_ty = try_vtable_global();
  }
  if (!ret_ty)
  {
    if (debug_vtable_call)
    {
      std::fprintf(
          stderr,
          "[vtable-call]   FAIL: unresolved vtable return type\n");
    }
    if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
    {
      const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
    }
    emitter.SetTempValue(call.result, DefaultFor(call.result));
    return;
  }
  if (debug_vtable_call)
  {
    std::string rty_str;
    llvm::raw_string_ostream os(rty_str);
    ret_ty->print(os);
    os.flush();
    std::fprintf(stderr, "[vtable-call]   ret_ty=%s has_ir_ret=%d\n",
                 rty_str.c_str(), call.ret_type ? 1 : 0);
  }

  auto emit_raw_vtable_call = [&]() -> llvm::Value *
  {
    std::vector<llvm::Value *> raw_call_args;
    raw_call_args.push_back(data_ptr);
    for (const auto &arg : call.args)
    {
      if (arg.kind == IRValue::Kind::Local &&
          arg.name == std::string(kPanicOutName))
      {
        if (llvm::Value *panic_slot =
                emitter.GetLocal(std::string(kPanicOutName)))
        {
          raw_call_args.push_back(panic_slot);
          continue;
        }
      }
      raw_call_args.push_back(EvaluateOrDefault(arg));
    }

    std::vector<llvm::Type *> param_tys;
    param_tys.reserve(raw_call_args.size());
    for (llvm::Value *arg : raw_call_args)
    {
      param_tys.push_back(arg ? arg->getType() : ptr_ty);
    }
    llvm::FunctionType *fn_ty =
        llvm::FunctionType::get(ret_ty, param_tys, false);
    llvm::CallInst *result = builder.CreateCall(fn_ty, fn_ptr, raw_call_args);
    if (result && !result->getType()->isVoidTy())
    {
      return result;
    }
    return nullptr;
  };

  auto emit_vtable_call = [&]() -> llvm::Value *
  {
    if (call.ret_type && !call.params.empty())
    {
      IRParam receiver_param;
      receiver_param.name = "__dyn_receiver";
      receiver_param.stable_name = receiver_param.name;
      receiver_param.type = analysis::MakeTypeRawPtr(
          analysis::RawPtrQual::Imm,
          analysis::MakeTypePrim("u8"));

      std::vector<IRParam> abi_params;
      abi_params.reserve(call.params.size() + 1);
      abi_params.push_back(std::move(receiver_param));
      abi_params.insert(abi_params.end(), call.params.begin(), call.params.end());

      std::vector<llvm::Value *> abi_args(call.args.size() + 1, nullptr);
      abi_args[0] = data_ptr;
      return EmitABICall(emitter,
                         &builder,
                         fn_ptr,
                         abi_params,
                         call.ret_type,
                         abi_args,
                         false,
                         false,
                         false,
                         std::nullopt,
                         &call.args);
    }
    return emit_raw_vtable_call();
  };

  if (call.check_dynamic_receiver_addr_active)
  {
    RecordDynamicReceiverAddressStateCheck();
    llvm::Value *active = EmitRuntimeCallBySymbol(
        emitter,
        &builder,
        BuiltinModalSymRegionAddrIsActive(),
        {data_ptr});
    if (!active)
    {
      ReportCodegenFailure(emitter);
      emitter.SetTempValue(call.result, DefaultFor(call.result));
      return;
    }

    llvm::Value *active_condition = AsBool(&builder, active);
    llvm::Function *func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *valid_bb = llvm::BasicBlock::Create(
        emitter.GetContext(), "dynrecv.addr.valid", func);
    llvm::BasicBlock *expired_bb = llvm::BasicBlock::Create(
        emitter.GetContext(), "dynrecv.addr.expired", func);
    llvm::BasicBlock *join_bb = llvm::BasicBlock::Create(
        emitter.GetContext(), "dynrecv.addr.join", func);

    builder.CreateCondBr(active_condition, valid_bb, expired_bb);

    builder.SetInsertPoint(expired_bb);
    StorePanicRecord(emitter, &builder, PanicCode(PanicReason::ExpiredDeref));
    builder.CreateBr(join_bb);

    builder.SetInsertPoint(valid_bb);
    llvm::Value *valid_result = emit_vtable_call();
    builder.CreateBr(join_bb);

    builder.SetInsertPoint(join_bb);
    if (valid_result && !ret_ty->isVoidTy())
    {
      llvm::PHINode *phi = builder.CreatePHI(ret_ty, 2, "dynrecv.result");
      phi->addIncoming(valid_result, valid_bb);
      phi->addIncoming(llvm::Constant::getNullValue(ret_ty), expired_bb);
      emitter.SetTempValue(call.result, phi);
    }
    else
    {
      emitter.SetTempValue(call.result, DefaultFor(call.result));
    }
    return;
  }

  llvm::Value *result = emit_vtable_call();
  if (result)
  {
    emitter.SetTempValue(call.result, result);
    return;
  }
  emitter.SetTempValue(call.result, DefaultFor(call.result));
}

} // namespace ultraviolet::codegen::emit_detail
