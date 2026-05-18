// =============================================================================
// IR Builder Implementation
// =============================================================================
//
// This file provides non-inline implementations for the IR builder API.
// Most builder functions are inline in ir_builder.h for performance.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.0 Codegen Model and Judgments (lines 14196-14347)
//   - ProcIR structure (line 14234)
//   - CodegenParams (line 14237)
//   - MethodParams, ClassMethodParams (lines 14239-14243)
//   - StateMethodParams, TransitionParams (lines 14245-14246)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/ir_model.cpp
//   - SeqIR builder functions (lines 379-413)
//
// =============================================================================

#include "05_codegen/ir/ir_builder.h"

namespace ultraviolet::codegen {

// Note: All builder functions are inline in ir_builder.h.
//
// The SeqIR functions are declared in ir_model.h and implemented
// in ir_model.cpp (see that file for the sequence builder logic).
//
// This file exists primarily for:
// 1. Potential future non-inline implementations
// 2. Explicit template instantiations if needed
// 3. Any complex builder logic that benefits from being out-of-line

}  // namespace ultraviolet::codegen
