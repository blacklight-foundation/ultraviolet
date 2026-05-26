// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/parallel.cpp
// Canonical owner for LLVM IR parallel instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRParallel &parallel) const
{
  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  llvm::Type *usize_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Type *i8_ptr_ty =
      llvm::PointerType::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0);
  llvm::Value *domain = EvaluateOrDefault(parallel.domain);
  if (!domain)
  {
    domain = llvm::Constant::getNullValue(GetDynamicType(emitter.GetContext()));
  }
  llvm::Value *cancel_token = parallel.cancel_token.has_value()
                                  ? EvaluateOrDefault(*parallel.cancel_token)
                                  : llvm::Constant::getAllOnesValue(usize_ty);
  cancel_token = CoerceTo(&builder, cancel_token, usize_ty);
  if (!cancel_token)
  {
    cancel_token = llvm::Constant::getAllOnesValue(usize_ty);
  }
  llvm::Value *name_ptr = llvm::ConstantPointerNull::get(
      llvm::cast<llvm::PointerType>(i8_ptr_ty));
  if (!parallel.name.empty())
  {
    name_ptr = builder.CreateGlobalStringPtr(parallel.name);
  }

  llvm::Value *ctx_ptr = nullptr;
  const std::string begin_sym = ConcurrencySymParallelBegin();
  if (std::optional<RuntimeFuncInfo> begin_info = GetRuntimeFuncInfo(begin_sym))
  {
    llvm::Function *begin_fn = emitter.GetModule().getFunction(begin_sym);
    const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(begin_sym);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(begin_sym);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!begin_fn)
    {
      ABICallResult begin_abi = ComputeCallABI(
          emitter,
          begin_info->params,
          begin_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (begin_abi.func_type)
      {
        begin_fn = llvm::Function::Create(
            begin_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            begin_sym,
            &emitter.GetModule());
        begin_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (begin_fn)
    {
      std::vector<llvm::Value *> begin_args;
      begin_args.reserve(3);
      begin_args.push_back(domain);
      begin_args.push_back(cancel_token);
      begin_args.push_back(name_ptr);
      ctx_ptr = EmitABICall(
          emitter,
          &builder,
          begin_fn,
          begin_info->params,
          begin_info->ret,
          begin_args,
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
  if (!ctx_ptr)
  {
    ctx_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  emitter.SetTempValue(parallel.result, ctx_ptr);
  emitter.PushParallelContext(parallel.result);
  emitter.EmitIR(parallel.body);
  emitter.PopParallelContext();
}

} // namespace ultraviolet::codegen::emit_detail
