// =============================================================================
// ABI Type: Unions (ABI-Union)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Union rule: ABITy(TypeUnion([T1,...,Tn])) => ::ultraviolet::analysis::layout::UnionLayout.layout
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyUnion(const analysis::ScopeContext& ctx,
                                  const analysis::TypeUnion& uni) {
  SPEC_RULE("ABI-Union");
  const auto layout = ::ultraviolet::analysis::layout::UnionLayoutOf(ctx, uni);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  return layout->layout;
}

}  // namespace ultraviolet::codegen
