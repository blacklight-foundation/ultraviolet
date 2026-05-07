// =============================================================================
// ABI Type: Primitives (ABI-Prim)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Prim rule: ABITy(TypePrim(name)) => <::cursive::analysis::layout::PrimSize(ctx, name), ::cursive::analysis::layout::PrimAlign(ctx, name)>
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace cursive::codegen {

std::optional<ABIType> ABITyPrim(const analysis::ScopeContext& ctx,
                                 const analysis::TypePrim& prim) {
  SPEC_RULE("ABI-Prim");
  const auto size = ::cursive::analysis::layout::PrimSize(ctx, prim.name);
  const auto align = ::cursive::analysis::layout::PrimAlign(ctx, prim.name);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return ABIType{*size, *align};
}

}  // namespace cursive::codegen
