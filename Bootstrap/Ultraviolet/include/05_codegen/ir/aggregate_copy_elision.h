#pragma once

#include <optional>

#include "05_codegen/ir/ir_model.h"

namespace ultraviolet::codegen {

struct LowerCtx;

std::optional<IRAggregateCopyElision> AnalyzeAggregateCopyElision(
    const ProcIR& proc,
    const LowerCtx& ctx);

}  // namespace ultraviolet::codegen
