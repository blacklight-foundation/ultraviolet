// =================================================================
// File: 04_analysis/typing/expr/closure_expr.h
// Construct: Closure Expression Type Checking
// Spec Section: CursiveSpecification.md Section 5.2 (Closures)
// =================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 8789-8846
//
// Type checking rules:
//   - T-Closure-Local: Non-escaping closure with local captures
//   - T-Closure-Escaping: Escaping closure requires shared deps annotation
//   - T-ClosureCall: Closure invocation typing
//
// Grammar:
//   closure_expr       ::= "|" closure_param_list? "|" ("->" type)? closure_body
//   closure_param_list ::= closure_param ("," closure_param)*
//   closure_param      ::= "move"? identifier (":" type)?
//   closure_body       ::= expression | block_expr
//
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// =============================================================================
// TypeClosureExpr - Type check a closure expression
// =============================================================================
//
// Parameters:
//   expr - The closure expression AST node
//   ctx - The scope context for type resolution
//
// Returns:
//   ExprTypeResult containing the closure type or error diagnostic
//
// =============================================================================

ExprTypeResult TypeClosureExpr(const ast::ClosureExpr& expr,
                               const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const TypeEnv& env,
                               const ExprTypeFn& type_expr,
                               const IdentTypeFn& type_ident,
                               const PlaceTypeFn& type_place);

}  // namespace cursive::analysis::expr
