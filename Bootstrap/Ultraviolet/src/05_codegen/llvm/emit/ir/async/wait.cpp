// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/wait.cpp
// Canonical owner for LLVM IR wait instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRWait &wait) const
{
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
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(wait_sym);
    const bool use_c_abi_aggregate_sret = runtime_foreign_boundary;
    if (!wait_fn)
    {
      ABICallResult wait_abi = ComputeCallABI(
          emitter,
          wait_info->params,
          wait_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/false);
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
          /*foreign_boundary_mode_independent=*/false);
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
