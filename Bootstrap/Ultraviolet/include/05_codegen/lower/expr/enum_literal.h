#pragma once

// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16187-16193: (Lower-Expr-EnumLiteral)
//     Gamma |- ResolveEnumVariant(path) => (enum_ty, variant_idx, payload_ty_opt)
//     Gamma |- LowerExpr(payload_opt) => <IR_p, v_p>
//     Gamma |- EnumValue(enum_ty, variant_idx, v_p) => v
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(EnumLiteral(path, payload_opt)) => <IR_p, v>
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower an enum literal expression to IR.
// Implements the (Lower-Expr-EnumLiteral) rule from the spec.
//
// Parameters:
//   source_expr - The enclosing expression node used for analysis type lookup
//   expr        - The enum literal expression node
//   ctx         - Lowering context
//
// Returns:
//   LowerResult with IR for payload evaluation and an enum value.
LowerResult LowerEnumLiteral(const ast::Expr& source_expr,
                             const ast::EnumLiteralExpr& expr,
                             LowerCtx& ctx);

}  // namespace ultraviolet::codegen
