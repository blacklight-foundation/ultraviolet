#pragma once

// =============================================================================
// cap_requirements.h - Capability Requirements Inference and Validation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 5.9.3 "Capability Requirements"
//   - Section 5.9.5 "Capability Classes"
//   - Section 8.10 "E-CAP Errors"
//
// This module provides:
//   - Inference of capability requirements from procedure bodies
//   - Validation that callers provide required capabilities
//   - Capability signature computation for procedures
//   - Propagation of capability requirements through call graphs
//
// =============================================================================

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// =============================================================================
// Capability kinds
// =============================================================================

enum class CapabilityKind {
  FileSystem,      // $FileSystem - file system operations
  Network,         // $Network - network operations
  HeapAllocator,   // $HeapAllocator - heap memory allocation
  ExecutionDomain, // $ExecutionDomain - parallel execution
  Reactor,         // $Reactor - async reactor
  System,          // System record - system operations
  Context,         // Context record - all capabilities
};

/// Convert capability kind to string for diagnostics
std::string_view CapabilityKindName(CapabilityKind kind);

/// Parse capability kind from type path
std::optional<CapabilityKind> CapabilityKindFromPath(const TypePath& path);

/// Parse capability kind from dynamic type
std::optional<CapabilityKind> CapabilityKindFromDynamic(const TypeDynamic& dyn);

// =============================================================================
// Capability sets
// =============================================================================

/// A set of capabilities required or provided by a procedure
struct CapabilitySet {
  bool has_filesystem = false;
  bool has_network = false;
  bool has_heap = false;
  bool has_execution_domain = false;
  bool has_reactor = false;
  bool has_system = false;
  bool has_context = false;  // Context implies all capabilities

  /// Create an empty capability set
  static CapabilitySet Empty();

  /// Create a capability set from Context (all capabilities)
  static CapabilitySet FromContext();

  /// Add a capability to the set
  void Add(CapabilityKind kind);

  /// Check if a capability is in the set
  bool Has(CapabilityKind kind) const;

  /// Check if this set is a subset of another
  bool IsSubsetOf(const CapabilitySet& other) const;

  /// Compute the union of two capability sets
  CapabilitySet Union(const CapabilitySet& other) const;

  /// Compute the intersection of two capability sets
  CapabilitySet Intersection(const CapabilitySet& other) const;

  /// Check if the set is empty
  bool IsEmpty() const;

  /// Convert to string for diagnostics
  std::string ToString() const;
};

// =============================================================================
// Capability signatures
// =============================================================================

/// Capability signature for a procedure
struct CapabilitySignature {
  /// Capabilities required by the procedure
  CapabilitySet required;

  /// Capabilities provided to callees (subset of required)
  CapabilitySet provided;

  /// Parameter indices that carry capabilities
  std::vector<std::size_t> capability_params;
};

// =============================================================================
// Capability inference
// =============================================================================

/// Infer capability requirements from a procedure's parameter types
CapabilitySet InferCapabilitiesFromParams(
    const std::vector<ast::Param>& params);

/// Infer capability requirements from a procedure's parameter types with
/// nominal expansion in the current module context.
CapabilitySet InferCapabilitiesFromParams(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::vector<ast::Param>& params);

/// Infer capability requirements from a type
CapabilitySet InferCapabilitiesFromType(const TypeRef& type);

/// Infer capability requirements from a type with nominal expansion in the
/// current module context.
CapabilitySet InferCapabilitiesFromType(const ScopeContext& ctx,
                                        const ast::ModulePath& current_module,
                                        const TypeRef& type);

/// Infer capability requirements from an AST type
CapabilitySet InferCapabilitiesFromAstType(const ast::Type& type);

/// Infer capability requirements from an AST type with nominal expansion in
/// the current module context.
CapabilitySet InferCapabilitiesFromAstType(const ScopeContext& ctx,
                                           const ast::ModulePath& current_module,
                                           const ast::Type& type);

/// Build the capability signature for a procedure declaration
CapabilitySignature BuildCapabilitySignature(const ast::ProcedureDecl& proc);

/// Build the capability signature for a procedure declaration with nominal
/// expansion in the current module context.
CapabilitySignature BuildCapabilitySignature(const ScopeContext& ctx,
                                            const ast::ModulePath& current_module,
                                            const ast::ProcedureDecl& proc);

/// Build the capability signature for a record method declaration.
CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& record_name,
    const ast::MethodDecl& method);

/// Build the capability signature for a record method declaration with nominal
/// expansion in the current module context.
CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& record_name,
    const ast::MethodDecl& method);

/// Build the capability signature for a class method declaration.
CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& class_name,
    const ast::ClassMethodDecl& method);

/// Build the capability signature for a class method declaration with nominal
/// expansion in the current module context.
CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& class_name,
    const ast::ClassMethodDecl& method);

/// Build the capability signature for a modal state method declaration.
CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::StateMethodDecl& method);

/// Build the capability signature for a modal state method declaration with
/// nominal expansion in the current module context.
CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::StateMethodDecl& method);

/// Build the capability signature for a modal transition declaration.
CapabilitySignature BuildCapabilitySignature(
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::TransitionDecl& transition);

/// Build the capability signature for a modal transition declaration with
/// nominal expansion in the current module context.
CapabilitySignature BuildCapabilitySignature(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const std::string& modal_name,
    const std::string& state_name,
    const ast::TransitionDecl& transition);

// =============================================================================
// Capability validation
// =============================================================================

/// Result of capability validation
struct CapabilityValidationResult {
  bool valid = false;
  std::string error_code;
  std::string error_message;
  CapabilitySet missing;  // Capabilities that are missing
};

/// Validate that a caller provides required capabilities
CapabilityValidationResult ValidateCapabilitySatisfied(
    const CapabilitySet& provided,
    const CapabilitySet& required,
    const core::Span& call_span);

/// Validate that a procedure has the capabilities it needs
CapabilityValidationResult ValidateProcedureCapabilities(
    const ast::ProcedureDecl& proc,
    const CapabilitySet& available);

// =============================================================================
// Capability checking for expressions
// =============================================================================

/// Check if a method call requires specific capabilities
std::optional<CapabilityKind> MethodCallRequiresCapability(
    const TypeRef& receiver_type,
    std::string_view method_name);

/// Check if an expression uses capabilities
CapabilitySet ExpressionUsesCapabilities(const ast::Expr& expr);

}  // namespace cursive::analysis
