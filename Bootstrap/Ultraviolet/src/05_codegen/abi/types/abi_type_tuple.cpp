// =============================================================================
// ABI Type: Tuples (ABI-Tuple)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Tuple rule: ABITy(TypeTuple([T1,...,Tn])) => TupleLayout.layout
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyTuple(const analysis::ScopeContext& ctx,
                                  const analysis::TypeTuple& tuple) {
  SPEC_RULE("ABI-Tuple");
  const auto layout = ::ultraviolet::analysis::layout::RecordLayoutOf(ctx, tuple.elements);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  return layout->layout;
}

}  // namespace ultraviolet::codegen
