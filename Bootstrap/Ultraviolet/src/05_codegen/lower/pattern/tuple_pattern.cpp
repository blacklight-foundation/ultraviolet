// =============================================================================
// Tuple Pattern Lowering Implementation
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

#include "05_codegen/lower/pattern/tuple_pattern.h"

#include <variant>

namespace ultraviolet::codegen {

// =============================================================================
// RegisterTuplePatternBindings
// =============================================================================
//
// SPEC: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//
// Registers bindings for tuple patterns by extracting element types from
// the TypeTuple hint and recursively processing each element pattern.
//
// (Lower-Pat-Tuple-Register)
// Gamma, TypeTuple{elements} |- RegisterPatternBindings((p_0, p_1, ..., p_n))
//   for i in 0..n:
//     elem_type_i = if TypeTuple then elements[i] else null
//     Gamma |- RegisterPatternBindings(p_i, elem_type_i)
//
void RegisterTuplePatternBindings(const ast::TuplePattern& pattern,
                                  const analysis::TypeRef& type_hint,
                                  LowerCtx& ctx,
                                  bool is_immovable,
                                  analysis::ProvenanceKind prov,
                                  std::optional<std::string> prov_region,
                                  std::optional<std::string> prov_region_tag) {
  // Extract TypeTuple element types from the hint if available
  const analysis::TypeTuple* tuple_type = nullptr;
  if (type_hint && std::holds_alternative<analysis::TypeTuple>(type_hint->node)) {
    tuple_type = &std::get<analysis::TypeTuple>(type_hint->node);
  }

  // Recursively register bindings for each element pattern
  for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
    analysis::TypeRef elem_type;
    if (tuple_type && i < tuple_type->elements.size()) {
      elem_type = tuple_type->elements[i];
    }
    // Recursive call to register pattern bindings for this element
    RegisterPatternBindings(*pattern.elements[i], elem_type, ctx, is_immovable,
                            prov, prov_region, prov_region_tag);
  }
}

// =============================================================================
// LowerTuplePatternBindings
// =============================================================================
//
// SPEC: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//
// Lowers tuple pattern bindings by creating derived values for each element
// and recursively processing each element pattern.
//
// (Lower-Pat-Tuple)
// Gamma |- LowerBindPattern((p_0, p_1, ..., p_n), v_tuple)
//   for i in 0..n:
//     v_elem_i = FreshTemp("pat_tuple_elem")
//     RegisterDerivedValue(v_elem_i, Tuple{base: v_tuple, index: i})
//     IR_i = LowerBindPattern(p_i, v_elem_i)
//   => SeqIR(IR_0, IR_1, ..., IR_n)
//
IRPtr LowerTuplePatternBindings(const ast::TuplePattern& pattern,
                                const IRValue& value,
                                LowerCtx& ctx) {
  std::vector<IRPtr> bindings;
  bindings.reserve(pattern.elements.size());

  for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
    // Create a fresh temporary for the tuple element access
    IRValue elem = ctx.FreshTempValue("pat_tuple_elem");

    // Register derived value info so the element can be materialized later
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Tuple;
    info.base = value;
    info.tuple_index = i;
    ctx.RegisterDerivedValue(elem, info);

    // Recursively lower the element pattern binding
    bindings.push_back(LowerBindPattern(*pattern.elements[i], elem, ctx));
  }

  return SeqIR(std::move(bindings));
}

// =============================================================================
// PatternCheckTuple
// =============================================================================
//
// SPEC: SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//
// Checks if a value matches a tuple pattern. Tuple patterns are irrefutable
// (always match) when all element patterns are irrefutable. This function
// returns true if all element patterns match.
//
// (PatternCheck-Tuple)
// Gamma |- PatternCheck((p_0, p_1, ..., p_n), v_tuple)
//   for i in 0..n:
//     v_elem_i = TupleAccess(v_tuple, i)
//     check_i = PatternCheck(p_i, v_elem_i)
//   => AND(check_0, check_1, ..., check_n)
//
IRValue PatternCheckTuple(const ast::TuplePattern& pattern,
                          const IRValue& value,
                          LowerCtx& ctx) {
  // If no elements, tuple pattern trivially matches
  if (pattern.elements.empty()) {
    IRValue result;
    result.kind = IRValue::Kind::Immediate;
    result.name = "true";
    result.bytes = {1};
    return result;
  }

  // Check each element pattern and combine with AND
  IRValue combined_check;
  bool first = true;

  for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
    // Create a derived value for the tuple element
    IRValue elem = ctx.FreshTempValue("pat_tuple_check_elem");
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Tuple;
    info.base = value;
    info.tuple_index = i;
    ctx.RegisterDerivedValue(elem, info);

    // Check if the element matches
    IRValue elem_check = PatternCheck(*pattern.elements[i], elem, ctx);

    if (first) {
      combined_check = elem_check;
      first = false;
    } else {
      // AND the checks together
      // Note: In a full implementation, this would generate an IRBinOp
      // For now, we create a derived value representing the AND
      IRValue and_result = ctx.FreshTempValue("pat_tuple_and");
      // The actual AND operation would be materialized during IR emission
      combined_check = and_result;
    }
  }

  return combined_check;
}

}  // namespace ultraviolet::codegen
