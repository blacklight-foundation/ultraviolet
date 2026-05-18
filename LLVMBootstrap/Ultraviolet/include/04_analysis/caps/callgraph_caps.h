#pragma once

// =============================================================================
// callgraph_caps.h - Call Graph Construction and Capability Flow Analysis
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 19.1 "Capability Flow Analysis"
//   - Section 19.2 "Call Graph Construction"
//
// This module provides:
//   - Call graph construction for modules
//   - Capability flow annotation on call graph edges
//   - Capability leak detection
//   - Capability chain validation from entry points
//   - Transitive closure computation for reachability
//
// =============================================================================

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"
#include "00_core/span.h"

namespace ultraviolet::analysis {

// =============================================================================
// Call graph node
// =============================================================================

/// Unique identifier for a procedure in the call graph
struct ProcId {
  std::string module_path;
  std::string name;

  bool operator==(const ProcId& other) const {
    return module_path == other.module_path && name == other.name;
  }
};

/// Hash function for ProcId
struct ProcIdHash {
  std::size_t operator()(const ProcId& id) const {
    return std::hash<std::string>{}(id.module_path) ^
           (std::hash<std::string>{}(id.name) << 1);
  }
};

/// Node in the call graph representing a procedure
struct CallGraphNode {
  ProcId id;

  /// True if this is an extern procedure
  bool is_extern = false;

  /// Capability signature for this procedure
  CapabilitySignature cap_sig;

  /// Source span for diagnostics
  core::Span span;
};

// =============================================================================
// Call graph edge
// =============================================================================

/// Edge in the call graph representing a call from caller to callee
struct CallGraphEdge {
  ProcId caller;
  ProcId callee;

  /// Capabilities passed along this edge
  CapabilitySet capabilities_passed;

  /// Source span of the call site
  core::Span call_span;

  /// True if this is a dynamic dispatch call (vtable)
  bool is_dynamic = false;
};

/// Unresolved direct call site that can be checked using callee expression type.
struct UnresolvedDirectCall {
  ProcId caller;

  /// Callee expression from the unresolved call site.
  const ast::Expr* callee_expr = nullptr;

  /// Source span of the unresolved call site.
  core::Span call_span;
};

/// Unresolved method call site that can be checked from receiver/arg types.
struct UnresolvedMethodCall {
  ProcId caller;

  /// Receiver expression from the unresolved method call site.
  const ast::Expr* receiver_expr = nullptr;

  /// Argument expressions from the unresolved method call site.
  std::vector<const ast::Expr*> arg_exprs;

  /// Method name at the call site (for diagnostics).
  std::string method_name;

  /// Source span of the unresolved method call site.
  core::Span call_span;
};

// =============================================================================
// Capability leak
// =============================================================================

/// Represents a detected capability leak
struct CapabilityLeak {
  /// Diagnostic code for leak classification
  std::string code;

  /// The capability that leaked
  CapabilityKind capability;

  /// Path from entry point to leak site
  std::vector<ProcId> path;

  /// The extern procedure that received the capability
  ProcId sink;

  /// Span of the leaking call
  core::Span leak_span;

  /// Diagnostic message
  std::string message;
};

// =============================================================================
// Call graph
// =============================================================================

/// Call graph for capability analysis
class CallGraph {
 public:
  /// Add a procedure node to the graph
  void AddNode(CallGraphNode node);

  /// Add a call edge to the graph
  void AddEdge(CallGraphEdge edge);

  /// Add an unresolved direct call to the graph
  void AddUnresolvedDirectCall(UnresolvedDirectCall call);

  /// Add an unresolved method call to the graph
  void AddUnresolvedMethodCall(UnresolvedMethodCall call);

  /// Get a node by its ID
  const CallGraphNode* GetNode(const ProcId& id) const;

  /// Get all nodes
  const std::unordered_map<ProcId, CallGraphNode, ProcIdHash>& GetNodes() const;

  /// Get all nodes (mutable)
  std::unordered_map<ProcId, CallGraphNode, ProcIdHash>& GetMutableNodes();

  /// Get all edges from a caller
  std::vector<const CallGraphEdge*> GetOutgoingEdges(const ProcId& caller) const;

  /// Get all edges to a callee
  std::vector<const CallGraphEdge*> GetIncomingEdges(const ProcId& callee) const;

  /// Get all edges
  const std::vector<CallGraphEdge>& GetEdges() const;

  /// Get all edges (mutable)
  std::vector<CallGraphEdge>& GetMutableEdges();

  /// Get all unresolved direct calls
  const std::vector<UnresolvedDirectCall>& GetUnresolvedDirectCalls() const;

  /// Get all unresolved method calls
  const std::vector<UnresolvedMethodCall>& GetUnresolvedMethodCalls() const;

  /// Check if the graph contains a node
  bool HasNode(const ProcId& id) const;

  /// Get all extern procedure nodes
  std::vector<const CallGraphNode*> GetExternNodes() const;

  /// Get the entry point (main) node if present
  const CallGraphNode* GetEntryPoint() const;

 private:
  std::unordered_map<ProcId, CallGraphNode, ProcIdHash> nodes_;
  std::vector<CallGraphEdge> edges_;
  std::vector<UnresolvedDirectCall> unresolved_direct_calls_;
  std::vector<UnresolvedMethodCall> unresolved_method_calls_;

  // Index for fast edge lookup
  std::unordered_map<ProcId, std::vector<std::size_t>, ProcIdHash>
      outgoing_edges_;
  std::unordered_map<ProcId, std::vector<std::size_t>, ProcIdHash>
      incoming_edges_;
};

// =============================================================================
// Call graph construction
// =============================================================================

/// Build a call graph from a module's procedure declarations
CallGraph BuildCallGraph(const ast::ASTModule& module);

/// Build a call graph from multiple modules
CallGraph BuildCallGraph(const std::vector<const ast::ASTModule*>& modules);

/// Build a call graph from a module using the active semantic scope context.
CallGraph BuildCallGraph(const ScopeContext& ctx, const ast::ASTModule& module);

/// Build a call graph from multiple modules using the active semantic scope
/// context.
CallGraph BuildCallGraph(const ScopeContext& ctx,
                         const std::vector<const ast::ASTModule*>& modules);

// =============================================================================
// Capability flow analysis
// =============================================================================

/// Annotate call graph edges with capability flow information
void AnnotateCapabilityFlow(CallGraph& cg);

/// Detect capabilities escaping to extern procedures
std::vector<CapabilityLeak> DetectCapabilityLeaks(const CallGraph& cg);

/// Validate that capability flow from entry point is correct
struct CapabilityChainError {
  std::string code;
  std::string message;
  std::optional<core::Span> span;
};

struct CapabilityChainResult {
  bool valid = false;
  std::vector<CapabilityChainError> errors;
  std::vector<CapabilityLeak> leaks;
};

CapabilityChainResult ValidateCapabilityChain(const CallGraph& cg);
CapabilityChainResult ValidateCapabilityChain(
    const CallGraph& cg,
    const std::unordered_map<const ast::Expr*, TypeRef>* expr_types);
CapabilityChainResult ValidateCapabilityChain(
    const ScopeContext& ctx,
    const CallGraph& cg,
    const std::unordered_map<const ast::Expr*, TypeRef>* expr_types);

// =============================================================================
// Reachability analysis
// =============================================================================

/// Reachability matrix for transitive closure
class ReachabilityMatrix {
 public:
  /// Check if 'from' can reach 'to' through any call path
  bool CanReach(const ProcId& from, const ProcId& to) const;

  /// Get all procedures reachable from a given procedure
  std::unordered_set<ProcId, ProcIdHash> ReachableFrom(const ProcId& from) const;

  /// Get all procedures that can reach a given procedure
  std::unordered_set<ProcId, ProcIdHash> ReachersOf(const ProcId& to) const;

 private:
  friend ReachabilityMatrix ComputeTransitiveClosure(const CallGraph& cg);

  std::unordered_map<ProcId, std::unordered_set<ProcId, ProcIdHash>, ProcIdHash>
      reachable_;
  std::unordered_map<ProcId, std::unordered_set<ProcId, ProcIdHash>, ProcIdHash>
      reachers_;
};

/// Compute transitive closure of the call graph
ReachabilityMatrix ComputeTransitiveClosure(const CallGraph& cg);

// =============================================================================
// Capability propagation
// =============================================================================

/// Preserve signature-based capability requirements and refresh flow annotations.
void PropagateCapabilityRequirements(CallGraph& cg);

}  // namespace ultraviolet::analysis
