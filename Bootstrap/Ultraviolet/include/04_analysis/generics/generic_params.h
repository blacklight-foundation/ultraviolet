// =============================================================================
// File: 04_analysis/generics/generic_params.h
// Construct: Generic Parameter Validation and Scope Building
// Spec Section: SPECIFICATION.md Section 13 "Generics"
// Spec Rules: GenericParams, TypeParam, TypeBound, Variance
// =============================================================================
//
// This file declares functions for validating generic parameter declarations
// and building scopes for type parameters.
//
// CRITICAL: Generic parameters use SEMICOLONS: <T; U>
//           Generic arguments use COMMAS: <T, U>
//
// =============================================================================

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "00_core/diagnostics.h"
#include "02_source/ast/ast.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

// =============================================================================
// Type Parameter Information
// =============================================================================

// Validated type parameter information
struct TypeParamInfo {
  std::string name;
  std::vector<ast::TypeBound> class_bounds;  // <: bounds with resolved paths and args
  std::optional<TypeRef> default_type;
  core::Span span;
};

// Const generic parameter information (UVX extension)
struct ConstParamInfo {
  std::string name;
  TypeRef type;  // Must be an integral type
  std::optional<std::int64_t> default_value;
  core::Span span;
};

// =============================================================================
// Validation Results
// =============================================================================

struct GenericParamValidationResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<TypeParamInfo> type_params;
  core::DiagnosticStream diagnostics;
};

struct ParamUniquenessResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::string duplicate_name;
  core::DiagnosticStream diagnostics;
};

struct DefaultValueResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::string param_name;
  core::DiagnosticStream diagnostics;
};

// =============================================================================
// Generic Parameter Validation Functions
// =============================================================================

// Validate a complete generic parameter declaration list.
// SPEC: SPECIFICATION.md Section 13.1 "Generic Parameters"
// Checks:
//   - Parameter name uniqueness
//   - Bound validity (class paths must resolve)
//   - Default value ordering (defaults after non-defaults is an error)
//   - Default type well-formedness
GenericParamValidationResult ValidateGenericParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);

// Check that all parameter names are unique within the parameter list.
// SPEC: E-TYP-2304 "Duplicate type parameter name"
ParamUniquenessResult CheckParamUniqueness(
    const std::optional<ast::GenericParams>& params_opt);

// Parse and validate a single type parameter.
// Returns validated parameter info including resolved bounds.
TypeParamInfo ParseTypeParam(
    const ScopeContext& ctx,
    const ast::TypeParam& param);

// Parse and validate a const generic parameter.
// SPEC: SPECIFICATION.md Section 13.1.2 "Const Generic Parameters"
// Type must be an integral type (i8, i16, i32, i64, u8, u16, u32, u64, usize, isize)
ConstParamInfo ParseConstParam(
    const ScopeContext& ctx,
    const ast::TypeParam& param,
    const TypeRef& param_type);

// Validate default type argument values.
// SPEC: SPECIFICATION.md Section 13.1.3 "Default Type Arguments"
// Rules:
//   - Parameters with defaults must come after parameters without defaults
//   - Default types must be well-formed in the scope of preceding parameters
//   - Default types must satisfy the parameter's bounds
DefaultValueResult ValidateDefaultValues(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);

// =============================================================================
// Scope Building
// =============================================================================

// Build a scope containing bindings for all type parameters.
// Each type parameter is bound as a type entity that can be referenced
// in bounds, defaults, and the body of the generic declaration.
// SPEC: BindTypeParams(Gamma, params)
Scope BuildParamScope(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);

// Extend Gamma with the type-parameter binding scope.
// The returned scope list preserves the original Gamma scopes and places the
// parameter bindings in the innermost position.
// SPEC: BindTypeParams(Gamma, params)
ScopeList BindTypeParams(
    const ScopeContext& ctx,
    const ast::GenericParams& params);

ScopeList BindTypeParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);

ScopeList BindTypeParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt,
    const std::optional<ast::PredicateClause>& predicate_clause_opt);

// =============================================================================
// Helper Functions
// =============================================================================

// Check if a type is valid as a const generic parameter type.
// Only integral types are allowed: i8-i64, u8-u64, isize, usize
bool IsValidConstParamType(const TypeRef& type);

// Get the number of required (non-default) parameters.
std::size_t RequiredParamCount(const std::optional<ast::GenericParams>& params_opt);

// Get the total number of parameters.
std::size_t TotalParamCount(const std::optional<ast::GenericParams>& params_opt);

// Check if a parameter list has any parameters with defaults.
bool HasDefaultParams(const std::optional<ast::GenericParams>& params_opt);

}  // namespace ultraviolet::analysis
