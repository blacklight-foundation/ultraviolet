#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerAllExpr(const ast::AllExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
