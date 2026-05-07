#pragma once

// =============================================================================
// Wildcard Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.6 (Pattern Matching Lowering)
//   - Wildcard pattern (_) matches anything
//   - Creates no bindings
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

// Wildcard patterns do not introduce any bindings (no-op)
void RegisterWildcardPatternBindings(const ast::WildcardPattern& pattern,
                                      const analysis::TypeRef& type_hint,
                                      LowerCtx& ctx,
                                      bool is_immovable,
                                      analysis::ProvenanceKind prov,
                                      std::optional<std::string> prov_region,
                                      std::optional<std::string> prov_region_tag);

// Wildcard patterns do not create bindings (returns EmptyIR)
IRPtr LowerWildcardPatternBindings(const ast::WildcardPattern& pattern,
                                    const IRValue& value,
                                    LowerCtx& ctx);

// Check if a value matches a wildcard pattern (always returns true)
IRValue PatternCheckWildcard(const ast::WildcardPattern& pattern,
                              const IRValue& value,
                              LowerCtx& ctx);

}  // namespace cursive::codegen
