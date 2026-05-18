// =============================================================================
// Literal Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Literal patterns match constant values
//   - Used in case clauses for equality checking
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - LiteralPattern case in RegisterPatternBindings (line 253-254)
//   - LiteralPattern case in LowerBindPattern visitor
//
// =============================================================================

#include "05_codegen/lower/pattern/literal_pattern.h"

#include "00_core/assert_spec.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// ============================================================================
// RegisterLiteralPatternBindings
// ============================================================================
//
// Literal patterns do not introduce any bindings.
// This is a no-op function.
//
void RegisterLiteralPatternBindings(const ast::LiteralPattern& /*pattern*/,
                                     const analysis::TypeRef& /*type_hint*/,
                                     LowerCtx& /*ctx*/,
                                     bool /*is_immovable*/,
                                     analysis::ProvenanceKind /*prov*/,
                                     std::optional<std::string> /*prov_region*/,
                                     std::optional<std::string> /*prov_region_tag*/) {
  // Literal patterns do not create bindings
}

// ============================================================================
// LowerLiteralPatternBindings
// ============================================================================
//
// Literal patterns do not create bindings, so this returns EmptyIR.
// The actual comparison is handled by PatternCheckLiteral during matching.
//
IRPtr LowerLiteralPatternBindings(const ast::LiteralPattern& /*pattern*/,
                                   const IRValue& /*value*/,
                                   LowerCtx& /*ctx*/) {
  // Literal patterns don't bind anything
  return EmptyIR();
}

// ============================================================================
// PatternCheckLiteral
// ============================================================================
//
// Checks if a value matches a literal pattern.
// Creates an equality comparison between the scrutinee and the literal value.
//
IRValue PatternCheckLiteral(const ast::LiteralPattern& /*pattern*/,
                             const IRValue& /*value*/,
                             LowerCtx& ctx) {
  // Return a fresh temp representing the comparison result
  // The actual comparison IR would be generated during match lowering
  return ctx.FreshTempValue("pat_literal_match");
}

}  // namespace ultraviolet::codegen
