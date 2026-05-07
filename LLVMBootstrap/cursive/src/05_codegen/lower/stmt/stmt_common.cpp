// =============================================================================
// Statement Lowering Common Utilities Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.5 (Statement Lowering)
//   - Lines 16586-16756: Statement lowering judgments
//   - LowerStmt dispatch
//   - LowerStmtList, LowerBlock
//   - Temporary cleanup handling
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//
// =============================================================================

#include "05_codegen/lower/stmt/stmt_common.h"

#include <algorithm>
#include <iostream>
#include <variant>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "02_source/ast/ast_utils.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_layout.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/stmt/let_stmt.h"
#include "05_codegen/lower/stmt/var_stmt.h"
#include "05_codegen/lower/stmt/assign_stmt.h"
#include "05_codegen/lower/stmt/compound_assign_stmt.h"
#include "05_codegen/lower/stmt/expr_stmt.h"
#include "05_codegen/lower/stmt/return_stmt.h"
#include "05_codegen/lower/stmt/break_stmt.h"
#include "05_codegen/lower/stmt/continue_stmt.h"
#include "05_codegen/lower/stmt/defer_stmt.h"
#include "05_codegen/lower/stmt/region_stmt.h"
#include "05_codegen/lower/stmt/frame_stmt.h"
#include "05_codegen/lower/stmt/unsafe_block_stmt.h"
#include "05_codegen/lower/stmt/error_stmt.h"
#include "05_codegen/lower/stmt/key_block_stmt.h"
#include "05_codegen/lower/expr/expr_common.h"

namespace cursive::codegen {

// =============================================================================
// Â§6.5 Provenance Information
// =============================================================================

ProvInfo BindProvInfo(const ProvInfo& init) {
  if (init.kind == analysis::ProvenanceKind::Bottom) {
    // Default to stack provenance for bindings
    return ProvInfo{analysis::ProvenanceKind::Stack, std::nullopt, std::nullopt, false};
  }
  return init;
}

ProvInfo ExprProvInfo(const ast::Expr& expr, const LowerCtx& ctx) {
  ProvInfo info;
  if (auto prov = ctx.LookupExprProv(expr)) {
    info.kind = *prov;
  }
  if (info.kind == analysis::ProvenanceKind::Region) {
    info.region = ctx.LookupExprRegion(expr);
    info.region_tag = ctx.LookupExprRegionTag(expr);
  }
  return info;
}

// =============================================================================
// Â§6.5 Block Analysis
// =============================================================================

bool BlockEndsWithReturn(const ast::Block& block) {
  if (block.stmts.empty()) {
    return false;
  }
  return std::holds_alternative<ast::ReturnStmt>(block.stmts.back());
}

bool BlockEndsWithBreak(const ast::Block& block) {
  if (block.stmts.empty()) {
    return false;
  }
  return std::holds_alternative<ast::BreakStmt>(block.stmts.back());
}

bool BlockEndsWithContinue(const ast::Block& block) {
  if (block.stmts.empty()) {
    return false;
  }
  return std::holds_alternative<ast::ContinueStmt>(block.stmts.back());
}

bool BlockDiverges(const ast::Block& block) {
  return BlockEndsWithReturn(block) ||
         BlockEndsWithBreak(block) ||
         BlockEndsWithContinue(block);
}

// =============================================================================
// Â§6.5 Pattern Utilities
// =============================================================================

void CollectPatternNames(const ast::Pattern& pattern,
                         std::vector<std::string>& out) {
  std::visit(
      [&out](const auto& pat) {
        using T = std::decay_t<decltype(pat)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          out.push_back(pat.name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (pat.name == "_") {
            return;
          }
          out.push_back(pat.name);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : pat.elements) {
            if (elem) {
              CollectPatternNames(*elem, out);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : pat.fields) {
            if (field.pattern_opt) {
              CollectPatternNames(*field.pattern_opt, out);
            } else {
              out.push_back(field.name);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!pat.payload_opt) {
            return;
          }
          std::visit(
              [&out](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    if (elem) {
                      CollectPatternNames(*elem, out);
                    }
                  }
                } else {
                  for (const auto& field : payload.fields) {
                    if (field.pattern_opt) {
                      CollectPatternNames(*field.pattern_opt, out);
                    } else {
                      out.push_back(field.name);
                    }
                  }
                }
              },
              *pat.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!pat.fields_opt) {
            return;
          }
          for (const auto& field : pat.fields_opt->fields) {
            if (field.pattern_opt) {
              CollectPatternNames(*field.pattern_opt, out);
            } else {
              out.push_back(field.name);
            }
          }
        }
        // WildcardPattern, LiteralPattern, RangePattern don't introduce names
      },
      pattern.node);
}

std::vector<std::string> PatternNames(const ast::Pattern& pattern) {
  std::vector<std::string> names;
  CollectPatternNames(pattern, names);
  return names;
}

// =============================================================================
// Â§6.5 Loop Utilities
// =============================================================================

analysis::TypeRef LoopPatternType(const analysis::TypeRef& iter_type) {
  if (!iter_type) {
    return nullptr;
  }
  if (std::holds_alternative<analysis::TypeArray>(iter_type->node)) {
    return std::get<analysis::TypeArray>(iter_type->node).element;
  }
  if (std::holds_alternative<analysis::TypeSlice>(iter_type->node)) {
    return std::get<analysis::TypeSlice>(iter_type->node).element;
  }
  // For range types, the element type is the range's element type
  // This would need to look up the Iterator::Item associated type
  return nullptr;
}

analysis::TypeRef LowerBindingType(const std::shared_ptr<ast::Type>& type_opt,
                                   LowerCtx& ctx) {
  if (!type_opt) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, type_opt)) {
    return *lowered;
  }
  return nullptr;
}

// =============================================================================
// Â§6.5 Parallel Collect Scope
// =============================================================================

ParallelCollectScope::ParallelCollectScope(LowerCtx& ctx_in) : ctx(ctx_in) {
  active = ctx.parallel_collect != nullptr;
  if (active) {
    ctx.parallel_collect_depth += 1;
  }
}

ParallelCollectScope::~ParallelCollectScope() {
  if (active) {
    ctx.parallel_collect_depth -= 1;
  }
}

// =============================================================================
// Â§6.5 Temporary Cleanup
// =============================================================================

std::vector<TempValue> TempDropOrder(const std::vector<TempValue>& temps) {
  // Temporaries are dropped in reverse order of creation
  std::vector<TempValue> result = temps;
  std::reverse(result.begin(), result.end());
  return result;
}

IRPtr TempCleanupIR(const std::vector<TempValue>& temps, LowerCtx& ctx) {
  SPEC_RULE("TempDropOrder");
  SPEC_RULE("TempCleanupIR");

  if (temps.empty()) {
    return EmptyIR();
  }

  std::vector<IRPtr> parts;
  auto drop_order = TempDropOrder(temps);
  for (const auto& temp : drop_order) {
    if (temp.type) {
      parts.push_back(EmitDrop(temp.type, temp.value, ctx));
    }
  }

  return SeqIR(std::move(parts));
}

// ============================================================================
// Â§6.5 Statement List Lowering
// ============================================================================

IRPtr LowerStmtList(const std::vector<ast::Stmt>& stmts, LowerCtx& ctx) {
  SPEC_RULE("Lower-StmtList-Empty");
  SPEC_RULE("Lower-StmtList-Cons");

  if (stmts.empty()) {
    return EmptyIR();
  }

  const bool debug_stmt = core::IsDebugEnabled("codegen");
  std::size_t stmt_index = 0;
  std::vector<IRPtr> ir_parts;
  for (const auto& stmt : stmts) {
    ++stmt_index;
    const core::Span stmt_span = ast::span_of(stmt);
    if (debug_stmt) {
      std::cerr << "[lower-stmt-debug] proc="
                << ctx.current_proc_symbol.value_or("<unknown>")
                << " stmt_index=" << stmt_index
                << " line=" << stmt_span.start_line
                << " col=" << stmt_span.start_col
                << " stage=start\n";
    }
    ir_parts.push_back(LowerStmt(stmt, ctx));
    if (debug_stmt) {
      std::cerr << "[lower-stmt-debug] proc="
                << ctx.current_proc_symbol.value_or("<unknown>")
                << " stmt_index=" << stmt_index
                << " line=" << stmt_span.start_line
                << " col=" << stmt_span.start_col
                << " stage=finish\n";
    }
  }

  return SeqIR(std::move(ir_parts));
}

// ============================================================================
// Â§6.5 Main Statement Dispatch
// ============================================================================

IRPtr LowerStmt(const ast::Stmt& stmt, LowerCtx& ctx) {
  std::vector<TempValue> temps;
  auto* prev_sink = ctx.temp_sink;
  ctx.temp_sink = &temps;

  bool temps_handled = false;
  auto is_noop = [](const IRPtr& ir) {
    return !ir || std::holds_alternative<IROpaque>(ir->node);
  };

  IRPtr ir = std::visit(
      [&ctx, &temps, &temps_handled](const auto& node) -> IRPtr {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          SPEC_RULE("Lower-Stmt-Error");
          return LowerErrorStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::LetStmt>) {
          return LowerLetStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          return LowerVarStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          return LowerUsingLocalStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          return LowerAssignStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          return LowerCompoundAssignStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return LowerExprStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return LowerDeferStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return LowerRegionStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return LowerFrameStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          temps_handled = true;
          return LowerReturnStmt(node, ctx, temps);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          temps_handled = true;
          return LowerBreakStmt(node, ctx, temps);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          temps_handled = true;
          return LowerContinueStmt(node, ctx, temps);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          return LowerUnsafeBlockStmt(node, ctx);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return LowerKeyBlockStmt(node, ctx);
        } else {
          // Unknown statement form
          return EmptyIR();
        }
      },
      stmt);

  ctx.temp_sink = prev_sink;

  if (!temps_handled) {
    IRPtr temp_cleanup = CleanupList(temps, ctx);
    if (!is_noop(temp_cleanup)) {
      return SeqIR({ir, temp_cleanup});
    }
  }

  return ir;
}

// ============================================================================
// Â§6.5 CleanupList - temporaries to drop at end of statement
// ============================================================================

IRPtr CleanupList(const std::vector<TempValue>& temps, LowerCtx& ctx) {
  return TempCleanupIR(temps, ctx);
}

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorStmtCommonRules() {
  // Block analysis
  SPEC_RULE("BlockEndsWithReturn");
  SPEC_RULE("BlockEndsWithBreak");
  SPEC_RULE("BlockEndsWithContinue");

  // Provenance
  SPEC_RULE("BindProvInfo-Stack");
  SPEC_RULE("BindProvInfo-Preserve");
  SPEC_RULE("ExprProvInfo");

  // Pattern names
  SPEC_RULE("PatternNames-Ident");
  SPEC_RULE("PatternNames-Typed");
  SPEC_RULE("PatternNames-Tuple");
  SPEC_RULE("PatternNames-Record");
  SPEC_RULE("PatternNames-Enum");
  SPEC_RULE("PatternNames-Modal");

  // Temp cleanup
  SPEC_RULE("TempDropOrder");
  SPEC_RULE("TempCleanupIR");
}

}  // namespace cursive::codegen
