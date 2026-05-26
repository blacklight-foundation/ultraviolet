#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerIndexAccess(const ast::IndexAccessExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
