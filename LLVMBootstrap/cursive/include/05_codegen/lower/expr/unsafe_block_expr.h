#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerUnsafeBlockExpr(const ast::UnsafeBlockExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
