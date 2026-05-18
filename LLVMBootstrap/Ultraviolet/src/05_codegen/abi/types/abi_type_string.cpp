// =============================================================================
// ABI Type: Strings and Bytes (ABI-StringBytes)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-StringBytes rule covers TypeString and TypeBytes in all states
//   - @View: fat pointer (ptr, len)
//   - @Managed: managed representation with capacity
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyString(const analysis::ScopeContext& ctx,
                                   const analysis::TypeRef& type) {
  SPEC_RULE("ABI-StringBytes");
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

std::optional<ABIType> ABITyBytes(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type) {
  SPEC_RULE("ABI-StringBytes");
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace ultraviolet::codegen
