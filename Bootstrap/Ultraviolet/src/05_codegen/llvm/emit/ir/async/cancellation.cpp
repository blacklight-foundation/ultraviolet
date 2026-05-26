// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/cancellation.cpp
// Canonical owner for LLVM IR async cancellation instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCancelCheck &check) const
{
  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  llvm::Type *i1_ty = llvm::Type::getInt1Ty(emitter.GetContext());
  llvm::Value *token = emitter.GetAddressableStorage(check.token);
  if (!token)
  {
    token = EvaluateOrDefault(check.token);
  }
  if (token && !token->getType()->isPointerTy())
  {
    if (LowerCtx *ctx = emitter.GetCurrentCtx())
    {
      ctx->ReportCodegenFailure();
    }
    token = nullptr;
  }
  token = token ? CoerceTo(&builder, token, ptr_ty) : nullptr;
  if (!token)
  {
    token = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  llvm::Value *out = nullptr;
  const std::string check_sym = BuiltinSymCancelTokenActiveIsCancelled();
  if (std::optional<RuntimeFuncInfo> check_info = GetRuntimeFuncInfo(check_sym))
  {
    llvm::Function *check_fn = emitter.GetModule().getFunction(check_sym);
    const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(check_sym);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(check_sym);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!check_fn)
    {
      ABICallResult check_abi = ComputeCallABI(
          emitter,
          check_info->params,
          check_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (check_abi.func_type)
      {
        check_fn = llvm::Function::Create(
            check_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            check_sym,
            &emitter.GetModule());
        check_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (check_fn)
    {
      std::vector<llvm::Value *> check_args;
      check_args.push_back(token);
      llvm::Value *raw = EmitABICall(
          emitter,
          &builder,
          check_fn,
          check_info->params,
          check_info->ret,
          check_args,
          use_c_abi_aggregate_sret,
          /*ffi_import_boundary=*/false,
          /*ffi_import_catch=*/false,
          std::nullopt,
          nullptr,
          nullptr,
          nullptr,
          runtime_foreign_boundary);
      out = CoerceTo(&builder, raw, i1_ty);
    }
  }
  if (!out)
  {
    out = llvm::ConstantInt::getFalse(i1_ty);
  }
  emitter.SetTempValue(check.result, out);
}

void IRInstructionVisitor::operator()(const IRCancelSuppress &) const
{
  // Runtime scheduling already suppresses dequeued-but-unstarted
  // cancelled work before wrapper body execution. This IR node exists
  // to preserve the explicit lowering surface required by the spec.
}

} // namespace ultraviolet::codegen::emit_detail
