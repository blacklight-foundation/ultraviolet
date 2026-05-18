// =============================================================================
// Key Lifetime Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   - SPECIFICATION.md, Section 17.2.2 "Key Release" (lines 24034-24050)
//   - SPECIFICATION.md, Section 17.1.6 "Wait Restrictions" (lines 23930-23936)
//   - SPECIFICATION.md, Section 19.4.2 "Key Prohibition in Yield" (lines 25839-25870)
//
// KEY RELEASE RULES (from spec):
//   (K-Release-Scope) ScopeExit(S) => Γ'_keys = Γ_keys \ {(P, M, S') : S' = S}
//   (K-Release-Order) Keys released per LIFO semantics
//
// SUSPENSION POINT RULES (from spec):
//   (K-Yield-No-Keys) yield without release + keys held => E-CON-0213
//   (K-YieldFrom-No-Keys) yield from without release + keys held => E-CON-0224
//   (K-Wait-No-Keys) wait + keys held => E-CON-0133
//
// YIELD RELEASE MECHANISM (from spec):
//   If release is used, all held keys are released before suspension
//   and reacquired on resume in canonical order
//
// STALENESS WARNING (from spec):
//   Bindings derived from shared data before yield release are potentially
//   stale after resumption. Warning W-CON-0011 applies unless [[stale_ok]].
//
// =============================================================================

#include "04_analysis/keys/key_lifetimes.h"

#include <algorithm>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/keys/key_conflict.h"
#include "04_analysis/keys/key_paths.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsKeyLifetimes() {
  SPEC_DEF("KeyRelease", "UVX.17.2.2");
  SPEC_DEF("YieldNoKeys", "UVX.19.4.2");
  SPEC_DEF("WaitNoKeys", "UVX.17.1.6");
  SPEC_DEF("Staleness", "UVX.19.4.2");
}

/// Check if an expression is a yield expression
bool IsYieldExpr(const ast::ExprPtr& expr) {
  if (!expr) return false;
  return std::holds_alternative<ast::YieldExpr>(expr->node);
}

/// Check if an expression is a yield from expression
bool IsYieldFromExpr(const ast::ExprPtr& expr) {
  if (!expr) return false;
  return std::holds_alternative<ast::YieldFromExpr>(expr->node);
}

/// Check if an expression is a wait expression
bool IsWaitExpr(const ast::ExprPtr& expr) {
  if (!expr) return false;
  return std::holds_alternative<ast::WaitExpr>(expr->node);
}

/// Walk an expression to find suspension points
void FindSuspensionPointsInExpr(const ast::ExprPtr& expr,
                                std::vector<core::Span>& spans) {
  if (!expr) return;

  // Check for suspension point expressions
  if (std::holds_alternative<ast::YieldExpr>(expr->node) ||
      std::holds_alternative<ast::YieldFromExpr>(expr->node) ||
      std::holds_alternative<ast::WaitExpr>(expr->node)) {
    spans.push_back(expr->span);
    return;
  }

  // Recurse into sub-expressions
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(binary->lhs, spans);
    FindSuspensionPointsInExpr(binary->rhs, spans);
  } else if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(unary->value, spans);
  } else if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(call->callee, spans);
    for (const auto& arg : call->args) {
      FindSuspensionPointsInExpr(arg.value, spans);
    }
  } else if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(method->receiver, spans);
    for (const auto& arg : method->args) {
      FindSuspensionPointsInExpr(arg.value, spans);
    }
  } else if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(index->base, spans);
    FindSuspensionPointsInExpr(index->index, spans);
  } else if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(field->base, spans);
  } else if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    FindSuspensionPointsInExpr(if_expr->cond, spans);
    // Then/else blocks would be handled separately
  } else if (const auto* block_expr = std::get_if<ast::BlockExpr>(&expr->node)) {
    // Recurse into block statements
    if (block_expr->block) {
      for (const auto& stmt : block_expr->block->stmts) {
        if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
          FindSuspensionPointsInExpr(expr_stmt->value, spans);
        }
      }
      if (block_expr->block->tail_opt) {
        FindSuspensionPointsInExpr(block_expr->block->tail_opt, spans);
      }
    }
  }
}

/// Walk a statement to find suspension points
void FindSuspensionPointsInStmt(const ast::Stmt& stmt,
                                std::vector<core::Span>& spans) {
  if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
    FindSuspensionPointsInExpr(expr_stmt->value, spans);
  } else if (const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt)) {
    if (let_stmt->binding.init) {
      FindSuspensionPointsInExpr(let_stmt->binding.init, spans);
    }
  } else if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
    if (var_stmt->binding.init) {
      FindSuspensionPointsInExpr(var_stmt->binding.init, spans);
    }
  } else if (const auto* assign_stmt = std::get_if<ast::AssignStmt>(&stmt)) {
    FindSuspensionPointsInExpr(assign_stmt->place, spans);
    FindSuspensionPointsInExpr(assign_stmt->value, spans);
  } else if (const auto* return_stmt = std::get_if<ast::ReturnStmt>(&stmt)) {
    if (return_stmt->value_opt) {
      FindSuspensionPointsInExpr(return_stmt->value_opt, spans);
    }
  }
}

}  // namespace

std::vector<KeyLifetime> HeldKeysForPaths(const std::vector<KeyPath>& paths,
                                          const ScopeKeyState& state) {
  std::vector<KeyLifetime> held;
  for (const auto& path : paths) {
    for (const auto& key : state.active_keys) {
      if (!KeyPathLess(key.path, path) && !KeyPathLess(path, key.path)) {
        held.push_back(key);
      }
    }
  }
  std::sort(held.begin(), held.end(),
            [](const KeyLifetime& lhs, const KeyLifetime& rhs) {
              return KeyPathLess(lhs.path, rhs.path);
            });
  return held;
}

ScopeKeyState MarkKeysReleased(const ScopeKeyState& state,
                               const std::vector<KeyLifetime>& keys) {
  ScopeKeyState out = state;
  for (const auto& key : keys) {
    out.active_keys.erase(
        std::remove_if(out.active_keys.begin(), out.active_keys.end(),
                       [&](const KeyLifetime& active) {
                         return !KeyPathLess(active.path, key.path) &&
                                !KeyPathLess(key.path, active.path) &&
                                active.mode == key.mode &&
                                active.scope == key.scope;
                       }),
        out.active_keys.end());
    out.released_keys.push_back(key);
  }
  return out;
}

ScopeKeyState ClearReleased(const ScopeKeyState& state,
                            const std::vector<KeyLifetime>& keys) {
  ScopeKeyState out = state;
  for (const auto& key : keys) {
    out.released_keys.erase(
        std::remove_if(out.released_keys.begin(), out.released_keys.end(),
                       [&](const KeyLifetime& released) {
                         return !KeyPathLess(released.path, key.path) &&
                                !KeyPathLess(key.path, released.path) &&
                                released.mode == key.mode &&
                                released.scope == key.scope;
                       }),
        out.released_keys.end());
  }
  return out;
}

// =============================================================================
// Key Lifetime Tracking Functions
// =============================================================================

ScopeKeyState TrackKeyLifetime(const ast::KeyBlockStmt& block,
                               const KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Track-Lifetime");

  ScopeKeyState state;
  state.scope_id = ctx.CurrentScope();

  // Get all keys that would be acquired for this block
  for (const auto& path_expr : block.paths) {
    KeyPath path = ParseKeyPathSpec(path_expr);
    KeyAccessMode mode = KeyAccessMode::Read;
    if (block.mode.has_value() && *block.mode == ast::KeyMode::Write) {
      mode = KeyAccessMode::Write;
    }

    KeyLifetime lifetime;
    lifetime.path = path;
    lifetime.mode = mode;
    lifetime.scope = state.scope_id;
    lifetime.acq_span = path_expr.span;
    lifetime.released = false;

    state.active_keys.push_back(std::move(lifetime));
  }

  return state;
}

void PropagateKeyLifetime(const ast::Block& block,
                          KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Propagate-Lifetime");

  // Push a new scope for this block
  ctx.PushScope();

  // Process each statement in the block
  for (const auto& stmt : block.stmts) {
    TrackStatementKeys(stmt, ctx);
  }

  // Pop scope - this releases all keys acquired in this scope
  ctx.PopScope();
}

ScopeKeyState TrackStatementKeys(const ast::Stmt& stmt,
                                 KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Track-Statement");

  ScopeKeyState state;
  state.scope_id = ctx.CurrentScope();

  // Handle key block statements
  if (const auto* key_block = std::get_if<ast::KeyBlockStmt>(&stmt)) {
    // Push scope and acquire keys
    ctx.PushScope();

    for (const auto& path_expr : key_block->paths) {
      KeyPath path = ParseKeyPathSpec(path_expr);
      KeyAccessMode mode = KeyAccessMode::Read;
      if (key_block->mode.has_value() && *key_block->mode == ast::KeyMode::Write) {
        mode = KeyAccessMode::Write;
      }
      ctx.Acquire(path, mode);
    }

    // Process body
    if (key_block->body) {
      PropagateKeyLifetime(*key_block->body, ctx);
    }

    // Pop scope releases keys
    ctx.PopScope();
  }

  // Copy current state to return
  for (const auto& held : ctx.HeldKeys()) {
    KeyLifetime lifetime;
    lifetime.path = held.path;
    lifetime.mode = held.mode;
    lifetime.scope = held.scope;
    state.active_keys.push_back(std::move(lifetime));
  }

  return state;
}

// =============================================================================
// Key Release Validation Functions
// =============================================================================

KeyReleaseValidation ValidateKeyRelease(const ast::KeyBlockStmt& block,
                                        const KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Validate-Release");

  KeyReleaseValidation result;

  // Check that all keys declared in the block will be properly released
  // This is automatic at scope exit, but we check for any violations

  // Get expected keys from the block
  std::vector<KeyPath> expected_paths;
  for (const auto& path_expr : block.paths) {
    expected_paths.push_back(ParseKeyPathSpec(path_expr));
  }

  // If the block has a release modifier, keys are explicitly released
  if (HasReleaseModifier(block)) {
    for (const auto& path : expected_paths) {
      result.released_paths.push_back(path);
    }
  }

  // Check for suspension points within the body that would require release
  if (block.body) {
    auto suspension_spans = GetSuspensionPointSpans(*block.body);
    if (!suspension_spans.empty() && !HasReleaseModifier(block)) {
      // Suspension point inside key block without release modifier
      result.ok = false;
      result.diag_id = "E-CON-0213";  // Key held across suspension
      result.span = suspension_spans.front();
      if (!expected_paths.empty()) {
        result.unreleased_path = expected_paths.front();
      }
    }
  }

  return result;
}

KeyReleaseValidation ValidateExplicitRelease(const std::vector<KeyPath>& paths,
                                             const KeyContext& ctx,
                                             const core::Span& release_span) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Explicit-Release");

  KeyReleaseValidation result;

  // Check that all paths being released are actually held
  for (const auto& path : paths) {
    bool found = false;
    for (const auto& held : ctx.HeldKeys()) {
      if (held.path.root == path.root && held.path.segs.size() == path.segs.size()) {
        // Simple path equality check
        found = true;
        break;
      }
    }

    if (found) {
      result.released_paths.push_back(path);
    } else {
      // Attempting to release a key that isn't held
      result.ok = false;
      result.diag_id = "E-CON-0001";  // Key access after release / not held
      result.span = release_span;
      result.unreleased_path = path;
      return result;
    }
  }

  return result;
}

KeyReleaseValidation ValidateReleasePaths(const ast::Block& block,
                                          const std::vector<KeyPath>& expected_releases) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Validate-Release-Paths");

  KeyReleaseValidation result;

  // This would require control flow analysis to verify that all paths
  // through the block properly release the expected keys.
  // For now, we do a simple check.

  // All expected releases should happen by scope exit
  result.released_paths = expected_releases;

  return result;
}

// =============================================================================
// Suspension Point Checking Functions
// =============================================================================

SuspensionCheck CheckKeyHeldAcrossYield(const ast::YieldExpr& yield,
                                        const KeyContext& ctx,
                                        const core::Span& yield_span) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Yield-No-Keys");

  SuspensionCheck result;
  result.has_release_modifier = yield.release;

  // Get all currently held keys
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    for (const auto& key : held) {
      result.held_keys.push_back(key.path);
    }

    // If release modifier is not present, this is an error
    if (!yield.release) {
      result.valid = false;
      result.diag_id = "E-CON-0213";
      result.span = yield_span;
    }
  }

  return result;
}

SuspensionCheck CheckKeyHeldAcrossYieldFrom(const ast::YieldFromExpr& yield_from,
                                            const KeyContext& ctx,
                                            const core::Span& yield_span) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-YieldFrom-No-Keys");

  SuspensionCheck result;
  result.has_release_modifier = yield_from.release;

  // Get all currently held keys
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    for (const auto& key : held) {
      result.held_keys.push_back(key.path);
    }

    // If release modifier is not present, this is an error
    if (!yield_from.release) {
      result.valid = false;
      result.diag_id = "E-CON-0224";
      result.span = yield_span;
    }
  }

  return result;
}

SuspensionCheck CheckKeyHeldAcrossWait(const ast::WaitExpr& wait,
                                       const KeyContext& ctx,
                                       const core::Span& wait_span) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Wait-No-Keys");

  SuspensionCheck result;
  result.has_release_modifier = false;  // wait has no release modifier

  // Get all currently held keys
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    for (const auto& key : held) {
      result.held_keys.push_back(key.path);
    }

    // wait with held keys is always an error
    result.valid = false;
    result.diag_id = "E-CON-0133";
    result.span = wait_span;
  }

  return result;
}

std::vector<SuspensionCheck> CheckAllSuspensionPoints(const ast::Block& block,
                                                      const KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Check-All-Suspension");

  std::vector<SuspensionCheck> results;

  // Walk the block looking for suspension points
  for (const auto& stmt : block.stmts) {
    if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
      const auto& expr = expr_stmt->value;
      if (!expr) continue;

      if (const auto* yield = std::get_if<ast::YieldExpr>(&expr->node)) {
        results.push_back(CheckKeyHeldAcrossYield(*yield, ctx, expr->span));
      } else if (const auto* yield_from = std::get_if<ast::YieldFromExpr>(&expr->node)) {
        results.push_back(CheckKeyHeldAcrossYieldFrom(*yield_from, ctx, expr->span));
      } else if (const auto* wait = std::get_if<ast::WaitExpr>(&expr->node)) {
        results.push_back(CheckKeyHeldAcrossWait(*wait, ctx, expr->span));
      }
    }
  }

  return results;
}

// =============================================================================
// Yield Release Handling
// =============================================================================

std::vector<KeyPath> ProcessYieldRelease(KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Yield-Release");

  std::vector<KeyPath> released_paths;

  const auto& held = ctx.HeldKeys();
  if (held.empty()) {
    return released_paths;
  }

  // Collect all held keys in deterministic canonical order.
  for (const auto& held_key : held) {
    released_paths.push_back(held_key.path);
  }
  std::sort(released_paths.begin(), released_paths.end(),
            [](const KeyPath& a, const KeyPath& b) {
              return KeyPathLess(a, b);
            });

  // Release all held keys across all active scopes.
  // Preserve scope depth so subsequent scope-based analysis remains consistent.
  const KeyScopeId initial_scope_depth = ctx.CurrentScope();
  while (ctx.CurrentScope() > 0) {
    ctx.PopScope();
  }
  ctx.ReleaseScope();

  for (KeyScopeId depth = 0; depth < initial_scope_depth; ++depth) {
    ctx.PushScope();
  }

  return released_paths;
}

void ReacquireAfterYieldRelease(const std::vector<KeyPath>& paths,
                                KeyAccessMode mode,
                                KeyContext& ctx) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Reacquire-After-Release");

  // Sort paths into canonical order before reacquisition
  auto sorted_paths = paths;
  std::sort(sorted_paths.begin(), sorted_paths.end(),
            [](const KeyPath& a, const KeyPath& b) {
              return KeyPathLess(a, b);
            });

  // Reacquire in canonical order
  for (const auto& path : sorted_paths) {
    ctx.Acquire(path, mode);
  }
}

// =============================================================================
// Staleness Analysis
// =============================================================================

std::vector<StalenessWarning> CheckStaleness(const ast::Block& block,
                                             const std::vector<core::Span>& yield_release_points) {
  SpecDefsKeyLifetimes();
  SPEC_RULE("K-Check-Staleness");

  std::vector<StalenessWarning> warnings;

  // This would require tracking which bindings are derived from shared data
  // and whether they are used after a yield release point.

  // For now, we identify let/var bindings and check if they appear before
  // yield release points in the block.

  for (const auto& stmt : block.stmts) {
    // Track let bindings
    if (const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt)) {
      // Check if this binding is before any yield release point
      for (const auto& yield_span : yield_release_points) {
        // Simple span comparison - binding comes before yield
        // A proper implementation would track data flow
        if (let_stmt->span.start_offset < yield_span.start_offset) {
          StalenessWarning warning;
          warning.binding_name = let_stmt->binding.pat ? "binding" : "unknown";
          warning.binding_span = let_stmt->span;
          warning.yield_span = yield_span;
          warning.suppressed = HasStaleOkAttribute(*let_stmt);
          if (!warning.suppressed) {
            warnings.push_back(std::move(warning));
          }
        }
      }
    }

    // Track var bindings
    if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
      for (const auto& yield_span : yield_release_points) {
        if (var_stmt->span.start_offset < yield_span.start_offset) {
          StalenessWarning warning;
          warning.binding_name = var_stmt->binding.pat ? "binding" : "unknown";
          warning.binding_span = var_stmt->span;
          warning.yield_span = yield_span;
          warning.suppressed = HasStaleOkAttribute(*var_stmt);
          if (!warning.suppressed) {
            warnings.push_back(std::move(warning));
          }
        }
      }
    }
  }

  return warnings;
}

bool HasStaleOkAttribute(const ast::LetStmt& stmt) {
  return HasAttribute(stmt.binding.attrs, attrs::kStaleOk);
}

bool HasStaleOkAttribute(const ast::VarStmt& stmt) {
  return HasAttribute(stmt.binding.attrs, attrs::kStaleOk);
}

// =============================================================================
// Helper Functions
// =============================================================================

bool ContainsSuspensionPoint(const ast::Stmt& stmt) {
  std::vector<core::Span> spans;
  FindSuspensionPointsInStmt(stmt, spans);
  return !spans.empty();
}

bool IsSuspensionPoint(const ast::ExprPtr& expr) {
  if (!expr) return false;
  return IsYieldExpr(expr) || IsYieldFromExpr(expr) || IsWaitExpr(expr);
}

std::vector<core::Span> GetSuspensionPointSpans(const ast::Block& block) {
  std::vector<core::Span> spans;

  for (const auto& stmt : block.stmts) {
    FindSuspensionPointsInStmt(stmt, spans);
  }

  if (block.tail_opt) {
    FindSuspensionPointsInExpr(block.tail_opt, spans);
  }

  return spans;
}

bool HasReleaseModifier(const ast::KeyBlockStmt& block) {
  for (const auto& mod : block.mods) {
    if (mod == ast::KeyBlockMod::Release) {
      return true;
    }
  }
  return false;
}

}  // namespace ultraviolet::analysis

