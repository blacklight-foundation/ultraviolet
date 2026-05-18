#pragma once

// =============================================================================
// Identifier Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Identifier pattern binds the matched value to a name
//   - Lower-BindList-Cons: SeqIR(BindVarIR(x, v), IR_r)
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// Register a single binding for an identifier pattern
void RegisterIdentifierPatternBindings(const ast::IdentifierPattern& pattern,
                                        const analysis::TypeRef& type_hint,
                                        LowerCtx& ctx,
                                        bool is_immovable,
                                        analysis::ProvenanceKind prov,
                                        std::optional<std::string> prov_region,
                                        std::optional<std::string> prov_region_tag);

// Create the binding IR for an identifier pattern
IRPtr LowerIdentifierPatternBindings(const ast::IdentifierPattern& pattern,
                                      const IRValue& value,
                                      LowerCtx& ctx);

// Check if a value matches an identifier pattern (always returns true)
IRValue PatternCheckIdentifier(const ast::IdentifierPattern& pattern,
                                const IRValue& value,
                                LowerCtx& ctx);

}  // namespace ultraviolet::codegen
