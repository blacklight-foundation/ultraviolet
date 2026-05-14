// =============================================================================
// File: 04_analysis/generics/where_bounds.h
// Construct: Where Clause Parsing and Bound Validation
// Spec Section: CursiveSpecification.md Section 13.3 "Type Bounds"
// Spec Rules: WhereClause, PredicateReq, PredOk, T-Constraint-Sat
// =============================================================================
//
// This file declares functions for parsing where clauses and validating
// that type arguments satisfy their bounds.
//
// CRITICAL: Predicate clause syntax is Predicate(Type), NOT Type: Predicate
//           Example: |: Bitcopy(T)  NOT where T: Bitcopy
//
// Available predicates: Bitcopy, Clone, Drop, FfiSafe, GpuSafe
//
// =============================================================================

#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <map>

#include "00_core/diagnostics.h"
#include "02_source/ast/ast.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/generics/monomorphize.h"

namespace cursive::analysis {

// =============================================================================
// Predicate Types
// =============================================================================

// Built-in predicates
enum class PredicateKind {
  Bitcopy,  // Can be copied bitwise
  Clone,    // Has clone() method
  Drop,     // Has drop() cleanup
  FfiSafe,  // Can cross FFI boundary
  GpuSafe,  // Can be captured into GPU work
};

// Convert predicate name string to kind
std::optional<PredicateKind> ParsePredicateName(const std::string& name);

// Convert predicate kind to string name
std::string_view PredicateKindToString(PredicateKind kind);

// =============================================================================
// Bound Types
// =============================================================================

// A predicate bound: Bitcopy(T), Clone(T), Drop(T), FfiSafe(T), GpuSafe(T)
struct PredicateBound {
  PredicateKind predicate;
  TypeRef type;  // The type being constrained
  core::Span span;
};

// A class bound: T <: Comparable
struct ClassBound {
  TypeRef type;        // The type being constrained
  TypePath class_path; // The class that must be implemented
  core::Span span;
};

// A bound is either a predicate bound or a class bound
using Bound = std::variant<PredicateBound, ClassBound>;

// =============================================================================
// Parsed Where Clause
// =============================================================================

// Represents a fully parsed where clause with all bounds extracted
struct ParsedWhereClause {
  std::vector<Bound> bounds;
  core::Span span;
};

// =============================================================================
// Validation Results
// =============================================================================

struct WhereParseResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  ParsedWhereClause clause;
  core::DiagnosticStream diagnostics;
};

// Note: BoundCheckResult is defined in monomorphize.h

struct InferredBounds {
  std::vector<Bound> bounds;
};

// =============================================================================
// Where Clause Parsing Functions
// =============================================================================

// Parse a where clause into a list of bounds.
// SPEC: CursiveSpecification.md Section 13.3.1 "Where Clauses"
// Handles both predicate bounds: where Bitcopy(T)
// and class bounds: where T <: Comparable
WhereParseResult ParseWhereClause(
    const ScopeContext& ctx,
    const std::optional<ast::PredicateClause>& where_opt);

// =============================================================================
// Bound Validation Functions
// =============================================================================

// Validate that a type argument satisfies a single bound.
// SPEC: CursiveSpecification.md PredOk predicate
BoundCheckResult ValidateBound(
    const ScopeContext& ctx,
    const Bound& bound,
    const TypeRef& type_arg);

// Check that a type satisfies a predicate bound (Bitcopy, Clone, Drop, FfiSafe).
// SPEC: PredOk(pred, T)
bool CheckPredicateBound(
    const ScopeContext& ctx,
    PredicateKind predicate,
    const TypeRef& type);

// Check that a type implements a class bound.
// SPEC: T <: Class satisfaction check
bool CheckClassBound(
    const ScopeContext& ctx,
    const TypeRef& type,
    const TypePath& class_path);

// Validate all bounds in a where clause against type arguments.
// SPEC: T-Constraint-Sat rule
BoundCheckResult ValidateAllBounds(
    const ScopeContext& ctx,
    const ParsedWhereClause& where_clause,
    const TypeSubst& subst);

// =============================================================================
// Substitution Functions
// =============================================================================

// Substitute type parameters in a where clause with concrete types.
// SPEC: TypeSubst operation on where clauses
ParsedWhereClause SubstituteWhere(
    const ParsedWhereClause& where_clause,
    const TypeSubst& subst);

// Substitute type parameters in a single bound.
Bound SubstituteBound(
    const Bound& bound,
    const TypeSubst& subst);

// =============================================================================
// Bound Inference Functions
// =============================================================================

// Infer required bounds from usage patterns in a generic body.
// This examines how type parameters are used and infers what bounds
// would be needed for the code to type-check.
// SPEC: CursiveSpecification.md Section 13.3.3 "Bound Inference"
InferredBounds InferBoundsFromUsage(
    const ScopeContext& ctx,
    const ast::BlockPtr& body,
    const std::vector<ast::TypeParam>& params);

// =============================================================================
// Helper Functions
// =============================================================================

// Check if a predicate name is valid (Bitcopy, Clone, Drop, FfiSafe)
bool IsPredName(const std::string& name);

// Get the span of a bound
core::Span BoundSpan(const Bound& bound);

// Get a human-readable description of a bound
std::string BoundToString(const Bound& bound);

// Check if two bounds are equivalent
bool BoundsEquivalent(const Bound& a, const Bound& b);

// Merge multiple bound lists, removing duplicates
std::vector<Bound> MergeBounds(const std::vector<std::vector<Bound>>& bound_lists);

}  // namespace cursive::analysis
