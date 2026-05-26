// =============================================================================
// File: 04_analysis/typing/expr/identifier.cpp
// Identifier Expression Typing
// Spec Section: 5.2.9
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.9: Type Inference (lines 9107-9173)
//   - Syn-Ident (lines 9123-9126): (x : T) in Gamma => Identifier(x) => T
//
// =============================================================================

#include "04_analysis/typing/expr/expr_common.h"

#include <optional>
#include <string>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsIdentifier() {
  SPEC_DEF("Syn-Ident", "5.2.9");
  SPEC_DEF("Ident-NotFound", "5.2.9");
  SPEC_DEF("Ident-NotValue", "5.2.9");
}

}  // namespace

// Type an identifier expression by looking up the binding in the environment
ExprTypeResult TypeIdentifierExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::IdentifierExpr& expr,
                                       const TypeEnv& env,
                                       const IdentTypeFn& type_ident) {
  SpecDefsIdentifier();
  (void)ctx;
  (void)type_ctx;

  ExprTypeResult result;

  // First try the type environment
  const auto binding = BindOf(env, expr.name);
  if (binding.has_value()) {
    if (binding->stale_after_release && !binding->stale_ok && type_ctx.diags) {
      if (auto diag = core::MakeDiagnosticById("W-CON-0011", core::Span{})) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
    SPEC_RULE("Syn-Ident");
    result.ok = true;
    result.type = binding->type;
    return result;
  }

  // Fall back to identifier resolution callback
  if (type_ident) {
    const auto ident_result = type_ident(expr.name);
    if (ident_result.ok) {
      SPEC_RULE("Syn-Ident");
      result.ok = true;
      result.type = ident_result.type;
      return result;
    }
    if (ident_result.diag_id.has_value()) {
      result.diag_id = ident_result.diag_id;
      return result;
    }
  }

  // Identifier not found
  SPEC_RULE("Ident-NotFound");
  result.diag_id = "Ident-NotFound";
  return result;
}

// Simplified overload without type_ident callback (uses env only)
ExprTypeResult TypeIdentifierExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::IdentifierExpr& expr,
                                   const TypeEnv& env) {
  SpecDefsIdentifier();
  (void)ctx;
  (void)type_ctx;

  ExprTypeResult result;

  const auto binding = BindOf(env, expr.name);
  if (binding.has_value()) {
    if (binding->stale_after_release && !binding->stale_ok && type_ctx.diags) {
      if (auto diag = core::MakeDiagnosticById("W-CON-0011", core::Span{})) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
    SPEC_RULE("Syn-Ident");
    result.ok = true;
    result.type = binding->type;
    return result;
  }

  SPEC_RULE("Ident-NotFound");
  result.diag_id = "Ident-NotFound";
  return result;
}

// Place typing for identifiers - identifier is always a place
PlaceTypeResult TypeIdentifierPlaceImpl(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::IdentifierExpr& expr,
                                         const TypeEnv& env,
                                         const IdentTypeFn& type_ident) {
  SpecDefsIdentifier();
  PlaceTypeResult result;

  const auto expr_result = TypeIdentifierExprImpl(ctx, type_ctx, expr, env, type_ident);
  if (!expr_result.ok) {
    result.diag_id = expr_result.diag_id;
    return result;
  }

  SPEC_RULE("P-Ident");
  result.ok = true;
  result.type = expr_result.type;
  // Note: PlaceTypeResult doesn't track is_place (all place results are places)
  return result;
}

}  // namespace ultraviolet::analysis::expr
