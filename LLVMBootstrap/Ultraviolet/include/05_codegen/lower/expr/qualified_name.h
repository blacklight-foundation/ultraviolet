#pragma once

// =============================================================================
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16067-16070: (Lower-Expr-Path)
//     Gamma |- ResolveValuePath(path) => ent
//     name = (ent.target_opt if present, else final(path))
//     PathOfModule(ent.origin_opt) = mod_path
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(QualifiedName(path)) => <ReadPathIR(mod_path, name), v>
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower a qualified name expression to IR.
// Implements the (Lower-Expr-Path) rule from the spec.
//
// Parameters:
//   expr - The qualified name expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with IR to read from module path and a symbol value.
LowerResult LowerQualifiedName(const ast::QualifiedNameExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
