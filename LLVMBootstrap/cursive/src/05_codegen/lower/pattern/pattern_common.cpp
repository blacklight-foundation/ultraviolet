// =============================================================================
// Pattern Lowering Common Utilities Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.6 (Pattern Lowering)
//   - Lines 16757-16813: Pattern lowering judgments
//   - PatternLowerJudg = {LowerBindPattern, LowerBindList, LowerIfCases, TagOf}
//   - Lower-Pat-General: MatchPattern + BindOrder + LowerBindList
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_pat.cpp
//
// =============================================================================

#include "05_codegen/lower/pattern/pattern_common.h"

#include <algorithm>
#include <set>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_layout.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_bind.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

// =============================================================================
// §6.6 Immediate Values
// =============================================================================

static IRValue BoolImmediate(bool value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = value ? "true" : "false";
  v.bytes = {static_cast<std::uint8_t>(value ? 1 : 0)};
  return v;
}

// =============================================================================
// §6.6 Type Lookup Utilities
// =============================================================================

analysis::TypeRef LowerSyntaxType(const std::shared_ptr<ast::Type>& type,
                                  LowerCtx& ctx) {
  if (!type) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, type)) {
    return *lowered;
  }
  return nullptr;
}

analysis::TypeRef StripPermAndRefine(const analysis::TypeRef& type) {
  analysis::TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm = std::get_if<analysis::TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    if (const auto* refine =
            std::get_if<analysis::TypeRefine>(&stripped->node)) {
      stripped = refine->base;
      continue;
    }
    break;
  }
  return stripped;
}

std::vector<analysis::TypeRef> GenericArgsFromType(
    const analysis::TypeRef& type) {
  const analysis::TypeRef stripped = StripPermAndRefine(type);
  if (!stripped) {
    return {};
  }
  const std::vector<analysis::TypeRef>* args =
      analysis::AppliedTypeArgs(*stripped);
  if (!args) {
    return {};
  }
  return *args;
}

analysis::TypeRef InstantiateActiveGenericType(const analysis::TypeRef& type,
                                               const LowerCtx& ctx) {
  if (!type || !ctx.active_generic_type_subst.has_value() ||
      ctx.active_generic_type_subst->empty()) {
    return type;
  }
  return analysis::InstantiateType(type, *ctx.active_generic_type_subst);
}

analysis::TypeRef ResolvePatternAliasType(const analysis::TypeRef& type,
                                          LowerCtx& ctx,
                                          std::size_t depth) {
  analysis::TypeRef stripped = StripPermAndRefine(type);
  if (!stripped) {
    return type;
  }
  if (depth > 16 || !ctx.sigma) {
    return stripped;
  }

  const auto* path = std::get_if<analysis::TypePathType>(&stripped->node);
  if (!path) {
    return stripped;
  }

  ast::TypePath syntax_path;
  syntax_path.reserve(path->path.size());
  for (const auto& segment : path->path) {
    syntax_path.push_back(segment);
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma->types.end()) {
    return stripped;
  }

  const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!alias) {
    return stripped;
  }

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  const auto lowered =
      ::cursive::analysis::layout::LowerTypeForLayout(scope, alias->type);
  if (!lowered.has_value()) {
    return stripped;
  }

  analysis::TypeRef resolved = *lowered;
  if (alias->generic_params && !alias->generic_params->params.empty()) {
    if (path->generic_args.size() > alias->generic_params->params.size()) {
      return stripped;
    }
    analysis::TypeSubst subst =
        analysis::BuildSubstitution(alias->generic_params->params,
                                    path->generic_args);
    resolved = analysis::InstantiateType(resolved, subst);
  } else if (!path->generic_args.empty()) {
    return stripped;
  }

  return ResolvePatternAliasType(resolved, ctx, depth + 1);
}

bool TypeEquivForUnionMatch(analysis::TypeRef lhs, analysis::TypeRef rhs) {
  lhs = analysis::StripPerm(lhs);
  rhs = analysis::StripPerm(rhs);
  const auto equiv = analysis::TypeEquiv(lhs, rhs);
  return equiv.ok && equiv.equiv;
}

const ast::RecordDecl* LookupRecordDecl(const ast::TypePath& path,
                                        const LowerCtx& ctx) {
  if (!ctx.sigma) {
    return nullptr;
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(path));
  if (it == ctx.sigma->types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

const ast::EnumDecl* LookupEnumDecl(const ast::TypePath& path,
                                    const LowerCtx& ctx) {
  if (!ctx.sigma) {
    return nullptr;
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(path));
  if (it == ctx.sigma->types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

const ast::ModalDecl* LookupModalDecl(const analysis::TypePath& path,
                                      const LowerCtx& ctx) {
  if (!ctx.sigma) {
    return nullptr;
  }
  ast::TypePath syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& seg : path) {
    syntax_path.push_back(seg);
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma->types.end()) {
    return nullptr;
  }
  return std::get_if<ast::ModalDecl>(&it->second);
}

// =============================================================================
// §6.6 Record/Enum/Modal Field Type Lookup
// =============================================================================

analysis::TypeRef RecordFieldType(const ast::RecordDecl& decl,
                                  std::string_view name,
                                  LowerCtx& ctx) {
  for (const auto& member : decl.members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      if (analysis::IdEq(field->name, name)) {
        return LowerSyntaxType(field->type, ctx);
      }
    }
  }
  return nullptr;
}

const ast::VariantDecl* FindVariant(const ast::EnumDecl& decl,
                                    std::string_view name) {
  for (const auto& variant : decl.variants) {
    if (analysis::IdEq(variant.name, name)) {
      return &variant;
    }
  }
  return nullptr;
}

analysis::TypeRef EnumPayloadFieldType(const ast::EnumDecl& decl,
                                       const ast::VariantDecl& variant,
                                       const std::vector<analysis::TypeRef>& generic_args,
                                       std::string_view name,
                                       LowerCtx& ctx) {
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  const auto member = analysis::layout::EnumRecordPayloadMemberLayout(
      scope, decl, variant, generic_args, name);
  if (member.has_value()) {
    return member->type;
  }
  return nullptr;
}

analysis::TypeRef EnumPayloadTupleType(const ast::EnumDecl& decl,
                                       const ast::VariantDecl& variant,
                                       const std::vector<analysis::TypeRef>& generic_args,
                                       std::size_t index,
                                       LowerCtx& ctx) {
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  const auto member = analysis::layout::EnumTuplePayloadMemberLayout(
      scope, decl, variant, generic_args, index);
  if (member.has_value()) {
    return member->type;
  }
  return nullptr;
}

const ast::StateBlock* FindState(const ast::ModalDecl& decl,
                                 std::string_view name) {
  for (const auto& state : decl.states) {
    if (analysis::IdEq(state.name, name)) {
      return &state;
    }
  }
  return nullptr;
}

analysis::TypeRef ModalFieldType(const ast::ModalDecl& decl,
                                 const std::vector<analysis::TypeRef>& modal_args,
                                 std::string_view state_name,
                                 std::string_view field_name,
                                 LowerCtx& ctx) {
  analysis::TypeSubst subst;
  if (decl.generic_params && !decl.generic_params->params.empty()) {
    if (modal_args.size() > decl.generic_params->params.size()) {
      return nullptr;
    }
    subst = analysis::BuildSubstitution(
        decl.generic_params->params,
        modal_args);
  }
  const auto* state = FindState(decl, state_name);
  if (!state) {
    return nullptr;
  }
  for (const auto& member : state->members) {
    if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
      if (analysis::IdEq(field->name, field_name)) {
        analysis::TypeRef field_type = LowerSyntaxType(field->type, ctx);
        if (field_type && !subst.empty()) {
          field_type = analysis::InstantiateType(field_type, subst);
        }
        return field_type;
      }
    }
  }
  return nullptr;
}

// =============================================================================
// §6.6 Move State Merging
// =============================================================================

void MergeFailures(LowerCtx& base, const LowerCtx& branch) {
  if (branch.resolve_failed) {
    base.resolve_failed = true;
  }
  if (branch.codegen_failed) {
    base.codegen_failed = true;
  }
  for (const auto& name : branch.resolve_failures) {
    if (std::find(base.resolve_failures.begin(), base.resolve_failures.end(), name) ==
        base.resolve_failures.end()) {
      base.resolve_failures.push_back(name);
    }
  }
}

void MergeMoveStates(LowerCtx& base,
                     const std::vector<const LowerCtx*>& branches) {
  for (auto& [name, stack] : base.binding_states) {
    if (stack.empty()) {
      continue;
    }
    auto& state = stack.back();

    bool moved_any = state.is_moved;
    std::set<std::string> fields;
    if (!moved_any) {
      fields.insert(state.moved_fields.begin(), state.moved_fields.end());
    }

    for (const auto* branch : branches) {
      if (!branch) {
        continue;
      }
      const BindingState* bstate = branch->GetBindingState(name);
      if (!bstate) {
        continue;
      }
      if (bstate->is_moved) {
        moved_any = true;
      } else if (!moved_any) {
        fields.insert(bstate->moved_fields.begin(), bstate->moved_fields.end());
      }
    }

    if (moved_any) {
      state.is_moved = true;
      state.moved_fields.clear();
    } else {
      state.is_moved = false;
      state.moved_fields.assign(fields.begin(), fields.end());
    }
  }
}

// =============================================================================
// §6.6 Pattern Type Extraction
// =============================================================================

analysis::TypeRef TuplePatternElementType(const analysis::TypeRef& tuple_type,
                                          std::size_t index) {
  if (!tuple_type) {
    return nullptr;
  }
  analysis::TypeRef stripped = analysis::StripPerm(tuple_type);
  if (!stripped) {
    return nullptr;
  }
  if (const auto* tuple = std::get_if<analysis::TypeTuple>(&stripped->node)) {
    if (index < tuple->elements.size()) {
      return tuple->elements[index];
    }
  }
  return nullptr;
}

std::vector<analysis::TypeRef> TuplePatternElementTypes(
    const analysis::TypeRef& tuple_type) {
  if (!tuple_type) {
    return {};
  }
  analysis::TypeRef stripped = analysis::StripPerm(tuple_type);
  if (!stripped) {
    return {};
  }
  if (const auto* tuple = std::get_if<analysis::TypeTuple>(&stripped->node)) {
    return tuple->elements;
  }
  return {};
}

std::optional<analysis::TypePath> TypePathOf(const analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* named = std::get_if<analysis::TypePathType>(&stripped->node)) {
    return named->path;
  }
  if (const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node)) {
    return modal->path;
  }
  return std::nullopt;
}

namespace {

struct OrderedPatternBinding {
  std::string name;
  IRValue value;
  analysis::TypeRef type_hint;
};

analysis::TypeRef LookupBindType(LowerCtx& ctx, const std::string& name) {
  if (const auto* state = ctx.GetBindingState(name)) {
    return state->type;
  }
  return nullptr;
}

analysis::ProvenanceKind LookupBindProv(LowerCtx& ctx, const std::string& name) {
  if (const auto* state = ctx.GetBindingState(name)) {
    return state->prov;
  }
  return analysis::ProvenanceKind::Bottom;
}

std::optional<std::string> LookupBindRegion(LowerCtx& ctx, const std::string& name) {
  if (const auto* state = ctx.GetBindingState(name)) {
    return state->prov_region;
  }
  return std::nullopt;
}

std::optional<std::string> LookupBindRegionTag(LowerCtx& ctx,
                                               const std::string& name) {
  if (const auto* state = ctx.GetBindingState(name)) {
    return state->prov_region_tag;
  }
  return std::nullopt;
}

IRPtr EmitOrderedPatternBindings(const std::vector<OrderedPatternBinding>& bindings,
                                 LowerCtx& ctx) {
  if (bindings.empty()) {
    return EmptyIR();
  }

  std::vector<IRPtr> emitted;
  emitted.reserve(bindings.size());
  for (const auto& binding_desc : bindings) {
    IRBindVar bind;
    bind.name = binding_desc.name;
    bind.stable_name = ctx.StableBindingName(binding_desc.name);
    bind.value = binding_desc.value;
    bind.type = LookupBindType(ctx, binding_desc.name);
    bind.type = InstantiateActiveGenericType(bind.type, ctx);
    if (!bind.type) {
      bind.type = InstantiateActiveGenericType(binding_desc.type_hint, ctx);
    }
    if (!bind.type) {
      bind.type = InstantiateActiveGenericType(
          ctx.LookupValueType(binding_desc.value), ctx);
    }
    bind.prov = LookupBindProv(ctx, binding_desc.name);
    bind.prov_region = LookupBindRegion(ctx, binding_desc.name);
    bind.prov_region_tag = LookupBindRegionTag(ctx, binding_desc.name);

    if (const DerivedValueInfo* derived = ctx.LookupDerivedValue(binding_desc.value)) {
      IRValue local_value;
      local_value.kind = IRValue::Kind::Local;
      local_value.name = binding_desc.name;
      ctx.RegisterDerivedValue(local_value, *derived);
    }

    emitted.push_back(MakeIR(std::move(bind)));
  }
  return SeqIR(std::move(emitted));
}

void MarkPatternScrutineeMoved(const IRValue& value, LowerCtx& ctx) {
  if (value.kind == IRValue::Kind::Local) {
    ctx.MarkMoved(value.name);
  }
}

void CollectPatternBindingsInOrder(const ast::Pattern& pattern,
                                   const IRValue& value,
                                   LowerCtx& ctx,
                                   std::vector<OrderedPatternBinding>& bindings) {
  std::visit(
      [&value, &ctx, &bindings](const auto& pat) {
        using T = std::decay_t<decltype(pat)>;

        if constexpr (std::is_same_v<T, ast::WildcardPattern> ||
                      std::is_same_v<T, ast::LiteralPattern> ||
                      std::is_same_v<T, ast::RangePattern>) {
          return;
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          bindings.push_back({pat.name, value, nullptr});
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (pat.name == "_") {
            return;
          }
          IRValue bind_value = value;
          analysis::TypeRef type_hint = nullptr;
          if (ctx.sigma) {
            type_hint =
                InstantiateActiveGenericType(LowerSyntaxType(pat.type, ctx), ctx);
            const auto base_type = ctx.LookupValueType(value);
            if (type_hint && base_type) {
              analysis::TypeRef stripped = base_type;
              if (const auto* perm = std::get_if<analysis::TypePerm>(&stripped->node)) {
                stripped = perm->base;
              }
              if (stripped && std::holds_alternative<analysis::TypeUnion>(stripped->node)) {
                const auto& uni = std::get<analysis::TypeUnion>(stripped->node);
                const analysis::ScopeContext& scope = ScopeForLowering(ctx);
                if (const auto layout = ::cursive::analysis::layout::UnionLayoutOf(scope, uni)) {
                  const auto& members = layout->member_list;
                  std::optional<std::size_t> member_index;
                  for (std::size_t i = 0; i < members.size(); ++i) {
                    if (TypeEquivForUnionMatch(type_hint, members[i])) {
                      member_index = i;
                      break;
                    }
                  }
                  if (member_index.has_value()) {
                    IRValue payload = ctx.FreshTempValue("pat_union_payload");
                    DerivedValueInfo info;
                    info.kind = DerivedValueInfo::Kind::UnionPayload;
                    info.base = value;
                    info.union_index = *member_index;
                    ctx.RegisterDerivedValue(payload, info);
                    if (value.kind == IRValue::Kind::Local) {
                      ctx.MarkMoved(value.name);
                    }
                    bind_value = payload;
                  }
                }
              }
            }
          }
          bindings.push_back({pat.name, bind_value, type_hint});
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          MarkPatternScrutineeMoved(value, ctx);
          for (std::size_t i = 0; i < pat.elements.size(); ++i) {
            IRValue elem = ctx.FreshTempValue("pat_tuple_elem");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::Tuple;
            info.base = value;
            info.tuple_index = i;
            ctx.RegisterDerivedValue(elem, info);
            CollectPatternBindingsInOrder(*pat.elements[i], elem, ctx, bindings);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          MarkPatternScrutineeMoved(value, ctx);
          for (const auto& field : pat.fields) {
            IRValue field_val = ctx.FreshTempValue("pat_field");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::Field;
            info.base = value;
            info.field = field.name;
            ctx.RegisterDerivedValue(field_val, info);
            if (field.pattern_opt) {
              CollectPatternBindingsInOrder(*field.pattern_opt, field_val, ctx, bindings);
            } else {
              bindings.push_back({field.name, field_val, nullptr});
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!pat.payload_opt) {
            return;
          }
          MarkPatternScrutineeMoved(value, ctx);
          const ast::EnumDecl* enum_decl = LookupEnumDecl(pat.path, ctx);
          const ast::VariantDecl* variant =
              enum_decl ? FindVariant(*enum_decl, pat.name) : nullptr;
          const std::vector<analysis::TypeRef> enum_generic_args =
              GenericArgsFromType(
                  InstantiateActiveGenericType(ctx.LookupValueType(value), ctx));
          std::visit(
              [&value, &ctx, &bindings, &pat, enum_decl, variant,
               &enum_generic_args](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (std::size_t i = 0; i < payload.elements.size(); ++i) {
                    analysis::TypeRef elem_type = nullptr;
                    if (enum_decl && variant) {
                      elem_type = EnumPayloadTupleType(
                          *enum_decl, *variant, enum_generic_args, i, ctx);
                    }
                    IRValue elem = ctx.FreshTempValue("pat_enum_payload_elem");
                    DerivedValueInfo info;
                    info.kind = DerivedValueInfo::Kind::EnumPayloadIndex;
                    info.base = value;
                    info.static_path = pat.path;
                    info.variant = pat.name;
                    info.tuple_index = i;
                    ctx.RegisterDerivedValue(elem, info);
                    if (elem_type) {
                      ctx.RegisterValueType(elem, elem_type);
                    }
                    CollectPatternBindingsInOrder(*payload.elements[i], elem, ctx, bindings);
                  }
                } else {
                  for (const auto& field : payload.fields) {
                    analysis::TypeRef field_type = nullptr;
                    if (enum_decl && variant) {
                      field_type = EnumPayloadFieldType(
                          *enum_decl, *variant, enum_generic_args, field.name, ctx);
                    }
                    IRValue field_val = ctx.FreshTempValue("pat_enum_payload_field");
                    DerivedValueInfo info;
                    info.kind = DerivedValueInfo::Kind::EnumPayloadField;
                    info.base = value;
                    info.static_path = pat.path;
                    info.variant = pat.name;
                    info.field = field.name;
                    ctx.RegisterDerivedValue(field_val, info);
                    if (field_type) {
                      ctx.RegisterValueType(field_val, field_type);
                    }
                    if (field.pattern_opt) {
                      CollectPatternBindingsInOrder(*field.pattern_opt, field_val, ctx,
                                                    bindings);
                    } else {
                      bindings.push_back({field.name, field_val, field_type});
                    }
                  }
                }
              },
              *pat.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!pat.fields_opt) {
            return;
          }
          MarkPatternScrutineeMoved(value, ctx);
          analysis::TypeRef modal_hint =
              ResolvePatternAliasType(InstantiateActiveGenericType(
                  ctx.LookupValueType(value), ctx), ctx);
          modal_hint = StripPermAndRefine(modal_hint);
          analysis::TypePath modal_path;
          std::vector<analysis::TypeRef> modal_args;
          IRValue modal_base = value;
          std::optional<std::size_t> matched_union_member_index;
          std::optional<analysis::TypeRef> matched_union_member_type;
          auto assign_modal_ref = [&](const analysis::TypeRef& type,
                                      bool require_state_match) -> bool {
            const analysis::TypeRef stripped = StripPermAndRefine(type);
            if (!stripped) {
              return false;
            }
            if (const auto* modal_state =
                    std::get_if<analysis::TypeModalState>(&stripped->node)) {
              if (require_state_match &&
                  !analysis::IdEq(modal_state->state, pat.state)) {
                return false;
              }
              modal_path = modal_state->path;
              modal_args = modal_state->generic_args;
              return true;
            }
            if (!require_state_match) {
              if (const auto* modal_ref =
                      std::get_if<analysis::TypePathType>(&stripped->node)) {
                modal_path = modal_ref->path;
                modal_args = modal_ref->generic_args;
                return true;
              }
            }
            return false;
          };
          if (!assign_modal_ref(modal_hint, false) && modal_hint &&
              std::holds_alternative<analysis::TypeUnion>(modal_hint->node)) {
            const auto& uni = std::get<analysis::TypeUnion>(modal_hint->node);
            const analysis::ScopeContext& scope = ScopeForLowering(ctx);
            std::vector<analysis::TypeRef> members = uni.members;
            if (const auto layout =
                    ::cursive::analysis::layout::UnionLayoutOf(scope, uni)) {
              members = layout->member_list;
            }
            std::optional<analysis::TypeRef> matched_member;
            std::optional<std::size_t> matched_member_index;
            bool matched_member_is_ambiguous = false;
            for (std::size_t i = 0; i < members.size(); ++i) {
              const auto& member = members[i];
              const analysis::TypeRef stripped = StripPermAndRefine(member);
              const auto* modal_state =
                  stripped
                      ? std::get_if<analysis::TypeModalState>(&stripped->node)
                      : nullptr;
              if (!modal_state || !analysis::IdEq(modal_state->state, pat.state)) {
                continue;
              }
              if (matched_member.has_value()) {
                matched_member_is_ambiguous = true;
                break;
              }
              matched_member = stripped;
              matched_member_index = i;
            }
            if (matched_member.has_value() && !matched_member_is_ambiguous) {
              assign_modal_ref(*matched_member, true);
              matched_union_member_index = matched_member_index;
              matched_union_member_type = *matched_member;
            }
          }
          if (matched_union_member_index.has_value() &&
              matched_union_member_type.has_value()) {
            modal_base = ctx.FreshTempValue("pat_modal_payload");
            DerivedValueInfo payload_info;
            payload_info.kind = DerivedValueInfo::Kind::UnionPayload;
            payload_info.base = value;
            payload_info.union_index = *matched_union_member_index;
            ctx.RegisterDerivedValue(modal_base, payload_info);
            ctx.RegisterValueType(modal_base, *matched_union_member_type);
          }
          const ast::ModalDecl* modal_decl =
              modal_path.empty() ? nullptr : LookupModalDecl(modal_path, ctx);
          for (const auto& field : pat.fields_opt->fields) {
            analysis::TypeRef field_type = nullptr;
            if (modal_decl) {
              field_type = ModalFieldType(
                  *modal_decl,
                  modal_args,
                  pat.state,
                  field.name,
                  ctx);
            }
            IRValue field_val = ctx.FreshTempValue("pat_modal_field");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::ModalField;
            info.base = modal_base;
            info.static_path = modal_path;
            info.modal_state = pat.state;
            info.field = field.name;
            ctx.RegisterDerivedValue(field_val, info);
            if (field_type) {
              ctx.RegisterValueType(field_val, field_type);
            }
            if (field.pattern_opt) {
              CollectPatternBindingsInOrder(*field.pattern_opt, field_val, ctx, bindings);
            } else {
              bindings.push_back({field.name, field_val, field_type});
            }
          }
        } else {
          SPEC_RULE("Lower-Pat-Err");
        }
      },
      pattern.node);
}

}  // namespace

std::vector<std::pair<std::string, IRValue>> PatternBindingValuesInOrder(
    const ast::Pattern& pattern,
    const IRValue& value,
    LowerCtx& ctx) {
  std::vector<OrderedPatternBinding> ordered;
  CollectPatternBindingsInOrder(pattern, value, ctx, ordered);

  std::vector<std::pair<std::string, IRValue>> result;
  result.reserve(ordered.size());
  for (const auto& binding : ordered) {
    result.emplace_back(binding.name, binding.value);
  }
  return result;
}

// ============================================================================
// §6.6 LowerBindPattern - Bind value to pattern
// ============================================================================

IRPtr LowerBindPattern(const ast::Pattern& pattern,
                       const IRValue& value,
                       LowerCtx& ctx) {
  SPEC_RULE("Lower-Pat-General");

  // The spec lowers pattern binding through three logical steps:
  // MatchPattern/BindPatternVal to recover the bound identifier map,
  // BindOrder to linearize it in PatNames order, and BindList to emit the
  // concrete binding operations in that same order.
  std::vector<OrderedPatternBinding> bindings;
  CollectPatternBindingsInOrder(pattern, value, ctx, bindings);
  return EmitOrderedPatternBindings(bindings, ctx);
}

// ============================================================================
// §6.6 LowerBindList - Bind values to patterns
// ============================================================================

IRPtr LowerBindList(const std::vector<std::shared_ptr<ast::Pattern>>& patterns,
                    const std::vector<IRValue>& values,
                    LowerCtx& ctx) {
  SPEC_RULE("Lower-BindList");
  if (patterns.size() != values.size()) {
    ctx.ReportCodegenFailure();
    return EmptyIR();
  }

  std::vector<OrderedPatternBinding> bindings;
  for (std::size_t i = 0; i < patterns.size(); ++i) {
    CollectPatternBindingsInOrder(*patterns[i], values[i], ctx, bindings);
  }
  return EmitOrderedPatternBindings(bindings, ctx);
}

// =============================================================================
// §6.6 RegisterPatternBindings - Wrapper for RegisterBindingsFromPattern
// =============================================================================
//
// This function provides the RegisterPatternBindings interface declared in
// lower_pat.h by forwarding to RegisterBindingsFromPattern in lower_bind.cpp.
// Both functions perform the same operation: walking a pattern and registering
// bindings in the lowering context.

void RegisterPatternBindings(const ast::Pattern& pattern,
                             const analysis::TypeRef& type_hint,
                             LowerCtx& ctx,
                             bool is_immovable,
                             analysis::ProvenanceKind prov,
                             std::optional<std::string> prov_region,
                             std::optional<std::string> prov_region_tag,
                             bool has_responsibility) {
  RegisterBindingsFromPattern(pattern, type_hint, ctx, is_immovable, prov,
                              prov_region, prov_region_tag, has_responsibility);
}

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorPatternCommonRules() {
  // TagOf
  SPEC_RULE("TagOf-Enum");
  SPEC_RULE("TagOf-Modal");

  // Type lookup
  SPEC_RULE("LookupRecordDecl");
  SPEC_RULE("LookupEnumDecl");
  SPEC_RULE("LookupModalDecl");

  // Field lookup
  SPEC_RULE("RecordFieldType");
  SPEC_RULE("EnumPayloadFieldType");
  SPEC_RULE("EnumPayloadTupleType");
  SPEC_RULE("ModalFieldType");

  // Move state merging
  SPEC_RULE("MergeFailures");
  SPEC_RULE("MergeMoveStates");
}

}  // namespace cursive::codegen
