// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/wait.cpp
// Canonical owner for LLVM IR wait instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

bool WaitHandleHasPath(const analysis::TypeRef &type,
                       bool (*predicate)(const analysis::TypePath &))
{
  if (!type)
  {
    return false;
  }
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped)
  {
    stripped = type;
  }
  const auto *path = analysis::AppliedTypePath(*stripped);
  return path && predicate(*path);
}

} // namespace

void IRInstructionVisitor::operator()(const IRWait &wait) const
{
  SPEC_RULE("requirement.21.WaitRuntimeSemantics");
  SPEC_RULE("def.21.WaitRuntimeHelpers");

  IRWaitKind wait_kind = wait.kind;
  if (wait_kind == IRWaitKind::Unknown)
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    const analysis::TypeRef handle_type =
        active_ctx ? active_ctx->LookupValueType(wait.handle) : nullptr;
    if (WaitHandleHasPath(handle_type, analysis::IsSpawnedTypePath))
    {
      wait_kind = IRWaitKind::Spawned;
    }
    else if (WaitHandleHasPath(handle_type, analysis::IsTrackedTypePath))
    {
      wait_kind = IRWaitKind::Tracked;
    }
  }

  if (wait_kind == IRWaitKind::Spawned)
  {
    SPEC_RULE("rule.21.EvalSigma-Wait-Spawned-Ready");
    SPEC_RULE("rule.21.EvalSigma-Wait-Spawned-Pending");
    SPEC_RULE("requirement.21.FailedSpawnedWaitHandledByParallelPanic");
  }
  else if (wait_kind == IRWaitKind::Tracked)
  {
    SPEC_RULE("rule.21.EvalSigma-Wait-Tracked-Ready");
    SPEC_RULE("rule.21.EvalSigma-Wait-Tracked-Pending");
  }
  SPEC_RULE("rule.21.EvalSigma-Wait-Ctrl");

  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  llvm::Value *handle = CoerceTo(&builder, EvaluateOrDefault(wait.handle), ptr_ty);
  if (!handle)
  {
    handle = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }
  llvm::Value *result_ptr = nullptr;
  const std::string wait_sym = ConcurrencySymSpawnWait();
  if (std::optional<RuntimeFuncInfo> wait_info = GetRuntimeFuncInfo(wait_sym))
  {
    llvm::Function *wait_fn = emitter.GetModule().getFunction(wait_sym);
    const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(wait_sym);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(wait_sym);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!wait_fn)
    {
      ABICallResult wait_abi = ComputeCallABI(
          emitter,
          wait_info->params,
          wait_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (wait_abi.func_type)
      {
        wait_fn = llvm::Function::Create(
            wait_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            wait_sym,
            &emitter.GetModule());
        wait_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (wait_fn)
    {
      std::vector<llvm::Value *> wait_args;
      wait_args.reserve(1);
      wait_args.push_back(handle);
      result_ptr = EmitABICall(
          emitter,
          &builder,
          wait_fn,
          wait_info->params,
          wait_info->ret,
          wait_args,
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
  if (!result_ptr)
  {
    result_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  llvm::Value *out = nullptr;
  llvm::Type *expected = ExpectedLLVMType(wait.result);
  if (core::IsDebugEnabled("wait"))
  {
    const LowerCtx *ctx = emitter.GetCurrentCtx();
    const std::string wait_type_text =
        (ctx && ctx->LookupValueType(wait.result))
            ? analysis::TypeToString(ctx->LookupValueType(wait.result))
            : std::string("<null>");
    const char *expected_kind =
        !expected ? "null" : expected->isIntegerTy() ? "int"
                         : expected->isPointerTy()   ? "ptr"
                         : expected->isStructTy()    ? "struct"
                         : expected->isArrayTy()     ? "array"
                         : expected->isVoidTy()      ? "void"
                                                     : "other";
    std::fprintf(stderr,
                 "[uv] irwait: result=%s expected=%s type=%s llvm=%s\n",
                 wait.result.name.c_str(),
                 expected ? "set" : "null",
                 wait_type_text.c_str(),
                 expected_kind);
  }
  if (expected)
  {
    if (expected->isPointerTy())
    {
      out = CoerceTo(&builder, result_ptr, expected);
    }
    else if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(expected);
             struct_ty && struct_ty->getNumElements() == 0)
    {
      out = llvm::Constant::getNullValue(expected);
    }
    else if (expected->isArrayTy())
    {
      llvm::Value *typed_ptr = builder.CreateBitCast(
          result_ptr, llvm::PointerType::get(expected, 0));
      out = builder.CreateLoad(expected, typed_ptr);
    }
    else if (!expected->isVoidTy())
    {
      llvm::Value *typed_ptr = builder.CreateBitCast(
          result_ptr, llvm::PointerType::get(expected, 0));
      out = builder.CreateLoad(expected, typed_ptr);
    }
  }
  if (!out)
  {
    out = DefaultFor(wait.result);
  }
  emitter.SetTempValue(wait.result, out);
}

} // namespace ultraviolet::codegen::emit_detail
