// =============================================================================
// Range Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Range patterns match values within a range
//   - lo..hi (exclusive) or lo..=hi (inclusive)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 356-364: RangePattern in RegisterPatternBindings
//   - Lines 573-574: RangePattern in LowerBindPattern
//
// =============================================================================

#include "05_codegen/lower/pattern/range_pattern.h"

#include "00_core/assert_spec.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// ============================================================================
// RegisterRangePatternBindings
// ============================================================================
//
// Range patterns can recursively contain patterns in their lo/hi bounds
// (typically literal patterns). This function recursively processes them.
//
void RegisterRangePatternBindings(
    const ast::RangePattern& pattern,
    const analysis::TypeRef& type_hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk) {
  (void)ctx;
  (void)is_immovable;
  (void)prov;
  (void)prov_region;
  (void)prov_region_tag;
  // Recursively process lo pattern if present
  if (pattern.lo) {
    walk(*pattern.lo, type_hint);
  }
  // Recursively process hi pattern if present
  if (pattern.hi) {
    walk(*pattern.hi, type_hint);
  }
}

// ============================================================================
// LowerRangePatternBindings
// ============================================================================
//
// Range patterns do not create bindings directly; the comparison is handled
// during pattern checking. Returns EmptyIR.
//
IRPtr LowerRangePatternBindings(const ast::RangePattern& /*pattern*/,
                                 const IRValue& /*value*/,
                                 LowerCtx& /*ctx*/) {
  // Range patterns don't bind anything directly
  return EmptyIR();
}

// ============================================================================
// PatternCheckRange
// ============================================================================
//
// Checks if a value falls within the range pattern bounds.
// Returns a fresh temp representing the comparison result.
//
IRValue PatternCheckRange(const ast::RangePattern& /*pattern*/,
                           const IRValue& /*value*/,
                           LowerCtx& ctx) {
  // Return a fresh temp representing the range check result
  // The actual bounds checking IR would be generated during match lowering
  return ctx.FreshTempValue("pat_range_match");
}

}  // namespace ultraviolet::codegen
