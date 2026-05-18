#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Section 6.4 Array Literal Lowering
// =============================================================================

// Lower-Expr-Array - Lower a segmented array literal expression.
// Lowers each explicit or repeated segment left-to-right, then produces a
// synthetic array value tracked via DerivedValueInfo.
LowerResult LowerArrayLiteral(const ast::ArrayExpr& expr, LowerCtx& ctx);

// Helper for internal ArrayRepeatExpr producers.
LowerResult LowerArrayRepeat(const ast::ArrayRepeatExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
