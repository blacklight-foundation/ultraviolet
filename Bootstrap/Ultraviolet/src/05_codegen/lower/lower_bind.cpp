// =============================================================================
// MIGRATION MAPPING: lower_bind.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.5 (Statement Lowering)
//   - Binding initialization and pattern destructuring
//   - Lines 16619-16637 (Lower-Stmt-Let, Lower-Stmt-Var, Shadow variants)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 171-183: LowerBindingType helper
//   - Pattern binding type resolution
//   - Provenance tracking for region safety
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 240-370: RegisterPatternBindings
//   - Lines 376+: LowerBindPattern
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRBindVar)
//   - ultraviolet/src/05_codegen/lower/lower_ctx.h (RegisterVar)
//   - ultraviolet/src/05_codegen/lower/pattern/pattern_common.h
//   - ultraviolet/src/04_analysis/layout/layout.h (LowerTypeForLayout)
//
// REFACTORING NOTES:
//   - This file bridges statement lowering and pattern matching
//   - ProvInfo struct should be part of lower_ctx
//   - Consider unifying binding registration across let/var/shadow
//
// =============================================================================

#include "05_codegen/lower/lower_bind.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <variant>

#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/lower/lower_expr.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

analysis::TypeRef ResolvePatternAliasType(const analysis::TypeRef& type,
                                          LowerCtx& ctx,
                                          std::size_t depth = 0);

namespace {

// Lower a syntax type to an analysis TypeRef for binding purposes
analysis::TypeRef LowerBindingType(const std::shared_ptr<ast::Type>& type_opt,
                                   LowerCtx& ctx) {
  if (!type_opt) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, type_opt)) {
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

std::vector<analysis::TypeRef> GenericArgsFromHint(
    const analysis::TypeRef& type_hint) {
  const analysis::TypeRef stripped = StripPermAndRefine(type_hint);
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

// Lookup record declaration by path
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

// Lookup enum declaration by path
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

// Lookup modal declaration by path
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

// Get type of a record field
analysis::TypeRef RecordFieldType(const ast::RecordDecl& decl,
                                  std::string_view name,
                                  LowerCtx& ctx) {
  for (const auto& member : decl.members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      if (analysis::IdEq(field->name, name)) {
        return LowerBindingType(field->type, ctx);
      }
    }
  }
  return nullptr;
}

// Find variant in enum
const ast::VariantDecl* FindVariant(const ast::EnumDecl& decl,
                                    std::string_view name) {
  for (const auto& variant : decl.variants) {
    if (analysis::IdEq(variant.name, name)) {
      return &variant;
    }
  }
  return nullptr;
}

// Get type of an enum payload field
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

// Find state in modal
const ast::StateBlock* FindState(const ast::ModalDecl& decl,
                                 std::string_view name) {
  for (const auto& state : decl.states) {
    if (analysis::IdEq(state.name, name)) {
      return &state;
    }
  }
  return nullptr;
}

// Get type of a modal field
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
        analysis::TypeRef field_type = LowerBindingType(field->type, ctx);
        if (field_type && !subst.empty()) {
          field_type = analysis::InstantiateType(field_type, subst);
        }
        return field_type;
      }
    }
  }
  return nullptr;
}

// Merge failure states from branch context into base context
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

// Merge move states from multiple branches (for control flow joins)
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

}  // namespace

// ============================================================================
// Binding Registration
// ============================================================================

void RegisterBindingsFromPattern(const ast::Pattern& pattern,
                                 const analysis::TypeRef& type_hint,
                                 LowerCtx& ctx,
                                 bool is_immovable,
                                 analysis::ProvenanceKind prov,
                                 std::optional<std::string> prov_region,
                                 std::optional<std::string> prov_region_tag,
                                 bool has_responsibility) {
  std::function<void(const ast::Pattern&, analysis::TypeRef)> walk =
      [&](const ast::Pattern& pat, analysis::TypeRef hint) {
        std::visit(
            [&](const auto& node) {
              using T = std::decay_t<decltype(node)>;
              if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
                return;
              } else if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
                return;
              } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
                ctx.RegisterVar(node.name, hint, has_responsibility, is_immovable, prov,
                                prov_region, false, prov_region_tag);
                return;
              } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
                if (node.name == "_") {
                  return;
                }
                analysis::TypeRef typed =
                    InstantiateActiveGenericType(LowerBindingType(node.type, ctx), ctx);
                if (!typed) {
                  typed = hint;
                }
                ctx.RegisterVar(node.name, typed, has_responsibility, is_immovable, prov,
                                prov_region, false, prov_region_tag);
                return;
              } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
                const analysis::TypeTuple* tuple_type = nullptr;
                if (hint && std::holds_alternative<analysis::TypeTuple>(hint->node)) {
                  tuple_type = &std::get<analysis::TypeTuple>(hint->node);
                }
                for (std::size_t i = 0; i < node.elements.size(); ++i) {
                  analysis::TypeRef elem_type;
                  if (tuple_type && i < tuple_type->elements.size()) {
                    elem_type = tuple_type->elements[i];
                  }
                  walk(*node.elements[i], elem_type);
                }
                return;
              } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
                const ast::RecordDecl* record = LookupRecordDecl(node.path, ctx);
                for (const auto& field : node.fields) {
                  analysis::TypeRef field_type;
                  if (record) {
                    field_type = RecordFieldType(*record, field.name, ctx);
                  }
                  if (field.pattern_opt) {
                    walk(*field.pattern_opt, field_type);
                  } else {
                    ctx.RegisterVar(field.name, field_type, has_responsibility, is_immovable, prov,
                                    prov_region, false, prov_region_tag);
                  }
                }
                return;
              } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
                const ast::EnumDecl* enum_decl = LookupEnumDecl(node.path, ctx);
                const ast::VariantDecl* variant = nullptr;
                if (enum_decl) {
                  variant = FindVariant(*enum_decl, node.name);
                }
                if (!node.payload_opt.has_value()) {
                  return;
                }
                std::visit(
                    [&](const auto& payload) {
                      using P = std::decay_t<decltype(payload)>;
                      if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                        for (std::size_t i = 0; i < payload.elements.size(); ++i) {
                          analysis::TypeRef elem_type;
                          if (enum_decl && variant) {
                            elem_type = EnumPayloadTupleType(
                                *enum_decl, *variant, GenericArgsFromHint(hint), i, ctx);
                          }
                          walk(*payload.elements[i], elem_type);
                        }
                      } else {
                        for (const auto& field : payload.fields) {
                          analysis::TypeRef field_type;
                          if (enum_decl && variant) {
                            field_type = EnumPayloadFieldType(
                                *enum_decl, *variant, GenericArgsFromHint(hint),
                                field.name, ctx);
                          }
                          if (field.pattern_opt) {
                            walk(*field.pattern_opt, field_type);
                          } else {
                            ctx.RegisterVar(field.name, field_type, has_responsibility, is_immovable, prov,
                                            prov_region, false, prov_region_tag);
                          }
                        }
                      }
                    },
                    *node.payload_opt);
                return;
              } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
                analysis::TypePath modal_path;
                std::vector<analysis::TypeRef> modal_args;
                analysis::TypeRef modal_hint =
                    ResolvePatternAliasType(hint, ctx);
                modal_hint = StripPermAndRefine(modal_hint);
                auto assign_modal_ref = [&](const analysis::TypeRef& type,
                                            bool require_state_match) -> bool {
                  const analysis::TypeRef stripped = StripPermAndRefine(type);
                  if (!stripped) {
                    return false;
                  }
                  if (const auto& modal_state =
                          std::get_if<analysis::TypeModalState>(&stripped->node)) {
                    if (require_state_match &&
                        !analysis::IdEq(modal_state->state, node.state)) {
                      return false;
                    }
                    modal_path = modal_state->path;
                    modal_args = modal_state->generic_args;
                    return true;
                  }
                  if (!require_state_match) {
                    if (const auto* applied_path = analysis::AppliedTypePath(*stripped)) {
                      modal_path = *applied_path;
                      if (const auto* applied_args = analysis::AppliedTypeArgs(*stripped)) {
                        modal_args = *applied_args;
                      } else {
                        modal_args.clear();
                      }
                      return true;
                    }
                  }
                  return false;
                };
                if (!assign_modal_ref(modal_hint, false)) {
                  if (modal_hint &&
                      std::holds_alternative<analysis::TypeUnion>(modal_hint->node)) {
                    const auto& uni = std::get<analysis::TypeUnion>(modal_hint->node);
                    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
                    std::vector<analysis::TypeRef> members = uni.members;
                    if (const auto layout =
                            ::ultraviolet::analysis::layout::UnionLayoutOf(scope, uni)) {
                      members = layout->member_list;
                    }
                    std::optional<analysis::TypeRef> matched_member;
                    bool matched_member_is_ambiguous = false;
                    for (const auto& member : members) {
                      const analysis::TypeRef stripped = StripPermAndRefine(member);
                      const auto* modal_state =
                          stripped
                              ? std::get_if<analysis::TypeModalState>(&stripped->node)
                              : nullptr;
                      if (!modal_state ||
                          !analysis::IdEq(modal_state->state, node.state)) {
                        continue;
                      }
                      if (matched_member.has_value()) {
                        matched_member_is_ambiguous = true;
                        break;
                      }
                      matched_member = stripped;
                    }
                    if (matched_member.has_value() && !matched_member_is_ambiguous) {
                      assign_modal_ref(*matched_member, true);
                    }
                  }
                }
                const ast::ModalDecl* modal_decl = modal_path.empty() ? nullptr : LookupModalDecl(modal_path, ctx);
                if (!node.fields_opt.has_value()) {
                  return;
                }
                for (const auto& field : node.fields_opt->fields) {
                  analysis::TypeRef field_type;
                  if (modal_decl) {
                    field_type = ModalFieldType(
                        *modal_decl,
                        modal_args,
                        node.state,
                        field.name,
                        ctx);
                  }
                  if (field.pattern_opt) {
                    walk(*field.pattern_opt, field_type);
                  } else {
                    ctx.RegisterVar(field.name, field_type, has_responsibility, is_immovable, prov,
                                    prov_region, false, prov_region_tag);
                  }
                }
                return;
              } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
                if (node.lo) {
                  walk(*node.lo, hint);
                }
                if (node.hi) {
                  walk(*node.hi, hint);
                }
                return;
              }
            },
            pat.node);
      };

  walk(pattern, InstantiateActiveGenericType(type_hint, ctx));
}

// ============================================================================
// Binding Emission
// ============================================================================

IRPtr EmitBinding(const std::string& name,
                  const IRValue& value,
                  const analysis::TypeRef& type,
                  analysis::ProvenanceKind prov,
                  std::optional<std::string> prov_region,
                  std::optional<std::string> prov_region_tag,
                  LowerCtx& ctx) {
  IRBindVar bind;
  bind.name = name;
  bind.stable_name = ctx.StableBindingName(name);
  bind.value = value;
  bind.type = type;
  bind.prov = prov;
  bind.prov_region = prov_region;
  bind.prov_region_tag = prov_region_tag;
  return MakeIR(std::move(bind));
}

// ============================================================================
// Provenance Helpers
// ============================================================================

analysis::ProvenanceKind GetBindingProvenance(const ast::Binding& binding,
                                              LowerCtx& ctx) {
  auto prov = ctx.LookupExprProv(*(binding.init));
  return prov.value_or(analysis::ProvenanceKind::Stack);
}

std::optional<std::string> GetBindingRegion(const ast::Binding& binding,
                                            LowerCtx& ctx) {
  return ctx.LookupExprRegion(*(binding.init));
}

std::optional<std::string> GetBindingRegionTag(const ast::Binding& binding,
                                               LowerCtx& ctx) {
  return ctx.LookupExprRegionTag(*(binding.init));
}

}  // namespace ultraviolet::codegen
