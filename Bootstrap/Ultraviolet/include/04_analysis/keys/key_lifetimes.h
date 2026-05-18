#pragma once

// =============================================================================
// Key Lifetime Analysis
// =============================================================================
//
// SPEC REFERENCE:
//   - Docs/SPECIFICATION.md, Section 17.2.2 "Key Release" (lines 24034-24050)
//   - Docs/SPECIFICATION.md, Section 17.1.6 "Wait Restrictions" (lines 23930-23936)
//   - Docs/SPECIFICATION.md, Section 19.4.2 "Key Prohibition in Yield" (lines 25839-25870)
//
// KEY LIFETIME SEMANTICS:
//   - Acquired: At # block entry
//   - Held: Within # block body
//   - Released: At # block exit (including break/return)
//
// KEY RELEASE RULES (from spec):
//   (K-Release-Scope) ScopeExit(S) => Γ'_keys = Γ_keys \ {(P, M, S') : S' = S}
//   (K-Release-Order) Keys released in LIFO order
//
// SUSPENSION POINT RULES:
//   - Keys MUST NOT be held across yield points (without release modifier)
//   - Keys MUST NOT be held across wait points
//   - yield release releases keys before suspension, reacquires on resume
//
// STALENESS:
//   - After yield release, bindings from shared data may be stale
//   - W-CON-0011 warning unless [[stale_ok]] present
//
// =============================================================================

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "04_analysis/keys/key_context.h"

namespace ultraviolet::analysis {

// =============================================================================
// Key Lifetime Information
// =============================================================================

/// Tracks the lifetime of a key acquisition
struct KeyLifetime {
  KeyPath path;           // The key path
  KeyAccessMode mode;     // Read or Write mode
  KeyScopeId scope;       // Scope where acquired
  core::Span acq_span;    // Location of acquisition

  /// Whether this key has been explicitly released
  bool released = false;

  /// Location of explicit release (if any)
  std::optional<core::Span> release_span;
};

/// Result of lifetime tracking for a scope
struct ScopeKeyState {
  std::vector<KeyLifetime> active_keys;      // Keys currently held
  std::vector<KeyLifetime> released_keys;    // Keys that have been released
  KeyScopeId scope_id;

  bool HasActiveKeys() const { return !active_keys.empty(); }
  bool AllReleased() const { return active_keys.empty(); }
};

using ReadSet = std::unordered_map<std::string, std::string>;
using WriteSet = std::unordered_map<std::string, std::string>;

struct SpecStartState {
  std::vector<KeyPath> paths;
  const ast::Block* body = nullptr;
};

struct SpecSnapshotState {
  std::vector<KeyPath> paths;
  const ast::Block* body = nullptr;
  ReadSet read_set;
};

struct SpecExecState {
  const ast::Block* body = nullptr;
  ReadSet read_set;
  WriteSet write_set;
};

struct SpecCommitState {
  ReadSet read_set;
  WriteSet write_set;
  std::string value_repr;
};

struct SpecRetryState {
  std::vector<KeyPath> paths;
  const ast::Block* body = nullptr;
  std::size_t retry_count = 0;
};

struct SpecFallbackState {
  std::vector<KeyPath> paths;
  const ast::Block* body = nullptr;
};

struct SpecDoneState {
  std::string value_repr;
};

struct SpecPanicState {};

using SpecState = std::variant<SpecStartState,
                               SpecSnapshotState,
                               SpecExecState,
                               SpecCommitState,
                               SpecRetryState,
                               SpecFallbackState,
                               SpecDoneState,
                               SpecPanicState>;

std::vector<KeyLifetime> HeldKeysForPaths(const std::vector<KeyPath>& paths,
                                          const ScopeKeyState& state);
ScopeKeyState MarkKeysReleased(const ScopeKeyState& state,
                               const std::vector<KeyLifetime>& keys);
ScopeKeyState ClearReleased(const ScopeKeyState& state,
                            const std::vector<KeyLifetime>& keys);

// =============================================================================
// Key Release Validation Result
// =============================================================================

struct KeyReleaseValidation {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::optional<KeyPath> unreleased_path;

  /// Paths that were released
  std::vector<KeyPath> released_paths;
};

// =============================================================================
// Suspension Point Check Results
// =============================================================================

/// Result of checking for keys held at suspension point
struct SuspensionCheck {
  bool valid = true;                    // True if no keys held (or release present)
  bool has_release_modifier = false;    // True if release modifier present
  std::vector<KeyPath> held_keys;       // Keys held at suspension point
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

// =============================================================================
// Staleness Tracking
// =============================================================================

/// A binding that may be stale after yield release
struct StalenessWarning {
  std::string binding_name;    // Name of the potentially stale binding
  core::Span binding_span;     // Where the binding was created
  core::Span yield_span;       // Where the yield release occurred
  bool suppressed = false;     // True if [[stale_ok]] present
};

// =============================================================================
// Key Lifetime Tracking Functions
// =============================================================================

/// Track key lifetime within a key block
/// Returns the key state at block entry
ScopeKeyState TrackKeyLifetime(const ast::KeyBlockStmt& block,
                               const KeyContext& ctx);

/// Propagate key lifetime through a block of statements
/// Updates the context as keys are acquired and released
void PropagateKeyLifetime(const ast::Block& block,
                          KeyContext& ctx);

/// Track key lifetime across control flow edges
/// Returns the key state after the given statement
ScopeKeyState TrackStatementKeys(const ast::Stmt& stmt,
                                 KeyContext& ctx);

// =============================================================================
// Key Release Validation Functions
// =============================================================================

/// Validate that keys are properly released at scope exit
/// Checks that all keys acquired in the scope are released
KeyReleaseValidation ValidateKeyRelease(const ast::KeyBlockStmt& block,
                                        const KeyContext& ctx);

/// Validate explicit key release (for release modifier in key blocks)
KeyReleaseValidation ValidateExplicitRelease(const std::vector<KeyPath>& paths,
                                             const KeyContext& ctx,
                                             const core::Span& release_span);

/// Check that keys are released on all control flow paths
/// Returns error if some paths don't release a key
KeyReleaseValidation ValidateReleasePaths(const ast::Block& block,
                                          const std::vector<KeyPath>& expected_releases);

// =============================================================================
// Suspension Point Checking Functions
// =============================================================================

/// Check for keys held across yield point
/// CRITICAL: Keys MUST NOT be held across yield (E-CON-0213)
SuspensionCheck CheckKeyHeldAcrossYield(const ast::YieldExpr& yield,
                                        const KeyContext& ctx,
                                        const core::Span& yield_span);

/// Check for keys held across yield from point
/// CRITICAL: Keys MUST NOT be held across yield from (E-CON-0224)
SuspensionCheck CheckKeyHeldAcrossYieldFrom(const ast::YieldFromExpr& yield_from,
                                            const KeyContext& ctx,
                                            const core::Span& yield_span);

/// Check for keys held across wait point
/// CRITICAL: Keys MUST NOT be held across wait (E-CON-0133)
SuspensionCheck CheckKeyHeldAcrossWait(const ast::WaitExpr& wait,
                                       const KeyContext& ctx,
                                       const core::Span& wait_span);

/// Check all suspension points in a block
/// Returns all suspension point violations
std::vector<SuspensionCheck> CheckAllSuspensionPoints(const ast::Block& block,
                                                      const KeyContext& ctx);

// =============================================================================
// Yield Release Handling
// =============================================================================

/// Process yield release - release all keys and mark for reacquisition
/// Returns the paths that were released
std::vector<KeyPath> ProcessYieldRelease(KeyContext& ctx);

/// Reacquire keys after yield release resume
/// Keys are reacquired in canonical order per spec
void ReacquireAfterYieldRelease(const std::vector<KeyPath>& paths,
                                KeyAccessMode mode,
                                KeyContext& ctx);

// =============================================================================
// Staleness Analysis
// =============================================================================

/// Check for bindings that may be stale after yield release
/// Returns warnings for bindings derived from shared data
std::vector<StalenessWarning> CheckStaleness(const ast::Block& block,
                                             const std::vector<core::Span>& yield_release_points);

/// Check if a binding has [[stale_ok]] attribute
bool HasStaleOkAttribute(const ast::LetStmt& stmt);
bool HasStaleOkAttribute(const ast::VarStmt& stmt);

// =============================================================================
// Helper Functions
// =============================================================================

/// Check if a statement contains a suspension point
bool ContainsSuspensionPoint(const ast::Stmt& stmt);

/// Check if an expression is a suspension point
bool IsSuspensionPoint(const ast::ExprPtr& expr);

/// Get all suspension point spans in a block
std::vector<core::Span> GetSuspensionPointSpans(const ast::Block& block);

/// Check if key release modifier is present in key block
bool HasReleaseModifier(const ast::KeyBlockStmt& block);

}  // namespace ultraviolet::analysis
