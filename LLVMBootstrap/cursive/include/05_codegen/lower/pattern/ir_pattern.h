#pragma once

#include "02_source/ast/ast.h"
#include "05_codegen/ir/ir_model.h"

namespace cursive::codegen {

struct LowerCtx;

IRRangeKind ToIRRangeKind(ast::RangeKind kind);
IRFenceOrder ToIRFenceOrder(ast::FenceOrder order);
IRPatternPtr LowerIRPattern(const ast::Pattern& pattern, LowerCtx& ctx);

}  // namespace cursive::codegen
