// =============================================================================
// Wildcard Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.6 (Pattern Matching Lowering)
//   - Wildcard pattern (_) matches anything
//   - Creates no bindings
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - WildcardPattern case in RegisterPatternBindings (lines 251-252)
//   - WildcardPattern case in LowerBindPattern: EmptyIR
//
// =============================================================================

#include "05_codegen/lower/pattern/wildcard_pattern.h"

#include "00_core/assert_spec.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

// ============================================================================
// RegisterWildcardPatternBindings
// ============================================================================
//
// Wildcard patterns do not introduce any bindings.
// This is a no-op function.
//
void RegisterWildcardPatternBindings(const ast::WildcardPattern& /*pattern*/,
                                      const analysis::TypeRef& /*type_hint*/,
                                      LowerCtx& /*ctx*/,
                                      bool /*is_immovable*/,
                                      analysis::ProvenanceKind /*prov*/,
                                      std::optional<std::string> /*prov_region*/,
                                      std::optional<std::string> /*prov_region_tag*/) {
  // Wildcard patterns do not create bindings
}

// ============================================================================
// LowerWildcardPatternBindings
// ============================================================================
//
// Wildcard patterns do not create bindings, so this returns EmptyIR.
//
IRPtr LowerWildcardPatternBindings(const ast::WildcardPattern& /*pattern*/,
                                    const IRValue& /*value*/,
                                    LowerCtx& /*ctx*/) {
  // Wildcard patterns don't bind anything
  return EmptyIR();
}

// ============================================================================
// PatternCheckWildcard
// ============================================================================
//
// Wildcard patterns always match - they are irrefutable.
// Returns a constant true value.
//
IRValue PatternCheckWildcard(const ast::WildcardPattern& /*pattern*/,
                              const IRValue& /*value*/,
                              LowerCtx& /*ctx*/) {
  // Wildcard patterns always match
  IRValue result;
  result.kind = IRValue::Kind::Immediate;
  result.name = "true";
  result.bytes = {static_cast<std::uint8_t>(1)};
  return result;
}

}  // namespace cursive::codegen
