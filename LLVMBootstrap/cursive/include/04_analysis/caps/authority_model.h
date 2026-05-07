#pragma once

// =============================================================================
// authority_model.h - Authority Model and Capability Flow Tracking
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 5.9 "Capabilities"
//   - Section 5.9.1 "Authority Model"
//   - Section 5.9.2 "No Ambient Authority"
//   - Section 19 "Capability Safety"
//
// This module provides:
//   - Authority model enforcement (no ambient authority)
//   - Capability access validation through explicit paths
//   - Capability flow tracking from Context to usage
//   - FFI capability isolation checking
//   - Capability passing validation
//
// AUTHORITY MODEL:
//   - All effects require explicit capability parameters
//   - Capabilities flow from Context through explicit parameters only
//   - Extern procedures CANNOT receive capability types (E-TYP-2623)
//   - main receives Context, which provides all root capabilities
//
// =============================================================================

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"
#include "00_core/span.h"

namespace cursive::analysis {

// =============================================================================
// Capability path tracking
// =============================================================================

/// Represents a path through which a capability flows
struct CapabilityPath {
  /// The root of the capability (usually "ctx")
  std::string root;

  /// The path components (e.g., ["ctx", "fs"] for ctx.fs)
  std::vector<std::string> path;

  /// The capability kind at this path
  CapabilityKind capability;

  /// Whether this is a valid path from Context
  bool valid = false;

  /// Convert to string for diagnostics
  std::string ToString() const;
};

// =============================================================================
// Authority validation results
// =============================================================================

/// Result of authority validation
struct AuthorityValidationResult {
  bool valid = false;
  std::string error_code;
  std::string error_message;
  core::Span span;
};

// =============================================================================
// Capability access validation
// =============================================================================

/// Validate that a capability is accessed through a valid path from Context
/// Returns the capability path if valid, or nullopt if invalid
std::optional<CapabilityPath> ValidateCapabilityAccess(
    const ast::Expr& expr,
    const TypeRef& expr_type);

/// Trace capability flow from an expression back to Context
/// Returns the path through which the capability was obtained
CapabilityPath TraceCapabilityFlow(
    const ast::Expr& expr,
    const TypeRef& expr_type);

// =============================================================================
// Ambient authority detection
// =============================================================================

/// Check if a procedure uses ambient authority (globals, statics, etc.)
/// Returns validation result with error details if ambient authority detected
AuthorityValidationResult CheckAmbientAuthority(
    const ast::ProcedureDecl& proc,
    const ExprTypeMap* expr_types = nullptr);
AuthorityValidationResult CheckAmbientAuthority(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ProcedureDecl& proc,
    const ExprTypeMap* expr_types = nullptr);

/// Check if an expression uses ambient authority
/// Returns true if the expression accesses capabilities without explicit parameters
bool ExpressionUsesAmbientAuthority(
    const ast::Expr& expr,
    const ExprTypeMap* expr_types = nullptr);

// =============================================================================
// Capability passing validation
// =============================================================================

/// Validate that capabilities are passed explicitly in a call
AuthorityValidationResult ValidateCapabilityPassing(
    const ast::CallExpr& call,
    const CapabilitySet& caller_caps,
    const CapabilitySet& callee_needs);

/// Validate that capabilities are passed explicitly in a method call
AuthorityValidationResult ValidateCapabilityPassing(
    const ast::MethodCallExpr& call,
    const CapabilitySet& caller_caps,
    const CapabilitySet& callee_needs);

// =============================================================================
// FFI capability isolation
// =============================================================================

/// Check that an extern procedure does not receive capabilities
/// Section 5.9.1: Foreign code CANNOT receive or return capability-bearing values
AuthorityValidationResult CheckCapabilityIsolation(
    const ast::ExternProcDecl& proc);
AuthorityValidationResult CheckCapabilityIsolation(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ExternProcDecl& proc);

/// Check that an extern block does not pass capabilities
AuthorityValidationResult CheckExternBlockIsolation(
    const ast::ExternBlock& block);
AuthorityValidationResult CheckExternBlockIsolation(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ExternBlock& block);

/// Validate that a call to extern does not pass capabilities
AuthorityValidationResult ValidateExternCall(
    const ast::CallExpr& call,
    const ast::ExternProcDecl& target,
    const std::vector<TypeRef>& arg_types);

// =============================================================================
// Context field access validation
// =============================================================================

/// Validate that a field access on Context produces valid capability
AuthorityValidationResult ValidateContextFieldAccess(
    const ast::FieldAccessExpr& access,
    const TypeRef& base_type);

/// Check if a field access produces a capability type
bool IsCapabilityFieldAccess(
    const ast::FieldAccessExpr& access,
    const TypeRef& base_type);

// =============================================================================
// Whole-program authority validation
// =============================================================================

/// Validate authority model for an entire module
/// Checks:
///   - No ambient authority usage
///   - All capabilities flow from Context
///   - No capabilities passed to extern
struct ModuleAuthorityResult {
  bool valid = true;
  std::vector<AuthorityValidationResult> errors;
};

ModuleAuthorityResult ValidateModuleAuthority(
    const ast::ASTModule& module,
    const ExprTypeMap* expr_types = nullptr);
ModuleAuthorityResult ValidateModuleAuthority(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    const ExprTypeMap* expr_types = nullptr);

/// Validate authority model for multiple modules
ModuleAuthorityResult ValidateModuleAuthority(
    const std::vector<const ast::ASTModule*>& modules,
    const ExprTypeMap* expr_types = nullptr);
ModuleAuthorityResult ValidateModuleAuthority(
    const ScopeContext& ctx,
    const std::vector<const ast::ASTModule*>& modules,
    const ExprTypeMap* expr_types = nullptr);

}  // namespace cursive::analysis

