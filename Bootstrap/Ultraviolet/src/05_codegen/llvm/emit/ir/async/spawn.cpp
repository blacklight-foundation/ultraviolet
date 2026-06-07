// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/spawn.cpp
// Canonical owner for LLVM IR spawn instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRSpawn &spawn) const
{
  SPEC_RULE("rule.20.Lower-Expr-Spawn");

  emitter.EmitIR(spawn.captured_env);

  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  auto *ptr_ptr_ty = llvm::cast<llvm::PointerType>(ptr_ty);
  llvm::Type *usize_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Type *i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
  llvm::FunctionType *body_fn_ty = llvm::FunctionType::get(
      llvm::Type::getVoidTy(emitter.GetContext()),
      {ptr_ty, ptr_ty, ptr_ty, ptr_ty},
      false);
  llvm::Type *body_fn_ptr_ty = llvm::PointerType::get(body_fn_ty, 0);

  auto materialize_runtime_receiver_pointer =
      [&](const IRValue &receiver_value) -> llvm::Value *
  {
    if (llvm::Value *storage = emitter.GetAddressableStorage(receiver_value))
    {
      if (llvm::Value *coerced = CoerceTo(&builder, storage, ptr_ty))
      {
        return coerced;
      }
      if (storage->getType()->isPointerTy())
      {
        return builder.CreateBitCast(storage, ptr_ty);
      }
    }

    llvm::Value *receiver = EvaluateOrDefault(receiver_value);
    if (!receiver)
    {
      return llvm::ConstantPointerNull::get(ptr_ptr_ty);
    }
    if (receiver->getType()->isPointerTy())
    {
      if (llvm::Value *coerced = CoerceTo(&builder, receiver, ptr_ty))
      {
        return coerced;
      }
      return builder.CreateBitCast(receiver, ptr_ty);
    }

    llvm::Function *func =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (!func)
    {
      return llvm::ConstantPointerNull::get(ptr_ptr_ty);
    }
    llvm::IRBuilder<> entry_builder(
        &func->getEntryBlock(),
        func->getEntryBlock().begin());
    llvm::AllocaInst *receiver_slot =
        entry_builder.CreateAlloca(receiver->getType(), nullptr, "reactor.receiver");
    builder.CreateStore(receiver, receiver_slot);
    if (llvm::Value *coerced = CoerceTo(&builder, receiver_slot, ptr_ty))
    {
      return coerced;
    }
    return builder.CreateBitCast(receiver_slot, ptr_ty);
  };

  llvm::Value *env_ptr = CoerceTo(&builder, EvaluateOrDefault(spawn.env_ptr), ptr_ty);
  if (!env_ptr)
  {
    env_ptr = llvm::ConstantPointerNull::get(ptr_ptr_ty);
  }
  llvm::Value *env_size = CoerceTo(&builder, EvaluateOrDefault(spawn.env_size), usize_ty);
  if (!env_size)
  {
    env_size = llvm::ConstantInt::get(usize_ty, 0);
  }
  llvm::Value *body_fn = EvaluateOrDefault(spawn.body_fn);
  body_fn = CoerceTo(&builder, body_fn, body_fn_ptr_ty);
  if (!body_fn)
  {
    body_fn = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(body_fn_ptr_ty));
  }
  llvm::Value *result_size =
      CoerceTo(&builder, EvaluateOrDefault(spawn.result_size), usize_ty);
  if (!result_size)
  {
    result_size = llvm::ConstantInt::get(usize_ty, 0);
  }
  llvm::Value *hosted_env = emitter.GetHostedCurrentEnvPtr();
  hosted_env = CoerceTo(&builder, hosted_env, ptr_ty);
  if (!hosted_env)
  {
    hosted_env = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_ty));
  }
  llvm::Value *affinity_mask = llvm::ConstantInt::get(usize_ty, 0);
  if (spawn.affinity_mask.has_value())
  {
    affinity_mask =
        CoerceTo(&builder, EvaluateOrDefault(*spawn.affinity_mask), usize_ty);
    if (!affinity_mask)
    {
      affinity_mask = llvm::ConstantInt::get(usize_ty, 0);
    }
  }
  llvm::Value *priority_hint = llvm::ConstantInt::get(i32_ty, -1, true);
  if (spawn.priority.has_value())
  {
    priority_hint =
        CoerceTo(&builder, EvaluateOrDefault(*spawn.priority), i32_ty);
    if (!priority_hint)
    {
      priority_hint = llvm::ConstantInt::get(i32_ty, -1, true);
    }
  }

  llvm::Value *handle = nullptr;
  if (spawn.runtime_symbol.has_value())
  {
    const std::string &runtime_sym = *spawn.runtime_symbol;
    if (std::optional<RuntimeFuncInfo> runtime_info = GetRuntimeFuncInfo(runtime_sym))
    {
      llvm::Function *runtime_fn = emitter.GetModule().getFunction(runtime_sym);
      const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(runtime_sym);
      const bool runtime_foreign_boundary = RuntimeUsesForeignABI(runtime_sym);
      const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
      if (!runtime_fn)
      {
        ABICallResult runtime_abi = ComputeCallABI(
            emitter,
            runtime_info->params,
            runtime_info->ret,
            use_c_abi_aggregate_sret,
            /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
        if (runtime_abi.func_type)
        {
          runtime_fn = llvm::Function::Create(
              runtime_abi.func_type,
              llvm::GlobalValue::ExternalLinkage,
              runtime_sym,
              &emitter.GetModule());
          runtime_fn->setCallingConv(llvm::CallingConv::C);
        }
      }
      if (runtime_fn)
      {
        std::vector<llvm::Value *> runtime_args;
        runtime_args.reserve(2);
        llvm::Value *receiver = spawn.runtime_receiver.has_value()
                                    ? materialize_runtime_receiver_pointer(
                                          *spawn.runtime_receiver)
                                    : nullptr;
        if (!receiver)
        {
          receiver = llvm::ConstantPointerNull::get(ptr_ptr_ty);
        }
        runtime_args.push_back(receiver);
        runtime_args.push_back(env_ptr);
        handle = EmitABICall(
            emitter,
            &builder,
            runtime_fn,
            runtime_info->params,
            runtime_info->ret,
            runtime_args,
            use_c_abi_aggregate_sret,
            /*ffi_import_boundary=*/false,
            /*ffi_import_catch=*/false,
            std::nullopt,
            nullptr,
            nullptr,
            nullptr,
            runtime_foreign_boundary);
      }
    }
  }
  else
  {
    const std::string spawn_sym = ConcurrencySymSpawnCreate();
    if (std::optional<RuntimeFuncInfo> spawn_info = GetRuntimeFuncInfo(spawn_sym))
    {
      llvm::Function *spawn_fn = emitter.GetModule().getFunction(spawn_sym);
      const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(spawn_sym);
      const bool runtime_foreign_boundary = RuntimeUsesForeignABI(spawn_sym);
      const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
      if (!spawn_fn)
      {
        ABICallResult spawn_abi = ComputeCallABI(
            emitter,
            spawn_info->params,
            spawn_info->ret,
            use_c_abi_aggregate_sret,
            /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
        if (spawn_abi.func_type)
        {
          spawn_fn = llvm::Function::Create(
              spawn_abi.func_type,
              llvm::GlobalValue::ExternalLinkage,
              spawn_sym,
              &emitter.GetModule());
          spawn_fn->setCallingConv(llvm::CallingConv::C);
        }
      }
      if (spawn_fn)
      {
        std::vector<llvm::Value *> spawn_args;
        spawn_args.reserve(7);
        spawn_args.push_back(env_ptr);
        spawn_args.push_back(env_size);
        spawn_args.push_back(body_fn);
        spawn_args.push_back(hosted_env);
        spawn_args.push_back(result_size);
        spawn_args.push_back(affinity_mask);
        spawn_args.push_back(priority_hint);
        handle = EmitABICall(
            emitter,
            &builder,
            spawn_fn,
            spawn_info->params,
            spawn_info->ret,
            spawn_args,
            use_c_abi_aggregate_sret,
            /*ffi_import_boundary=*/false,
            /*ffi_import_catch=*/false,
            std::nullopt,
            nullptr,
            nullptr,
            nullptr,
            runtime_foreign_boundary);
      }
    }
  }
  if (!handle)
  {
    handle = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  if (llvm::Type *expected = ExpectedLLVMType(spawn.result))
  {
    if (llvm::Value *coerced = CoerceTo(&builder, handle, expected))
    {
      handle = coerced;
    }
  }
  emitter.SetTempValue(spawn.result, handle);
}

} // namespace ultraviolet::codegen::emit_detail
