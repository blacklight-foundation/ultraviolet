// =============================================================================
// ABI Parameter Passing (§6.2.3)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.3 ABI Parameter and Return Passing
//   - ByValOk predicate
//   - ABI-Param-ByRef-Alias rule
//   - ABI-Param-ByRef-Move rule
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

// ByValOk(T) iff sizeof(T) <= ByValMax and alignof(T) <= PtrAlign.
bool ByValOk(const analysis::ScopeContext& ctx, const analysis::TypeRef& type) {
  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  const auto align = ::ultraviolet::analysis::layout::AlignOf(ctx, type);
  if (!size.has_value() || !align.has_value()) {
    return false;
  }
  return *size <= kByValMax &&
         *align <= ::ultraviolet::analysis::layout::PtrAlign(ctx);
}

// ABIParam(mode, T) => PassKind
std::optional<PassKind> ABIParam(const analysis::ScopeContext& ctx,
                                 std::optional<analysis::ParamMode> mode,
                                 const analysis::TypeRef& type,
                                 ABIParamPolicy policy) {
  if (!type) {
    return std::nullopt;
  }

  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  if (!size.has_value()) {
    return std::nullopt;
  }

  if (policy == ABIParamPolicy::ModeAware) {
    if (!mode.has_value()) {
      SPEC_RULE("ABI-Param-ByRef-Alias");
      return PassKind::ByRef;
    }
    SPEC_RULE("ABI-Param-ByRef-Move");
    return PassKind::ByRef;
  }

  if (*size == 0 || ByValOk(ctx, type)) {
    SPEC_RULE("ABI-ForeignParam-ByValue");
    return PassKind::ByValue;
  }

  SPEC_RULE("ABI-ForeignParam-ByRef");
  return PassKind::ByRef;
}

}  // namespace ultraviolet::codegen
