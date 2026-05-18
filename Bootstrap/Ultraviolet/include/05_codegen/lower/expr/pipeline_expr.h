#pragma once

// =============================================================================
// Pipeline Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Lines 16288-16302 (Pipeline Lowering)
//   - Lower-Expr-Pipeline: e1 => e2 desugars to function/closure application
//   - IsFunc case: CallIR(v_2, [v_1])
//   - IsClosure case: IndirectCall(code, [env, v_1])
//
// The pipeline operator => passes the LHS value as the first argument to
// the RHS function or closure.
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

/// Lower a pipeline expression (e1 => e2) to IR.
///
/// Pipeline expressions desugar to function application:
///   e1 => e2  =>  e2(e1)
///
/// For functions: creates CallIR(v_2, [v_1])
/// For closures: creates IndirectCall(code, [env, v_1])
///
/// Returns the IR sequence and result value.
LowerResult LowerPipelineExpr(const ast::BinaryExpr& expr, LowerCtx& ctx);

/// Overload accepting the AST PipelineExpr node directly
LowerResult LowerPipelineExpr(const ast::PipelineExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
