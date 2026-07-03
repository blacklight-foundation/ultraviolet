// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/if_case.cpp
// Canonical owner for LLVM IR if-case instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include "00_core/assert_spec.h"
#include "05_codegen/llvm/llvm_ub_safe.h"

#include <cctype>
#include <functional>
#include <string>
#include <unordered_set>

namespace ultraviolet::codegen::emit_detail {

namespace {

void RecordIfCaseLoweringConformance(std::size_t arm_count,
                                      const char *result_form)
{
  if (!core::Conformance::Enabled())
  {
    return;
  }

  core::Conformance::Record(
      "def.24.StructuredIRLoweringForms",
      std::nullopt,
      "obligation=def.24.StructuredIRLoweringForms;"
      "structured_form=IfCaseIRForm;"
      "structured_lower_form=IfCaseLowerForm;"
      "source=IRIfCaseEmission");

  std::string payload =
      "obligation=rule.24.Lower-IfCaseIR;"
      "ir_form=IfCaseIRForm;"
      "lower_form=IfCaseLowerForm;"
      "arm_count=";
  payload += std::to_string(arm_count);
  payload += ";llvm_condbr=true;merge=ifcase.merge;result=";
  payload += result_form ? result_form : "value";
  core::Conformance::Record("rule.24.Lower-IfCaseIR", std::nullopt, payload);
}

}  // namespace

void IRInstructionVisitor::operator()(const IRIfCase &if_case) const
{
  llvm::Function *func = builder.GetInsertBlock()->getParent();
  if (!func)
  {
    emitter.SetTempValue(if_case.result, DefaultFor(if_case.result));
    return;
  }

  const LowerCtx *ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(ctx);

  analysis::TypeRef root_scrutinee_type = if_case.scrutinee_type;
  if (!root_scrutinee_type && ctx)
  {
    root_scrutinee_type = ctx->LookupValueType(if_case.scrutinee);
  }

  llvm::Type *root_scrutinee_ll_ty =
      root_scrutinee_type ? emitter.GetLLVMType(root_scrutinee_type) : nullptr;
  llvm::Value *scrutinee_storage = emitter.GetAddressableStorage(if_case.scrutinee);
  const bool scrutinee_from_storage =
      scrutinee_storage && root_scrutinee_ll_ty &&
      IsAddressBackedAggregateType(root_scrutinee_ll_ty);
  llvm::Value *scrutinee =
      scrutinee_from_storage ? nullptr : EvaluateOrDefault(if_case.scrutinee);
  llvm::Value *match_subject =
      scrutinee_from_storage ? scrutinee_storage : scrutinee;
  if (!match_subject || if_case.arms.empty())
  {
    emitter.SetTempValue(if_case.result, DefaultFor(if_case.result));
    return;
  }

  auto parse_int_literal = [](const std::string &lexeme) -> std::optional<long long>
  {
    if (lexeme.empty())
    {
      return std::nullopt;
    }
    std::size_t i = 0;
    if (lexeme[i] == '+' || lexeme[i] == '-')
    {
      ++i;
    }
    const std::size_t start = i;
    while (i < lexeme.size() && std::isdigit(static_cast<unsigned char>(lexeme[i])))
    {
      ++i;
    }
    if (i == start)
    {
      return std::nullopt;
    }
    try
    {
      return std::stoll(lexeme.substr(0, i));
    }
    catch (...)
    {
      return std::nullopt;
    }
  };

  auto normalize_match_type = [&](analysis::TypeRef ty) -> analysis::TypeRef
  {
    if (!ty)
    {
      return nullptr;
    }
    analysis::TypeRef stripped = analysis::StripPerm(ty);
    if (!stripped)
    {
      stripped = ty;
    }
    analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, stripped);
    if (!resolved)
    {
      return stripped;
    }
    analysis::TypeRef resolved_stripped = analysis::StripPerm(resolved);
    if (!resolved_stripped)
    {
      resolved_stripped = resolved;
    }
    return resolved_stripped;
  };

  auto lookup_enum_decl = [&](analysis::TypeRef type,
                              analysis::TypePath *out_path) -> const ast::EnumDecl *
  {
    type = normalize_match_type(type);
    const auto *path = type ? analysis::AppliedTypePath(*type) : nullptr;
    if (!path)
    {
      return nullptr;
    }
    if (out_path)
    {
      *out_path = *path;
    }
    if (const ast::EnumDecl* decl = analysis::LookupEnumDecl(scope, *path))
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
  };

  auto enum_generic_args_for_type = [&](analysis::TypeRef type)
      -> std::vector<analysis::TypeRef>
  {
    type = normalize_match_type(type);
    if (!type)
    {
      return {};
    }
    const auto *args = analysis::AppliedTypeArgs(*type);
    if (!args)
    {
      return {};
    }
    return *args;
  };

  auto find_variant = [](const ast::EnumDecl &decl,
                         std::string_view variant_name) -> const ast::VariantDecl *
  {
    for (const auto &variant : decl.variants)
    {
      if (analysis::IdEq(variant.name, std::string(variant_name)))
      {
        return &variant;
      }
    }
    return nullptr;
  };

  auto variant_disc = [&](const ast::EnumDecl &decl,
                          std::string_view variant_name) -> std::optional<std::uint64_t>
  {
    const auto discs = analysis::EnumDiscriminants(decl);
    if (!discs.ok || discs.discs.size() != decl.variants.size())
    {
      return std::nullopt;
    }
    for (std::size_t i = 0; i < decl.variants.size(); ++i)
    {
      if (analysis::IdEq(decl.variants[i].name, std::string(variant_name)))
      {
        return discs.discs[i];
      }
    }
    return std::nullopt;
  };

  struct EnumPayloadMemberInfo
  {
    analysis::TypeRef type;
    std::uint64_t offset = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_align = 1;
    bool ok = false;
  };

  auto payload_member_by_index = [&](const ast::EnumDecl &enum_decl,
                                     const ast::VariantDecl &variant,
                                     const std::vector<analysis::TypeRef> &generic_args,
                                     std::size_t index) -> EnumPayloadMemberInfo
  {
    EnumPayloadMemberInfo out;
    const auto member = ::ultraviolet::analysis::layout::EnumTuplePayloadMemberLayout(
        scope,
        enum_decl,
        variant,
        generic_args,
        index);
    if (!member.has_value())
    {
      return out;
    }
    out.type = member->type;
    out.offset = member->offset;
    out.payload_size = member->payload_size;
    out.payload_align = member->payload_align;
    out.ok = true;
    return out;
  };

  auto payload_member_by_field = [&](const ast::EnumDecl &enum_decl,
                                     const ast::VariantDecl &variant,
                                     const std::vector<analysis::TypeRef> &generic_args,
                                     std::string_view field_name) -> EnumPayloadMemberInfo
  {
    EnumPayloadMemberInfo out;
    const auto member = ::ultraviolet::analysis::layout::EnumRecordPayloadMemberLayout(
        scope,
        enum_decl,
        variant,
        generic_args,
        field_name);
    if (!member.has_value())
    {
      return out;
    }
    out.type = member->type;
    out.offset = member->offset;
    out.payload_size = member->payload_size;
    out.payload_align = member->payload_align;
    out.ok = true;
    return out;
  };

  std::unordered_set<llvm::Value *> storage_subjects;
  auto storage_subject_key = [](llvm::Value *value) -> llvm::Value *
  {
    return value ? value->stripPointerCasts() : nullptr;
  };
  auto remember_storage_subject = [&](llvm::Value *value) -> void
  {
    if (llvm::Value *key = storage_subject_key(value))
    {
      storage_subjects.insert(key);
    }
  };
  auto is_storage_subject = [&](llvm::Value *value) -> bool
  {
    llvm::Value *key = storage_subject_key(value);
    return key && storage_subjects.find(key) != storage_subjects.end();
  };
  if (scrutinee_from_storage)
  {
    remember_storage_subject(match_subject);
  }

  auto bitcast_storage_ptr = [&](llvm::Value *storage,
                                 llvm::Type *pointee_ty) -> llvm::Value *
  {
    if (!storage || !pointee_ty || !storage->getType()->isPointerTy() ||
        pointee_ty->isVoidTy())
    {
      return nullptr;
    }
    llvm::Type *ptr_ty = llvm::PointerType::get(pointee_ty, 0);
    return storage->getType() == ptr_ty ? storage
                                        : builder.CreateBitCast(storage, ptr_ty);
  };

  auto load_storage_value = [&](llvm::Value *storage,
                                llvm::Type *value_ty) -> llvm::Value *
  {
    llvm::Value *ptr = bitcast_storage_ptr(storage, value_ty);
    if (!ptr)
    {
      return nullptr;
    }
    llvm::LoadInst *load = builder.CreateLoad(value_ty, ptr);
    load->setAlignment(llvm::Align(1));
    return load;
  };

  auto value_or_storage_from_ptr = [&](llvm::Value *storage,
                                       llvm::Type *storage_ty,
                                       llvm::Type *value_ty,
                                       bool recursive_indirect) -> llvm::Value *
  {
    if (!storage || !storage_ty || !value_ty)
    {
      return nullptr;
    }
    llvm::Value *typed_ptr = bitcast_storage_ptr(storage, storage_ty);
    if (!typed_ptr)
    {
      return nullptr;
    }
    if (recursive_indirect)
    {
      llvm::Value *target_ptr = builder.CreateLoad(storage_ty, typed_ptr);
      if (!target_ptr || !target_ptr->getType()->isPointerTy())
      {
        return nullptr;
      }
      target_ptr = builder.CreateBitCast(
          target_ptr,
          llvm::PointerType::get(value_ty, 0));
      if (IsAddressBackedAggregateType(value_ty))
      {
        remember_storage_subject(target_ptr);
        return target_ptr;
      }
      llvm::LoadInst *target_load = builder.CreateLoad(value_ty, target_ptr);
      target_load->setAlignment(llvm::Align(1));
      return target_load;
    }
    if (IsAddressBackedAggregateType(value_ty))
    {
      remember_storage_subject(typed_ptr);
      return typed_ptr;
    }
    llvm::LoadInst *load = builder.CreateLoad(storage_ty, typed_ptr);
    load->setAlignment(llvm::Align(1));
    if (load->getType() == value_ty)
    {
      return load;
    }
    if (llvm::Value *coerced = CoerceTo(&builder, load, value_ty))
    {
      return coerced;
    }
    return load;
  };

  auto load_tag_from_storage = [&](llvm::Value *storage,
                                   analysis::TypeRef value_type) -> llvm::Value *
  {
    if (!storage)
    {
      return nullptr;
    }
    llvm::Type *llvm_ty = value_type ? emitter.GetLLVMType(value_type) : nullptr;
    if (!llvm_ty || llvm_ty->isVoidTy())
    {
      return nullptr;
    }
    if (llvm_ty->isIntegerTy() || llvm_ty->isPointerTy())
    {
      return load_storage_value(storage, llvm_ty);
    }
    auto *struct_ty = llvm::dyn_cast<llvm::StructType>(llvm_ty);
    if (!struct_ty || struct_ty->getNumElements() == 0)
    {
      return nullptr;
    }
    llvm::Type *disc_ty = struct_ty->getElementType(0);
    llvm::Value *typed_storage = bitcast_storage_ptr(storage, struct_ty);
    if (!typed_storage)
    {
      return nullptr;
    }
    llvm::Value *disc_value =
        EmitLoadAtOffset(emitter, &builder, typed_storage, 0, disc_ty);
    if (auto *disc = llvm::dyn_cast_or_null<llvm::LoadInst>(disc_value))
    {
      disc->setAlignment(llvm::Align(1));
    }
    return disc_value;
  };

  auto payload_member_from_storage = [&](llvm::Value *base_storage,
                                         llvm::StructType *tagged_ty,
                                         std::uint64_t payload_align,
                                         std::uint64_t member_offset,
                                         llvm::Type *storage_ty,
                                         llvm::Type *value_ty,
                                         bool recursive_indirect) -> llvm::Value *
  {
    if (!base_storage || !tagged_ty || !storage_ty || !value_ty)
    {
      return nullptr;
    }
    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        tagged_ty,
        base_storage,
        payload_align);
    if (!payload_i8)
    {
      return nullptr;
    }
    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *field_i8 = builder.CreateGEP(
        i8_ty,
        payload_i8,
        llvm::ConstantInt::get(i64_ty, member_offset));
    return value_or_storage_from_ptr(
        field_i8,
        storage_ty,
        value_ty,
        recursive_indirect);
  };

  auto field_from_storage_offset = [&](llvm::Value *base_storage,
                                       std::uint64_t offset,
                                       llvm::Type *storage_ty,
                                       llvm::Type *value_ty,
                                       bool recursive_indirect) -> llvm::Value *
  {
    if (!base_storage || !storage_ty || !value_ty)
    {
      return nullptr;
    }
    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *base_i8 = builder.CreateBitCast(
        base_storage,
        llvm::PointerType::get(i8_ty, 0));
    llvm::Value *field_i8 = builder.CreateGEP(
        i8_ty,
        base_i8,
        llvm::ConstantInt::get(i64_ty, offset));
    return value_or_storage_from_ptr(
        field_i8,
        storage_ty,
        value_ty,
        recursive_indirect);
  };

  auto enum_disc_value = [&](llvm::Value *enum_value,
                             analysis::TypeRef enum_type) -> llvm::Value *
  {
    if (!enum_value)
    {
      return nullptr;
    }
    if (is_storage_subject(enum_value))
    {
      return load_tag_from_storage(enum_value, enum_type);
    }
    if (enum_value->getType()->isIntegerTy())
    {
      return enum_value;
    }
    auto *enum_ty = llvm::dyn_cast<llvm::StructType>(enum_value->getType());
    if (!enum_ty || enum_ty->getNumElements() == 0)
    {
      return nullptr;
    }
    return builder.CreateExtractValue(enum_value, {0u});
  };

  auto load_enum_payload_member = [&](llvm::Value *enum_value,
                                      analysis::TypeRef enum_type,
                                      const EnumPayloadMemberInfo &member) -> llvm::Value *
  {
    if (!enum_value || !member.ok || !member.type)
    {
      return nullptr;
    }
    llvm::Type *member_ty = emitter.GetLLVMType(member.type);
    auto *enum_ty = llvm::dyn_cast<llvm::StructType>(enum_value->getType());
    if (!member_ty || !enum_ty || enum_ty->getNumElements() < 2)
    {
      if (is_storage_subject(enum_value))
      {
        llvm::Type *enum_ll = enum_type ? emitter.GetLLVMType(enum_type) : nullptr;
        enum_ty = llvm::dyn_cast_or_null<llvm::StructType>(enum_ll);
        if (!enum_ty || enum_ty->getNumElements() < 2)
        {
          return nullptr;
        }
        return payload_member_from_storage(
            enum_value,
            enum_ty,
            member.payload_align,
            member.offset,
            member_ty,
            member_ty,
            false);
      }
      return nullptr;
    }
    if (is_storage_subject(enum_value))
    {
      return payload_member_from_storage(
          enum_value,
          enum_ty,
          member.payload_align,
          member.offset,
          member_ty,
          member_ty,
          false);
    }
    llvm::Function *current_fn =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (!current_fn)
    {
      return nullptr;
    }
    llvm::IRBuilder<> entry_builder(
        &current_fn->getEntryBlock(),
        current_fn->getEntryBlock().begin());
    llvm::AllocaInst *enum_slot = entry_builder.CreateAlloca(enum_ty);
    builder.CreateStore(enum_value, enum_slot);

    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        enum_ty,
        enum_slot,
        member.payload_align);
    if (!payload_i8)
    {
      return nullptr;
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *field_i8 = builder.CreateGEP(
        i8_ty,
        payload_i8,
        llvm::ConstantInt::get(i64_ty, member.offset));
    return value_or_storage_from_ptr(
        field_i8,
        member_ty,
        member_ty,
        false);
  };

  auto lookup_modal_decl = [&](analysis::TypeRef type,
                               analysis::TypePath *out_path) -> const ast::ModalDecl *
  {
    type = normalize_match_type(type);
    if (!type)
    {
      return nullptr;
    }
    if (const auto *state = std::get_if<analysis::TypeModalState>(&type->node))
    {
      if (out_path)
      {
        *out_path = state->path;
      }
      return analysis::LookupModalDecl(scope, state->path);
    }
    const auto *path = analysis::AppliedTypePath(*type);
    if (!path)
    {
      return nullptr;
    }
    if (out_path)
    {
      *out_path = *path;
    }
    return analysis::LookupModalDecl(scope, *path);
  };

  auto find_modal_state = [](const ast::ModalDecl &decl,
                             std::string_view state_name) -> const ast::StateBlock *
  {
    for (const auto &state : decl.states)
    {
      if (analysis::IdEq(state.name, std::string(state_name)))
      {
        return &state;
      }
    }
    return nullptr;
  };

  auto modal_state_disc =
      [&](const ast::ModalDecl &decl,
          std::string_view state_name) -> std::optional<std::uint64_t>
  {
    for (std::size_t i = 0; i < decl.states.size(); ++i)
    {
      if (analysis::IdEq(decl.states[i].name, std::string(state_name)))
      {
        return static_cast<std::uint64_t>(i);
      }
    }
    return std::nullopt;
  };

  struct ModalPayloadMemberInfo
  {
    analysis::TypeRef type;
    llvm::Type *storage_type = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_align = 1;
    bool tagged = true;
    bool recursive_indirect = false;
    bool ok = false;
  };

  auto modal_payload_member_by_field = [&](const ast::ModalDecl &modal_decl,
                                           const analysis::TypePath &modal_path,
                                           const std::vector<analysis::TypeRef> &modal_args,
                                           std::string_view state_name,
                                           std::string_view field_name)
      -> ModalPayloadMemberInfo
  {
    ModalPayloadMemberInfo out;
    analysis::TypeSubst modal_subst;
    if (modal_decl.generic_params && !modal_decl.generic_params->params.empty())
    {
      if (modal_args.size() > modal_decl.generic_params->params.size())
      {
        return out;
      }
      modal_subst = analysis::BuildSubstitution(
          modal_decl.generic_params->params,
          modal_args);
    }

    const auto modal_layout = ::ultraviolet::analysis::layout::ModalLayoutOf(scope, modal_decl, modal_args);
    if (!modal_layout.has_value())
    {
      return out;
    }
    out.payload_size = modal_layout->payload_size;
    out.payload_align = modal_layout->payload_align;
    out.tagged = modal_layout->disc_type.has_value();

    const ast::StateBlock *state = find_modal_state(modal_decl, state_name);
    if (!state)
    {
      return out;
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
      const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, field->type);
      if (!lowered.has_value())
      {
        return out;
      }
      analysis::TypeRef field_type = *lowered;
      if (!modal_subst.empty())
      {
        field_type = analysis::InstantiateType(field_type, modal_subst);
      }
      field_types.push_back(field_type);
      field_names.push_back(field->name);
    }

    analysis::TypeRef aggregate_type = analysis::MakeTypeModalState(
        modal_path,
        std::string(state_name),
        modal_args);
    const auto layout = ComputeLayoutLLVMRecord(
        emitter,
        scope,
        aggregate_type,
        field_types);
    if (!layout.has_value())
    {
      return out;
    }
    for (std::size_t i = 0; i < field_names.size() && i < layout->fields.size(); ++i)
    {
      if (analysis::IdEq(field_names[i], std::string(field_name)))
      {
        out.type = field_types[i];
        out.storage_type = layout->fields[i].llvm_type;
        out.offset = layout->fields[i].offset;
        out.recursive_indirect = layout->fields[i].recursive_indirect;
        out.ok = true;
        break;
      }
    }
    return out;
  };

  auto modal_disc_value = [&](llvm::Value *modal_value,
                              analysis::TypeRef modal_type) -> llvm::Value *
  {
    if (!modal_value)
    {
      return nullptr;
    }
    if (is_storage_subject(modal_value))
    {
      return load_tag_from_storage(modal_value, modal_type);
    }
    if (modal_value->getType()->isIntegerTy())
    {
      // Some tagged modal values with zero-sized payload lower to a raw
      // discriminant scalar. Pattern state checks must compare that
      // discriminant directly.
      return modal_value;
    }
    auto *modal_ty = llvm::dyn_cast<llvm::StructType>(modal_value->getType());
    if (!modal_ty || modal_ty->getNumElements() == 0)
    {
      return nullptr;
    }
    return builder.CreateExtractValue(modal_value, {0u});
  };

  auto load_modal_payload_member = [&](llvm::Value *modal_value,
                                       analysis::TypeRef modal_type,
                                       const ModalPayloadMemberInfo &member) -> llvm::Value *
  {
    if (!modal_value || !member.ok || !member.type)
    {
      return nullptr;
    }
    llvm::Type *member_ty = emitter.GetLLVMType(member.type);
    llvm::Type *storage_ty = member.storage_type ? member.storage_type : member_ty;
    if (!member_ty || !storage_ty)
    {
      return nullptr;
    }
    llvm::Value *modal_slot = modal_value;
    if (!is_storage_subject(modal_value))
    {
      llvm::Function *current_fn =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return nullptr;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      modal_slot = entry_builder.CreateAlloca(modal_value->getType());
      builder.CreateStore(modal_value, modal_slot);
    }

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *payload_i8 = nullptr;
    if (member.tagged)
    {
      auto *modal_ty = llvm::dyn_cast<llvm::StructType>(modal_value->getType());
      if (!modal_ty && is_storage_subject(modal_value))
      {
        modal_ty = llvm::dyn_cast_or_null<llvm::StructType>(
            modal_type ? emitter.GetLLVMType(modal_type) : nullptr);
      }
      if (!modal_ty || modal_ty->getNumElements() < 2)
      {
        const auto member_size = ::ultraviolet::analysis::layout::SizeOf(scope, member.type);
        if (member_size.has_value() && *member_size == 0)
        {
          return llvm::Constant::getNullValue(member_ty);
        }
        return nullptr;
      }
      payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          &builder,
          modal_ty,
          modal_slot,
          member.payload_align);
    }
    else
    {
      payload_i8 = builder.CreateBitCast(
          modal_slot,
          llvm::PointerType::get(i8_ty, 0));
    }
    if (!payload_i8)
    {
      return nullptr;
    }

    llvm::Value *field_i8 = builder.CreateGEP(
        i8_ty,
        payload_i8,
        llvm::ConstantInt::get(i64_ty, member.offset));
    return value_or_storage_from_ptr(
        field_i8,
        storage_ty,
        member_ty,
        member.recursive_indirect);
  };

  auto union_disc_value = [&](llvm::Value *union_value,
                              analysis::TypeRef union_type) -> llvm::Value *
  {
    if (!union_value)
    {
      return nullptr;
    }
    if (is_storage_subject(union_value))
    {
      return load_tag_from_storage(union_value, union_type);
    }
    if (union_value->getType()->isIntegerTy() || union_value->getType()->isPointerTy())
    {
      return union_value;
    }
    auto *union_ty = llvm::dyn_cast<llvm::StructType>(union_value->getType());
    if (!union_ty || union_ty->getNumElements() == 0)
    {
      return nullptr;
    }
    return builder.CreateExtractValue(union_value, {0u});
  };

  auto load_union_payload_member =
      [&](llvm::Value *union_value,
          analysis::TypeRef union_type,
          const ::ultraviolet::analysis::layout::UnionLayout &union_layout,
          std::size_t member_index) -> llvm::Value *
  {
    if (!union_value || member_index >= union_layout.member_list.size())
    {
      return nullptr;
    }
    const analysis::TypeRef member_type = union_layout.member_list[member_index];
    if (!member_type)
    {
      return nullptr;
    }

    llvm::Type *member_ty = emitter.GetLLVMType(member_type);
    if (!member_ty)
    {
      return nullptr;
    }

    if (union_layout.niche)
    {
      if (is_storage_subject(union_value))
      {
        llvm::Type *union_ty = union_type ? emitter.GetLLVMType(union_type) : nullptr;
        if (union_ty && !union_ty->isVoidTy())
        {
          if (llvm::Value *loaded = load_storage_value(union_value, union_ty))
          {
            return CoerceTo(&builder, loaded, member_ty);
          }
        }
      }
      return CoerceTo(&builder, union_value, member_ty);
    }

    auto *union_ty = llvm::dyn_cast<llvm::StructType>(union_value->getType());
    if (!union_ty && is_storage_subject(union_value))
    {
      union_ty = llvm::dyn_cast_or_null<llvm::StructType>(
          union_type ? emitter.GetLLVMType(union_type) : nullptr);
    }
    if (!union_ty || union_ty->getNumElements() < 2)
    {
      return llvm::Constant::getNullValue(member_ty);
    }

    if (is_storage_subject(union_value))
    {
      return payload_member_from_storage(
          union_value,
          union_ty,
          union_layout.payload_align,
          0,
          member_ty,
          member_ty,
          false);
    }

    llvm::Function *current_fn =
        builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
    if (!current_fn)
    {
      return nullptr;
    }

    llvm::IRBuilder<> entry_builder(
        &current_fn->getEntryBlock(),
        current_fn->getEntryBlock().begin());
    llvm::AllocaInst *union_slot = entry_builder.CreateAlloca(union_ty);
    builder.CreateStore(union_value, union_slot);

    llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
        emitter,
        &builder,
        union_ty,
        union_slot,
        union_layout.payload_align);
    if (!payload_i8)
    {
      return llvm::Constant::getNullValue(member_ty);
    }

    llvm::Value *member_ptr = builder.CreateBitCast(
        payload_i8,
        llvm::PointerType::get(member_ty, 0));
    llvm::LoadInst *load = builder.CreateLoad(member_ty, member_ptr);
    load->setAlignment(llvm::Align(1));
    if (IsAddressBackedAggregateType(member_ty))
    {
      remember_storage_subject(member_ptr);
      return member_ptr;
    }
    return load;
  };

  std::function<llvm::Value *(const IRPatternPtr &,
                              llvm::Value *,
                              analysis::TypeRef)>
      emit_pattern_cond_for_value;

  emit_pattern_cond_for_value =
      [&](const IRPatternPtr &pattern,
          llvm::Value *subject,
          analysis::TypeRef subject_type) -> llvm::Value *
  {
    if (!pattern)
    {
      return llvm::ConstantInt::getTrue(emitter.GetContext());
    }
    return std::visit(
        [&](const auto &pat) -> llvm::Value *
        {
          using P = std::decay_t<decltype(pat)>;
          if constexpr (std::is_same_v<P, IRWildcardPattern> ||
                        std::is_same_v<P, IRIdentifierPattern>)
          {
            return llvm::ConstantInt::getTrue(emitter.GetContext());
          }
          else if constexpr (std::is_same_v<P, IRTypedPattern>)
          {
            if (!subject || !subject_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            analysis::TypeRef scrut = normalize_match_type(subject_type);
            analysis::TypeRef typed_target = nullptr;
            if (pat.type)
            {
              typed_target = analysis::StripPerm(pat.type);
              if (!typed_target)
              {
                typed_target = pat.type;
              }
            }

            const auto *target_modal_state =
                typed_target
                    ? std::get_if<analysis::TypeModalState>(&typed_target->node)
                    : nullptr;
            if (target_modal_state)
            {
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

              const auto *scrut_modal_state =
                  scrut ? std::get_if<analysis::TypeModalState>(&scrut->node) : nullptr;
              if (scrut_modal_state &&
                  same_modal_path(scrut_modal_state->path, target_modal_state->path))
              {
                return analysis::IdEq(scrut_modal_state->state, target_modal_state->state)
                           ? llvm::ConstantInt::getTrue(emitter.GetContext())
                           : llvm::ConstantInt::getFalse(emitter.GetContext());
              }

              const auto *scrut_modal_ref =
                  scrut ? std::get_if<analysis::TypePathType>(&scrut->node) : nullptr;
              if (scrut_modal_ref &&
                  same_modal_path(scrut_modal_ref->path, target_modal_state->path))
              {
                const ast::ModalDecl *modal_decl =
                    analysis::LookupModalDecl(scope, target_modal_state->path);
                if (!modal_decl)
                {
                  return llvm::ConstantInt::getFalse(emitter.GetContext());
                }
                const auto expected_disc =
                    modal_state_disc(*modal_decl, target_modal_state->state);
                llvm::Value *disc = modal_disc_value(subject, subject_type);
                if (!expected_disc.has_value() || !disc ||
                    !disc->getType()->isIntegerTy())
                {
                  return llvm::ConstantInt::getFalse(emitter.GetContext());
                }
                return EmitTypedEq(
                    &builder,
                    disc,
                    llvm::ConstantInt::get(disc->getType(), *expected_disc));
              }
            }

            const auto *uni = scrut ? std::get_if<analysis::TypeUnion>(&scrut->node) : nullptr;
            if (!uni || uni->members.empty())
            {
              // Typed patterns over non-unions are irrefutable in if-case contexts
              // once typechecking has succeeded.
              return llvm::ConstantInt::getTrue(emitter.GetContext());
            }

            auto parse_case_index = [](std::string_view name) -> std::optional<std::size_t>
            {
              constexpr std::string_view prefix = "__case";
              if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix)
              {
                return std::nullopt;
              }
              std::size_t idx = 0;
              for (std::size_t i = prefix.size(); i < name.size(); ++i)
              {
                const char ch = name[i];
                if (ch < '0' || ch > '9')
                {
                  return std::nullopt;
                }
                idx = idx * 10 + static_cast<std::size_t>(ch - '0');
              }
              return idx;
            };
            auto is_unit_type = [](const analysis::TypeRef &type) -> bool
            {
              if (!type)
              {
                return false;
              }
              analysis::TypeRef stripped = analysis::StripPerm(type);
              if (!stripped)
              {
                return false;
              }
              if (const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node))
              {
                return prim->name == "()";
              }
              return false;
            };
            auto find_member_index = [](const std::vector<analysis::TypeRef> &members,
                                        const analysis::TypeRef &target)
                -> std::optional<std::size_t>
            {
              auto strip_perm_refine = [](analysis::TypeRef type) -> analysis::TypeRef
              {
                while (type)
                {
                  if (const auto *perm = std::get_if<analysis::TypePerm>(&type->node))
                  {
                    type = perm->base;
                    continue;
                  }
                  if (const auto *refine = std::get_if<analysis::TypeRefine>(&type->node))
                  {
                    type = refine->base;
                    continue;
                  }
                  break;
                }
                return type;
              };
              if (!target)
              {
                return std::nullopt;
              }
              const analysis::TypeRef target_base = strip_perm_refine(target);
              for (std::size_t i = 0; i < members.size(); ++i)
              {
                const auto equiv =
                    analysis::TypeEquiv(strip_perm_refine(members[i]), target_base);
                if (equiv.ok && equiv.equiv)
                {
                  return i;
                }
              }
              return std::nullopt;
            };

            std::vector<analysis::TypeRef> members = uni->members;
            std::optional<::ultraviolet::analysis::layout::UnionLayout> union_layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *uni);
            if (union_layout.has_value())
            {
              members = union_layout->member_list;
            }

            std::optional<std::size_t> member_index;
            if (pat.type)
            {
              member_index = find_member_index(members, pat.type);
            }
            if (!member_index.has_value())
            {
              member_index = parse_case_index(pat.name);
            }
            if (!member_index.has_value() || *member_index >= members.size())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            if (union_layout.has_value() && union_layout->niche)
            {
              // Ultraviolet niche layout is currently used for pointer-valid payload unions.
              // All non-payload members are unit and represented by the niche value.
              std::optional<std::size_t> payload_index;
              for (std::size_t i = 0; i < members.size(); ++i)
              {
                if (!is_unit_type(members[i]))
                {
                  payload_index = i;
                  break;
                }
              }
              if (!payload_index.has_value())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }

              llvm::Value *disc = union_disc_value(subject, subject_type);
              if (!disc || !disc->getType()->isPointerTy())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              llvm::Constant *niche_zero = llvm::Constant::getNullValue(disc->getType());
              if (*member_index == *payload_index)
              {
                return builder.CreateICmpNE(disc, niche_zero);
              }
              return builder.CreateICmpEQ(disc, niche_zero);
            }

            llvm::Value *disc = is_storage_subject(subject)
                                     ? union_disc_value(subject, subject_type)
                                     : nullptr;
            if (!disc)
            {
              auto *subject_struct =
                  llvm::dyn_cast<llvm::StructType>(subject->getType());
              if (!subject_struct || subject_struct->getNumElements() < 1)
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              disc = builder.CreateExtractValue(subject, {0u});
            }
            if (!disc || !disc->getType()->isIntegerTy())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            return EmitTypedEq(
                &builder,
                disc,
                llvm::ConstantInt::get(disc->getType(), *member_index));
          }
          else if constexpr (std::is_same_v<P, IRLiteralPattern>)
          {
            if (!subject)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *lit = nullptr;
            if (pat.literal.kind == IRLiteralKind::Bool)
            {
              lit = llvm::ConstantInt::get(
                  llvm::Type::getInt1Ty(emitter.GetContext()),
                  pat.literal.lexeme == "true" ? 1 : 0);
            }
            else if (pat.literal.kind == IRLiteralKind::Int)
            {
              if (auto parsed = parse_int_literal(pat.literal.lexeme))
              {
                if (subject->getType()->isIntegerTy())
                {
                  lit = llvm::ConstantInt::get(
                      subject->getType(),
                      static_cast<uint64_t>(*parsed),
                      true);
                }
              }
            }
            if (!lit)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            return EmitTypedEq(&builder, subject, lit);
          }
          else if constexpr (std::is_same_v<P, IRRangePattern>)
          {
            if (!subject || !subject->getType()->isIntegerTy())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            auto parse_const_pattern_int =
                [&](const IRPatternPtr &bound) -> std::optional<long long>
            {
              if (!bound)
              {
                return std::nullopt;
              }
              const auto *lit = std::get_if<IRLiteralPattern>(&bound->node);
              if (!lit || lit->literal.kind != IRLiteralKind::Int)
              {
                return std::nullopt;
              }
              return parse_int_literal(lit->literal.lexeme);
            };

            const std::optional<long long> lo = parse_const_pattern_int(pat.lo);
            const std::optional<long long> hi = parse_const_pattern_int(pat.hi);
            if (!lo.has_value() || !hi.has_value())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            const bool is_signed = subject_type ? IsSignedIntegerType(subject_type) : true;
            llvm::Value *lo_value = llvm::ConstantInt::get(
                subject->getType(),
                static_cast<std::uint64_t>(*lo),
                true);
            llvm::Value *hi_value = llvm::ConstantInt::get(
                subject->getType(),
                static_cast<std::uint64_t>(*hi),
                true);

            llvm::Value *lower_ok = is_signed
                                        ? builder.CreateICmpSGE(subject, lo_value)
                                        : builder.CreateICmpUGE(subject, lo_value);

            llvm::Value *upper_ok = nullptr;
            if (pat.kind == IRRangeKind::Inclusive)
            {
              upper_ok = is_signed
                             ? builder.CreateICmpSLE(subject, hi_value)
                             : builder.CreateICmpULE(subject, hi_value);
            }
            else if (pat.kind == IRRangeKind::Exclusive)
            {
              upper_ok = is_signed
                             ? builder.CreateICmpSLT(subject, hi_value)
                             : builder.CreateICmpULT(subject, hi_value);
            }
            else
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            return builder.CreateAnd(lower_ok, upper_ok);
          }
          else if constexpr (std::is_same_v<P, IRTuplePattern>)
          {
            if (!subject_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            const analysis::TypeRef stripped_subject =
                normalize_match_type(subject_type);
            if (pat.elements.empty())
            {
              if (!stripped_subject)
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              if (const auto *prim =
                      std::get_if<analysis::TypePrim>(&stripped_subject->node))
              {
                return prim->name == "()"
                           ? llvm::ConstantInt::getTrue(emitter.GetContext())
                           : llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              const auto *empty_tuple =
                  std::get_if<analysis::TypeTuple>(&stripped_subject->node);
              return empty_tuple && empty_tuple->elements.empty()
                         ? llvm::ConstantInt::getTrue(emitter.GetContext())
                         : llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            if (!subject)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            const auto *tuple_type =
                stripped_subject
                    ? std::get_if<analysis::TypeTuple>(&stripped_subject->node)
                    : nullptr;
            if (!tuple_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            if (tuple_type->elements.size() < pat.elements.size())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            auto *tuple_struct = llvm::dyn_cast<llvm::StructType>(subject->getType());
            std::optional<::ultraviolet::analysis::layout::RecordLayout> tuple_layout;
            if (is_storage_subject(subject))
            {
              tuple_layout =
                  ::ultraviolet::analysis::layout::TupleLayoutOf(
                      scope,
                      tuple_type->elements);
              if (!tuple_layout.has_value() ||
                  tuple_layout->offsets.size() < pat.elements.size())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
            }
            else if (!tuple_struct ||
                     tuple_struct->getNumElements() < pat.elements.size())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *tuple_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            for (std::size_t i = 0; i < pat.elements.size(); ++i)
            {
              llvm::Value *elem_value = nullptr;
              if (is_storage_subject(subject))
              {
                llvm::Type *elem_ty = emitter.GetLLVMType(tuple_type->elements[i]);
                elem_value = field_from_storage_offset(
                    subject,
                    tuple_layout->offsets[i],
                    elem_ty,
                    elem_ty,
                    false);
              }
              else
              {
                elem_value =
                    builder.CreateExtractValue(subject, {static_cast<unsigned>(i)});
              }
              llvm::Value *elem_ok = emit_pattern_cond_for_value(
                  pat.elements[i], elem_value, tuple_type->elements[i]);
              tuple_ok = builder.CreateAnd(
                  AsBool(&builder, tuple_ok),
                  AsBool(&builder, elem_ok));
            }
            return tuple_ok;
          }
          else if constexpr (std::is_same_v<P, IRRecordPattern>)
          {
            if (!subject || !subject_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            const analysis::TypeRef stripped_subject =
                normalize_match_type(subject_type);
            const auto *path_type =
                stripped_subject
                    ? std::get_if<analysis::TypePathType>(&stripped_subject->node)
                    : nullptr;
            if (!path_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            auto same_path = [](const analysis::TypePath &lhs,
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
            if (!same_path(path_type->path, pat.path))
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            auto *record_struct = llvm::dyn_cast<llvm::StructType>(subject->getType());
            if (!record_struct && !is_storage_subject(subject))
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *record_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            for (const auto &field : pat.fields)
            {
              if (!field.pattern)
              {
                continue;
              }
              const auto field_meta =
                  ResolveFieldAccessMeta(scope, stripped_subject, field.name);
              if (!field_meta.has_value() ||
                  (!is_storage_subject(subject) &&
                   field_meta->index >= record_struct->getNumElements()))
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              llvm::Value *field_value = nullptr;
              if (is_storage_subject(subject))
              {
                const auto layout = ComputeLayoutLLVMRecord(
                    emitter,
                    scope,
                    field_meta->aggregate_type,
                    field_meta->aggregate_fields,
                    field_meta->layout_options);
                if (!layout.has_value() ||
                    field_meta->index >= layout->fields.size())
                {
                  return llvm::ConstantInt::getFalse(emitter.GetContext());
                }
                const LayoutLLVMField &layout_field =
                    layout->fields[field_meta->index];
                llvm::Type *field_ty = emitter.GetLLVMType(field_meta->field_type);
                if (!field_ty || field_ty->isVoidTy())
                {
                  field_ty = layout_field.llvm_type;
                }
                field_value = field_from_storage_offset(
                    subject,
                    layout_field.offset,
                    layout_field.llvm_type ? layout_field.llvm_type : field_ty,
                    field_ty,
                    layout_field.recursive_indirect);
              }
              else
              {
                field_value = builder.CreateExtractValue(
                    subject,
                    {static_cast<unsigned>(field_meta->index)});
              }
              llvm::Value *field_ok = emit_pattern_cond_for_value(
                  field.pattern, field_value, field_meta->field_type);
              record_ok = builder.CreateAnd(
                  AsBool(&builder, record_ok),
                  AsBool(&builder, field_ok));
            }
            return record_ok;
          }
          else if constexpr (std::is_same_v<P, IREnumPattern>)
          {
            const ast::EnumDecl *enum_decl = nullptr;
            analysis::TypePath enum_path;
            if (!pat.path.empty())
            {
              enum_decl = analysis::LookupEnumDecl(scope, pat.path);
              if (!enum_decl && !scope.current_module.empty() && pat.path.size() == 1u)
              {
                analysis::TypePath qualified = scope.current_module;
                qualified.insert(qualified.end(), pat.path.begin(), pat.path.end());
                enum_decl = analysis::LookupEnumDecl(scope, qualified);
                if (enum_decl)
                {
                  enum_path = qualified;
                }
              }
              else if (enum_decl)
              {
                enum_path = pat.path;
              }
            }
            if (!enum_decl)
            {
              enum_decl = lookup_enum_decl(subject_type, &enum_path);
            }
            if (!enum_decl || !subject)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            const std::vector<analysis::TypeRef> subject_enum_args =
                enum_generic_args_for_type(subject_type);
            const auto expected_disc = variant_disc(*enum_decl, pat.name);
            llvm::Value *actual_disc = enum_disc_value(subject, subject_type);
            if (!expected_disc.has_value() || !actual_disc)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *disc_eq = EmitTypedEq(
                &builder,
                actual_disc,
                llvm::ConstantInt::get(actual_disc->getType(), *expected_disc));
            if (!pat.payload.has_value())
            {
              return disc_eq;
            }
            const ast::VariantDecl *variant = find_variant(*enum_decl, pat.name);
            if (!variant)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *payload_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            std::visit(
                [&](const auto &payload_pattern)
                {
                  using PayloadP = std::decay_t<decltype(payload_pattern)>;
                  if constexpr (std::is_same_v<PayloadP, IRTuplePayloadPattern>)
                  {
                    for (std::size_t i = 0; i < payload_pattern.elements.size(); ++i)
                    {
                      const auto member = payload_member_by_index(
                          *enum_decl,
                          *variant,
                          subject_enum_args,
                          i);
                      llvm::Value *member_val =
                          load_enum_payload_member(subject, subject_type, member);
                      llvm::Value *member_ok = emit_pattern_cond_for_value(
                          payload_pattern.elements[i],
                          member_val,
                          member.type);
                      payload_ok = builder.CreateAnd(
                          AsBool(&builder, payload_ok),
                          AsBool(&builder, member_ok));
                    }
                  }
                  else
                  {
                    for (const auto &field : payload_pattern.fields)
                    {
                      if (!field.pattern)
                      {
                        continue;
                      }
                      const auto member =
                          payload_member_by_field(
                              *enum_decl,
                              *variant,
                              subject_enum_args,
                              field.name);
                      llvm::Value *member_val =
                          load_enum_payload_member(subject, subject_type, member);
                      llvm::Value *member_ok = emit_pattern_cond_for_value(
                          field.pattern,
                          member_val,
                          member.type);
                      payload_ok = builder.CreateAnd(
                          AsBool(&builder, payload_ok),
                          AsBool(&builder, member_ok));
                    }
                  }
                },
                *pat.payload);
            return builder.CreateAnd(
                AsBool(&builder, disc_eq),
                AsBool(&builder, payload_ok));
          }
          else if constexpr (std::is_same_v<P, IRModalPattern>)
          {
            if (!subject)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            analysis::TypeRef stripped_subject = normalize_match_type(subject_type);
            const auto *subject_union =
                stripped_subject ? std::get_if<analysis::TypeUnion>(&stripped_subject->node)
                                 : nullptr;
            if (subject_union)
            {
              const auto union_layout =
                  ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *subject_union);
              if (!union_layout.has_value())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }

              std::optional<std::size_t> modal_member_index;
              std::optional<analysis::TypeModalState> modal_member_state;
              for (std::size_t i = 0; i < union_layout->member_list.size(); ++i)
              {
                analysis::TypeRef member_type =
                    normalize_match_type(union_layout->member_list[i]);
                const auto *state_type =
                    member_type ? std::get_if<analysis::TypeModalState>(&member_type->node)
                                : nullptr;
                if (state_type && analysis::IdEq(state_type->state, pat.state))
                {
                  modal_member_index = i;
                  modal_member_state = *state_type;
                  break;
                }
              }
              if (!modal_member_index.has_value() || !modal_member_state.has_value())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }

              llvm::Value *state_ok = llvm::ConstantInt::getFalse(emitter.GetContext());
              if (union_layout->niche)
              {
                std::optional<std::size_t> payload_index;
                for (std::size_t i = 0; i < union_layout->member_list.size(); ++i)
                {
                  const auto member_size =
                      ::ultraviolet::analysis::layout::SizeOf(scope, union_layout->member_list[i]);
                  if (member_size.has_value() && *member_size != 0)
                  {
                    payload_index = i;
                    break;
                  }
                }
                if (payload_index.has_value())
                {
                  llvm::Value *disc = union_disc_value(subject, subject_type);
                  if (disc && disc->getType()->isPointerTy())
                  {
                    llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(disc->getType()));
                    state_ok = (*modal_member_index == *payload_index)
                                   ? builder.CreateICmpNE(disc, null_ptr)
                                   : builder.CreateICmpEQ(disc, null_ptr);
                  }
                }
              }
              else
              {
                llvm::Value *disc = union_disc_value(subject, subject_type);
                if (!disc || !disc->getType()->isIntegerTy())
                {
                  return llvm::ConstantInt::getFalse(emitter.GetContext());
                }
                state_ok = EmitTypedEq(
                    &builder,
                    disc,
                    llvm::ConstantInt::get(disc->getType(), *modal_member_index));
              }

              llvm::Value *payload_ok =
                  llvm::ConstantInt::getTrue(emitter.GetContext());
              if (pat.fields.has_value())
              {
                const ast::ModalDecl *modal_decl =
                    analysis::LookupModalDecl(scope, modal_member_state->path);
                if (!modal_decl)
                {
                  return llvm::ConstantInt::getFalse(emitter.GetContext());
                }
                llvm::Value *modal_member_value =
                    load_union_payload_member(
                        subject,
                        subject_type,
                        *union_layout,
                        *modal_member_index);
                for (const auto &field : pat.fields->fields)
                {
                  if (!field.pattern)
                  {
                    continue;
                  }
                  ModalPayloadMemberInfo member =
                      modal_payload_member_by_field(*modal_decl,
                                                    modal_member_state->path,
                                                    modal_member_state->generic_args,
                                                    pat.state,
                                                    field.name);
                  member.tagged = false;
                  llvm::Value *member_val =
                      load_modal_payload_member(
                          modal_member_value,
                          modal_member_state->path.empty()
                              ? nullptr
                              : analysis::MakeTypeModalState(
                                    modal_member_state->path,
                                    pat.state,
                                    modal_member_state->generic_args),
                          member);
                  llvm::Value *member_ok =
                      emit_pattern_cond_for_value(field.pattern, member_val, member.type);
                  payload_ok = builder.CreateAnd(
                      AsBool(&builder, payload_ok),
                      AsBool(&builder, member_ok));
                }
              }

              return builder.CreateAnd(
                  AsBool(&builder, state_ok),
                  AsBool(&builder, payload_ok));
            }

            analysis::TypePath modal_path;
            const ast::ModalDecl *modal_decl = lookup_modal_decl(subject_type, &modal_path);
            if (!modal_decl)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            const auto *subject_modal_state =
                stripped_subject
                    ? std::get_if<analysis::TypeModalState>(&stripped_subject->node)
                    : nullptr;
            const auto *subject_modal_path =
                stripped_subject ? analysis::AppliedTypePath(*stripped_subject)
                                 : nullptr;
            const auto *subject_modal_path_args =
                stripped_subject ? analysis::AppliedTypeArgs(*stripped_subject)
                                 : nullptr;
            std::vector<analysis::TypeRef> subject_modal_args;
            if (subject_modal_state)
            {
              subject_modal_args = subject_modal_state->generic_args;
            }
            else if (subject_modal_path && subject_modal_path_args)
            {
              subject_modal_args = *subject_modal_path_args;
            }
            const auto modal_layout = ::ultraviolet::analysis::layout::ModalLayoutOf(scope, *modal_decl, subject_modal_args);
            const bool subject_is_modal_state = (subject_modal_state != nullptr);
            const bool subject_is_async_modal_state =
                subject_modal_state && analysis::IsAsyncType(stripped_subject);
            llvm::Value *runtime_disc = modal_disc_value(subject, subject_type);

            llvm::Value *state_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            if (subject_modal_state)
            {
              if (!analysis::IdEq(subject_modal_state->state, pat.state))
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
            }
            else if ((modal_layout.has_value() && modal_layout->disc_type.has_value()) ||
                     runtime_disc != nullptr)
            {
              const auto expected_disc = modal_state_disc(*modal_decl, pat.state);
              if (!expected_disc.has_value() || !runtime_disc)
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              state_ok = EmitTypedEq(
                  &builder,
                  runtime_disc,
                  llvm::ConstantInt::get(runtime_disc->getType(), *expected_disc));
            }
            else
            {
              const auto payload_state = analysis::PayloadState(scope, *modal_decl);
              if (!payload_state.has_value() ||
                  !analysis::IdEq(std::string(*payload_state), pat.state))
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
            }

            llvm::Value *payload_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            if (pat.fields.has_value())
            {
              for (const auto &field : pat.fields->fields)
              {
                if (!field.pattern)
                {
                  continue;
                }
                auto member =
                    modal_payload_member_by_field(*modal_decl,
                                                  modal_path,
                                                  subject_modal_args,
                                                  pat.state,
                                                  field.name);
                if (subject_is_modal_state && !subject_is_async_modal_state)
                {
                  member.tagged = false;
                }
                llvm::Value *member_val =
                    load_modal_payload_member(subject, subject_type, member);
                llvm::Value *member_ok = emit_pattern_cond_for_value(
                    field.pattern,
                    member_val,
                    member.type);
                payload_ok = builder.CreateAnd(
                    AsBool(&builder, payload_ok),
                    AsBool(&builder, member_ok));
              }
            }

            return builder.CreateAnd(
                AsBool(&builder, state_ok),
                AsBool(&builder, payload_ok));
          }
          else
          {
            // Conservative fallback for patterns without direct predicate IR.
            return llvm::ConstantInt::getTrue(emitter.GetContext());
          }
        },
        pattern->node);
  };

  auto emit_pattern_cond = [&](const IRPatternPtr &pattern) -> llvm::Value *
  {
    analysis::TypeRef scrut_type = if_case.scrutinee_type;
    if (!scrut_type && ctx)
    {
      scrut_type = ctx->LookupValueType(if_case.scrutinee);
    }
    return emit_pattern_cond_for_value(pattern, match_subject, scrut_type);
  };

  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "ifcase.merge", func);
  struct IncomingValue
  {
    llvm::BasicBlock *pred = nullptr;
    llvm::Value *value = nullptr;
    llvm::Value *storage = nullptr;
    analysis::TypeRef source_type;
  };
  std::vector<IncomingValue> incoming;
  bool has_fallthrough_arm = false;
  const LLVMEmitter::FlowStateSnapshot branch_state =
      emitter.SaveFlowState();

  analysis::TypeRef result_type = LookupValueType(if_case.result);
  if (!result_type)
  {
    for (const auto &arm : if_case.arms)
    {
      if (arm.value_type)
      {
        result_type = arm.value_type;
        break;
      }
    }
  }
  llvm::Type *result_ty = ExpectedLLVMType(if_case.result);
  if (!result_ty && result_type)
  {
    result_ty = emitter.GetLLVMType(result_type);
  }
  const bool aggregate_result =
      result_ty && !result_ty->isVoidTy() &&
      IsAddressBackedAggregateType(result_ty);
  llvm::Value *merged_storage = nullptr;
  if (aggregate_result)
  {
    merged_storage =
        emitter.AcquireReusableAggregateStorage(func, result_ty, "ifcase.result");
    llvm::Type *expected_ptr_ty = llvm::PointerType::get(result_ty, 0);
    if (merged_storage && merged_storage->getType() != expected_ptr_ty)
    {
      merged_storage = builder.CreateBitCast(merged_storage, expected_ptr_ty);
    }
  }

  auto store_aggregate_result = [&](const IRValue &arm_ir,
                                    llvm::Value *arm_storage,
                                    llvm::Value *arm_value,
                                    const analysis::TypeRef &arm_type) -> bool
  {
    if (!merged_storage || !result_ty)
    {
      return false;
    }
    analysis::TypeRef target_type = result_type ? result_type : arm_type;
    if (arm_storage && arm_storage->getType()->isPointerTy())
    {
      if (TryEmitBitcopyAggregateStorageCopy(
              emitter,
              &builder,
              merged_storage,
              arm_storage,
              target_type,
              arm_type))
      {
        return true;
      }
      if (TryEmitAggregateStorageTransfer(
              emitter,
              &builder,
              merged_storage,
              arm_storage,
              target_type,
              arm_type))
      {
        return true;
      }
    }
    if (target_type &&
        StoreIRValueToStorage(
            emitter,
            &builder,
            merged_storage,
            arm_ir,
            target_type,
            result_ty,
            1))
    {
      return true;
    }
    if (target_type &&
        TryEmitDerivedAggregateToStorage(
            emitter,
            &builder,
            merged_storage,
            arm_ir,
            target_type))
    {
      return true;
    }
    if (!arm_value)
    {
      return false;
    }
    llvm::Value *candidate = arm_value;
    if (!candidate)
    {
      return false;
    }
    llvm::Value *coerced = CoerceToTyped(
        emitter,
        &builder,
        candidate,
        result_ty,
        arm_type,
        result_type);
    if (!coerced)
    {
      coerced = CoerceTo(&builder, candidate, result_ty);
    }
    if (!coerced)
    {
      coerced = llvm::Constant::getNullValue(result_ty);
    }
    builder.CreateStore(coerced, merged_storage);
    return true;
  };

  for (std::size_t i = 0; i < if_case.arms.size(); ++i)
  {
    const IRIfCaseClause &arm = if_case.arms[i];
    const bool is_last = (i + 1 == if_case.arms.size());

    llvm::BasicBlock *arm_bb =
        llvm::BasicBlock::Create(emitter.GetContext(), "ifcase.case", func);
    llvm::BasicBlock *next_bb = is_last
                                    ? nullptr
                                    : llvm::BasicBlock::Create(emitter.GetContext(), "ifcase.next", func);

    emitter.RestoreFlowState(branch_state);
    llvm::Value *cond = AsBool(&builder, emit_pattern_cond(arm.pattern));
    if (is_last)
    {
      llvm::BasicBlock *unmatched_bb =
          llvm::BasicBlock::Create(emitter.GetContext(), "ifcase.unmatched", func);
      builder.CreateCondBr(cond, arm_bb, unmatched_bb);
      builder.SetInsertPoint(unmatched_bb);
      // Spec EvalIfCases-None / §17.5.6: a full fall-through panics with
      // MatchFail; it MUST NOT be undefined behavior.
      StorePanicRecord(emitter, &builder, PanicCode(PanicReason::MatchFail));
      EmitReturn(emitter, &builder);
      if (!builder.GetInsertBlock()->getTerminator())
      {
        builder.CreateUnreachable();
      }
    }
    else
    {
      builder.CreateCondBr(cond, arm_bb, next_bb);
    }

    builder.SetInsertPoint(arm_bb);
    emitter.RestoreFlowState(branch_state);
    emitter.EmitIR(arm.body);
    llvm::BasicBlock *value_bb = builder.GetInsertBlock();
    if (!value_bb->getTerminator())
    {
      llvm::Value *arm_storage = emitter.GetAddressableStorage(arm.value);
      llvm::Value *arm_value = nullptr;
      analysis::TypeRef arm_type =
          arm.value_type ? arm.value_type : LookupValueType(arm.value);
      if (aggregate_result && merged_storage)
      {
        if (!store_aggregate_result(arm.value, arm_storage, nullptr, arm_type))
        {
          arm_value = EvaluateOrDefault(arm.value);
          (void)store_aggregate_result(
              arm.value,
              arm_storage,
              arm_value,
              arm_type);
        }
      }
      else
      {
        arm_value = EvaluateOrDefault(arm.value);
      }
      if (arm.cleanup_ir)
      {
        emitter.EmitIR(arm.cleanup_ir);
      }
      llvm::BasicBlock *arm_end = builder.GetInsertBlock();
      if (!arm_end->getTerminator())
      {
        has_fallthrough_arm = true;
        builder.CreateBr(merge_bb);
        if (!aggregate_result || !merged_storage)
        {
          incoming.push_back({arm_end, arm_value, arm_storage, arm_type});
        }
      }
    }

    if (!is_last)
    {
      builder.SetInsertPoint(next_bb);
    }
  }

  builder.SetInsertPoint(merge_bb);
  emitter.RestoreFlowState(branch_state);
  if (!result_ty)
  {
    if (!incoming.empty() && incoming.front().value)
    {
      result_ty = incoming.front().value->getType();
    }
    else
    {
      result_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    }
  }
  if (!has_fallthrough_arm)
  {
    if (!merge_bb->getTerminator())
    {
      builder.CreateUnreachable();
    }
    return;
  }
  if (!result_ty || result_ty->isVoidTy() ||
      IsZeroSizedLLVMType(emitter, result_ty))
  {
    emitter.SetTempValue(if_case.result, DefaultFor(if_case.result));
    return;
  }

  if (aggregate_result && merged_storage)
  {
    emitter.ForgetTempStorage(if_case.result);
    emitter.SetTempStorage(if_case.result, merged_storage);
    RecordIfCaseLoweringConformance(if_case.arms.size(), "aggregate-storage");
    return;
  }

  auto coerce_in_predecessor = [&](llvm::BasicBlock *pred,
                                   llvm::Value *value,
                                   const analysis::TypeRef &source_type) -> llvm::Value *
  {
    llvm::Value *candidate = value ? value : llvm::Constant::getNullValue(result_ty);
    if (!candidate)
    {
      return llvm::Constant::getNullValue(result_ty);
    }
    if (pred && pred->getTerminator())
    {
      llvm::IRBuilder<> pred_builder(pred->getTerminator());
      llvm::Value *coerced = CoerceToTyped(
          emitter,
          &pred_builder,
          candidate,
          result_ty,
          source_type,
          result_type);
      if (!coerced)
      {
        coerced = CoerceTo(&pred_builder, candidate, result_ty);
      }
      return coerced ? coerced : llvm::Constant::getNullValue(result_ty);
    }
    llvm::Value *coerced = CoerceToTyped(
        emitter,
        &builder,
        candidate,
        result_ty,
        source_type,
        result_type);
    if (!coerced)
    {
      coerced = CoerceTo(&builder, candidate, result_ty);
    }
    return coerced ? coerced : llvm::Constant::getNullValue(result_ty);
  };

  if (incoming.size() == 1)
  {
    emitter.SetTempValue(
        if_case.result,
        coerce_in_predecessor(incoming.front().pred,
                              incoming.front().value,
                              incoming.front().source_type));
    RecordIfCaseLoweringConformance(if_case.arms.size(), "single-value");
    return;
  }

  llvm::PHINode *phi = builder.CreatePHI(result_ty, incoming.size(), "ifcase.result");
  for (const auto &entry : incoming)
  {
    phi->addIncoming(coerce_in_predecessor(entry.pred,
                                           entry.value,
                                           entry.source_type),
                     entry.pred);
  }
  emitter.SetTempValue(if_case.result, phi);
  RecordIfCaseLoweringConformance(if_case.arms.size(), "phi-value");
}

} // namespace ultraviolet::codegen::emit_detail
