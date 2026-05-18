// =============================================================================
// ABI Return Passing (§6.2.3)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.3 ABI Return Passing
//   - ABI-Ret-ByValue rule
//   - ABI-Ret-ByRef rule (SRet)
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

// ABIRet(T) => PassKind
std::optional<PassKind> ABIRet(const analysis::ScopeContext& ctx,
                               const analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }

  const auto size = ::ultraviolet::analysis::layout::SizeOf(ctx, type);
  if (!size.has_value()) {
    return std::nullopt;
  }

  // (ABI-Ret-ByValue)
  // Zero-sized or ByValOk return types are passed by value.
  if (*size == 0 || ByValOk(ctx, type)) {
    SPEC_RULE("ABI-Ret-ByValue");
    return PassKind::ByValue;
  }

  // (ABI-Ret-ByRef)
  // Large return types use sret (struct return).
  SPEC_RULE("ABI-Ret-ByRef");
  return PassKind::SRet;
}

// ABICall computes the full call ABI info
std::optional<ABICallInfo> ABICall(
    const analysis::ScopeContext& ctx,
    const std::vector<std::pair<std::optional<analysis::ParamMode>, analysis::TypeRef>>& params,
    const analysis::TypeRef& ret,
    ABIParamPolicy policy) {
  SPEC_RULE("ABI-Call");

  ABICallInfo info;
  info.param_kinds.reserve(params.size());

  for (const auto& [mode, type] : params) {
    const auto kind = ABIParam(ctx, mode, type, policy);
    if (!kind.has_value()) {
      return std::nullopt;
    }
    info.param_kinds.push_back(*kind);
  }

  const auto ret_kind = ABIRet(ctx, ret);
  if (!ret_kind.has_value()) {
    return std::nullopt;
  }
  info.ret_kind = *ret_kind;
  info.has_sret = (*ret_kind == PassKind::SRet);

  return info;
}

}  // namespace ultraviolet::codegen
