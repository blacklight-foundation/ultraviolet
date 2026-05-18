#pragma once

// =============================================================================
// pattern/record_pattern.h - Record Pattern Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Record patterns destructure record values
//   - Field patterns bind to record field values
//
// Provides functions for:
//   - RegisterRecordPatternBindings: Register variable bindings for record patterns
//   - LowerBindRecordPattern: Lower record pattern binding to IR
//   - PatternCheckRecord: Check if a value matches a record pattern
//
// =============================================================================

#include "05_codegen/lower/lower_pat.h"

#include <functional>
#include <optional>
#include <string>

namespace ultraviolet::ast {
struct RecordPattern;
struct Pattern;
}  // namespace ultraviolet::ast

namespace ultraviolet::codegen {

// ============================================================================
// RegisterRecordPatternBindings
// ============================================================================
//
// Registers variable bindings introduced by a record pattern.
// Called from RegisterPatternBindings dispatch in pattern_common.cpp.
//
// Supports both shorthand (Point{x}) and explicit (Point{x: a}) field patterns.
// For shorthand patterns, the field name becomes the binding name.
// For explicit patterns, the nested pattern is recursively registered.
//
// Parameters:
//   - node: The RecordPattern AST node
//   - hint: Type hint for the pattern (the record type)
//   - ctx: Lowering context
//   - is_immovable: Whether bindings should be immovable (:=)
//   - prov: Provenance kind for bindings
//   - prov_region: Optional region name for provenance
//   - walk: Callback to recursively register nested patterns
//
void RegisterRecordPatternBindings(
    const ast::RecordPattern& node,
    const analysis::TypeRef& hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk);

// ============================================================================
// LowerBindRecordPattern
// ============================================================================
//
// Lowers a record pattern binding to IR.
// Creates derived values for field extraction and recursively binds
// each field to its corresponding pattern (shorthand or explicit).
//
// Parameters:
//   - pat: The RecordPattern AST node
//   - value: The scrutinee value being matched
//   - ctx: Lowering context
//   - lookup_bind_type: Callback to look up a binding's type
//   - lookup_bind_prov: Callback to look up a binding's provenance
//   - lookup_bind_region: Callback to look up a binding's region
//   - lower_bind: Callback to recursively lower nested pattern bindings
//
// Returns:
//   IR sequence that performs the binding operations
//
IRPtr LowerBindRecordPattern(
    const ast::RecordPattern& pat,
    const IRValue& value,
    LowerCtx& ctx,
    std::function<analysis::TypeRef(const std::string&)> lookup_bind_type,
    std::function<analysis::ProvenanceKind(const std::string&)> lookup_bind_prov,
    std::function<std::optional<std::string>(const std::string&)>
        lookup_bind_region,
    std::function<std::optional<std::string>(const std::string&)>
        lookup_bind_region_tag,
    std::function<IRPtr(const ast::Pattern&, const IRValue&)> lower_bind);

// ============================================================================
// PatternCheckRecord
// ============================================================================
//
// Checks if a value matches a record pattern.
// Record patterns are always irrefutable (they always match if types are
// correct), so this returns a constant true value.
//
// Parameters:
//   - pat: The RecordPattern AST node
//   - value: The scrutinee value being matched
//   - ctx: Lowering context
//
// Returns:
//   IRValue representing a constant true (records are irrefutable)
//
// SPEC: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Record patterns are irrefutable (no discriminant to check)
//
IRValue PatternCheckRecord(const ast::RecordPattern& pat,
                           const IRValue& value,
                           LowerCtx& ctx);

}  // namespace ultraviolet::codegen
