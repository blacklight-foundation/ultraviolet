// =============================================================================
// Typed Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Typed pattern x: T binds with explicit type annotation
//   - Type is lowered and used for binding registration
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 259-266: TypedPattern in RegisterPatternBindings
//   - Lines 413-459: TypedPattern in LowerBindPattern
//
// =============================================================================

#include "05_codegen/lower/pattern/typed_pattern.h"

#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_layout.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

namespace {

// Lower a syntax type to analysis TypeRef
analysis::TypeRef LowerSyntaxType(const std::shared_ptr<ast::Type>& type,
                                  LowerCtx& ctx) {
  if (!type) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, type)) {
    return *lowered;
  }
  return nullptr;
}

analysis::TypeRef ResolveAliasTypeForPattern(const analysis::TypeRef& type,
                                             LowerCtx& ctx,
                                             std::size_t depth = 0) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    stripped = type;
  }
  if (!stripped || depth > 16 || !ctx.sigma) {
    return stripped;
  }

  const auto* path = std::get_if<analysis::TypePathType>(&stripped->node);
  if (!path) {
    return stripped;
  }

  ast::Path syntax_path;
  syntax_path.reserve(path->path.size());
  for (const auto& seg : path->path) {
    syntax_path.push_back(seg);
  }
  const ast::TypeAliasDecl* alias = nullptr;
  if (const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
      it != ctx.sigma->types.end()) {
    alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  }
  if (!alias && syntax_path.size() == 1) {
    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    const auto resolved = analysis::ResolveTypeName(scope, syntax_path.front());
    if (resolved.has_value() && resolved->origin_opt.has_value()) {
      ast::Path full_path = *resolved->origin_opt;
      full_path.push_back(resolved->target_opt.value_or(syntax_path.front()));
      if (const auto it = ctx.sigma->types.find(analysis::PathKeyOf(full_path));
          it != ctx.sigma->types.end()) {
        alias = std::get_if<ast::TypeAliasDecl>(&it->second);
      }
    }
  }
  if (!alias) {
    return stripped;
  }

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);

  const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, alias->type);
  if (!lowered.has_value()) {
    return stripped;
  }

  analysis::TypeRef inst = *lowered;
  if (alias->generic_params &&
      !alias->generic_params->params.empty() &&
      !path->generic_args.empty()) {
    analysis::TypeSubst subst =
        analysis::BuildSubstitution(alias->generic_params->params,
                                    path->generic_args);
    inst = analysis::InstantiateType(inst, subst);
  }

  return ResolveAliasTypeForPattern(inst, ctx, depth + 1);
}

bool TypeEquivForUnionMatch(analysis::TypeRef lhs, analysis::TypeRef rhs) {
  auto strip_perm_refine = [](analysis::TypeRef type) -> analysis::TypeRef {
    while (type) {
      if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
        type = perm->base;
        continue;
      }
      if (const auto* refine = std::get_if<analysis::TypeRefine>(&type->node)) {
        type = refine->base;
        continue;
      }
      break;
    }
    return type;
  };
  lhs = strip_perm_refine(lhs);
  rhs = strip_perm_refine(rhs);
  const auto equiv = analysis::TypeEquiv(lhs, rhs);
  return equiv.ok && equiv.equiv;
}

}  // namespace

// ============================================================================
// RegisterTypedPatternBindings
// ============================================================================
//
// Registers a binding for the typed pattern with its explicit type annotation.
// The type from the annotation takes precedence over any type hint.
//
void RegisterTypedPatternBindings(const ast::TypedPattern& pattern,
                                   const analysis::TypeRef& type_hint,
                                   LowerCtx& ctx,
                                   bool is_immovable,
                                   analysis::ProvenanceKind prov,
                                   std::optional<std::string> prov_region,
                                   std::optional<std::string> prov_region_tag) {
  if (pattern.name == "_") {
    return;
  }

  // Lower the explicit type annotation
  analysis::TypeRef typed = LowerSyntaxType(pattern.type, ctx);
  if (!typed) {
    ctx.ReportCodegenFailure();
  }
  ctx.RegisterVar(pattern.name, typed ? typed : type_hint, true, is_immovable,
                  prov, prov_region, false, prov_region_tag);
}

// ============================================================================
// LowerTypedPatternBindings
// ============================================================================
//
// Creates the binding IR for a typed pattern.
// For union types, this may involve extracting the payload from the union
// if the bound type matches one of the union members.
//
IRPtr LowerTypedPatternBindings(const ast::TypedPattern& pattern,
                                 const IRValue& value,
                                 LowerCtx& ctx) {
  if (pattern.name == "_") {
    return EmptyIR();
  }

  // Look up binding info from context
  auto lookup_bind_type = [&ctx](const std::string& name) -> analysis::TypeRef {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->type;
    }
    return nullptr;
  };
  auto lookup_bind_prov = [&ctx](const std::string& name) -> analysis::ProvenanceKind {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov;
    }
    return analysis::ProvenanceKind::Bottom;
  };
  auto lookup_bind_region = [&ctx](const std::string& name) -> std::optional<std::string> {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov_region;
    }
    return std::nullopt;
  };
  auto lookup_bind_region_tag = [&ctx](const std::string& name) -> std::optional<std::string> {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov_region_tag;
    }
    return std::nullopt;
  };

  IRValue bind_value = value;

  // Handle union type extraction if needed
  if (ctx.sigma) {
    analysis::TypeRef target = LowerSyntaxType(pattern.type, ctx);
    target = ResolveAliasTypeForPattern(target, ctx);
    const auto base_type = ResolveAliasTypeForPattern(ctx.LookupValueType(value), ctx);

    if (target && base_type) {
      // Check if the base type is a union
      if (std::holds_alternative<analysis::TypeUnion>(base_type->node)) {
        const auto& uni = std::get<analysis::TypeUnion>(base_type->node);
        const analysis::ScopeContext& scope = ScopeForLowering(ctx);

        if (const auto layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, uni)) {
          const auto& members = layout->member_list;
          std::optional<std::size_t> member_index;

          // Find which union member matches the target type
          for (std::size_t i = 0; i < members.size(); ++i) {
            if (TypeEquivForUnionMatch(target, members[i])) {
              member_index = i;
              break;
            }
          }

          if (member_index.has_value()) {
            // Create a derived value for the union payload extraction
            IRValue payload = ctx.FreshTempValue("pat_union_payload");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::UnionPayload;
            info.base = value;
            info.union_index = *member_index;
            ctx.RegisterDerivedValue(payload, info);
            bind_value = payload;
          }
        }
      }
    }
  }

  // Create the binding
  IRBindVar bind;
  bind.name = pattern.name;
  bind.stable_name = ctx.StableBindingName(pattern.name);
  bind.value = bind_value;
  bind.type = lookup_bind_type(pattern.name);
  if (!bind.type) {
    bind.type = LowerSyntaxType(pattern.type, ctx);
    if (!bind.type) {
      ctx.ReportCodegenFailure();
    }
  }
  bind.prov = lookup_bind_prov(pattern.name);
  bind.prov_region = lookup_bind_region(pattern.name);
  bind.prov_region_tag = lookup_bind_region_tag(pattern.name);

  IRValue local_value;
  local_value.kind = IRValue::Kind::Local;
  local_value.name = bind.stable_name.empty() ? pattern.name : bind.stable_name;
  if (const DerivedValueInfo* derived = ctx.LookupDerivedValue(bind_value)) {
    ctx.RegisterDerivedValue(local_value, *derived);
  } else {
    analysis::TypeRef target = ResolveAliasTypeForPattern(bind.type, ctx);
    const bool target_is_closure =
        target && std::holds_alternative<analysis::TypeClosure>(target->node);
    if (target_is_closure && bind_value.kind == IRValue::Kind::Symbol) {
      IRValue env_null;
      env_null.kind = IRValue::Kind::Immediate;
      env_null.name = "null";
      env_null.bytes = {0, 0, 0, 0, 0, 0, 0, 0};

      DerivedValueInfo closure_info;
      closure_info.kind = DerivedValueInfo::Kind::TupleLit;
      closure_info.elements.push_back(env_null);
      closure_info.elements.push_back(bind_value);
      ctx.RegisterDerivedValue(local_value, closure_info);
    }
  }

  return MakeIR(std::move(bind));
}

// ============================================================================
// PatternCheckTyped
// ============================================================================
//
// Typed patterns are validated by static typing; runtime matching is irrefutable.
//
IRValue PatternCheckTyped(const ast::TypedPattern& pattern,
                           const IRValue& value,
                           LowerCtx& ctx) {
  (void)pattern;
  (void)value;
  (void)ctx;
  IRValue result;
  result.kind = IRValue::Kind::Immediate;
  result.name = "true";
  result.bytes = {static_cast<std::uint8_t>(1)};
  return result;
}

}  // namespace ultraviolet::codegen
