#pragma once

#include <optional>

#include "05_codegen/ir/ir_model.h"

namespace cursive::codegen {

struct LowerCtx;

std::optional<IRAggregateCopyElision> AnalyzeAggregateCopyElision(
    const ProcIR& proc,
    const LowerCtx& ctx);

}  // namespace cursive::codegen
