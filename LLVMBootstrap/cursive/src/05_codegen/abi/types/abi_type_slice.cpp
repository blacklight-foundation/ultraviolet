// =============================================================================
// ABI Type: Slices (ABI-Slice)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Slice rule: ABITy(TypeSlice(U)) => <2 * PtrSize, PtrAlign>
//   - Slices are fat pointers: (pointer, length).
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace cursive::codegen {

std::optional<ABIType> ABITySlice(const analysis::ScopeContext& ctx,
                                  const analysis::TypeSlice& /*slice*/) {
  SPEC_RULE("ABI-Slice");
  return ABIType{2 * ::cursive::analysis::layout::PtrSize(ctx),
                 ::cursive::analysis::layout::PtrAlign(ctx)};
}

}  // namespace cursive::codegen
