// =============================================================================
// key_block_stmt.cpp - Key block statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 17.1: Keys and Synchronization (lines 24089+)
//   - Key block grammar (line 24094)
//   - Key acquisition semantics (line 24123)
//   - Nested keys (line 24450)
//   - Speculative blocks (line 24536)
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/keys/key_conflict.h"
#include "04_analysis/keys/key_lifetimes.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_layout.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/resolve/scopes.h"
#include "00_core/diagnostic_messages.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsKeyBlockStmt() {
  SPEC_DEF("T-KeyBlockStmt", "17.1");
  SPEC_DEF("KeyAcquisition", "17.1");
  SPEC_DEF("KeyRelease", "17.1");
  SPEC_DEF("NestedKeys", "17.1");
  SPEC_DEF("SpeculativeExec", "17.1");
  SPEC_DEF("NonIndexShape", "17.3.7");
  SPEC_DEF("OrderedBase", "17.3.7");
  SPEC_DEF("OrderedComparable", "17.3.7");
  SPEC_DEF("StaticallyComparableIndices", "17.3.7");
  SPEC_DEF("K-Ordered-Ok", "17.3.7");
  SPEC_DEF("K-Ordered-Base-Err", "17.3.7");
  SPEC_DEF("K-Ordered-Redundant-Warn", "17.3.7");
  SPEC_DEF("K-Dynamic-Index-Conflict", "17.3.2");
  SPEC_DEF("K-Static-Required", "17.3.2");
  SPEC_DEF("K-Read-Block-No-Write", "17.2.1");
  SPEC_DEF("K-Read-Write-Reject", "17.2.1");
  SPEC_DEF("K-Release-SameMode-Err", "19.4.4");
  SPEC_DEF("K-Nested-Same-Path", "19.4.4");
  SPEC_DEF("K-Reentrant", "19.4.4");
  SPEC_DEF("KeyBlock-GPU-Err", "17.1");
}

static bool KeyPathEqual(const KeyPath& lhs, const KeyPath& rhs) {
  return !KeyPathLess(lhs, rhs) && !KeyPathLess(rhs, lhs);
}

static std::vector<HeldKeyTypingInfo> CanonicalHeldKeyInfos(
    const std::vector<ast::KeyPathExpr>& paths,
    ast::KeyMode mode) {
  std::vector<HeldKeyTypingInfo> infos;
  infos.reserve(paths.size());
  for (const auto& path : paths) {
    infos.push_back(HeldKeyTypingInfo{ParseKeyPathSpec(path), mode});
  }
  std::sort(infos.begin(), infos.end(),
            [](const HeldKeyTypingInfo& lhs, const HeldKeyTypingInfo& rhs) {
              return KeyPathLess(lhs.path, rhs.path);
            });
  infos.erase(std::unique(infos.begin(), infos.end(),
                          [](const HeldKeyTypingInfo& lhs,
                             const HeldKeyTypingInfo& rhs) {
                            return KeyPathEqual(lhs.path, rhs.path);
                          }),
              infos.end());
  return infos;
}

static bool IsDynamicIndexExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr) {
  const auto const_len = ConstLen(ctx, expr);
  return !(const_len.ok && const_len.value.has_value());
}

static ast::ExprPtr MakeExprNode(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static bool ExprUsesAnyNameFromSet(const ast::ExprPtr& expr,
                                   const std::unordered_set<IdKey>& names);

static bool ExprUsesAnyNameFromSet(const ast::ExprPtr& expr,
                                   const std::unordered_set<IdKey>& names) {
  if (!expr || names.empty()) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return names.find(IdKeyOf(node.name)) != names.end();
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprUsesAnyNameFromSet(node.expr, names);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::PropagateExpr>) {
          return ExprUsesAnyNameFromSet(node.value, names);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return ExprUsesAnyNameFromSet(node.place, names);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprUsesAnyNameFromSet(node.lhs, names) ||
                 ExprUsesAnyNameFromSet(node.rhs, names);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprUsesAnyNameFromSet(node.base, names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprUsesAnyNameFromSet(node.base, names) ||
                 ExprUsesAnyNameFromSet(node.index, names);
        }
        return false;
      },
      expr->node);
}

struct AffineIndexExpr {
  bool ok = false;
  std::string base_name;
  std::int64_t offset = 0;
};

struct LoopRangeBounds {
  bool ok = false;
  std::int64_t lo = 0;
  std::int64_t hi = 0;  // exclusive
};

static bool TryConstI64(const ast::ExprPtr& expr, std::int64_t& value) {
  const auto constant = EvaluateConstant(expr);
  if (!constant.known || constant.is_bool) {
    return false;
  }
  value = constant.value;
  return true;
}

static LoopRangeBounds ExtractLoopRangeBounds(const ast::ExprPtr& expr) {
  LoopRangeBounds out;
  if (!expr) {
    return out;
  }
  const auto* range = std::get_if<ast::RangeExpr>(&expr->node);
  if (!range) {
    return out;
  }

  std::int64_t lo = 0;
  std::int64_t hi = 0;
  switch (range->kind) {
    case ast::RangeKind::Exclusive:
      if (!TryConstI64(range->lhs, lo) || !TryConstI64(range->rhs, hi)) {
        return out;
      }
      break;
    case ast::RangeKind::Inclusive:
      if (!TryConstI64(range->lhs, lo) || !TryConstI64(range->rhs, hi)) {
        return out;
      }
      hi += 1;
      break;
    default:
      return out;
  }

  out.ok = true;
  out.lo = lo;
  out.hi = hi;
  return out;
}

static AffineIndexExpr ParseAffineIndexExpr(const ast::ExprPtr& expr) {
  AffineIndexExpr out;
  if (!expr) {
    return out;
  }

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    out.ok = true;
    out.base_name = ident->name;
    out.offset = 0;
    return out;
  }

  if (const auto* path = std::get_if<ast::PathExpr>(&expr->node)) {
    if (path->path.empty()) {
      out.ok = true;
      out.base_name = path->name;
      out.offset = 0;
    }
    return out;
  }

  if (const auto* attr = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return ParseAffineIndexExpr(attr->expr);
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    if (binary->op != "+" && binary->op != "-") {
      return out;
    }

    std::int64_t rhs_const = 0;
    if (TryConstI64(binary->rhs, rhs_const)) {
      auto lhs = ParseAffineIndexExpr(binary->lhs);
      if (!lhs.ok) {
        return out;
      }
      lhs.offset += (binary->op == "+") ? rhs_const : -rhs_const;
      return lhs;
    }

    std::int64_t lhs_const = 0;
    if (binary->op == "+" && TryConstI64(binary->lhs, lhs_const)) {
      auto rhs = ParseAffineIndexExpr(binary->rhs);
      if (!rhs.ok) {
        return out;
      }
      rhs.offset += lhs_const;
      return rhs;
    }
  }

  return out;
}

static bool ProvablyEquivalentIndexExpr(const ScopeContext& ctx,
                                        const TypeEnv& env,
                                        const ast::ExprPtr& lhs,
                                        const ast::ExprPtr& rhs) {
  if (!lhs || !rhs) {
    return false;
  }
  if (ExprStructEqual(lhs, rhs)) {
    return true;
  }

  const auto lhs_const = EvaluateConstant(lhs);
  const auto rhs_const = EvaluateConstant(rhs);
  if (lhs_const.known && rhs_const.known &&
      lhs_const.is_bool == rhs_const.is_bool &&
      lhs_const.value == rhs_const.value &&
      lhs_const.bool_value == rhs_const.bool_value) {
    return true;
  }

  if (const auto* lhs_ident = std::get_if<ast::IdentifierExpr>(&lhs->node)) {
    if (const auto* rhs_ident = std::get_if<ast::IdentifierExpr>(&rhs->node)) {
      return IdEq(lhs_ident->name, rhs_ident->name) &&
             BindOf(env, lhs_ident->name).has_value() &&
             BindOf(env, rhs_ident->name).has_value();
    }
  }

  if (const auto* lhs_path = std::get_if<ast::PathExpr>(&lhs->node)) {
    if (const auto* rhs_path = std::get_if<ast::PathExpr>(&rhs->node)) {
      return lhs_path->path == rhs_path->path && IdEq(lhs_path->name, rhs_path->name);
    }
  }

  (void)ctx;
  return false;
}

static bool ProofEstablishesNotEqual(const StmtTypeContext& type_ctx,
                                     const ast::ExprPtr& lhs,
                                     const ast::ExprPtr& rhs) {
  if (!type_ctx.proof_ctx || !lhs || !rhs) {
    return false;
  }
  ast::BinaryExpr neq;
  neq.op = "!=";
  neq.lhs = lhs;
  neq.rhs = rhs;
  auto pred = MakeExprNode(lhs->span, neq);
  return StaticProof(*type_ctx.proof_ctx, pred).provable;
}

static bool ProvablyDisjointIndexExpr(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const TypeEnv& env,
                                      const ast::ExprPtr& lhs,
                                      const ast::ExprPtr& rhs) {
  if (!lhs || !rhs) {
    return false;
  }

  const auto lhs_const = EvaluateConstant(lhs);
  const auto rhs_const = EvaluateConstant(rhs);
  if (lhs_const.known && rhs_const.known &&
      lhs_const.is_bool == rhs_const.is_bool) {
    if (lhs_const.is_bool) {
      return lhs_const.bool_value != rhs_const.bool_value;
    }
    return lhs_const.value != rhs_const.value;
  }

  if (ProofEstablishesNotEqual(type_ctx, lhs, rhs)) {
    return true;
  }

  const auto lhs_affine = ParseAffineIndexExpr(lhs);
  const auto rhs_affine = ParseAffineIndexExpr(rhs);
  if (lhs_affine.ok && rhs_affine.ok &&
      IdEq(lhs_affine.base_name, rhs_affine.base_name) &&
      lhs_affine.offset != rhs_affine.offset) {
    return true;
  }

  if (type_ctx.parallel_bindings &&
      (ExprUsesAnyNameFromSet(lhs, *type_ctx.parallel_bindings) ||
       ExprUsesAnyNameFromSet(rhs, *type_ctx.parallel_bindings))) {
    return true;
  }

  if (type_ctx.loop_iteration_ranges) {
    const auto* lhs_ident = std::get_if<ast::IdentifierExpr>(&lhs->node);
    const auto* rhs_ident = std::get_if<ast::IdentifierExpr>(&rhs->node);
    if (lhs_ident && rhs_ident && !IdEq(lhs_ident->name, rhs_ident->name)) {
      const auto lhs_it =
          type_ctx.loop_iteration_ranges->find(IdKeyOf(lhs_ident->name));
      const auto rhs_it =
          type_ctx.loop_iteration_ranges->find(IdKeyOf(rhs_ident->name));
      if (lhs_it != type_ctx.loop_iteration_ranges->end() &&
          rhs_it != type_ctx.loop_iteration_ranges->end()) {
        const auto lhs_bounds = ExtractLoopRangeBounds(lhs_it->second);
        const auto rhs_bounds = ExtractLoopRangeBounds(rhs_it->second);
        if (lhs_bounds.ok && rhs_bounds.ok &&
            (lhs_bounds.hi <= rhs_bounds.lo ||
             rhs_bounds.hi <= lhs_bounds.lo)) {
          return true;
        }
      }
    }
  }

  (void)ctx;
  (void)env;
  return false;
}

static bool IsMemoryOrderAttributeName(std::string_view name) {
  return name == attrs::kRelaxed ||
         name == attrs::kAcquire ||
         name == attrs::kRelease ||
         name == attrs::kAcqRel ||
         name == attrs::kSeqCst;
}

static void CollectYieldReleasePointsExpr(
    const ast::ExprPtr& expr,
    std::vector<core::Span>& out) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          if (node.release) {
            out.push_back(expr->span);
          }
          CollectYieldReleasePointsExpr(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          if (node.release) {
            out.push_back(expr->span);
          }
          CollectYieldReleasePointsExpr(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          CollectYieldReleasePointsExpr(node.expr, out);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CollectYieldReleasePointsExpr(node.lhs, out);
          CollectYieldReleasePointsExpr(node.rhs, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CollectYieldReleasePointsExpr(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CollectYieldReleasePointsExpr(node.base, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          CollectYieldReleasePointsExpr(node.base, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CollectYieldReleasePointsExpr(node.base, out);
          CollectYieldReleasePointsExpr(node.index, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CollectYieldReleasePointsExpr(node.callee, out);
          for (const auto& arg : node.args) {
            CollectYieldReleasePointsExpr(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CollectYieldReleasePointsExpr(node.receiver, out);
          for (const auto& arg : node.args) {
            CollectYieldReleasePointsExpr(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            CollectYieldReleasePointsExpr(elem, out);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            CollectYieldReleasePointsExpr(elem, out);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          CollectYieldReleasePointsExpr(node.value, out);
          CollectYieldReleasePointsExpr(node.count, out);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            CollectYieldReleasePointsExpr(field.value, out);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      CollectYieldReleasePointsExpr(elem, out);
                    }
                  } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                    for (const auto& field : payload.fields) {
                      CollectYieldReleasePointsExpr(field.value, out);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CollectYieldReleasePointsExpr(node.cond, out);
          CollectYieldReleasePointsExpr(node.then_expr, out);
          CollectYieldReleasePointsExpr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          CollectYieldReleasePointsExpr(node.scrutinee, out);
          for (const auto& case_clause : node.cases) {
            CollectYieldReleasePointsExpr(case_clause.body, out);
          }
          CollectYieldReleasePointsExpr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CollectYieldReleasePointsExpr(node.scrutinee, out);
          CollectYieldReleasePointsExpr(node.then_expr, out);
          CollectYieldReleasePointsExpr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            for (const auto& stmt : node.block->stmts) {
              std::visit(
                  [&](const auto& stmt_node) {
                    using ST = std::decay_t<decltype(stmt_node)>;
                    if constexpr (std::is_same_v<ST, ast::ExprStmt>) {
                      CollectYieldReleasePointsExpr(stmt_node.value, out);
                    } else if constexpr (std::is_same_v<ST, ast::LetStmt>) {
                      CollectYieldReleasePointsExpr(stmt_node.binding.init, out);
                    } else if constexpr (std::is_same_v<ST, ast::VarStmt>) {
                      CollectYieldReleasePointsExpr(stmt_node.binding.init, out);
                    } else if constexpr (std::is_same_v<ST, ast::AssignStmt>) {
                      CollectYieldReleasePointsExpr(stmt_node.place, out);
                      CollectYieldReleasePointsExpr(stmt_node.value, out);
                    } else if constexpr (std::is_same_v<ST, ast::ReturnStmt> ||
                                         std::is_same_v<ST, ast::BreakStmt>) {
                      CollectYieldReleasePointsExpr(stmt_node.value_opt, out);
                    }
                  },
                  stmt);
            }
            CollectYieldReleasePointsExpr(node.block->tail_opt, out);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            for (const auto& stmt : node.block->stmts) {
              if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
                CollectYieldReleasePointsExpr(expr_stmt->value, out);
              }
            }
            CollectYieldReleasePointsExpr(node.block->tail_opt, out);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            for (const auto& stmt : node.body->stmts) {
              if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
                CollectYieldReleasePointsExpr(expr_stmt->value, out);
              }
            }
            CollectYieldReleasePointsExpr(node.body->tail_opt, out);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          CollectYieldReleasePointsExpr(node.cond, out);
          if (node.body) {
            for (const auto& stmt : node.body->stmts) {
              if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
                CollectYieldReleasePointsExpr(expr_stmt->value, out);
              }
            }
            CollectYieldReleasePointsExpr(node.body->tail_opt, out);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          CollectYieldReleasePointsExpr(node.iter, out);
          if (node.body) {
            for (const auto& stmt : node.body->stmts) {
              if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
                CollectYieldReleasePointsExpr(expr_stmt->value, out);
              }
            }
            CollectYieldReleasePointsExpr(node.body->tail_opt, out);
          }
        }
      },
      expr->node);
}

static std::vector<core::Span> CollectYieldReleasePoints(
    const ast::Block& body) {
  std::vector<core::Span> out;
  for (const auto& stmt : body.stmts) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            CollectYieldReleasePointsExpr(node.value, out);
          } else if constexpr (std::is_same_v<T, ast::LetStmt>) {
            CollectYieldReleasePointsExpr(node.binding.init, out);
          } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
            CollectYieldReleasePointsExpr(node.binding.init, out);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            CollectYieldReleasePointsExpr(node.place, out);
            CollectYieldReleasePointsExpr(node.value, out);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            CollectYieldReleasePointsExpr(node.value_opt, out);
          }
        },
        stmt);
  }
  CollectYieldReleasePointsExpr(body.tail_opt, out);
  return out;
}

static bool KeyPathHasDynamicIndex(const ScopeContext& ctx,
                                   const ast::KeyPathExpr& path) {
  for (const auto& seg : path.segs) {
    if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
      if (IsDynamicIndexExpr(ctx, idx->expr)) {
        return true;
      }
    }
  }
  return false;
}

struct OrderedFieldShape {
  bool marked = false;
  std::string name;

  bool operator==(const OrderedFieldShape& other) const {
    return marked == other.marked && IdEq(name, other.name);
  }
};

struct OrderedBaseShape {
  std::string root;
  std::vector<OrderedFieldShape> non_index_shape;

  bool operator==(const OrderedBaseShape& other) const {
    if (!IdEq(root, other.root) ||
        non_index_shape.size() != other.non_index_shape.size()) {
      return false;
    }
    for (std::size_t i = 0; i < non_index_shape.size(); ++i) {
      if (!(non_index_shape[i] == other.non_index_shape[i])) {
        return false;
      }
    }
    return true;
  }
};

static OrderedBaseShape OrderedBaseOf(const ast::KeyPathExpr& path) {
  OrderedBaseShape base;
  base.root = path.root;
  for (const auto& seg : path.segs) {
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      base.non_index_shape.push_back(
          OrderedFieldShape{field->marked, field->name});
    }
  }
  return base;
}

static bool OrderedComparablePaths(const std::vector<ast::KeyPathExpr>& paths) {
  if (paths.empty()) {
    return true;
  }

  const OrderedBaseShape first_base = OrderedBaseOf(paths.front());
  for (std::size_t i = 1; i < paths.size(); ++i) {
    if (!(OrderedBaseOf(paths[i]) == first_base)) {
      return false;
    }
  }
  return true;
}

static bool IsStaticallyComparableIndexExpr(const ScopeContext& ctx,
                                            const ast::ExprPtr& expr) {
  const auto const_len = ConstLen(ctx, expr);
  return const_len.ok && const_len.value.has_value();
}

static bool StaticallyComparableOrderedPaths(
    const ScopeContext& ctx,
    const std::vector<ast::KeyPathExpr>& paths) {
  if (!OrderedComparablePaths(paths) || paths.empty()) {
    return false;
  }

  const auto& first = paths.front();
  for (const auto& path : paths) {
    if (path.segs.size() != first.segs.size()) {
      return false;
    }

    for (std::size_t i = 0; i < first.segs.size(); ++i) {
      const auto& lhs = first.segs[i];
      const auto& rhs = path.segs[i];

      const auto* lhs_field = std::get_if<ast::KeySegField>(&lhs);
      const auto* rhs_field = std::get_if<ast::KeySegField>(&rhs);
      if (lhs_field || rhs_field) {
        if (!lhs_field || !rhs_field) {
          return false;
        }
        if (lhs_field->marked != rhs_field->marked ||
            !IdEq(lhs_field->name, rhs_field->name)) {
          return false;
        }
        continue;
      }

      const auto* lhs_index = std::get_if<ast::KeySegIndex>(&lhs);
      const auto* rhs_index = std::get_if<ast::KeySegIndex>(&rhs);
      if (!lhs_index || !rhs_index) {
        return false;
      }
      if (!IsStaticallyComparableIndexExpr(ctx, lhs_index->expr) ||
          !IsStaticallyComparableIndexExpr(ctx, rhs_index->expr)) {
        return false;
      }
    }
  }

  return true;
}

static bool ExtractRootAndIndices(const ast::ExprPtr& expr,
                                  std::string& root,
                                  std::vector<ast::ExprPtr>& indices) {
  if (!expr) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    root = ident->name;
    return true;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return ExtractRootAndIndices(field->base, root, indices);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    if (!ExtractRootAndIndices(index->base, root, indices)) {
      return false;
    }
    indices.push_back(index->index);
    return true;
  }
  return false;
}

static void CollectIndexAccessesOnRoot(const ast::ExprPtr& expr,
                                       std::string_view root,
                                       std::vector<ast::ExprPtr>& out) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          std::string base_root;
          std::vector<ast::ExprPtr> ignored;
          if (ExtractRootAndIndices(node.base, base_root, ignored) &&
              IdEq(base_root, std::string(root))) {
            out.push_back(node.index);
          }
          CollectIndexAccessesOnRoot(node.base, root, out);
          CollectIndexAccessesOnRoot(node.index, root, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CollectIndexAccessesOnRoot(node.base, root, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CollectIndexAccessesOnRoot(node.value, root, out);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CollectIndexAccessesOnRoot(node.lhs, root, out);
          CollectIndexAccessesOnRoot(node.rhs, root, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CollectIndexAccessesOnRoot(node.callee, root, out);
          for (const auto& arg : node.args) {
            CollectIndexAccessesOnRoot(arg.value, root, out);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CollectIndexAccessesOnRoot(node.receiver, root, out);
          for (const auto& arg : node.args) {
            CollectIndexAccessesOnRoot(arg.value, root, out);
          }
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          CollectIndexAccessesOnRoot(node.value, root, out);
        }
      },
      expr->node);
}

static void CollectWrittenPathsFromBlock(const ast::Block& block,
                                         std::vector<KeyPath>& out);

static std::optional<core::Span> FindImpureSpeculativeCallExpr(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr);

static std::optional<core::Span> FindImpureSpeculativeCallExpr(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }

  ContractContext contract_ctx;
  contract_ctx.scope_ctx = &ctx;
  if (std::holds_alternative<ast::CallExpr>(expr->node) ||
      std::holds_alternative<ast::MethodCallExpr>(expr->node)) {
    const auto purity = CheckPurity(contract_ctx, expr);
    if (!purity.ok) {
      return purity.span.has_value() ? purity.span : std::optional<core::Span>(expr->span);
    }
  }

  return std::visit(
      [&](const auto& node) -> std::optional<core::Span> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return FindImpureSpeculativeCallExpr(ctx, node.expr);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return FindImpureSpeculativeCallExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return FindImpureSpeculativeCallExpr(ctx, node.place);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr> ||
                             std::is_same_v<T, ast::PipelineExpr>) {
          if (const auto left = FindImpureSpeculativeCallExpr(ctx, node.lhs)) {
            return left;
          }
          return FindImpureSpeculativeCallExpr(ctx, node.rhs);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return FindImpureSpeculativeCallExpr(ctx, node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          if (const auto base = FindImpureSpeculativeCallExpr(ctx, node.base)) {
            return base;
          }
          return FindImpureSpeculativeCallExpr(ctx, node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (const auto callee = FindImpureSpeculativeCallExpr(ctx, node.callee)) {
            return callee;
          }
          for (const auto& arg : node.args) {
            if (const auto found = FindImpureSpeculativeCallExpr(ctx, arg.value)) {
              return found;
            }
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (const auto receiver = FindImpureSpeculativeCallExpr(ctx, node.receiver)) {
            return receiver;
          }
          for (const auto& arg : node.args) {
            if (const auto found = FindImpureSpeculativeCallExpr(ctx, arg.value)) {
              return found;
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (const auto found = FindImpureSpeculativeCallExpr(ctx, elem)) {
              return found;
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          std::optional<core::Span> found;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (!found.has_value()) {
              found = FindImpureSpeculativeCallExpr(ctx, elem);
            }
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          if (const auto value = FindImpureSpeculativeCallExpr(ctx, node.value)) {
            return value;
          }
          return FindImpureSpeculativeCallExpr(ctx, node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (const auto found = FindImpureSpeculativeCallExpr(ctx, field.value)) {
              return found;
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            return std::visit(
                [&](const auto& payload) -> std::optional<core::Span> {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      if (const auto found =
                              FindImpureSpeculativeCallExpr(ctx, elem)) {
                        return found;
                      }
                    }
                  } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                    for (const auto& field : payload.fields) {
                      if (const auto found =
                              FindImpureSpeculativeCallExpr(ctx, field.value)) {
                        return found;
                      }
                    }
                  }
                  return std::nullopt;
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          if (const auto cond = FindImpureSpeculativeCallExpr(ctx, node.cond)) {
            return cond;
          }
          if (const auto then_expr =
                  FindImpureSpeculativeCallExpr(ctx, node.then_expr)) {
            return then_expr;
          }
          return FindImpureSpeculativeCallExpr(ctx, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (const auto scrutinee =
                  FindImpureSpeculativeCallExpr(ctx, node.scrutinee)) {
            return scrutinee;
          }
          for (const auto& arm : node.cases) {
            if (const auto found = FindImpureSpeculativeCallExpr(ctx, arm.body)) {
              return found;
            }
          }
          return FindImpureSpeculativeCallExpr(ctx, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          if (const auto scrutinee =
                  FindImpureSpeculativeCallExpr(ctx, node.scrutinee)) {
            return scrutinee;
          }
          if (const auto then_expr =
                  FindImpureSpeculativeCallExpr(ctx, node.then_expr)) {
            return then_expr;
          }
          return FindImpureSpeculativeCallExpr(ctx, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            for (const auto& stmt : node.block->stmts) {
              if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
                if (const auto found =
                        FindImpureSpeculativeCallExpr(ctx, expr_stmt->value)) {
                  return found;
                }
              }
            }
            return FindImpureSpeculativeCallExpr(ctx, node.block->tail_opt);
          }
        }
        return std::nullopt;
      },
      expr->node);
}

static void CollectWrittenPathsFromExpr(const ast::ExprPtr& expr,
                                        std::vector<KeyPath>& out) {
  if (!expr) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          CollectWrittenPathsFromExpr(node.expr, out);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            CollectWrittenPathsFromBlock(*node.block, out);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            CollectWrittenPathsFromBlock(*node.block, out);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr> ||
                             std::is_same_v<T, ast::LoopConditionalExpr> ||
                             std::is_same_v<T, ast::LoopIterExpr>) {
          if (node.body) {
            CollectWrittenPathsFromBlock(*node.body, out);
          }
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CollectWrittenPathsFromExpr(node.then_expr, out);
          CollectWrittenPathsFromExpr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          for (const auto& arm : node.cases) {
            CollectWrittenPathsFromExpr(arm.body, out);
          }
          CollectWrittenPathsFromExpr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CollectWrittenPathsFromExpr(node.then_expr, out);
          CollectWrittenPathsFromExpr(node.else_expr, out);
        }
      },
      expr->node);
}

static void CollectWrittenPathsFromStmt(const ast::Stmt& stmt,
                                        std::vector<KeyPath>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                      std::is_same_v<T, ast::CompoundAssignStmt>) {
          const auto built = BuildKeyPath(node.place);
          if (built.success) {
            out.push_back(built.path);
          }
          CollectWrittenPathsFromExpr(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          CollectWrittenPathsFromExpr(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::LetStmt> ||
                             std::is_same_v<T, ast::VarStmt>) {
          CollectWrittenPathsFromExpr(node.binding.init, out);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          if (node.body) {
            CollectWrittenPathsFromBlock(*node.body, out);
          }
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          CollectWrittenPathsFromExpr(node.opts_opt, out);
          if (node.body) {
            CollectWrittenPathsFromBlock(*node.body, out);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            CollectWrittenPathsFromBlock(*node.body, out);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          CollectWrittenPathsFromExpr(node.value_opt, out);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          // Nested key blocks establish their own key mode and release semantics;
          // the enclosing block must not classify nested writes as writes under
          // the enclosing mode.
          return;
        }
      },
      stmt);
}

static void CollectWrittenPathsFromBlock(const ast::Block& block,
                                         std::vector<KeyPath>& out) {
  for (const auto& stmt : block.stmts) {
    CollectWrittenPathsFromStmt(stmt, out);
  }
  CollectWrittenPathsFromExpr(block.tail_opt, out);
}

static bool PathCoveredByExplicitKeys(const KeyPath& path,
                                      const std::vector<KeyPath>& explicit_keys) {
  for (const auto& key : explicit_keys) {
    if (IsPrefix(key, path)) {
      return true;
    }
  }
  return false;
}

static bool ExprUsesNameFromSet(const ast::ExprPtr& expr,
                                const std::unordered_set<IdKey>& names) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return names.find(cursive::analysis::IdKeyOf(node.name)) != names.end();
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprUsesNameFromSet(node.expr, names);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return ExprUsesNameFromSet(node.value, names);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr> ||
                             std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprUsesNameFromSet(node.place, names);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprUsesNameFromSet(node.lhs, names) ||
                 ExprUsesNameFromSet(node.rhs, names);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprUsesNameFromSet(node.base, names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprUsesNameFromSet(node.base, names) ||
                 ExprUsesNameFromSet(node.index, names);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprUsesNameFromSet(node.callee, names)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprUsesNameFromSet(arg.value, names)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprUsesNameFromSet(node.receiver, names)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprUsesNameFromSet(arg.value, names)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprUsesNameFromSet(elem, names)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            found = found || ExprUsesNameFromSet(elem, names);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprUsesNameFromSet(node.value, names) ||
                 ExprUsesNameFromSet(node.count, names);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprUsesNameFromSet(field.value, names)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprUsesNameFromSet(node.cond, names) ||
                 ExprUsesNameFromSet(node.then_expr, names) ||
                 ExprUsesNameFromSet(node.else_expr, names);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprUsesNameFromSet(node.scrutinee, names)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (ExprUsesNameFromSet(arm.body, names)) {
              return true;
            }
          }
          return ExprUsesNameFromSet(node.else_expr, names);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprUsesNameFromSet(node.scrutinee, names) ||
                 ExprUsesNameFromSet(node.then_expr, names) ||
                 ExprUsesNameFromSet(node.else_expr, names);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprUsesNameFromSet(node.lhs, names) ||
                 ExprUsesNameFromSet(node.rhs, names);
        }
        return false;
      },
      expr->node);
}

static bool BlockCreatesTask(const ast::Block& block) {
  auto expr_creates_task = [&](const ast::ExprPtr& expr, auto&& self) -> bool {
    if (!expr) {
      return false;
    }
    return std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::SpawnExpr> ||
                        std::is_same_v<T, ast::ParallelExpr> ||
                        std::is_same_v<T, ast::DispatchExpr>) {
            return true;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return self(node.expr, self);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            return self(node.value, self);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr> ||
                               std::is_same_v<T, ast::AddressOfExpr>) {
            return self(node.place, self);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            return self(node.lhs, self) || self(node.rhs, self);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            return self(node.base, self);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            return self(node.base, self) || self(node.index, self);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            if (self(node.callee, self)) {
              return true;
            }
            for (const auto& arg : node.args) {
              if (self(arg.value, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            if (self(node.receiver, self)) {
              return true;
            }
            for (const auto& arg : node.args) {
              if (self(arg.value, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              if (self(elem, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            bool found = false;
            ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
              found = found || self(elem, self);
            });
            return found;
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            return self(node.value, self) || self(node.count, self);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              if (self(field.value, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            return self(node.cond, self) || self(node.then_expr, self) ||
                   self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            if (self(node.scrutinee, self)) {
              return true;
            }
            for (const auto& arm : node.cases) {
              if (self(arm.body, self)) {
                return true;
              }
            }
            return self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            return self(node.scrutinee, self) || self(node.then_expr, self) ||
                   self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            return self(node.lhs, self) || self(node.rhs, self);
          } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
            return node.block && BlockCreatesTask(*node.block);
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
            return node.block && BlockCreatesTask(*node.block);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr> ||
                               std::is_same_v<T, ast::LoopConditionalExpr> ||
                               std::is_same_v<T, ast::LoopIterExpr>) {
            return node.body && BlockCreatesTask(*node.body);
          }
          return false;
        },
        expr->node);
  };

  for (const auto& stmt : block.stmts) {
    const bool found = std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            return expr_creates_task(node.value, expr_creates_task);
          } else if constexpr (std::is_same_v<T, ast::LetStmt> ||
                               std::is_same_v<T, ast::VarStmt>) {
            return expr_creates_task(node.binding.init, expr_creates_task);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            return expr_creates_task(node.place, expr_creates_task) ||
                   expr_creates_task(node.value, expr_creates_task);
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            return expr_creates_task(node.place, expr_creates_task) ||
                   expr_creates_task(node.value, expr_creates_task);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            return expr_creates_task(node.value_opt, expr_creates_task);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt> ||
                               std::is_same_v<T, ast::CtStmt>) {
            return node.body && BlockCreatesTask(*node.body);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            return expr_creates_task(node.opts_opt, expr_creates_task) ||
                   (node.body && BlockCreatesTask(*node.body));
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            return node.body && BlockCreatesTask(*node.body);
          }
          return false;
        },
        stmt);
    if (found) {
      return true;
    }
  }

  return expr_creates_task(block.tail_opt, expr_creates_task);
}

static bool BlockMayBeExpensiveToReexecute(const ast::Block& block) {
  auto expr_expensive = [&](const ast::ExprPtr& expr, auto&& self) -> bool {
    if (!expr) {
      return false;
    }
    return std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::CallExpr> ||
                        std::is_same_v<T, ast::MethodCallExpr> ||
                        std::is_same_v<T, ast::LoopInfiniteExpr> ||
                        std::is_same_v<T, ast::LoopConditionalExpr> ||
                        std::is_same_v<T, ast::LoopIterExpr> ||
                        std::is_same_v<T, ast::ParallelExpr> ||
                        std::is_same_v<T, ast::DispatchExpr> ||
                        std::is_same_v<T, ast::SpawnExpr> ||
                        std::is_same_v<T, ast::RaceExpr> ||
                        std::is_same_v<T, ast::AllExpr>) {
            return true;
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            return self(node.expr, self);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            return self(node.value, self);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr> ||
                               std::is_same_v<T, ast::AddressOfExpr>) {
            return self(node.place, self);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            return self(node.lhs, self) || self(node.rhs, self);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            return self(node.base, self);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            return self(node.base, self) || self(node.index, self);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              if (self(elem, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            bool found = false;
            ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
              found = found || self(elem, self);
            });
            return found;
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            return self(node.value, self) || self(node.count, self);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              if (self(field.value, self)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            return self(node.cond, self) || self(node.then_expr, self) ||
                   self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            if (self(node.scrutinee, self)) {
              return true;
            }
            for (const auto& arm : node.cases) {
              if (self(arm.body, self)) {
                return true;
              }
            }
            return self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            return self(node.scrutinee, self) || self(node.then_expr, self) ||
                   self(node.else_expr, self);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            return self(node.lhs, self) || self(node.rhs, self);
          } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
            return node.block && BlockMayBeExpensiveToReexecute(*node.block);
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
            return node.block && BlockMayBeExpensiveToReexecute(*node.block);
          }
          return false;
        },
        expr->node);
  };

  for (const auto& stmt : block.stmts) {
    const bool expensive = std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            return expr_expensive(node.value, expr_expensive);
          } else if constexpr (std::is_same_v<T, ast::LetStmt> ||
                               std::is_same_v<T, ast::VarStmt>) {
            return expr_expensive(node.binding.init, expr_expensive);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            return expr_expensive(node.place, expr_expensive) ||
                   expr_expensive(node.value, expr_expensive);
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            return expr_expensive(node.place, expr_expensive) ||
                   expr_expensive(node.value, expr_expensive);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            return expr_expensive(node.value_opt, expr_expensive);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt> ||
                               std::is_same_v<T, ast::CtStmt>) {
            return node.body && BlockMayBeExpensiveToReexecute(*node.body);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            return expr_expensive(node.opts_opt, expr_expensive) ||
                   (node.body && BlockMayBeExpensiveToReexecute(*node.body));
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            return node.body && BlockMayBeExpensiveToReexecute(*node.body);
          }
          return false;
        },
        stmt);
    if (expensive) {
      return true;
    }
  }

  return expr_expensive(block.tail_opt, expr_expensive);
}

static StaticSafetyClassification ClassifyStaticSafety(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const std::vector<ast::KeyPathExpr>& paths,
    const ast::Block& body,
    bool speculative_only,
    bool has_dynamic_key_path,
    bool provably_disjoint) {
  StaticSafetyClassification classification;

  if (!type_ctx.in_parallel) {
    classification.conditions.insert(StaticSafetyCondition::SequentialContext);
  }

  if (speculative_only) {
    classification.conditions.insert(StaticSafetyCondition::SpeculativeOnly);
  }

  const bool local_disjoint_proof_is_sufficient =
      provably_disjoint && (!type_ctx.in_parallel || !has_dynamic_key_path);
  if (local_disjoint_proof_is_sufficient) {
    classification.conditions.insert(StaticSafetyCondition::DisjointPaths);
  }

  if (!BlockCreatesTask(body)) {
    classification.conditions.insert(StaticSafetyCondition::NoEscape);
  }

  if (type_ctx.parallel_bindings) {
    for (const auto& path : paths) {
      for (const auto& seg : path.segs) {
        const auto* idx = std::get_if<ast::KeySegIndex>(&seg);
        if (idx && ExprUsesNameFromSet(idx->expr, *type_ctx.parallel_bindings)) {
          classification.conditions.insert(
              StaticSafetyCondition::DispatchIndexed);
        }
      }
    }
  }

  for (const auto& path : paths) {
    const auto binding = BindOf(env, path.root);
    if (!binding.has_value()) {
      continue;
    }
    const TypeRef type = binding->type;
    if (type && PermOfType(type) == Permission::Unique) {
      classification.conditions.insert(StaticSafetyCondition::UniqueOrigin);
      break;
    }
  }

  (void)ctx;
  return classification;
}

static bool BodyHasDynamicIndexConflict(const ScopeContext& ctx,
                                        const StmtTypeContext& type_ctx,
                                        const TypeEnv& env,
                                        const ast::Block& body,
                                        std::string_view root) {
  for (const auto& stmt : body.stmts) {
    const auto* assign = std::get_if<ast::AssignStmt>(&stmt);
    if (!assign) {
      continue;
    }
    std::string place_root;
    std::vector<ast::ExprPtr> lhs_indices;
    if (!ExtractRootAndIndices(assign->place, place_root, lhs_indices) ||
        !IdEq(place_root, std::string(root))) {
      continue;
    }
    for (const auto& idx : lhs_indices) {
      std::vector<ast::ExprPtr> rhs_indices;
      CollectIndexAccessesOnRoot(assign->value, root, rhs_indices);
      for (const auto& rhs_idx : rhs_indices) {
        const bool dynamic_pair =
            IsDynamicIndexExpr(ctx, idx) || IsDynamicIndexExpr(ctx, rhs_idx);
        if (!dynamic_pair) {
          continue;
        }
        if (ProvablyEquivalentIndexExpr(ctx, env, idx, rhs_idx)) {
          continue;
        }
        if (!ProvablyDisjointIndexExpr(ctx, type_ctx, env, idx, rhs_idx)) {
          return true;
        }
      }
    }
  }
  return false;
}

static bool IsGpuDomainType(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  if (!cur) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&cur->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&cur->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
}

static TypeRef StripKeyPathType(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static std::size_t CountKeyMarkers(const ast::KeyPathExpr& path) {
  std::size_t count = 0u;
  for (const auto& seg : path.segs) {
    std::visit(
        [&](const auto& node) {
          if (node.marked) {
            ++count;
          }
        },
        seg);
  }
  return count;
}

static TypeRef AdvanceKeyPathType(const ScopeContext& ctx,
                                  const TypeRef& current,
                                  const ast::KeySeg& seg) {
  if (!current) {
    return current;
  }

  const TypeRef base = StripKeyPathType(current);
  if (!base) {
    return current;
  }

  if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
    if (const auto* path_type = std::get_if<TypePathType>(&base->node)) {
      const auto* record = LookupRecordDecl(ctx, path_type->path);
      if (record) {
        if (const auto field_type = FieldType(*record, field->name, ctx);
            field_type.has_value()) {
          return *field_type;
        }
      }
    }
    return current;
  }

  const auto* index = std::get_if<ast::KeySegIndex>(&seg);
  if (!index) {
    return current;
  }

  if (const auto* arr = std::get_if<TypeArray>(&base->node)) {
    return arr->element;
  }
  if (const auto* slice = std::get_if<TypeSlice>(&base->node)) {
    return slice->element;
  }
  return current;
}

static std::optional<std::string_view> ValidateKeyPathConformance(
    const ScopeContext& ctx,
    const ast::KeyPathExpr& path,
    const TypeEnv& env) {
  const auto binding = BindOf(env, path.root);
  if (!binding.has_value()) {
    return "E-CON-0031";
  }

  if (PermOfType(binding->type) != Permission::Shared) {
    return "E-CON-0032";
  }

  if (CountKeyMarkers(path) > 1) {
    return "E-CON-0003";
  }

  TypeRef current_type = binding->type;
  for (const auto& seg : path.segs) {
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      const TypeRef base = StripKeyPathType(current_type);
      bool is_record_base = false;
      if (base) {
        if (const auto* path_type = std::get_if<TypePathType>(&base->node)) {
          is_record_base = LookupRecordDecl(ctx, path_type->path) != nullptr;
        }
      }

      if (field->marked && !is_record_base) {
        return "E-CON-0033";
      }
    }
    current_type = AdvanceKeyPathType(ctx, current_type, seg);
  }

  return std::nullopt;
}

static bool PathMarkerMatchesTypeBoundary(const ScopeContext& ctx,
                                          const ast::KeyPathExpr& path,
                                          const TypeEnv& env) {
  const auto binding = BindOf(env, path.root);
  if (!binding.has_value()) {
    return false;
  }

  TypeRef current_type = binding->type;
  for (const auto& seg : path.segs) {
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      const TypeRef base = StripKeyPathType(current_type);
      if (base) {
        if (const auto* path_type = std::get_if<TypePathType>(&base->node)) {
          if (const auto* record = LookupRecordDecl(ctx, path_type->path)) {
            for (const auto& member : record->members) {
              const auto* field_decl = std::get_if<ast::FieldDecl>(&member);
              if (!field_decl || !IdEq(field_decl->name, field->name)) {
                continue;
              }
              if (field->marked && field_decl->key_boundary) {
                return true;
              }
              break;
            }
          }
        }
      }
    }
    current_type = AdvanceKeyPathType(ctx, current_type, seg);
  }
  return false;
}

static bool KeyModeSufficient(ast::KeyMode held, ast::KeyMode required) {
  return held == ast::KeyMode::Write || held == required;
}

static bool HasLoopFineGrainedKeyCandidate(const ast::KeyPathExpr& path) {
  if (path.segs.size() < 2) {
    return false;
  }
  for (const auto& seg : path.segs) {
    bool marked = false;
    std::visit([&](const auto& node) { marked = node.marked; }, seg);
    if (marked) {
      return false;
    }
  }
  return true;
}

static void MarkSharedDerivedBindingsStale(TypeEnv& env) {
  for (auto& scope : env.scopes) {
    for (auto& [_, binding] : scope) {
      if (binding.derived_from_shared) {
        binding.stale_after_release = true;
      }
    }
  }
}

}  // namespace

StmtTypeResult TypeKeyBlockStmt(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::KeyBlockStmt& node,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place) {
  SpecDefsKeyBlockStmt();
  (void)type_expr;
  (void)type_ident;
  (void)type_place;

  if (!node.body) {
    return {false, std::nullopt, {}, {}};
  }

  if (type_ctx.in_speculative) {
    return {false, "E-CON-0090", {}, {}};
  }

  bool has_memory_order_attr = false;
  std::size_t memory_order_attr_count = 0;
  if (!node.attrs.empty()) {
    ast::AttributeList statement_attrs;
    ast::AttributeList key_block_attrs;
    for (const auto& attr : node.attrs) {
      if (IsMemoryOrderAttributeName(attr.name)) {
        key_block_attrs.push_back(attr);
        has_memory_order_attr = true;
        ++memory_order_attr_count;
      } else {
        statement_attrs.push_back(attr);
      }
    }

    if (!statement_attrs.empty()) {
      const auto statement_attr_validation =
          ValidateAttributes(statement_attrs, AttributeTarget::Statement);
      if (!statement_attr_validation.ok) {
        return {false, statement_attr_validation.diag_id, {}, {},
                statement_attr_validation.message};
      }
    }

    if (!key_block_attrs.empty()) {
      const auto key_block_attr_validation =
          ValidateAttributes(key_block_attrs, AttributeTarget::KeyBlock);
      if (!key_block_attr_validation.ok) {
        return {false, key_block_attr_validation.diag_id, {}, {},
                key_block_attr_validation.message};
      }
      if (memory_order_attr_count > 1) {
        return {false, "E-MOD-2450", {}, {}};
      }
    }
  }

  if (GpuContext(env)) {
    SPEC_RULE("KeyBlock-GPU-Err");
    return {false, "E-CON-0155", {}, {}};
  }

  for (const auto& path : node.paths) {
    if (const auto diag_id = ValidateKeyPathConformance(ctx, path, env);
        diag_id.has_value()) {
      return {false, *diag_id, {}, {}};
    }
    if (type_ctx.diags && PathMarkerMatchesTypeBoundary(ctx, path, env)) {
      if (auto diag = core::MakeDiagnosticById("W-CON-0003", path.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
  }

  const bool has_speculative_mod =
      std::find(node.mods.begin(), node.mods.end(),
                ast::KeyBlockMod::Speculative) != node.mods.end();
  const bool has_ordered_mod = std::find(node.mods.begin(), node.mods.end(),
                                         ast::KeyBlockMod::Ordered) !=
                               node.mods.end();
  const bool has_release_mod = std::find(node.mods.begin(), node.mods.end(),
                                         ast::KeyBlockMod::Release) !=
                               node.mods.end();
  const ast::KeyMode inner_mode = node.mode.value_or(ast::KeyMode::Read);
  const auto current_key_infos = CanonicalHeldKeyInfos(node.paths, inner_mode);

  if (has_memory_order_attr && (type_ctx.in_speculative || has_speculative_mod)) {
    return {false, "E-CON-0096", {}, {}};
  }

  if (has_speculative_mod && has_release_mod) {
    return {false, "E-CON-0094", {}, {}};
  }
  if (has_speculative_mod &&
      (!node.mode.has_value() || *node.mode != ast::KeyMode::Write)) {
    return {false, "E-CON-0095", {}, {}};
  }
  if (has_ordered_mod && !OrderedComparablePaths(node.paths)) {
    SPEC_RULE("K-Ordered-Base-Err");
    return {false, "E-CON-0014", {}, {}};
  }
  if (has_ordered_mod) {
    SPEC_RULE("K-Ordered-Ok");
    if (type_ctx.diags && StaticallyComparableOrderedPaths(ctx, node.paths)) {
      SPEC_RULE("K-Ordered-Redundant-Warn");
      if (auto diag = core::MakeDiagnosticById("W-CON-0013", node.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
  }

  if (type_ctx.loop_flag == LoopFlag::Loop && type_ctx.diags &&
      !has_release_mod && !has_speculative_mod && !has_ordered_mod) {
    for (const auto& path : node.paths) {
      if (!HasLoopFineGrainedKeyCandidate(path)) {
        continue;
      }
      if (auto diag = core::MakeDiagnosticById("W-CON-0001", path.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
      break;
    }
  }

  if (has_speculative_mod && type_ctx.diags) {
    constexpr std::uint64_t kSpecLargeStructThreshold = 128;
    bool warned_large = false;
    for (const auto& path : node.paths) {
      const auto binding = BindOf(env, path.root);
      if (!binding.has_value() || !binding->type) {
        continue;
      }
      const auto size = SizeOf(ctx, binding->type);
      if (!size.has_value() || *size <= kSpecLargeStructThreshold) {
        continue;
      }
      if (auto diag = core::MakeDiagnosticById("W-CON-0020", path.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
      warned_large = true;
      break;
    }
    if (!warned_large && BlockMayBeExpensiveToReexecute(*node.body)) {
      if (auto diag = core::MakeDiagnosticById("W-CON-0021", node.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
  }

  bool emitted_release_interleaving_warning = false;
  for (const auto& current_key : current_key_infos) {
    for (const auto& held_key : type_ctx.held_key_paths) {
      if (!KeyPathEqual(current_key.path, held_key.path)) {
        continue;
      }

      SPEC_RULE("K-Nested-Same-Path");
      if (held_key.mode == current_key.mode) {
        if (type_ctx.diags) {
          if (auto diag = core::MakeDiagnosticById("W-CON-0002", node.span)) {
            core::Emit(*type_ctx.diags, *diag);
          }
        }
        if (has_release_mod) {
          SPEC_RULE("K-Release-SameMode-Err");
          return {false, "E-CON-0018", {}, {}};
        }
        continue;
      }

      if (KeyModeSufficient(held_key.mode, current_key.mode) && type_ctx.diags) {
        if (auto diag = core::MakeDiagnosticById("W-CON-0002", node.span)) {
          core::Emit(*type_ctx.diags, *diag);
        }
      }

      if (!has_release_mod) {
        return {false, "E-CON-0012", {}, {}};
      }

      SPEC_RULE("K-Reentrant");
      if (!emitted_release_interleaving_warning && type_ctx.diags) {
        if (auto diag = core::MakeDiagnosticById("W-CON-0010", node.span)) {
          core::Emit(*type_ctx.diags, *diag);
        }
        emitted_release_interleaving_warning = true;
      }
    }
  }

  if (has_release_mod && !emitted_release_interleaving_warning && type_ctx.diags) {
    for (const auto& current_key : current_key_infos) {
      const auto it = std::find_if(
          type_ctx.held_key_paths.begin(), type_ctx.held_key_paths.end(),
          [&](const HeldKeyTypingInfo& held_key) {
            return KeyPathEqual(current_key.path, held_key.path);
          });
      if (it == type_ctx.held_key_paths.end()) {
        continue;
      }
      if (auto diag = core::MakeDiagnosticById("W-CON-0010", node.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
      emitted_release_interleaving_warning = true;
      break;
    }
  }

  std::unordered_set<std::string> keyed_roots;
  bool has_dynamic_key_path = false;
  std::vector<KeyPath> explicit_key_paths;
  explicit_key_paths.reserve(node.paths.size());
  for (const auto& path : node.paths) {
    keyed_roots.insert(path.root);
    explicit_key_paths.push_back(ParseKeyPathSpec(path));
    if (KeyPathHasDynamicIndex(ctx, path)) {
      has_dynamic_key_path = true;
    }
  }

  std::vector<KeyPath> written_paths;
  CollectWrittenPathsFromBlock(*node.body, written_paths);
  const bool provably_disjoint_indices =
      !has_dynamic_key_path ||
      std::all_of(keyed_roots.begin(), keyed_roots.end(),
                  [&](const std::string& root) {
                    return !BodyHasDynamicIndexConflict(ctx, type_ctx, env,
                                                        *node.body, root);
                  });
  const auto safety = ClassifyStaticSafety(ctx, type_ctx, env, node.paths,
                                           *node.body, has_speculative_mod,
                                           has_dynamic_key_path,
                                           provably_disjoint_indices);

  if (has_dynamic_key_path) {
    if (!provably_disjoint_indices) {
      SPEC_RULE("K-Dynamic-Index-Conflict");
      return {false, "E-CON-0010", {}, {}};
    }
    if (!safety.IsStaticallySafe() && !type_ctx.contract_dynamic) {
      SPEC_RULE("K-Static-Required");
      return {false, "E-CON-0020", {}, {}};
    }
    if (type_ctx.contract_dynamic && type_ctx.diags) {
      const char* diag_id = safety.IsStaticallySafe() ? "I-CON-0013"
                                                      : "I-CON-0011";
      if (auto diag = core::MakeDiagnosticById(diag_id, node.span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
  }

  const bool writes_explicit_key_path =
      std::any_of(written_paths.begin(), written_paths.end(),
                  [&](const KeyPath& path) {
                    return PathCoveredByExplicitKeys(path, explicit_key_paths);
                  });
  if (writes_explicit_key_path &&
      (!node.mode.has_value() || *node.mode != ast::KeyMode::Write)) {
      SPEC_RULE("K-Read-Block-No-Write");
      return {false, "E-CON-0070", {}, {}};
  }

  if (has_speculative_mod) {
    for (const auto& path : written_paths) {
      if (!PathCoveredByExplicitKeys(path, explicit_key_paths)) {
        return {false, "E-CON-0091", {}, {}};
      }
    }
  }

  // Create a context with keys_held = true for the body.
  // The inner expression typing closures must be rebuilt with this context;
  // otherwise nested expression checks (notably yield) observe the outer
  // context and miss key-held constraints.
  TypeEnv key_env = env;
  StmtTypeContext key_ctx = type_ctx;
  key_ctx.keys_held = true;
  key_ctx.key_mode = inner_mode;
  key_ctx.held_key_paths = type_ctx.held_key_paths;
  key_ctx.held_key_paths.insert(key_ctx.held_key_paths.end(),
                                current_key_infos.begin(),
                                current_key_infos.end());
  key_ctx.in_speculative = has_speculative_mod;
  key_ctx.env_ref = &key_env;

  auto current_env = [&]() -> const TypeEnv& { return key_env; };
  ExprTypeFn key_type_expr = [&](const ast::ExprPtr& inner) -> ExprTypeResult {
    return TypeExpr(ctx, key_ctx, inner, current_env());
  };
  IdentTypeFn key_type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)},
                              current_env());
  };
  PlaceTypeFn key_type_place = [&](const ast::ExprPtr& inner) -> PlaceTypeResult {
    return TypePlace(ctx, key_ctx, inner, current_env());
  };

  // Type the key block body
  const auto info = TypeBlockInfo(ctx, key_ctx, *node.body, key_env,
                                  key_type_expr, key_type_ident,
                                  key_type_place, &key_env);
  if (!info.ok) {
    return {false, info.diag_id, {}, {}, info.diag_detail, info.diag_span};
  }

  if (has_speculative_mod) {
    for (const auto& stmt : node.body->stmts) {
      if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
        if (const auto impure_span =
                FindImpureSpeculativeCallExpr(ctx, expr_stmt->value)) {
          return {false, "E-CON-0097", {}, {}, {}, impure_span};
        }
      }
    }
    if (const auto impure_span =
            FindImpureSpeculativeCallExpr(ctx, node.body->tail_opt)) {
      return {false, "E-CON-0097", {}, {}, {}, impure_span};
    }
  }

  if (type_ctx.diags) {
    const auto yield_release_points = CollectYieldReleasePoints(*node.body);
    if (!yield_release_points.empty()) {
      const auto stale_warnings = CheckStaleness(*node.body, yield_release_points);
      for (const auto& warning : stale_warnings) {
        if (warning.suppressed) {
          continue;
        }
        if (auto diag = core::MakeDiagnosticById("W-CON-0011", warning.yield_span)) {
          core::Emit(*type_ctx.diags, *diag);
        }
      }
    }
  }

  TypeEnv out_env = env;
  if (has_release_mod) {
    MarkSharedDerivedBindingsStale(out_env);
  }

  FlowInfo flow;
  if (IsPrimType(info.type, "!")) {
    flow.results.push_back(info.type);
  }
  flow.breaks = info.breaks;
  flow.break_void = info.break_void;

  SPEC_RULE("T-KeyBlockStmt");
  return {true, std::nullopt, std::move(out_env), std::move(flow)};
}

}  // namespace cursive::analysis
