#pragma once

// =============================================================================
// Tuple Pattern Lowering Declarations
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Tuple patterns destructure tuple values
//   - Each element pattern is recursively processed
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 267-279: TuplePattern in RegisterPatternBindings
//   - Lines 462-473: TuplePattern in LowerBindPattern
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// =============================================================================
// RegisterTuplePatternBindings
// =============================================================================
//
// Registers bindings introduced by a tuple pattern.
//
// SPEC: (Lower-Pat-Tuple-Register)
// Extracts TypeTuple element types from the type hint and recursively
// registers bindings for each element pattern.
//
// Parameters:
//   pattern     - The tuple pattern to process
//   type_hint   - Optional TypeTuple hint for element types
//   ctx         - Lowering context for binding registration
//   is_immovable - Whether bindings should be immovable (:=)
//   prov        - Provenance kind for region tracking
//   prov_region - Optional region name for provenance
//
void RegisterTuplePatternBindings(const ast::TuplePattern& pattern,
                                  const analysis::TypeRef& type_hint,
                                  LowerCtx& ctx,
                                  bool is_immovable = false,
                                  analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom,
                                  std::optional<std::string> prov_region = std::nullopt,
                                  std::optional<std::string> prov_region_tag = std::nullopt);

// =============================================================================
// LowerTuplePatternBindings
// =============================================================================
//
// Lowers a tuple pattern to IR that binds the tuple elements.
//
// SPEC: (Lower-Pat-Tuple)
// Gamma |- LowerBindPattern((p_0, p_1, ..., p_n), v_tuple)
//   for i in 0..n:
//     v_elem_i = FreshTemp("pat_tuple_elem")
//     RegisterDerivedValue(v_elem_i, Tuple{base: v_tuple, index: i})
//     IR_i = LowerBindPattern(p_i, v_elem_i)
//   => SeqIR(IR_0, IR_1, ..., IR_n)
//
// Parameters:
//   pattern - The tuple pattern to lower
//   value   - The IRValue representing the tuple being destructured
//   ctx     - Lowering context
//
// Returns:
//   IRPtr containing the sequence of binding operations
//
IRPtr LowerTuplePatternBindings(const ast::TuplePattern& pattern,
                                const IRValue& value,
                                LowerCtx& ctx);

// =============================================================================
// PatternCheckTuple
// =============================================================================
//
// Checks if a value matches a tuple pattern.
//
// SPEC: (PatternCheck-Tuple)
// Gamma |- PatternCheck((p_0, p_1, ..., p_n), v_tuple)
//   for i in 0..n:
//     v_elem_i = TupleAccess(v_tuple, i)
//     check_i = PatternCheck(p_i, v_elem_i)
//   => AND(check_0, check_1, ..., check_n)
//
// Note: Tuple patterns are irrefutable when all element patterns are
// irrefutable. The check returns true if all element checks pass.
//
// Parameters:
//   pattern - The tuple pattern to check
//   value   - The IRValue representing the tuple being matched
//   ctx     - Lowering context
//
// Returns:
//   IRValue representing the boolean check result
//
IRValue PatternCheckTuple(const ast::TuplePattern& pattern,
                          const IRValue& value,
                          LowerCtx& ctx);

}  // namespace ultraviolet::codegen
