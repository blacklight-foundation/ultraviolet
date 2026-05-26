#pragma once

// =============================================================================
// Typed Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Typed pattern x: T binds with explicit type annotation
//   - Type is lowered and used for binding registration
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// Register a binding for a typed pattern with its explicit type annotation
void RegisterTypedPatternBindings(const ast::TypedPattern& pattern,
                                   const analysis::TypeRef& type_hint,
                                   LowerCtx& ctx,
                                   bool is_immovable,
                                   analysis::ProvenanceKind prov,
                                   std::optional<std::string> prov_region,
                                   std::optional<std::string> prov_region_tag);

// Create the binding IR for a typed pattern (handles union type extraction)
IRPtr LowerTypedPatternBindings(const ast::TypedPattern& pattern,
                                 const IRValue& value,
                                 LowerCtx& ctx);

// Check if a value matches a typed pattern (for union discrimination)
IRValue PatternCheckTyped(const ast::TypedPattern& pattern,
                           const IRValue& value,
                           LowerCtx& ctx);

}  // namespace ultraviolet::codegen
