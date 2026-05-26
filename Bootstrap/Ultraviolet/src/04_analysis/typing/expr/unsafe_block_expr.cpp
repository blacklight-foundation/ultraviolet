// =================================================================
// File: 04_analysis/typing/expr/unsafe_block_expr.cpp
// Construct: Unsafe Block Expression Type Checking
// Spec Section: 5.5
// Spec Rules: T-Unsafe-Block
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/transmute_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsUnsafeBlock() {
  SPEC_DEF("T-Unsafe-Block", "5.5");
}

}  // namespace

// Section 5.5 Unsafe Block Expression Typing
//
// Typing rule (T-Unsafe-Block):
// InUnsafe' = true
// Gamma; InUnsafe' |- body : T
// --------------------------------------------------
// Gamma |- unsafe { body } : T
//
// unsafe block enables:
// - Raw pointer dereference (*ptr where ptr: *imm T or *mut T)
// - Calls to extern procedures
// - transmute expressions
// - Bidirectional control characters (LexSecure lifted)
// - Manual memory operations
//
// The block type is the type of the body expression.
// unsafe does not change the type, only enables certain operations.
//
ExprTypeResult TypeUnsafeBlockExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::UnsafeBlockExpr& expr,
                                       const TypeEnv& env,
                                       const ExprTypeFn& type_expr,
                                       const IdentTypeFn& type_ident,
                                       const PlaceTypeFn& type_place) {
  SPEC_RULE("T-Unsafe-Block");
  ExprTypeResult result;

  // Handle empty body case
  if (!expr.block) {
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }

  // Create unsafe context for body type checking
  // The unsafe flag is tracked via context/span mechanism
  // and checked by operations that require unsafe

  // Type check the block body
  // The ScopeContext tracks unsafe spans, which will be checked
  // by operations like transmute, raw pointer deref, extern calls, etc.
  ExprTypeResult body_result = TypeBlock(ctx, type_ctx, *expr.block, env,
                                         type_expr, type_ident, type_place);
  if (!body_result.ok) {
    result.diag_id = body_result.diag_id;
    return result;
  }

  EmitInvalidTransmuteTargetWarningsInBlock(ctx, type_ctx, *expr.block, env);

  result.ok = true;
  result.type = body_result.type;
  return result;
}

}  // namespace ultraviolet::analysis::expr
