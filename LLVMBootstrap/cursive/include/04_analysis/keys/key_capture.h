#pragma once

// =============================================================================
// Key Capture Analysis
// =============================================================================
//
// SPEC REFERENCE:
//   - CursiveSpecification.md, Section 18.3 "Capture Semantics" (lines 24943-25022)
//   - CursiveSpecification.md, Section 18.5.3 "Key-Based Parallelism" (lines 25209-25240)
//   - CursiveSpecification.md, Section 19.4.2 "Key Prohibition in Yield" (lines 25839-25870)
//
// KEY CAPTURE SEMANTICS:
//   - const data: Captured by reference, no key needed
//   - shared data: Captured by reference, key needed for synchronization
//   - unique data: MUST use explicit move, at most one work item
//
// DISPATCH KEY CLAUSE:
//   dispatch i in 0..100 key data[i] write {
//       data[i] = compute(i)
//   }
//   - Per-iteration key acquisition based on key pattern
//   - Different index values produce different keys (parallel)
//   - Same index expressions serialize
//
// YIELD KEY PROHIBITION:
//   - Keys MUST NOT be held across yield points
//   - Use yield release to explicitly release before suspend
//   - E-CON-0213: yield while key held (without release)
//
// =============================================================================

#include <optional>
#include <string>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "04_analysis/keys/key_context.h"

namespace cursive::analysis {

// =============================================================================
// Captured Key Information
// =============================================================================

/// Permission type for captures
enum class CapturePermission {
  Const,   // Read-only, no key needed
  Shared,  // Synchronized via key system
  Unique,  // Exclusive access, requires move
};

/// A single captured binding with its key requirements
struct CapturedBinding {
  std::string name;             // Binding name
  CapturePermission permission; // Permission of the binding
  bool requires_move = false;   // True if unique and needs explicit move
  bool has_explicit_move = false; // True if move keyword present
  core::Span span;              // Location for diagnostics
};

/// Result of key capture analysis for a spawn/dispatch block
struct CapturedKeys {
  std::vector<CapturedBinding> bindings;
  std::vector<KeyPath> key_paths;  // Paths that need key acquisition

  bool HasKeyRequirements() const { return !key_paths.empty(); }
};

// =============================================================================
// Dispatch Key Pattern
// =============================================================================

/// Key pattern for dispatch iterations
struct DispatchKeyPattern {
  KeyPath base_path;                   // Base path (e.g., data)
  std::optional<std::string> index_var; // Loop variable used in index
  KeyAccessMode mode;                  // Read or Write
  core::Span span;

  /// Whether this pattern produces distinct keys per iteration
  bool IsPerIteration() const { return index_var.has_value(); }
};

// =============================================================================
// Capture Validation Result
// =============================================================================

struct CaptureValidation {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::optional<std::string> binding_name;
  std::optional<KeyPath> conflicting_path;
};

// =============================================================================
// Yield Key Check Result
// =============================================================================

struct YieldKeyCheck {
  bool keys_held = false;          // True if keys are held at yield point
  bool has_release = false;        // True if yield release modifier present
  std::vector<KeyPath> held_paths; // Paths held at yield point
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

// =============================================================================
// Key Capture Analysis Functions
// =============================================================================

/// Analyze which keys are captured by a spawn block body
/// Returns the set of captured bindings and their key requirements
CapturedKeys AnalyzeKeyCapture(const ast::BlockExpr& block,
                               const KeyContext& outer_ctx);

/// Analyze which keys are captured by a dispatch block body
CapturedKeys AnalyzeDispatchCapture(const ast::DispatchExpr& dispatch,
                                    const KeyContext& outer_ctx);

/// Validate that captured keys don't conflict between concurrent spawn blocks
/// Checks that multiple spawns within a parallel block have compatible captures
CaptureValidation ValidateKeyCapture(const std::vector<CapturedKeys>& spawn_captures,
                                     const core::Span& parallel_span);

/// Validate a single spawn's capture against the current key context
CaptureValidation ValidateSpawnCapture(const CapturedKeys& capture,
                                       const KeyContext& ctx,
                                       const core::Span& spawn_span);

// =============================================================================
// Dispatch Key Clause Functions
// =============================================================================

/// Compute key patterns for dispatch iterations
/// Returns the inferred or explicit key patterns for the dispatch
std::vector<DispatchKeyPattern> ComputeDispatchKeys(const ast::DispatchExpr& dispatch);

/// Validate dispatch key clause syntax and semantics
/// Ensures the key clause is well-formed and references valid paths
CaptureValidation ValidateDispatchKeyClause(const ast::DispatchKeyClause& clause,
                                            const KeyContext& ctx);

/// Determine if two dispatch key patterns are disjoint for different iterations
/// Two patterns are disjoint if they use the same loop variable in their index
bool DispatchKeysDisjoint(const DispatchKeyPattern& p1,
                          const DispatchKeyPattern& p2,
                          const std::string& loop_var);

// =============================================================================
// Yield Key Analysis Functions
// =============================================================================

/// Check for keys held across yield points in async procedures
/// Returns diagnostic information if keys are held without release modifier
YieldKeyCheck CheckKeyAcrossYield(const ast::YieldExpr& yield,
                                  const KeyContext& ctx,
                                  const core::Span& yield_span);

/// Check for keys held across yield from points
YieldKeyCheck CheckKeyAcrossYieldFrom(const ast::YieldFromExpr& yield_from,
                                      const KeyContext& ctx,
                                      const core::Span& yield_span);

/// Check for keys held across wait points
/// Similar to yield but for wait expressions
YieldKeyCheck CheckKeyAcrossWait(const ast::WaitExpr& wait,
                                 const KeyContext& ctx,
                                 const core::Span& wait_span);

// =============================================================================
// Helper Functions
// =============================================================================

/// Extract the loop variable name from a dispatch pattern
std::optional<std::string> ExtractLoopVariable(const ast::PatternPtr& pattern);

/// Check if an expression uses the specified loop variable
bool ExpressionUsesLoopVar(const ast::ExprPtr& expr, const std::string& loop_var);

/// Infer key paths from body accesses (when no explicit key clause)
std::vector<DispatchKeyPattern> InferDispatchKeyPaths(const ast::Block& body,
                                                      const std::string& loop_var);

}  // namespace cursive::analysis
