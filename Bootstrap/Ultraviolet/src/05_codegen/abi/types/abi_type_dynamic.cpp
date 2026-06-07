// =============================================================================
// ABI Type: Dynamic Objects (ABI-Dynamic)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Dynamic rule: ABITy(TypeDynamic(Cl)) => DynLayout(Cl)
//   - Dynamic objects are fat pointers: (data, vtable).
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

#include <string>

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyDynamic(const analysis::ScopeContext& ctx,
                                    const analysis::TypeDynamic& /*dyn*/) {
  SPEC_RULE("ABI-Dynamic");
  SPEC_RULE("rule.24.ABI-Dynamic");
  const auto dyn_layout = ::ultraviolet::analysis::layout::DynLayoutOf(ctx);
  if (core::Conformance::Enabled()) {
    std::string payload;
    payload.reserve(80);
    payload += "source=ABITyDynamic;operation=ABITy;layout=DynLayout;size=";
    payload += std::to_string(dyn_layout.layout.size);
    payload += ";align=";
    payload += std::to_string(dyn_layout.layout.align);
    core::Conformance::Record("rule.14.ABI-Dynamic", std::nullopt, payload);
  }
  return dyn_layout.layout;
}

}  // namespace ultraviolet::codegen
