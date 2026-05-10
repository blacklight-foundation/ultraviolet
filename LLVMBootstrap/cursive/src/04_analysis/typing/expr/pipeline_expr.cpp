// =============================================================================
// File: 04_analysis/typing/expr/pipeline_expr.cpp
// Pipeline Expression Typing
// Spec Section: CursiveSpecification.md Section 5.2 (Expressions)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 8848-8872
//
// Type checking rules:
//   - T-Pipeline: e1 => e2 where e2 is callable taking e1's type
//   - T-Pipeline-NotCallable-Err: e2 is not a function or closure
//   - T-Pipeline-TypeMismatch-Err: e1's type not compatible with e2's parameter
//   - T-Pipeline-ArgCount-Err: e2 doesn't take exactly one argument
//
// Grammar:
//   pipeline_expr ::= base_postfix_expr ("=>" base_postfix_expr)*
//
// =============================================================================

#include <string_view>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsPipeline() {
  SPEC_DEF("T-Pipeline", "CursiveSpecification.md Lines 8848-8852");
  SPEC_DEF("T-Pipeline-NotCallable-Err", "CursiveSpecification.md Lines 8855-8859");
  SPEC_DEF("T-Pipeline-TypeMismatch-Err", "CursiveSpecification.md Lines 8861-8866");
  SPEC_DEF("T-Pipeline-ArgCount-Err", "CursiveSpecification.md Lines 8868-8872");
}

}  // namespace

// =============================================================================
// TypePipelineExpr - Type check a pipeline expression (e1 => e2)
// =============================================================================
//
// SPEC_RULE("T-Pipeline"):
//   Gamma; R; L |- e_1 : T_1
//   Gamma; R; L |- e_2 : T_f
//   (T_f = TypeFunc([(m, T_p)], R_f) or T_f = TypeClosure([(m, T_p)], R_f, _))
//   Gamma |- T_1 <: T_p
//   ---------------------------
//   Gamma; R; L |- PipelineExpr(e_1, e_2) : R_f
//
// The pipeline operator passes the LHS as the single argument to RHS.
// RHS must be a function or closure taking exactly one parameter.
//
// NOTE: Uses StmtTypeContext and TypeEnv from the active type system.
//
// =============================================================================

ExprTypeResult TypePipelineExpr(const ast::PipelineExpr& expr,
                                const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const TypeEnv& env) {
  SpecDefsPipeline();
  ExprTypeResult result;

  if (!expr.lhs || !expr.rhs) {
    result.diag_id = "E-SEM-2538";
    return result;
  }

  // Type the LHS expression
  ExprTypeResult lhs_result = TypeExpr(ctx, type_ctx, expr.lhs, env);
  if (!lhs_result.type) {
    result.diag_id = "E-SEM-2538";
    return result;
  }

  // Type the RHS expression (should be callable)
  ExprTypeResult rhs_result = TypeExpr(ctx, type_ctx, expr.rhs, env);
  if (!rhs_result.type) {
    result.diag_id = "E-SEM-2538";
    return result;
  }

  TypeRef stripped = StripPerm(rhs_result.type);
  if (!stripped) {
    result.diag_id = "E-SEM-2538";
    return result;
  }

  // Check if RHS is a function type
  if (const auto* func = std::get_if<TypeFunc>(&stripped->node)) {
    SPEC_RULE("T-Pipeline");

    // Check argument count
    if (func->params.size() != 1) {
      SPEC_RULE("T-Pipeline-ArgCount-Err");
      result.diag_id = "E-SEM-2539";
      return result;
    }

    // LHS must be a subtype of RHS parameter type.
    const auto sub = Subtyping(ctx, lhs_result.type, func->params[0].type);
    if (!sub.ok || !sub.subtype) {
      SPEC_RULE("T-Pipeline-TypeMismatch-Err");
      result.diag_id = "E-SEM-2539";
      return result;
    }

    // Result type is the function's return type
    result.ok = true;
    result.type = func->ret;
    return result;
  }

  // Check if RHS is a closure type
  if (const auto* closure = std::get_if<TypeClosure>(&stripped->node)) {
    SPEC_RULE("T-Pipeline");

    // Check argument count
    if (closure->params.size() != 1) {
      SPEC_RULE("T-Pipeline-ArgCount-Err");
      result.diag_id = "E-SEM-2539";
      return result;
    }

    // LHS must be a subtype of RHS parameter type.
    const auto sub = Subtyping(ctx, lhs_result.type, closure->params[0].second);
    if (!sub.ok || !sub.subtype) {
      SPEC_RULE("T-Pipeline-TypeMismatch-Err");
      result.diag_id = "E-SEM-2539";
      return result;
    }

    // Result type is the closure's return type
    result.ok = true;
    result.type = closure->ret;
    return result;
  }

  // RHS is not callable
  SPEC_RULE("T-Pipeline-NotCallable-Err");
  result.diag_id = "E-SEM-2538";
  return result;
}

}  // namespace cursive::analysis
