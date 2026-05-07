// =============================================================================
// File: 05_codegen/llvm/emit/ir/call/vtable.cpp
// Canonical owner for LLVM IR vtable call instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

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

  // Build call arguments: (data_ptr, ...user_args_including_panic_out)
  // Note: call.args already includes the panic out-parameter appended by
  // method_call.cpp, so we must NOT add it again here.
  std::vector<llvm::Value *> call_args;
  call_args.push_back(data_ptr);
  for (const auto &arg : call.args)
  {
    call_args.push_back(EvaluateOrDefault(arg));
  }

  if (debug_vtable_call)
  {
    std::fprintf(stderr, "[vtable-call]   call_args count=%zu\n",
                 call_args.size());
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

  std::vector<llvm::Type *> param_tys;
  param_tys.reserve(call_args.size());
  for (llvm::Value *arg : call_args)
  {
    param_tys.push_back(arg ? arg->getType() : ptr_ty);
  }
  llvm::FunctionType *fn_ty = llvm::FunctionType::get(ret_ty, param_tys, false);

  llvm::CallInst *result = builder.CreateCall(fn_ty, fn_ptr, call_args);
  if (result && !result->getType()->isVoidTy())
  {
    emitter.SetTempValue(call.result, result);
  }
  else
  {
    emitter.SetTempValue(call.result, DefaultFor(call.result));
  }
}

} // namespace cursive::codegen::emit_detail
