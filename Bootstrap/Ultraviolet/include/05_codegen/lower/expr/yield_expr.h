#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerYieldExpr(const ast::YieldExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
