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
  llvm::Type *range_ty = GetRangeType(emitter.GetContext());
  llvm::Type *string_view_ty = GetStringViewType(emitter.GetContext());

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

  auto evaluate_range_bound = [&](const std::optional<IRValue> &bound_opt,
                                  llvm::Type *target_ty) -> llvm::Value *
  {
    if (!target_ty)
    {
      return nullptr;
    }
    llvm::Value *fallback = llvm::ConstantInt::get(target_ty, 0);
    if (!bound_opt.has_value())
    {
      return fallback;
    }

    llvm::Value *value = emitter.EvaluateIRValue(*bound_opt);
    if (!value)
    {
      return fallback;
    }

    if (value->getType()->isPointerTy() && target_ty->isIntegerTy())
    {
      llvm::Type *load_ty = target_ty;
      if (active_ctx)
      {
        if (analysis::TypeRef bound_type = active_ctx->LookupValueType(*bound_opt))
        {
          if (llvm::Type *bound_ll = emitter.GetLLVMType(bound_type))
          {
            if (bound_ll->isIntegerTy())
            {
              load_ty = bound_ll;
            }
          }
        }
      }
      llvm::Value *typed_ptr = value;
      llvm::Type *ptr_to_load_ty = llvm::PointerType::get(load_ty, 0);
      if (typed_ptr->getType() != ptr_to_load_ty)
      {
        typed_ptr = builder.CreateBitCast(typed_ptr, ptr_to_load_ty);
      }
      value = builder.CreateLoad(load_ty, typed_ptr);
    }

    value = as_usize(value, fallback);
    if (value && value->getType() != target_ty)
    {
      value = CoerceTo(&builder, value, target_ty);
    }
    if (!value)
    {
      return fallback;
    }
    return value;
  };

  auto materialize_range = [&]() -> llvm::Value *
  {
    if (active_ctx)
    {
      if (const DerivedValueInfo *derived = active_ctx->LookupDerivedValue(dispatch.range))
      {
        if (derived->kind == DerivedValueInfo::Kind::RangeLit)
        {
          auto *range_struct_ty = llvm::dyn_cast<llvm::StructType>(range_ty);
          if (range_struct_ty && range_struct_ty->getNumElements() >= 3)
          {
            llvm::Value *out = llvm::Constant::getNullValue(range_struct_ty);
            llvm::Type *kind_ty = range_struct_ty->getElementType(0);
            llvm::Type *lo_ty = range_struct_ty->getElementType(1);
            llvm::Type *hi_ty = range_struct_ty->getElementType(2);

            llvm::Value *kind = llvm::ConstantInt::get(
                kind_ty,
                static_cast<std::uint64_t>(derived->range.kind));

            llvm::Value *lo = llvm::ConstantInt::get(lo_ty, 0);
            if (derived->range.lo.has_value())
            {
              lo = evaluate_range_bound(derived->range.lo, lo_ty);
            }

            llvm::Value *hi = llvm::ConstantInt::get(hi_ty, 0);
            if (derived->range.hi.has_value())
            {
              hi = evaluate_range_bound(derived->range.hi, hi_ty);
            }

            out = builder.CreateInsertValue(out, kind, {0u});
            out = builder.CreateInsertValue(out, lo, {1u});
            out = builder.CreateInsertValue(out, hi, {2u});
            return out;
          }
        }
      }
    }

    llvm::Value *range = EvaluateOrDefault(dispatch.range);
    if (range && range->getType() != range_ty)
    {
      range = CoerceTo(&builder, range, range_ty);
    }
    if (!range)
    {
      range = llvm::Constant::getNullValue(range_ty);
    }
    return range;
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

  const std::string dispatch_sym = ConcurrencySymDispatchRun();
  if (std::optional<RuntimeFuncInfo> dispatch_info =
          GetRuntimeFuncInfo(dispatch_sym))
  {
    llvm::Function *dispatch_fn = emitter.GetModule().getFunction(dispatch_sym);
    const bool use_c_abi_aggregate_sret = true;
    if (!dispatch_fn)
    {
      ABICallResult dispatch_abi = ComputeCallABI(
          emitter,
          dispatch_info->params,
          dispatch_info->ret,
          use_c_abi_aggregate_sret);
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
          use_c_abi_aggregate_sret);
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
