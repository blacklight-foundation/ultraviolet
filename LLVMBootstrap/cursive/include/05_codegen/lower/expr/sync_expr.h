#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

LowerResult LowerSyncExpr(const ast::SyncExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
