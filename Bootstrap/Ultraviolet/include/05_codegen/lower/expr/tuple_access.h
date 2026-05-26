#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerTupleAccess(const ast::TupleAccessExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
