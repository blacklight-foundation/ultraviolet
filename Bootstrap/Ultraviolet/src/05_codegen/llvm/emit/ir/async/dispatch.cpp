// =============================================================================
// File: 05_codegen/llvm/emit/ir/async/dispatch.cpp
// Canonical owner for LLVM IR dispatch instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRDispatch &dispatch) const
{
  emitter.EmitIR(dispatch.captured_env);
  emitter.EmitIR(dispatch.body);

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  llvm::Type *ptr_ty = emitter.GetOpaquePtr();
  llvm::Type *i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
  llvm::Type *usize_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Type *string_view_ty = GetStringViewType(emitter.GetContext());
  const std::string dispatch_sym = ConcurrencySymDispatchRun();
  std::optional<RuntimeFuncInfo> dispatch_info = GetRuntimeFuncInfo(dispatch_sym);
  llvm::Type *range_ty = GetRangeType(emitter.GetContext());
  if (dispatch_info.has_value() && !dispatch_info->params.empty())
  {
    if (llvm::Type *runtime_range_ty =
            emitter.GetLLVMType(dispatch_info->params.front().type))
    {
      range_ty = runtime_range_ty;
    }
  }

  auto as_usize = [&](llvm::Value *value, llvm::Value *fallback) -> llvm::Value *
  {
    if (!value)
    {
      return fallback;
    }
    if (!value->getType()->isIntegerTy())
    {
      value = CoerceTo(&builder, value, usize_ty);
    }
    else if (value->getType()->getIntegerBitWidth() != 64)
    {
      value = builder.CreateIntCast(value, usize_ty, false);
    }
    return value ? value : fallback;
  };

  auto runtime_range_tag = [&](IRRangeKind kind) -> std::uint64_t
  {
    switch (kind)
    {
    case IRRangeKind::To:
      return 0u;
    case IRRangeKind::ToInclusive:
      return 1u;
    case IRRangeKind::Full:
      return 2u;
    case IRRangeKind::From:
      return 3u;
    case IRRangeKind::Exclusive:
      return 4u;
    case IRRangeKind::Inclusive:
      return 5u;
    }
    return 0u;
  };

  auto build_runtime_range = [&](IRRangeKind kind,
                                 llvm::Value *lo,
                                 llvm::Value *hi) -> llvm::Value *
  {
    auto *range_struct_ty = llvm::dyn_cast<llvm::StructType>(range_ty);
    if (!range_struct_ty)
    {
      return nullptr;
    }

    const bool has_explicit_padding =
        range_struct_ty->getNumElements() >= 4 &&
        range_struct_ty->getElementType(1)->isArrayTy();
    const unsigned kind_index = 0u;
    const unsigned lo_index = has_explicit_padding ? 2u : 1u;
    const unsigned hi_index = has_explicit_padding ? 3u : 2u;
    if (hi_index >= range_struct_ty->getNumElements())
    {
      return nullptr;
    }

    llvm::Type *kind_ty = range_struct_ty->getElementType(kind_index);
    llvm::Type *lo_ty = range_struct_ty->getElementType(lo_index);
    llvm::Type *hi_ty = range_struct_ty->getElementType(hi_index);
    llvm::Value *tag =
        llvm::ConstantInt::get(kind_ty, runtime_range_tag(kind));
    llvm::Value *lo_value = lo ? as_usize(lo, llvm::ConstantInt::get(lo_ty, 0))
                               : llvm::ConstantInt::get(lo_ty, 0);
    llvm::Value *hi_value = hi ? as_usize(hi, llvm::ConstantInt::get(hi_ty, 0))
                               : llvm::ConstantInt::get(hi_ty, 0);

    if (lo_value && lo_value->getType() != lo_ty)
    {
      lo_value = CoerceTo(&builder, lo_value, lo_ty);
    }
    if (hi_value && hi_value->getType() != hi_ty)
    {
      hi_value = CoerceTo(&builder, hi_value, hi_ty);
    }
    if (!lo_value || !hi_value)
    {
      return nullptr;
    }

    llvm::Value *out = llvm::Constant::getNullValue(range_struct_ty);
    out = builder.CreateInsertValue(out, tag, {kind_index});
    out = builder.CreateInsertValue(out, lo_value, {lo_index});
    out = builder.CreateInsertValue(out, hi_value, {hi_index});
    return out;
  };

  auto materialize_range = [&]() -> llvm::Value *
  {
    if (auto resolved = ResolveRangeValue(dispatch.range, usize_ty);
        resolved.has_value())
    {
      if (llvm::Value *range =
              build_runtime_range(resolved->kind, resolved->lo, resolved->hi))
      {
        return range;
      }
    }
    if (active_ctx)
    {
      const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
    }
    return llvm::Constant::getNullValue(range_ty);
  };

  llvm::Value *range = materialize_range();
  llvm::Value *elem_size = as_usize(
      EvaluateOrDefault(dispatch.elem_size),
      llvm::ConstantInt::get(usize_ty, 0));
  llvm::Value *result_size = as_usize(
      EvaluateOrDefault(dispatch.result_size),
      llvm::ConstantInt::get(usize_ty, 0));

  llvm::Value *body_fn = CoerceTo(&builder, EvaluateOrDefault(dispatch.body_fn), ptr_ty);
  if (!body_fn)
  {
    body_fn = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  llvm::Value *env_ptr = CoerceTo(&builder, EvaluateOrDefault(dispatch.env_ptr), ptr_ty);
  if (!env_ptr)
  {
    env_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }
  llvm::Value *hosted_env = emitter.GetHostedCurrentEnvPtr();
  hosted_env = CoerceTo(&builder, hosted_env, ptr_ty);
  if (!hosted_env)
  {
    hosted_env = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_ty));
  }

  llvm::Value *result_ptr = CoerceTo(&builder, EvaluateOrDefault(dispatch.result_ptr), ptr_ty);
  if (!result_ptr)
  {
    result_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
  }

  llvm::Value *reduce_fn = llvm::ConstantPointerNull::get(
      llvm::cast<llvm::PointerType>(ptr_ty));
  if (dispatch.reduce_fn.has_value())
  {
    reduce_fn = CoerceTo(&builder, EvaluateOrDefault(*dispatch.reduce_fn), ptr_ty);
    if (!reduce_fn)
    {
      reduce_fn = llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(ptr_ty));
    }
  }

  llvm::Value *reduce_op = llvm::Constant::getNullValue(string_view_ty);
  if (dispatch.reduce_op.has_value())
  {
    auto *view_ty = llvm::dyn_cast<llvm::StructType>(string_view_ty);
    if (view_ty && view_ty->getNumElements() >= 2)
    {
      llvm::Type *ptr_field_ty = view_ty->getElementType(0);
      llvm::Type *len_field_ty = view_ty->getElementType(1);
      llvm::Value *op_ptr = builder.CreateGlobalStringPtr(*dispatch.reduce_op);
      op_ptr = CoerceTo(&builder, op_ptr, ptr_field_ty);
      if (!op_ptr)
      {
        op_ptr = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ptr_field_ty));
      }
      llvm::Value *op_len = llvm::ConstantInt::get(
          len_field_ty,
          static_cast<std::uint64_t>(dispatch.reduce_op->size()));
      llvm::Value *out = llvm::Constant::getNullValue(view_ty);
      out = builder.CreateInsertValue(out, op_ptr, {0u});
      out = builder.CreateInsertValue(out, op_len, {1u});
      reduce_op = out;
    }
  }

  llvm::Value *ordered = llvm::ConstantInt::get(i32_ty, dispatch.ordered ? 1 : 0);
  llvm::Value *chunk_size = llvm::ConstantInt::get(usize_ty, 0);
  if (dispatch.chunk_size.has_value())
  {
    chunk_size = as_usize(
        EvaluateOrDefault(*dispatch.chunk_size),
        llvm::ConstantInt::get(usize_ty, 0));
  }

  if (dispatch_info.has_value())
  {
    llvm::Function *dispatch_fn = emitter.GetModule().getFunction(dispatch_sym);
    const bool runtime_c_aggregate_boundary =
        RuntimeUsesCAggregateABI(dispatch_sym);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(dispatch_sym);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!dispatch_fn)
    {
      ABICallResult dispatch_abi = ComputeCallABI(
          emitter,
          dispatch_info->params,
          dispatch_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (dispatch_abi.func_type)
      {
        dispatch_fn = llvm::Function::Create(
            dispatch_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            dispatch_sym,
            &emitter.GetModule());
        dispatch_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (dispatch_fn)
    {
      std::vector<llvm::Value *> dispatch_args;
      dispatch_args.reserve(11);
      dispatch_args.push_back(range);
      dispatch_args.push_back(elem_size);
      dispatch_args.push_back(result_size);
      dispatch_args.push_back(body_fn);
      dispatch_args.push_back(hosted_env);
      dispatch_args.push_back(env_ptr);
      dispatch_args.push_back(reduce_op);
      dispatch_args.push_back(result_ptr);
      dispatch_args.push_back(reduce_fn);
      dispatch_args.push_back(ordered);
      dispatch_args.push_back(chunk_size);
      (void)EmitABICall(
          emitter,
          &builder,
          dispatch_fn,
          dispatch_info->params,
          dispatch_info->ret,
          dispatch_args,
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

  const bool has_reduce = dispatch.reduce_op.has_value() || dispatch.reduce_fn.has_value();
  llvm::Value *out = nullptr;
  llvm::Type *expected = ExpectedLLVMType(dispatch.result);
  if (expected)
  {
    if (expected->isStructTy() &&
        llvm::cast<llvm::StructType>(expected)->getNumElements() == 0)
    {
      out = llvm::Constant::getNullValue(expected);
    }
    else if (has_reduce && !expected->isVoidTy())
    {
      llvm::Value *typed_result_ptr = builder.CreateBitCast(
          result_ptr, llvm::PointerType::get(expected, 0));
      out = builder.CreateLoad(expected, typed_result_ptr);
    }
  }
  if (!out)
  {
    out = DefaultFor(dispatch.result);
  }
  emitter.SetTempValue(dispatch.result, out);
}

} // namespace ultraviolet::codegen::emit_detail
