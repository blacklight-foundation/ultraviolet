// =============================================================================
// MIGRATION MAPPING: expr/if_case_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 and 6.6 (If-Is Case Analysis Lowering)
//   - Lines 16208-16211: (Lower-Expr-IfCase)
//   - Lines 16757-16813: Pattern matching lowering section
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 676-783: LowerIfCases
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir_model.h (IRIfCase, IRIfCaseClause)
//   - cursive/src/05_codegen/lower/pattern/*.h
//
// =============================================================================

#include "05_codegen/lower/expr/if_case_expr.h"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/unwind.h"
#include "05_codegen/ir/ir_control_flow.h"
#include "05_codegen/lower/lower_pat.h"
#include "05_codegen/lower/pattern/ir_pattern.h"

namespace cursive::codegen {

analysis::TypeRef ResolvePatternAliasType(const analysis::TypeRef& type,
                                          LowerCtx& ctx,
                                          std::size_t depth = 0);

namespace {

LowerCtx MakeBranchCtx(LowerCtx& base) {
  auto saved_value_types = std::move(base.values.value_types);
  auto saved_derived_values = std::move(base.values.derived_values);
  auto saved_drop_glue_types = std::move(base.values.drop_glue_types);

  LowerCtx branch = base;

  base.values.value_types = std::move(saved_value_types);
  base.values.derived_values = std::move(saved_derived_values);
  base.values.drop_glue_types = std::move(saved_drop_glue_types);

  branch.values.value_types.clear();
  branch.values.derived_values.clear();
  branch.values.drop_glue_types.clear();
  branch.values.parent = &base;
  branch.extra_procs.clear();
  return branch;
}

IRPtr CleanupTemps(const std::vector<TempValue>& temps, LowerCtx& ctx) {
  if (temps.empty()) {
    return EmptyIR();
  }

  CleanupPlan plan;
  plan.reserve(temps.size());
  for (auto it = temps.rbegin(); it != temps.rend(); ++it) {
    CleanupAction action;
    action.kind = CleanupAction::Kind::DropTemp;
    action.type = it->type;
    action.value = it->value;
    plan.push_back(std::move(action));
  }
  CleanupPlan remainder = ComputeCleanupPlanToFunctionRoot(ctx);
  return EmitCleanupWithRemainder(plan, remainder, ctx);
}

struct OwnedIfCaseScrutinee {
  std::string name;
  analysis::TypeRef type;
  analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom;
  std::optional<std::string> prov_region;
  std::optional<std::string> prov_region_tag;
};

struct LowerIfCaseClauseResult {
  LowerResult result;
  IRPtr cleanup_ir;
  analysis::TypeRef value_type;
  bool terminates = false;
};

struct AliasExpandResult {
  bool ok = true;
  analysis::TypeRef type = nullptr;
  bool expanded = false;
};

struct TypeAliasLookup {
  const ast::TypeAliasDecl* decl = nullptr;
  ast::ModulePath owner_module;
};

std::optional<TypeAliasLookup> LookupTypeAliasDecl(
    const analysis::ScopeContext& scope,
    const analysis::TypePath& path) {
  if (path.empty()) {
    return std::nullopt;
  }

  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& segment : path) {
      full.push_back(segment);
    }
    const auto it = scope.sigma.types.find(analysis::PathKeyOf(full));
    if (it == scope.sigma.types.end()) {
      return std::nullopt;
    }
    const ast::TypeAliasDecl* decl = std::get_if<ast::TypeAliasDecl>(&it->second);
    if (!decl) {
      return std::nullopt;
    }
    ast::ModulePath owner(full.begin(), full.end() - 1);
    return TypeAliasLookup{decl, std::move(owner)};
  }

  const auto resolved = analysis::ResolveTypeName(scope, path.front());
  if (!resolved.has_value() || !resolved->origin_opt.has_value()) {
    return std::nullopt;
  }

  ast::Path full = *resolved->origin_opt;
  full.push_back(resolved->target_opt.value_or(path.front()));
  const auto it = scope.sigma.types.find(analysis::PathKeyOf(full));
  if (it == scope.sigma.types.end()) {
    return std::nullopt;
  }
  const ast::TypeAliasDecl* decl = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!decl) {
    return std::nullopt;
  }
  return TypeAliasLookup{decl, *resolved->origin_opt};
}

analysis::TypePath ResolveAliasTypePath(
    const analysis::TypePath& path,
    const ast::ModulePath& owner_module,
    const LowerCtx& ctx) {
  if (path.size() != 1) {
    return path;
  }

  if (ctx.resolve_type_name_in_module) {
    if (const auto resolved =
            ctx.resolve_type_name_in_module(owner_module, path.front())) {
      return *resolved;
    }
  }

  if (ctx.sigma) {
    ast::Path same_module_path = owner_module;
    same_module_path.push_back(path.front());
    const analysis::PathKey key = analysis::PathKeyOf(same_module_path);
    if (ctx.sigma->types.find(key) != ctx.sigma->types.end() ||
        ctx.sigma->classes.find(key) != ctx.sigma->classes.end()) {
      return same_module_path;
    }
  }

  return path;
}

std::shared_ptr<ast::Type> ResolveAliasTypeNames(
    const std::shared_ptr<ast::Type>& type,
    const ast::ModulePath& owner_module,
    const LowerCtx& ctx) {
  if (!type) {
    return type;
  }

  auto out = std::make_shared<ast::Type>(*type);
  std::visit(
      [&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          node.base = ResolveAliasTypeNames(node.base, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (auto& member : node.types) {
            member = ResolveAliasTypeNames(member, owner_module, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (auto& param : node.params) {
            param.type = ResolveAliasTypeNames(param.type, owner_module, ctx);
          }
          node.ret = ResolveAliasTypeNames(node.ret, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (auto& param : node.params) {
            param.type = ResolveAliasTypeNames(param.type, owner_module, ctx);
          }
          node.ret = ResolveAliasTypeNames(node.ret, owner_module, ctx);
          if (node.deps_opt.has_value()) {
            for (auto& dep : *node.deps_opt) {
              dep.type = ResolveAliasTypeNames(dep.type, owner_module, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (auto& element : node.elements) {
            element = ResolveAliasTypeNames(element, owner_module, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          node.element = ResolveAliasTypeNames(node.element, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          node.element = ResolveAliasTypeNames(node.element, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          node.element = ResolveAliasTypeNames(node.element, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          node.element = ResolveAliasTypeNames(node.element, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          node.path = ResolveAliasTypePath(node.path, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          node.path = ResolveAliasTypePath(node.path, owner_module, ctx);
          for (auto& arg : node.generic_args) {
            arg = ResolveAliasTypeNames(arg, owner_module, ctx);
          }
          ast::SyncTypeModalStateFromFields(node);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          node.path = ResolveAliasTypePath(node.path, owner_module, ctx);
          for (auto& arg : node.generic_args) {
            arg = ResolveAliasTypeNames(arg, owner_module, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          node.path = ResolveAliasTypePath(node.path, owner_module, ctx);
          for (auto& arg : node.args) {
            arg = ResolveAliasTypeNames(arg, owner_module, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          node.path = ResolveAliasTypePath(node.path, owner_module, ctx);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          node.base = ResolveAliasTypeNames(node.base, owner_module, ctx);
        }
      },
      out->node);
  return out;
}

AliasExpandResult ExpandTypeAlias(
    const analysis::ScopeContext& scope,
    const analysis::TypePath& path,
    const std::vector<analysis::TypeRef>& args,
    const LowerCtx& ctx) {
  AliasExpandResult result;
  const auto alias_lookup = LookupTypeAliasDecl(scope, path);
  if (!alias_lookup.has_value()) {
    return result;
  }
  const ast::TypeAliasDecl& alias = *alias_lookup->decl;

  const auto resolved_alias_type =
      ResolveAliasTypeNames(alias.type, alias_lookup->owner_module, ctx);
  const auto lowered = analysis::LowerType(scope, resolved_alias_type);
  if (!lowered.ok || !lowered.type) {
    result.ok = false;
    return result;
  }

  if (!alias.generic_params.has_value()) {
    if (!args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias.generic_params->params;
  if (args.size() > params.size()) {
    return result;
  }

  const auto subst = analysis::BuildSubstitution(params, args);
  result.type = analysis::InstantiateType(lowered.type, subst);
  result.ok = result.type != nullptr;
  result.expanded = result.ok;
  return result;
}

AliasExpandResult NormalizeAliasTopLevel(
    const analysis::ScopeContext& scope,
    analysis::TypeRef type,
    const LowerCtx& ctx) {
  AliasExpandResult result;
  result.type = std::move(type);

  for (int depth = 0; depth < 16; ++depth) {
    if (!result.type) {
      return result;
    }

    const auto* path = analysis::AppliedTypePath(*result.type);
    const auto* args = analysis::AppliedTypeArgs(*result.type);
    if (!path || !args) {
      return result;
    }

    const auto expanded = ExpandTypeAlias(scope, *path, *args, ctx);
    if (!expanded.ok) {
      result.ok = false;
      return result;
    }
    if (!expanded.expanded) {
      return result;
    }

    result.type = expanded.type;
    result.expanded = true;
  }

  return result;
}

std::optional<std::string> ScrutineeIdentifier(const ast::Expr& scrutinee) {
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&scrutinee.node)) {
    return std::string(ident->name);
  }
  return std::nullopt;
}

bool ModalStateMatches(const analysis::TypeRef& type,
                       std::string_view state_name) {
  const auto stripped = analysis::StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node);
  return modal && analysis::IdEq(modal->state, state_name);
}

analysis::TypeRef RefinedModalPatternType(
    const analysis::ScopeContext& scope,
    const ast::ModalPattern& pattern,
    const analysis::TypeRef& scrutinee_type,
    const LowerCtx& ctx) {
  if (!scrutinee_type) {
    return nullptr;
  }

  if (const auto* perm = std::get_if<analysis::TypePerm>(&scrutinee_type->node)) {
    const analysis::TypeRef narrowed =
        RefinedModalPatternType(scope, pattern, perm->base, ctx);
    if (!narrowed) {
      return nullptr;
    }
    SPEC_RULE("PatternNarrow-Perm");
    return analysis::MakeTypePerm(perm->perm, narrowed);
  }

  const auto normalized =
      NormalizeAliasTopLevel(scope, scrutinee_type, ctx);
  if (!normalized.ok || !normalized.type) {
    return nullptr;
  }
  if (normalized.expanded) {
    return RefinedModalPatternType(scope, pattern, normalized.type, ctx);
  }

  if (const auto* union_type =
          std::get_if<analysis::TypeUnion>(&normalized.type->node)) {
    std::vector<analysis::TypeRef> matched;
    matched.reserve(union_type->members.size());
    for (const auto& member : union_type->members) {
      const analysis::TypeRef member_narrowed =
          RefinedModalPatternType(scope, pattern, member, ctx);
      if (member_narrowed) {
        matched.push_back(member_narrowed);
      }
    }
    if (matched.empty()) {
      return nullptr;
    }
    SPEC_RULE("PatternNarrow-Union");
    if (matched.size() == 1) {
      return matched.front();
    }
    return analysis::MakeTypeUnion(std::move(matched));
  }

  if (ModalStateMatches(normalized.type, pattern.state)) {
    SPEC_RULE("PatternNarrow-ModalState");
    return normalized.type;
  }

  const auto* path = analysis::AppliedTypePath(*normalized.type);
  const auto* args = analysis::AppliedTypeArgs(*normalized.type);
  if (!path) {
    return nullptr;
  }

  ast::TypePath ast_path;
  ast_path.reserve(path->size());
  for (const auto& segment : *path) {
    ast_path.push_back(segment);
  }
  const ast::ModalDecl* modal_decl = analysis::LookupModalDecl(scope, ast_path);
  if (!modal_decl || !analysis::HasState(*modal_decl, pattern.state)) {
    return nullptr;
  }

  SPEC_RULE("PatternNarrow-ModalRef");
  return analysis::MakeTypeModalState(
      *path,
      pattern.state,
      args ? *args : std::vector<analysis::TypeRef>{});
}

analysis::TypeRef RefinedPatternType(
    const analysis::ScopeContext& scope,
    const ast::Pattern& pattern,
    const analysis::TypeRef& scrutinee_type,
    const LowerCtx& ctx) {
  if (const auto* modal = std::get_if<ast::ModalPattern>(&pattern.node)) {
    return RefinedModalPatternType(scope, *modal, scrutinee_type, ctx);
  }
  if (const auto* typed = std::get_if<ast::TypedPattern>(&pattern.node)) {
    const auto lowered = analysis::LowerType(scope, typed->type);
    return lowered.ok ? lowered.type : nullptr;
  }
  return nullptr;
}

bool TypeEquivIgnorePermLocal(const analysis::TypeRef& lhs,
                              const analysis::TypeRef& rhs) {
  const auto lhs_base = analysis::StripPerm(lhs);
  const auto rhs_base = analysis::StripPerm(rhs);
  const auto equiv = analysis::TypeEquiv(lhs_base ? lhs_base : lhs,
                                         rhs_base ? rhs_base : rhs);
  return equiv.ok && equiv.equiv;
}

bool TypedPatternMatchesType(const analysis::ScopeContext& scope,
                             const ast::TypedPattern& pattern,
                             const analysis::TypeRef& expected,
                             LowerCtx& ctx) {
  if (!expected) {
    return false;
  }
  const auto lowered = analysis::LowerType(scope, pattern.type);
  if (!lowered.ok || !lowered.type) {
    return false;
  }
  if (TypeEquivIgnorePermLocal(lowered.type, expected)) {
    return true;
  }

  const auto lowered_base = analysis::StripPerm(lowered.type);
  const auto expected_stripped = analysis::StripPerm(expected);
  const auto expected_base = ResolvePatternAliasType(
      expected_stripped ? expected_stripped : expected, ctx);
  if (!lowered_base || !expected_base) {
    return false;
  }

  const auto* lowered_modal =
      std::get_if<analysis::TypeModalState>(&lowered_base->node);
  if (!lowered_modal) {
    return false;
  }
  const auto* expected_path = analysis::AppliedTypePath(*expected_base);
  const auto* expected_args = analysis::AppliedTypeArgs(*expected_base);
  if (!expected_path || !expected_args ||
      lowered_modal->path != *expected_path ||
      lowered_modal->generic_args.size() != expected_args->size()) {
    return false;
  }
  for (std::size_t i = 0; i < lowered_modal->generic_args.size(); ++i) {
    if (!TypeEquivIgnorePermLocal(lowered_modal->generic_args[i],
                                  (*expected_args)[i])) {
      return false;
    }
  }
  return true;
}

bool PatternMayMatchType(const analysis::ScopeContext& scope,
                         const ast::Pattern& pattern,
                         const analysis::TypeRef& expected,
                         LowerCtx& ctx) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::WildcardPattern> ||
                      std::is_same_v<T, ast::IdentifierPattern>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          return TypedPatternMatchesType(scope, node, expected, ctx);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          return RefinedModalPatternType(scope, node, expected, ctx) != nullptr;
        } else {
          return RefinedPatternType(scope, pattern, expected, ctx) != nullptr;
        }
      },
      pattern.node);
}

analysis::TypeRef RejectedPatternType(const analysis::ScopeContext& scope,
                                      const ast::Pattern& pattern,
                                      const analysis::TypeRef& scrutinee_type,
                                      LowerCtx& ctx) {
  if (!scrutinee_type) {
    return nullptr;
  }

  if (const auto* perm = std::get_if<analysis::TypePerm>(&scrutinee_type->node)) {
    const auto rejected = RejectedPatternType(scope, pattern, perm->base, ctx);
    return rejected ? analysis::MakeTypePerm(perm->perm, rejected) : nullptr;
  }

  const analysis::TypeRef normalized =
      ResolvePatternAliasType(scrutinee_type, ctx);
  if (!normalized) {
    return nullptr;
  }

  const auto* union_type = std::get_if<analysis::TypeUnion>(&normalized->node);
  if (!union_type) {
    return nullptr;
  }

  std::vector<analysis::TypeRef> rejected;
  rejected.reserve(union_type->members.size());
  for (const auto& member : union_type->members) {
    if (!PatternMayMatchType(scope, pattern, member, ctx)) {
      rejected.push_back(member);
    }
  }

  if (rejected.empty() || rejected.size() == union_type->members.size()) {
    return nullptr;
  }
  if (rejected.size() == 1) {
    return rejected.front();
  }
  return analysis::MakeTypeUnion(std::move(rejected));
}

std::optional<std::size_t> UnionMemberIndexForType(
    const analysis::ScopeContext& scope,
    const analysis::TypeRef& scrutinee_type,
    const analysis::TypeRef& target_type,
    LowerCtx& ctx) {
  if (!scrutinee_type || !target_type) {
    return std::nullopt;
  }

  analysis::TypeRef normalized_scrutinee =
      ResolvePatternAliasType(scrutinee_type, ctx);
  normalized_scrutinee = analysis::StripPerm(normalized_scrutinee);
  if (!normalized_scrutinee) {
    return std::nullopt;
  }

  const auto* union_type =
      std::get_if<analysis::TypeUnion>(&normalized_scrutinee->node);
  if (!union_type) {
    return std::nullopt;
  }

  std::vector<analysis::TypeRef> members = union_type->members;
  if (const auto layout = analysis::layout::UnionLayoutOf(scope, *union_type)) {
    members = layout->member_list;
  }

  analysis::TypeRef normalized_target = ResolvePatternAliasType(target_type, ctx);
  normalized_target = analysis::StripPerm(normalized_target);
  if (!normalized_target) {
    return std::nullopt;
  }

  for (std::size_t i = 0; i < members.size(); ++i) {
    if (TypeEquivIgnorePermLocal(members[i], normalized_target)) {
      return i;
    }
  }
  return std::nullopt;
}

void RefineScrutineeBindingToType(const ast::Expr& scrutinee,
                                  const analysis::TypeRef& refined_type,
                                  LowerCtx& ctx) {
  if (!refined_type) {
    return;
  }
  const auto name = ScrutineeIdentifier(scrutinee);
  if (!name.has_value()) {
    return;
  }
  auto binding_it = ctx.binding_states.find(*name);
  if (binding_it == ctx.binding_states.end() || binding_it->second.empty()) {
    return;
  }
  binding_it->second.back().type = refined_type;
}

struct ElseNarrowing {
  analysis::TypeRef type;
  bool narrowed = false;
};

ElseNarrowing ElseNarrowedType(const std::vector<ast::IfCaseClause>& arms,
                               const analysis::TypeRef& scrutinee_type,
                               LowerCtx& ctx) {
  ElseNarrowing result;
  if (!ctx.sigma || !scrutinee_type) {
    return result;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  analysis::TypeRef remaining = scrutinee_type;
  bool narrowed = false;

  for (const auto& arm : arms) {
    if (!arm.pattern) {
      continue;
    }
    const analysis::TypeRef rejected =
        RejectedPatternType(scope, *arm.pattern, remaining, ctx);
    if (rejected) {
      remaining = rejected;
      narrowed = true;
    }
  }

  if (narrowed) {
    result.type = remaining;
    result.narrowed = true;
  }
  return result;
}

IRPtr BindElseNarrowedScrutinee(const ast::Expr& scrutinee,
                                const IRValue& scrutinee_value,
                                const analysis::TypeRef& scrutinee_type,
                                const analysis::TypeRef& narrowed_type,
                                LowerCtx& ctx) {
  const auto name = ScrutineeIdentifier(scrutinee);
  if (!name.has_value() || !narrowed_type) {
    return EmptyIR();
  }

  const BindingState* original_binding = ctx.GetBindingState(*name);
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  const auto member_index =
      UnionMemberIndexForType(scope, scrutinee_type, narrowed_type, ctx);
  if (!member_index.has_value()) {
    RefineScrutineeBindingToType(scrutinee, narrowed_type, ctx);
    return EmptyIR();
  }

  ctx.RegisterVar(*name,
                  narrowed_type,
                  original_binding ? original_binding->has_responsibility : true,
                  original_binding ? original_binding->is_immovable : false,
                  original_binding ? original_binding->prov
                                   : analysis::ProvenanceKind::Bottom,
                  original_binding ? original_binding->prov_region
                                   : std::optional<std::string>{},
                  original_binding
                      ? original_binding->preserve_addr_provenance
                      : false,
                  original_binding ? original_binding->prov_region_tag
                                   : std::optional<std::string>{});

  IRValue payload = ctx.FreshTempValue("ifcase_else_payload");
  DerivedValueInfo info;
  info.kind = DerivedValueInfo::Kind::UnionPayload;
  info.base = scrutinee_value;
  info.union_index = *member_index;
  ctx.RegisterDerivedValue(payload, std::move(info));
  ctx.RegisterValueType(payload, narrowed_type);

  IRBindVar bind;
  bind.name = *name;
  bind.stable_name = ctx.StableBindingName(*name);
  bind.value = payload;
  bind.type = narrowed_type;
  if (const BindingState* narrowed_binding = ctx.GetBindingState(*name)) {
    bind.prov = narrowed_binding->prov;
    bind.prov_region = narrowed_binding->prov_region;
    bind.prov_region_tag = narrowed_binding->prov_region_tag;
  } else {
    bind.prov = analysis::ProvenanceKind::Bottom;
  }
  return MakeIR(std::move(bind));
}

void RefineScrutineeBinding(const ast::Expr& scrutinee,
                            const ast::Pattern& pattern,
                            const analysis::TypeRef& scrutinee_type,
                            LowerCtx& ctx) {
  if (!ctx.sigma) {
    return;
  }

  const auto name = ScrutineeIdentifier(scrutinee);
  if (!name.has_value()) {
    return;
  }

  auto binding_it = ctx.binding_states.find(*name);
  if (binding_it == ctx.binding_states.end() || binding_it->second.empty()) {
    return;
  }

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  auto& binding = binding_it->second.back();
  analysis::TypeRef refined =
      RefinedPatternType(scope, pattern, binding.type, ctx);
  if (!refined && scrutinee_type) {
    refined = RefinedPatternType(scope, pattern, scrutinee_type, ctx);
  }
  if (!refined) {
    return;
  }

  binding.type = refined;
}

LowerIfCaseClauseResult LowerIfCaseClauseImpl(
    const ast::IfCaseClause& arm,
    const ast::Expr& scrutinee_expr,
    const IRValue& scrutinee,
    const analysis::TypeRef& scrutinee_type,
    analysis::ProvenanceKind scrutinee_prov,
    std::optional<std::string> scrutinee_region,
    std::optional<std::string> scrutinee_region_tag,
    const std::optional<OwnedIfCaseScrutinee>& owned_scrutinee,
    LowerCtx& ctx);

// Merge value type info from branch context into base context
// Used when lowering case clauses where only one branch may execute
void MergeLowerCtxTemps(LowerCtx& base, const LowerCtx& branch) {
  for (const auto& [name, type] : branch.values.value_types) {
    if (!base.values.value_types.count(name)) {
      base.values.value_types.emplace(name, type);
    }
  }
  for (const auto& [name, info] : branch.values.derived_values) {
    if (!base.values.derived_values.count(name)) {
      base.values.derived_values.emplace(name, info);
    }
  }
  for (const auto& [name, type] : branch.values.static_types) {
    if (!base.values.static_types.count(name)) {
      base.values.static_types.emplace(name, type);
    }
  }
  for (const auto& [name, type] : branch.values.drop_glue_types) {
    if (!base.values.drop_glue_types.count(name)) {
      base.values.drop_glue_types.emplace(name, type);
    }
  }
}

// Merge move states from multiple branch contexts into a single base context
// A binding is considered moved if it was moved in any branch
void MergeMoveStates(LowerCtx& base, const std::vector<const LowerCtx*>& branches) {
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

// Merge failure flags from branch context into base context
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

}  // namespace

// ============================================================================
// Lower-IfCases - Lower if-case expression
// ============================================================================
//
// Per CursiveSpecification.md (Lower-IfCases) lines 16808-16811:
// Gamma |- LowerExpr(scrut) => <IR_s, v_s>
// ----------------------------------------------------------------------------
// Gamma |- LowerIfCases(scrut, arms) => <SeqIR(IR_s, IfCaseIR(v_s, arms)), v_case>
//
// The implementation:
// 1. Lowers the scrutinee expression
// 2. For each case clause:
//    a. Creates a new scope for pattern bindings
//    b. Registers pattern bindings with type information
//    c. Binds the pattern to the scrutinee value
//    d. Lowers the arm body expression
//    e. Computes and emits cleanup for the arm scope
// 3. Merges move states and failure flags across all case clauses
// 4. Produces IRIfCase with the scrutinee value and all lowered clauses
// ============================================================================

LowerResult LowerIfCases(const ast::Expr& scrutinee,
                         const std::vector<ast::IfCaseClause>& arms,
                         const ast::ExprPtr& else_expr,
                         bool single_form,
                         LowerCtx& ctx,
                         const analysis::TypeRef& expected_result_type) {
  SPEC_RULE("Lower-IfCases");

  // Lower the scrutinee under a dedicated temp sink. Nested temporaries used
  // to compute the scrutinee are cleaned before the case analysis executes, while
  // the top-level scrutinee value itself is managed per clause when it is not backed
  // by a local binding.
  auto* prev_sink = ctx.temp_sink;
  std::vector<TempValue> scrutinee_temps;
  ctx.temp_sink = &scrutinee_temps;
  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto scrutinee_result = LowerExpr(scrutinee, ctx);
  ctx.temp_sink = prev_sink;
  ctx.suppress_temp_at_depth = prev_suppress;
  IRPtr scrutinee_cleanup = CleanupTemps(scrutinee_temps, ctx);
  if (IsNoopIR(scrutinee_cleanup)) {
    scrutinee_cleanup = EmptyIR();
  }

  // Get the type of the scrutinee for pattern binding registration
  analysis::TypeRef scrutinee_type;
  if (ctx.expr_type) {
    scrutinee_type = ctx.expr_type(scrutinee);
  }
  if (!scrutinee_type) {
    scrutinee_type = ctx.LookupValueType(scrutinee_result.value);
  }
  if (!scrutinee_type) {
    if (const auto name = ScrutineeIdentifier(scrutinee)) {
      if (const BindingState* binding = ctx.GetBindingState(*name)) {
        scrutinee_type = binding->type;
      }
    }
  }
  if (scrutinee_type) {
    scrutinee_type = ResolvePatternAliasType(scrutinee_type, ctx);
  }

  // Get provenance information for the scrutinee
  analysis::ProvenanceKind scrutinee_prov = analysis::ProvenanceKind::Bottom;
  if (auto prov = ctx.LookupExprProv(scrutinee)) {
    scrutinee_prov = *prov;
  }
  std::optional<std::string> scrutinee_region;
  std::optional<std::string> scrutinee_region_tag;
  if (scrutinee_prov == analysis::ProvenanceKind::Region) {
    scrutinee_region = ctx.LookupExprRegion(scrutinee);
    scrutinee_region_tag = ctx.LookupExprRegionTag(scrutinee);
  }

  // Register the scrutinee value's type if known
  if (scrutinee_type) {
    ctx.RegisterValueType(scrutinee_result.value, scrutinee_type);
  }

  std::optional<OwnedIfCaseScrutinee> owned_scrutinee;
  if (scrutinee_type &&
      scrutinee_result.value.kind != IRValue::Kind::Local &&
      scrutinee_result.value.kind != IRValue::Kind::Symbol) {
    OwnedIfCaseScrutinee info;
    info.name = ctx.FreshTempValue("ifcase_scrutinee").name;
    info.type = scrutinee_type;
    info.prov = scrutinee_prov;
    info.prov_region = scrutinee_region;
    info.prov_region_tag = scrutinee_region_tag;
    owned_scrutinee = std::move(info);
  }

  // Create case clauses IR.
  std::vector<IRIfCaseClause> ir_arms;
  std::vector<LowerCtx> arm_ctxs;
  arm_ctxs.reserve(arms.size() + (else_expr || single_form ? 1 : 0));

  analysis::TypeRef result_type = expected_result_type;
  auto merge_result_type = [&](analysis::TypeRef candidate) {
    if (!candidate) {
      return;
    }
    if (!result_type) {
      result_type = candidate;
    }
  };

  for (const auto& arm : arms) {
    // Create a copy of the context for this clause to track clause-local state.
    LowerCtx arm_ctx = MakeBranchCtx(ctx);

    // Lower this case clause.
    auto clause_result = LowerIfCaseClauseImpl(arm, scrutinee,
                                               scrutinee_result.value,
                                               scrutinee_type, scrutinee_prov,
                                               scrutinee_region, scrutinee_region_tag,
                                               owned_scrutinee,
                                               arm_ctx);

    // Merge arm context temps back to base context
    MergeLowerCtxTemps(ctx, arm_ctx);
    ctx.MergeGeneratedProcsFrom(arm_ctx);

    // Update temp counter to the maximum across all arms
    *ctx.temp_counter = std::max(*ctx.temp_counter, *arm_ctx.temp_counter);

    // Build the IR case clause.
    IRIfCaseClause ir_arm;
    ir_arm.pattern = LowerIRPattern(*arm.pattern, arm_ctx);
    ir_arm.body = clause_result.result.ir;
    ir_arm.cleanup_ir = clause_result.cleanup_ir;
    ir_arm.value = clause_result.result.value;
    ir_arm.value_type = clause_result.value_type;
    if (!clause_result.terminates) {
      merge_result_type(clause_result.value_type);
    }
    ir_arms.push_back(std::move(ir_arm));
    arm_ctxs.push_back(std::move(arm_ctx));
  }

  if (else_expr || single_form) {
    LowerCtx else_ctx = MakeBranchCtx(ctx);
    LowerResult else_result;
    analysis::TypeRef else_type;
    IRPtr else_scope_enter_ir = EmptyIR();
    IRPtr else_narrow_ir = EmptyIR();
    IRPtr else_cleanup_ir = EmptyIR();
    bool else_has_narrow_scope = false;

    const ElseNarrowing else_narrowing =
        ElseNarrowedType(arms, scrutinee_type, else_ctx);
    if (else_narrowing.narrowed) {
      else_has_narrow_scope = true;
      else_ctx.PushScope(false, false);
      else_narrow_ir =
          BindElseNarrowedScrutinee(scrutinee,
                                    scrutinee_result.value,
                                    scrutinee_type,
                                    else_narrowing.type,
                                    else_ctx);
    }

    if (else_expr) {
      else_result = LowerExpr(*else_expr, else_ctx);
    } else {
      ast::Block unit_block;
      else_result = LowerBlock(unit_block, else_ctx);
    }

    const bool else_may_fallthrough = IRFlowMayFallThrough(else_result.ir);
    IRPtr else_capture_ir = EmptyIR();
    if (else_may_fallthrough) {
      else_type = else_ctx.LookupValueType(else_result.value);
      if (!else_type && else_expr && ctx.expr_type) {
        else_type = ctx.expr_type(*else_expr);
      }
      if (!else_type && !else_expr && single_form) {
        else_type = analysis::MakeTypePrim("()");
      }
      if (else_has_narrow_scope && else_type) {
        IRBindVar capture;
        capture.name = else_ctx.FreshTempValue("if_case_else_result").name;
        capture.value = else_result.value;
        capture.type = else_type;
        capture.prov = analysis::ProvenanceKind::Bottom;
        capture.prov_region = std::nullopt;
        capture.prov_region_tag = std::nullopt;
        else_result.value.kind = IRValue::Kind::Local;
        else_result.value.name = capture.name;
        else_capture_ir = MakeIR(std::move(capture));
      }
      merge_result_type(else_type);
    }

    if (else_has_narrow_scope) {
      else_ctx.RegisterRuntimeScopeExitIfRequired();
      if (else_ctx.CurrentScopeRequiresRuntime()) {
        if (const auto scope_id = else_ctx.CurrentRuntimeScopeId()) {
          else_scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, else_ctx);
        }
      }
      CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(else_ctx);
      CleanupPlan remainder =
          ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, else_ctx);
      else_cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, else_ctx);
      else_ctx.PopScope();
      else_result.ir = SeqIR({else_scope_enter_ir,
                              else_narrow_ir,
                              else_result.ir,
                              else_capture_ir});
    }

    MergeLowerCtxTemps(ctx, else_ctx);
    ctx.MergeGeneratedProcsFrom(else_ctx);
    *ctx.temp_counter = std::max(*ctx.temp_counter, *else_ctx.temp_counter);

    IRIfCaseClause else_arm;
    else_arm.pattern = std::make_shared<IRPattern>(IRPattern{IRWildcardPattern{}});
    else_arm.body = else_result.ir;
    else_arm.cleanup_ir =
        else_may_fallthrough && else_has_narrow_scope ? else_cleanup_ir
                                                      : EmptyIR();
    else_arm.value = else_result.value;
    else_arm.value_type = else_type;
    ir_arms.push_back(std::move(else_arm));
    arm_ctxs.push_back(std::move(else_ctx));
  }

  // Merge move states from all clauses into the base context.
  // A binding is considered moved if it was moved in any clause.
  std::vector<const LowerCtx*> branches;
  branches.reserve(arm_ctxs.size());
  for (const auto& arm_ctx : arm_ctxs) {
    branches.push_back(&arm_ctx);
  }
  MergeMoveStates(ctx, branches);

  // Merge failure flags from all clauses.
  for (const auto& arm_ctx : arm_ctxs) {
    MergeFailures(ctx, arm_ctx);
  }

  // Build the IRIfCase node.
  IRIfCase if_case;
  if_case.scrutinee = scrutinee_result.value;
  if_case.scrutinee_type = scrutinee_type;
  if_case.arms = std::move(ir_arms);

  // Create the result value for the if-is expression.
  IRValue result = ctx.FreshTempValue("ifcase");
  if_case.result = result;

  if (result_type) {
    ctx.RegisterValueType(result, result_type);
  }

  return LowerResult{SeqIR({scrutinee_result.ir,
                            scrutinee_cleanup,
                            MakeIR(std::move(if_case))}),
                     result};
}

// ============================================================================
// Lower-IfCaseClause - Lower a single case clause
// ============================================================================
//
// Per spec section 6.6:
// 1. Push a new scope for pattern bindings
// 2. Register pattern bindings with the scrutinee's type
// 3. Bind the pattern to the scrutinee value
// 4. Lower the arm body expression
// 5. Compute and emit cleanup for the arm scope
// 6. Pop the scope and return the result
// ============================================================================

namespace {

LowerIfCaseClauseResult LowerIfCaseClauseImpl(
    const ast::IfCaseClause& arm,
    const ast::Expr& scrutinee_expr,
    const IRValue& scrutinee,
    const analysis::TypeRef& scrutinee_type,
    analysis::ProvenanceKind scrutinee_prov,
    std::optional<std::string> scrutinee_region,
    std::optional<std::string> scrutinee_region_tag,
    const std::optional<OwnedIfCaseScrutinee>& owned_scrutinee,
    LowerCtx& ctx) {
  // Push a new scope for pattern bindings
  ctx.PushScope(false, false);
  IRPtr scope_enter_ir = EmptyIR();

  IRValue bind_scrutinee = scrutinee;
  IRPtr scrutinee_bind_ir = EmptyIR();
  if (owned_scrutinee.has_value()) {
    ctx.RegisterVar(owned_scrutinee->name,
                    owned_scrutinee->type,
                    true,
                    false,
                    owned_scrutinee->prov,
                    owned_scrutinee->prov_region,
                    false,
                    owned_scrutinee->prov_region_tag);

    IRBindVar scrutinee_bind;
    scrutinee_bind.name = owned_scrutinee->name;
    scrutinee_bind.value = scrutinee;
    scrutinee_bind.type = owned_scrutinee->type;
    scrutinee_bind.prov = owned_scrutinee->prov;
    scrutinee_bind.prov_region = owned_scrutinee->prov_region;
    scrutinee_bind.prov_region_tag = owned_scrutinee->prov_region_tag;
    scrutinee_bind_ir = MakeIR(std::move(scrutinee_bind));

    bind_scrutinee.kind = IRValue::Kind::Local;
    bind_scrutinee.name = owned_scrutinee->name;
  }

  bool scrutinee_has_responsibility = true;
  if (bind_scrutinee.kind == IRValue::Kind::Local) {
    if (const BindingState* scrutinee_state = ctx.GetBindingState(bind_scrutinee.name)) {
      scrutinee_has_responsibility = scrutinee_state->has_responsibility;
    }
  } else if (scrutinee_prov == analysis::ProvenanceKind::Param) {
    scrutinee_has_responsibility = false;
  }

  // Register the bindings introduced by the pattern
  RegisterPatternBindings(*arm.pattern, scrutinee_type, ctx, false,
                          scrutinee_prov, scrutinee_region, scrutinee_region_tag,
                          scrutinee_has_responsibility);

  // Bind the pattern - this creates IR to extract values from the scrutinee
  IRPtr bind_ir = LowerBindPattern(*arm.pattern, bind_scrutinee, ctx);

  RefineScrutineeBinding(scrutinee_expr, *arm.pattern, scrutinee_type, ctx);

  // Lower the body expression
  LowerResult body_result;
  std::vector<TempValue> body_temps;
  if (arm.body) {
    auto* prev_sink = ctx.temp_sink;
    ctx.temp_sink = &body_temps;
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    body_result = LowerExpr(*arm.body, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    ctx.temp_sink = prev_sink;
  } else {
    // Empty body returns unit
    body_result.ir = EmptyIR();
    body_result.value = ctx.FreshTempValue("unit");
    ctx.RegisterValueType(body_result.value, analysis::MakeTypePrim("()"));
  }

  IRValue arm_value = body_result.value;
  IRPtr capture_ir = EmptyIR();
  const bool body_may_fallthrough = IRFlowMayFallThrough(body_result.ir);
  analysis::TypeRef body_type = ctx.LookupValueType(body_result.value);
  if (!body_type && !arm.body) {
    body_type = analysis::MakeTypePrim("()");
  }
  if (arm.body && body_may_fallthrough) {
    if (!body_type && ctx.expr_type) {
      body_type = ctx.expr_type(*arm.body);
    }
    if (body_type) {
      IRBindVar capture;
      capture.name = ctx.FreshTempValue("if_case_clause_result").name;
      capture.value = body_result.value;
      capture.type = body_type;
      capture.prov = analysis::ProvenanceKind::Bottom;
      capture.prov_region = std::nullopt;
      capture.prov_region_tag = std::nullopt;
      arm_value.kind = IRValue::Kind::Local;
      arm_value.name = capture.name;
      capture_ir = MakeIR(std::move(capture));
    }
  }

  IRPtr body_temp_cleanup = CleanupTemps(body_temps, ctx);
  if (IsNoopIR(body_temp_cleanup)) {
    body_temp_cleanup = EmptyIR();
  }

  // Compute cleanup for the clause scope.
  ctx.RegisterRuntimeScopeExitIfRequired();
  if (ctx.CurrentScopeRequiresRuntime()) {
    if (const auto scope_id = ctx.CurrentRuntimeScopeId()) {
      scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, ctx);
    }
  }
  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  CleanupPlan remainder = ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, ctx);
  IRPtr cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, ctx);

  std::vector<IRPtr> parts;
  parts.push_back(scope_enter_ir);
  parts.push_back(scrutinee_bind_ir);
  parts.push_back(bind_ir);
  parts.push_back(body_result.ir);
  if (body_may_fallthrough) {
    parts.push_back(capture_ir);
  }

  // Pop the clause scope.
  ctx.PopScope();

  LowerIfCaseClauseResult result;
  result.result = LowerResult{SeqIR(std::move(parts)), arm_value};
  result.cleanup_ir = body_may_fallthrough
      ? SeqIR({body_temp_cleanup, cleanup_ir})
      : EmptyIR();
  result.value_type = body_may_fallthrough ? body_type : nullptr;
  result.terminates = !body_may_fallthrough;
  return result;
}

}  // namespace

LowerResult LowerIfCaseClause(const ast::IfCaseClause& arm,
                          const IRValue& scrutinee,
                          const analysis::TypeRef& scrutinee_type,
                          analysis::ProvenanceKind scrutinee_prov,
                          std::optional<std::string> scrutinee_region,
                          std::optional<std::string> scrutinee_region_tag,
                          LowerCtx& ctx) {
  ast::Expr synthetic_scrutinee;
  return LowerIfCaseClauseImpl(arm, synthetic_scrutinee, scrutinee,
                               scrutinee_type, scrutinee_prov,
                               std::move(scrutinee_region),
                               std::move(scrutinee_region_tag),
                               std::nullopt, ctx).result;
}

}  // namespace cursive::codegen
