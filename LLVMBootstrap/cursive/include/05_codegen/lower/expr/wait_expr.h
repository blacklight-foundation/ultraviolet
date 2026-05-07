#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerWaitExpr(const ast::WaitExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
