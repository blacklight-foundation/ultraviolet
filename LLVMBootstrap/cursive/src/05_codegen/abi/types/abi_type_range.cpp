// =============================================================================
// ABI Type: Ranges (ABI-Range)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Range rule: ABITy(range-family) => <sizeof(range), alignof(range)>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace cursive::codegen {

std::optional<ABIType> ABITyRange(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type) {
  SPEC_RULE("ABI-Range");
  const auto size = ::cursive::analysis::layout::SizeOf(ctx, type);
  const auto align = ::cursive::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace cursive::codegen
