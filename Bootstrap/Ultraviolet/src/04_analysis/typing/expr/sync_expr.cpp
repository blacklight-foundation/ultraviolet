// =================================================================
// File: 04_analysis/typing/expr/sync_expr.cpp
// Construct: Sync Expression Type Checking
// Spec Section: 17.3.5
// Spec Rules: T-Sync
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/composite/unions.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

#include <type_traits>
#include <variant>

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsSync() {
  SPEC_DEF("T-Sync", "17.3.5");
}

struct YieldUsage {
  bool has_yield = false;
  bool has_yield_from = false;
};

// Detect yield/yield from anywhere in an expression tree, including nested
// blocks/statements and key/index/attribute expression children.
struct YieldUsageFinder {
  YieldUsage usage;

  bool Done() const { return usage.has_yield && usage.has_yield_from; }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr || Done()) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            VisitApplyArgs(node.args);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr> ||
                               std::is_same_v<T, ast::BinaryExpr> ||
                               std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::TransmuteExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                               std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place);
      } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
        for (const auto& elem : node.elements) {
          VisitExpr(elem);
        }
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
          VisitExpr(elem);
        });
      } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
        VisitExpr(node.value);
        VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (node.payload_opt.has_value()) {
              VisitEnumPayload(*node.payload_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& arm : node.cases) {
              VisitExpr(arm.body);
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            VisitBlock(node.block);
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitAttributes(node.attrs);
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            VisitExpr(node.body);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
            usage.has_yield = true;
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
            usage.has_yield_from = true;
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              VisitExpr(arm.handler.value);
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            VisitBlock(node.body);
          } else {
            // ErrorExpr, LiteralExpr, IdentifierExpr, QualifiedNameExpr, PathExpr,
            // PtrNullExpr, SizeofExpr, AlignofExpr, ResultExpr, FenceExpr
            // do not contain subexpressions.
          }
        },
        expr->node);
  }

  void VisitBlock(const ast::BlockPtr& block) {
    if (!block || Done()) {
      return;
    }
    for (const auto& stmt : block->stmts) {
      VisitStmt(stmt);
      if (Done()) {
        return;
      }
    }
    VisitExpr(block->tail_opt);
  }

  void VisitStmt(const ast::Stmt& stmt) {
    if (Done()) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            (void)node;
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>) {
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            VisitExpr(node.opts_opt);
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            VisitBlock(node.body);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            VisitBlock(node.body);
          } else {
            // ContinueStmt / ErrorStmt have no subexpressions.
          }
        },
        stmt);
  }

  void VisitApplyArgs(const ast::ApplyArgs& args) {
    if (Done()) {
      return;
    }
    if (std::holds_alternative<ast::ParenArgs>(args)) {
      const auto& paren = std::get<ast::ParenArgs>(args).args;
      for (const auto& arg : paren) {
        VisitExpr(arg.value);
      }
    } else {
      const auto& brace = std::get<ast::BraceArgs>(args).fields;
      for (const auto& field : brace) {
        VisitExpr(field.value);
      }
    }
  }

  void VisitEnumPayload(const ast::EnumPayload& payload) {
    if (Done()) {
      return;
    }
    if (std::holds_alternative<ast::EnumPayloadParen>(payload)) {
      const auto& paren = std::get<ast::EnumPayloadParen>(payload).elements;
      for (const auto& elem : paren) {
        VisitExpr(elem);
      }
    } else {
      const auto& brace = std::get<ast::EnumPayloadBrace>(payload).fields;
      for (const auto& field : brace) {
        VisitExpr(field.value);
      }
    }
  }

  void VisitKeyPath(const ast::KeyPathExpr& path) {
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitAttributes(const ast::AttributeList& attrs) {
    if (Done()) {
      return;
    }
    for (const auto& attr : attrs) {
      for (const auto& arg : attr.args) {
        VisitAttributeArg(arg);
        if (Done()) {
          return;
        }
      }
    }
  }

  void VisitAttributeArg(const ast::AttributeArg& arg) {
    if (Done()) {
      return;
    }
    if (const auto* nested = std::get_if<std::vector<ast::AttributeArg>>(&arg.value)) {
      for (const auto& nested_arg : *nested) {
        VisitAttributeArg(nested_arg);
        if (Done()) {
          return;
        }
      }
    }
  }
};

YieldUsage AnalyzeYieldUsage(const ast::ExprPtr& expr) {
  YieldUsageFinder finder;
  finder.VisitExpr(expr);
  return finder.usage;
}

}  // namespace

// Section 17.3.5 Sync Expression Typing
//
// Typing rule (T-Sync):
// Gamma |- expr : Async<(), (), Result, E>
// Not in async context
// No yield/yield from in expr
// --------------------------------------------------
// Gamma |- sync expr : Result | E
//
// sync runs an async to completion synchronously (blocking).
// Constraints:
// - Only valid in non-async context
// - Async must have Out = () (no yields)
// - Async must have In = () (no inputs)
//
ExprTypeResult TypeSyncExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::SyncExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& /*type_ident*/,
                            const PlaceTypeFn& /*type_place*/) {
  SPEC_RULE("T-Sync");
  ExprTypeResult result;

  const auto yield_usage = AnalyzeYieldUsage(expr.value);

  // Check for yield inside sync expression (disallowed)
  if (yield_usage.has_yield) {
    result.diag_id = "E-CON-0212";
    return result;
  }

  // Check for yield from inside sync expression (disallowed)
  if (yield_usage.has_yield_from) {
    result.diag_id = "E-CON-0223";
    return result;
  }

  // Check that we're not in an async context
  // sync is only valid in non-async procedures
  if (AsyncSigOf(ctx, type_ctx.return_type).has_value()) {
    result.diag_id = "E-CON-0250";  // sync in async context
    return result;
  }

  // Type the async expression
  const auto value_result = type_expr(expr.value);
  if (!value_result.ok) {
    result.diag_id = value_result.diag_id;
    return result;
  }

  // Extract async signature
  const auto async_sig = AsyncSigOf(ctx, value_result.type);
  if (!async_sig.has_value() || !IsPrimType(async_sig->out, "()")) {
    result.diag_id = "E-CON-0251";  // not a valid async for sync
    return result;
  }

  // Async must have In = ()
  if (!IsPrimType(async_sig->in, "()")) {
    result.diag_id = "E-CON-0252";  // async requires input
    return result;
  }

  result.ok = true;
  // If error type is ! (never), the union simplifies to just the result type
  // because T | ! is equivalent to T (! contributes no inhabitants)
  if (IsPrimType(async_sig->err, "!")) {
    result.type = async_sig->result;
  } else {
    const auto union_type = MakeTypeUnion({async_sig->result, async_sig->err});
    if (union_type && std::holds_alternative<TypeUnion>(union_type->node)) {
      const auto intro = TypeUnionIntro(ctx, async_sig->result, union_type);
      if (!intro.ok) {
        result.diag_id = intro.diag_id;
        return result;
      }
      result.type = intro.type;
    } else {
      result.type = union_type;
    }
  }
  return result;
}

}  // namespace ultraviolet::analysis::expr
