// =============================================================================
// ABI Type: Modal Types (ABI-Modal)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Modal rule: ABITy(TypeModalState) => ModalLayout
//   - Modal types have a discriminant and state-specific payload.
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyModalState(const analysis::ScopeContext& ctx,
                                       const analysis::TypeRef& type) {
  SPEC_RULE("ABI-Modal");
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace ultraviolet::codegen
