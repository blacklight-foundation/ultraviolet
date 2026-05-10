#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// §6.4 Lower-Expr-Tuple - Lower a tuple literal expression
// Lowers each element expression left-to-right via LowerList, then
// produces a synthetic tuple value tracked via DerivedValueInfo.
LowerResult LowerTuple(const ast::TupleExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
