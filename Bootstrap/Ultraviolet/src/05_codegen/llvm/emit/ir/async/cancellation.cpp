// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/cancellation.cpp
// Canonical owner for LLVM IR async cancellation instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

llvm::Value *cancelTokenAddress(const IRInstructionVisitor &visitor,
                                const IRValue &token)
{
  llvm::Type *ptr_ty = visitor.emitter.GetOpaquePtr();
  llvm::Value *token_ptr = visitor.emitter.GetAddressableStorage(token);
  if (!token_ptr)
  {
    token_ptr = visitor.EvaluateOrDefault(token);
  }
  if (token_ptr && !token_ptr->getType()->isPointerTy())
  {
    if (LowerCtx *ctx = visitor.emitter.GetCurrentCtx())
    {
      ctx->ReportCodegenFailure();
    }
    token_ptr = nullptr;
  }
  token_ptr = token_ptr ? CoerceTo(&visitor.builder, token_ptr, ptr_ty) : nullptr;
  if (!token_ptr)
  {
    token_ptr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_ty));
  }
  return token_ptr;
}

struct CancelRuntimeCallResult {
  llvm::Value *value = nullptr;
  llvm::Value *storage = nullptr;
};

CancelRuntimeCallResult emitCancelRuntimeCall(
    const IRInstructionVisitor &visitor,
    const std::string &symbol,
    const std::vector<llvm::Value *> &args,
    const IRValue &result)
{
  CancelRuntimeCallResult call_result;
  if (std::optional<RuntimeFuncInfo> info = GetRuntimeFuncInfo(symbol))
  {
    llvm::Function *fn = visitor.emitter.GetModule().getFunction(symbol);
    const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(symbol);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(symbol);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!fn)
    {
      ABICallResult abi = ComputeCallABI(
          visitor.emitter,
          info->params,
          info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (abi.func_type)
      {
        fn = llvm::Function::Create(
            abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            symbol,
            &visitor.emitter.GetModule());
        fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (fn)
    {
      llvm::Value *preferred_result_storage =
          visitor.emitter.TakePreferredResultStorage(result);
      call_result.value = EmitABICall(
          visitor.emitter,
          &visitor.builder,
          fn,
          info->params,
          info->ret,
          args,
          use_c_abi_aggregate_sret,
          /*ffi_import_boundary=*/false,
          /*ffi_import_catch=*/false,
          std::nullopt,
          nullptr,
          &call_result.storage,
          preferred_result_storage,
          runtime_foreign_boundary,
          RuntimeUsesExplicitOutResultABI(symbol));
    }
  }
  return call_result;
}

void setCancelResult(const IRInstructionVisitor &visitor,
                     const IRValue &result,
                     CancelRuntimeCallResult call_result)
{
  if (call_result.storage)
  {
    visitor.emitter.SetTempStorage(result, call_result.storage);
  }
  if (!call_result.value && !call_result.storage)
  {
    call_result.value = visitor.DefaultFor(result);
  }
  if (call_result.value)
  {
    if (llvm::Type *expected = visitor.ExpectedLLVMType(result))
    {
      if (llvm::Value *coerced =
              CoerceTo(&visitor.builder, call_result.value, expected))
      {
        call_result.value = coerced;
      }
    }
    visitor.emitter.SetTempValue(result, call_result.value);
  }
}

} // namespace

void IRInstructionVisitor::operator()(const IRCancelCreate &create) const
{
  SPEC_RULE("def.20.CancelIR");
  SPEC_RULE("rule.20.Lower-Cancel-New");

  CancelRuntimeCallResult out = emitCancelRuntimeCall(
      *this,
      BuiltinSymCancelTokenNew(),
      {},
      create.result);
  setCancelResult(*this, create.result, out);
}

void IRInstructionVisitor::operator()(const IRCancelRequest &request) const
{
  SPEC_RULE("def.20.CancelIR");
  SPEC_RULE("rule.20.Lower-Cancel-Request");

  llvm::Value *token = cancelTokenAddress(*this, request.token);
  CancelRuntimeCallResult out = emitCancelRuntimeCall(
      *this,
      BuiltinSymCancelTokenActiveCancel(),
      {token},
      request.result);
  setCancelResult(*this, request.result, out);
}

void IRInstructionVisitor::operator()(const IRCancelWait &wait) const
{
  SPEC_RULE("def.20.CancelIR");
  SPEC_RULE("rule.20.Lower-Cancel-Wait");
  SPEC_RULE("requirement.20.CancellationCheckpointLowering");

  llvm::Value *token = cancelTokenAddress(*this, wait.token);
  CancelRuntimeCallResult out = emitCancelRuntimeCall(
      *this,
      BuiltinSymCancelTokenActiveWaitCancelled(),
      {token},
      wait.result);
  setCancelResult(*this, wait.result, out);
}

void IRInstructionVisitor::operator()(const IRCancelCheck &check) const
{
  SPEC_RULE("def.20.CancelIR");
  SPEC_RULE("requirement.20.CancellationCheckpointLowering");

  llvm::Type *i1_ty = llvm::Type::getInt1Ty(emitter.GetContext());
  llvm::Value *token = cancelTokenAddress(*this, check.token);

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
  SPEC_RULE("def.20.CancelIR");
  SPEC_RULE("requirement.20.SpawnDispatchCancellationLowering");

  // Runtime scheduling already suppresses dequeued-but-unstarted
  // cancelled work before wrapper body execution. This IR node exists
  // to preserve the explicit lowering surface required by the spec.
}

} // namespace ultraviolet::codegen::emit_detail
