// =============================================================================
// File: 05_codegen/llvm/emit/ir_storage_emit.cpp
// Canonical owner for LLVM IR storage, flow-state, and bind lowering.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

#include <algorithm>

namespace ultraviolet::codegen {

using namespace emit_detail;

  namespace {

  bool SameStorageObject(llvm::Value *left, llvm::Value *right)
  {
    if (!left || !right)
    {
      return false;
    }
    return left->stripPointerCasts() == right->stripPointerCasts();
  }

  bool IsStorageBackedAggregate(LLVMEmitter &emitter, llvm::Type *ty)
  {
    if (!ty || IsZeroSizedLLVMType(emitter, ty))
    {
      return false;
    }
    if (ty->isArrayTy())
    {
      return true;
    }
    auto *struct_ty = llvm::dyn_cast<llvm::StructType>(ty);
    return struct_ty && struct_ty->getNumElements() != 0;
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

  bool IsPointerValueType(analysis::TypeRef type)
  {
    type = StripPermOrSelf(type);
    if (!type)
    {
      return false;
    }
    if (std::holds_alternative<analysis::TypePtr>(type->node) ||
        std::holds_alternative<analysis::TypeRawPtr>(type->node))
    {
      return true;
    }
    if (const auto *path = std::get_if<analysis::TypePathType>(&type->node))
    {
      if (!path->path.empty())
      {
        const std::string &tail = path->path.back();
        return tail == "Ptr" || tail == "RawPtr" || tail == "GpuPtr";
      }
    }
    return false;
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

  analysis::TypeRef PointeeTypeOrSelf(const analysis::ScopeContext &scope,
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

  analysis::TypeRef LookupStorageType(LLVMEmitter &emitter,
                                      const LowerCtx &ctx,
                                      const IRValue &value)
  {
    if (analysis::TypeRef type = ctx.LookupValueType(value))
    {
      return type;
    }
    if (value.kind == IRValue::Kind::Local)
    {
      if (analysis::TypeRef type = emitter.LookupLocalType(value.name))
      {
        return type;
      }
      if (const BindingState *state = ctx.GetBindingState(value.name))
      {
        return state->type;
      }
    }
    return nullptr;
  }

  llvm::Value *PointerValueOrNull(LLVMEmitter &emitter,
                                  llvm::IRBuilder<> *builder,
                                  llvm::Value *value)
  {
    if (!value)
    {
      return nullptr;
    }
    if (value->getType()->isPointerTy())
    {
      return value;
    }
    if (value->getType()->isIntegerTy() && builder)
    {
      return builder->CreateIntToPtr(value, emitter.GetOpaquePtr());
    }
    return nullptr;
  }

  llvm::Value *StorageBaseForAggregateAccess(LLVMEmitter &emitter,
                                             llvm::IRBuilder<> *builder,
                                             const LowerCtx &ctx,
                                             const analysis::ScopeContext &scope,
                                             const IRValue &base_value,
                                             llvm::Value **evaluated_value = nullptr)
  {
    auto evaluate_base = [&]() -> llvm::Value *
    {
      llvm::Value *value = emitter.EvaluateIRValue(base_value);
      if (evaluated_value)
      {
        *evaluated_value = value;
      }
      return value;
    };

    if (DirectPointeeTypeOrNull(
            scope,
            LookupStorageType(emitter, ctx, base_value)))
    {
      return PointerValueOrNull(emitter, builder, evaluate_base());
    }

    llvm::Value *base = emitter.GetAddressableStorage(base_value);
    if (!base)
    {
      base = PointerValueOrNull(emitter, builder, evaluate_base());
    }
    return base;
  }

  std::optional<FieldAccessMeta> ResolveFieldMetaForStorage(
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
      if (analysis::TypeRef pointee = PointeeTypeOrSelf(scope, type))
      {
        if (auto meta = ResolveFieldAccessMeta(scope, pointee, field))
        {
          return meta;
        }
      }
      return ResolveFieldAccessMeta(scope, type, field);
    };

    if (analysis::TypeRef base_type = LookupStorageType(emitter, ctx, base))
    {
      if (auto meta = resolve_from_type(base_type))
      {
        return meta;
      }
    }

    const DerivedValueInfo *derived = ctx.LookupDerivedValue(base);
    for (std::size_t depth = 0; depth < 16 && derived; ++depth)
    {
      if (derived->kind == DerivedValueInfo::Kind::AddrLocal)
      {
        IRValue local;
        local.kind = IRValue::Kind::Local;
        local.name = derived->name;
        if (analysis::TypeRef local_type =
                LookupStorageType(emitter, ctx, local))
        {
          return resolve_from_type(local_type);
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

  llvm::Value *StorageForDerivedField(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> *builder,
                                      const LowerCtx &ctx,
                                      const analysis::ScopeContext &scope,
                                      const IRValue &value,
                                      const DerivedValueInfo &derived)
  {
    if (!builder)
    {
      return nullptr;
    }
    auto meta = ResolveFieldMetaForStorage(
        emitter, ctx, scope, derived.base, derived.field);
    if (!meta.has_value() || meta->index >= meta->aggregate_fields.size())
    {
      return nullptr;
    }

    llvm::Value *base = StorageBaseForAggregateAccess(
        emitter, builder, ctx, scope, derived.base);
    if (!base || !base->getType()->isPointerTy())
    {
      return nullptr;
    }

    const auto layout = ComputeLayoutLLVMRecord(
        emitter,
        scope,
        meta->aggregate_type,
        meta->aggregate_fields,
        meta->layout_options);
    if (!layout.has_value() || meta->index >= layout->fields.size())
    {
      return nullptr;
    }

    const LayoutLLVMField &field = layout->fields[meta->index];
    llvm::Value *base_i8 = builder->CreateBitCast(
        base,
        llvm::PointerType::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0));
    llvm::Value *field_i8 = builder->CreateGEP(
        llvm::Type::getInt8Ty(emitter.GetContext()),
        base_i8,
        llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(emitter.GetContext()),
            field.offset));

    analysis::TypeRef field_type = LookupStorageType(emitter, ctx, value);
    if (!field_type)
    {
      field_type = meta->field_type;
    }
    llvm::Type *field_ty = field_type ? emitter.GetLLVMType(field_type)
                                      : field.llvm_type;
    if (field.recursive_indirect)
    {
      llvm::Type *slot_ty = field.llvm_type ? field.llvm_type
                                            : emitter.GetOpaquePtr();
      llvm::Value *slot_ptr =
          builder->CreateBitCast(field_i8, llvm::PointerType::get(slot_ty, 0));
      llvm::Value *target_ptr = builder->CreateLoad(slot_ty, slot_ptr);
      if (field_ty && !field_ty->isVoidTy())
      {
        target_ptr = builder->CreateBitCast(
            target_ptr,
            llvm::PointerType::get(field_ty, 0));
      }
      return target_ptr;
    }
    if (!field_ty || field_ty->isVoidTy())
    {
      field_ty = field.llvm_type;
    }
    if (!field_ty || field_ty->isVoidTy())
    {
      return nullptr;
    }
    return builder->CreateBitCast(
        field_i8,
        llvm::PointerType::get(field_ty, 0));
  }

  llvm::Value *StorageForDerivedTuple(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> *builder,
                                      const LowerCtx &ctx,
                                      const analysis::ScopeContext &scope,
                                      const IRValue &value,
                                      const DerivedValueInfo &derived)
  {
    if (!builder)
    {
      return nullptr;
    }
    llvm::Value *base = StorageBaseForAggregateAccess(
        emitter, builder, ctx, scope, derived.base);
    if (!base || !base->getType()->isPointerTy())
    {
      return nullptr;
    }

    analysis::TypeRef base_type =
        PointeeTypeOrSelf(scope, LookupStorageType(emitter, ctx, derived.base));
    const auto *tuple =
        base_type ? std::get_if<analysis::TypeTuple>(&base_type->node) : nullptr;
    if (!tuple || derived.tuple_index >= tuple->elements.size())
    {
      return nullptr;
    }

    std::optional<std::uint64_t> field_offset = derived.byte_offset;
    if (!field_offset.has_value())
    {
      if (const auto layout =
              ::ultraviolet::analysis::layout::TupleLayoutOf(scope,
                                                             tuple->elements))
      {
        if (derived.tuple_index < layout->offsets.size())
        {
          field_offset = layout->offsets[derived.tuple_index];
        }
      }
    }
    if (!field_offset.has_value())
    {
      return nullptr;
    }

    analysis::TypeRef elem_type = LookupStorageType(emitter, ctx, value);
    if (!elem_type)
    {
      elem_type = tuple->elements[derived.tuple_index];
    }
    llvm::Type *elem_ty = elem_type ? emitter.GetLLVMType(elem_type) : nullptr;
    if (!elem_ty || elem_ty->isVoidTy())
    {
      return nullptr;
    }

    llvm::Value *base_i8 = builder->CreateBitCast(
        base,
        llvm::PointerType::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0));
    llvm::Value *field_i8 = builder->CreateGEP(
        llvm::Type::getInt8Ty(emitter.GetContext()),
        base_i8,
        llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(emitter.GetContext()),
            *field_offset));
    return builder->CreateBitCast(field_i8, llvm::PointerType::get(elem_ty, 0));
  }

  llvm::Value *StorageForDerivedIndex(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> *builder,
                                      const LowerCtx &ctx,
                                      const analysis::ScopeContext &scope,
                                      const IRValue &value,
                                      const DerivedValueInfo &derived)
  {
    if (!builder)
    {
      return nullptr;
    }
    llvm::Value *base_value = nullptr;
    llvm::Value *base_ptr = StorageBaseForAggregateAccess(
        emitter, builder, ctx, scope, derived.base, &base_value);
    if (!base_ptr || !base_ptr->getType()->isPointerTy())
    {
      return nullptr;
    }

    analysis::TypeRef base_type =
        PointeeTypeOrSelf(scope, LookupStorageType(emitter, ctx, derived.base));
    analysis::TypeRef elem_type = LookupStorageType(emitter, ctx, value);
    const auto *array_type =
        base_type ? std::get_if<analysis::TypeArray>(&base_type->node) : nullptr;
    const auto *slice_type =
        base_type ? std::get_if<analysis::TypeSlice>(&base_type->node) : nullptr;
    if (!elem_type)
    {
      if (array_type)
      {
        elem_type = array_type->element;
      }
      else if (slice_type)
      {
        elem_type = slice_type->element;
      }
    }
    llvm::Type *elem_ty = elem_type ? emitter.GetLLVMType(elem_type) : nullptr;
    if (!elem_ty || elem_ty->isVoidTy())
    {
      return nullptr;
    }

    llvm::Value *index = emitter.EvaluateIRValue(derived.index);
    if (!index && derived.range.lo.has_value())
    {
      index = emitter.EvaluateIRValue(*derived.range.lo);
    }
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    if (!index || !index->getType()->isIntegerTy())
    {
      index = llvm::ConstantInt::get(i64_ty, 0);
    }
    if (index->getType()->getIntegerBitWidth() != 64)
    {
      index = builder->CreateIntCast(index, i64_ty, false);
    }

    if (array_type)
    {
      llvm::Type *array_ll = emitter.GetLLVMType(base_type);
      auto *array_ll_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(array_ll);
      if (!array_ll_ty)
      {
        return nullptr;
      }
      llvm::Value *typed_array = builder->CreateBitCast(
          base_ptr,
          llvm::PointerType::get(array_ll_ty, 0));
      llvm::Value *zero = llvm::ConstantInt::get(i64_ty, 0);
      return builder->CreateGEP(array_ll_ty, typed_array, {zero, index});
    }

    if (slice_type)
    {
      llvm::Value *data_ptr = nullptr;
      if (base_value && base_value->getType()->isStructTy())
      {
        data_ptr = builder->CreateExtractValue(base_value, {0u});
      }
      else
      {
        data_ptr = EmitSequenceDataPtrFromAddr(
            emitter, *builder, base_type, base_ptr);
      }
      llvm::Value *elem_base = PointerValueOrNull(emitter, builder, data_ptr);
      if (!elem_base)
      {
        return nullptr;
      }
      llvm::Value *typed_data = builder->CreateBitCast(
          elem_base,
          llvm::PointerType::get(elem_ty, 0));
      return builder->CreateGEP(elem_ty, typed_data, index);
    }

    return nullptr;
  }

  const ast::EnumDecl *LookupEnumDeclForStorage(
      const analysis::ScopeContext &scope,
      analysis::TypeRef type,
      const analysis::TypePath &static_path,
      analysis::TypePath *out_path)
  {
    type = ResolveAliasOrSelf(scope, type);
    const analysis::TypePath *path =
        type ? analysis::AppliedTypePath(*type) : nullptr;
    if (!path && !static_path.empty())
    {
      path = &static_path;
    }
    if (!path)
    {
      return nullptr;
    }
    if (out_path)
    {
      *out_path = *path;
    }
    if (const ast::EnumDecl *decl = analysis::LookupEnumDecl(scope, *path))
    {
      return decl;
    }
    if (!scope.current_module.empty() && path->size() == 1u)
    {
      analysis::TypePath qualified = scope.current_module;
      qualified.insert(qualified.end(), path->begin(), path->end());
      if (out_path)
      {
        *out_path = qualified;
      }
      return analysis::LookupEnumDecl(scope, qualified);
    }
    return nullptr;
  }

  const ast::VariantDecl *FindEnumVariantForStorage(
      const ast::EnumDecl &decl,
      std::string_view name)
  {
    for (const ast::VariantDecl &variant : decl.variants)
    {
      if (analysis::IdEq(variant.name, std::string(name)))
      {
        return &variant;
      }
    }
    return nullptr;
  }

  std::vector<analysis::TypeRef> GenericArgsForStorageType(
      const analysis::ScopeContext &scope,
      analysis::TypeRef type)
  {
    type = ResolveAliasOrSelf(scope, type);
    const auto *args = type ? analysis::AppliedTypeArgs(*type) : nullptr;
    return args ? *args : std::vector<analysis::TypeRef>{};
  }

  llvm::Value *StorageMemberAddress(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *member_i8,
                                    llvm::Type *storage_ty,
                                    llvm::Type *value_ty,
                                    bool recursive_indirect)
  {
    if (!builder || !member_i8 || !storage_ty || !value_ty ||
        storage_ty->isVoidTy() || value_ty->isVoidTy())
    {
      return nullptr;
    }
    llvm::Value *slot_ptr = builder->CreateBitCast(
        member_i8,
        llvm::PointerType::get(storage_ty, 0));
    if (recursive_indirect)
    {
      llvm::Value *target_ptr = builder->CreateLoad(storage_ty, slot_ptr);
      if (!target_ptr || !target_ptr->getType()->isPointerTy())
      {
        return nullptr;
      }
      return builder->CreateBitCast(
          target_ptr,
          llvm::PointerType::get(value_ty, 0));
    }
    if (storage_ty == value_ty)
    {
      return slot_ptr;
    }
    return builder->CreateBitCast(
        member_i8,
        llvm::PointerType::get(value_ty, 0));
  }

  llvm::Value *TaggedPayloadMemberAddress(LLVMEmitter &emitter,
                                          llvm::IRBuilder<> *builder,
                                          llvm::StructType *tagged_ty,
                                          llvm::Value *base_storage,
                                          std::uint64_t payload_align,
                                          std::uint64_t member_offset,
                                          llvm::Type *storage_ty,
                                          llvm::Type *value_ty,
                                          bool recursive_indirect)
  {
    if (!builder || !tagged_ty || !base_storage)
    {
      return nullptr;
    }
    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        builder,
        tagged_ty,
        base_storage,
        payload_align);
    if (!payload_i8)
    {
      return nullptr;
    }
    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *field_i8 = builder->CreateGEP(
        i8_ty,
        payload_i8,
        llvm::ConstantInt::get(i64_ty, member_offset));
    return StorageMemberAddress(
        emitter,
        builder,
        field_i8,
        storage_ty,
        value_ty,
        recursive_indirect);
  }

  llvm::Value *StorageForDerivedEnumPayload(
      LLVMEmitter &emitter,
      llvm::IRBuilder<> *builder,
      const LowerCtx &ctx,
      const analysis::ScopeContext &scope,
      const IRValue &value,
      const DerivedValueInfo &derived)
  {
    if (!builder)
    {
      return nullptr;
    }
    analysis::TypeRef enum_type =
        ResolveAliasOrSelf(scope, LookupStorageType(emitter, ctx, derived.base));
    analysis::TypePath enum_path;
    const ast::EnumDecl *enum_decl =
        LookupEnumDeclForStorage(scope, enum_type, derived.static_path, &enum_path);
    if (!enum_decl)
    {
      return nullptr;
    }
    const ast::VariantDecl *variant =
        FindEnumVariantForStorage(*enum_decl, derived.variant);
    if (!variant)
    {
      return nullptr;
    }
    const std::vector<analysis::TypeRef> enum_args =
        GenericArgsForStorageType(scope, enum_type);

    std::optional<analysis::layout::EnumPayloadMemberLayout> member;
    if (derived.kind == DerivedValueInfo::Kind::EnumPayloadIndex)
    {
      member = analysis::layout::EnumTuplePayloadMemberLayout(
          scope,
          *enum_decl,
          *variant,
          enum_args,
          derived.tuple_index);
    }
    else
    {
      member = analysis::layout::EnumRecordPayloadMemberLayout(
          scope,
          *enum_decl,
          *variant,
          enum_args,
          derived.field);
    }
    if (!member.has_value())
    {
      return nullptr;
    }

    llvm::Type *enum_ty = enum_type ? emitter.GetLLVMType(enum_type) : nullptr;
    auto *enum_struct = llvm::dyn_cast_or_null<llvm::StructType>(enum_ty);
    llvm::Type *member_ty = emitter.GetLLVMType(member->type);
    if (!enum_struct || !member_ty)
    {
      return nullptr;
    }

    llvm::Value *base_storage = emitter.GetAddressableStorage(derived.base);
    if (!base_storage)
    {
      llvm::Value *base_value = emitter.EvaluateIRValue(derived.base);
      if (base_value)
      {
        if (base_value->getType()->isPointerTy())
        {
          base_storage = builder->CreateBitCast(
              base_value,
              llvm::PointerType::get(enum_struct, 0));
        }
        else
        {
          llvm::Value *stored_base = base_value;
          if (stored_base->getType() != enum_struct)
          {
            stored_base = CoerceTo(builder, stored_base, enum_struct);
          }
          if (stored_base)
          {
            llvm::Function *current_fn =
                builder->GetInsertBlock()
                    ? builder->GetInsertBlock()->getParent()
                    : nullptr;
            if (current_fn)
            {
              llvm::IRBuilder<> entry_builder(
                  &current_fn->getEntryBlock(),
                  current_fn->getEntryBlock().begin());
              llvm::AllocaInst *base_slot =
                  entry_builder.CreateAlloca(enum_struct);
              builder->CreateStore(stored_base, base_slot);
              base_storage = base_slot;
            }
          }
        }
      }
    }
    if (!base_storage)
    {
      return nullptr;
    }
    return TaggedPayloadMemberAddress(
        emitter,
        builder,
        enum_struct,
        base_storage,
        member->payload_align,
        member->offset,
        member_ty,
        member_ty,
        false);
  }

  const ast::ModalDecl *LookupModalDeclForStorage(
      const analysis::ScopeContext &scope,
      analysis::TypeRef type,
      const analysis::TypePath &static_path,
      analysis::TypePath *out_path)
  {
    type = ResolveAliasOrSelf(scope, type);
    if (const auto *state =
            type ? std::get_if<analysis::TypeModalState>(&type->node) : nullptr)
    {
      if (out_path)
      {
        *out_path = state->path;
      }
      return analysis::LookupModalDecl(scope, state->path);
    }
    const analysis::TypePath *path =
        type ? analysis::AppliedTypePath(*type) : nullptr;
    if (!path && !static_path.empty())
    {
      path = &static_path;
    }
    if (!path)
    {
      return nullptr;
    }
    if (out_path)
    {
      *out_path = *path;
    }
    return analysis::LookupModalDecl(scope, *path);
  }

  const ast::StateBlock *FindModalStateForStorage(
      const ast::ModalDecl &decl,
      std::string_view state_name)
  {
    for (const ast::StateBlock &state : decl.states)
    {
      if (analysis::IdEq(state.name, std::string(state_name)))
      {
        return &state;
      }
    }
    return nullptr;
  }

  llvm::Value *StorageForDerivedUnionPayload(
      LLVMEmitter &emitter,
      llvm::IRBuilder<> *builder,
      const LowerCtx &ctx,
      const analysis::ScopeContext &scope,
      const IRValue &value,
      const DerivedValueInfo &derived)
  {
    (void)value;
    if (!builder)
    {
      return nullptr;
    }
    llvm::Value *base_storage = emitter.GetAddressableStorage(derived.base);
    if (!base_storage)
    {
      return nullptr;
    }
    analysis::TypeRef union_type =
        ResolveAliasOrSelf(scope, LookupStorageType(emitter, ctx, derived.base));
    const auto *uni =
        union_type ? std::get_if<analysis::TypeUnion>(&union_type->node) : nullptr;
    if (!uni)
    {
      return nullptr;
    }
    const auto layout = analysis::layout::UnionLayoutOf(scope, *uni);
    if (!layout.has_value() ||
        derived.union_index >= layout->member_list.size())
    {
      return nullptr;
    }
    analysis::TypeRef member_type = layout->member_list[derived.union_index];
    llvm::Type *member_ty = member_type ? emitter.GetLLVMType(member_type) : nullptr;
    if (!member_ty)
    {
      return nullptr;
    }
    if (layout->niche)
    {
      return builder->CreateBitCast(
          base_storage,
          llvm::PointerType::get(member_ty, 0));
    }
    llvm::Type *union_ty = union_type ? emitter.GetLLVMType(union_type) : nullptr;
    auto *union_struct = llvm::dyn_cast_or_null<llvm::StructType>(union_ty);
    if (!union_struct)
    {
      return nullptr;
    }
    return TaggedPayloadMemberAddress(
        emitter,
        builder,
        union_struct,
        base_storage,
        layout->payload_align,
        0,
        member_ty,
        member_ty,
        false);
  }

  llvm::Value *StorageForDerivedModalField(
      LLVMEmitter &emitter,
      llvm::IRBuilder<> *builder,
      const LowerCtx &ctx,
      const analysis::ScopeContext &scope,
      const IRValue &value,
      const DerivedValueInfo &derived)
  {
    if (!builder)
    {
      return nullptr;
    }
    llvm::Value *base_storage = emitter.GetAddressableStorage(derived.base);
    if (!base_storage)
    {
      return nullptr;
    }

    analysis::TypeRef base_type =
        ResolveAliasOrSelf(scope, LookupStorageType(emitter, ctx, derived.base));
    if (!base_type)
    {
      if (const DerivedValueInfo *base_derived =
              ctx.LookupDerivedValue(derived.base))
      {
        if (base_derived->kind == DerivedValueInfo::Kind::UnionPayload)
        {
          analysis::TypeRef union_type =
              ResolveAliasOrSelf(scope,
                                 LookupStorageType(emitter, ctx, base_derived->base));
          if (const auto *uni =
                  union_type ? std::get_if<analysis::TypeUnion>(&union_type->node)
                             : nullptr)
          {
            std::vector<analysis::TypeRef> members = uni->members;
            if (const auto layout = analysis::layout::UnionLayoutOf(scope, *uni))
            {
              members = layout->member_list;
            }
            if (base_derived->union_index < members.size())
            {
              base_type = ResolveAliasOrSelf(scope, members[base_derived->union_index]);
            }
          }
        }
      }
    }

    analysis::TypePath modal_path;
    const ast::ModalDecl *modal_decl =
        LookupModalDeclForStorage(scope, base_type, derived.static_path, &modal_path);
    if (!modal_decl)
    {
      return nullptr;
    }

    const auto *modal_state_type =
        base_type ? std::get_if<analysis::TypeModalState>(&base_type->node) : nullptr;
    std::vector<analysis::TypeRef> modal_args;
    if (modal_state_type)
    {
      modal_args = modal_state_type->generic_args;
    }
    else
    {
      const auto *args = base_type ? analysis::AppliedTypeArgs(*base_type) : nullptr;
      if (args)
      {
        modal_args = *args;
      }
    }

    analysis::TypeSubst modal_subst;
    if (modal_decl->generic_params && !modal_decl->generic_params->params.empty())
    {
      if (modal_args.size() > modal_decl->generic_params->params.size())
      {
        return nullptr;
      }
      modal_subst = analysis::BuildSubstitution(
          modal_decl->generic_params->params,
          modal_args);
    }

    const ast::StateBlock *state =
        FindModalStateForStorage(*modal_decl, derived.modal_state);
    if (!state)
    {
      return nullptr;
    }

    std::vector<analysis::TypeRef> field_types;
    std::vector<std::string> field_names;
    for (const auto &member : state->members)
    {
      const auto *field = std::get_if<ast::StateFieldDecl>(&member);
      if (!field)
      {
        continue;
      }
      const auto lowered =
          analysis::layout::LowerTypeForLayout(scope, field->type);
      if (!lowered.has_value())
      {
        return nullptr;
      }
      analysis::TypeRef field_type = *lowered;
      if (!modal_subst.empty())
      {
        field_type = analysis::InstantiateType(field_type, modal_subst);
      }
      field_types.push_back(field_type);
      field_names.push_back(field->name);
    }

    analysis::TypeRef modal_state_ref = analysis::MakeTypeModalState(
        modal_path,
        derived.modal_state,
        modal_args);
    const auto record_layout =
        ComputeLayoutLLVMRecord(emitter, scope, modal_state_ref, field_types);
    if (!record_layout.has_value())
    {
      return nullptr;
    }

    std::optional<std::size_t> field_index;
    for (std::size_t i = 0; i < field_names.size(); ++i)
    {
      if (analysis::IdEq(field_names[i], derived.field))
      {
        field_index = i;
        break;
      }
    }
    if (!field_index.has_value() ||
        *field_index >= record_layout->fields.size())
    {
      return nullptr;
    }

    const auto modal_layout =
        analysis::layout::ModalLayoutOf(scope, *modal_decl, modal_args);
    if (!modal_layout.has_value())
    {
      return nullptr;
    }
    const LayoutLLVMField &field = record_layout->fields[*field_index];
    analysis::TypeRef field_type = field_types[*field_index];
    llvm::Type *field_ty = emitter.GetLLVMType(field_type);
    if (!field_ty || field_ty->isVoidTy())
    {
      field_ty = field.llvm_type;
    }
    if (!field_ty || field_ty->isVoidTy())
    {
      return nullptr;
    }

    const bool base_is_modal_state = modal_state_type != nullptr;
    const bool base_is_async_modal_state =
        modal_state_type && analysis::IsAsyncType(base_type);
    const bool tagged =
        !(base_is_modal_state && !base_is_async_modal_state) &&
        modal_layout->disc_type.has_value();

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *payload_i8 = nullptr;
    if (tagged)
    {
      llvm::Type *modal_ty = base_type ? emitter.GetLLVMType(base_type) : nullptr;
      auto *modal_struct = llvm::dyn_cast_or_null<llvm::StructType>(modal_ty);
      if (!modal_struct)
      {
        return nullptr;
      }
      payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          builder,
          modal_struct,
          base_storage,
          modal_layout->payload_align);
    }
    else
    {
      payload_i8 = builder->CreateBitCast(
          base_storage,
          llvm::PointerType::get(i8_ty, 0));
    }
    if (!payload_i8)
    {
      return nullptr;
    }

    llvm::Value *field_i8 = builder->CreateGEP(
        i8_ty,
        payload_i8,
        llvm::ConstantInt::get(i64_ty, field.offset));
    return StorageMemberAddress(
        emitter,
        builder,
        field_i8,
        field.llvm_type ? field.llvm_type : field_ty,
        field_ty,
        field.recursive_indirect);
  }

  llvm::Value *StorageForDerivedAggregateLiteral(
      LLVMEmitter &emitter,
      llvm::IRBuilder<> *builder,
      const LowerCtx &ctx,
      const IRValue &value,
      const DerivedValueInfo &derived)
  {
    (void)derived;
    if (!builder)
    {
      return nullptr;
    }
    analysis::TypeRef value_type = LookupStorageType(emitter, ctx, value);
    llvm::Type *llvm_ty = value_type ? emitter.GetLLVMType(value_type) : nullptr;
    if (!IsStorageBackedAggregate(emitter, llvm_ty))
    {
      return nullptr;
    }
    llvm::Function *func =
        builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
    if (!func)
    {
      return nullptr;
    }
    llvm::AllocaInst *storage =
        emitter.AcquireReusableAggregateStorage(func, llvm_ty, "aggregate.literal");
    if (!storage)
    {
      return nullptr;
    }
    llvm::Value *typed_storage = storage;
    llvm::Type *ptr_ty = llvm::PointerType::get(llvm_ty, 0);
    if (typed_storage->getType() != ptr_ty)
    {
      typed_storage = builder->CreateBitCast(typed_storage, ptr_ty);
    }
    if (!TryEmitDerivedAggregateToStorage(
            emitter,
            builder,
            typed_storage,
            value,
            value_type))
    {
      return nullptr;
    }
    emitter.SetTempStorage(value, typed_storage);
    return typed_storage;
  }

  bool MatchesAggregateReturnLocal(const IRBindVar &bind,
                                   const IRAggregateCopyElision &info)
  {
    if (!info.return_local_uses_sret)
    {
      return false;
    }
    return bind.name == info.return_local ||
           (!info.return_local_stable_name.empty() &&
            bind.name == info.return_local_stable_name) ||
           (!bind.stable_name.empty() &&
            (bind.stable_name == info.return_local ||
             bind.stable_name == info.return_local_stable_name));
  }

  llvm::Value *SRetStorageForAggregateReturnLocal(LLVMEmitter &emitter,
                                                   llvm::Function *func,
                                                   const IRBindVar &bind,
                                                   llvm::Type *bind_ty)
  {
    if (!func || !bind_ty || func->arg_size() == 0)
    {
      return nullptr;
    }
    const LowerCtx *ctx = emitter.GetCurrentCtx();
    if (!ctx)
    {
      return nullptr;
    }
    const std::string symbol = std::string(func->getName());
    const LowerCtx::ProcSigInfo *sig = ctx->LookupProcSig(symbol);
    if (!sig || !sig->aggregate_copy_elision.has_value() ||
        !MatchesAggregateReturnLocal(bind, *sig->aggregate_copy_elision))
    {
      return nullptr;
    }
    ABICallResult abi = ComputeProcABI(emitter, symbol, sig->params, sig->ret);
    if (!abi.valid || !abi.has_sret)
    {
      return nullptr;
    }
    llvm::Value *out = func->getArg(0);
    llvm::Type *target_ptr_ty = llvm::PointerType::get(bind_ty, 0);
    if (out->getType() == target_ptr_ty)
    {
      return out;
    }
    auto *builder =
        static_cast<llvm::IRBuilder<> *>(emitter.GetBuilderRaw());
    return builder ? builder->CreateBitCast(out, target_ptr_ty) : nullptr;
  }

  } // namespace

  llvm::Value *LLVMEmitter::GetAddressableStorage(const IRValue &value)
  {
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder)
    {
      return nullptr;
    }

    switch (value.kind)
    {
    case IRValue::Kind::Local:
    {
      llvm::Value *local = GetLocalBindStorage(value.name);
      return (local && local->getType()->isPointerTy()) ? local : nullptr;
    }
    case IRValue::Kind::Symbol:
    {
      if (llvm::Value *global = GetGlobal(value.name))
      {
        return global->getType()->isPointerTy() ? global : nullptr;
      }
      if (llvm::GlobalVariable *global = module_->getNamedGlobal(value.name))
      {
        return global;
      }
      return nullptr;
    }
    case IRValue::Kind::Opaque:
    {
      if (llvm::Value *storage = GetTempStorage(value))
      {
        return storage;
      }
      const LowerCtx *ctx = GetCurrentCtx();
      if (!ctx)
      {
        return nullptr;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(value);
      if (!derived)
      {
        return nullptr;
      }
      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::Field:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedField(*this,
                                      builder,
                                      *ctx,
                                      scope,
                                      value,
                                      *derived);
      }
      case DerivedValueInfo::Kind::Tuple:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedTuple(*this,
                                      builder,
                                      *ctx,
                                      scope,
                                      value,
                                      *derived);
      }
      case DerivedValueInfo::Kind::Index:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedIndex(*this,
                                      builder,
                                      *ctx,
                                      scope,
                                      value,
                                      *derived);
      }
      case DerivedValueInfo::Kind::EnumPayloadIndex:
      case DerivedValueInfo::Kind::EnumPayloadField:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedEnumPayload(*this,
                                            builder,
                                            *ctx,
                                            scope,
                                            value,
                                            *derived);
      }
      case DerivedValueInfo::Kind::UnionPayload:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedUnionPayload(*this,
                                             builder,
                                             *ctx,
                                             scope,
                                             value,
                                             *derived);
      }
      case DerivedValueInfo::Kind::ModalField:
      {
        const analysis::ScopeContext &scope = BuildScope(ctx);
        return StorageForDerivedModalField(*this,
                                           builder,
                                           *ctx,
                                           scope,
                                           value,
                                           *derived);
      }
      case DerivedValueInfo::Kind::RecordLit:
      case DerivedValueInfo::Kind::ArrayLit:
      case DerivedValueInfo::Kind::ArraySegments:
      case DerivedValueInfo::Kind::ArrayRepeat:
      case DerivedValueInfo::Kind::EnumLit:
      {
        return StorageForDerivedAggregateLiteral(*this,
                                                 builder,
                                                 *ctx,
                                                 value,
                                                 *derived);
      }
      case DerivedValueInfo::Kind::AddrLocal:
      case DerivedValueInfo::Kind::AddrStatic:
      case DerivedValueInfo::Kind::AddrField:
      case DerivedValueInfo::Kind::AddrTuple:
      case DerivedValueInfo::Kind::AddrIndex:
      case DerivedValueInfo::Kind::AddrDeref:
      case DerivedValueInfo::Kind::AddrUnionPayload:
      {
        llvm::Value *addr = EvaluateIRValue(value);
        return (addr && addr->getType()->isPointerTy()) ? addr : nullptr;
      }
      case DerivedValueInfo::Kind::LoadFromAddr:
      {
        llvm::Value *base = EvaluateIRValue(derived->base);
        if (!base)
        {
          return nullptr;
        }
        analysis::TypeRef value_type =
            ctx ? ctx->LookupValueType(value) : nullptr;
        llvm::Type *value_llvm_ty =
            value_type ? GetLLVMType(value_type) : nullptr;
        llvm::Type *target_ptr_ty =
            value_llvm_ty ? llvm::PointerType::get(value_llvm_ty, 0) : nullptr;
        if (base->getType()->isIntegerTy() && target_ptr_ty)
        {
          return builder->CreateIntToPtr(base, target_ptr_ty);
        }
        if (!base->getType()->isPointerTy())
        {
          return nullptr;
        }
        if (target_ptr_ty && base->getType() != target_ptr_ty)
        {
          return builder->CreateBitCast(base, target_ptr_ty);
        }
        return base;
      }
      default:
        return nullptr;
      }
    }
    default:
      return nullptr;
    }
  }

  LLVMEmitter::FlowStateSnapshot LLVMEmitter::SaveFlowState() const
  {
    FlowStateSnapshot snapshot;
    snapshot.locals = locals_;
    snapshot.local_home_storage = local_home_storage_;
    snapshot.local_types = local_types_;
    snapshot.values = values_;
    snapshot.storage_values = storage_values_;
    snapshot.preferred_result_storage = preferred_result_storage_;
    return snapshot;
  }

  void LLVMEmitter::RestoreFlowState(const FlowStateSnapshot &snapshot)
  {
    std::vector<std::pair<std::string, llvm::Value *>> added_home_storage;
    for (const auto &[name, storage] : local_home_storage_)
    {
      if (storage && !snapshot.local_home_storage.contains(name))
      {
        added_home_storage.emplace_back(name, storage);
      }
    }

    std::vector<std::pair<std::string, analysis::TypeRef>> added_local_types;
    for (const auto &[name, type] : local_types_)
    {
      if (type && !snapshot.local_types.contains(name))
      {
        added_local_types.emplace_back(name, type);
      }
    }

    locals_ = snapshot.locals;
    local_home_storage_ = snapshot.local_home_storage;
    local_types_ = snapshot.local_types;
    values_ = snapshot.values;
    storage_values_ = snapshot.storage_values;
    preferred_result_storage_ = snapshot.preferred_result_storage;
    for (auto &[name, storage] : added_home_storage)
    {
      local_home_storage_.emplace(std::move(name), storage);
    }
    for (auto &[name, type] : added_local_types)
    {
      local_types_.emplace(std::move(name), type);
    }
  }

  llvm::AllocaInst *LLVMEmitter::AcquireReusableAggregateStorage(
      llvm::Function *func,
      llvm::Type *ty,
      std::string_view name)
  {
    if (!func || !ty) {
      return nullptr;
    }
    if (!ty->isStructTy() && !ty->isArrayTy()) {
      return nullptr;
    }

    auto func_it = reusable_aggregate_storage_.find(func);
    if (func_it != reusable_aggregate_storage_.end()) {
      auto type_it = func_it->second.find(ty);
      if (type_it != func_it->second.end() && !type_it->second.empty()) {
        llvm::AllocaInst *slot = type_it->second.back();
        type_it->second.pop_back();
        return slot;
      }
    }

    return CreateEntryAlloca(func, ty, std::string(name));
  }

  void LLVMEmitter::ReleaseReusableAggregateStorage(llvm::Value *storage)
  {
    auto *alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(storage);
    if (!alloca) {
      return;
    }
    llvm::Type *ty = alloca->getAllocatedType();
    if (!ty || (!ty->isStructTy() && !ty->isArrayTy())) {
      return;
    }
    llvm::Function *func = alloca->getFunction();
    if (!func) {
      return;
    }
    auto &bucket = reusable_aggregate_storage_[func][ty];
    if (std::find(bucket.begin(), bucket.end(), alloca) == bucket.end()) {
      bucket.push_back(alloca);
    }
  }

  void LLVMEmitter::ForgetTempStorage(const IRValue &value)
  {
    if (value.kind != IRValue::Kind::Opaque) {
      return;
    }
    storage_values_.erase(value.name);
    values_.erase(value.name);
  }

  void LLVMEmitter::ReleaseTempStorage(const IRValue &value)
  {
    if (value.kind != IRValue::Kind::Opaque) {
      return;
    }
    auto it = storage_values_.find(value.name);
    if (it == storage_values_.end()) {
      return;
    }
    ReleaseReusableAggregateStorage(it->second);
    storage_values_.erase(it);
    values_.erase(value.name);
  }

  void LLVMEmitter::ReleaseMoveConsumedStorage(const IRValue &value)
  {
    if (value.kind == IRValue::Kind::Opaque)
    {
      ReleaseTempStorage(value);
      return;
    }

    if (value.kind != IRValue::Kind::Local)
    {
      return;
    }

    auto local_it = locals_.find(value.name);
    if (local_it == locals_.end())
    {
      return;
    }

    llvm::Value *storage = local_it->second;
    auto *alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(storage);
    if (!alloca)
    {
      return;
    }

    llvm::Type *ty = alloca->getAllocatedType();
    if (!ty || (!ty->isStructTy() && !ty->isArrayTy()))
    {
      return;
    }

    if (GetLocalHomeStorage(value.name) != storage)
    {
      SetLocalHomeStorage(value.name, storage);
    }
    locals_.erase(local_it);
  }

  void LLVMEmitter::RegisterLocalBindStorage(const std::string &name, llvm::Value *val)
  {
    SetLocal(name, val);
    if (val && val->getType()->isPointerTy())
    {
      SetLocalHomeStorage(name, val);
    }
  }

  llvm::Value *LLVMEmitter::GetLocalBindStorage(const std::string &name)
  {
    llvm::Value *local = GetLocal(name);
    if (local && local->getType()->isPointerTy())
    {
      if (GetLocalHomeStorage(name) != local)
      {
        SetLocalHomeStorage(name, local);
      }
      return local;
    }
    return GetLocalHomeStorage(name);
  }

  // T-LLVM-010: Bind local variable
  void LLVMEmitter::EmitBindVar(const IRBindVar &bind)
  {
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    llvm::Value *init_val = nullptr;
    llvm::Type *ty = nullptr;
    if (bind.type)
    {
      ty = GetLLVMType(bind.type);
    }
    if ((!ty || ty->isVoidTy()) && init_val)
    {
      ty = init_val->getType();
    }
    if (bind.type && init_val)
    {
      analysis::TypeRef stripped = analysis::StripPerm(bind.type);
      if (!stripped)
      {
        stripped = bind.type;
      }
      if (stripped &&
          std::holds_alternative<analysis::TypeFunc>(stripped->node) &&
          IsClosurePairLLVMType(init_val->getType()))
      {
        // Non-capturing closures are represented as (env_ptr, code_ptr) pairs
        // during lowering. Preserve the concrete closure value representation
        // even when the source-level binding is annotated as TypeFunc.
        ty = init_val->getType();
      }
    }
    if (!ty || ty->isVoidTy())
    {
      ty = llvm::Type::getInt64Ty(context_);
    }
    llvm::Function *func = builder->GetInsertBlock()->getParent();
    llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
    llvm::Value *bind_slot = nullptr;
    bool adopted_existing_storage = false;
    auto is_current_bind_alias = [&](const std::string &name) -> bool
    {
      return name == bind.name ||
             (!bind.stable_name.empty() && name == bind.stable_name);
    };
    auto aliases_live_local_storage = [&](llvm::Value *storage) -> bool
    {
      for (const auto &[name, local_storage] : local_home_storage_)
      {
        if (is_current_bind_alias(name))
        {
          continue;
        }
        if (SameStorageObject(storage, local_storage))
        {
          return true;
        }
      }
      for (const auto &[name, local_storage] : locals_)
      {
        if (is_current_bind_alias(name))
        {
          continue;
        }
        if (SameStorageObject(storage, local_storage))
        {
          return true;
        }
      }
      return false;
    };
    analysis::TypeRef source_type = nullptr;
    if (bind.value.kind == IRValue::Kind::Local)
    {
      const auto it = local_types_.find(bind.value.name);
      if (it != local_types_.end())
      {
        source_type = it->second;
      }
    }
    if (!source_type)
    {
      if (const LowerCtx *ctx = GetCurrentCtx())
      {
        source_type = ctx->LookupValueType(bind.value);
      }
    }
    llvm::Type *source_llvm_ty = source_type ? GetLLVMType(source_type) : nullptr;
    IRBindVar bind_for_slot = bind;
    if (!bind_for_slot.type && source_type)
    {
      bind_for_slot.type = source_type;
    }
    std::optional<BindSlot> bind_slot_info;
    if (current_ctx_)
    {
      bind_slot_info = ResolveBindSlot(bind_for_slot, *current_ctx_);
      if (!bind_slot_info.has_value())
      {
        if (!ty || ty->isVoidTy())
        {
          current_ctx_->ReportCodegenFailure();
          return;
        }
        BindSlot fallback_slot;
        fallback_slot.kind = BindSlot::Kind::Alloca;
        fallback_slot.name = bind.name;
        fallback_slot.type = bind_for_slot.type;
        bind_slot_info = std::move(fallback_slot);
      }
    }
    const std::string async_slot_name =
        !bind.stable_name.empty() &&
                async_state_ &&
                async_state_->info &&
                async_state_->info->slots.contains(bind.stable_name)
            ? bind.stable_name
            : bind.name;
    if (async_state_ && async_state_->info &&
        async_state_->info->slots.contains(async_slot_name))
    {
      bind_slot = GetLocal(async_slot_name);
    }

    const bool aggregate_bind_ty = ty && (ty->isStructTy() || ty->isArrayTy());
    const bool use_region_slot =
        bind_slot_info.has_value() &&
        bind_slot_info->kind == BindSlot::Kind::RegionSlot;
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) &&
        !use_region_slot &&
        aggregate_bind_ty)
    {
      bind_slot =
          SRetStorageForAggregateReturnLocal(*this, func, bind, ty);
    }
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) &&
        !use_region_slot &&
        aggregate_bind_ty)
    {
      if (bind.value.kind == IRValue::Kind::Opaque)
      {
        if (llvm::Value *existing_storage = GetTempStorage(bind.value))
        {
          bool compatible_storage = (source_llvm_ty == ty);
          auto *alloca_inst =
              llvm::dyn_cast<llvm::AllocaInst>(existing_storage->stripPointerCasts());
          if (!compatible_storage && alloca_inst)
          {
            compatible_storage = (alloca_inst->getAllocatedType() == ty);
          }
          if (compatible_storage && alloca_inst &&
              !aliases_live_local_storage(existing_storage))
          {
            llvm::Type *slot_ptr_ty = llvm::PointerType::get(ty, 0);
            if (existing_storage->getType() != slot_ptr_ty)
            {
              existing_storage = builder->CreateBitCast(existing_storage, slot_ptr_ty);
            }
            bind_slot = existing_storage;
            adopted_existing_storage = true;
          }
        }
      }
    }
    if ((!bind_slot || !bind_slot->getType()->isPointerTy()) && use_region_slot)
    {
      IRValue region_local;
      region_local.kind = IRValue::Kind::Local;
      region_local.name = bind_slot_info->region;
      llvm::Value *region_value = EvaluateIRValue(region_local);
      if (!region_value)
      {
        if (current_ctx_)
        {
          SPEC_RULE("BindSlot-Err");
          current_ctx_->ReportCodegenFailure();
        }
        return;
      }

      std::uint64_t alloc_size = 0;
      std::uint64_t alloc_align = 1;
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      if (bind_slot_info->type)
      {
        if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, bind_slot_info->type))
        {
          alloc_size = *size;
        }
        if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, bind_slot_info->type))
        {
          alloc_align = *align;
        }
      }
      const llvm::DataLayout &dl = GetModule().getDataLayout();
      if (alloc_size == 0 && !ty->isVoidTy())
      {
        alloc_size = static_cast<std::uint64_t>(dl.getTypeAllocSize(ty));
      }
      if (alloc_align == 0)
      {
        alloc_align = 1;
      }
      if (alloc_align == 1 && !ty->isVoidTy())
      {
        alloc_align = std::max<std::uint64_t>(
            alloc_align,
            static_cast<std::uint64_t>(dl.getABITypeAlign(ty).value()));
      }

      llvm::Value *raw_ptr = nullptr;
      const std::string alloc_sym = BuiltinModalSymRegionAlloc();
      if (std::optional<RuntimeFuncInfo> alloc_info = GetRuntimeFuncInfo(alloc_sym))
      {
        llvm::Function *alloc_fn = GetModule().getFunction(alloc_sym);
        const bool runtime_c_aggregate_boundary =
            RuntimeUsesCAggregateABI(alloc_sym);
        const bool runtime_foreign_boundary = RuntimeUsesForeignABI(alloc_sym);
        const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
        if (!alloc_fn)
        {
          ABICallResult alloc_abi = ComputeCallABI(
              *this,
              alloc_info->params,
              alloc_info->ret,
              use_c_abi_aggregate_sret,
              /*foreign_boundary_mode_independent=*/runtime_foreign_boundary,
              RuntimeUsesExplicitOutResultABI(alloc_sym));
          if (alloc_abi.func_type)
          {
            alloc_fn = llvm::Function::Create(
                alloc_abi.func_type,
                llvm::GlobalValue::ExternalLinkage,
                alloc_sym,
                &GetModule());
            alloc_fn->setCallingConv(llvm::CallingConv::C);
          }
        }
        if (alloc_fn)
        {
          llvm::Type *usize_ty = llvm::Type::getInt64Ty(GetContext());
          std::vector<llvm::Value *> alloc_args;
          alloc_args.reserve(3);
          alloc_args.push_back(region_value);
          alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_size));
          alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_align));
          raw_ptr = EmitABICall(
              *this,
              builder,
              alloc_fn,
              alloc_info->params,
              alloc_info->ret,
              alloc_args,
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

      if (!raw_ptr)
      {
        if (current_ctx_)
        {
          SPEC_RULE("BindSlot-Err");
          current_ctx_->ReportCodegenFailure();
        }
        return;
      }

      bind_slot = builder->CreateBitCast(raw_ptr, llvm::PointerType::get(ty, 0));
    }
    if (!bind_slot || !bind_slot->getType()->isPointerTy())
    {
      bind_slot = entry_builder.CreateAlloca(ty, nullptr, bind.name);
    }
    if (bind.type)
    {
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, bind.type); align.has_value())
      {
        if (auto *alloca_inst = llvm::dyn_cast<llvm::AllocaInst>(bind_slot))
        {
          alloca_inst->setAlignment(llvm::Align(std::max<std::uint64_t>(1, *align)));
        }
      }
    }

    bool copied_from_storage = false;
    if (!adopted_existing_storage)
    {
      llvm::Value *source_storage = GetAddressableStorage(bind.value);
      const bool binds_pointer_value =
          IsPointerValueType(source_type) || IsPointerValueType(bind.type);
      if (source_storage && !binds_pointer_value)
      {
        const BindingState *target_state =
            current_ctx_ ? current_ctx_->GetBindingState(bind.name) : nullptr;
        const bool source_is_temp_storage =
            bind.value.kind == IRValue::Kind::Opaque &&
            GetTempStorage(bind.value) == source_storage;
        copied_from_storage = TryEmitBitcopyAggregateStorageCopy(
            *this,
            builder,
            bind_slot,
            source_storage,
            bind.type ? bind.type : source_type,
            source_type);
        if (!copied_from_storage &&
            ((target_state && !target_state->has_responsibility) ||
             source_is_temp_storage))
        {
          copied_from_storage = TryEmitAggregateStorageTransfer(
              *this,
              builder,
              bind_slot,
              source_storage,
              bind.type ? bind.type : source_type,
              source_type);
        }
        if (!copied_from_storage &&
            ty &&
            !ty->isVoidTy() &&
            !IsZeroSizedLLVMType(*this, ty) &&
            !IsStorageBackedAggregate(*this, ty))
        {
          llvm::Value *typed_source = builder->CreateBitCast(
              source_storage,
              llvm::PointerType::get(ty, 0));
          llvm::LoadInst *source_load = builder->CreateLoad(ty, typed_source);
          source_load->setAlignment(llvm::Align(1));
          init_val = source_load;
        }
      }
      else
      {
        copied_from_storage = TryEmitDerivedAggregateToStorage(
            *this,
            builder,
            bind_slot,
            bind.value,
            bind.type ? bind.type : source_type);
      }
    }

    if (!adopted_existing_storage && !copied_from_storage && !init_val)
    {
      init_val = EvaluateIRValue(bind.value);
    }

    // Store the initial value
    if (!adopted_existing_storage && !copied_from_storage && !init_val)
    {
      init_val = llvm::Constant::getNullValue(ty);
    }
    else if (!adopted_existing_storage && !copied_from_storage)
    {
      llvm::Value *coerced_init =
          CoerceToTyped(*this, builder, init_val, ty, source_type, bind.type);
      if (!coerced_init)
      {
        if (init_val->getType() == ty)
        {
          coerced_init = init_val;
        }
        else if (llvm::Value *plain = CoerceTo(builder, init_val, ty))
        {
          coerced_init = plain;
        }
      }
      init_val = coerced_init;
      if (!init_val)
      {
        init_val = llvm::Constant::getNullValue(ty);
      }
    }
    if (!adopted_existing_storage && !copied_from_storage)
    {
      llvm::Value *typed_slot = bind_slot;
      llvm::Type *slot_ptr_ty = llvm::PointerType::get(ty, 0);
      if (typed_slot->getType() != slot_ptr_ty)
      {
        typed_slot = builder->CreateBitCast(typed_slot, slot_ptr_ty);
      }
      builder->CreateStore(init_val, typed_slot);
    }
    else if (adopted_existing_storage)
    {
      storage_values_.erase(bind.value.name);
      values_.erase(bind.value.name);
    }

    if (!adopted_existing_storage)
    {
      ReleaseTempStorage(bind.value);
    }

    RegisterLocalBindStorage(bind.name, bind_slot);
    if (bind.type)
    {
      local_types_[bind.name] = bind.type;
    }
    if (!bind.stable_name.empty() && bind.stable_name != bind.name)
    {
      RegisterLocalBindStorage(bind.stable_name, bind_slot);
      if (bind.type)
      {
        local_types_[bind.stable_name] = bind.type;
      }
    }
  }

} // namespace ultraviolet::codegen
