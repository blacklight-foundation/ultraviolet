// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/alloc.cpp
// Canonical owner for LLVM IR allocation instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRAlloc &alloc) const
{
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef value_type = alloc.type;
  if (!value_type && active_ctx)
  {
    value_type = active_ctx->LookupValueType(alloc.value);
  }

  llvm::Value *value = EvaluateOrDefault(alloc.value);
  llvm::Type *value_ty = value_type ? emitter.GetLLVMType(value_type) : nullptr;
  if ((!value_ty || value_ty->isVoidTy()) && value)
  {
    value_ty = value->getType();
  }
  if (!value_ty || value_ty->isVoidTy())
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  std::optional<IRValue> target_region = alloc.region;
  if (!target_region.has_value())
  {
    if (const IRValue *current = emitter.CurrentActiveRegion())
    {
      target_region = *current;
    }
  }
  if (!target_region.has_value())
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  llvm::Value *region_value = EvaluateOrDefault(*target_region);
  if (!region_value)
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  std::uint64_t alloc_size = 0;
  std::uint64_t alloc_align = 1;
  if (value_type)
  {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, value_type))
    {
      alloc_size = *size;
    }
    if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, value_type))
    {
      alloc_align = *align;
    }
  }
  if (alloc_align == 0)
  {
    alloc_align = 1;
  }

  const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
  if (alloc_size == 0 && !value_ty->isVoidTy())
  {
    alloc_size = static_cast<std::uint64_t>(dl.getTypeAllocSize(value_ty));
  }
  if (alloc_align == 1 && !value_ty->isVoidTy())
  {
    alloc_align = std::max<std::uint64_t>(
        alloc_align,
        static_cast<std::uint64_t>(dl.getABITypeAlign(value_ty).value()));
  }

  llvm::Type *usize_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Value *raw_ptr = nullptr;
  const std::string alloc_sym = BuiltinModalSymRegionAlloc();
  if (std::optional<RuntimeFuncInfo> alloc_info = GetRuntimeFuncInfo(alloc_sym))
  {
    llvm::Function *alloc_fn = emitter.GetModule().getFunction(alloc_sym);
    const bool use_c_abi_aggregate_sret = true;
    if (!alloc_fn)
    {
      ABICallResult alloc_abi = ComputeCallABI(
          emitter,
          alloc_info->params,
          alloc_info->ret,
          use_c_abi_aggregate_sret);
      if (alloc_abi.func_type)
      {
        alloc_fn = llvm::Function::Create(
            alloc_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            alloc_sym,
            &emitter.GetModule());
        alloc_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (alloc_fn)
    {
      std::vector<llvm::Value *> alloc_args;
      alloc_args.reserve(3);
      alloc_args.push_back(region_value);
      alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_size));
      alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_align));
      raw_ptr = EmitABICall(
          emitter,
          &builder,
          alloc_fn,
          alloc_info->params,
          alloc_info->ret,
          alloc_args,
          use_c_abi_aggregate_sret);
    }
  }
  if (!raw_ptr)
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  analysis::TypeRef source_type = active_ctx ? active_ctx->LookupValueType(alloc.value) : nullptr;
  if (!source_type)
  {
    source_type = value_type;
  }
  if (value->getType() != value_ty)
  {
    if (value_type)
    {
      llvm::Value *coerced =
          CoerceToTyped(emitter, &builder, value, value_ty, source_type, value_type);
      value = coerced ? coerced : llvm::Constant::getNullValue(value_ty);
    }
    else
    {
      llvm::Value *coerced = CoerceTo(&builder, value, value_ty);
      value = coerced ? coerced : llvm::Constant::getNullValue(value_ty);
    }
  }

  llvm::Value *typed_ptr = builder.CreateBitCast(
      raw_ptr,
      llvm::PointerType::get(value_ty, 0));
  builder.CreateStore(value, typed_ptr);

  emitter.SetTempStorage(alloc.result, typed_ptr);
  emitter.SetTempValue(alloc.result, raw_ptr);
}

} // namespace ultraviolet::codegen::emit_detail
