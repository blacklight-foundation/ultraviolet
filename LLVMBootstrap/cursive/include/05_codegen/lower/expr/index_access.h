#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerIndexAccess(const ast::IndexAccessExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
