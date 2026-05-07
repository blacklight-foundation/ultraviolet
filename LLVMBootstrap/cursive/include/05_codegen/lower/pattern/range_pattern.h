#pragma once

// =============================================================================
// Range Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.6 (Pattern Matching Lowering)
//   - Range patterns match values within a range
//   - lo..hi (exclusive) or lo..=hi (inclusive)
//
// =============================================================================

#include <functional>

#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

// Register bindings for range pattern (recursively processes lo/hi)
void RegisterRangePatternBindings(
    const ast::RangePattern& pattern,
    const analysis::TypeRef& type_hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk);

// Range patterns do not create bindings directly (returns EmptyIR)
IRPtr LowerRangePatternBindings(const ast::RangePattern& pattern,
                                 const IRValue& value,
                                 LowerCtx& ctx);

// Check if a value falls within the range pattern bounds
IRValue PatternCheckRange(const ast::RangePattern& pattern,
                           const IRValue& value,
                           LowerCtx& ctx);

}  // namespace cursive::codegen
