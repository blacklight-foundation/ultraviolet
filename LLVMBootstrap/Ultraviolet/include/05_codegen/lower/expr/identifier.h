#pragma once

// =============================================================================
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16057-16060: (Lower-Expr-Ident-Local)
//     Gamma |- ResolveValueName(x) => ent    ent.origin_opt = bottom
//     Gamma |- LowerReadPlace(Identifier(x)) => <IR, v>
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(Identifier(x)) => <IR, v>
//
//   - Lines 16062-16065: (Lower-Expr-Ident-Path)
//     Gamma |- ResolveValueName(x) => ent    ent.origin_opt = mp
//     name = (ent.target_opt if present, else x)    PathOfModule(mp) = path
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(Identifier(x)) => <ReadPathIR(path, name), v>
//
// Identifier expressions resolve to either:
//   1. Local bindings - IRReadVar (no path, direct local access)
//   2. Captures - Load from captured environment in spawn/dispatch bodies
//   3. Globals/statics - IRReadPath (module path + name)
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower an identifier expression to IR.
// Implements the (Lower-Expr-Ident-Local) and (Lower-Expr-Ident-Path) rules.
//
// Parameters:
//   expr - The full expression (for type lookup)
//   ident - The identifier expression node
//   ctx - Lowering context
//
// Returns:
//   LowerResult with appropriate IR:
//   - For local bindings: IRReadVar + local IRValue
//   - For captures: IRReadPtr sequence + loaded value
//   - For globals: IRReadPath + symbol IRValue
LowerResult LowerIdentifier(const ast::Expr& expr,
                            const ast::IdentifierExpr& ident,
                            LowerCtx& ctx);

}  // namespace ultraviolet::codegen
