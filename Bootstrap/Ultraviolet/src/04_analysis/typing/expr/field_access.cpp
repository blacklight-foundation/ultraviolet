// =============================================================================
// File: 04_analysis/typing/expr/field_access.cpp
// Field Access Expression Typing
// Spec Sections: 5.2.12 + 5.5
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.12: Record Field Access
//   Section 5.5: Modal State-Specific Fields
//   - T-Field-Record: Basic field access
//   - T-Field-Record-Perm: With permission propagation
//   - Union-DirectAccess-Err (line 9067-9070): Union field access error
//
// =============================================================================

#include "04_analysis/typing/expr/field_access.h"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/records.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal_fields.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/deprecation_warnings.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_lookup.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsFieldAccess() {
  SPEC_DEF("T-Field-Record", "5.2.12");
  SPEC_DEF("T-Field-Record-Perm", "5.2.12");
  SPEC_DEF("P-Field-Record", "5.2.12");
  SPEC_DEF("P-Field-Record-Perm", "5.2.12");
  // Trace anchors retained for compatibility with existing trace data.
  SPEC_DEF("T-FieldAccess", "5.2.12");
  SPEC_DEF("T-FieldAccess-Perm", "5.2.12");
  SPEC_DEF("Union-DirectAccess-Err", "5.2.7");
  SPEC_DEF("FieldAccess-Unknown", "5.2.12");
  SPEC_DEF("FieldAccess-NotVisible", "5.2.12");
  SPEC_DEF("FieldAccess-Enum", "5.2.12");
  SPEC_DEF("Fields", "5.2.12");
  SPEC_DEF("T-Modal-Field", "5.5");
  SPEC_DEF("T-Modal-Field-Perm", "5.5");
  SPEC_DEF("Modal-Field-Missing", "5.5");
  SPEC_DEF("Modal-Field-General-Err", "5.5");
  SPEC_DEF("Modal-Field-NotVisible", "5.5");
  SPEC_DEF("TupleIndex-NonConst", "5.2.12");
}

// Strip permission qualifiers to find the base type
static TypeRef StripPermLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

// Extract permission qualifier if present
static std::optional<Permission> ExtractPerm(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->perm;
  }
  return std::nullopt;
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

static const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                                     const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& seg : path) {
      full.push_back(seg);
    }
    const auto it = ctx.sigma.types.find(PathKeyOf(full));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

static AliasExpandResult ExpandTypeAliasApply(const ScopeContext& ctx,
                                              const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDecl(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

static AliasExpandResult NormalizeFieldBaseType(const ScopeContext& ctx,
                                                const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
	    const auto* path = AppliedTypePath(*out.type);
	    const auto* args = AppliedTypeArgs(*out.type);
	    if (!path || !args) {
	      return out;
	    }
	    const auto expanded = ExpandTypeAliasApply(ctx, TypePathType{*path, *args});
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = StripPermLocal(expanded.type);
    out.expanded = true;
  }
  return out;
}

static const ast::EnumDecl* LookupEnumDeclByPath(const ScopeContext& ctx,
                                                  const TypePath& path) {
  PathKey key;
  key.reserve(path.size());
  for (const auto& seg : path) {
    key.push_back(seg);
  }
  const auto it = ctx.sigma.types.find(key);
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

// Look up field in modal state
struct ModalFieldLookupResult {
  TypeRef type;
  const ast::StateFieldDecl* decl = nullptr;
};

struct ClassFieldLookupResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  const ast::ClassFieldDecl* decl = nullptr;
};

static ClassFieldLookupResult LookupClassSelfField(
    const ScopeContext& ctx,
    const ast::ClassPath& class_path,
    std::string_view field_name) {
  ClassFieldLookupResult result;
  const auto table = ClassFieldTable(ctx, class_path);
  if (!table.ok) {
    result.diag_id = table.diag_id;
    return result;
  }

  const ast::ClassFieldDecl* field = nullptr;
  for (const auto* candidate : table.fields) {
    if (candidate && IdEq(candidate->name, std::string(field_name))) {
      field = candidate;
      break;
    }
  }
  if (!field) {
    result.diag_id = "E-TYP-1904";
    return result;
  }

  const auto lowered = LowerType(ctx, field->type);
  if (!lowered.ok || !lowered.type) {
    result.diag_id = lowered.diag_id;
    return result;
  }

  result.ok = true;
  result.type = SubstSelfType(SelfVarType(), lowered.type);
  result.decl = field;
  return result;
}

static std::optional<ModalFieldLookupResult> LookupModalField(
    const ScopeContext& ctx,
    const TypePath& path,
    const std::vector<TypeRef>& generic_args,
    std::string_view state,
    std::string_view field_name) {
  PathKey key;
  for (const auto& seg : path) {
    key.push_back(seg);
  }

  const auto it = ctx.sigma.types.find(key);
  if (it == ctx.sigma.types.end()) {
    return std::nullopt;
  }

  // Check if it's a modal
  const auto* modal = std::get_if<ast::ModalDecl>(&it->second);
  if (!modal) {
    return std::nullopt;
  }

  TypeSubst modal_subst;
  if (modal->generic_params.has_value()) {
    if (generic_args.size() > modal->generic_params->params.size()) {
      return std::nullopt;
    }
    modal_subst =
        BuildModalRefSubstitution(modal->generic_params->params, generic_args);
  }

  // Find the state
  for (const auto& state_decl : modal->states) {
    if (IdEq(state_decl.name, std::string(state))) {
      // Search for the field in state members
      for (const auto& member : state_decl.members) {
        if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
          if (IdEq(field->name, std::string(field_name))) {
            const auto result = LowerType(ctx, field->type);
            if (result.ok) {
              return ModalFieldLookupResult{
                  InstantiateType(result.type, modal_subst), field};
            }
            return std::nullopt;
          }
        }
      }
      break;
    }
  }

  return std::nullopt;
}

static const ast::ModalDecl* LookupModalDeclByPath(const ScopeContext& ctx,
                                                    const TypePath& path) {
  ast::TypePath ast_path;
  ast_path.reserve(path.size());
  for (const auto& seg : path) {
    ast_path.push_back(seg);
  }
  const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::ModalDecl>(&it->second);
}

}  // namespace

ExprTypeResult TypeFieldAccessExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::FieldAccessExpr& expr,
                                       const TypeEnv& env) {
  SpecDefsFieldAccess();
  ExprTypeResult result;

  if (!expr.base) {
    return result;
  }

  // Get base type from environment or by typing the base expression
  const auto base_type =
      TypeExpr(ctx, SuppressSharedAccessCheck(type_ctx), expr.base, env);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    return result;
  }

  // Extract permission if present
  const auto perm = ExtractPerm(base_type.type);
  const auto stripped = StripPermLocal(base_type.type);
  const auto normalized = NormalizeFieldBaseType(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto stripped_base = normalized.type;

  if (!stripped_base) {
    result.diag_id = "E-TYP-1904";
    return result;
  }

  // Check for union type - direct field access is an error
  if (std::holds_alternative<TypeUnion>(stripped_base->node)) {
    SPEC_RULE("Union-DirectAccess-Err");
    result.diag_id = "E-TYP-2202";  // Union-DirectAccess-Err
    return result;
  }

  // Handle record path and type-application forms.
  if (const auto* path = AppliedTypePath(*stripped_base)) {
      if (IsSelfVarPath(*path) && type_ctx.current_class_path.has_value()) {
        const auto field_lookup =
            LookupClassSelfField(ctx, *type_ctx.current_class_path, expr.name);
        if (!field_lookup.ok) {
          result.diag_id = field_lookup.diag_id;
          return result;
        }
        if (field_lookup.decl) {
          EmitDeprecatedReferenceWarningFromAttrs(
              field_lookup.decl->attrs, type_ctx,
              expr.base ? std::optional<core::Span>(expr.base->span)
                        : std::nullopt);
        }
        if (perm.has_value()) {
          result.ok = true;
          result.type = MakeTypePerm(*perm, field_lookup.type);
        } else {
          result.ok = true;
          result.type = field_lookup.type;
        }
        return result;
      }

      const auto* path_args = AppliedTypeArgs(*stripped_base);
      if (LookupEnumDeclByPath(ctx, *path) != nullptr) {
        SPEC_RULE("FieldAccess-Enum");
        result.diag_id = "E-TYP-1904";
        return result;
      }
      if (LookupModalDeclByPath(ctx, *path) != nullptr) {
        SPEC_RULE("Modal-Field-General-Err");
        result.diag_id = "E-TYP-2057";
        return result;
      }

      const auto* record = LookupRecordDecl(ctx, *path);
      if (!record) {
        SPEC_RULE("FieldAccess-Unknown");
        result.diag_id = "E-TYP-1904";
        return result;
      }

      const auto* field_decl = LookupFieldDecl(*record, expr.name);
      if (!field_decl) {
        SPEC_RULE("FieldAccess-Unknown");
        result.diag_id = "E-TYP-1904";
        return result;
      }
      const auto field_type =
          FieldType(*record, expr.name, ctx, path_args ? *path_args
                                                       : std::vector<TypeRef>{});
      if (!field_type.has_value()) {
        SPEC_RULE("FieldAccess-Unknown");
        result.diag_id = "E-TYP-1904";
        return result;
      }
      if (!FieldVisible(ctx, *record, expr.name, *path)) {
        SPEC_RULE("FieldAccess-NotVisible");
        result.diag_id = "FieldAccess-NotVisible";
        return result;
      }
      EmitDeprecatedReferenceWarningFromAttrs(
          field_decl->attrs, type_ctx,
          expr.base ? std::optional<core::Span>(expr.base->span) : std::nullopt);

      // Apply permission propagation
      if (perm.has_value()) {
        SPEC_RULE("T-Field-Record-Perm");
        SPEC_RULE("T-FieldAccess-Perm");
        result.ok = true;
        result.type = MakeTypePerm(*perm, *field_type);
      } else {
        SPEC_RULE("T-Field-Record");
        SPEC_RULE("T-FieldAccess");
        result.ok = true;
        result.type = *field_type;
      }
      return result;
  }

  // Handle modal state types
  if (const auto* modal = std::get_if<TypeModalState>(&stripped_base->node)) {
    const auto field_lookup = LookupModalField(ctx, modal->path,
                                               modal->generic_args,
                                               modal->state, expr.name);
    if (!field_lookup.has_value()) {
      SPEC_RULE("Modal-Field-Missing");
      result.diag_id = "E-TYP-2052";
      return result;
    }
    if (!ModalFieldVisible(ctx, modal->path)) {
      SPEC_RULE("Modal-Field-NotVisible");
      result.diag_id = "E-TYP-2064";
      return result;
    }
    if (field_lookup->decl) {
      EmitDeprecatedReferenceWarningFromAttrs(
          field_lookup->decl->attrs, type_ctx,
          expr.base ? std::optional<core::Span>(expr.base->span) : std::nullopt);
    }

    // Apply permission propagation
    if (perm.has_value()) {
      SPEC_RULE("T-Modal-Field-Perm");
      result.ok = true;
      result.type = MakeTypePerm(*perm, field_lookup->type);
    } else {
      SPEC_RULE("T-Modal-Field");
      result.ok = true;
      result.type = field_lookup->type;
    }
    return result;
  }

  // Handle tuple types (via numeric field name)
  if (const auto* tuple = std::get_if<TypeTuple>(&stripped_base->node)) {
    // This should go to tuple_access, but handle gracefully
    (void)tuple;
    SPEC_RULE("TupleIndex-NonConst");
    result.diag_id = "TupleIndex-NonConst";
    return result;
  }

  result.diag_id = "E-TYP-1904";
  return result;
}

PlaceTypeResult TypeFieldAccessPlaceImpl(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::FieldAccessExpr& expr,
                                         const TypeEnv& env) {
  SpecDefsFieldAccess();
  PlaceTypeResult result;

  // For place typing, we first type as expression then mark as place
  const auto expr_result = TypeFieldAccessExprImpl(ctx, type_ctx, expr, env);
  if (!expr_result.ok) {
    result.diag_id = expr_result.diag_id;
    return result;
  }

  if (expr_result.type &&
      std::holds_alternative<TypePerm>(expr_result.type->node)) {
    SPEC_RULE("P-Field-Record-Perm");
  } else {
    SPEC_RULE("P-Field-Record");
  }
  result.ok = true;
  result.type = expr_result.type;
  return result;
}

}  // namespace ultraviolet::analysis::expr
