// =============================================================================
// File: 05_codegen/llvm/emit/ir/call/direct.cpp
// Canonical owner for LLVM IR direct call instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include <functional>

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCall &call) const
{
  std::vector<llvm::Value *> args;
  args.reserve(call.args.size());
  for (const auto &arg : call.args)
  {
    args.push_back(EvaluateOrDefault(arg));
  }

  // Pure string/bytes view builtins are specified as pointer/length metadata
  // operations. Lower them directly instead of crossing the runtime ABI.
  auto build_prefix_pair = [&](llvm::StructType *pair_ty,
                               llvm::Value *data,
                               llvm::Value *len) -> llvm::Value *
  {
    if (!pair_ty || pair_ty->getNumElements() < 2 || !data || !len)
    {
      return nullptr;
    }
    llvm::Type *data_ty = pair_ty->getElementType(0);
    llvm::Type *len_ty = pair_ty->getElementType(1);
    data = CoerceTo(&builder, data, data_ty);
    len = CoerceTo(&builder, len, len_ty);
    if (!data || !len)
    {
      return nullptr;
    }
    llvm::Value *pair = llvm::UndefValue::get(pair_ty);
    pair = builder.CreateInsertValue(pair, data, {0u});
    pair = builder.CreateInsertValue(pair, len, {1u});
    return pair;
  };

  auto load_prefix_pair = [&](llvm::Value *value,
                              llvm::StructType *pair_ty,
                              llvm::StringRef name) -> llvm::Value *
  {
    if (!value || !pair_ty)
    {
      return nullptr;
    }
    if (llvm::isa<llvm::ConstantPointerNull>(value))
    {
      return nullptr;
    }
    if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(value->getType()))
    {
      if (struct_ty->getNumElements() < 2)
      {
        return nullptr;
      }
      llvm::Value *data = builder.CreateExtractValue(value, {0u});
      llvm::Value *len = builder.CreateExtractValue(value, {1u});
      return build_prefix_pair(pair_ty, data, len);
    }
    if (!value->getType()->isPointerTy())
    {
      return nullptr;
    }
    llvm::Value *ptr = value;
    llvm::Type *wanted_ptr_ty = llvm::PointerType::get(pair_ty, 0);
    if (ptr->getType() != wanted_ptr_ty)
    {
      ptr = builder.CreateBitCast(ptr, wanted_ptr_ty);
    }
    return builder.CreateLoad(pair_ty, ptr, name);
  };

  auto set_intrinsic_result = [&](llvm::Value *value) -> bool
  {
    if (!value)
    {
      return false;
    }
    if (llvm::Type *expected = ExpectedLLVMType(call.result))
    {
      if (llvm::Value *coerced = CoerceTo(&builder, value, expected))
      {
        value = coerced;
      }
    }
    emitter.SetTempValue(call.result, value);
    return true;
  };

  auto emit_pure_string_bytes_intrinsic = [&]() -> bool
  {
    if (call.callee.kind != IRValue::Kind::Symbol || args.empty())
    {
      return false;
    }

    const std::string &symbol = call.callee.name;
    llvm::StructType *string_view_ty = GetStringViewType(emitter.GetContext());
    llvm::StructType *bytes_view_ty = GetBytesViewType(emitter.GetContext());
    llvm::StructType *slice_ty = GetSliceType(emitter.GetContext());

    auto extract_len = [&](llvm::StructType *pair_ty) -> llvm::Value *
    {
      llvm::Value *pair = load_prefix_pair(args[0], pair_ty, "view.prefix");
      if (!pair)
      {
        return nullptr;
      }
      return builder.CreateExtractValue(pair, {1u}, "view.len");
    };

    auto coerce_scalar_arg = [&](llvm::Value *value,
                                 llvm::Type *target_ty,
                                 llvm::StringRef name) -> llvm::Value *
    {
      if (!value || !target_ty)
      {
        return nullptr;
      }
      if (value->getType() == target_ty)
      {
        return value;
      }
      if (value->getType()->isPointerTy() && target_ty->isFirstClassType())
      {
        return builder.CreateLoad(target_ty, value, name);
      }
      return CoerceTo(&builder, value, target_ty);
    };

    if (symbol == BuiltinSymStringLength())
    {
      return set_intrinsic_result(extract_len(string_view_ty));
    }
    if (symbol == BuiltinSymBytesLength())
    {
      return set_intrinsic_result(extract_len(bytes_view_ty));
    }
    if (symbol == BuiltinSymStringIsEmpty())
    {
      if (llvm::Value *len = extract_len(string_view_ty))
      {
        llvm::Value *is_empty = builder.CreateICmpEQ(
            len,
            llvm::ConstantInt::get(len->getType(), 0));
        return set_intrinsic_result(
            builder.CreateZExt(is_empty, llvm::Type::getInt8Ty(emitter.GetContext())));
      }
      return false;
    }
    if (symbol == BuiltinSymBytesIsEmpty())
    {
      if (llvm::Value *len = extract_len(bytes_view_ty))
      {
        llvm::Value *is_empty = builder.CreateICmpEQ(
            len,
            llvm::ConstantInt::get(len->getType(), 0));
        return set_intrinsic_result(
            builder.CreateZExt(is_empty, llvm::Type::getInt8Ty(emitter.GetContext())));
      }
      return false;
    }
    if (symbol == BuiltinSymStringAsView())
    {
      return set_intrinsic_result(
          load_prefix_pair(args[0], string_view_ty, "string.as_view"));
    }
    if (symbol == BuiltinSymStringSlice() && args.size() >= 3)
    {
      llvm::Value *string_pair =
          load_prefix_pair(args[0], string_view_ty, "string.slice.self");
      if (!string_pair)
      {
        return false;
      }

      llvm::Value *data = builder.CreateExtractValue(string_pair, {0u});
      llvm::Value *len = builder.CreateExtractValue(string_pair, {1u});
      if (!data || !len)
      {
        return false;
      }

      llvm::Type *data_ty = string_view_ty->getElementType(0);
      llvm::Type *len_ty = string_view_ty->getElementType(1);
      data = CoerceTo(&builder, data, data_ty);
      len = CoerceTo(&builder, len, len_ty);
      llvm::Value *start = coerce_scalar_arg(args[1], len_ty, "string.slice.start");
      llvm::Value *end = coerce_scalar_arg(args[2], len_ty, "string.slice.end");
      if (!data || !len || !start || !end || !data_ty->isPointerTy() ||
          !len_ty->isIntegerTy())
      {
        return false;
      }

      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
      llvm::Value *data_i8 = CoerceTo(&builder, data, i8_ptr_ty);
      if (!data_i8)
      {
        return false;
      }

      llvm::Value *zero_len = llvm::ConstantInt::get(len_ty, 0);
      llvm::Value *null_data =
          llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(data_ty));
      llvm::Value *null_i8 =
          llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8_ptr_ty));

      llvm::Value *start_le_end = builder.CreateICmpULE(start, end);
      llvm::Value *end_le_len = builder.CreateICmpULE(end, len);
      llvm::Value *in_range = builder.CreateAnd(start_le_end, end_le_len);
      llvm::Value *slice_len = builder.CreateSub(end, start);

      llvm::Value *offset_i8 = builder.CreateGEP(i8_ty, data_i8, start);
      llvm::Value *data_present = builder.CreateICmpNE(data_i8, null_i8);
      llvm::Value *valid_i8 = builder.CreateSelect(data_present, offset_i8, null_i8);
      llvm::Value *valid_data = CoerceTo(&builder, valid_i8, data_ty);
      if (!valid_data)
      {
        return false;
      }

      llvm::Value *result_data =
          builder.CreateSelect(in_range, valid_data, null_data);
      llvm::Value *result_len =
          builder.CreateSelect(in_range, slice_len, zero_len);
      return set_intrinsic_result(
          build_prefix_pair(string_view_ty, result_data, result_len));
    }
    if (symbol == BuiltinSymBytesAsView() ||
        symbol == BuiltinSymBytesView())
    {
      return set_intrinsic_result(
          load_prefix_pair(args[0], bytes_view_ty, "bytes.as_view"));
    }
    if (symbol == BuiltinSymBytesViewString())
    {
      llvm::Value *string_pair =
          load_prefix_pair(args[0], string_view_ty, "bytes.view_string");
      if (!string_pair)
      {
        return false;
      }
      llvm::Value *data = builder.CreateExtractValue(string_pair, {0u});
      llvm::Value *len = builder.CreateExtractValue(string_pair, {1u});
      return set_intrinsic_result(build_prefix_pair(bytes_view_ty, data, len));
    }
    if (symbol == BuiltinSymBytesAsSlice())
    {
      llvm::Value *bytes_pair =
          load_prefix_pair(args[0], bytes_view_ty, "bytes.as_slice");
      if (!bytes_pair)
      {
        return false;
      }
      llvm::Value *data = builder.CreateExtractValue(bytes_pair, {0u});
      llvm::Value *len = builder.CreateExtractValue(bytes_pair, {1u});
      return set_intrinsic_result(build_prefix_pair(slice_ty, data, len));
    }

    return false;
  };

  if (emit_pure_string_bytes_intrinsic())
  {
    return;
  }

  if (call.callee.kind == IRValue::Kind::Symbol &&
      IsDropGlueSymbol(call.callee.name) &&
      !call.args.empty())
  {
    if (llvm::Value *storage = emitter.GetAddressableStorage(call.args.front()))
    {
      args.front() = storage;
    }
    else if (llvm::Value *value = emitter.EvaluateIRValue(call.args.front()))
    {
      llvm::Type *storage_ty = value->getType();
      if (const LowerCtx *ctx = emitter.GetCurrentCtx())
      {
        if (analysis::TypeRef source_type = ctx->LookupValueType(call.args.front()))
        {
          if (llvm::Type *expected_ty = emitter.GetLLVMType(source_type))
          {
            storage_ty = expected_ty;
            if (value->getType() != expected_ty)
            {
              if (llvm::Value *coerced = CoerceTo(&builder, value, expected_ty))
              {
                value = coerced;
              }
            }
          }
        }
      }

      llvm::Function *func =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (func && storage_ty && !storage_ty->isVoidTy())
      {
        llvm::IRBuilder<> entry_builder(
            &func->getEntryBlock(),
            func->getEntryBlock().begin());
        llvm::AllocaInst *slot =
            entry_builder.CreateAlloca(storage_ty, nullptr, "drop.arg");
        if (value->getType() == storage_ty)
        {
          builder.CreateStore(value, slot);
        }
        else
        {
          builder.CreateStore(llvm::Constant::getNullValue(storage_ty), slot);
          llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
          llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
          const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
          const std::uint64_t src_size =
              static_cast<std::uint64_t>(dl.getTypeAllocSize(value->getType()));
          const std::uint64_t dst_size =
              static_cast<std::uint64_t>(dl.getTypeAllocSize(storage_ty));
          const std::uint64_t copy_size = std::min(src_size, dst_size);
          if (copy_size > 0)
          {
            llvm::AllocaInst *src_slot =
                entry_builder.CreateAlloca(value->getType(), nullptr, "drop.src");
            builder.CreateStore(value, src_slot);
            builder.CreateMemCpy(
                builder.CreateBitCast(slot, llvm::PointerType::get(i8_ty, 0)),
                llvm::Align(1),
                builder.CreateBitCast(src_slot, llvm::PointerType::get(i8_ty, 0)),
                llvm::Align(1),
                llvm::ConstantInt::get(i64_ty, copy_size));
          }
        }
        args.front() = slot;
      }
    }
  }

  if (call.callee.kind == IRValue::Kind::Symbol)
  {
    const LowerCtx *comb_ctx = emitter.GetCurrentCtx();
    analysis::TypeRef comb_source_async_type =
        (comb_ctx && !call.args.empty())
            ? comb_ctx->LookupValueType(call.args[0])
            : nullptr;
    const analysis::ScopeContext &comb_scope = BuildScope(comb_ctx);
    const auto comb_source_sig =
        analysis::AsyncSigOf(comb_scope, comb_source_async_type);

    std::optional<AsyncCombinatorKind> comb_kind =
        AsyncCombinatorKindFromSymbol(call.callee.name);
    if (!comb_kind.has_value() && comb_source_sig.has_value())
    {
      comb_kind = analysis::LookupBuiltinAsyncCombinator(call.callee.name);
    }

    if (comb_kind.has_value())
    {
      if (args.empty() || call.args.empty())
      {
        emitter.SetTempValue(call.result, DefaultFor(call.result));
        return;
      }

      auto infer_callable_ret_type = [&](const IRValue &target) -> analysis::TypeRef
      {
        if (comb_ctx)
        {
          analysis::TypeRef callee_type =
              analysis::StripPerm(comb_ctx->LookupValueType(target));
          if (!callee_type)
          {
            callee_type = comb_ctx->LookupValueType(target);
          }
          if (const auto *fn =
                  callee_type ? std::get_if<analysis::TypeFunc>(&callee_type->node)
                              : nullptr)
          {
            return fn->ret;
          }
          if (const auto *closure =
                  callee_type ? std::get_if<analysis::TypeClosure>(&callee_type->node)
                              : nullptr)
          {
            return closure->ret;
          }
        }
        if (target.kind == IRValue::Kind::Symbol && comb_ctx)
        {
          if (const auto *sig = comb_ctx->LookupProcSig(target.name))
          {
            return sig->ret;
          }
          if (const auto alias = emitter.LookupSymbolAlias(target.name))
          {
            if (const auto *sig = comb_ctx->LookupProcSig(*alias))
            {
              return sig->ret;
            }
          }
        }
        return nullptr;
      };

      analysis::TypeRef source_async_type =
          comb_source_async_type;
      analysis::TypeRef result_async_type =
          comb_ctx ? comb_ctx->LookupValueType(call.result) : nullptr;

      const auto source_sig = comb_source_sig;
      if (!result_async_type && source_sig)
      {
        if (*comb_kind == AsyncCombinatorKind::Map && call.args.size() >= 2)
        {
          if (analysis::TypeRef out_type = infer_callable_ret_type(call.args[1]))
          {
            result_async_type = analysis::MakeTypePath(
                {"Async"},
                {out_type, source_sig->in, source_sig->result, source_sig->err});
          }
        }
        else if (*comb_kind == AsyncCombinatorKind::Fold &&
                 call.args.size() >= 2 && comb_ctx)
        {
          analysis::TypeRef acc_type = comb_ctx->LookupValueType(call.args[1]);
          if (acc_type)
          {
            result_async_type = analysis::MakeTypePath(
                {"Async"},
                {analysis::MakeTypePrim("()"),
                 analysis::MakeTypePrim("()"),
                 acc_type,
                 source_sig->err});
          }
        }
        else if (*comb_kind == AsyncCombinatorKind::Chain &&
                 call.args.size() >= 2)
        {
          if (analysis::TypeRef chain_ret = infer_callable_ret_type(call.args[1]))
          {
            result_async_type = chain_ret;
          }
        }
        else if (*comb_kind == AsyncCombinatorKind::Filter ||
                 *comb_kind == AsyncCombinatorKind::Take)
        {
          result_async_type = source_async_type;
        }
      }

      if (!result_async_type)
      {
        result_async_type = source_async_type;
      }

      const auto result_sig = analysis::AsyncSigOf(comb_scope, result_async_type);
      llvm::Value *source_async = args[0];
      auto *async_struct =
          llvm::dyn_cast_or_null<llvm::StructType>(
              source_async ? source_async->getType() : nullptr);
      if (!comb_source_sig || !result_sig || !source_async || !async_struct ||
          async_struct->getNumElements() < 1 ||
          !async_struct->getElementType(0)->isIntegerTy())
      {
        emitter.SetTempValue(call.result, source_async ? source_async : DefaultFor(call.result));
        return;
      }

      llvm::Function *func =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (!func)
      {
        emitter.SetTempValue(call.result, DefaultFor(call.result));
        return;
      }

      llvm::IRBuilder<> entry_builder(
          &func->getEntryBlock(),
          func->getEntryBlock().begin());
      llvm::AllocaInst *async_slot = entry_builder.CreateAlloca(async_struct);
      builder.CreateStore(source_async, async_slot);

      llvm::Type *expected_result_ty = ExpectedLLVMType(call.result);
      if (!expected_result_ty && result_async_type)
      {
        expected_result_ty = emitter.GetLLVMType(result_async_type);
      }
      if (!expected_result_ty)
      {
        expected_result_ty = source_async->getType();
      }
      llvm::AllocaInst *result_slot = nullptr;
      if (expected_result_ty && !expected_result_ty->isVoidTy())
      {
        result_slot = entry_builder.CreateAlloca(expected_result_ty);
        builder.CreateStore(llvm::Constant::getNullValue(expected_result_ty), result_slot);
      }

      auto materialize_as_type = [&](llvm::Value *value, llvm::Type *dst_ty) -> llvm::Value *
      {
        if (!value || !dst_ty)
        {
          return nullptr;
        }
        if (value->getType() == dst_ty)
        {
          return value;
        }
        if (llvm::Value *coerced = CoerceTo(&builder, value, dst_ty))
        {
          return coerced;
        }

        llvm::AllocaInst *dst_slot = entry_builder.CreateAlloca(dst_ty);
        builder.CreateStore(llvm::Constant::getNullValue(dst_ty), dst_slot);
        llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(value->getType());
        builder.CreateStore(value, src_slot);

        llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
        llvm::Value *dst_i8 = builder.CreateBitCast(dst_slot, llvm::PointerType::get(i8_ty, 0));
        llvm::Value *src_i8 = builder.CreateBitCast(src_slot, llvm::PointerType::get(i8_ty, 0));
        const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
        const std::uint64_t src_size =
            static_cast<std::uint64_t>(dl.getTypeAllocSize(value->getType()));
        const std::uint64_t dst_size =
            static_cast<std::uint64_t>(dl.getTypeAllocSize(dst_ty));
        const std::uint64_t copy_size = std::min(src_size, dst_size);
        if (copy_size > 0)
        {
          builder.CreateMemCpy(
              dst_i8,
              llvm::Align(1),
              src_i8,
              llvm::Align(1),
              llvm::ConstantInt::get(i64_ty, copy_size));
        }
        return builder.CreateLoad(dst_ty, dst_slot);
      };

      auto store_result = [&](llvm::Value *value)
      {
        if (!result_slot || !expected_result_ty || expected_result_ty->isVoidTy())
        {
          return;
        }
        llvm::Value *out = value;
        if (!out)
        {
          out = llvm::Constant::getNullValue(expected_result_ty);
        }
        else if (out->getType() != expected_result_ty)
        {
          if (result_async_type)
          {
            if (llvm::Value *typed = CoerceToTyped(
                    emitter,
                    &builder,
                    out,
                    expected_result_ty,
                    source_async_type,
                    result_async_type))
            {
              out = typed;
            }
            else if (llvm::Value *plain = CoerceTo(&builder, out, expected_result_ty))
            {
              out = plain;
            }
            else
            {
              out = materialize_as_type(out, expected_result_ty);
            }
          }
          else if (llvm::Value *plain = CoerceTo(&builder, out, expected_result_ty))
          {
            out = plain;
          }
          else
          {
            out = materialize_as_type(out, expected_result_ty);
          }
        }
        if (!out)
        {
          out = llvm::Constant::getNullValue(expected_result_ty);
        }
        builder.CreateStore(out, result_slot);
      };

      auto finish_from = [&](llvm::Value *value)
      {
        if (result_slot && expected_result_ty && !expected_result_ty->isVoidTy())
        {
          if (value)
          {
            store_result(value);
          }
          llvm::Value *out = builder.CreateLoad(expected_result_ty, result_slot);
          emitter.SetTempValue(call.result, out ? out : DefaultFor(call.result));
          return;
        }
        emitter.SetTempValue(call.result, value ? value : DefaultFor(call.result));
      };

      std::size_t temp_index = 0;
      auto make_temp_local = [&](std::string_view stem,
                                 llvm::Value *value,
                                 const analysis::TypeRef & /*type*/) -> IRValue
      {
        // These are ephemeral SSA values, not addressable locals.
        // Store them as opaque temps so EvaluateIRValue reads the
        // materialized value instead of falling back through local slots.
        IRValue temp;
        temp.kind = IRValue::Kind::Opaque;
        temp.name = call.result.name + "." + std::string(stem) + "." +
                    std::to_string(temp_index++);
        emitter.SetTempValue(temp, value);
        return temp;
      };

      auto invoke_callable =
          [&](const IRValue &callee,
              const std::vector<std::pair<llvm::Value *, analysis::TypeRef>> &call_args,
              std::string_view stem,
              analysis::TypeRef expected_ret_type) -> llvm::Value *
      {
        IRCall inner;
        inner.callee = callee;
        inner.args.reserve(call_args.size() + 1);
        for (std::size_t i = 0; i < call_args.size(); ++i)
        {
          std::string arg_stem = std::string(stem) + "_arg" + std::to_string(i);
          inner.args.push_back(make_temp_local(arg_stem, call_args[i].first, call_args[i].second));
        }

        llvm::Value *panic_arg_value =
            LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
        const analysis::TypeRef panic_arg_type = PanicOutType();
        llvm::Type *panic_arg_ll = emitter.GetLLVMType(panic_arg_type);
        if (panic_arg_ll && panic_arg_value &&
            panic_arg_value->getType() != panic_arg_ll)
        {
          if (llvm::Value *coerced = CoerceTo(&builder, panic_arg_value, panic_arg_ll))
          {
            panic_arg_value = coerced;
          }
          else if (panic_arg_value->getType()->isPointerTy() &&
                   panic_arg_ll->isPointerTy())
          {
            panic_arg_value = builder.CreateBitCast(panic_arg_value, panic_arg_ll);
          }
          else
          {
            panic_arg_value = nullptr;
          }
        }
        if (!panic_arg_value && panic_arg_ll && !panic_arg_ll->isVoidTy())
        {
          panic_arg_value = llvm::Constant::getNullValue(panic_arg_ll);
        }
        if (panic_arg_value)
        {
          inner.args.push_back(
              make_temp_local(std::string(stem) + "_panic", panic_arg_value, panic_arg_type));
        }

        inner.result.kind = IRValue::Kind::Opaque;
        inner.result.name = call.result.name + "." + std::string(stem) + ".ret." +
                            std::to_string(temp_index++);
        analysis::TypeRef callable_ret_type = infer_callable_ret_type(callee);
        if (!callable_ret_type)
        {
          callable_ret_type = expected_ret_type;
        }
        (*this)(inner);
        llvm::Value *out = emitter.EvaluateIRValue(inner.result);
        if (!out && callable_ret_type)
        {
          if (llvm::Value *storage = emitter.GetTempStorage(inner.result))
          {
            if (llvm::Type *ret_ty = emitter.GetLLVMType(callable_ret_type))
            {
              llvm::Value *typed_ptr = storage;
              llvm::Type *expected_ptr_ty = llvm::PointerType::get(ret_ty, 0);
              if (typed_ptr->getType() != expected_ptr_ty)
              {
                typed_ptr = builder.CreateBitCast(typed_ptr, expected_ptr_ty);
              }
              out = builder.CreateLoad(ret_ty, typed_ptr);
            }
          }
        }
        if (!out)
        {
          llvm::Type *fallback_ty =
              callable_ret_type ? emitter.GetLLVMType(callable_ret_type) : nullptr;
          if (fallback_ty && !fallback_ty->isVoidTy())
          {
            out = llvm::Constant::getNullValue(fallback_ty);
          }
          else
          {
            out = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(emitter.GetContext()), 0);
          }
        }
        return out;
      };

      auto load_payload_from_slot =
          [&](llvm::AllocaInst *slot,
              const analysis::TypeRef &payload_type) -> llvm::Value *
      {
        if (!slot || !payload_type || IsUnitTypeRef(payload_type) || IsNeverTypeRef(payload_type))
        {
          return nullptr;
        }
        llvm::Type *payload_ll = emitter.GetLLVMType(payload_type);
        if (!payload_ll || payload_ll->isVoidTy())
        {
          return nullptr;
        }
        llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
            emitter,
            &builder,
            async_struct,
            slot,
            ::ultraviolet::analysis::layout::kPtrAlign);
        if (!payload_i8)
        {
          return nullptr;
        }
        llvm::Value *payload_ptr =
            builder.CreateBitCast(payload_i8, llvm::PointerType::get(payload_ll, 0));
        llvm::LoadInst *loaded = builder.CreateLoad(payload_ll, payload_ptr);
        loaded->setAlignment(llvm::Align(1));
        return loaded;
      };

      auto store_payload_to_slot =
          [&](llvm::AllocaInst *slot,
              llvm::Value *payload_value,
              const analysis::TypeRef &payload_type)
      {
        if (!slot || !payload_value || !payload_type ||
            IsUnitTypeRef(payload_type) || IsNeverTypeRef(payload_type))
        {
          return;
        }
        llvm::Type *payload_ll = emitter.GetLLVMType(payload_type);
        if (!payload_ll || payload_ll->isVoidTy())
        {
          return;
        }
        llvm::Value *coerced = payload_value;
        if (coerced->getType() != payload_ll)
        {
          if (llvm::Value *typed = CoerceToTyped(
                  emitter,
                  &builder,
                  coerced,
                  payload_ll,
                  payload_type,
                  payload_type))
          {
            coerced = typed;
          }
          else if (llvm::Value *plain = CoerceTo(&builder, coerced, payload_ll))
          {
            coerced = plain;
          }
          else
          {
            coerced = materialize_as_type(coerced, payload_ll);
          }
        }
        if (!coerced)
        {
          return;
        }
        llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
            emitter,
            &builder,
            async_struct,
            slot,
            ::ultraviolet::analysis::layout::kPtrAlign);
        if (!payload_i8)
        {
          return;
        }
        llvm::Value *payload_ptr =
            builder.CreateBitCast(payload_i8, llvm::PointerType::get(payload_ll, 0));
        llvm::StoreInst *stored = builder.CreateStore(coerced, payload_ptr);
        stored->setAlignment(llvm::Align(1));
      };

      auto make_async_complete = [&](llvm::Value *payload_value,
                                     const analysis::TypeRef &payload_type) -> llvm::Value *
      {
        analysis::TypeRef complete_payload_type =
            payload_type ? payload_type : analysis::MakeTypePrim("()");
        llvm::Value *payload = payload_value;
        if (!payload)
        {
          payload = llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(emitter.GetContext()), 0);
        }
        IRValue payload_ir = make_temp_local("async_complete_payload", payload, complete_payload_type);
        IRAsyncComplete complete;
        complete.value = payload_ir;
        complete.result.kind = IRValue::Kind::Opaque;
        complete.result.name = call.result.name + ".async_complete." +
                               std::to_string(temp_index++);
        complete.async_type = result_async_type;
        complete.result_type = complete_payload_type;
        (*this)(complete);
        return emitter.EvaluateIRValue(complete.result);
      };

      auto make_async_fail = [&](llvm::Value *payload_value,
                                 const analysis::TypeRef &payload_type) -> llvm::Value *
      {
        analysis::TypeRef fail_payload_type =
            payload_type ? payload_type : analysis::MakeTypePrim("!");
        llvm::Value *payload = payload_value;
        if (!payload)
        {
          payload = llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(emitter.GetContext()), 0);
        }
        IRValue payload_ir = make_temp_local("async_fail_payload", payload, fail_payload_type);
        IRAsyncFail fail;
        fail.value = payload_ir;
        fail.result.kind = IRValue::Kind::Opaque;
        fail.result.name = call.result.name + ".async_fail." +
                           std::to_string(temp_index++);
        fail.async_type = result_async_type;
        fail.error_type = fail_payload_type;
        (*this)(fail);
        return emitter.EvaluateIRValue(fail.result);
      };

      const AsyncStateDiscs source_discs =
          LoweredAsyncStateDiscs(comb_scope, *source_sig);
      const std::uint64_t suspended_disc = source_discs.suspended;
      const std::uint64_t completed_disc = source_discs.completed;
      const std::optional<std::uint64_t> failed_disc = source_discs.failed;

      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *opaque_ptr_ty = emitter.GetOpaquePtr();
      auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);
      llvm::Value *panic_ptr = LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
      if (panic_ptr)
      {
        if (llvm::Value *coerced = CoerceTo(&builder, panic_ptr, opaque_ptr_ty))
        {
          panic_ptr = coerced;
        }
        else if (panic_ptr->getType()->isPointerTy())
        {
          panic_ptr = builder.CreateBitCast(panic_ptr, opaque_ptr_ty);
        }
        else
        {
          panic_ptr = nullptr;
        }
      }
      if (!panic_ptr)
      {
        panic_ptr = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }

      auto emit_resume_step = [&]()
      {
        llvm::Value *suspended_ptr = builder.CreateBitCast(async_slot, opaque_ptr_ty);
        llvm::Value *unit_input = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
        llvm::Value *resume_call = EmitAsyncResumeRuntimeCall(
            emitter,
            &builder,
            suspended_ptr,
            unit_input,
            panic_ptr);
        llvm::Value *resumed_async = materialize_as_type(resume_call, async_struct);
        if (!resumed_async)
        {
          resumed_async = llvm::Constant::getNullValue(async_struct);
        }
        builder.CreateStore(resumed_async, async_slot);
      };

      if (*comb_kind == AsyncCombinatorKind::Map)
      {
        if (call.args.size() < 2 || args.size() < 2)
        {
          finish_from(source_async);
          return;
        }
        llvm::BasicBlock *suspended_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.map.suspended", func);
        llvm::BasicBlock *merge_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.map.merge", func);
        llvm::Value *current_async = builder.CreateLoad(async_struct, async_slot);
        llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
        llvm::Value *is_suspended = EmitTypedEq(
            &builder,
            disc,
            llvm::ConstantInt::get(disc->getType(), suspended_disc));
        builder.CreateCondBr(AsBool(&builder, is_suspended), suspended_bb, merge_bb);

        builder.SetInsertPoint(suspended_bb);
        llvm::Value *output = load_payload_from_slot(async_slot, source_sig->out);
        llvm::Value *mapped = invoke_callable(
            call.args[1],
            {{output, source_sig->out}},
            "map_fn",
            result_sig->out);
        store_payload_to_slot(async_slot, mapped, result_sig->out);
        builder.CreateBr(merge_bb);

        builder.SetInsertPoint(merge_bb);
        finish_from(builder.CreateLoad(async_struct, async_slot));
        return;
      }

      if (*comb_kind == AsyncCombinatorKind::Filter)
      {
        if (call.args.size() < 2 || args.size() < 2)
        {
          finish_from(source_async);
          return;
        }
        llvm::BasicBlock *loop_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.filter.loop", func);
        llvm::BasicBlock *suspended_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.filter.suspended", func);
        llvm::BasicBlock *resume_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.filter.resume", func);
        llvm::BasicBlock *exit_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.filter.exit", func);
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(loop_bb);
        llvm::Value *current_async = builder.CreateLoad(async_struct, async_slot);
        llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
        auto *disc_ty = llvm::cast<llvm::IntegerType>(disc->getType());
        llvm::SwitchInst *sw = builder.CreateSwitch(
            disc, exit_bb, failed_disc.has_value() ? 3 : 2);
        sw->addCase(llvm::ConstantInt::get(disc_ty, suspended_disc), suspended_bb);
        sw->addCase(llvm::ConstantInt::get(disc_ty, completed_disc), exit_bb);
        if (failed_disc.has_value())
        {
          sw->addCase(llvm::ConstantInt::get(disc_ty, *failed_disc), exit_bb);
        }

        builder.SetInsertPoint(suspended_bb);
        llvm::Value *output = load_payload_from_slot(async_slot, source_sig->out);
        llvm::Value *pred_val = invoke_callable(
            call.args[1],
            {{output, source_sig->out}},
            "filter_pred",
            analysis::MakeTypePrim("bool"));
        builder.CreateCondBr(AsBool(&builder, pred_val), exit_bb, resume_bb);

        builder.SetInsertPoint(resume_bb);
        emit_resume_step();
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(exit_bb);
        finish_from(builder.CreateLoad(async_struct, async_slot));
        return;
      }

      if (*comb_kind == AsyncCombinatorKind::Take)
      {
        if (call.args.size() < 2 || args.size() < 2 || !result_sig->result)
        {
          finish_from(source_async);
          return;
        }
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
        auto load_count_value = [&](llvm::Value *storage) -> llvm::Value *
        {
          if (!storage || !storage->getType()->isPointerTy())
          {
            return nullptr;
          }
          llvm::Type *count_ptr_ty = llvm::PointerType::get(i64_ty, 0);
          if (storage->getType() != count_ptr_ty)
          {
            storage = builder.CreateBitCast(storage, count_ptr_ty);
          }
          return builder.CreateLoad(i64_ty, storage, "take.count");
        };

        llvm::Value *count = nullptr;
        if (llvm::Value *storage = emitter.GetAddressableStorage(call.args[1]))
        {
          count = load_count_value(storage);
        }
        if (!count)
        {
          count = args[1];
          if (count && count->getType()->isPointerTy())
          {
            if (llvm::Value *loaded = load_count_value(count))
            {
              count = loaded;
            }
          }
        }
        if (!count)
        {
          count = llvm::ConstantInt::get(i64_ty, 1);
        }
        if (count->getType() != i64_ty)
        {
          if (llvm::Value *coerced = CoerceTo(&builder, count, i64_ty))
          {
            count = coerced;
          }
          else
          {
            count = llvm::ConstantInt::get(i64_ty, 1);
          }
        }

        llvm::Value *source_ptr = builder.CreateBitCast(async_slot, opaque_ptr_ty);
        llvm::Value *taken = EmitRuntimeCallBySymbol(
            emitter,
            &builder,
            BuiltinSymAsyncTake(),
            {
                CoerceOrNullOpaquePtr(emitter, &builder, source_ptr),
                count,
                CoerceOrNullOpaquePtr(emitter, &builder, panic_ptr),
            });
        store_result(taken);
        finish_from(nullptr);
        return;
      }

      if (*comb_kind == AsyncCombinatorKind::Fold)
      {
        if (call.args.size() < 3 || args.size() < 3)
        {
          finish_from(source_async);
          return;
        }
        analysis::TypeRef acc_type = result_sig->result;
        llvm::Type *acc_ll =
            acc_type ? emitter.GetLLVMType(acc_type) : args[1]->getType();
        if (!acc_ll || acc_ll->isVoidTy())
        {
          acc_ll = args[1]->getType();
        }
        llvm::AllocaInst *acc_slot = entry_builder.CreateAlloca(acc_ll);
        auto load_acc_value = [&](llvm::Value *storage) -> llvm::Value *
        {
          if (!storage || !storage->getType()->isPointerTy())
          {
            return nullptr;
          }
          llvm::Type *acc_ptr_ty = llvm::PointerType::get(acc_ll, 0);
          if (storage->getType() != acc_ptr_ty)
          {
            storage = builder.CreateBitCast(storage, acc_ptr_ty);
          }
          return builder.CreateLoad(acc_ll, storage, "fold.acc.init");
        };

        llvm::Value *init_acc = nullptr;
        if (llvm::Value *storage = emitter.GetAddressableStorage(call.args[1]))
        {
          init_acc = load_acc_value(storage);
        }
        if (!init_acc)
        {
          init_acc = args[1];
          if (init_acc && init_acc->getType()->isPointerTy() && !acc_ll->isPointerTy())
          {
            if (llvm::Value *loaded = load_acc_value(init_acc))
            {
              init_acc = loaded;
            }
          }
        }
        if (!init_acc)
        {
          init_acc = llvm::Constant::getNullValue(acc_ll);
        }
        if (init_acc->getType() != acc_ll)
        {
          if (llvm::Value *coerced = CoerceTo(&builder, init_acc, acc_ll))
          {
            init_acc = coerced;
          }
          else
          {
            init_acc = materialize_as_type(init_acc, acc_ll);
          }
        }
        builder.CreateStore(init_acc, acc_slot);

        llvm::BasicBlock *loop_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.fold.loop", func);
        llvm::BasicBlock *suspended_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.fold.suspended", func);
        llvm::BasicBlock *resume_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.fold.resume", func);
        llvm::BasicBlock *completed_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.fold.completed", func);
        llvm::BasicBlock *failed_bb = failed_disc.has_value()
                                          ? llvm::BasicBlock::Create(
                                                emitter.GetContext(),
                                                "ac.fold.failed",
                                                func)
                                          : nullptr;
        llvm::BasicBlock *merge_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.fold.merge", func);
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(loop_bb);
        llvm::Value *current_async = builder.CreateLoad(async_struct, async_slot);
        llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
        auto *disc_ty = llvm::cast<llvm::IntegerType>(disc->getType());
        llvm::SwitchInst *sw = builder.CreateSwitch(
            disc, completed_bb, failed_disc.has_value() ? 3 : 2);
        sw->addCase(llvm::ConstantInt::get(disc_ty, suspended_disc), suspended_bb);
        sw->addCase(llvm::ConstantInt::get(disc_ty, completed_disc), completed_bb);
        if (failed_disc.has_value())
        {
          sw->addCase(llvm::ConstantInt::get(disc_ty, *failed_disc), failed_bb);
        }

        builder.SetInsertPoint(suspended_bb);
        llvm::Value *out = load_payload_from_slot(async_slot, source_sig->out);
        llvm::Value *acc = builder.CreateLoad(acc_ll, acc_slot);
        llvm::Value *next_acc = invoke_callable(
            call.args[2],
            {{acc, acc_type}, {out, source_sig->out}},
            "fold_fn",
            acc_type);
        if (next_acc->getType() != acc_ll)
        {
          if (llvm::Value *coerced = CoerceTo(&builder, next_acc, acc_ll))
          {
            next_acc = coerced;
          }
          else
          {
            next_acc = materialize_as_type(next_acc, acc_ll);
          }
        }
        if (!next_acc)
        {
          next_acc = llvm::Constant::getNullValue(acc_ll);
        }
        builder.CreateStore(next_acc, acc_slot);
        builder.CreateBr(resume_bb);

        builder.SetInsertPoint(resume_bb);
        emit_resume_step();
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(completed_bb);
        llvm::Value *final_acc = builder.CreateLoad(acc_ll, acc_slot);
        llvm::Value *complete = make_async_complete(final_acc, acc_type);
        store_result(complete);
        builder.CreateBr(merge_bb);

        if (failed_bb)
        {
          builder.SetInsertPoint(failed_bb);
          llvm::Value *err = load_payload_from_slot(async_slot, source_sig->err);
          llvm::Value *fail = make_async_fail(err, source_sig->err);
          store_result(fail);
          builder.CreateBr(merge_bb);
        }

        builder.SetInsertPoint(merge_bb);
        finish_from(nullptr);
        return;
      }

      if (*comb_kind == AsyncCombinatorKind::Chain)
      {
        if (call.args.size() < 2 || args.size() < 2)
        {
          finish_from(source_async);
          return;
        }
        llvm::BasicBlock *loop_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.chain.loop", func);
        llvm::BasicBlock *suspended_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.chain.suspended", func);
        llvm::BasicBlock *resume_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.chain.resume", func);
        llvm::BasicBlock *completed_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.chain.completed", func);
        llvm::BasicBlock *failed_bb = failed_disc.has_value()
                                          ? llvm::BasicBlock::Create(
                                                emitter.GetContext(),
                                                "ac.chain.failed",
                                                func)
                                          : nullptr;
        llvm::BasicBlock *merge_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "ac.chain.merge", func);
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(loop_bb);
        llvm::Value *current_async = builder.CreateLoad(async_struct, async_slot);
        llvm::Value *disc = builder.CreateExtractValue(current_async, {0u});
        auto *disc_ty = llvm::cast<llvm::IntegerType>(disc->getType());
        llvm::SwitchInst *sw = builder.CreateSwitch(
            disc, completed_bb, failed_disc.has_value() ? 3 : 2);
        sw->addCase(llvm::ConstantInt::get(disc_ty, suspended_disc), suspended_bb);
        sw->addCase(llvm::ConstantInt::get(disc_ty, completed_disc), completed_bb);
        if (failed_disc.has_value())
        {
          sw->addCase(llvm::ConstantInt::get(disc_ty, *failed_disc), failed_bb);
        }

        builder.SetInsertPoint(suspended_bb);
        builder.CreateBr(resume_bb);

        builder.SetInsertPoint(resume_bb);
        emit_resume_step();
        builder.CreateBr(loop_bb);

        builder.SetInsertPoint(completed_bb);
        llvm::Value *completed_value = load_payload_from_slot(async_slot, source_sig->result);
        llvm::Value *chained_async = invoke_callable(
            call.args[1],
            {{completed_value, source_sig->result}},
            "chain_fn",
            result_async_type);
        store_result(chained_async);
        builder.CreateBr(merge_bb);

        if (failed_bb)
        {
          builder.SetInsertPoint(failed_bb);
          llvm::Value *err = load_payload_from_slot(async_slot, source_sig->err);
          llvm::Value *fail = make_async_fail(err, source_sig->err);
          store_result(fail);
          builder.CreateBr(merge_bb);
        }

        builder.SetInsertPoint(merge_bb);
        finish_from(nullptr);
        return;
      }

      finish_from(source_async);
      return;
    }
  }

  const LowerCtx *ctx = emitter.GetCurrentCtx();
  const LowerCtx::ProcSigInfo *sig = nullptr;
  LowerCtx::ProcSigInfo inferred_sig;
  LowerCtx::ProcSigInfo callable_type_adjusted_sig;
  LowerCtx::ProcSigInfo closure_adjusted_sig;
  LowerCtx::ProcSigInfo closure_code_adjusted_sig;
  LowerCtx::ProcSigInfo hosted_adjusted_sig;
  std::string callee_symbol = call.callee.name;
  bool sig_is_concrete_local_callable = false;
  bool callee_is_closure_code_component = false;
  const bool is_async_resume_runtime_symbol =
      (call.callee.kind == IRValue::Kind::Symbol) &&
      (call.callee.name == BuiltinSymAsyncResume());
  if (is_async_resume_runtime_symbol)
  {
    llvm::Type *opaque_ptr_ty = emitter.GetOpaquePtr();
    auto *opaque_ptr_ptr_ty = llvm::cast<llvm::PointerType>(opaque_ptr_ty);
    llvm::Function *func =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;

    auto materialize_resume_pointer =
        [&](std::size_t index, std::string_view name) -> llvm::Value *
    {
      if (index >= args.size() || index >= call.args.size() || !func)
      {
        return llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }

      if (llvm::Value *storage = emitter.GetAddressableStorage(call.args[index]))
      {
        if (llvm::Value *coerced = CoerceTo(&builder, storage, opaque_ptr_ty))
        {
          return coerced;
        }
        if (storage->getType()->isPointerTy())
        {
          return builder.CreateBitCast(storage, opaque_ptr_ty);
        }
      }

      llvm::Value *value = args[index];
      if (!value)
      {
        return llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }

      analysis::TypeRef value_type = ctx ? ctx->LookupValueType(call.args[index]) : nullptr;
      if (index == 1 && value_type && IsUnitTypeRef(analysis::StripPerm(value_type)))
      {
        return llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }

      llvm::Type *storage_ty = value_type ? emitter.GetLLVMType(value_type) : nullptr;
      if (!storage_ty || storage_ty->isVoidTy())
      {
        storage_ty = value->getType();
      }
      if (!storage_ty || storage_ty->isVoidTy())
      {
        return llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
      }

      llvm::IRBuilder<> entry_builder(
          &func->getEntryBlock(),
          func->getEntryBlock().begin());
      llvm::AllocaInst *slot = entry_builder.CreateAlloca(
          storage_ty,
          nullptr,
          llvm::Twine("async.resume.") + llvm::StringRef(name.data(), name.size()));
      llvm::Value *stored = value;
      if (stored->getType() != storage_ty)
      {
        if (llvm::Value *typed = CoerceToTyped(
                emitter,
                &builder,
                stored,
                storage_ty,
                value_type,
                value_type))
        {
          stored = typed;
        }
        else if (llvm::Value *plain = CoerceTo(&builder, stored, storage_ty))
        {
          stored = plain;
        }
        else
        {
          stored = llvm::Constant::getNullValue(storage_ty);
        }
      }
      builder.CreateStore(stored, slot);
      return builder.CreateBitCast(slot, opaque_ptr_ty);
    };

    args[0] = materialize_resume_pointer(0, "suspended");
    if (args.size() >= 2)
    {
      args[1] = materialize_resume_pointer(1, "input");
    }

    llvm::Value *panic_arg_value =
        LoadLocalValue(emitter, &builder, std::string(kPanicOutName));
    if (panic_arg_value)
    {
      if (llvm::Value *coerced = CoerceTo(&builder, panic_arg_value, opaque_ptr_ty))
      {
        panic_arg_value = coerced;
      }
      else if (panic_arg_value->getType()->isPointerTy())
      {
        panic_arg_value = builder.CreateBitCast(panic_arg_value, opaque_ptr_ty);
      }
      else
      {
        panic_arg_value = nullptr;
      }
    }
    if (!panic_arg_value)
    {
      panic_arg_value = llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty);
    }

    if (args.size() < 3)
    {
      args.resize(3, llvm::ConstantPointerNull::get(opaque_ptr_ptr_ty));
    }
    if (!args[2] || llvm::isa<llvm::ConstantPointerNull>(args[2]))
    {
      args[2] = panic_arg_value;
    }
  }
  const bool is_foundational_eq_symbol =
      (call.callee.kind == IRValue::Kind::Symbol) &&
      (call.callee.name == BuiltinSymEqEq());
  const bool is_foundational_step_successor_symbol =
      (call.callee.kind == IRValue::Kind::Symbol) &&
      (call.callee.name == BuiltinSymStepSuccessor());
  const bool is_foundational_step_predecessor_symbol =
      (call.callee.kind == IRValue::Kind::Symbol) &&
      (call.callee.name == BuiltinSymStepPredecessor());
  if (is_foundational_eq_symbol ||
      is_foundational_step_successor_symbol ||
      is_foundational_step_predecessor_symbol)
  {
    auto report_builtin_failure = [&]()
    {
      if (ctx)
      {
        const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
      }
      emitter.SetTempValue(call.result, DefaultFor(call.result));
    };

    if (args.empty())
    {
      report_builtin_failure();
      return;
    }

    const analysis::TypeRef recv_type = NormalizeValueType(call.args[0]);
    if (!recv_type)
    {
      report_builtin_failure();
      return;
    }

    if (is_foundational_eq_symbol)
    {
      if (args.size() < 2)
      {
        report_builtin_failure();
        return;
      }
      llvm::Value *eq = EmitBuiltinEqCall(builder, recv_type, args[0], args[1]);
      if (!eq)
      {
        report_builtin_failure();
        return;
      }
      emitter.SetTempValue(call.result, eq);
      return;
    }

    const analysis::TypeRef result_type = NormalizeValueType(call.result);
    llvm::Type *target_ll = ExpectedLLVMType(call.result);
    if (!target_ll && result_type)
    {
      target_ll = emitter.GetLLVMType(result_type);
    }
    if (!result_type || !target_ll)
    {
      report_builtin_failure();
      return;
    }

    const auto step_result =
        is_foundational_step_successor_symbol
            ? EmitBuiltinSuccessor(builder, recv_type, args[0])
            : EmitBuiltinPredecessor(builder, recv_type, args[0]);
    if (!step_result.has_value())
    {
      report_builtin_failure();
      return;
    }

    llvm::Value *packed_some = PackUnionFromMember(
        emitter, &builder, step_result->next, target_ll, recv_type, result_type);
    const analysis::TypeRef unit_type = analysis::MakeTypePrim("()");
    llvm::Type *unit_ll = emitter.GetLLVMType(unit_type);
    llvm::Value *unit_value = unit_ll ? llvm::UndefValue::get(unit_ll) : nullptr;
    llvm::Value *packed_none = PackUnionFromMember(
        emitter, &builder, unit_value, target_ll, unit_type, result_type);
    llvm::Function *fn =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (!packed_some || !packed_none || !fn)
    {
      report_builtin_failure();
      return;
    }

    llvm::BasicBlock *step_some_bb =
        llvm::BasicBlock::Create(emitter.GetContext(), "step.some", fn);
    llvm::BasicBlock *step_none_bb =
        llvm::BasicBlock::Create(emitter.GetContext(), "step.none", fn);
    llvm::BasicBlock *step_merge_bb =
        llvm::BasicBlock::Create(emitter.GetContext(), "step.merge", fn);
    builder.CreateCondBr(step_result->has_next, step_some_bb, step_none_bb);

    builder.SetInsertPoint(step_some_bb);
    builder.CreateBr(step_merge_bb);
    llvm::BasicBlock *step_some_end = builder.GetInsertBlock();

    builder.SetInsertPoint(step_none_bb);
    builder.CreateBr(step_merge_bb);
    llvm::BasicBlock *step_none_end = builder.GetInsertBlock();

    builder.SetInsertPoint(step_merge_bb);
    llvm::PHINode *phi = builder.CreatePHI(target_ll, 2);
    phi->addIncoming(packed_some, step_some_end);
    phi->addIncoming(packed_none, step_none_end);
    emitter.SetTempValue(call.result, phi);
    return;
  }
  bool use_c_abi_aggregate_sret = false;
  bool runtime_foreign_boundary = false;
  if (call.callee.kind == IRValue::Kind::Symbol)
  {
    if (auto alias = emitter.LookupSymbolAlias(call.callee.name))
    {
      callee_symbol = *alias;
    }
    if (auto runtime = GetRuntimeFuncInfo(callee_symbol))
    {
      inferred_sig.params = runtime->params;
      inferred_sig.ret = runtime->ret;
      sig = &inferred_sig;
      runtime_foreign_boundary = RuntimeUsesForeignABI(callee_symbol);
    }
    else if (auto runtime = GetRuntimeFuncInfo(call.callee.name))
    {
      inferred_sig.params = runtime->params;
      inferred_sig.ret = runtime->ret;
      sig = &inferred_sig;
      callee_symbol = call.callee.name;
      runtime_foreign_boundary = RuntimeUsesForeignABI(call.callee.name);
    }
    if (!sig && ctx)
    {
      sig = ctx->LookupProcSig(callee_symbol);
      if (!sig)
      {
        sig = ctx->LookupProcSig(call.callee.name);
      }
    }
    if (!sig)
    {
      if (auto runtime = GetRuntimeFuncInfo(callee_symbol))
      {
        inferred_sig.params = runtime->params;
        inferred_sig.ret = runtime->ret;
        sig = &inferred_sig;
        runtime_foreign_boundary = RuntimeUsesForeignABI(callee_symbol);
      }
      else if (auto runtime = GetRuntimeFuncInfo(call.callee.name))
      {
        inferred_sig.params = runtime->params;
        inferred_sig.ret = runtime->ret;
        sig = &inferred_sig;
        callee_symbol = call.callee.name;
        runtime_foreign_boundary = RuntimeUsesForeignABI(call.callee.name);
      }
    }
    runtime_foreign_boundary =
        runtime_foreign_boundary ||
        RuntimeUsesForeignABI(callee_symbol) ||
        RuntimeUsesForeignABI(call.callee.name);
  }
  if (!sig && ctx)
  {
    auto is_complete_sig = [](const LowerCtx::ProcSigInfo &info) -> bool
    {
      if (!info.ret)
      {
        return false;
      }
      for (const auto &param : info.params)
      {
        if (!param.type)
        {
          return false;
        }
      }
      return true;
    };
    // Prefer concrete closure-code signatures over inferred function types.
    // This preserves ABI decisions (notably sret) when analysis-level closure
    // types are still partially inferred.
    std::function<std::optional<std::string>(const IRValue &, unsigned)> closure_code_symbol =
        [&](const IRValue &value, unsigned depth) -> std::optional<std::string>
    {
      if (depth > 8)
      {
        return std::nullopt;
      }
      if (value.kind == IRValue::Kind::Symbol)
      {
        return value.name;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(value);
      if (!derived)
      {
        return std::nullopt;
      }
      if (derived->kind == DerivedValueInfo::Kind::TupleLit &&
          derived->elements.size() > 1)
      {
        return closure_code_symbol(derived->elements[1], depth + 1);
      }
      if (derived->kind == DerivedValueInfo::Kind::Tuple &&
          derived->tuple_index == 1)
      {
        return closure_code_symbol(derived->base, depth + 1);
      }
      if (derived->kind == DerivedValueInfo::Kind::LoadFromAddr ||
          derived->kind == DerivedValueInfo::Kind::AddrTuple ||
          derived->kind == DerivedValueInfo::Kind::AddrLocal)
      {
        return closure_code_symbol(derived->base, depth + 1);
      }
      return std::nullopt;
    };
    std::function<bool(const IRValue &, unsigned)> is_closure_code_component =
        [&](const IRValue &value, unsigned depth) -> bool
    {
      if (depth > 8)
      {
        return false;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(value);
      if (!derived)
      {
        return false;
      }
      if (derived->kind == DerivedValueInfo::Kind::Tuple &&
          derived->tuple_index == 1)
      {
        return true;
      }
      if (derived->kind == DerivedValueInfo::Kind::LoadFromAddr ||
          derived->kind == DerivedValueInfo::Kind::AddrTuple ||
          derived->kind == DerivedValueInfo::Kind::AddrLocal)
      {
        return is_closure_code_component(derived->base, depth + 1);
      }
      return false;
    };

    if (call.callee.kind == IRValue::Kind::Opaque)
    {
      if (const DerivedValueInfo *derived = ctx->LookupDerivedValue(call.callee))
      {
        if (derived->kind == DerivedValueInfo::Kind::Tuple &&
            derived->tuple_index == 1)
        {
          if (const DerivedValueInfo *base_derived =
                  ctx->LookupDerivedValue(derived->base))
          {
            if (base_derived->kind == DerivedValueInfo::Kind::TupleLit &&
                base_derived->elements.size() > 1)
            {
              const IRValue &code_elem = base_derived->elements[1];
              if (code_elem.kind == IRValue::Kind::Symbol)
              {
                if (const LowerCtx::ProcSigInfo *concrete =
                        ctx->LookupProcSig(code_elem.name))
                {
                  sig = concrete;
                  callee_symbol = code_elem.name;
                  sig_is_concrete_local_callable = true;
                  callee_is_closure_code_component = true;
                }
              }
            }
          }
        }
      }
    }

    if (!sig)
    {
      if (std::optional<std::string> closure_symbol =
              closure_code_symbol(call.callee, 0))
      {
        if (const LowerCtx::ProcSigInfo *concrete =
                ctx->LookupProcSig(*closure_symbol))
        {
          sig = concrete;
          callee_symbol = *closure_symbol;
          sig_is_concrete_local_callable = true;
          callee_is_closure_code_component =
              is_closure_code_component(call.callee, 0);
        }
      }
    }

    analysis::TypeRef callee_type = analysis::StripPerm(ctx->LookupValueType(call.callee));
    if (!callee_type)
    {
      callee_type = ctx->LookupValueType(call.callee);
    }
    if (!callee_type && call.callee.kind == IRValue::Kind::Local)
    {
      callee_type = analysis::StripPerm(emitter.LookupLocalType(call.callee.name));
      if (!callee_type)
      {
        callee_type = emitter.LookupLocalType(call.callee.name);
      }
    }
    if (callee_type)
    {
      const analysis::ScopeContext scope = BuildScope(ctx);
      if (analysis::TypeRef resolved =
              ResolveAliasTypeInScope(scope, callee_type))
      {
        callee_type = resolved;
      }
      if (analysis::TypeRef stripped = analysis::StripPerm(callee_type))
      {
        callee_type = stripped;
      }
    }
    if (!sig && !callee_type && call.callee.kind == IRValue::Kind::Opaque)
    {
      if (const DerivedValueInfo *derived = ctx->LookupDerivedValue(call.callee))
      {
        if (derived->kind == DerivedValueInfo::Kind::Tuple &&
            derived->tuple_index == 1)
        {
          analysis::TypeRef base_type =
              analysis::StripPerm(ctx->LookupValueType(derived->base));
          if (!base_type)
          {
            base_type = ctx->LookupValueType(derived->base);
          }
          if (const auto *closure = base_type
                                        ? std::get_if<analysis::TypeClosure>(&base_type->node)
                                        : nullptr)
          {
            std::vector<analysis::TypeFuncParam> params;
            analysis::TypeFuncParam env_param;
            env_param.mode = analysis::ParamMode::Move;
            env_param.type = analysis::MakeTypePtr(
                analysis::MakeTypePrim("u8"),
                analysis::PtrState::Valid);
            params.push_back(std::move(env_param));
            for (const auto &[is_move, param_type] : closure->params)
            {
              analysis::TypeFuncParam p;
              if (is_move)
              {
                p.mode = analysis::ParamMode::Move;
              }
              p.type = param_type;
              params.push_back(std::move(p));
            }
            analysis::TypeFuncParam panic_param;
            panic_param.mode = analysis::ParamMode::Move;
            panic_param.type = PanicOutType();
            params.push_back(std::move(panic_param));
            callee_type = analysis::MakeTypeFunc(std::move(params), closure->ret);
          }
          else if (const auto *tuple = base_type
                                           ? std::get_if<analysis::TypeTuple>(&base_type->node)
                                           : nullptr)
          {
            if (tuple->elements.size() == 2)
            {
              callee_type = analysis::StripPerm(tuple->elements[1]);
              if (!callee_type)
              {
                callee_type = tuple->elements[1];
              }
            }
          }
        }
      }
    }
    if (!sig)
    {
      if (const auto *closure = callee_type
                                    ? std::get_if<analysis::TypeClosure>(&callee_type->node)
                                    : nullptr)
      {
        inferred_sig.params.clear();
        analysis::TypeFuncParam env_func_param;
        env_func_param.mode = analysis::ParamMode::Move;
        env_func_param.type = analysis::MakeTypePtr(
            analysis::MakeTypePrim("u8"),
            analysis::PtrState::Valid);
        IRParam env_ir_param;
        env_ir_param.mode = env_func_param.mode;
        env_ir_param.name = "env";
        env_ir_param.type = env_func_param.type;
        inferred_sig.params.push_back(std::move(env_ir_param));
        for (std::size_t i = 0; i < closure->params.size(); ++i)
        {
          IRParam p;
          if (closure->params[i].first)
          {
            p.mode = analysis::ParamMode::Move;
          }
          p.name = "arg" + std::to_string(i);
          p.type = closure->params[i].second;
          inferred_sig.params.push_back(std::move(p));
        }
        // Closure calls lower as env + user args + hidden panic-out.
        if (call.args.size() == closure->params.size() + 2)
        {
          inferred_sig.params.push_back(PanicOutParam());
        }
        inferred_sig.ret = closure->ret;
        if (is_complete_sig(inferred_sig))
        {
          sig = &inferred_sig;
        }
      }
      else if (const auto *fn = callee_type
                                    ? std::get_if<analysis::TypeFunc>(&callee_type->node)
                                    : nullptr)
      {
        inferred_sig.params.clear();
        inferred_sig.params.reserve(fn->params.size());
        for (std::size_t i = 0; i < fn->params.size(); ++i)
        {
          IRParam p;
          p.mode = fn->params[i].mode;
          p.name = "arg" + std::to_string(i);
          p.type = fn->params[i].type;
          inferred_sig.params.push_back(std::move(p));
        }
        // Hidden panic-out is part of LoweredSigOf even when SigOf comes
        // from ExprType(callee)=TypeFunc(...). If lowering appended one extra
        // call argument, recover that ABI parameter here to keep call
        // signatures consistent for indirect procedure values.
        if (call.args.size() == inferred_sig.params.size() + 1)
        {
          inferred_sig.params.push_back(PanicOutParam());
        }
        inferred_sig.ret = fn->ret;
        if (is_complete_sig(inferred_sig))
        {
          sig = &inferred_sig;
        }
      }
    }

    // Recover concrete signatures for local procedure values bound from a
    // symbol (e.g. `let f: (i32,i32)->i32 = EqAdd; f(...)`). Local
    // binding states are popped after lowering; recover from the local
    // alloca store to keep indirect-call emission aligned with spec
    // LoweredSigOf/NeedsPanicOut requirements.
    if (call.callee.kind == IRValue::Kind::Local)
    {
      if (llvm::Value *local_slot = emitter.GetLocal(call.callee.name))
      {
        std::function<llvm::Function *(llvm::Value *, unsigned)> stored_function_from_value =
            [&](llvm::Value *value, unsigned depth) -> llvm::Function *
        {
          if (!value || depth > 8)
          {
            return nullptr;
          }
          if (llvm::Function *fn = FunctionFromLLVMValue(value))
          {
            return fn;
          }
          if (auto *load = llvm::dyn_cast<llvm::LoadInst>(value))
          {
            return stored_function_from_value(load->getPointerOperand(), depth + 1);
          }
          if (auto *cast = llvm::dyn_cast<llvm::CastInst>(value))
          {
            return stored_function_from_value(cast->getOperand(0), depth + 1);
          }
          if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
          {
            for (llvm::User *user : alloca->users())
            {
              auto *store = llvm::dyn_cast<llvm::StoreInst>(user);
              if (!store || store->getPointerOperand() != alloca)
              {
                continue;
              }
              if (llvm::Function *fn =
                      stored_function_from_value(store->getValueOperand(), depth + 1))
              {
                return fn;
              }
            }
          }
          return nullptr;
        };

        llvm::Function *stored_fn = stored_function_from_value(local_slot, 0);
        if (!stored_fn)
        {
          if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(local_slot))
          {
            for (llvm::User *user : alloca->users())
            {
              auto *store = llvm::dyn_cast<llvm::StoreInst>(user);
              if (!store || store->getPointerOperand() != alloca)
              {
                continue;
              }
              if (llvm::Function *fn = FunctionFromLLVMValue(store->getValueOperand()))
              {
                stored_fn = fn;
                break;
              }
            }
          }
          else
          {
            stored_fn = FunctionFromLLVMValue(local_slot);
          }
        }

        if (stored_fn)
        {
          const std::string stored_name = std::string(stored_fn->getName());
          if (const LowerCtx::ProcSigInfo *concrete = ctx->LookupProcSig(stored_name))
          {
            sig = concrete;
            callee_symbol = stored_name;
            sig_is_concrete_local_callable = true;
          }
        }
      }
    }

    if (sig && is_closure_code_component(call.callee, 0) &&
        !sig->params.empty())
    {
      analysis::TypeRef first_param_type =
          analysis::StripPerm(sig->params.front().type);
      if (!first_param_type)
      {
        first_param_type = sig->params.front().type;
      }
      if (first_param_type &&
          std::holds_alternative<analysis::TypeRawPtr>(first_param_type->node))
      {
        closure_code_adjusted_sig = *sig;
        closure_code_adjusted_sig.params.front().name = "__env";
        closure_code_adjusted_sig.params.front().mode =
            analysis::ParamMode::Move;
        sig = &closure_code_adjusted_sig;
        callee_is_closure_code_component = true;
      }
    }
  }

  if (sig && emitter.RequiresHostedEnvParam(callee_symbol))
  {
    hosted_adjusted_sig = *sig;
    if (!HasLeadingHostedEnvParam(hosted_adjusted_sig.params))
    {
      hosted_adjusted_sig.params.insert(
          hosted_adjusted_sig.params.begin(),
          HostedEnvParam());
    }
    sig = &hosted_adjusted_sig;
    llvm::Value *env_arg = emitter.GetHostedCurrentEnvPtr();
    if (!env_arg)
    {
      env_arg = llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(emitter.GetOpaquePtr()));
    }
    args.insert(args.begin(), env_arg);
  }

  const bool raw_export_boundary =
      ctx && ctx->LookupExportUnwindMode(callee_symbol).has_value();
  if (runtime_foreign_boundary ||
      (sig && (sig->ffi_import || raw_export_boundary)))
  {
    // Foreign-boundary-visible signatures must honor platform C ABI
    // aggregate-return lowering, including hidden sret where required.
    use_c_abi_aggregate_sret = true;
  }

  llvm::Value *callee = emitter.EvaluateIRValue(call.callee);
  if (!callee && call.callee.kind == IRValue::Kind::Symbol)
  {
    if (llvm::Function *existing = emitter.GetModule().getFunction(callee_symbol))
    {
      callee = existing;
    }
    else
    {
      llvm::FunctionType *decl_ty = nullptr;
      if (sig)
      {
        ABICallResult abi = ComputeCallABI(
            emitter,
            sig->params,
            sig->ret,
            use_c_abi_aggregate_sret,
            /*foreign_boundary_mode_independent=*/false);
        decl_ty = abi.func_type;
      }
      if (!decl_ty && IsRuntimeFunction(callee_symbol))
      {
        llvm::Type *ret_ty = ExpectedLLVMType(call.result);
        if (!ret_ty)
        {
          ret_ty = llvm::Type::getVoidTy(emitter.GetContext());
        }
        std::vector<llvm::Type *> arg_tys;
        arg_tys.reserve(args.size());
        for (llvm::Value *arg : args)
        {
          arg_tys.push_back(arg ? arg->getType() : emitter.GetOpaquePtr());
        }
        decl_ty = llvm::FunctionType::get(ret_ty, arg_tys, false);
      }
      if (decl_ty)
      {
        llvm::Function *declared = llvm::Function::Create(
            decl_ty,
            llvm::GlobalValue::ExternalLinkage,
            callee_symbol,
            &emitter.GetModule());
        declared->setCallingConv(
            sig ? CallingConvForAbi(sig->abi) : llvm::CallingConv::C);
        callee = declared;
      }
    }
  }

  auto extract_value_index = [](llvm::Value *value) -> std::optional<unsigned>
  {
    auto *extract = llvm::dyn_cast_or_null<llvm::ExtractValueInst>(value);
    if (!extract || extract->getNumIndices() != 1)
    {
      return std::nullopt;
    }
    return *extract->idx_begin();
  };

  std::function<llvm::Function *(llvm::Value *, unsigned)> function_from_value;
  std::function<llvm::Function *(llvm::Value *, unsigned, unsigned)>
      function_from_aggregate_element;

  function_from_value = [&](llvm::Value *value, unsigned depth) -> llvm::Function *
  {
    if (!value || depth > 8)
    {
      return nullptr;
    }
    if (llvm::Function *fn = FunctionFromLLVMValue(value))
    {
      return fn;
    }
    if (auto *extract = llvm::dyn_cast<llvm::ExtractValueInst>(value))
    {
      if (extract->getNumIndices() == 1)
      {
        return function_from_aggregate_element(
            extract->getAggregateOperand(), *extract->idx_begin(), depth + 1);
      }
    }
    if (auto *load = llvm::dyn_cast<llvm::LoadInst>(value))
    {
      return function_from_value(load->getPointerOperand(), depth + 1);
    }
    if (auto *cast = llvm::dyn_cast<llvm::CastInst>(value))
    {
      return function_from_value(cast->getOperand(0), depth + 1);
    }
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
      llvm::Function *found = nullptr;
      for (llvm::User *user : alloca->users())
      {
        auto *store = llvm::dyn_cast<llvm::StoreInst>(user);
        if (!store || store->getPointerOperand() != alloca)
        {
          continue;
        }
        llvm::Function *stored =
            function_from_value(store->getValueOperand(), depth + 1);
        if (!stored)
        {
          continue;
        }
        if (found && found != stored)
        {
          return nullptr;
        }
        found = stored;
      }
      return found;
    }
    return nullptr;
  };

  function_from_aggregate_element =
      [&](llvm::Value *aggregate, unsigned index, unsigned depth) -> llvm::Function *
  {
    if (!aggregate || depth > 8)
    {
      return nullptr;
    }
    if (auto *insert = llvm::dyn_cast<llvm::InsertValueInst>(aggregate))
    {
      if (insert->getNumIndices() == 1 && *insert->idx_begin() == index)
      {
        return function_from_value(insert->getInsertedValueOperand(), depth + 1);
      }
      return function_from_aggregate_element(
          insert->getAggregateOperand(), index, depth + 1);
    }
    if (auto *load = llvm::dyn_cast<llvm::LoadInst>(aggregate))
    {
      return function_from_aggregate_element(
          load->getPointerOperand(), index, depth + 1);
    }
    if (auto *cast = llvm::dyn_cast<llvm::CastInst>(aggregate))
    {
      return function_from_aggregate_element(cast->getOperand(0), index, depth + 1);
    }
    if (auto *constant = llvm::dyn_cast<llvm::Constant>(aggregate))
    {
      if (llvm::Constant *element = constant->getAggregateElement(index))
      {
        return function_from_value(element, depth + 1);
      }
    }
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(aggregate))
    {
      llvm::Function *found = nullptr;
      for (llvm::User *user : alloca->users())
      {
        auto *store = llvm::dyn_cast<llvm::StoreInst>(user);
        if (!store || store->getPointerOperand() != alloca)
        {
          continue;
        }
        llvm::Function *stored =
            function_from_aggregate_element(store->getValueOperand(), index, depth + 1);
        if (!stored)
        {
          continue;
        }
        if (found && found != stored)
        {
          return nullptr;
        }
        found = stored;
      }
      return found;
    }
    return nullptr;
  };

  const std::optional<unsigned> callee_extract_index = extract_value_index(callee);
  if (ctx && callee_extract_index.has_value() && *callee_extract_index == 1)
  {
    if (llvm::Function *closure_fn = function_from_value(callee, 0))
    {
      const std::string closure_name = std::string(closure_fn->getName());
      if (const LowerCtx::ProcSigInfo *concrete = ctx->LookupProcSig(closure_name))
      {
        sig = concrete;
        callee_symbol = closure_name;
        sig_is_concrete_local_callable = true;
        callee_is_closure_code_component = true;
        callee = closure_fn;
      }
    }
  }

  if (ctx)
  {
    if (llvm::Function *concrete_fn = function_from_value(callee, 0))
    {
      const std::string concrete_name = std::string(concrete_fn->getName());
      if (const LowerCtx::ProcSigInfo *concrete =
              ctx->LookupProcSig(concrete_name))
      {
        sig = concrete;
        callee_symbol = concrete_name;
        sig_is_concrete_local_callable = true;
        callee = concrete_fn;
      }
    }
  }

  const bool unresolved_drop_glue =
      call.callee.kind == IRValue::Kind::Symbol &&
      (IsDropGlueSymbol(callee_symbol) || IsDropGlueSymbol(call.callee.name));

  if (!callee)
  {
    if (!unresolved_drop_glue &&
        core::IsDebugEnabled("call"))
    {
      llvm::Function *caller_fn =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      const std::string caller_name =
          caller_fn ? caller_fn->getName().str() : std::string("<no-func>");
      std::fprintf(stderr,
                   "[uv] unresolved call: caller=%s kind=%d callee=%s resolved=%s args=%zu\n",
                   caller_name.c_str(),
                   static_cast<int>(call.callee.kind),
                   call.callee.name.c_str(),
                   callee_symbol.c_str(),
                   args.size());
    }
    if (!unresolved_drop_glue && ctx)
    {
      const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
    }
    emitter.SetTempValue(call.result, DefaultFor(call.result));
    return;
  }

  bool callee_is_closure_pair = false;
  if (IsClosurePairLLVMType(callee->getType()))
  {
    llvm::Value *env_ptr = builder.CreateExtractValue(callee, {0u});
    llvm::Value *code_ptr = builder.CreateExtractValue(callee, {1u});
    if (env_ptr && code_ptr)
    {
      args.insert(args.begin(), env_ptr);
      callee = code_ptr;
      callee_is_closure_pair = true;
      if (ctx)
      {
        if (llvm::Function *closure_fn = FunctionFromLLVMValue(callee))
        {
          const std::string closure_name = std::string(closure_fn->getName());
          if (const LowerCtx::ProcSigInfo *concrete = ctx->LookupProcSig(closure_name))
          {
            sig = concrete;
            callee_symbol = closure_name;
            sig_is_concrete_local_callable = true;
          }
        }
      }
    }
  }

  if (callee_is_closure_pair && sig &&
      sig->params.size() + 1 == args.size())
  {
    closure_adjusted_sig = *sig;
    IRParam env_param;
    env_param.mode = analysis::ParamMode::Move;
    env_param.name = "__env";
    env_param.type = analysis::MakeTypeRawPtr(
        analysis::RawPtrQual::Imm,
        analysis::MakeTypePrim("u8"));
    closure_adjusted_sig.params.insert(
        closure_adjusted_sig.params.begin(),
        std::move(env_param));
    sig = &closure_adjusted_sig;
  }

  if (!sig && ctx)
  {
    llvm::Function *fn = FunctionFromLLVMValue(callee);
    if (fn)
    {
      const std::string fn_name = std::string(fn->getName());
      sig = ctx->LookupProcSig(fn_name);
      if (!sig && !callee_symbol.empty())
      {
        sig = ctx->LookupProcSig(callee_symbol);
      }
    }
  }

  if (sig && ctx && call.callee.kind != IRValue::Kind::Symbol &&
      !sig_is_concrete_local_callable)
  {
    analysis::TypeRef callable_type =
        analysis::StripPerm(ctx->LookupValueType(call.callee));
    if (!callable_type)
    {
      callable_type = ctx->LookupValueType(call.callee);
    }
    if (!callable_type && call.callee.kind == IRValue::Kind::Local)
    {
      callable_type =
          analysis::StripPerm(emitter.LookupLocalType(call.callee.name));
      if (!callable_type)
      {
        callable_type = emitter.LookupLocalType(call.callee.name);
      }
    }
    if (callable_type)
    {
      const analysis::ScopeContext scope = BuildScope(ctx);
      if (analysis::TypeRef resolved =
              ResolveAliasTypeInScope(scope, callable_type))
      {
        callable_type = analysis::StripPerm(resolved);
        if (!callable_type)
        {
          callable_type = resolved;
        }
      }
    }

    if (const auto *fn_type = callable_type
                                  ? std::get_if<analysis::TypeFunc>(
                                        &callable_type->node)
                                  : nullptr)
    {
      const bool compatible =
          fn_type->params.size() == sig->params.size() ||
          (fn_type->params.size() + 1 == sig->params.size() &&
           sig->params.back().name == std::string(kPanicOutName));
      if (compatible)
      {
        callable_type_adjusted_sig = *sig;
        for (std::size_t i = 0; i < fn_type->params.size(); ++i)
        {
          callable_type_adjusted_sig.params[i].mode = fn_type->params[i].mode;
          callable_type_adjusted_sig.params[i].type = fn_type->params[i].type;
        }
        callable_type_adjusted_sig.ret = fn_type->ret;
        sig = &callable_type_adjusted_sig;
      }
    }
    else if (const auto *closure = callable_type
                                      ? std::get_if<analysis::TypeClosure>(
                                            &callable_type->node)
                                      : nullptr)
    {
      const bool compatible =
          closure->params.size() + 1 == sig->params.size() ||
          (closure->params.size() + 2 == sig->params.size() &&
           sig->params.back().name == std::string(kPanicOutName));
      if (compatible)
      {
        callable_type_adjusted_sig = *sig;
        for (std::size_t i = 0; i < closure->params.size(); ++i)
        {
          callable_type_adjusted_sig.params[i + 1].mode =
              closure->params[i].first
                  ? std::optional<analysis::ParamMode>(analysis::ParamMode::Move)
                  : std::nullopt;
          callable_type_adjusted_sig.params[i + 1].type =
              closure->params[i].second;
        }
        callable_type_adjusted_sig.ret = closure->ret;
        sig = &callable_type_adjusted_sig;
      }
    }
  }

  if (sig_is_concrete_local_callable &&
      callee_is_closure_code_component &&
      sig &&
      sig->params.size() + 1 == args.size())
  {
    args.erase(args.begin());
  }

  llvm::Value *call_result = nullptr;
  llvm::Value *call_result_storage = nullptr;
  if (call.callee.kind == IRValue::Kind::Symbol && ctx)
  {
    if (const auto *proc_module = ctx->LookupProcModule(callee_symbol))
    {
      SPEC_RULE("LowerIRInstr-CheckPoison");
      emitter.EmitPoisonCheck(core::StringOfPath(*proc_module));
    }
  }
  if (sig)
  {
    llvm::Value *preferred_result_storage =
        emitter.TakePreferredResultStorage(call.result);
    const bool ffi_import_boundary = sig->ffi_import;
    const bool ffi_import_catch = ffi_import_boundary &&
        sig->ffi_import_unwind_mode ==
            LowerCtx::FfiImportUnwindMode::Catch;
    const bool foreign_boundary_mode_independent =
        ffi_import_boundary || raw_export_boundary;
    call_result = EmitABICall(
        emitter,
        &builder,
        callee,
        sig->params,
        sig->ret,
        args,
        use_c_abi_aggregate_sret,
        ffi_import_boundary,
        ffi_import_catch,
        std::nullopt,
        &call.args,
        &call_result_storage,
        preferred_result_storage,
        foreign_boundary_mode_independent);
  }
  else
  {
    llvm::FunctionType *fn_ty = nullptr;
    if (auto *fn = llvm::dyn_cast<llvm::Function>(callee))
    {
      fn_ty = fn->getFunctionType();
    }
    if (fn_ty)
    {
      std::vector<llvm::Value *> coerced_args;
      coerced_args.reserve(fn_ty->getNumParams());
      for (unsigned i = 0; i < fn_ty->getNumParams(); ++i)
      {
        llvm::Type *param_ty = fn_ty->getParamType(i);
        llvm::Value *arg = i < args.size()
                               ? args[i]
                               : llvm::Constant::getNullValue(param_ty);
        arg = CoerceTo(&builder, arg, param_ty);
        if (!arg)
        {
          arg = llvm::Constant::getNullValue(param_ty);
        }
        coerced_args.push_back(arg);
      }
      llvm::CallInst *call_inst = builder.CreateCall(fn_ty, callee, coerced_args);
      if (auto *callee_fn = llvm::dyn_cast<llvm::Function>(callee))
      {
        call_inst->setCallingConv(callee_fn->getCallingConv());
      }
      if (!call_inst->getType()->isVoidTy())
      {
        call_result = call_inst;
      }
    }
  }

  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef call_result_type =
      active_ctx ? active_ctx->LookupValueType(call.result) : nullptr;

  if ((call_result || call_result_storage) &&
      (callee_symbol == BuiltinSymAsyncResume() ||
       is_async_resume_runtime_symbol))
  {
    auto repack_async_resume_union =
        [&](llvm::Value *raw_result,
            const analysis::TypeRef &expected_type) -> llvm::Value *
    {
      if (!raw_result || !expected_type || !active_ctx)
      {
        return raw_result;
      }

      const analysis::TypeRef stripped_target =
          ResolveAliasType(active_ctx, expected_type);
      const auto *target_union =
          stripped_target
              ? std::get_if<analysis::TypeUnion>(&stripped_target->node)
              : nullptr;
      if (!target_union || target_union->members.empty())
      {
        return raw_result;
      }

      const analysis::ScopeContext &scope = BuildScope(active_ctx);
      const auto union_layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *target_union);
      if (!union_layout.has_value() || union_layout->niche)
      {
        return raw_result;
      }

      auto same_modal_path = [](const analysis::TypePath &lhs,
                                const analysis::TypePath &rhs) -> bool
      {
        if (lhs.size() != rhs.size())
        {
          return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
          if (!analysis::IdEq(lhs[i], rhs[i]))
          {
            return false;
          }
        }
        return true;
      };
      auto same_generic_args = [](const std::vector<analysis::TypeRef> &lhs,
                                  const std::vector<analysis::TypeRef> &rhs) -> bool
      {
        if (lhs.size() != rhs.size())
        {
          return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
          if (analysis::TypeToString(lhs[i]) != analysis::TypeToString(rhs[i]))
          {
            return false;
          }
        }
        return true;
      };

      struct AsyncMember
      {
        analysis::TypeRef type;
        std::size_t index = 0;
      };
      std::optional<analysis::TypePath> async_path;
      std::vector<analysis::TypeRef> async_args;
      std::optional<AsyncMember> suspended_member;
      std::optional<AsyncMember> completed_member;
      std::optional<AsyncMember> failed_member;

      for (std::size_t member_index = 0;
           member_index < union_layout->member_list.size();
           ++member_index)
      {
        const auto &member = union_layout->member_list[member_index];
        analysis::TypeRef stripped_member = analysis::StripPerm(member);
        const auto *modal_state =
            stripped_member
                ? std::get_if<analysis::TypeModalState>(&stripped_member->node)
                : nullptr;
        if (!modal_state ||
            !analysis::IsAsyncModalPath(modal_state->path))
        {
          return raw_result;
        }

        if (!async_path.has_value())
        {
          async_path = modal_state->path;
          async_args = modal_state->generic_args;
        }
        else if (!same_modal_path(*async_path, modal_state->path) ||
                 !same_generic_args(async_args, modal_state->generic_args))
        {
          return raw_result;
        }

        if (analysis::IdEq(modal_state->state, "Suspended"))
        {
          suspended_member = AsyncMember{stripped_member, member_index};
        }
        else if (analysis::IdEq(modal_state->state, "Completed"))
        {
          completed_member = AsyncMember{stripped_member, member_index};
        }
        else if (analysis::IdEq(modal_state->state, "Failed"))
        {
          failed_member = AsyncMember{stripped_member, member_index};
        }
      }

      if (!suspended_member.has_value() &&
          !completed_member.has_value() &&
          !failed_member.has_value())
      {
        return raw_result;
      }

      llvm::Type *target_ll = ExpectedLLVMType(call.result);
      if (!target_ll)
      {
        target_ll = emitter.GetLLVMType(stripped_target);
      }
      if (!target_ll)
      {
        return raw_result;
      }

      llvm::Value *raw_disc = nullptr;
      if (raw_result->getType()->isIntegerTy())
      {
        raw_disc = raw_result;
      }
      else if (auto *raw_struct =
                   llvm::dyn_cast<llvm::StructType>(raw_result->getType());
               raw_struct && raw_struct->getNumElements() >= 1 &&
               raw_struct->getElementType(0)->isIntegerTy())
      {
        raw_disc = builder.CreateExtractValue(raw_result, {0u});
      }
      if (!raw_disc || !raw_disc->getType()->isIntegerTy())
      {
        return raw_result;
      }

      analysis::TypeRef async_runtime_type = nullptr;
      if (async_path.has_value())
      {
        async_runtime_type =
            analysis::MakeTypePath(*async_path, async_args);
      }
      const AsyncStateDiscs async_discs =
          LoweredAsyncStateDiscs(scope, async_runtime_type);
      const std::uint64_t suspended_disc = async_discs.suspended;
      const std::uint64_t completed_disc = async_discs.completed;
      const std::optional<std::uint64_t> failed_disc = async_discs.failed;

      auto *union_struct_ty = llvm::dyn_cast<llvm::StructType>(target_ll);
      if (!union_struct_ty || union_struct_ty->getNumElements() < 2)
      {
        return raw_result;
      }

      llvm::Function *current_fn =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return raw_result;
      }

      const AsyncMember *default_member = nullptr;
      if (failed_member.has_value())
      {
        default_member = &*failed_member;
      }
      else if (completed_member.has_value())
      {
        default_member = &*completed_member;
      }
      else if (suspended_member.has_value())
      {
        default_member = &*suspended_member;
      }

      if (!default_member)
      {
        return raw_result;
      }

      llvm::Type *disc_ty = union_struct_ty->getElementType(0);
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      auto member_index_value = [&](std::size_t index) -> llvm::Value *
      {
        llvm::Value *value = CoerceTo(
            &builder,
            llvm::ConstantInt::get(i64_ty, static_cast<std::uint64_t>(index)),
            disc_ty);
        if (!value)
        {
          value = llvm::Constant::getNullValue(disc_ty);
        }
        return value;
      };

      llvm::Value *mapped_index = member_index_value(default_member->index);
      auto select_state = [&](const std::optional<AsyncMember> &member_opt,
                              std::uint64_t disc_value)
      {
        if (!member_opt.has_value())
        {
          return;
        }
        llvm::Value *is_state = EmitTypedEq(
            &builder,
            raw_disc,
            llvm::ConstantInt::get(raw_disc->getType(), disc_value));
        mapped_index = builder.CreateSelect(
            AsBool(&builder, is_state),
            member_index_value(member_opt->index),
            mapped_index);
      };

      select_state(suspended_member, suspended_disc);
      select_state(completed_member, completed_disc);
      if (failed_disc.has_value())
      {
        select_state(failed_member, *failed_disc);
      }

      llvm::Value *union_value = llvm::Constant::getNullValue(union_struct_ty);
      union_value = builder.CreateInsertValue(union_value, mapped_index, {0u});

      if (union_layout->payload_size == 0)
      {
        return union_value;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *union_slot =
          entry_builder.CreateAlloca(union_struct_ty, nullptr, "async.resume.union");
      llvm::AllocaInst *raw_slot =
          entry_builder.CreateAlloca(raw_result->getType(), nullptr, "async.resume.raw");
      builder.CreateStore(union_value, union_slot);
      builder.CreateStore(raw_result, raw_slot);

      llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          &builder,
          union_struct_ty,
          union_slot,
          union_layout->payload_align);
      if (!payload_i8)
      {
        return union_value;
      }

      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
      const std::uint64_t raw_size =
          static_cast<std::uint64_t>(dl.getTypeAllocSize(raw_result->getType()));
      const std::uint64_t copy_size = std::min(raw_size, union_layout->payload_size);
      if (copy_size > 0)
      {
        builder.CreateMemCpy(
            payload_i8,
            llvm::Align(1),
            builder.CreateBitCast(raw_slot, llvm::PointerType::get(i8_ty, 0)),
            llvm::Align(1),
            llvm::ConstantInt::get(i64_ty, copy_size));
      }
      return builder.CreateLoad(union_struct_ty, union_slot);
    };

    llvm::Value *raw_async_result = call_result;
    if (!raw_async_result && call_result_storage && sig && sig->ret)
    {
      if (llvm::Type *raw_ty = emitter.GetLLVMType(sig->ret))
      {
        raw_async_result = builder.CreateLoad(raw_ty, call_result_storage);
      }
    }
    llvm::Value *repacked =
        repack_async_resume_union(raw_async_result, call_result_type);
    if (repacked && repacked != raw_async_result && call_result_storage)
    {
      llvm::Type *target_ty = ExpectedLLVMType(call.result);
      if (!target_ty && call_result_type)
      {
        target_ty = emitter.GetLLVMType(call_result_type);
      }
      if (target_ty)
      {
        llvm::Value *store_value = CoerceTo(&builder, repacked, target_ty);
        if (!store_value)
        {
          store_value = repacked;
        }
        builder.CreateStore(store_value, call_result_storage);
      }
    }
    call_result = repacked;
  }

  if (call_result && call_result_type && sig && sig->ret)
  {
    analysis::TypeRef source_ret = analysis::StripPerm(sig->ret);
    if (!source_ret)
    {
      source_ret = sig->ret;
    }
    analysis::TypeRef target_ret = analysis::StripPerm(call_result_type);
    if (!target_ret)
    {
      target_ret = call_result_type;
    }

    bool same_ret = false;
    if (source_ret && target_ret)
    {
      const auto eq = analysis::TypeEquiv(source_ret, target_ret);
      same_ret = eq.ok && eq.equiv;
    }

    if (!same_ret)
    {
      llvm::Type *target_ty = ExpectedLLVMType(call.result);
      if (!target_ty && target_ret)
      {
        target_ty = emitter.GetLLVMType(target_ret);
      }
      if (target_ty && call_result->getType() != target_ty)
      {
        llvm::Value *coerced = CoerceToTyped(
            emitter,
            &builder,
            call_result,
            target_ty,
            source_ret,
            target_ret);
        if (!coerced)
        {
          coerced = CoerceTo(&builder, call_result, target_ty);
        }
        if (coerced)
        {
          call_result = coerced;
        }
      }
    }
  }

  bool never_call = false;
  if (sig && IsNeverType(sig->ret))
  {
    never_call = true;
  }
  else if (call_result_type)
  {
    never_call = IsNeverType(call_result_type);
  }

  if (call_result_storage)
  {
    emitter.SetTempStorage(call.result, call_result_storage);
  }
  if (!call_result && !call_result_storage)
  {
    call_result = DefaultFor(call.result);
  }
  if (call_result)
  {
    emitter.SetTempValue(call.result, call_result);
  }

  if (never_call && !builder.GetInsertBlock()->getTerminator())
  {
    builder.CreateUnreachable();
  }
}

} // namespace ultraviolet::codegen::emit_detail
