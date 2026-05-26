#pragma once

// =============================================================================
// Modal Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16782-16785
//   - TagOf-Modal: Get modal state index
//   - StateIndex(M, S) = i
//
// =============================================================================

#include <functional>

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// Register bindings for modal pattern fields
void RegisterModalPatternBindings(
    const ast::ModalPattern& pattern,
    const analysis::TypeRef& type_hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk);

// Create binding IR for modal pattern fields (state-specific field extraction)
IRPtr LowerModalPatternBindings(
    const ast::ModalPattern& pattern,
    const IRValue& value,
    LowerCtx& ctx,
    std::function<analysis::TypeRef(const std::string&)> lookup_bind_type,
    std::function<analysis::ProvenanceKind(const std::string&)> lookup_bind_prov,
    std::function<std::optional<std::string>(const std::string&)> lookup_bind_region,
    std::function<std::optional<std::string>(const std::string&)> lookup_bind_region_tag,
    std::function<IRPtr(const ast::Pattern&, const IRValue&)> lower_bind);

// Check if a value matches a modal pattern by comparing state tags
IRValue PatternCheckModal(const ast::ModalPattern& pattern,
                           const IRValue& value,
                           LowerCtx& ctx);

}  // namespace ultraviolet::codegen
