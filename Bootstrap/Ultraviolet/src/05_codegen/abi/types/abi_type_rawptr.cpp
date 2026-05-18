// =============================================================================
// ABI Type: Raw Pointers (ABI-RawPtr)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-RawPtr rule: ABITy(TypeRawPtr(q, U)) => <PtrSize, PtrAlign>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyRawPtr(const analysis::ScopeContext& ctx,
                                   const analysis::TypeRawPtr& /*rawptr*/) {
  SPEC_RULE("ABI-RawPtr");
  return ABIType{::ultraviolet::analysis::layout::PtrSize(ctx),
                 ::ultraviolet::analysis::layout::PtrAlign(ctx)};
}

}  // namespace ultraviolet::codegen
