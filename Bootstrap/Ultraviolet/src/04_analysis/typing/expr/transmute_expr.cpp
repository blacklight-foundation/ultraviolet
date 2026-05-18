// =================================================================
// File: 04_analysis/typing/expr/transmute_expr.cpp
// Construct: Transmute Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Transmute, T-Transmute-SizeEq, T-Transmute-AlignEq,
//             Transmute-Unsafe-Err, W-Transmute-Invalid-Target
// =================================================================
#include "04_analysis/typing/expr/transmute_expr.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/layout/layout.h"

#include <type_traits>

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsTransmute() {
  SPEC_DEF("T-Transmute", "5.2.12");
  SPEC_DEF("T-Transmute-SizeEq", "5.2.12");
  SPEC_DEF("T-Transmute-AlignEq", "5.2.12");
  SPEC_DEF("Transmute-Unsafe-Err", "5.2.12");
  SPEC_DEF("W-Transmute-Invalid-Target", "5.2.12");
}

std::optional<std::uint64_t> SizeOfInternal(const ScopeContext& ctx,
                                            const TypeRef& type) {
  const auto layout = layout::LayoutOf(ctx, type);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  return layout->size;
}

std::optional<std::uint64_t> AlignOfInternal(const ScopeContext& ctx,
                                             const TypeRef& type) {
  const auto layout = layout::LayoutOf(ctx, type);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  return layout->align;
}

static bool IsNumericPrim(std::string_view name) {
  static constexpr std::string_view kNames[] = {
      "i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16",
      "u32", "u64", "u128", "usize", "f16", "f32", "f64"};
  for (const auto candidate : kNames) {
    if (name == candidate) {
      return true;
    }
  }
  return false;
}

static bool KnownInvalidTransmuteTarget(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          if (IsNumericPrim(node.name)) {
            return false;
          }
          return IdEq(node.name, "bool") || IdEq(node.name, "char");
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return KnownInvalidTransmuteTarget(node.element);
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return KnownInvalidTransmuteTarget(node.base);
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          return false;
        } else {
          return true;
        }
      },
      stripped->node);
}

static bool SameSpan(const core::Span& lhs, const core::Span& rhs) {
  return lhs.file == rhs.file &&
         lhs.start_offset == rhs.start_offset &&
         lhs.end_offset == rhs.end_offset &&
         lhs.start_line == rhs.start_line &&
         lhs.start_col == rhs.start_col &&
         lhs.end_line == rhs.end_line &&
         lhs.end_col == rhs.end_col;
}

static void EmitInvalidTargetWarning(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const core::Span& span) {
  auto* diags = type_ctx.diags ? type_ctx.diags : ctx.diagnostics;
  if (!diags) {
    return;
  }
  for (const auto& diag : *diags) {
    if (diag.code == "W-SAFE-0100" && diag.span.has_value() &&
        SameSpan(*diag.span, span)) {
      return;
    }
  }
  auto diag = core::MakeDiagnosticById(
      "W-SAFE-0100", std::optional<core::Span>(span));
  if (diag) {
    core::Emit(*diags, *diag);
  }
}

static void EmitInvalidTargetWarningsInExpr(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::ExprPtr& expr,
                                            const TypeEnv& env);

static void EmitInvalidTargetWarningsInStmt(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::Stmt& stmt,
                                            const TypeEnv& env) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.place, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.place, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body) {
            EmitInvalidTransmuteTargetWarningsInBlock(ctx, type_ctx,
                                                      *node.body, env);
          }
          if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.opts_opt, env);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value_opt, env);
        }
      },
      stmt);
}

static void EmitInvalidTargetWarningsInExpr(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::ExprPtr& expr,
                                            const TypeEnv& env) {
  if (!expr) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          const auto lowered = LowerType(ctx, node.to);
          if (lowered.ok && KnownInvalidTransmuteTarget(lowered.type)) {
            EmitInvalidTargetWarning(ctx, type_ctx, expr->span);
          }
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.expr, env);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr> ||
                             std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            EmitInvalidTransmuteTargetWarningsInBlock(ctx, type_ctx,
                                                      *node.block, env);
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.lhs, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::CastExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.place, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, elem, env);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          for (const auto& segment : node.elements) {
            if (const auto* elem = std::get_if<ast::ArrayElemSegment>(&segment)) {
              EmitInvalidTargetWarningsInExpr(ctx, type_ctx, elem->value, env);
            } else if (const auto* repeat =
                           std::get_if<ast::ArrayRepeatSegment>(&segment)) {
              EmitInvalidTargetWarningsInExpr(ctx, type_ctx, repeat->value, env);
              EmitInvalidTargetWarningsInExpr(ctx, type_ctx, repeat->count, env);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.value, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.count, env);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, field.value, env);
          }
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.base, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.index, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.callee, env);
          for (const auto& arg : node.args) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, arg.value, env);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.receiver, env);
          for (const auto& arg : node.args) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, arg.value, env);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.cond, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.then_expr, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.scrutinee, env);
          for (const auto& case_clause : node.cases) {
            EmitInvalidTargetWarningsInExpr(ctx, type_ctx, case_clause.body, env);
          }
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.lhs, env);
          EmitInvalidTargetWarningsInExpr(ctx, type_ctx, node.rhs, env);
        }
      },
      expr->node);
}

}  // namespace

// Section 5.5 Transmute Expression Typing
//
// Typing rule (T-Transmute):
// InUnsafe = true
// Gamma |- expr : From
// Size(From) = Size(To)
// Align(From) = Align(To)
// --------------------------------------------------
// Gamma |- transmute<From, To>(expr) : To
//
// transmute reinterprets the bit pattern of a value as a different type.
// This is extremely unsafe and requires:
// - Being inside an unsafe block
// - From and To types having the same size
// - From and To types having the same alignment
//
ExprTypeResult TypeTransmuteExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::TransmuteExpr& expr,
                                     const TypeEnv& env,
                                     const core::Span& span) {
  ExprTypeResult result;

  // Transmute not allowed in pure context (contracts)
  if (type_ctx.require_pure) {
    result.diag_id = "E-SEM-2802";
    return result;
  }

  // Check unsafe context requirement
  if (!IsInUnsafeSpan(ctx, span)) {
    SPEC_RULE("Transmute-Unsafe-Err");
    result.diag_id = "Transmute-Unsafe-Err";
    return result;
  }

  // Validate required fields exist
  if (!expr.from || !expr.to || !expr.value) {
    return result;
  }

  // Lower the From type
  const auto from = LowerType(ctx, expr.from);
  if (!from.ok) {
    result.diag_id = from.diag_id;
    return result;
  }

  // Check From type is well-formed
  const auto from_wf = TypeWF(ctx, from.type);
  if (!from_wf.ok) {
    result.diag_id = from_wf.diag_id;
    return result;
  }

  // Lower the To type
  const auto to = LowerType(ctx, expr.to);
  if (!to.ok) {
    result.diag_id = to.diag_id;
    return result;
  }

  // Check To type is well-formed
  const auto to_wf = TypeWF(ctx, to.type);
  if (!to_wf.ok) {
    result.diag_id = to_wf.diag_id;
    return result;
  }

  // Check size compatibility
  const auto from_size = SizeOfInternal(ctx, from.type);
  const auto to_size = SizeOfInternal(ctx, to.type);
  if (!from_size.has_value() || !to_size.has_value()) {
    return result;
  }
  if (*from_size != *to_size) {
    SPEC_RULE("T-Transmute-SizeEq");
    result.diag_id = "T-Transmute-SizeEq";
    return result;
  }
  SPEC_RULE("T-Transmute-SizeEq");

  // Check alignment compatibility
  const auto from_align = AlignOfInternal(ctx, from.type);
  const auto to_align = AlignOfInternal(ctx, to.type);
  if (!from_align.has_value() || !to_align.has_value()) {
    return result;
  }
  if (*from_align != *to_align) {
    SPEC_RULE("T-Transmute-AlignEq");
    result.diag_id = "T-Transmute-AlignEq";
    return result;
  }
  SPEC_RULE("T-Transmute-AlignEq");

  const auto value_check =
      CheckExprAgainst(ctx, type_ctx, expr.value, from.type, env);
  if (!value_check.ok) {
    result.diag_id = value_check.diag_id;
    result.diag_detail = value_check.diag_detail;
    result.diag_span = value_check.diag_span;
    return result;
  }

  if (KnownInvalidTransmuteTarget(to.type)) {
    SPEC_RULE("W-Transmute-Invalid-Target");
    EmitInvalidTargetWarning(ctx, type_ctx, span);
  }

  SPEC_RULE("T-Transmute");
  result.ok = true;
  result.type = to.type;
  return result;
}

void EmitInvalidTransmuteTargetWarningsInBlock(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const ast::Block& block,
                                               const TypeEnv& env) {
  for (const auto& stmt : block.stmts) {
    EmitInvalidTargetWarningsInStmt(ctx, type_ctx, stmt, env);
  }
  EmitInvalidTargetWarningsInExpr(ctx, type_ctx, block.tail_opt, env);
}

}  // namespace ultraviolet::analysis::expr
