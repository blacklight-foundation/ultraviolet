// =============================================================================
// ABI Type: Ranges (ABI-Range)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Range rule: ABITy(range-family) => <sizeof(range), alignof(range)>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

#include <variant>

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyRange(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type) {
  SPEC_RULE("ABI-Range");
  SPEC_RULE("rule.24.ABI-Range");
  if (std::holds_alternative<analysis::TypeRangeInclusive>(type->node)) {
    SPEC_RULE("rule.24.ABI-RangeInclusive");
  } else if (std::holds_alternative<analysis::TypeRangeFrom>(type->node)) {
    SPEC_RULE("rule.24.ABI-RangeFrom");
  } else if (std::holds_alternative<analysis::TypeRangeTo>(type->node)) {
    SPEC_RULE("rule.24.ABI-RangeTo");
  } else if (std::holds_alternative<analysis::TypeRangeToInclusive>(type->node)) {
    SPEC_RULE("rule.24.ABI-RangeToInclusive");
  } else if (std::holds_alternative<analysis::TypeRangeFull>(type->node)) {
    SPEC_RULE("rule.24.ABI-RangeFull");
  }
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace ultraviolet::codegen
