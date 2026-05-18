// =============================================================================
// ABI Type: Arrays (ABI-Array)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Array rule: ABITy(TypeArray(T, e)) => <sizeof(T) * e, alignof(T)>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyArray(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type) {
  SPEC_RULE("ABI-Array");
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace ultraviolet::codegen
