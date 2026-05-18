#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerYieldFromExpr(const ast::YieldFromExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
