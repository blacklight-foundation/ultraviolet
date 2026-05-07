#pragma once

// Compatibility include surface for analysis callers. The canonical layout
// implementation lives in 04_analysis/layout and is shared with lowering,
// ABI, and backend code.

#include "04_analysis/layout/layout.h"

namespace cursive::analysis {

using layout::AlignOf;
using layout::Layout;
using layout::LayoutEnv;
using layout::LayoutEnvOf;
using layout::LayoutOf;
using layout::PrimAlign;
using layout::PrimSize;
using layout::PtrAlign;
using layout::PtrSize;
using layout::SizeOf;
using layout::kPtrAlign;
using layout::kPtrSize;

}  // namespace cursive::analysis
