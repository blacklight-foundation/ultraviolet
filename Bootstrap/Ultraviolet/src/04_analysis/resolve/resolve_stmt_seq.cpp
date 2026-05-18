// =============================================================================
// resolve_stmt_seq.cpp - Statement Sequence and Block Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   Docs/SPECIFICATION.md §5.1.2 "Name Introduction and Shadowing" (Lines 6718-6821)
//
// CONTENT:
//   1. ResolveStmtSeq - Resolve a sequence of statements
//   2. ResolveBlock - Resolve a block (statements + optional tail expression)
//   3. ResolveExprOpt - Helper for optional expression resolution
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsStmtSeq() {
  SPEC_DEF("ResolveStmtSeq", "5.1.7");
  SPEC_DEF("ResolveBlock", "5.1.7");
  SPEC_DEF("ResolveBlockStmts", "5.1.7");
  SPEC_DEF("ResolveBlockBody", "5.1.7");
}

// -----------------------------------------------------------------------------
// Scope Guard Helper
// -----------------------------------------------------------------------------
// RAII-style scope management for block resolution.
// Pushes a new scope on construction, pops on destruction.
// -----------------------------------------------------------------------------

struct ScopeGuard {
  ScopeContext* ctx = nullptr;
  bool active = false;

  explicit ScopeGuard(ScopeContext& ctx_in) : ctx(&ctx_in), active(true) {
    ctx->scopes.insert(ctx->scopes.begin(), Scope{});
  }

  ~ScopeGuard() {
    if (active && ctx && !ctx->scopes.empty()) {
      ctx->scopes.erase(ctx->scopes.begin());
    }
  }

  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
};

// -----------------------------------------------------------------------------
// Optional Expression Resolution Helper
// -----------------------------------------------------------------------------

ResExprResult ResolveExprOpt(ResolveContext& ctx,
                             const ast::ExprPtr& expr_opt) {
  if (!expr_opt) {
    SPEC_RULE("ResolveExprOpt-None");
    return {true, std::nullopt, std::nullopt, expr_opt};
  }
  const auto resolved = ResolveExpr(ctx, expr_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {},
            resolved.diag_detail, resolved.diag_children};
  }
  SPEC_RULE("ResolveExprOpt-Some");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

}  // namespace

// =============================================================================
// Public Interface
// =============================================================================

// -----------------------------------------------------------------------------
// ResolveStmtSeq
// -----------------------------------------------------------------------------
// Resolves a sequence of statements in order.
// Bindings introduced by earlier statements are visible to later statements.
// This is the core of context threading through statement sequences.
//
// Implements (Resolve-Stmt-Seq) from §5.1.7:
//   stmts = [s_1, ..., s_n] ∧
//   Γ_0 = Γ ∧
//   ∀ i ∈ 1..n. Γ_{i-1} ⊢ ResolveStmt(s_i) ⇓ Γ_i
//   → Γ ⊢ ResolveStmtSeq(stmts) ⇓ ok
// -----------------------------------------------------------------------------

ResolveStmtSeqResult ResolveStmtSeq(ResolveContext& ctx,
                                    const std::vector<ast::Stmt>& stmts) {
  SpecDefsStmtSeq();
  ResolveStmtSeqResult result;
  result.ok = true;

  if (stmts.empty()) {
    SPEC_RULE("ResolveStmtSeq-Empty");
    return result;
  }

  result.stmts.reserve(stmts.size());
  for (const auto& stmt : stmts) {
    const auto resolved = ResolveStmt(ctx, stmt);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.stmts.push_back(resolved.stmt);
    SPEC_RULE("ResolveStmtSeq-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveBlock
// -----------------------------------------------------------------------------
// Resolves a block consisting of a statement sequence and optional tail
// expression. The block introduces a new scope for bindings.
//
// Implements (Resolve-Block-Stmts) from §5.1.7:
//   Γ ⊢ PushScope(S_block) ⇓ Γ₁ ∧
//   Γ₁ ⊢ ResolveStmtSeq(stmts) ⇓ ok ∧
//   (tail ≠ ⊥ → Γ₁ ⊢ ResolveExpr(tail) ⇓ ok) ∧
//   Γ₁ ⊢ PopScope ⇓ Γ
//   → Γ ⊢ ResolveBlock(block) ⇓ ok
//
// The tail expression is resolved after all statements, using the
// accumulated context from statement resolution. This allows the tail
// expression to reference any bindings introduced by statements.
// -----------------------------------------------------------------------------

ResolveBlockResult ResolveBlock(ResolveContext& ctx,
                                const ast::Block& block) {
  SpecDefsStmtSeq();
  ResolveBlockResult result;

  if (!ctx.ctx) {
    return result;
  }

  // Push a new scope for the block
  ScopeGuard guard(*ctx.ctx);

  // Resolve statements in order
  const auto stmts = ResolveStmtSeq(ctx, block.stmts);
  if (!stmts.ok) {
    return {false, stmts.diag_id, stmts.span, {},
            stmts.diag_detail, stmts.diag_children};
  }

  // Resolve tail expression if present (uses accumulated context)
  const auto tail = ResolveExprOpt(ctx, block.tail_opt);
  if (!tail.ok) {
    return {false, tail.diag_id, tail.span, {},
            tail.diag_detail, tail.diag_children};
  }

  // Build result block
  result.ok = true;
  result.block = block;
  result.block.stmts = stmts.stmts;
  result.block.tail_opt = tail.value;

  SPEC_RULE("ResolveBlock-Ok");
  return result;
}

}  // namespace ultraviolet::analysis
