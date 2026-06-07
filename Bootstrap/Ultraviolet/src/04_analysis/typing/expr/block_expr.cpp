// =================================================================
// File: 04_analysis/typing/expr/block_expr.cpp
// Construct: Block Expression Type Checking
// Spec Section: 5.2.11
// Spec Rules: T-Block, StmtSeq-Empty, StmtSeq-Cons
// =================================================================

#include "04_analysis/typing/expr/block_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/typecheck.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsBlockExpr() {
  SPEC_DEF("T-Block", "5.2.11");
  SPEC_DEF("StmtSeq-Empty", "5.2.11");
  SPEC_DEF("StmtSeq-Cons", "5.2.11");
  SPEC_DEF("T-Unsafe-Expr", "5.2.11");
}

}  // namespace

// T-Block: Block expression typing
ExprTypeResult TypeBlockExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::BlockExpr& expr,
                                 const TypeEnv& env,
                                 const TypeExprFn& type_expr,
                                 const TypeIdentFn& type_ident,
                                 const PlaceTypeFn& type_place) {
  SpecDefsBlockExpr();

  if (!expr.block) {
    return {false, std::nullopt, {}};
  }

  StmtTypeContext body_ctx = type_ctx;
  body_ctx.env_ref = nullptr;
  return TypeBlock(ctx, body_ctx, *expr.block, env, type_expr,
                   type_ident, type_place);
}

// Check block expression against expected type
CheckResult CheckBlockExprImpl(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::BlockExpr& expr,
                               const TypeEnv& env,
                               const TypeRef& expected,
                               const TypeExprFn& type_expr,
                               const TypeIdentFn& type_ident,
                               const PlaceTypeFn& type_place) {
  SpecDefsBlockExpr();

  if (!expr.block) {
    return {};
  }

  StmtTypeContext body_ctx = type_ctx;
  body_ctx.env_ref = nullptr;
  return CheckBlock(ctx, body_ctx, *expr.block, env, expected, type_expr,
                    type_ident, type_place);
}

}  // namespace ultraviolet::analysis::expr
