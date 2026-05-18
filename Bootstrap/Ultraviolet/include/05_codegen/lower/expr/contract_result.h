#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerResultExpr(const ast::ResultExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
