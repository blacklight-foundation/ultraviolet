#pragma once

// =============================================================================
// Literal Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Literal patterns match constant values
//   - Used in case clauses for equality checking
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// Literal patterns do not introduce any bindings (no-op)
void RegisterLiteralPatternBindings(const ast::LiteralPattern& pattern,
                                     const analysis::TypeRef& type_hint,
                                     LowerCtx& ctx,
                                     bool is_immovable,
                                     analysis::ProvenanceKind prov,
                                     std::optional<std::string> prov_region,
                                     std::optional<std::string> prov_region_tag);

// Literal patterns do not create bindings (returns EmptyIR)
IRPtr LowerLiteralPatternBindings(const ast::LiteralPattern& pattern,
                                   const IRValue& value,
                                   LowerCtx& ctx);

// Check if a value matches a literal pattern (equality comparison)
IRValue PatternCheckLiteral(const ast::LiteralPattern& pattern,
                             const IRValue& value,
                             LowerCtx& ctx);

}  // namespace ultraviolet::codegen
