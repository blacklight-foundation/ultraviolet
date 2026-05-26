// =============================================================================
// ABI Type: Safe Pointers (ABI-Ptr)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Ptr rule: ABITy(TypePtr(U, s)) => <PtrSize, PtrAlign>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyPtr(const analysis::ScopeContext& ctx,
                                const analysis::TypePtr& /*ptr*/) {
  SPEC_RULE("ABI-Ptr");
  return ABIType{::ultraviolet::analysis::layout::PtrSize(ctx),
                 ::ultraviolet::analysis::layout::PtrAlign(ctx)};
}

}  // namespace ultraviolet::codegen
