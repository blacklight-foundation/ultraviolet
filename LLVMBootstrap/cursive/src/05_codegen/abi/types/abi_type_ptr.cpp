// =============================================================================
// ABI Type: Safe Pointers (ABI-Ptr)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Ptr rule: ABITy(TypePtr(U, s)) => <PtrSize, PtrAlign>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace cursive::codegen {

std::optional<ABIType> ABITyPtr(const analysis::ScopeContext& ctx,
                                const analysis::TypePtr& /*ptr*/) {
  SPEC_RULE("ABI-Ptr");
  return ABIType{::cursive::analysis::layout::PtrSize(ctx),
                 ::cursive::analysis::layout::PtrAlign(ctx)};
}

}  // namespace cursive::codegen
