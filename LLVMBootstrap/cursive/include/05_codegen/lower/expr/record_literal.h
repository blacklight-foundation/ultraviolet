#pragma once

// =============================================================================
// record_literal.h - Record Literal Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16085-16088: (Lower-Expr-Record)
//
// Declares the LowerRecord function for lowering record literal expressions.
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// =============================================================================
// Section 6.4 Lower-Expr-Record
// =============================================================================

/// Lower a record literal expression (RecordExpr).
///
/// Handles all record literal forms:
/// - Plain record types: Point{ x: 1, y: 2 }
/// - Generic record types: Container<i32>{ data: 42 }
/// - Modal state record types: Connection@Open{ socket: fd }
///
/// Field initializers are lowered in declaration order (LTR). The field values
/// are consumed by the record construction and are not tracked as temporaries.
///
/// @param expr The record expression AST node
/// @param ctx  The lowering context
/// @return IR to execute and the resulting record value
LowerResult LowerRecord(const ast::Expr& full_expr,
                        const ast::RecordExpr& expr,
                        LowerCtx& ctx);

}  // namespace cursive::codegen
