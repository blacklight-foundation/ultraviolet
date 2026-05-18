#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

LowerResult LowerEntryExpr(const ast::EntryExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
