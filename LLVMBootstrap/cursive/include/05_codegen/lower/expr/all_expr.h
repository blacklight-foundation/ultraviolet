#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerAllExpr(const ast::AllExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
