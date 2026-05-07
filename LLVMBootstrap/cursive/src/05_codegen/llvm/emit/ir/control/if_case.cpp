// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/if_case.cpp
// Canonical owner for LLVM IR if-case instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRIfCase &if_case) const
{
  llvm::Function *func = builder.GetInsertBlock()->getParent();
  if (!func)
  {
    emitter.SetTempValue(if_case.result, DefaultFor(if_case.result));
    return;
  }

  llvm::Value *scrutinee = EvaluateOrDefault(if_case.scrutinee);
  if (!scrutinee || if_case.arms.empty())
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

  const LowerCtx *ctx = emitter.GetCurrentCtx();
  const analysis::ScopeContext &scope = BuildScope(ctx);

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
    const auto member = ::cursive::analysis::layout::EnumTuplePayloadMemberLayout(
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
    const auto member = ::cursive::analysis::layout::EnumRecordPayloadMemberLayout(
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

  auto enum_disc_value = [&](llvm::Value *enum_value) -> llvm::Value *
  {
    if (!enum_value)
    {
      return nullptr;
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
      return nullptr;
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
    llvm::Value *field_ptr = builder.CreateBitCast(
        field_i8,
        llvm::PointerType::get(member_ty, 0));
    llvm::LoadInst *load = builder.CreateLoad(member_ty, field_ptr);
    load->setAlignment(llvm::Align(1));
    return load;
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
    std::uint64_t offset = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_align = 1;
    bool tagged = true;
    bool ok = false;
  };

  auto modal_payload_member_by_field = [&](const ast::ModalDecl &modal_decl,
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

    const auto modal_layout = ::cursive::analysis::layout::ModalLayoutOf(scope, modal_decl, modal_args);
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
      const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, field->type);
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

    const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope, field_types);
    if (!layout.has_value())
    {
      return out;
    }
    for (std::size_t i = 0; i < field_names.size() && i < layout->offsets.size(); ++i)
    {
      if (analysis::IdEq(field_names[i], std::string(field_name)))
      {
        out.type = field_types[i];
        out.offset = layout->offsets[i];
        out.ok = true;
        break;
      }
    }
    return out;
  };

  auto modal_disc_value = [&](llvm::Value *modal_value) -> llvm::Value *
  {
    if (!modal_value)
    {
      return nullptr;
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
                                       const ModalPayloadMemberInfo &member) -> llvm::Value *
  {
    if (!modal_value || !member.ok || !member.type)
    {
      return nullptr;
    }
    llvm::Type *member_ty = emitter.GetLLVMType(member.type);
    if (!member_ty)
    {
      return nullptr;
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
    llvm::AllocaInst *modal_slot = entry_builder.CreateAlloca(modal_value->getType());
    builder.CreateStore(modal_value, modal_slot);

    llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    llvm::Value *payload_i8 = nullptr;
    if (member.tagged)
    {
      auto *modal_ty = llvm::dyn_cast<llvm::StructType>(modal_value->getType());
      if (!modal_ty || modal_ty->getNumElements() < 2)
      {
        const auto member_size = ::cursive::analysis::layout::SizeOf(scope, member.type);
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
    llvm::Value *field_ptr = builder.CreateBitCast(
        field_i8,
        llvm::PointerType::get(member_ty, 0));
    llvm::LoadInst *load = builder.CreateLoad(member_ty, field_ptr);
    load->setAlignment(llvm::Align(1));
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
                llvm::Value *disc = modal_disc_value(subject);
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
            std::optional<::cursive::analysis::layout::UnionLayout> union_layout = ::cursive::analysis::layout::UnionLayoutOf(scope, *uni);
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
              // Cursive0 niche layout is currently used for pointer-valid payload unions.
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

              llvm::Constant *niche_zero = llvm::Constant::getNullValue(subject->getType());
              if (*member_index == *payload_index)
              {
                return builder.CreateICmpNE(subject, niche_zero);
              }
              return builder.CreateICmpEQ(subject, niche_zero);
            }

            auto *subject_struct = llvm::dyn_cast<llvm::StructType>(subject->getType());
            if (!subject_struct || subject_struct->getNumElements() < 1)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *disc = builder.CreateExtractValue(subject, {0u});
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
            if (!subject || !subject_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            const analysis::TypeRef stripped_subject =
                normalize_match_type(subject_type);
            const auto *tuple_type =
                stripped_subject
                    ? std::get_if<analysis::TypeTuple>(&stripped_subject->node)
                    : nullptr;
            if (!tuple_type)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            auto *tuple_struct = llvm::dyn_cast<llvm::StructType>(subject->getType());
            if (!tuple_struct ||
                tuple_struct->getNumElements() < pat.elements.size() ||
                tuple_type->elements.size() < pat.elements.size())
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            llvm::Value *tuple_ok = llvm::ConstantInt::getTrue(emitter.GetContext());
            for (std::size_t i = 0; i < pat.elements.size(); ++i)
            {
              llvm::Value *elem_value =
                  builder.CreateExtractValue(subject, {static_cast<unsigned>(i)});
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
            if (!record_struct)
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
                  field_meta->index >= record_struct->getNumElements())
              {
                return llvm::ConstantInt::getFalse(emitter.GetContext());
              }
              llvm::Value *field_value = builder.CreateExtractValue(
                  subject, {static_cast<unsigned>(field_meta->index)});
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
            llvm::Value *actual_disc = enum_disc_value(subject);
            if (!expected_disc.has_value() || !actual_disc)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }
            if (core::IsDebugEnabled("obj") &&
                DebugTargetEnumPath(enum_path.empty() ? pat.path : enum_path))
            {
              std::cerr << "[ifcase-enum] path="
                        << core::StringOfPath(enum_path.empty() ? pat.path : enum_path)
                        << " variant=" << pat.name
                        << " expected="
                        << static_cast<unsigned long long>(*expected_disc)
                        << " subject_type="
                        << (subject_type ? analysis::TypeToString(subject_type) : "<null>")
                        << " actual_disc=" << LLVMValueRepr(actual_disc)
                        << " subject=" << LLVMValueRepr(subject)
                        << "\n";
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
                      llvm::Value *member_val = load_enum_payload_member(subject, member);
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
                      llvm::Value *member_val = load_enum_payload_member(subject, member);
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
            analysis::TypePath modal_path;
            const ast::ModalDecl *modal_decl = lookup_modal_decl(subject_type, &modal_path);
            if (!modal_decl)
            {
              return llvm::ConstantInt::getFalse(emitter.GetContext());
            }

            analysis::TypeRef stripped_subject = normalize_match_type(subject_type);
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
            const auto modal_layout = ::cursive::analysis::layout::ModalLayoutOf(scope, *modal_decl, subject_modal_args);
            const bool subject_is_modal_state = (subject_modal_state != nullptr);
            const bool subject_is_async_modal_state =
                subject_modal_state && analysis::IsAsyncType(stripped_subject);
            llvm::Value *runtime_disc = modal_disc_value(subject);

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
                                                  subject_modal_args,
                                                  pat.state,
                                                  field.name);
                if (subject_is_modal_state && !subject_is_async_modal_state)
                {
                  member.tagged = false;
                }
                llvm::Value *member_val = load_modal_payload_member(subject, member);
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
    return emit_pattern_cond_for_value(pattern, scrutinee, scrut_type);
  };

  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "ifcase.merge", func);
  struct IncomingValue
  {
    llvm::BasicBlock *pred = nullptr;
    llvm::Value *value = nullptr;
    llvm::Value *storage = nullptr;
  };
  std::vector<IncomingValue> incoming;
  llvm::BasicBlock *fallback_pred = nullptr;
  const LLVMEmitter::FlowStateSnapshot branch_state =
      emitter.SaveFlowState();

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
      fallback_pred = builder.GetInsertBlock();
      builder.CreateCondBr(cond, arm_bb, merge_bb);
    }
    else
    {
      builder.CreateCondBr(cond, arm_bb, next_bb);
    }

    builder.SetInsertPoint(arm_bb);
    emitter.RestoreFlowState(branch_state);
  emitter.EmitIR(arm.body);
  llvm::BasicBlock *arm_end = builder.GetInsertBlock();
  if (!arm_end->getTerminator())
  {
    llvm::Value *arm_storage = emitter.GetAddressableStorage(arm.value);
    llvm::Value *arm_value = EvaluateOrDefault(arm.value);
    builder.CreateBr(merge_bb);
    incoming.push_back({arm_end, arm_value, arm_storage});
  }

    if (!is_last)
    {
      builder.SetInsertPoint(next_bb);
    }
  }

  builder.SetInsertPoint(merge_bb);
  emitter.RestoreFlowState(branch_state);
  analysis::TypeRef result_type = LookupValueType(if_case.result);
  llvm::Type *result_ty = ExpectedLLVMType(if_case.result);
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
  if (!result_ty || result_ty->isVoidTy())
  {
    return;
  }

  if (fallback_pred)
  {
    incoming.push_back({fallback_pred, llvm::Constant::getNullValue(result_ty), nullptr});
  }

  const bool aggregate_result = IsAddressBackedAggregateType(result_ty);

  if (aggregate_result)
  {
    llvm::Value *merged_storage =
        emitter.AcquireReusableAggregateStorage(func, result_ty, "ifcase.result");
    llvm::Type *expected_ptr_ty = llvm::PointerType::get(result_ty, 0);
    if (merged_storage && merged_storage->getType() != expected_ptr_ty)
    {
      merged_storage = builder.CreateBitCast(merged_storage, expected_ptr_ty);
    }
    if (merged_storage)
    {
      for (const auto &entry : incoming)
      {
        llvm::IRBuilder<> pred_builder(entry.pred->getTerminator());
        llvm::Value *candidate = nullptr;
        if (entry.storage && entry.storage->getType()->isPointerTy())
        {
          llvm::Value *typed_storage = entry.storage;
          if (typed_storage->getType() != expected_ptr_ty)
          {
            typed_storage = pred_builder.CreateBitCast(typed_storage, expected_ptr_ty);
          }
          candidate = pred_builder.CreateLoad(result_ty, typed_storage);
        }
        if (!candidate)
        {
          candidate = entry.value ? entry.value : llvm::Constant::getNullValue(result_ty);
        }
        llvm::Value *coerced = CoerceToTyped(
            emitter,
            &pred_builder,
            candidate,
            result_ty,
            nullptr,
            result_type);
        if (!coerced)
        {
          coerced = CoerceTo(&pred_builder, candidate, result_ty);
        }
        if (!coerced)
        {
          coerced = llvm::Constant::getNullValue(result_ty);
        }
        pred_builder.CreateStore(coerced, merged_storage);
      }

      emitter.ForgetTempStorage(if_case.result);
      emitter.SetTempStorage(if_case.result, merged_storage);
      return;
    }
  }

  auto coerce_in_predecessor = [&](llvm::BasicBlock *pred, llvm::Value *value) -> llvm::Value *
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
          nullptr,
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
        nullptr,
        result_type);
    if (!coerced)
    {
      coerced = CoerceTo(&builder, candidate, result_ty);
    }
    return coerced ? coerced : llvm::Constant::getNullValue(result_ty);
  };

  if (incoming.empty())
  {
    emitter.SetTempValue(if_case.result, llvm::Constant::getNullValue(result_ty));
    return;
  }
  if (incoming.size() == 1)
  {
    emitter.SetTempValue(
        if_case.result,
        coerce_in_predecessor(incoming.front().pred, incoming.front().value));
    return;
  }

  llvm::PHINode *phi = builder.CreatePHI(result_ty, incoming.size(), "ifcase.result");
  for (const auto &entry : incoming)
  {
    phi->addIncoming(coerce_in_predecessor(entry.pred, entry.value), entry.pred);
  }
  emitter.SetTempValue(if_case.result, phi);
}

} // namespace cursive::codegen::emit_detail
