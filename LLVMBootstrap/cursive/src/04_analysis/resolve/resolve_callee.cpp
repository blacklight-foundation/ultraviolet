// =============================================================================
// resolve_callee.cpp - Callee Expression Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.3 "Lookup" (Lines 6822-6899)
//   CursiveSpecification.md §5.1.6 "Qualified Disambiguation" (Lines 7310-7429)
//
// CONTENT:
//   1. ResolveCallee - Resolve the callee expression of a function call
//   2. ResolveCalleeIdent - Resolve identifier callee
//   3. ResolveCalleeQualified - Resolve qualified callee
//   4. ResolveCalleeExpr - Resolve arbitrary expression as callee
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"
#include "04_analysis/resolve/resolve_qual.h"

#include <type_traits>
#include <utility>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/visibility.h"
#include "04_analysis/resolve/scopes_lookup.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCallee() {
  SPEC_DEF("ResolveCallee", "5.1.7");
  SPEC_DEF("ResolveCalleeIdent", "5.1.3");
  SPEC_DEF("ResolveCalleeQualified", "5.1.6");
  SPEC_DEF("ResolveCalleeExpr", "5.1.7");
  SPEC_DEF("Callable", "5.1.6");
}

// -----------------------------------------------------------------------------
// Path Helpers
// -----------------------------------------------------------------------------

ast::Path FullPath(const ast::Path& path, std::string_view name) {
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

// -----------------------------------------------------------------------------
// Record Path Resolution Helper
// -----------------------------------------------------------------------------
// Checks if a qualified path refers to a record type.
// Used to determine if a call-like syntax is a record constructor.
// -----------------------------------------------------------------------------

const ast::RecordDecl* FindRecordDecl(const ScopeContext& ctx,
                                       const ast::TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

// -----------------------------------------------------------------------------
// ResolveQualContext Builder
// -----------------------------------------------------------------------------

ResolveQualContext BuildResolveQualContext(ResolveContext& ctx) {
  ResolveQualContext qual_ctx;
  qual_ctx.ctx = ctx.ctx;
  qual_ctx.name_maps = ctx.name_maps;
  qual_ctx.module_names = ctx.module_names;
  qual_ctx.can_access = ctx.can_access;
  qual_ctx.language_service = ctx.language_service;
  qual_ctx.resolve_expr = [](const ScopeContext& qctx,
                             const NameMapTable& name_maps,
                             const source::ModuleNames& module_names,
                             LanguageServiceIndex* language_service,
                             const ast::ExprPtr& expr) {
    ResolveContext local_ctx;
    local_ctx.ctx = const_cast<ScopeContext*>(&qctx);
    local_ctx.name_maps = &name_maps;
    local_ctx.module_names = &module_names;
    local_ctx.can_access = CanAccess;
    local_ctx.language_service = language_service;
    const auto resolved = ResolveExpr(local_ctx, expr);
    return ResolveExprResult{resolved.ok, resolved.diag_id, resolved.value};
  };
  qual_ctx.resolve_type_path = [](const ScopeContext& qctx,
                                  const NameMapTable& name_maps,
                                  const source::ModuleNames& module_names,
                                  const ast::TypePath& path) {
    ResolveContext local_ctx;
    local_ctx.ctx = const_cast<ScopeContext*>(&qctx);
    local_ctx.name_maps = &name_maps;
    local_ctx.module_names = &module_names;
    local_ctx.can_access = CanAccess;
    const auto resolved = ResolveTypePath(local_ctx, path);
    return ResolveTypePathResult{resolved.ok, resolved.diag_id, resolved.value};
  };
  return qual_ctx;
}

}  // namespace

// =============================================================================
// Public Interface
// =============================================================================

// -----------------------------------------------------------------------------
// ResolveCallee
// -----------------------------------------------------------------------------
// Resolves the callee expression of a function call.
// The callee can be:
//   - An identifier (simple function call)
//   - A qualified name (module::function call)
//   - An arbitrary expression (function value call)
//
// Implements (Resolve-Callee) from §5.1.3/§5.1.6:
//   (Resolve-Callee-Ident):
//     Γ ⊢ ResolveValueName(x) ⇓ ent ∧ Callable(ent)
//     → Γ ⊢ ResolveCallee(Ident(x)) ⇓ ok
//
//   (Resolve-Callee-Qualified):
//     Γ ⊢ ResolveQualified(path, name, Value, CanAccess) ⇓ ent ∧
//     Callable(ent)
//     → Γ ⊢ ResolveCallee(Qualified(path, name)) ⇓ ok
//
// Note: Callable validation is deferred to type checking. At resolution time,
// we only perform name lookup without verifying the entity is actually callable.
// -----------------------------------------------------------------------------

ResExprResult ResolveCallee(ResolveContext& ctx,
                            const ast::ExprPtr& callee,
                            const std::vector<ast::Arg>& args) {
  SpecDefsCallee();
  if (!callee) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }

  return std::visit(
      [&](const auto& node) -> ResExprResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          // Identifier callee - look up as value name
          const auto ent = ResolveValueName(*ctx.ctx, node.name);
          if (ent.has_value()) {
            SPEC_RULE("ResolveCallee-Ident-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }

          // Built-in record constructors are resolved through the centralized
          // capability/type registry rather than ad-hoc per-name checks.
          if (args.empty() &&
              LookupBuiltinRecordCtorPath(node.name).has_value()) {
            SPEC_RULE("ResolveCallee-Ident-Record");
            return {true, std::nullopt, std::nullopt, callee};
          }

          // Check if identifier refers to a record type (constructor call)
          if (args.empty()) {
            const auto type_ent = ResolveTypeName(*ctx.ctx, node.name);
            if (type_ent.has_value()) {
              const auto name = type_ent->target_opt.value_or(node.name);
              ast::ModulePath module;
              if (type_ent->origin_opt.has_value()) {
                module = *type_ent->origin_opt;
              }
              const auto path = FullPath(module, name);
              const auto it = ctx.ctx->sigma.types.find(PathKeyOf(path));
              if (it != ctx.ctx->sigma.types.end() &&
                  std::holds_alternative<ast::RecordDecl>(it->second)) {
                SPEC_RULE("ResolveCallee-Ident-Record");
                return {true, std::nullopt, std::nullopt, callee};
              }
            }
          }

          // Fall back to general expression resolution
          return ResolveExpr(ctx, callee);

        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          TypePath modal_path;
          modal_path.reserve(node.path.size());
          for (const auto& seg : node.path) {
            modal_path.push_back(seg);
          }
          if (IsBuiltinModalTypePath(modal_path) &&
              IsBuiltinModalMemberName(modal_path, node.name)) {
            SPEC_RULE("ResolveCallee-Path-Builtin");
            return {true, std::nullopt, std::nullopt, callee};
          }
          // Path expression callee - qualified lookup
          const auto value = ResolveQualified(
              *ctx.ctx, *ctx.name_maps, *ctx.module_names, node.path, node.name,
              EntityKind::Value, ctx.can_access);
          if (value.ok) {
            SPEC_RULE("ResolveCallee-Path-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }

          // Check if path refers to a record type (qualified constructor)
          const ResolveQualContext qual_ctx = BuildResolveQualContext(ctx);
          if (args.empty()) {
            const auto record = ResolveRecordPath(qual_ctx, node.path, node.name);
            if (record.ok) {
              SPEC_RULE("ResolveCallee-Path-Record");
              return {true, std::nullopt, std::nullopt, callee};
            }
          }

          // Fall back to general expression resolution
          return ResolveExpr(ctx, callee);

        } else {
          // Other expressions - resolve normally
          SPEC_RULE("ResolveCallee-Other");
          return ResolveExpr(ctx, callee);
        }
      },
      callee->node);
}

// -----------------------------------------------------------------------------
// ResolveCalleeSimple
// -----------------------------------------------------------------------------
// Simplified callee resolution without argument context.
// Used when we don't have argument information available.
// -----------------------------------------------------------------------------

ResExprResult ResolveCalleeSimple(ResolveContext& ctx,
                                  const ast::ExprPtr& callee) {
  SpecDefsCallee();
  if (!callee) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }

  return std::visit(
      [&](const auto& node) -> ResExprResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          // Identifier callee - look up as value name
          const auto ent = ResolveValueName(*ctx.ctx, node.name);
          if (ent.has_value()) {
            SPEC_RULE("ResolveCalleeSimple-Ident-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }

          // Fall back to general expression resolution
          return ResolveExpr(ctx, callee);

        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          TypePath modal_path;
          modal_path.reserve(node.path.size());
          for (const auto& seg : node.path) {
            modal_path.push_back(seg);
          }
          if (IsBuiltinModalTypePath(modal_path) &&
              IsBuiltinModalMemberName(modal_path, node.name)) {
            SPEC_RULE("ResolveCalleeSimple-Path-Builtin");
            return {true, std::nullopt, std::nullopt, callee};
          }
          // Path expression callee - qualified lookup
          const auto value = ResolveQualified(
              *ctx.ctx, *ctx.name_maps, *ctx.module_names, node.path, node.name,
              EntityKind::Value, ctx.can_access);
          if (value.ok) {
            SPEC_RULE("ResolveCalleeSimple-Path-Value");
            return {true, std::nullopt, std::nullopt, callee};
          }

          // Fall back to general expression resolution
          return ResolveExpr(ctx, callee);

        } else {
          // Other expressions - resolve normally
          SPEC_RULE("ResolveCalleeSimple-Other");
          return ResolveExpr(ctx, callee);
        }
      },
      callee->node);
}

// -----------------------------------------------------------------------------
// ResolveMethodReceiver
// -----------------------------------------------------------------------------
// Resolves the receiver expression of a method call.
// Method name resolution is deferred to type checking.
// -----------------------------------------------------------------------------

ResExprResult ResolveMethodReceiver(ResolveContext& ctx,
                                    const ast::ExprPtr& receiver) {
  SpecDefsCallee();
  if (!receiver) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }

  // Simply resolve the receiver as a normal expression
  const auto resolved = ResolveExpr(ctx, receiver);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  SPEC_RULE("ResolveMethodReceiver");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

}  // namespace cursive::analysis
