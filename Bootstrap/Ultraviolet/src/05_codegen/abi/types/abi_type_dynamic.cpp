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

namespace ultraviolet::codegen {

std::optional<ABIType> ABITyDynamic(const analysis::ScopeContext& ctx,
                                    const analysis::TypeDynamic& /*dyn*/) {
  SPEC_RULE("ABI-Dynamic");
  const auto dyn_layout = ::ultraviolet::analysis::layout::DynLayoutOf(ctx);
  return dyn_layout.layout;
}

}  // namespace ultraviolet::codegen
