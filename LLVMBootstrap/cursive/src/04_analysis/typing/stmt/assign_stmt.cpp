// =============================================================================
// assign_stmt.cpp - Assignment statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Statement Typing - Assignments (lines 9449-9488)
//   - PlaceRoot definition (lines 9451-9455)
//   - T-Assign (lines 9457-9460): Basic assignment
//   - Assign-NotPlace (lines 9467-9470): Not a place error
//   - Assign-Immutable-Err (lines 9472-9475): Immutable binding error
//   - Assign-Type-Err (lines 9477-9480): Type mismatch error
//   - Assign-Const-Err (lines 9484-9487): Const-qualified error
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <cstdio>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/composite/function_types.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/resolve/scopes.h"
#include "00_core/process_config.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/if_case_check.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

ExprTypeResult TypeExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env);
PlaceTypeResult TypePlace(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::ExprPtr& expr,
                          const TypeEnv& env);

namespace {

static inline void SpecDefsAssignStmt() {
  SPEC_DEF("T-Assign", "5.2.11");
  SPEC_DEF("Assign-NotPlace", "5.2.11");
  SPEC_DEF("Assign-Immutable-Err", "5.2.11");
  SPEC_DEF("Assign-Type-Err", "5.2.11");
  SPEC_DEF("Assign-Const-Err", "5.2.11");
  SPEC_DEF("PlaceRoot", "5.2.11");
}

static std::optional<std::string_view> PlaceRootName(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return PlaceRootName(attributed->expr);
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceRootName(field->base);
  }
  if (const auto* tup = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceRootName(tup->base);
  }
  if (const auto* idx = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceRootName(idx->base);
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return PlaceRootName(deref->value);
  }
  return std::nullopt;
}

static void UpdateAssignedBindingProvenance(TypeEnv& env,
                                            std::string_view name,
                                            const ProvExprTrackResult& prov) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->find(key);
    if (found == it->end()) {
      continue;
    }
    ApplyBindingProvenanceSeed(found->second, prov.kind, prov.region);
    return;
  }
}

static void UpdateAssignedBindingSharedState(TypeEnv& env,
                                             std::string_view name,
                                             bool derived_from_shared) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->find(key);
    if (found == it->end()) {
      continue;
    }
    found->second.derived_from_shared = derived_from_shared;
    found->second.stale_after_release = false;
    return;
  }
}

static bool IsRootIdentifierPlace(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return IsRootIdentifierPlace(attributed->expr);
  }
  return std::holds_alternative<ast::IdentifierExpr>(expr->node);
}

static bool PlaceWritesThroughDeref(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(attributed->expr);
  }
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return true;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(field->base);
  }
  if (const auto* tup = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(tup->base);
  }
  if (const auto* idx = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(idx->base);
  }
  return false;
}

static bool IsPlaceExprNode(const ast::ExprNode& node) {
  if (std::holds_alternative<ast::IdentifierExpr>(node)) {
    return true;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&node)) {
    return attributed->expr && IsPlaceExprNode(attributed->expr->node);
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&node)) {
    return field->base && IsPlaceExprNode(field->base->node);
  }
  if (const auto* tup = std::get_if<ast::TupleAccessExpr>(&node)) {
    return tup->base && IsPlaceExprNode(tup->base->node);
  }
  if (const auto* idx = std::get_if<ast::IndexAccessExpr>(&node)) {
    return idx->base && IsPlaceExprNode(idx->base->node);
  }
  if (std::holds_alternative<ast::DerefExpr>(node)) {
    return true;
  }
  return false;
}

static bool IsPlaceExprLocal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return IsPlaceExprNode(expr->node);
}

static std::optional<KeyPath> TryBuildKeyPath(const ast::ExprPtr& expr) {
  const auto result = BuildKeyPath(expr);
  if (!result.success) {
    return std::nullopt;
  }
  return result.path;
}

static bool HasCoveringWriteKey(const StmtTypeContext& type_ctx,
                                const KeyPath& path) {
  for (const auto& held : type_ctx.held_key_paths) {
    if (held.mode == ast::KeyMode::Write && IsPrefix(held.path, path)) {
      return true;
    }
  }
  return false;
}

static std::optional<ast::KeyMode> CoveringKeyMode(const StmtTypeContext& type_ctx,
                                                   const KeyPath& path) {
  std::optional<ast::KeyMode> best;
  for (const auto& held : type_ctx.held_key_paths) {
    if (!IsPrefix(held.path, path)) {
      continue;
    }
    if (!best.has_value() || held.mode == ast::KeyMode::Write) {
      best = held.mode;
    }
  }
  return best;
}

static bool IsCompoundRewriteOp(std::string_view op) {
  return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

static const ast::ExprPtr* StripAttributedExprRef(const ast::ExprPtr& expr) {
  const ast::ExprPtr* current = &expr;
  while (*current) {
    const auto* attributed = std::get_if<ast::AttributedExpr>(&(*current)->node);
    if (!attributed) {
      break;
    }
    current = &attributed->expr;
  }
  return current;
}

static bool ExprReadsExactPath(const ast::ExprPtr& expr, const KeyPath& path);

static bool BlockReadsExactPath(const ast::Block& block, const KeyPath& path);

static bool StmtReadsExactPath(const ast::Stmt& stmt, const KeyPath& path) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return ExprReadsExactPath(node.binding.init, path);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          return ExprReadsExactPath(node.place, path) ||
                 ExprReadsExactPath(node.value, path);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ExprReadsExactPath(node.place, path) ||
                 ExprReadsExactPath(node.value, path);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprReadsExactPath(node.value, path);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprReadsExactPath(node.opts_opt, path) ||
                 (node.body && BlockReadsExactPath(*node.body, path));
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprReadsExactPath(node.value_opt, path);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body && BlockReadsExactPath(*node.body, path)) {
            return true;
          }
          for (const auto& key_path : node.paths) {
            for (const auto& seg : key_path.segs) {
              if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
                if (ExprReadsExactPath(index->expr, path)) {
                  return true;
                }
              }
            }
          }
          return false;
        } else {
          return false;
        }
      },
      stmt);
}

static bool BlockReadsExactPath(const ast::Block& block, const KeyPath& path) {
  for (const auto& stmt : block.stmts) {
    if (StmtReadsExactPath(stmt, path)) {
      return true;
    }
  }
  return ExprReadsExactPath(block.tail_opt, path);
}

static bool ExprReadsExactPath(const ast::ExprPtr& expr, const KeyPath& path) {
  if (!expr) {
    return false;
  }

  if (const auto expr_path = TryBuildKeyPath(expr);
      expr_path.has_value() && KeyPathEquals(*expr_path, path)) {
    return true;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& args = std::get<ast::ParenArgs>(node.args).args;
            for (const auto& arg : args) {
              if (ExprReadsExactPath(arg.value, path)) {
                return true;
              }
            }
            return false;
          }
          const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
          for (const auto& field : fields) {
            if (ExprReadsExactPath(field.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprReadsExactPath(node.lhs, path) ||
                 ExprReadsExactPath(node.rhs, path);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprReadsExactPath(node.lhs, path) ||
                 ExprReadsExactPath(node.rhs, path);
        } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return ExprReadsExactPath(node.value, path);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return ExprReadsExactPath(node.place, path);
      } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
        for (const auto& elem : node.elements) {
          if (ExprReadsExactPath(elem, path)) {
            return true;
          }
        }
        return false;
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        bool reads_exact_path = false;
        ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
          if (reads_exact_path) {
            return;
          }
          if (ExprReadsExactPath(elem, path)) {
            reads_exact_path = true;
          }
        });
        return reads_exact_path;
      } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
        return ExprReadsExactPath(node.value, path) ||
               ExprReadsExactPath(node.count, path);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprReadsExactPath(field.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
            const auto& payload = std::get<ast::EnumPayloadParen>(*node.payload_opt);
            for (const auto& elem : payload.elements) {
              if (ExprReadsExactPath(elem, path)) {
                return true;
              }
            }
            return false;
          }
          const auto& payload = std::get<ast::EnumPayloadBrace>(*node.payload_opt);
          for (const auto& field : payload.fields) {
            if (ExprReadsExactPath(field.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprReadsExactPath(node.cond, path) ||
                 ExprReadsExactPath(node.then_expr, path) ||
                 ExprReadsExactPath(node.else_expr, path);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprReadsExactPath(node.scrutinee, path) ||
              ExprReadsExactPath(node.else_expr, path)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ExprReadsExactPath(case_clause.body, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprReadsExactPath(node.scrutinee, path) ||
                 ExprReadsExactPath(node.then_expr, path) ||
                 ExprReadsExactPath(node.else_expr, path);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return (node.invariant_opt.has_value() &&
                  ExprReadsExactPath(node.invariant_opt->predicate, path)) ||
                 (node.body && BlockReadsExactPath(*node.body, path));
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          if (ExprReadsExactPath(node.cond, path)) {
            return true;
          }
          if (node.invariant_opt.has_value() &&
              ExprReadsExactPath(node.invariant_opt->predicate, path)) {
            return true;
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          if (ExprReadsExactPath(node.iter, path)) {
            return true;
          }
          if (node.invariant_opt.has_value() &&
              ExprReadsExactPath(node.invariant_opt->predicate, path)) {
            return true;
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block && BlockReadsExactPath(*node.block, path);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return ExprReadsExactPath(node.body, path);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          if (ExprReadsExactPath(node.cond, path)) {
            return true;
          }
          if (node.then_block && BlockReadsExactPath(*node.then_block, path)) {
            return true;
          }
          return node.else_block_opt &&
                 BlockReadsExactPath(*node.else_block_opt, path);
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          if (ExprReadsExactPath(node.iter, path)) {
            return true;
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprReadsExactPath(node.expr, path);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return ExprReadsExactPath(node.body, path);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ExprReadsExactPath(node.lhs, path) ||
                 ExprReadsExactPath(node.rhs, path);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprReadsExactPath(node.base, path);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprReadsExactPath(node.base, path);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprReadsExactPath(node.base, path) ||
                 ExprReadsExactPath(node.index, path);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprReadsExactPath(node.callee, path)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprReadsExactPath(arg.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          if (ExprReadsExactPath(node.callee, path)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprReadsExactPath(arg.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprReadsExactPath(node.receiver, path)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprReadsExactPath(arg.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            if (ExprReadsExactPath(arm.expr, path) ||
                ExprReadsExactPath(arm.handler.value, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& sub : node.exprs) {
            if (ExprReadsExactPath(sub, path)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprReadsExactPath(node.expr, path);
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (ExprReadsExactPath(node.domain, path)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprReadsExactPath(opt.value, path)) {
              return true;
            }
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            if (ExprReadsExactPath(opt.value, path)) {
              return true;
            }
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return ExprReadsExactPath(node.handle, path);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (ExprReadsExactPath(node.range, path)) {
            return true;
          }
          if (node.key_clause.has_value()) {
            for (const auto& seg : node.key_clause->key_path.segs) {
              if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
                if (ExprReadsExactPath(index->expr, path)) {
                  return true;
                }
              }
            }
          }
          for (const auto& opt : node.opts) {
            if (ExprReadsExactPath(opt.chunk_expr, path)) {
              return true;
            }
            if (ExprReadsExactPath(opt.workgroup_expr, path)) {
              return true;
            }
          }
          return node.body && BlockReadsExactPath(*node.body, path);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool IsCompoundRewriteCandidate(const ast::AssignStmt& node,
                                       const KeyPath& path) {
  const ast::ExprPtr* stripped_value = StripAttributedExprRef(node.value);
  if (!stripped_value || !*stripped_value) {
    return false;
  }
  const auto* binary = std::get_if<ast::BinaryExpr>(&(*stripped_value)->node);
  if (!binary || !IsCompoundRewriteOp(binary->op)) {
    return false;
  }
  const auto lhs_path = TryBuildKeyPath(binary->lhs);
  return lhs_path.has_value() && KeyPathEquals(*lhs_path, path);
}

struct RootMutabilityResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<ast::Mutability> mut;
};

static RootMutabilityResult LookupRootMutability(const ScopeContext& ctx,
                                                 const TypeEnv& env,
                                                 std::string_view name) {
  if (const auto local_mut = MutOf(env, name)) {
    return {true, std::nullopt, local_mut};
  }

  const auto static_lookup = LookupModuleStatic(ctx, ctx.current_module, name);
  if (!static_lookup.ok) {
    return {false, static_lookup.diag_id, std::nullopt};
  }
  if (static_lookup.type) {
    return {true, std::nullopt,
            static_lookup.is_mutable
                ? std::optional<ast::Mutability>{ast::Mutability::Var}
                : std::optional<ast::Mutability>{ast::Mutability::Let}};
  }

  return {true, std::nullopt, std::nullopt};
}

static std::string ExprKindName(const ast::ExprPtr& expr) {
  if (!expr) {
    return "null";
  }
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return "IdentifierExpr(" + node.name + ")";
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return "DerefExpr";
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return "FieldAccessExpr";
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return "TupleAccessExpr";
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return "IndexAccessExpr";
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return "AttributedExpr";
        } else {
          return "Expr";
        }
      },
      expr->node);
}

static ExprTypeResult TypeExprWithCurrentEnv(const ScopeContext& ctx,
                                             const StmtTypeContext& type_ctx,
                                             const TypeEnv& env,
                                             const ExprTypeFn& type_expr,
                                             const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  const auto via_env = TypeExpr(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_expr(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static PlaceTypeResult TypePlaceWithCurrentEnv(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const TypeEnv& env,
                                               const PlaceTypeFn& type_place,
                                               const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value()) {
      return {true, std::nullopt, binding->type};
    }
  }
  const auto via_env = TypePlace(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_place(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static TypeRef StablePlaceTypeForAssign(const ast::ExprPtr& place,
                                        const TypeEnv& env,
                                        const TypeRef& fallback) {
  if (!place) {
    return fallback;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&place->node)) {
    return StablePlaceTypeForAssign(attributed->expr, env, fallback);
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&place->node)) {
    if (const auto binding = BindOf(env, ident->name)) {
      return StableBindingType(*binding);
    }
  }
  return fallback;
}

static IdentTypeFn IdentTypeWithCurrentEnv(const ScopeContext& ctx,
                                           const TypeEnv& env,
                                           const IdentTypeFn& type_ident) {
  (void)ctx;
  return [&](std::string_view name) -> ExprTypeResult {
    const auto binding = BindOf(env, name);
    if (binding.has_value()) {
      ExprTypeResult local;
      local.ok = true;
      local.type = binding->type;
      return local;
    }
    return type_ident(name);
  };
}

}  // namespace

StmtTypeResult TypeAssignStmt(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::AssignStmt& node,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place) {
  SpecDefsAssignStmt();
  const StmtTypeContext read_ctx =
      WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
  const StmtTypeContext write_ctx =
      WithSharedAccessMode(type_ctx, ast::KeyMode::Write);

  // Check that the target is a place expression (lvalue)
  if (!IsPlaceExprLocal(node.place)) {
    SPEC_RULE("Assign-NotPlace");
    return {false, "E-SEM-3131", {}, {}};
  }

  // Type the place expression
  auto type_place_current = [&](const ast::ExprPtr& inner) {
    return TypePlaceWithCurrentEnv(ctx, write_ctx, env, type_place, inner);
  };
  const auto place_type = type_place_current(node.place);
  if (!place_type.ok) {
    std::string detail = place_type.diag_detail;
    if (detail.empty()) {
      detail = "assign-place-kind=" + ExprKindName(node.place);
    } else {
      detail += "; assign-place-kind=" + ExprKindName(node.place);
    }
    if (node.place) {
      if (const auto* deref = std::get_if<ast::DerefExpr>(&node.place->node)) {
        if (deref->value) {
          if (const auto* ident =
                  std::get_if<ast::IdentifierExpr>(&deref->value->node)) {
            const auto binding = BindOf(env, ident->name);
            detail += "; deref-ident=" + ident->name;
            detail += "; deref-ident-binding=";
            detail += binding.has_value() ? "present" : "absent";
          }
        }
      }
    }
    return {false, place_type.diag_id, {}, {}, detail};
  }

  // Check for const permission
  if (const auto* perm = std::get_if<TypePerm>(&place_type.type->node)) {
    if (perm->perm == Permission::Const) {
      SPEC_RULE("Assign-Const-Err");
      return {false, "E-SEM-3132", {}, {}};
    }
  }

  const auto place_key_path = TryBuildKeyPath(node.place);
  const bool read_then_write_same_path =
      place_key_path.has_value() &&
      ExprReadsExactPath(node.value, *place_key_path);

  bool shared_write_with_key = false;
  if (const auto* perm = std::get_if<TypePerm>(&place_type.type->node)) {
    if (perm->perm == Permission::Shared) {
      const bool has_write_key =
          place_key_path.has_value()
              ? HasCoveringWriteKey(type_ctx, *place_key_path)
              : (type_ctx.keys_held &&
                 type_ctx.key_mode.has_value() &&
                 *type_ctx.key_mode == ast::KeyMode::Write);
      if (!has_write_key) {
        const auto covering_mode =
            place_key_path.has_value()
                ? CoveringKeyMode(type_ctx, *place_key_path)
                : type_ctx.key_mode;
        if (read_then_write_same_path) {
          SPEC_RULE("K-Read-Write-Reject");
          return {false, "E-CON-0060", {}, {}};
        }
        if (covering_mode.has_value()) {
          return {false, "E-CON-0005", {}, {}};
        }
      }
      shared_write_with_key = true;
    }
  }

  // Find the root of the place and check mutability
  const bool writes_through_deref = PlaceWritesThroughDeref(node.place);
  const auto root = PlaceRootName(node.place);
  if (!writes_through_deref && root.has_value()) {
    const auto root_mut = LookupRootMutability(ctx, env, *root);
    if (!root_mut.ok) {
      return {false, root_mut.diag_id, {}, {}};
    }
    if (!shared_write_with_key &&
        root_mut.mut.has_value() &&
        *root_mut.mut == ast::Mutability::Let) {
      SPEC_RULE("Assign-Immutable-Err");
      return {false, "E-MOD-2401", {}, {}};
    }
  }

  // Assignment checks compare against the stored value type, not the access
  // permission wrapper.
  TypeRef assign_target_type =
      StablePlaceTypeForAssign(node.place, env, place_type.type);
  if (const auto* perm = std::get_if<TypePerm>(&place_type.type->node)) {
    assign_target_type = perm->base;
  }
  if (assign_target_type &&
      std::holds_alternative<TypePerm>(assign_target_type->node)) {
    const auto* perm = std::get_if<TypePerm>(&assign_target_type->node);
    assign_target_type = perm->base;
  }

  // Type check the value against the place type
  const auto check =
      CheckExprAgainst(ctx, read_ctx, node.value, assign_target_type, env);
  if (!check.ok) {
    if (core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline")) {
      const auto inferred_dbg =
          InferExpr(ctx, node.value,
                    [&](const ast::ExprPtr& inner) {
                      return TypeExprWithCurrentEnv(ctx, read_ctx, env,
                                                    type_expr, inner);
                    },
                    type_place_current,
                    IdentTypeWithCurrentEnv(ctx, env, type_ident));
      if (inferred_dbg.ok) {
        const std::string expected_dbg = TypeToString(place_type.type);
        const std::string inferred_dbg_text = TypeToString(inferred_dbg.type);
        const std::string diag_dbg =
            check.diag_id.has_value() ? std::string(*check.diag_id) : "<none>";
        std::fprintf(stderr,
                     "[assign-check-fail] %s:%zu:%zu expected=%s inferred=%s diag=%s\n",
                     node.span.file.c_str(),
                     node.span.start_line,
                     node.span.start_col,
                     expected_dbg.c_str(),
                     inferred_dbg_text.c_str(),
                     diag_dbg.c_str());
      } else {
        const std::string expected_dbg = TypeToString(place_type.type);
        const std::string diag_dbg =
            check.diag_id.has_value() ? std::string(*check.diag_id) : "<none>";
        std::fprintf(stderr,
                     "[assign-check-fail] %s:%zu:%zu expected=%s inferred=<infer-failed> diag=%s\n",
                     node.span.file.c_str(),
                     node.span.start_line,
                     node.span.start_col,
                     expected_dbg.c_str(),
                     diag_dbg.c_str());
      }
    }
    if (!check.diag_id.has_value() || *check.diag_id == "E-SEM-2526") {
      SPEC_RULE("Assign-Type-Err");
      return {false, "E-SEM-3133", {}, {}};
    }
    return {false, check.diag_id, {}, {}};
  }

  if (shared_write_with_key && read_then_write_same_path && type_ctx.diags) {
    SPEC_RULE("K-RMW-Permitted");
    if (auto diag = core::MakeDiagnosticById(
            IsCompoundRewriteCandidate(node, *place_key_path)
                ? "W-CON-0006"
                : "W-CON-0004",
            node.span)) {
      core::Emit(*type_ctx.diags, *diag);
    }
  }

  TypeEnv out_env = env;
  if (!writes_through_deref && root.has_value()) {
    const auto value_prov = TrackExprProvenance(ctx, node.value, env);
    if (value_prov.ok) {
      UpdateAssignedBindingProvenance(out_env, *root, value_prov);
    }
    if (IsRootIdentifierPlace(node.place)) {
      UpdateAssignedBindingSharedState(
          out_env, *root, ExprNeedsKeyAccess(ctx, read_ctx, node.value, env));
    }
  }

  SPEC_RULE("T-Assign");
  return {true, std::nullopt, std::move(out_env), {}};
}

}  // namespace cursive::analysis
