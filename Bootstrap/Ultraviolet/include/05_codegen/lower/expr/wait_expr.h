#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerWaitExpr(const ast::WaitExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
