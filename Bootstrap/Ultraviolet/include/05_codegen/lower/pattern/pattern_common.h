#pragma once

// =============================================================================
// Pattern Lowering Common Utilities
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.6 (Pattern Lowering)
//   - Lines 16757-16813: Pattern lowering judgments
//   - PatternLowerJudg = {LowerBindPattern, LowerBindList, LowerIfCases, TagOf}
//   - Lower-Pat-General: MatchPattern + BindOrder + LowerBindList
//
// This header provides internal utilities shared across pattern lowering
// implementations. The main public API is in lower_pat.h.
//
// CONTENTS:
//   - Boolean immediate helper
//   - Type lookup utilities for patterns
//   - Move state merging for branching
//   - Pattern type extraction
//
// =============================================================================

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "05_codegen/lower/lower_pat.h"

namespace ultraviolet::codegen {

// =============================================================================
// §6.6 Immediate Values
// =============================================================================

// Create a boolean immediate value (for pattern match conditions)

// =============================================================================
// §6.6 Type Lookup Utilities
// =============================================================================

// Lower a syntax type to analysis TypeRef for pattern typing
analysis::TypeRef LowerSyntaxType(const std::shared_ptr<ast::Type>& type,
                                  LowerCtx& ctx);

// Resolve transparent type aliases before pattern narrowing/binding.
analysis::TypeRef ResolvePatternAliasType(const analysis::TypeRef& type,
                                          LowerCtx& ctx,
                                          std::size_t depth = 0);

// Look up a record declaration by path
const ast::RecordDecl* LookupRecordDecl(const ast::TypePath& path,
                                        const LowerCtx& ctx);

// Look up an enum declaration by path
const ast::EnumDecl* LookupEnumDecl(const ast::TypePath& path,
                                    const LowerCtx& ctx);

// Look up a modal declaration by path
const ast::ModalDecl* LookupModalDecl(const analysis::TypePath& path,
                                      const LowerCtx& ctx);

// =============================================================================
// §6.6 Record/Enum/Modal Field Type Lookup
// =============================================================================

// Get the type of a record field by name
analysis::TypeRef RecordFieldType(const ast::RecordDecl& decl,
                                  std::string_view name,
                                  LowerCtx& ctx);

// Find a variant in an enum declaration by name
const ast::VariantDecl* FindVariant(const ast::EnumDecl& decl,
                                    std::string_view name);

// Get the type of an enum payload field by name
analysis::TypeRef EnumPayloadFieldType(const ast::VariantDecl& variant,
                                       std::string_view name,
                                       LowerCtx& ctx);

// Get the type of an enum payload tuple element by index
analysis::TypeRef EnumPayloadTupleType(const ast::VariantDecl& variant,
                                       std::size_t index,
                                       LowerCtx& ctx);

// Find a state in a modal declaration by name
const ast::StateBlock* FindState(const ast::ModalDecl& decl,
                                 std::string_view name);

// Get the type of a modal state field by state and field name
analysis::TypeRef ModalFieldType(const ast::ModalDecl& decl,
                                 const std::vector<analysis::TypeRef>& modal_args,
                                 std::string_view state_name,
                                 std::string_view field_name,
                                 LowerCtx& ctx);

// =============================================================================
// §6.6 Move State Merging for Branching Patterns
// =============================================================================

// Merge resolve/codegen failures from a branch context into the base
void MergeFailures(LowerCtx& base, const LowerCtx& branch);

// Merge move states from multiple branches (for case clauses)
// Any binding moved in any branch is considered moved in the merged state
void MergeMoveStates(LowerCtx& base,
                     const std::vector<const LowerCtx*>& branches);

// =============================================================================
// §6.6 Pattern Type Extraction
// =============================================================================

// Get the type of a tuple pattern element
analysis::TypeRef TuplePatternElementType(const analysis::TypeRef& tuple_type,
                                          std::size_t index);

// Get the types of all tuple pattern elements
std::vector<analysis::TypeRef> TuplePatternElementTypes(
    const analysis::TypeRef& tuple_type);

// Get the path from a type reference if it's a named type
std::optional<analysis::TypePath> TypePathOf(const analysis::TypeRef& type);

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorPatternCommonRules();

}  // namespace ultraviolet::codegen
