#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerPropagateExpr(const ast::PropagateExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
