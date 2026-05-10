// =============================================================================
// ABI Lowering Coordination (§6.2)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2 ABI Lowering (Cursive0)
//   - CallConvDefault = Cursive0ABI
//
// This file provides high-level ABI coordination. LLVM-specific lowering
// is in llvm/llvm_call.cpp.
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/typing/types.h"
#include "00_core/spec_trace.h"

namespace cursive::codegen {
namespace {

// Strip permission wrapper from type.
analysis::TypeRef StripPerm(const analysis::TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return StripPerm(perm->base);
  }
  return type;
}

// Check if type is a valid (non-null) pointer.
bool IsValidPtrType(const analysis::TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
    return ptr->state.has_value() && *ptr->state == analysis::PtrState::Valid;
  }
  return false;
}

}  // namespace

// Compute the ABI parameter indices for a call.
// Returns a vector where each element is the LLVM parameter index for the
// corresponding source parameter, or nullopt for ZST parameters.
std::vector<std::optional<unsigned>> ComputeParamIndices(
    const analysis::ScopeContext& ctx,
    const std::vector<std::pair<std::optional<analysis::ParamMode>, analysis::TypeRef>>& params,
    bool has_sret) {
  SPEC_RULE("ABI-ParamIndices");

  std::vector<std::optional<unsigned>> indices(params.size(), std::nullopt);

  unsigned llvm_index = has_sret ? 1u : 0u;
  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto& [mode, type] = params[i];

    // Determine pass kind.
    const auto kind = ABIParam(ctx, mode, type);
    if (!kind.has_value()) {
      indices[i] = llvm_index++;
      continue;
    }

    if (*kind == PassKind::ByRef) {
      indices[i] = llvm_index++;
      continue;
    }

    // ByValue - check if ZST.
    const auto size = ::cursive::analysis::layout::SizeOf(ctx, type);
    if (!size.has_value()) {
      indices[i] = llvm_index++;
      continue;
    }

    if (*size == 0) {
      // ZST has no LLVM parameter.
      indices[i] = std::nullopt;
      continue;
    }

    // Non-ZST ByValue.
    indices[i] = llvm_index++;
  }

  return indices;
}

// Check if a type should be passed as a valid pointer.
bool ShouldPassAsValidPtr(const analysis::TypeRef& type) {
  return IsValidPtrType(type);
}

}  // namespace cursive::codegen
