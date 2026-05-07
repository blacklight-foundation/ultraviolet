#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerTransmuteExpr(const ast::Expr& expr,
                               const ast::TransmuteExpr& transmute,
                               LowerCtx& ctx);

}  // namespace cursive::codegen
