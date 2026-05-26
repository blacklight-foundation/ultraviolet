// =============================================================================
// Key Capture Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   - Docs/SPECIFICATION.md, Section 18.3 "Capture Semantics" (lines 24943-25022)
//   - Docs/SPECIFICATION.md, Section 18.5.3 "Key-Based Parallelism" (lines 25209-25240)
//   - Docs/SPECIFICATION.md, Section 19.4.2 "Key Prohibition in Yield" (lines 25839-25870)
//
// CAPTURE RULES (from spec):
//   - const: Captured by reference, no key needed (Parallel-Closure-Capture-Const)
//   - shared: Captured by reference, key synchronized (Parallel-Closure-Capture-Shared)
//   - unique: MUST use explicit move (Parallel-Closure-Capture-Unique)
//
// KEY PROHIBITION (from spec):
//   - K-Yield-No-Keys: yield without release when keys held -> E-CON-0213
//   - K-YieldFrom-No-Keys: yield from without release when keys held -> E-CON-0224
//   - K-Wait-No-Keys: wait while keys held -> E-CON-0133
//
// DISPATCH KEY PATTERNS (from spec):
//   | Key Pattern   | Keys Generated      | Parallelism Degree    |
//   |---------------|---------------------|------------------------|
//   | data[i]       | n distinct keys     | Full parallel          |
//   | data[i / 2]   | n/2 distinct keys   | Pairs serialize        |
//   | data[i % k]   | k distinct keys     | k-way parallel         |
//   | data[f(i)]    | Unknown at compile  | Runtime serialization  |
//
// =============================================================================

#include "04_analysis/keys/key_capture.h"

#include <algorithm>

#include "00_core/assert_spec.h"
#include "04_analysis/keys/key_conflict.h"
#include "04_analysis/keys/key_paths.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsKeyCapture() {
  SPEC_DEF("KeyCapture", "UVX.18.3");
  SPEC_DEF("DispatchKey", "UVX.18.5.3");
  SPEC_DEF("YieldNoKeys", "UVX.19.4.2");
}

/// Check if an identifier pattern extracts a name
std::optional<std::string> GetPatternName(const ast::Pattern& pat) {
  if (const auto* ident = std::get_if<ast::IdentifierPattern>(&pat.node)) {
    return ident->name;
  }
  return std::nullopt;
}

/// Recursively check if an expression contains a reference to the loop variable
bool ContainsLoopVar(const ast::ExprPtr& expr, const std::string& loop_var) {
  if (!expr) return false;

  // Check for direct identifier
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name == loop_var;
  }

  // Check binary expressions
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    return ContainsLoopVar(binary->lhs, loop_var) ||
           ContainsLoopVar(binary->rhs, loop_var);
  }

  // Check unary expressions
  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    return ContainsLoopVar(unary->value, loop_var);
  }

  // Check index access
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return ContainsLoopVar(index->base, loop_var) ||
           ContainsLoopVar(index->index, loop_var);
  }

  // Check field access
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return ContainsLoopVar(field->base, loop_var);
  }

  // Check method call
  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    if (ContainsLoopVar(method->receiver, loop_var)) return true;
    for (const auto& arg : method->args) {
      if (ContainsLoopVar(arg.value, loop_var)) return true;
    }
    return false;
  }

  // Check call expression
  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    if (ContainsLoopVar(call->callee, loop_var)) return true;
    for (const auto& arg : call->args) {
      if (ContainsLoopVar(arg.value, loop_var)) return true;
    }
    return false;
  }

  // Check cast
  if (const auto* cast = std::get_if<ast::CastExpr>(&expr->node)) {
    return ContainsLoopVar(cast->value, loop_var);
  }

  // Check attributed expression
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return ContainsLoopVar(attributed->expr, loop_var);
  }

  // Check move/address/deref
  if (const auto* move = std::get_if<ast::MoveExpr>(&expr->node)) {
    return ContainsLoopVar(move->place, loop_var);
  }
  if (const auto* address = std::get_if<ast::AddressOfExpr>(&expr->node)) {
    return ContainsLoopVar(address->place, loop_var);
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return ContainsLoopVar(deref->value, loop_var);
  }

  // Check propagate
  if (const auto* propagate = std::get_if<ast::PropagateExpr>(&expr->node)) {
    return ContainsLoopVar(propagate->value, loop_var);
  }

  // Check tuple/array literals
  if (const auto* tuple = std::get_if<ast::TupleExpr>(&expr->node)) {
    for (const auto& element : tuple->elements) {
      if (ContainsLoopVar(element, loop_var)) return true;
    }
    return false;
  }
  if (const auto* array = std::get_if<ast::ArrayExpr>(&expr->node)) {
    bool contains_loop_var = false;
    ast::ForEachArrayExprSubexpr(*array, [&](const ast::ExprPtr& element) {
      if (contains_loop_var) {
        return;
      }
      if (ContainsLoopVar(element, loop_var)) {
        contains_loop_var = true;
      }
    });
    return contains_loop_var;
  }
  if (const auto* repeat = std::get_if<ast::ArrayRepeatExpr>(&expr->node)) {
    return ContainsLoopVar(repeat->value, loop_var) ||
           ContainsLoopVar(repeat->count, loop_var);
  }

  // Check control-flow expressions with nested expressions
  if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    return ContainsLoopVar(if_expr->cond, loop_var) ||
           ContainsLoopVar(if_expr->then_expr, loop_var) ||
           ContainsLoopVar(if_expr->else_expr, loop_var);
  }
  if (const auto* range = std::get_if<ast::RangeExpr>(&expr->node)) {
    return ContainsLoopVar(range->lhs, loop_var) ||
           ContainsLoopVar(range->rhs, loop_var);
  }

  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr->node)) {
    return ContainsLoopVar(dispatch->range, loop_var);
  }

  return false;
}

void CollectIndexAccessesInStmt(const ast::Stmt& stmt,
                                std::vector<ast::ExprPtr>& accesses);

void CollectIndexAccessesInExpr(const ast::ExprPtr& expr,
                                std::vector<ast::ExprPtr>& accesses) {
  if (!expr) {
    return;
  }

  if (std::holds_alternative<ast::IndexAccessExpr>(expr->node)) {
    accesses.push_back(expr);
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(binary->lhs, accesses);
    CollectIndexAccessesInExpr(binary->rhs, accesses);
    return;
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(unary->value, accesses);
    return;
  }

  if (const auto* cast = std::get_if<ast::CastExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(cast->value, accesses);
    return;
  }

  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(index->base, accesses);
    CollectIndexAccessesInExpr(index->index, accesses);
    return;
  }

  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(field->base, accesses);
    return;
  }

  if (const auto* tuple_access = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(tuple_access->base, accesses);
    return;
  }

  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(method->receiver, accesses);
    for (const auto& arg : method->args) {
      CollectIndexAccessesInExpr(arg.value, accesses);
    }
    return;
  }

  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(call->callee, accesses);
    for (const auto& arg : call->args) {
      CollectIndexAccessesInExpr(arg.value, accesses);
    }
    return;
  }

  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(attributed->expr, accesses);
    return;
  }

  if (const auto* move = std::get_if<ast::MoveExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(move->place, accesses);
    return;
  }

  if (const auto* address = std::get_if<ast::AddressOfExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(address->place, accesses);
    return;
  }

  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(deref->value, accesses);
    return;
  }

  if (const auto* propagate = std::get_if<ast::PropagateExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(propagate->value, accesses);
    return;
  }

  if (const auto* tuple = std::get_if<ast::TupleExpr>(&expr->node)) {
    for (const auto& element : tuple->elements) {
      CollectIndexAccessesInExpr(element, accesses);
    }
    return;
  }

  if (const auto* array = std::get_if<ast::ArrayExpr>(&expr->node)) {
    ast::ForEachArrayExprSubexpr(*array, [&](const ast::ExprPtr& element) {
      CollectIndexAccessesInExpr(element, accesses);
    });
    return;
  }

  if (const auto* repeat = std::get_if<ast::ArrayRepeatExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(repeat->value, accesses);
    CollectIndexAccessesInExpr(repeat->count, accesses);
    return;
  }

  if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(if_expr->cond, accesses);
    CollectIndexAccessesInExpr(if_expr->then_expr, accesses);
    CollectIndexAccessesInExpr(if_expr->else_expr, accesses);
    return;
  }

  if (const auto* if_case_expr = std::get_if<ast::IfCaseExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(if_case_expr->scrutinee, accesses);
    for (const auto& arm : if_case_expr->cases) {
      CollectIndexAccessesInExpr(arm.body, accesses);
    }
    CollectIndexAccessesInExpr(if_case_expr->else_expr, accesses);
    return;
  }

  if (const auto* if_is_expr = std::get_if<ast::IfIsExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(if_is_expr->scrutinee, accesses);
    CollectIndexAccessesInExpr(if_is_expr->then_expr, accesses);
    CollectIndexAccessesInExpr(if_is_expr->else_expr, accesses);
    return;
  }

  if (const auto* range = std::get_if<ast::RangeExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(range->lhs, accesses);
    CollectIndexAccessesInExpr(range->rhs, accesses);
    return;
  }

  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&expr->node)) {
    if (block_expr->block) {
      for (const auto& stmt : block_expr->block->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(block_expr->block->tail_opt, accesses);
    }
    return;
  }

  if (const auto* unsafe_block = std::get_if<ast::UnsafeBlockExpr>(&expr->node)) {
    if (unsafe_block->block) {
      for (const auto& stmt : unsafe_block->block->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(unsafe_block->block->tail_opt, accesses);
    }
    return;
  }

  if (const auto* loop_inf = std::get_if<ast::LoopInfiniteExpr>(&expr->node)) {
    if (loop_inf->invariant_opt.has_value()) {
      CollectIndexAccessesInExpr(loop_inf->invariant_opt->predicate, accesses);
    }
    if (loop_inf->body) {
      for (const auto& stmt : loop_inf->body->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(loop_inf->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* loop_cond = std::get_if<ast::LoopConditionalExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(loop_cond->cond, accesses);
    if (loop_cond->invariant_opt.has_value()) {
      CollectIndexAccessesInExpr(loop_cond->invariant_opt->predicate, accesses);
    }
    if (loop_cond->body) {
      for (const auto& stmt : loop_cond->body->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(loop_cond->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* loop_iter = std::get_if<ast::LoopIterExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(loop_iter->iter, accesses);
    if (loop_iter->invariant_opt.has_value()) {
      CollectIndexAccessesInExpr(loop_iter->invariant_opt->predicate, accesses);
    }
    if (loop_iter->body) {
      for (const auto& stmt : loop_iter->body->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(loop_iter->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr->node)) {
    CollectIndexAccessesInExpr(dispatch->range, accesses);
    if (dispatch->body) {
      for (const auto& stmt : dispatch->body->stmts) {
        CollectIndexAccessesInStmt(stmt, accesses);
      }
      CollectIndexAccessesInExpr(dispatch->body->tail_opt, accesses);
    }
  }
}

void CollectIndexAccessesInStmt(const ast::Stmt& stmt,
                                std::vector<ast::ExprPtr>& accesses) {
  if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
    CollectIndexAccessesInExpr(expr_stmt->value, accesses);
    return;
  }

  if (const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt)) {
    CollectIndexAccessesInExpr(let_stmt->binding.init, accesses);
    return;
  }

  if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
    CollectIndexAccessesInExpr(var_stmt->binding.init, accesses);
    return;
  }

  if (const auto* using_local = std::get_if<ast::UsingLocalStmt>(&stmt)) {
    // UsingLocalStmt is a compile-time alias; no runtime expression.
    (void)using_local;
    return;
  }

  if (const auto* assign = std::get_if<ast::AssignStmt>(&stmt)) {
    CollectIndexAccessesInExpr(assign->place, accesses);
    CollectIndexAccessesInExpr(assign->value, accesses);
    return;
  }

  if (const auto* compound = std::get_if<ast::CompoundAssignStmt>(&stmt)) {
    CollectIndexAccessesInExpr(compound->place, accesses);
    CollectIndexAccessesInExpr(compound->value, accesses);
    return;
  }

  if (const auto* ret = std::get_if<ast::ReturnStmt>(&stmt)) {
    CollectIndexAccessesInExpr(ret->value_opt, accesses);
    return;
  }

  if (const auto* brk = std::get_if<ast::BreakStmt>(&stmt)) {
    CollectIndexAccessesInExpr(brk->value_opt, accesses);
    return;
  }

  if (const auto* defer_stmt = std::get_if<ast::DeferStmt>(&stmt)) {
    if (defer_stmt->body) {
      for (const auto& nested : defer_stmt->body->stmts) {
        CollectIndexAccessesInStmt(nested, accesses);
      }
      CollectIndexAccessesInExpr(defer_stmt->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* region = std::get_if<ast::RegionStmt>(&stmt)) {
    CollectIndexAccessesInExpr(region->opts_opt, accesses);
    if (region->body) {
      for (const auto& nested : region->body->stmts) {
        CollectIndexAccessesInStmt(nested, accesses);
      }
      CollectIndexAccessesInExpr(region->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* frame = std::get_if<ast::FrameStmt>(&stmt)) {
    if (frame->body) {
      for (const auto& nested : frame->body->stmts) {
        CollectIndexAccessesInStmt(nested, accesses);
      }
      CollectIndexAccessesInExpr(frame->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* unsafe_block = std::get_if<ast::UnsafeBlockStmt>(&stmt)) {
    if (unsafe_block->body) {
      for (const auto& nested : unsafe_block->body->stmts) {
        CollectIndexAccessesInStmt(nested, accesses);
      }
      CollectIndexAccessesInExpr(unsafe_block->body->tail_opt, accesses);
    }
    return;
  }

  if (const auto* key_block = std::get_if<ast::KeyBlockStmt>(&stmt)) {
    if (key_block->body) {
      for (const auto& nested : key_block->body->stmts) {
        CollectIndexAccessesInStmt(nested, accesses);
      }
      CollectIndexAccessesInExpr(key_block->body->tail_opt, accesses);
    }
    return;
  }
}

std::optional<DispatchKeyPattern> BuildDispatchPatternFromIndexExpr(
    const ast::ExprPtr& expr,
    const std::string& loop_var,
    KeyAccessMode mode) {
  if (!expr) {
    return std::nullopt;
  }
  if (!std::holds_alternative<ast::IndexAccessExpr>(expr->node)) {
    return std::nullopt;
  }
  if (!ContainsLoopVar(expr, loop_var)) {
    return std::nullopt;
  }

  KeyPathResult path_result = BuildKeyPath(expr);
  if (!path_result.success || path_result.path.root.empty()) {
    return std::nullopt;
  }

  DispatchKeyPattern pattern;
  pattern.base_path = std::move(path_result.path);
  pattern.index_var = loop_var;
  pattern.mode = mode;
  pattern.span = expr->span;
  return pattern;
}

std::string DispatchPatternSignature(const DispatchKeyPattern& pattern) {
  std::string signature = KeyPathToCanonical(pattern.base_path);
  signature += "|";
  signature += (pattern.mode == KeyAccessMode::Read) ? "read" : "write";
  signature += "|";
  signature += pattern.index_var.value_or("");
  return signature;
}

void CanonicalizeDispatchPatterns(std::vector<DispatchKeyPattern>& patterns) {
  std::sort(patterns.begin(), patterns.end(),
            [](const DispatchKeyPattern& lhs, const DispatchKeyPattern& rhs) {
              const std::string lhs_sig = DispatchPatternSignature(lhs);
              const std::string rhs_sig = DispatchPatternSignature(rhs);
              if (lhs_sig != rhs_sig) {
                return lhs_sig < rhs_sig;
              }
              if (lhs.span.start_offset != rhs.span.start_offset) {
                return lhs.span.start_offset < rhs.span.start_offset;
              }
              return lhs.span.end_offset < rhs.span.end_offset;
            });

  patterns.erase(
      std::unique(patterns.begin(), patterns.end(),
                  [](const DispatchKeyPattern& lhs, const DispatchKeyPattern& rhs) {
                    return DispatchPatternSignature(lhs) ==
                           DispatchPatternSignature(rhs);
                  }),
      patterns.end());
}

}  // namespace

// =============================================================================
// Key Capture Analysis
// =============================================================================

CapturedKeys AnalyzeKeyCapture(const ast::BlockExpr& block,
                               const KeyContext& outer_ctx) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Capture-Analysis");

  CapturedKeys result;

  // Analysis would walk the block's statements and expressions to find:
  // 1. Free variables referenced from outer scope
  // 2. Their permission levels (const/shared/unique)
  // 3. Whether explicit move is present for unique bindings

  // For now, return empty captures - actual implementation would need
  // integration with the scope/binding tracking infrastructure

  return result;
}

CapturedKeys AnalyzeDispatchCapture(const ast::DispatchExpr& dispatch,
                                    const KeyContext& outer_ctx) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Dispatch-Capture");

  CapturedKeys result;

  // Get the loop variable name
  auto loop_var_opt = ExtractLoopVariable(dispatch.pattern);

  // Analyze the body for captured bindings
  if (dispatch.body) {
    // The body would be analyzed similarly to spawn blocks
    // but with awareness of the dispatch-specific key clause
  }

  // If there's an explicit key clause, add its path to requirements
  if (dispatch.key_clause.has_value()) {
    KeyPath key_path = ParseKeyPathSpec(dispatch.key_clause->key_path);
    result.key_paths.push_back(std::move(key_path));
  }

  return result;
}

CaptureValidation ValidateKeyCapture(const std::vector<CapturedKeys>& spawn_captures,
                                     const core::Span& parallel_span) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Validate-Capture");

  CaptureValidation result;

  // Check that multiple spawn blocks don't have conflicting key captures
  // Two spawns conflict if they both need write access to overlapping paths
  for (size_t i = 0; i < spawn_captures.size(); ++i) {
    for (size_t j = i + 1; j < spawn_captures.size(); ++j) {
      const auto& cap1 = spawn_captures[i];
      const auto& cap2 = spawn_captures[j];

      // Check for key path conflicts
      for (const auto& p1 : cap1.key_paths) {
        for (const auto& p2 : cap2.key_paths) {
          // For spawn blocks, assume write mode for simplicity
          // A more sophisticated analysis would track actual access modes
          if (KeysConflict(p1, KeyAccessMode::Write, p2, KeyAccessMode::Write)) {
            result.ok = false;
            result.diag_id = "E-CON-0060";  // Parallel key conflict
            result.span = parallel_span;
            result.conflicting_path = p1;
            return result;
          }
        }
      }
    }
  }

  // Check for unique captures that aren't moved
  for (const auto& cap : spawn_captures) {
    for (const auto& binding : cap.bindings) {
      if (binding.permission == CapturePermission::Unique &&
          !binding.has_explicit_move) {
        result.ok = false;
        result.diag_id = "E-CON-0120";  // Unique capture without move
        result.span = binding.span;
        result.binding_name = binding.name;
        return result;
      }
    }
  }

  return result;
}

CaptureValidation ValidateSpawnCapture(const CapturedKeys& capture,
                                       const KeyContext& ctx,
                                       const core::Span& spawn_span) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Validate-Spawn-Capture");

  CaptureValidation result;

  // Check that captured keys don't conflict with currently held keys
  for (const auto& path : capture.key_paths) {
    auto conflict = CheckAcquisitionConflict(ctx, path, KeyAccessMode::Write, spawn_span);
    if (conflict.conflict) {
      result.ok = false;
      result.diag_id = conflict.diag_id;
      result.span = conflict.span;
      result.conflicting_path = path;
      return result;
    }
  }

  // Check unique bindings have explicit move
  for (const auto& binding : capture.bindings) {
    if (binding.permission == CapturePermission::Unique) {
      if (!binding.has_explicit_move) {
        result.ok = false;
        result.diag_id = "E-CON-0120";
        result.span = binding.span;
        result.binding_name = binding.name;
        return result;
      }
    }
  }

  return result;
}

// =============================================================================
// Dispatch Key Clause Functions
// =============================================================================

std::vector<DispatchKeyPattern> ComputeDispatchKeys(const ast::DispatchExpr& dispatch) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Dispatch-Keys");

  std::vector<DispatchKeyPattern> patterns;

  // Extract the loop variable
  auto loop_var_opt = ExtractLoopVariable(dispatch.pattern);

  // If there's an explicit key clause, use it
  if (dispatch.key_clause.has_value()) {
    const auto& clause = dispatch.key_clause.value();
    DispatchKeyPattern pattern;
    pattern.base_path = ParseKeyPathSpec(clause.key_path);
    pattern.span = clause.span;

    // Convert ast::KeyMode to analysis::KeyAccessMode
    pattern.mode = (clause.mode == ast::KeyMode::Read)
                       ? KeyAccessMode::Read
                       : KeyAccessMode::Write;

    // Check if the key path uses the loop variable
    // This determines if we get per-iteration keys
    if (loop_var_opt.has_value()) {
      // Walk through the key path segments to find index expressions
      for (const auto& seg : clause.key_path.segs) {
        if (const auto* index_seg = std::get_if<ast::KeySegIndex>(&seg)) {
          if (index_seg->expr && ContainsLoopVar(index_seg->expr, *loop_var_opt)) {
            pattern.index_var = *loop_var_opt;
            break;
          }
        }
      }
    }

    patterns.push_back(std::move(pattern));
    return patterns;
  }

  // No explicit key clause - infer from body
  if (dispatch.body && loop_var_opt.has_value()) {
    patterns = InferDispatchKeyPaths(*dispatch.body, *loop_var_opt);
  }

  return patterns;
}

CaptureValidation ValidateDispatchKeyClause(const ast::DispatchKeyClause& clause,
                                            const KeyContext& ctx) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Validate-Dispatch-Key");

  CaptureValidation result;

  // Convert the key path
  KeyPath path = ParseKeyPathSpec(clause.key_path);

  // Check that the path root is valid (exists in scope)
  if (path.root.empty()) {
    result.ok = false;
    result.diag_id = "E-CON-0031";  // Path not rooted at binding
    result.span = clause.span;
    return result;
  }

  // Check for conflicts with already-held keys
  KeyAccessMode mode = (clause.mode == ast::KeyMode::Read)
                           ? KeyAccessMode::Read
                           : KeyAccessMode::Write;

  auto conflict = CheckAcquisitionConflict(ctx, path, mode, clause.span);
  if (conflict.conflict) {
    result.ok = false;
    result.diag_id = conflict.diag_id;
    result.span = conflict.span;
    result.conflicting_path = path;
    return result;
  }

  return result;
}

bool DispatchKeysDisjoint(const DispatchKeyPattern& p1,
                          const DispatchKeyPattern& p2,
                          const std::string& loop_var) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Disjoint-Dispatch");

  // Two dispatch key patterns are disjoint if:
  // 1. They have different roots (trivially disjoint)
  if (p1.base_path.root != p2.base_path.root) {
    return true;
  }

  // 2. Both use the loop variable in their index positions
  //    This means different iterations will access different elements
  if (p1.IsPerIteration() && p2.IsPerIteration()) {
    if (p1.index_var == loop_var && p2.index_var == loop_var) {
      // Both patterns produce distinct keys per iteration
      // e.g., data[i] and result[i] are disjoint across iterations
      return true;
    }
  }

  // 3. Paths don't overlap at all
  if (!PathsOverlap(p1.base_path, p2.base_path)) {
    return true;
  }

  return false;
}

// =============================================================================
// Yield Key Analysis Functions
// =============================================================================

YieldKeyCheck CheckKeyAcrossYield(const ast::YieldExpr& yield,
                                  const KeyContext& ctx,
                                  const core::Span& yield_span) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Yield-No-Keys");

  YieldKeyCheck result;
  result.has_release = yield.release;

  // Check if any keys are currently held
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    result.keys_held = true;
    for (const auto& key : held) {
      result.held_paths.push_back(key.path);
    }

    // If release modifier is not present, this is an error
    if (!yield.release) {
      result.diag_id = "E-CON-0213";  // yield while key held
      result.span = yield_span;
    }
  }

  return result;
}

YieldKeyCheck CheckKeyAcrossYieldFrom(const ast::YieldFromExpr& yield_from,
                                      const KeyContext& ctx,
                                      const core::Span& yield_span) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-YieldFrom-No-Keys");

  YieldKeyCheck result;
  result.has_release = yield_from.release;

  // Check if any keys are currently held
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    result.keys_held = true;
    for (const auto& key : held) {
      result.held_paths.push_back(key.path);
    }

    // If release modifier is not present, this is an error
    if (!yield_from.release) {
      result.diag_id = "E-CON-0224";  // yield from while key held
      result.span = yield_span;
    }
  }

  return result;
}

YieldKeyCheck CheckKeyAcrossWait(const ast::WaitExpr& wait,
                                 const KeyContext& ctx,
                                 const core::Span& wait_span) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Wait-No-Keys");

  YieldKeyCheck result;
  result.has_release = false;  // wait doesn't have release modifier

  // Check if any keys are currently held
  const auto& held = ctx.HeldKeys();
  if (!held.empty()) {
    result.keys_held = true;
    for (const auto& key : held) {
      result.held_paths.push_back(key.path);
    }

    // wait with held keys is always an error
    result.diag_id = "E-CON-0133";  // wait while key held
    result.span = wait_span;
  }

  return result;
}

// =============================================================================
// Helper Functions
// =============================================================================

std::optional<std::string> ExtractLoopVariable(const ast::PatternPtr& pattern) {
  if (!pattern) return std::nullopt;
  return GetPatternName(*pattern);
}

bool ExpressionUsesLoopVar(const ast::ExprPtr& expr, const std::string& loop_var) {
  return ContainsLoopVar(expr, loop_var);
}

std::vector<DispatchKeyPattern> InferDispatchKeyPaths(const ast::Block& body,
                                                      const std::string& loop_var) {
  SpecDefsKeyCapture();
  SPEC_RULE("K-Infer-Dispatch-Keys");

  std::vector<DispatchKeyPattern> patterns;

  // Walk through body statements looking for indexed accesses
  // that use the loop variable

  // For each statement in the body
  for (const auto& stmt : body.stmts) {
    // Look for assignment statements like: data[i] = ...
    if (const auto* assign = std::get_if<ast::AssignStmt>(&stmt)) {
      if (assign->place &&
          std::holds_alternative<ast::IndexAccessExpr>(assign->place->node)) {
        auto pattern =
            BuildDispatchPatternFromIndexExpr(assign->place, loop_var, KeyAccessMode::Write);
        if (pattern.has_value()) {
          patterns.push_back(std::move(*pattern));
        }
      }
    }

    // Look for compound assignment statements like: data[i] += ...
    if (const auto* compound = std::get_if<ast::CompoundAssignStmt>(&stmt)) {
      if (compound->place &&
          std::holds_alternative<ast::IndexAccessExpr>(compound->place->node)) {
        auto pattern =
            BuildDispatchPatternFromIndexExpr(compound->place, loop_var, KeyAccessMode::Write);
        if (pattern.has_value()) {
          patterns.push_back(std::move(*pattern));
        }
      }
    }

    // Look for expression statements with indexed reads.
    if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
      std::vector<ast::ExprPtr> index_accesses;
      CollectIndexAccessesInExpr(expr_stmt->value, index_accesses);
      for (const auto& index_expr : index_accesses) {
        auto pattern =
            BuildDispatchPatternFromIndexExpr(index_expr, loop_var, KeyAccessMode::Read);
        if (pattern.has_value()) {
          patterns.push_back(std::move(*pattern));
        }
      }
    }
  }

  CanonicalizeDispatchPatterns(patterns);
  return patterns;
}

}  // namespace ultraviolet::analysis

