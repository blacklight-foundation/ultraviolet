// =============================================================================
// region_stmt.cpp - Region statement typing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.5: Memory and Pointers - Region statements
//   - region { body } creates arena scope
//   - region (options) { body } with configuration
//   - region as r { body } with named region binding
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/type_infer.h"

namespace ultraviolet::analysis {

// IntroResult and IntroAll are declared in type_stmt.h

namespace {

static inline void SpecDefsRegionStmt() {
  SPEC_DEF("T-RegionStmt", "5.2.17");
  SPEC_DEF("RegionBind", "5.2.17");
  SPEC_DEF("RegionOptsExpr", "5.2.17");
}

// Introduce a region binding
static IntroResult RegionBind(const TypeEnv& env,
                              const std::optional<std::string>& alias_opt) {
  std::string name = alias_opt.has_value() ? *alias_opt : FreshRegionName(env);
  std::vector<std::pair<std::string, TypeRef>> binds;
  binds.emplace_back(name, RegionActiveTypeRef());
  return IntroAll(env, binds, ast::Mutability::Let, false);
}

}  // namespace

StmtTypeResult TypeRegionStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::RegionStmt& node,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place) {
  SpecDefsRegionStmt();

  // Get options expression (use default if none provided)
  const ast::ExprPtr opts_expr =
      node.opts_opt ? node.opts_opt : MakeDefaultRegionOptionsExpr();

  // Check that options expression has type RegionOptions
  const auto check = CheckExpr(ctx, opts_expr, RegionOptionsTypeRef(),
                               type_expr, type_place, type_ident);
  if (!check.ok) {
    return {false, check.diag_id, {}, {}};
  }

  TypeEnv region_env = PushScope(env);

  // Introduce region binding (named or anonymous)
  const auto intro = RegionBind(region_env, node.alias_opt);
  if (!intro.ok) {
    return {false, intro.diag_id, {}, {}};
  }

  if (!node.body) {
    return {false, std::nullopt, {}, {}};
  }

  const auto typed = TypeScopedStmtBody(ctx, type_ctx, *node.body, intro.env,
                                        env.scopes.size(), type_expr,
                                        type_ident, type_place);
  if (!typed.ok) {
    return {false, typed.diag_id, {}, {}, typed.diag_detail, typed.diag_span};
  }

  SPEC_RULE("T-RegionStmt");
  return typed;
}

}  // namespace ultraviolet::analysis
