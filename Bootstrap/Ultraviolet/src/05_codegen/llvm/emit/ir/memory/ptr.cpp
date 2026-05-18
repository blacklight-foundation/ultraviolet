// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/ptr.cpp
// Canonical owner for LLVM IR pointer read and write instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

void ReportCodegenFailure(LLVMEmitter &emitter)
{
  if (const LowerCtx *ctx = emitter.GetCurrentCtx())
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

analysis::TypeRef StripPermOrSelf(analysis::TypeRef type)
{
  if (!type)
  {
    return nullptr;
  }
  if (analysis::TypeRef stripped = analysis::StripPerm(type))
  {
    return stripped;
  }
  return type;
}

analysis::TypeRef ResolveAliasOrSelf(const analysis::ScopeContext &scope,
                                     analysis::TypeRef type)
{
  if (!type)
  {
    return nullptr;
  }
  if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, type))
  {
    return StripPermOrSelf(resolved);
  }
  return StripPermOrSelf(type);
}

analysis::TypeRef PointeeTypeOrNull(const analysis::ScopeContext &scope,
                                    analysis::TypeRef type)
{
  analysis::TypeRef current = ResolveAliasOrSelf(scope, type);
  if (!current)
  {
    return nullptr;
  }
  for (std::size_t depth = 0; depth < 8 && current; ++depth)
  {
    if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&current->node))
    {
      current = ResolveAliasOrSelf(scope, raw->element);
      continue;
    }
    if (const auto *ptr = std::get_if<analysis::TypePtr>(&current->node))
    {
      current = ResolveAliasOrSelf(scope, ptr->element);
      continue;
    }
    return current;
  }
  return current;
}

analysis::TypeRef DirectPointeeTypeOrNull(const analysis::ScopeContext &scope,
                                          analysis::TypeRef type)
{
  analysis::TypeRef current = ResolveAliasOrSelf(scope, type);
  if (!current)
  {
    return nullptr;
  }
  if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&current->node))
  {
    return ResolveAliasOrSelf(scope, raw->element);
  }
  if (const auto *ptr = std::get_if<analysis::TypePtr>(&current->node))
  {
    return ResolveAliasOrSelf(scope, ptr->element);
  }
  return nullptr;
}

std::optional<FieldAccessMeta> ResolveFieldMetaForValue(
    LLVMEmitter &emitter,
    const LowerCtx &ctx,
    const analysis::ScopeContext &scope,
    const IRValue &base,
    std::string_view field)
{
  auto resolve_from_type = [&](analysis::TypeRef type)
      -> std::optional<FieldAccessMeta>
  {
    if (!type)
    {
      return std::nullopt;
    }
    if (analysis::TypeRef pointee = PointeeTypeOrNull(scope, type))
    {
      if (auto meta = ResolveFieldAccessMeta(scope, pointee, field))
      {
        return meta;
      }
    }
    return ResolveFieldAccessMeta(scope, type, field);
  };

  if (analysis::TypeRef base_type = ctx.LookupValueType(base))
  {
    if (auto meta = resolve_from_type(base_type))
    {
      return meta;
    }
  }

  if (base.kind == IRValue::Kind::Local)
  {
    if (analysis::TypeRef local_type = emitter.LookupLocalType(base.name))
    {
      if (auto meta = resolve_from_type(local_type))
      {
        return meta;
      }
    }
    if (const BindingState *state = ctx.GetBindingState(base.name))
    {
      if (auto meta = resolve_from_type(state->type))
      {
        return meta;
      }
    }
  }

  const DerivedValueInfo *derived = ctx.LookupDerivedValue(base);
  for (std::size_t depth = 0; depth < 16 && derived; ++depth)
  {
    if (derived->kind == DerivedValueInfo::Kind::AddrLocal)
    {
      if (analysis::TypeRef local_type = emitter.LookupLocalType(derived->name))
      {
        if (auto meta = resolve_from_type(local_type))
        {
          return meta;
        }
      }
      if (const BindingState *state = ctx.GetBindingState(derived->name))
      {
        if (auto meta = resolve_from_type(state->type))
        {
          return meta;
        }
      }
      break;
    }
    if (derived->kind == DerivedValueInfo::Kind::AddrField ||
        derived->kind == DerivedValueInfo::Kind::AddrTuple ||
        derived->kind == DerivedValueInfo::Kind::AddrIndex ||
        derived->kind == DerivedValueInfo::Kind::AddrDeref)
    {
      derived = ctx.LookupDerivedValue(derived->base);
      continue;
    }
    break;
  }

  return std::nullopt;
}

llvm::Value *ResolveAddrFieldPointer(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> &builder,
                                     const IRValue &ptr,
                                     const DerivedValueInfo &derived,
                                     std::optional<FieldAccessMeta> &out_meta)
{
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return nullptr;
  }
  const analysis::ScopeContext &scope = BuildScope(ctx);
  out_meta = ResolveFieldMetaForValue(
      emitter, *ctx, scope, derived.base, derived.field);
  if (!out_meta.has_value())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Value *base = emitter.GetAddressableStorage(derived.base);
  if (!base)
  {
    llvm::Value *base_value = emitter.EvaluateIRValue(derived.base);
    if (base_value && base_value->getType()->isPointerTy())
    {
      base = base_value;
    }
  }
  if (!base)
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  const FieldAccessMeta &meta = *out_meta;
  if (meta.index >= meta.aggregate_fields.size())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  auto layout = ComputeLayoutLLVMRecord(
      emitter,
      scope,
      meta.aggregate_type,
      meta.aggregate_fields,
      meta.layout_options);
  if (!layout.has_value() || meta.index >= layout->fields.size())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }
  llvm::Value *base_i8 = builder.CreateBitCast(
      base,
      llvm::PointerType::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0));
  llvm::Value *field_ptr = builder.CreateGEP(
      llvm::Type::getInt8Ty(emitter.GetContext()),
      base_i8,
      llvm::ConstantInt::get(
          llvm::Type::getInt64Ty(emitter.GetContext()),
          layout->fields[meta.index].offset));

  llvm::Type *field_storage_type = layout->fields[meta.index].llvm_type;
  if (layout->fields[meta.index].recursive_indirect)
  {
    llvm::Type *slot_ty = field_storage_type ? field_storage_type
                                             : emitter.GetOpaquePtr();
    llvm::Value *slot_ptr =
        builder.CreateBitCast(field_ptr, llvm::PointerType::get(slot_ty, 0));
    llvm::Value *target_ptr = builder.CreateLoad(slot_ty, slot_ptr);
    if (meta.field_type)
    {
      if (llvm::Type *field_ty = emitter.GetLLVMType(meta.field_type))
      {
        target_ptr = builder.CreateBitCast(
            target_ptr, llvm::PointerType::get(field_ty, 0));
      }
    }
    return target_ptr;
  }

  analysis::TypeRef ptr_pointee = nullptr;
  if (analysis::TypeRef ptr_type = ctx->LookupValueType(ptr))
  {
    ptr_pointee = DirectPointeeTypeOrNull(scope, ptr_type);
  }
  llvm::Type *target_ty = nullptr;
  if (ptr_pointee)
  {
    target_ty = emitter.GetLLVMType(ptr_pointee);
  }
  if (!target_ty && meta.field_type)
  {
    target_ty = emitter.GetLLVMType(meta.field_type);
  }
  if (!target_ty)
  {
    target_ty = field_storage_type;
  }
  if (!target_ty || target_ty->isVoidTy())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }
  return builder.CreateBitCast(field_ptr, llvm::PointerType::get(target_ty, 0));
}

llvm::Value *ResolveAddrTuplePointer(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> &builder,
                                     const IRValue &ptr,
                                     const DerivedValueInfo &derived,
                                     analysis::TypeRef &out_type)
{
  out_type = nullptr;
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return nullptr;
  }

  const analysis::ScopeContext &scope = BuildScope(ctx);
  if (analysis::TypeRef ptr_type = ctx->LookupValueType(ptr))
  {
    out_type = DirectPointeeTypeOrNull(scope, ptr_type);
  }

  analysis::TypeRef base_type = nullptr;
  if (analysis::TypeRef base_value_type = ctx->LookupValueType(derived.base))
  {
    base_type = DirectPointeeTypeOrNull(scope, base_value_type);
    if (!base_type)
    {
      base_type = ResolveAliasOrSelf(scope, base_value_type);
    }
  }

  std::optional<std::uint64_t> field_offset = derived.byte_offset;
  const auto *tuple =
      base_type ? std::get_if<analysis::TypeTuple>(&base_type->node) : nullptr;
  if (tuple && derived.tuple_index < tuple->elements.size())
  {
    if (!out_type)
    {
      out_type = ResolveAliasOrSelf(scope, tuple->elements[derived.tuple_index]);
    }
    if (!field_offset.has_value())
    {
      if (const auto layout =
              ::ultraviolet::analysis::layout::RecordLayoutOf(scope, tuple->elements))
      {
        if (derived.tuple_index < layout->offsets.size())
        {
          field_offset = layout->offsets[derived.tuple_index];
        }
      }
    }
  }

  if (!out_type || !field_offset.has_value())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Value *base = emitter.EvaluateIRValue(derived.base);
  if (!base || !base->getType()->isPointerTy())
  {
    base = emitter.GetAddressableStorage(derived.base);
  }
  if (!base || !base->getType()->isPointerTy())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Type *target_ty = emitter.GetLLVMType(out_type);
  if (!target_ty || target_ty->isVoidTy())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Value *base_i8 = builder.CreateBitCast(
      base,
      llvm::PointerType::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0));
  llvm::Value *field_ptr = builder.CreateGEP(
      llvm::Type::getInt8Ty(emitter.GetContext()),
      base_i8,
      llvm::ConstantInt::get(
          llvm::Type::getInt64Ty(emitter.GetContext()),
          *field_offset));
  return builder.CreateBitCast(field_ptr, llvm::PointerType::get(target_ty, 0));
}

llvm::Value *PointerFromValue(LLVMEmitter &emitter,
                              llvm::IRBuilder<> &builder,
                              llvm::Value *value,
                              llvm::Type *pointee_type = nullptr)
{
  if (!value)
  {
    return nullptr;
  }

  llvm::Type *target_ptr_ty =
      pointee_type ? llvm::PointerType::get(pointee_type, 0)
                   : emitter.GetOpaquePtr();
  if (value->getType()->isPointerTy())
  {
    if (pointee_type && value->getType() != target_ptr_ty)
    {
      return builder.CreateBitCast(value, target_ptr_ty);
    }
    return value;
  }
  if (value->getType()->isIntegerTy())
  {
    return builder.CreateIntToPtr(value, target_ptr_ty);
  }
  return nullptr;
}

llvm::Value *ResolveAddrIndexPointer(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> &builder,
                                     const IRValue &ptr,
                                     const DerivedValueInfo &derived,
                                     analysis::TypeRef &out_type)
{
  out_type = nullptr;
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return nullptr;
  }

  const analysis::ScopeContext &scope = BuildScope(ctx);
  if (analysis::TypeRef ptr_type = ctx->LookupValueType(ptr))
  {
    out_type = DirectPointeeTypeOrNull(scope, ptr_type);
  }

  analysis::TypeRef base_type = nullptr;
  if (analysis::TypeRef base_value_type = ctx->LookupValueType(derived.base))
  {
    base_type = DirectPointeeTypeOrNull(scope, base_value_type);
    if (!base_type)
    {
      base_type = ResolveAliasOrSelf(scope, base_value_type);
    }
  }
  base_type = ResolveAliasOrSelf(scope, base_type);

  analysis::TypeRef elem_type = nullptr;
  const auto *array_type =
      base_type ? std::get_if<analysis::TypeArray>(&base_type->node) : nullptr;
  const auto *slice_type =
      base_type ? std::get_if<analysis::TypeSlice>(&base_type->node) : nullptr;
  if (array_type)
  {
    elem_type = ResolveAliasOrSelf(scope, array_type->element);
  }
  else if (slice_type)
  {
    elem_type = ResolveAliasOrSelf(scope, slice_type->element);
  }
  if (!out_type)
  {
    out_type = elem_type;
  }
  out_type = ResolveAliasOrSelf(scope, out_type);
  if (!elem_type)
  {
    elem_type = out_type;
  }
  if (!elem_type)
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Type *elem_ll = emitter.GetLLVMType(elem_type);
  llvm::Type *target_ll = emitter.GetLLVMType(out_type ? out_type : elem_type);
  if (!elem_ll || elem_ll->isVoidTy() || !target_ll || target_ll->isVoidTy())
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Value *index = emitter.EvaluateIRValue(derived.index);
  if ((!index || !index->getType()->isIntegerTy()) &&
      derived.range.lo.has_value())
  {
    index = emitter.EvaluateIRValue(*derived.range.lo);
  }
  if (!index || !index->getType()->isIntegerTy())
  {
    index = llvm::ConstantInt::get(i64_ty, 0);
  }
  if (index->getType()->getIntegerBitWidth() != 64)
  {
    index = builder.CreateIntCast(index, i64_ty, false);
  }

  llvm::Value *base_storage = emitter.GetAddressableStorage(derived.base);
  llvm::Value *base_value = nullptr;
  llvm::Value *base_ptr = base_storage;
  if (!base_ptr)
  {
    base_value = emitter.EvaluateIRValue(derived.base);
    base_ptr = PointerFromValue(emitter, builder, base_value);
  }
  if (!base_ptr)
  {
    ReportCodegenFailure(emitter);
    return nullptr;
  }

  llvm::Value *elem_ptr = nullptr;
  if (array_type)
  {
    if (llvm::Type *array_ll = emitter.GetLLVMType(base_type))
    {
      llvm::Value *array_ptr =
          PointerFromValue(emitter, builder, base_ptr, array_ll);
      if (array_ptr)
      {
        llvm::Value *zero = llvm::ConstantInt::get(i64_ty, 0);
        elem_ptr = builder.CreateGEP(array_ll, array_ptr, {zero, index});
      }
    }
  }
  else if (slice_type)
  {
    if (!base_value)
    {
      base_value = emitter.EvaluateIRValue(derived.base);
    }

    llvm::Value *data_ptr = nullptr;
    if (base_value && base_value->getType()->isStructTy())
    {
      data_ptr = builder.CreateExtractValue(base_value, {0u});
    }
    else if (llvm::Type *slice_ll = emitter.GetLLVMType(base_type))
    {
      if (base_ptr && base_ptr->getType()->isPointerTy())
      {
        llvm::Value *typed_slice_ptr =
            builder.CreateBitCast(base_ptr, llvm::PointerType::get(slice_ll, 0));
        llvm::Value *loaded_slice = builder.CreateLoad(slice_ll, typed_slice_ptr);
        if (loaded_slice && loaded_slice->getType()->isStructTy())
        {
          data_ptr = builder.CreateExtractValue(loaded_slice, {0u});
        }
      }
    }

    llvm::Value *elem_base_ptr =
        PointerFromValue(emitter, builder, data_ptr, elem_ll);
    if (elem_base_ptr)
    {
      elem_ptr = builder.CreateGEP(elem_ll, elem_base_ptr, index);
    }
  }

  if (!elem_ptr)
  {
    llvm::Value *elem_base_ptr =
        PointerFromValue(emitter, builder, base_ptr, elem_ll);
    if (!elem_base_ptr)
    {
      ReportCodegenFailure(emitter);
      return nullptr;
    }
    elem_ptr = builder.CreateGEP(elem_ll, elem_base_ptr, index);
  }

  llvm::Type *target_ptr_ty = llvm::PointerType::get(target_ll, 0);
  if (elem_ptr->getType() != target_ptr_ty)
  {
    elem_ptr = builder.CreateBitCast(elem_ptr, target_ptr_ty);
  }
  return elem_ptr;
}

bool EmitStoreToPointer(LLVMEmitter &emitter,
                        llvm::IRBuilder<> &builder,
                        llvm::Value *ptr,
                        const IRValue &value_ref,
                        analysis::TypeRef target_type)
{
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  analysis::TypeRef source_type =
      ctx ? ctx->LookupValueType(value_ref) : nullptr;
  if (!source_type && value_ref.kind == IRValue::Kind::Local)
  {
    source_type = emitter.LookupLocalType(value_ref.name);
  }

  if (llvm::Value *source_storage = emitter.GetAddressableStorage(value_ref))
  {
    analysis::TypeRef copy_source_type = source_type ? source_type : target_type;
    if (TryEmitBitcopyAggregateStorageCopy(
            emitter, &builder, ptr, source_storage, target_type, copy_source_type))
    {
      emitter.ReleaseTempStorage(value_ref);
      return true;
    }
  }

  if (target_type &&
      TryEmitDerivedAggregateToStorage(emitter, &builder, ptr, value_ref, target_type))
  {
    emitter.ReleaseTempStorage(value_ref);
    return true;
  }

  llvm::Value *value = nullptr;
  if (target_type)
  {
    llvm::Type *target_ty = emitter.GetLLVMType(target_type);
    if (target_ty && !target_ty->isVoidTy())
    {
      value = emitter.EvaluateIRValue(value_ref);
      if (value)
      {
        llvm::Value *coerced = CoerceToTyped(
            emitter, &builder, value, target_ty, source_type, target_type);
        if (!coerced)
        {
          coerced = CoerceTo(&builder, value, target_ty);
        }
        if (!coerced && value->getType() == target_ty)
        {
          coerced = value;
        }
        value = coerced;
      }
      if (!value)
      {
        if (target_ty->isPointerTy())
        {
          ReportCodegenFailure(emitter);
          return false;
        }
        value = llvm::Constant::getNullValue(target_ty);
      }
      llvm::Value *typed_ptr = ptr;
      llvm::Type *target_ptr_ty = llvm::PointerType::get(target_ty, 0);
      if (typed_ptr->getType()->isIntegerTy())
      {
        typed_ptr = builder.CreateIntToPtr(typed_ptr, target_ptr_ty);
      }
      else if (typed_ptr->getType() != target_ptr_ty)
      {
        typed_ptr = CoerceTo(&builder, typed_ptr, target_ptr_ty);
        if (!typed_ptr && ptr->getType()->isPointerTy())
        {
          typed_ptr = builder.CreateBitCast(ptr, target_ptr_ty);
        }
      }
      if (!typed_ptr || !typed_ptr->getType()->isPointerTy())
      {
        ReportCodegenFailure(emitter);
        return false;
      }
      builder.CreateStore(value, typed_ptr);
      emitter.ReleaseTempStorage(value_ref);
      return true;
    }
  }

  value = emitter.EvaluateIRValue(value_ref);
  if (!value)
  {
    return false;
  }
  llvm::Type *value_ty = value->getType();
  llvm::PointerType *typed_ptr_ty = llvm::PointerType::get(value_ty, 0);
  if (ptr->getType()->isIntegerTy())
  {
    ptr = builder.CreateIntToPtr(ptr, typed_ptr_ty);
  }
  else if (!ptr->getType()->isPointerTy())
  {
    ReportCodegenFailure(emitter);
    return false;
  }

  llvm::Value *typed_ptr = ptr;
  if (typed_ptr->getType() != typed_ptr_ty)
  {
    auto *src_ptr_ty = llvm::dyn_cast<llvm::PointerType>(typed_ptr->getType());
    if (!src_ptr_ty)
    {
      ReportCodegenFailure(emitter);
      return false;
    }
    if (src_ptr_ty->getAddressSpace() == typed_ptr_ty->getAddressSpace())
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, typed_ptr_ty);
    }
    else
    {
      typed_ptr = CoerceTo(&builder, typed_ptr, typed_ptr_ty);
      if (!typed_ptr)
      {
        ReportCodegenFailure(emitter);
        return false;
      }
    }
  }

  builder.CreateStore(value, typed_ptr);
  emitter.ReleaseTempStorage(value_ref);
  return true;
}

} // namespace

void IRInstructionVisitor::operator()(const IRReadPtr &read) const
{
  llvm::Type *result_ty = ExpectedLLVMType(read.result);
  if (!result_ty)
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }
  llvm::Value *ptr = EvaluateOrDefault(read.ptr);
  if (!ptr)
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }

  llvm::PointerType *typed_ptr_ty = llvm::PointerType::get(result_ty, 0);
  if (ptr->getType()->isIntegerTy())
  {
    ptr = builder.CreateIntToPtr(ptr, typed_ptr_ty);
  }
  else if (!ptr->getType()->isPointerTy())
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }
  llvm::Value *typed_ptr = ptr;
  if (typed_ptr->getType() != typed_ptr_ty)
  {
    auto *src_ptr_ty = llvm::dyn_cast<llvm::PointerType>(typed_ptr->getType());
    if (!src_ptr_ty)
    {
      emitter.SetTempValue(read.result, DefaultFor(read.result));
      return;
    }
    if (src_ptr_ty->getAddressSpace() == typed_ptr_ty->getAddressSpace())
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, typed_ptr_ty);
    }
    else
    {
      typed_ptr = CoerceTo(&builder, typed_ptr, typed_ptr_ty);
      if (!typed_ptr)
      {
        emitter.SetTempValue(read.result, DefaultFor(read.result));
        return;
      }
    }
  }

  llvm::Value *loaded = builder.CreateLoad(result_ty, typed_ptr);
  emitter.SetTempValue(read.result, loaded ? loaded : DefaultFor(read.result));
}

void IRInstructionVisitor::operator()(const IRWritePtr &write) const
{
  auto hosted_state_pointer = [&](const IRValue &value) -> bool
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    if (!active_ctx || !emitter.IsHostedLibraryBuild())
    {
      return false;
    }

    IRValue cursor = value;
    for (int depth = 0; depth < 16; ++depth)
    {
      if (cursor.kind == IRValue::Kind::Symbol)
      {
        std::string symbol = cursor.name;
        if (auto alias = emitter.LookupSymbolAlias(symbol))
        {
          symbol = *alias;
        }
        return emitter.HasHostedStateSlot(symbol);
      }
      if (cursor.kind != IRValue::Kind::Opaque)
      {
        return false;
      }

      const DerivedValueInfo *derived = active_ctx->LookupDerivedValue(cursor);
      if (!derived)
      {
        return false;
      }

      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::AddrStatic:
      {
        std::string symbol = derived->name;
        if (!derived->static_path.empty() && !derived->name.empty())
        {
          if (auto* lower_ctx = emitter.GetCurrentCtx();
              lower_ctx && lower_ctx->sigma) {
            if (auto addr =
                    StaticAddr(*lower_ctx->sigma,
                               derived->static_path,
                               derived->name)) {
              symbol = addr->name;
            } else {
              symbol = StaticSymPath(derived->static_path,
                                     derived->name);
            }
          } else {
            symbol = StaticSymPath(derived->static_path,
                                   derived->name);
          }
        }
        if (emitter.HasHostedStateSlot(symbol))
        {
          return true;
        }
        if (!derived->name.empty())
        {
          if (auto alias = emitter.LookupSymbolAlias(derived->name))
          {
            if (emitter.HasHostedStateSlot(*alias))
            {
              return true;
            }
          }
          if (emitter.HasHostedStateSlot(derived->name))
          {
            return true;
          }
        }
        return false;
      }
      case DerivedValueInfo::Kind::AddrField:
      case DerivedValueInfo::Kind::AddrTuple:
      case DerivedValueInfo::Kind::AddrIndex:
      case DerivedValueInfo::Kind::AddrDeref:
      case DerivedValueInfo::Kind::LoadFromAddr:
        cursor = derived->base;
        continue;
      default:
        return false;
      }
    }
    return false;
  };

  analysis::TypeRef target_type = nullptr;
  if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
  {
    if (const DerivedValueInfo *derived =
            active_ctx->LookupDerivedValue(write.ptr);
        derived && derived->kind == DerivedValueInfo::Kind::AddrField)
    {
      std::optional<FieldAccessMeta> field_meta;
      llvm::Value *field_ptr =
          ResolveAddrFieldPointer(emitter, builder, write.ptr, *derived, field_meta);
      if (!field_ptr)
      {
        return;
      }
      if (field_meta.has_value())
      {
        target_type = field_meta->field_type;
      }
      (void)EmitStoreToPointer(
          emitter, builder, field_ptr, write.value, target_type);
      return;
    }
    if (const DerivedValueInfo *derived =
            active_ctx->LookupDerivedValue(write.ptr);
        derived && derived->kind == DerivedValueInfo::Kind::AddrTuple)
    {
      llvm::Value *tuple_ptr =
          ResolveAddrTuplePointer(emitter, builder, write.ptr, *derived, target_type);
      if (!tuple_ptr)
      {
        return;
      }
      (void)EmitStoreToPointer(
          emitter, builder, tuple_ptr, write.value, target_type);
      return;
    }
    if (const DerivedValueInfo *derived =
            active_ctx->LookupDerivedValue(write.ptr);
        derived && derived->kind == DerivedValueInfo::Kind::AddrIndex)
    {
      llvm::Value *index_ptr =
          ResolveAddrIndexPointer(emitter, builder, write.ptr, *derived, target_type);
      if (!index_ptr)
      {
        return;
      }
      (void)EmitStoreToPointer(
          emitter, builder, index_ptr, write.value, target_type);
      return;
    }
  }

  llvm::Value *ptr = emitter.EvaluateIRValue(write.ptr);
  if (!ptr)
  {
    if (hosted_state_pointer(write.ptr))
    {
      if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
      {
        const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
      }
      return;
    }
    ptr = DefaultFor(write.ptr);
  }
  llvm::Value *value = EvaluateOrDefault(write.value);
  if (!ptr || !value)
  {
    return;
  }

  if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
  {
    if (analysis::TypeRef ptr_type = active_ctx->LookupValueType(write.ptr))
    {
      const analysis::ScopeContext &scope = BuildScope(active_ctx);
      target_type = DirectPointeeTypeOrNull(scope, ptr_type);
    }
  }

  if (target_type)
  {
    (void)EmitStoreToPointer(
        emitter, builder, ptr, write.value, target_type);
    return;
  }

  llvm::Type *value_ty = value->getType();
  llvm::PointerType *typed_ptr_ty = llvm::PointerType::get(value_ty, 0);
  if (ptr->getType()->isIntegerTy())
  {
    ptr = builder.CreateIntToPtr(ptr, typed_ptr_ty);
  }
  else if (!ptr->getType()->isPointerTy())
  {
    return;
  }

  llvm::Value *typed_ptr = ptr;
  if (typed_ptr->getType() != typed_ptr_ty)
  {
    auto *src_ptr_ty = llvm::dyn_cast<llvm::PointerType>(typed_ptr->getType());
    if (!src_ptr_ty)
    {
      return;
    }
    if (src_ptr_ty->getAddressSpace() == typed_ptr_ty->getAddressSpace())
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, typed_ptr_ty);
    }
    else
    {
      typed_ptr = CoerceTo(&builder, typed_ptr, typed_ptr_ty);
      if (!typed_ptr)
      {
        return;
      }
    }
  }

  builder.CreateStore(value, typed_ptr);
}

} // namespace ultraviolet::codegen::emit_detail
