// =============================================================================
// type_expr.cpp - Main Expression Typing Dispatcher
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Chapter 16: Expressions
//   Section 8.3: Type Inference
//   - ExprJudg: Expression judgments
//   - Lift-Expr: Context lifting
//   - Place-Check: Place expression checking
//
// MIGRATED FROM: cursive-bootstrap/src/03_analysis/types/type_expr.cpp
//   - TypeExpr (lines 945-958)
//   - TypeExprImpl (lines 643-829)
//   - TypePlace (lines 1035-1046)
//   - TypePlaceImpl (lines 832-885)
//   - CheckExprAgainst (lines 960-1000)
//   - CheckPlaceAgainst (lines 1003-1033)
//   - IsPlaceExpr (lines 895-918)
//   - IsInUnsafeSpan (lines 920-942)
//
// =============================================================================

#include "04_analysis/typing/type_expr.h"

#include <cstddef>
#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/span.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/index_access.h"
#include "04_analysis/typing/literals.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/dynamic_context.h"
#include "04_analysis/typing/type_decls.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"
#include "02_source/parser/parser.h"

// Include individual expression typing headers
#include "04_analysis/typing/expr/addr_of.h"
#include "04_analysis/typing/expr/alignof.h"
#include "04_analysis/typing/expr/alloc_expr.h"
#include "04_analysis/typing/expr/array_literal.h"
#include "04_analysis/typing/expr/array_repeat.h"
#include "04_analysis/typing/expr/binary.h"
#include "04_analysis/typing/expr/block_expr.h"
#include "04_analysis/typing/expr/call.h"
#include "04_analysis/typing/expr/call_type_args.h"
#include "04_analysis/typing/expr/cast.h"
#include "04_analysis/typing/expr/closure_expr.h"
#include "04_analysis/typing/expr/deref.h"
#include "04_analysis/typing/expr/enum_literal.h"
#include "04_analysis/typing/expr/error_expr.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/expr/field_access.h"
#include "04_analysis/typing/expr/if_expr.h"
#include "04_analysis/typing/expr/method_call.h"
#include "04_analysis/typing/expr/move_expr.h"
#include "04_analysis/typing/expr/path.h"
#include "04_analysis/typing/expr/propagate_expr.h"
#include "04_analysis/typing/expr/range.h"
#include "04_analysis/typing/expr/record_literal.h"
#include "04_analysis/typing/expr/sizeof.h"
#include "04_analysis/typing/expr/transmute_expr.h"
#include "04_analysis/typing/expr/tuple_access.h"
#include "04_analysis/typing/expr/unary.h"
#include "04_analysis/typing/expr/all_expr.h"
#include "04_analysis/typing/expr/dispatch_expr.h"

// Forward declarations for expression typing modules that currently do not
// expose dedicated public headers.
namespace cursive::analysis::expr {
ExprTypeResult TypeUnsafeBlockExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::UnsafeBlockExpr& expr,
                                       const TypeEnv& env,
                                       const TypeExprFn& type_expr,
                                       const IdentTypeFn& type_ident,
                                       const PlaceTypeFn& type_place);
ExprTypeResult TypeTupleExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::TupleExpr& expr,
                                 const TypeEnv& env);
ExprTypeResult TypeYieldExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::YieldExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);
ExprTypeResult TypeYieldFromExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::YieldFromExpr& expr,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const IdentTypeFn& type_ident,
                                 const PlaceTypeFn& type_place);
ExprTypeResult TypeSyncExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::SyncExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& type_ident,
                            const PlaceTypeFn& type_place);
ExprTypeResult TypeRaceExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::RaceExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& type_ident,
                            const PlaceTypeFn& type_place);
ExprTypeResult TypeSpawnExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::SpawnExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);
ExprTypeResult TypeWaitExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::WaitExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& type_ident,
                            const PlaceTypeFn& type_place);
}  // namespace cursive::analysis::expr

namespace cursive::analysis {
ExprTypeResult TypePipelineExpr(const ast::PipelineExpr& expr,
                                const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const TypeEnv& env);
}  // namespace cursive::analysis

namespace cursive::analysis {

namespace {

static inline void SpecDefsTypeExpr() {
  SPEC_DEF("ExprJudg", "5.2.12");
  SPEC_DEF("Lift-Expr", "5.2.12");
  SPEC_DEF("Place-Check", "5.2.12");
  SPEC_DEF("Syn-PtrNull-Err", "5.2.12");
  SPEC_DEF("P-Ident", "5.2.12");
  SPEC_DEF("P-Field", "5.2.12");
  SPEC_DEF("P-Tuple", "5.2.12");
  SPEC_DEF("P-Index", "5.2.12");
  SPEC_DEF("P-Deref", "5.2.12");
  SPEC_DEF("Expr-Unresolved-Err", "5.2.12");
}

// Check if an attribute is present in the attribute list
bool HasAttribute(const ast::AttributeList& attrs, std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return true;
    }
  }
  return false;
}

bool IsMemoryOrderAttribute(std::string_view name) {
  return name == ::cursive::analysis::attrs::kRelaxed ||
         name == ::cursive::analysis::attrs::kAcquire ||
         name == ::cursive::analysis::attrs::kRelease ||
         name == ::cursive::analysis::attrs::kAcqRel ||
         name == ::cursive::analysis::attrs::kSeqCst;
}

bool HasMemoryOrderAttribute(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (IsMemoryOrderAttribute(attr.name)) {
      return true;
    }
  }
  return false;
}

std::size_t CountMemoryOrderAttributes(const ast::AttributeList& attrs) {
  std::size_t count = 0;
  for (const auto& attr : attrs) {
    if (IsMemoryOrderAttribute(attr.name)) {
      ++count;
    }
  }
  return count;
}

bool ComputeExprDynamicContext(const ast::Expr& expr, bool inherited) {
  if (!ast::DynamicExpr(expr)) {
    return inherited;
  }

  const ast::AttributeList& attrs = ast::ExprAttrList(expr);
  const std::array<DynamicScopeAncestor, 1> ancestors{
      MakeDynamicScopeAncestor(attrs, expr.span)};
  return inherited || ComputeDynamicContext(expr.span, ancestors);
}

bool ExprContainsKeyedOrSharedDataAccess(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& env);

bool BlockContainsKeyedOrSharedDataAccess(const ScopeContext& ctx,
                                          const StmtTypeContext& type_ctx,
                                          const ast::Block& block,
                                          const TypeEnv& env);

std::optional<std::string_view> ValidateMemoryOrderAttributePlacement(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::AttributeList& attrs,
    const ast::ExprPtr& expr,
    const TypeEnv& env) {
  if (!HasMemoryOrderAttribute(attrs)) {
    return std::nullopt;
  }
  if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, expr, env)) {
    return std::nullopt;
  }
  return "E-MOD-2450";
}

bool HasSharedPlacePermission(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::ExprPtr& expr,
                              const TypeEnv& env) {
  if (!expr) {
    return false;
  }

  const auto place = TypePlace(ctx, type_ctx, expr, env);
  return place.ok && place.type &&
         PermOfType(place.type) == Permission::Shared;
}

bool HasSharedReceiverPermission(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::ExprPtr& receiver,
                                 const TypeEnv& env) {
  if (!receiver) {
    return false;
  }

  const auto place = TypePlace(ctx, type_ctx, receiver, env);
  if (place.ok && place.type &&
      PermOfType(place.type) == Permission::Shared) {
    return true;
  }

  const auto value = TypeExpr(ctx, type_ctx, receiver, env);
  return value.ok && value.type &&
         PermOfType(value.type) == Permission::Shared;
}

bool StmtContainsKeyedOrSharedDataAccess(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::Stmt& stmt,
                                         const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          return HasSharedPlacePermission(ctx, type_ctx, node.place, env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.place,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.value,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.value,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          return node.body &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx, *node.body,
                                                      env);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.opts_opt, env) ||
                 (node.body &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return node.body &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx, *node.body,
                                                      env);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.value_opt, env);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt> ||
                             std::is_same_v<T, ast::ErrorStmt>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return true;
        } else {
          return false;
        }
      },
      stmt);
}

bool BlockContainsKeyedOrSharedDataAccess(const ScopeContext& ctx,
                                          const StmtTypeContext& type_ctx,
                                          const ast::Block& block,
                                          const TypeEnv& env) {
  for (const auto& stmt : block.stmts) {
    if (StmtContainsKeyedOrSharedDataAccess(ctx, type_ctx, stmt, env)) {
      return true;
    }
  }

  return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, block.tail_opt,
                                             env);
}

bool ExprContainsKeyedOrSharedDataAccess(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& env) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr> ||
                      std::is_same_v<T, ast::FieldAccessExpr> ||
                      std::is_same_v<T, ast::TupleAccessExpr> ||
                      std::is_same_v<T, ast::IndexAccessExpr> ||
                      std::is_same_v<T, ast::DerefExpr>) {
          if (HasSharedPlacePermission(ctx, type_ctx, expr, env)) {
            return true;
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (HasSharedReceiverPermission(ctx, type_ctx, node.receiver, env)) {
            return true;
          }
        }

        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (const auto* paren = std::get_if<ast::ParenArgs>(&node.args)) {
            for (const auto& arg : paren->args) {
              if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, arg.value,
                                                      env)) {
                return true;
              }
            }
          } else if (const auto* brace =
                         std::get_if<ast::BraceArgs>(&node.args)) {
            for (const auto& field : brace->fields) {
              if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                      field.value, env)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.lhs,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.rhs,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.lhs,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.rhs,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.value,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.expr,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.place,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, elem, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            found = found || ExprContainsKeyedOrSharedDataAccess(
                                 ctx, type_ctx, elem, env);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.value,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.count,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, field.value,
                                                    env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          return std::visit(
              [&](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& elem : payload.elements) {
                    if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, elem,
                                                            env)) {
                      return true;
                    }
                  }
                } else if constexpr (std::is_same_v<P,
                                                    ast::EnumPayloadBrace>) {
                  for (const auto& field : payload.fields) {
                    if (ExprContainsKeyedOrSharedDataAccess(
                            ctx, type_ctx, field.value, env)) {
                      return true;
                    }
                  }
                }
                return false;
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.cond,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.then_expr, env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.scrutinee,
                                                  env)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                    case_clause.body, env)) {
              return true;
            }
          }
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.scrutinee, env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.then_expr, env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                     node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return (node.invariant_opt.has_value() &&
                  ExprContainsKeyedOrSharedDataAccess(
                      ctx, type_ctx, node.invariant_opt->predicate, env)) ||
                 (node.body &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.cond,
                                                     env) ||
                 (node.invariant_opt.has_value() &&
                  ExprContainsKeyedOrSharedDataAccess(
                      ctx, type_ctx, node.invariant_opt->predicate, env)) ||
                 (node.body &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.iter,
                                                     env) ||
                 (node.invariant_opt.has_value() &&
                  ExprContainsKeyedOrSharedDataAccess(
                      ctx, type_ctx, node.invariant_opt->predicate, env)) ||
                 (node.body &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                      *node.block, env);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.body,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.cond,
                                                     env) ||
                 (node.then_block &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.then_block, env)) ||
                 (node.else_block_opt &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.else_block_opt,
                                                       env));
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.iter,
                                                     env) ||
                 (node.body &&
                  BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                       *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.expr,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.body,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.lhs,
                                                     env) ||
                 ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.rhs,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr> ||
                             std::is_same_v<T, ast::CallTypeArgsExpr>) {
          if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.callee,
                                                  env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, arg.value,
                                                    env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.receiver,
                                                  env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, arg.value,
                                                    env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, arm.expr,
                                                    env) ||
                ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                    arm.handler.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& sub : node.exprs) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, sub, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.domain,
                                                  env)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, opt.value,
                                                    env)) {
              return true;
            }
          }
          return node.body &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                      *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, opt.value,
                                                    env)) {
              return true;
            }
          }
          return node.body &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                      *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.handle,
                                                     env);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx, node.range,
                                                  env)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                    opt.chunk_expr, env) ||
                ExprContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                    opt.workgroup_expr, env)) {
              return true;
            }
          }
          return node.body &&
                 BlockContainsKeyedOrSharedDataAccess(ctx, type_ctx,
                                                      *node.body, env);
        } else {
          return false;
        }
      },
      expr->node);
}


static TypeRef ProjectFilesType() { return MakeTypePath({"ProjectFiles"}); }
static TypeRef TypeEmitterType() { return MakeTypePath({"TypeEmitter"}); }
static TypeRef IntrospectType() { return MakeTypePath({"Introspect"}); }
static TypeRef ComptimeDiagnosticsType() { return MakeTypePath({"ComptimeDiagnostics"}); }
static TypeRef TypeMetaType() { return MakeTypePath({"Type"}); }
static TypeRef AstMetaType() { return MakeTypePath({"Ast"}); }
static TypeRef AstExprMetaType() { return MakeTypePath({"Ast", "Expr"}); }
static TypeRef AstStmtMetaType() { return MakeTypePath({"Ast", "Stmt"}); }
static TypeRef AstItemMetaType() { return MakeTypePath({"Ast", "Item"}); }
static TypeRef AstTypeMetaType() { return MakeTypePath({"Ast", "Type"}); }
static TypeRef AstPatternMetaType() { return MakeTypePath({"Ast", "Pattern"}); }

static std::optional<ast::QuoteKind> ExpectedQuoteKind(const TypeRef& type) {
  const auto* path = type ? std::get_if<TypePathType>(&type->node) : nullptr;
  if (!path || path->generic_args.size() != 0) {
    return std::nullopt;
  }
  if (path->path == TypePath{"Ast", "Expr"}) {
    return ast::QuoteKind::Expr;
  }
  if (path->path == TypePath{"Ast", "Stmt"}) {
    return ast::QuoteKind::Stmt;
  }
  if (path->path == TypePath{"Ast", "Item"}) {
    return ast::QuoteKind::Item;
  }
  if (path->path == TypePath{"Ast", "Type"}) {
    return ast::QuoteKind::Type;
  }
  if (path->path == TypePath{"Ast", "Pattern"}) {
    return ast::QuoteKind::Pattern;
  }
  return std::nullopt;
}

static bool IsAstMetaType(const TypeRef& type) {
  const auto* path = type ? std::get_if<TypePathType>(&type->node) : nullptr;
  return path && path->generic_args.empty() && path->path == TypePath{"Ast"};
}

static TypeRef QuoteKindType(ast::QuoteKind kind) {
  switch (kind) {
    case ast::QuoteKind::Expr:
      return AstExprMetaType();
    case ast::QuoteKind::Stmt:
      return AstStmtMetaType();
    case ast::QuoteKind::Unspecified:
      return AstMetaType();
    case ast::QuoteKind::Item:
      return AstItemMetaType();
    case ast::QuoteKind::Type:
      return AstTypeMetaType();
    case ast::QuoteKind::Pattern:
      return AstPatternMetaType();
  }
  return AstMetaType();
}

static ast::Parser MakeQuoteTokenParser(const std::vector<ast::Token>& tokens) {
  static const std::vector<ast::DocComment> kNoDocs;

  ast::Parser parser;
  parser.owned_tokens = std::make_shared<std::vector<ast::Token>>(tokens);
  parser.tokens = parser.owned_tokens.get();
  parser.docs = &kNoDocs;
  parser.quote_mode = true;
  return parser;
}

static bool IsQuotedStatementForm(const ast::Stmt& stmt) {
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        return std::is_same_v<T, ast::LetStmt> ||
               std::is_same_v<T, ast::VarStmt> ||
               std::is_same_v<T, ast::UsingLocalStmt> ||
               std::is_same_v<T, ast::AssignStmt> ||
               std::is_same_v<T, ast::CompoundAssignStmt> ||
               std::is_same_v<T, ast::ExprStmt> ||
               std::is_same_v<T, ast::ReturnStmt> ||
               std::is_same_v<T, ast::BreakStmt> ||
               std::is_same_v<T, ast::ContinueStmt> ||
               std::is_same_v<T, ast::DeferStmt> ||
               std::is_same_v<T, ast::RegionStmt> ||
               std::is_same_v<T, ast::FrameStmt> ||
               std::is_same_v<T, ast::UnsafeBlockStmt> ||
               std::is_same_v<T, ast::KeyBlockStmt> ||
               std::is_same_v<T, ast::ComptimeStmt>;
      },
      stmt);
}

static bool QuoteParsesAs(const ast::QuoteExpr& quote, ast::QuoteKind kind) {
  ast::Parser parser = MakeQuoteTokenParser(quote.tokens);
  const std::size_t start_index = parser.index;
  switch (kind) {
    case ast::QuoteKind::Expr: {
      auto parsed = ast::ParseExpr(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Stmt: {
      auto parsed = ast::ParseStmt(parser);
      return parsed.parser.index != start_index &&
             IsQuotedStatementForm(parsed.elem) && ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Unspecified:
      return false;
    case ast::QuoteKind::Item: {
      auto parsed = ast::ParseItem(parser);
      return parsed.parser.index != start_index &&
             !std::holds_alternative<ast::ErrorItem>(parsed.item) &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Type: {
      auto parsed = ast::ParseType(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
    case ast::QuoteKind::Pattern: {
      auto parsed = ast::ParsePattern(parser);
      return parsed.parser.index != start_index && parsed.elem &&
             ast::AtEof(parsed.parser);
    }
  }
  return false;
}

static std::optional<ast::QuoteKind> ResolveQuoteKindStatic(
    const ast::QuoteExpr& quote,
    const TypeRef* expected_type) {
  if (quote.kind != ast::QuoteKind::Unspecified) {
    return QuoteParsesAs(quote, quote.kind) ? std::optional<ast::QuoteKind>(quote.kind)
                                            : std::nullopt;
  }

  if (expected_type) {
    if (auto expected_kind = ExpectedQuoteKind(*expected_type)) {
      return QuoteParsesAs(quote, *expected_kind) ? expected_kind : std::nullopt;
    }
  }

  std::optional<ast::QuoteKind> resolved;
  std::size_t matches = 0;
  for (ast::QuoteKind kind :
       {ast::QuoteKind::Expr, ast::QuoteKind::Stmt, ast::QuoteKind::Item}) {
    const bool parses = QuoteParsesAs(quote, kind);
    if (!parses) {
      continue;
    }
    ++matches;
    if (!resolved.has_value()) {
      resolved = kind;
    }
  }
  if (matches == 1) {
    return resolved;
  }
  return std::nullopt;
}

static bool IsComptimeEnv(const TypeEnv& env) {
  return BindOf(env, "diagnostics").has_value() || BindOf(env, "introspect").has_value() ||
         BindOf(env, "emitter").has_value() || BindOf(env, "files").has_value() ||
         BindOf(env, "target").has_value();
}

static TypeEnv ExtendComptimeEnv(const TypeEnv& env, const ast::AttributeList* attr_list) {
  TypeEnv out = PushScope(env);
  auto& scope = out.scopes.back();
  scope[IdKeyOf("introspect")] = TypeBinding{ast::Mutability::Let, IntrospectType()};
  scope[IdKeyOf("diagnostics")] = TypeBinding{ast::Mutability::Let, ComptimeDiagnosticsType()};
  if (attr_list && HasAttribute(*attr_list, ::cursive::analysis::attrs::kFiles)) {
    scope[IdKeyOf("files")] = TypeBinding{ast::Mutability::Let, ProjectFilesType()};
  }
  if (attr_list && HasAttribute(*attr_list, ::cursive::analysis::attrs::kEmit)) {
    scope[IdKeyOf("emitter")] = TypeBinding{ast::Mutability::Let, TypeEmitterType()};
  }
  return out;
}

static std::string TypePathKeyString(const TypePath& path) {
  if (path.empty()) {
    return {};
  }
  std::string out = path.front();
  for (std::size_t i = 1; i < path.size(); ++i) {
    out.append("::");
    out.append(path[i]);
  }
  return out;
}

static bool IsComptimeMetaTypePath(const TypePath& path) {
  return path == TypePath{"Type"} || path == TypePath{"Ast"} ||
         path == TypePath{"Ast", "Expr"} ||
         path == TypePath{"Ast", "Stmt"} ||
         path == TypePath{"Ast", "Item"} ||
         path == TypePath{"Ast", "Type"} ||
         path == TypePath{"Ast", "Pattern"};
}

static std::vector<std::string> GenericParamNamesForCt(
    const std::optional<ast::GenericParams>& generic_params_opt) {
  std::vector<std::string> names;
  if (!generic_params_opt.has_value()) {
    return names;
  }
  names.reserve(generic_params_opt->params.size());
  for (const auto& param : generic_params_opt->params) {
    names.push_back(param.name);
  }
  return names;
}

static std::optional<std::vector<TypeRef>> ResolveDeclGenericArgsForCt(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& generic_params_opt,
    const std::vector<TypeRef>& provided_args) {
  if (!generic_params_opt.has_value()) {
    if (!provided_args.empty()) {
      return std::nullopt;
    }
    return std::vector<TypeRef>{};
  }

  const auto& params = generic_params_opt->params;
  if (provided_args.size() > params.size()) {
    return std::nullopt;
  }

  std::vector<TypeRef> out;
  out.reserve(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i < provided_args.size()) {
      out.push_back(provided_args[i]);
      continue;
    }
    if (!params[i].default_type) {
      return std::nullopt;
    }
    const auto lowered = LowerType(ctx, params[i].default_type);
    if (!lowered.ok || !lowered.type) {
      return std::nullopt;
    }
    out.push_back(lowered.type);
  }
  return out;
}

static bool CtAvailTypeImpl(const ScopeContext& ctx,
                            const TypeRef& type,
                            std::set<std::string>& active_paths);

static bool CtAvailRecordFields(const ScopeContext& ctx,
                                const ast::RecordDecl& decl,
                                const std::vector<TypeRef>& args,
                                std::set<std::string>& active_paths) {
  const auto param_names = GenericParamNamesForCt(decl.generic_params);
  for (const auto* field : RecordFields(decl)) {
    if (!field || !field->type) {
      continue;
    }
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    const auto field_type =
        ApplyGenericSubstitution(lowered.type, param_names, args);
    if (!CtAvailTypeImpl(ctx, StripPerm(field_type), active_paths)) {
      return false;
    }
  }
  return true;
}

static bool CtAvailEnumPayloads(const ScopeContext& ctx,
                                const ast::EnumDecl& decl,
                                const std::vector<TypeRef>& args,
                                std::set<std::string>& active_paths) {
  const auto param_names = GenericParamNamesForCt(decl.generic_params);
  for (const auto& variant : decl.variants) {
    if (!variant.payload_opt.has_value()) {
      continue;
    }
    bool ok = true;
    std::visit(
        [&](const auto& payload) {
          using P = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
            for (const auto& elem : payload.elements) {
              const auto lowered = LowerType(ctx, elem);
              if (!lowered.ok || !lowered.type) {
                ok = false;
                return;
              }
              const auto elem_type =
                  ApplyGenericSubstitution(lowered.type, param_names, args);
              if (!CtAvailTypeImpl(ctx, StripPerm(elem_type), active_paths)) {
                ok = false;
                return;
              }
            }
          } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
            for (const auto& field : payload.fields) {
              const auto lowered = LowerType(ctx, field.type);
              if (!lowered.ok || !lowered.type) {
                ok = false;
                return;
              }
              const auto field_type =
                  ApplyGenericSubstitution(lowered.type, param_names, args);
              if (!CtAvailTypeImpl(ctx, StripPerm(field_type), active_paths)) {
                ok = false;
                return;
              }
            }
          }
        },
        *variant.payload_opt);
    if (!ok) {
      return false;
    }
  }
  return true;
}

static bool CtAvailTypeImpl(const ScopeContext& ctx,
                            const TypeRef& type,
                            std::set<std::string>& active_paths) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return node.state.has_value() &&
                 (*node.state == StringState::View ||
                  *node.state == StringState::Managed);
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return node.state.has_value() &&
                 (*node.state == BytesState::View ||
                  *node.state == BytesState::Managed);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (!CtAvailTypeImpl(ctx, elem, active_paths)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return CtAvailTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return CtAvailTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return CtAvailTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          if (IsComptimeMetaTypePath(node.path)) {
            return true;
          }

          const std::string key = TypePathKeyString(node.path);
          if (!active_paths.insert(key).second) {
            return false;
          }

          if (const auto* record = LookupRecordDecl(ctx, node.path)) {
            const auto args =
                ResolveDeclGenericArgsForCt(ctx, record->generic_params,
                                            node.generic_args);
            const bool ok = args.has_value() &&
                            CtAvailRecordFields(ctx, *record, *args, active_paths);
            active_paths.erase(key);
            return ok;
          }

          if (const auto* en = LookupEnumDecl(ctx, node.path)) {
            const auto args =
                ResolveDeclGenericArgsForCt(ctx, en->generic_params,
                                            node.generic_args);
            const bool ok = args.has_value() &&
                            CtAvailEnumPayloads(ctx, *en, *args, active_paths);
            active_paths.erase(key);
            return ok;
          }

          active_paths.erase(key);
          return false;
        } else {
          return false;
        }
      },
      type->node);
}

static bool CtAvailType(const ScopeContext& ctx, const TypeRef& type) {
  std::set<std::string> active_paths;
  return CtAvailTypeImpl(ctx, type, active_paths);
}

enum class QuoteSplicePosition {
  Expr,
  Stmt,
  Item,
  Type,
  Pattern,
  Identifier,
};

static CheckResult OkCheckResult() {
  CheckResult result;
  result.ok = true;
  return result;
}

static CheckResult MakeDiagCheckResult(std::string_view diag_id,
                                       const core::Span& span) {
  CheckResult result;
  result.ok = false;
  result.diag_id = diag_id;
  result.diag_span = span;
  return result;
}

static bool IsExactTypePath(const TypeRef& type, const TypePath& path) {
  const auto stripped = StripPerm(type);
  const auto* node = stripped ? std::get_if<TypePathType>(&stripped->node) : nullptr;
  return node && node->generic_args.empty() && node->path == path;
}

static bool IsAnyAstType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  const auto* node = stripped ? std::get_if<TypePathType>(&stripped->node) : nullptr;
  return node && node->generic_args.empty() && !node->path.empty() &&
         node->path.front() == "Ast";
}

static bool IsStringStateType(const TypeRef& type, StringState state) {
  const auto stripped = StripPerm(type);
  const auto* node = stripped ? std::get_if<TypeString>(&stripped->node) : nullptr;
  return node && node->state.has_value() && *node->state == state;
}

static bool CtLiteralTypeImpl(const ScopeContext& ctx,
                              const TypeRef& type,
                              std::set<std::string>& active_paths);

static bool CtLiteralRecordFields(const ScopeContext& ctx,
                                  const ast::RecordDecl& decl,
                                  const std::vector<TypeRef>& args,
                                  std::set<std::string>& active_paths) {
  const auto param_names = GenericParamNamesForCt(decl.generic_params);
  for (const auto* field : RecordFields(decl)) {
    if (!field || !field->type) {
      continue;
    }
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    const auto field_type =
        ApplyGenericSubstitution(lowered.type, param_names, args);
    if (!CtLiteralTypeImpl(ctx, StripPerm(field_type), active_paths)) {
      return false;
    }
  }
  return true;
}

static bool CtLiteralEnumPayloads(const ScopeContext& ctx,
                                  const ast::EnumDecl& decl,
                                  const std::vector<TypeRef>& args,
                                  std::set<std::string>& active_paths) {
  const auto param_names = GenericParamNamesForCt(decl.generic_params);
  for (const auto& variant : decl.variants) {
    if (!variant.payload_opt.has_value()) {
      continue;
    }
    bool ok = true;
    std::visit(
        [&](const auto& payload) {
          using P = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
            for (const auto& elem : payload.elements) {
              const auto lowered = LowerType(ctx, elem);
              if (!lowered.ok || !lowered.type) {
                ok = false;
                return;
              }
              const auto elem_type =
                  ApplyGenericSubstitution(lowered.type, param_names, args);
              if (!CtLiteralTypeImpl(ctx, StripPerm(elem_type), active_paths)) {
                ok = false;
                return;
              }
            }
          } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
            for (const auto& field : payload.fields) {
              const auto lowered = LowerType(ctx, field.type);
              if (!lowered.ok || !lowered.type) {
                ok = false;
                return;
              }
              const auto field_type =
                  ApplyGenericSubstitution(lowered.type, param_names, args);
              if (!CtLiteralTypeImpl(ctx, StripPerm(field_type), active_paths)) {
                ok = false;
                return;
              }
            }
          }
        },
        *variant.payload_opt);
    if (!ok) {
      return false;
    }
  }
  return true;
}

static bool CtLiteralTypeImpl(const ScopeContext& ctx,
                              const TypeRef& type,
                              std::set<std::string>& active_paths) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return node.name != "!";
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return node.state.has_value() &&
                 (*node.state == StringState::View ||
                  *node.state == StringState::Managed);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (!CtLiteralTypeImpl(ctx, elem, active_paths)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return CtLiteralTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return CtLiteralTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return CtLiteralTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          const std::string key = TypePathKeyString(node.path);
          if (!active_paths.insert(key).second) {
            return false;
          }

          if (const auto* record = LookupRecordDecl(ctx, node.path)) {
            const auto args =
                ResolveDeclGenericArgsForCt(ctx, record->generic_params,
                                            node.generic_args);
            const bool ok = args.has_value() &&
                            CtLiteralRecordFields(ctx, *record, *args, active_paths);
            active_paths.erase(key);
            return ok;
          }

          if (const auto* en = LookupEnumDecl(ctx, node.path)) {
            const auto args =
                ResolveDeclGenericArgsForCt(ctx, en->generic_params,
                                            node.generic_args);
            const bool ok = args.has_value() &&
                            CtLiteralEnumPayloads(ctx, *en, *args, active_paths);
            active_paths.erase(key);
            return ok;
          }

          active_paths.erase(key);
          return false;
        } else {
          return false;
        }
      },
      type->node);
}

static bool CtLiteralType(const ScopeContext& ctx, const TypeRef& type) {
  std::set<std::string> active_paths;
  return CtLiteralTypeImpl(ctx, type, active_paths);
}

static bool IsSpliceCompatible(const ScopeContext& ctx,
                               QuoteSplicePosition pos,
                               const TypeRef& type) {
  switch (pos) {
    case QuoteSplicePosition::Expr:
      return IsAstMetaType(type) || IsExactTypePath(type, {"Ast", "Expr"}) ||
             (!IsAnyAstType(type) && CtLiteralType(ctx, type));
    case QuoteSplicePosition::Stmt:
      return IsExactTypePath(type, {"Ast", "Stmt"}) ||
             IsExactTypePath(type, {"Ast", "Expr"});
    case QuoteSplicePosition::Item:
      return IsExactTypePath(type, {"Ast", "Item"});
    case QuoteSplicePosition::Type:
      return IsExactTypePath(type, {"Ast", "Type"}) ||
             IsExactTypePath(type, {"Type"});
    case QuoteSplicePosition::Pattern:
      return IsExactTypePath(type, {"Ast", "Pattern"});
    case QuoteSplicePosition::Identifier:
      return IsStringStateType(type, StringState::Managed) ||
             IsStringStateType(type, StringState::View);
  }
  return false;
}

static CheckResult CheckQuoteExprSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& env);

static CheckResult CheckQuoteTypeSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::TypePtr& type,
                                         const TypeEnv& env);

static CheckResult CheckQuotePatternSplices(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::PatternPtr& pattern,
                                            const TypeEnv& env);

static CheckResult CheckQuoteStmtSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::Stmt& stmt,
                                         const TypeEnv& env);

static CheckResult CheckQuoteItemSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ASTItem& item,
                                         const TypeEnv& env);

static CheckResult CheckQuoteApplyArgsSplices(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::ApplyArgs& args,
                                              const TypeEnv& env);

static CheckResult CheckQuoteKeyPathSplices(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::KeyPathExpr& key_path,
                                            const TypeEnv& env);

static CheckResult CheckSpliceSource(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::ExprPtr& source,
                                     const TypeEnv& env,
                                     QuoteSplicePosition pos,
                                     const core::Span& span) {
  const auto typed = TypeExpr(ctx, type_ctx, source, env);
  if (!typed.ok) {
    CheckResult result;
    result.ok = false;
    result.diag_id = typed.diag_id;
    result.diag_detail = typed.diag_detail;
    result.diag_span = typed.diag_span.has_value() ? typed.diag_span : span;
    return result;
  }
  if (!IsSpliceCompatible(ctx, pos, typed.type)) {
    return MakeDiagCheckResult("E-CTE-0230", span);
  }
  return OkCheckResult();
}

static CheckResult CheckSplicedIdentifier(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::SpliceIdentNode>& splice_opt,
    const TypeEnv& env) {
  if (!splice_opt.has_value()) {
    return OkCheckResult();
  }
  return CheckSpliceSource(ctx, type_ctx, splice_opt->name_expr, env,
                           QuoteSplicePosition::Identifier, splice_opt->span);
}

static CheckResult CheckQuoteArgSplices(const ScopeContext& ctx,
                                        const StmtTypeContext& type_ctx,
                                        const ast::Arg& arg,
                                        const TypeEnv& env) {
  return CheckQuoteExprSplices(ctx, type_ctx, arg.value, env);
}

static CheckResult CheckQuoteFieldInitSplices(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::FieldInit& field,
                                              const TypeEnv& env) {
  return CheckQuoteExprSplices(ctx, type_ctx, field.value, env);
}

static CheckResult CheckQuoteApplyArgsSplices(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::ApplyArgs& args,
                                              const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          for (const auto& arg : node.args) {
            auto checked = CheckQuoteArgSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else {
          for (const auto& field : node.fields) {
            auto checked =
                CheckQuoteFieldInitSplices(ctx, type_ctx, field, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        }
      },
      args);
}

static CheckResult CheckQuoteRaceHandlerSplices(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const ast::RaceHandler& handler,
                                                const TypeEnv& env) {
  return CheckQuoteExprSplices(ctx, type_ctx, handler.value, env);
}

static CheckResult CheckQuoteRaceArmSplices(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::RaceArm& arm,
                                            const TypeEnv& env) {
  auto checked = CheckQuoteExprSplices(ctx, type_ctx, arm.expr, env);
  if (!checked.ok) {
    return checked;
  }
  checked = CheckQuotePatternSplices(ctx, type_ctx, arm.pattern, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteRaceHandlerSplices(ctx, type_ctx, arm.handler, env);
}

static CheckResult CheckQuoteParallelOptionSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::ParallelOption& opt,
    const TypeEnv& env) {
  return CheckQuoteExprSplices(ctx, type_ctx, opt.value, env);
}

static CheckResult CheckQuoteSpawnOptionSplices(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const ast::SpawnOption& opt,
                                                const TypeEnv& env) {
  return CheckQuoteExprSplices(ctx, type_ctx, opt.value, env);
}

static CheckResult CheckQuoteDispatchOptionSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::DispatchOption& opt,
    const TypeEnv& env) {
  auto checked = CheckQuoteExprSplices(ctx, type_ctx, opt.chunk_expr, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteExprSplices(ctx, type_ctx, opt.workgroup_expr, env);
}

static CheckResult CheckQuoteEnumPayloadSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::EnumPayload>& payload_opt,
    const TypeEnv& env) {
  if (!payload_opt.has_value()) {
    return OkCheckResult();
  }
  return std::visit(
      [&](const auto& payload) -> CheckResult {
        using P = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
          for (const auto& elem : payload.elements) {
            auto checked = CheckQuoteExprSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
        } else {
          for (const auto& field : payload.fields) {
            auto checked = CheckQuoteFieldInitSplices(ctx, type_ctx, field, env);
            if (!checked.ok) {
              return checked;
            }
          }
        }
        return OkCheckResult();
      },
      *payload_opt);
}

static CheckResult CheckQuoteBlockSplices(const ScopeContext& ctx,
                                          const StmtTypeContext& type_ctx,
                                          const ast::Block& block,
                                          const TypeEnv& env) {
  for (const auto& stmt : block.stmts) {
    auto checked = CheckQuoteStmtSplices(ctx, type_ctx, stmt, env);
    if (!checked.ok) {
      return checked;
    }
  }
  return CheckQuoteExprSplices(ctx, type_ctx, block.tail_opt, env);
}

static CheckResult CheckQuoteOptionalBlockSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::BlockPtr& block_opt,
    const TypeEnv& env) {
  if (!block_opt) {
    return OkCheckResult();
  }
  return CheckQuoteBlockSplices(ctx, type_ctx, *block_opt, env);
}

static CheckResult CheckQuoteLoopInvariantSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::LoopInvariant>& invariant_opt,
    const TypeEnv& env) {
  if (!invariant_opt.has_value()) {
    return OkCheckResult();
  }
  return CheckQuoteExprSplices(ctx, type_ctx, invariant_opt->predicate, env);
}

static CheckResult CheckQuoteExprSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& env) {
  if (!expr) {
    return OkCheckResult();
  }
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return CheckSpliceSource(ctx, type_ctx, node.expr, env,
                                   QuoteSplicePosition::Expr, node.span);
        } else if constexpr (std::is_same_v<T, ast::SpliceIdentNode>) {
          return CheckSpliceSource(ctx, type_ctx, node.name_expr, env,
                                   QuoteSplicePosition::Identifier, node.span);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.lhs, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return CheckQuoteApplyArgsSplices(ctx, type_ctx, node.args, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.callee, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& arg : node.generic_args) {
            checked = CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          for (const auto& arg : node.args) {
            checked = CheckQuoteArgSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto checked =
              CheckQuoteExprSplices(ctx, type_ctx, node.receiver, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& arg : node.args) {
            checked = CheckQuoteArgSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.lhs, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.place, env);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.place, env);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            auto checked = CheckQuoteExprSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          CheckResult result = OkCheckResult();
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (!result.ok) {
              return;
            }
            result = CheckQuoteExprSplices(ctx, type_ctx, elem, env);
          });
          return result;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.count, env);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            auto checked = CheckQuoteFieldInitSplices(ctx, type_ctx, field, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return std::visit(
              [&](const auto& target) -> CheckResult {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<Target, ast::ModalStateRef>) {
                  for (const auto& arg : target.generic_args) {
                    auto checked =
                        CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
                    if (!checked.ok) {
                      return checked;
                    }
                  }
                }
                return OkCheckResult();
              },
              node.target);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          return CheckQuoteEnumPayloadSplices(ctx, type_ctx, node.payload_opt, env);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.cond, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteExprSplices(ctx, type_ctx, node.then_expr, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto checked =
              CheckQuoteExprSplices(ctx, type_ctx, node.scrutinee, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuotePatternSplices(ctx, type_ctx, node.pattern, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteExprSplices(ctx, type_ctx, node.then_expr, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto checked =
              CheckQuoteExprSplices(ctx, type_ctx, node.scrutinee, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& clause : node.cases) {
            checked = CheckQuotePatternSplices(ctx, type_ctx, clause.pattern, env);
            if (!checked.ok) {
              return checked;
            }
            checked = CheckQuoteExprSplices(ctx, type_ctx, clause.body, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          auto checked = CheckQuoteLoopInvariantSplices(ctx, type_ctx,
                                                        node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.cond, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteLoopInvariantSplices(ctx, type_ctx,
                                                   node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.pattern, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteTypeSplices(ctx, type_ctx, node.type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteExprSplices(ctx, type_ctx, node.iter, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteLoopInvariantSplices(ctx, type_ctx,
                                                   node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.block, env);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.block, env);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.body, env);
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.cond, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteBlockSplices(ctx, type_ctx, *node.then_block, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteOptionalBlockSplices(ctx, type_ctx,
                                                node.else_block_opt, env);
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.pattern, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteTypeSplices(ctx, type_ctx, node.type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteExprSplices(ctx, type_ctx, node.iter, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.expr, env);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.expr, env);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          auto checked = CheckQuoteTypeSplices(ctx, type_ctx, node.from, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteTypeSplices(ctx, type_ctx, node.to, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (const auto& param : node.params) {
            auto checked =
                CheckQuoteTypeSplices(ctx, type_ctx, param.type_opt, env);
            if (!checked.ok) {
              return checked;
            }
          }
          auto checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.ret_type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.body, env);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.lhs, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.base, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.index, env);
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.callee, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& arg : node.type_args) {
            checked = CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          for (const auto& arg : node.args) {
            checked = CheckQuoteArgSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            auto checked = CheckQuoteRaceArmSplices(ctx, type_ctx, arm, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& elem : node.exprs) {
            auto checked = CheckQuoteExprSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.domain, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& opt : node.opts) {
            checked = CheckQuoteParallelOptionSplices(ctx, type_ctx, opt, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            auto checked = CheckQuoteSpawnOptionSplices(ctx, type_ctx, opt, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.handle, env);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.pattern, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteExprSplices(ctx, type_ctx, node.range, env);
          if (!checked.ok) {
            return checked;
          }
          if (node.key_clause.has_value()) {
            checked = CheckQuoteKeyPathSplices(ctx, type_ctx,
                                               node.key_clause->key_path, env);
            if (!checked.ok) {
              return checked;
            }
          }
          for (const auto& opt : node.opts) {
            checked = CheckQuoteDispatchOptionSplices(ctx, type_ctx, opt, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else {
          return OkCheckResult();
        }
      },
      expr->node);
}

static CheckResult CheckQuoteTypeSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::TypePtr& type,
                                         const TypeEnv& env) {
  if (!type) {
    return OkCheckResult();
  }
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return CheckSpliceSource(ctx, type_ctx, node.expr, env,
                                   QuoteSplicePosition::Type, node.span);
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& elem : node.types) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, param.type, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteTypeSplices(ctx, type_ctx, node.ret, env);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, param.type, env);
            if (!checked.ok) {
              return checked;
            }
          }
          auto checked = CheckQuoteTypeSplices(ctx, type_ctx, node.ret, env);
          if (!checked.ok) {
            return checked;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              checked = CheckQuoteTypeSplices(ctx, type_ctx, dep.type, env);
              if (!checked.ok) {
                return checked;
              }
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          auto checked = CheckQuoteTypeSplices(ctx, type_ctx, node.element, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.length, env);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.element, env);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.element, env);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.element, env);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          for (const auto& arg : node.generic_args) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          for (const auto& arg : node.args) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, arg, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          auto checked = CheckQuoteTypeSplices(ctx, type_ctx, node.base, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.predicate, env);
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.base, env);
        } else {
          return OkCheckResult();
        }
      },
      type->node);
}

static CheckResult CheckQuotePatternSplices(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::PatternPtr& pattern,
                                            const TypeEnv& env) {
  if (!pattern) {
    return OkCheckResult();
  }
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return CheckSpliceSource(ctx, type_ctx, node.expr, env,
                                   QuoteSplicePosition::Pattern, node.span);
        } else if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return CheckSplicedIdentifier(ctx, type_ctx, node.name_splice_opt, env);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          auto checked =
              CheckSplicedIdentifier(ctx, type_ctx, node.name_splice_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            auto checked = CheckQuotePatternSplices(ctx, type_ctx, elem, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            auto checked =
                CheckQuotePatternSplices(ctx, type_ctx, field.pattern_opt, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return OkCheckResult();
          }
          return std::visit(
              [&](const auto& payload) -> CheckResult {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    auto checked =
                        CheckQuotePatternSplices(ctx, type_ctx, elem, env);
                    if (!checked.ok) {
                      return checked;
                    }
                  }
                } else {
                  for (const auto& field : payload.fields) {
                    auto checked = CheckQuotePatternSplices(
                        ctx, type_ctx, field.pattern_opt, env);
                    if (!checked.ok) {
                      return checked;
                    }
                  }
                }
                return OkCheckResult();
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (node.fields_opt.has_value()) {
            for (const auto& field : node.fields_opt->fields) {
              auto checked =
                  CheckQuotePatternSplices(ctx, type_ctx, field.pattern_opt, env);
              if (!checked.ok) {
                return checked;
              }
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          auto checked = CheckQuotePatternSplices(ctx, type_ctx, node.lo, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuotePatternSplices(ctx, type_ctx, node.hi, env);
        } else {
          return OkCheckResult();
        }
      },
      pattern->node);
}

static CheckResult CheckQuoteParamSplices(const ScopeContext& ctx,
                                          const StmtTypeContext& type_ctx,
                                          const ast::Param& param,
                                          const TypeEnv& env) {
  auto checked =
      CheckSplicedIdentifier(ctx, type_ctx, param.name_splice_opt, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteTypeSplices(ctx, type_ctx, param.type, env);
}

static CheckResult CheckQuoteGenericParamsSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::GenericParams>& params_opt,
    const TypeEnv& env) {
  if (!params_opt.has_value()) {
    return OkCheckResult();
  }
  for (const auto& param : params_opt->params) {
    auto checked = CheckQuoteTypeSplices(ctx, type_ctx, param.default_type, env);
    if (!checked.ok) {
      return checked;
    }
  }
  return OkCheckResult();
}

static CheckResult CheckQuoteWhereClauseSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::PredicateClause>& clause_opt,
    const TypeEnv& env) {
  if (!clause_opt.has_value()) {
    return OkCheckResult();
  }
  for (const auto& predicate : *clause_opt) {
    auto checked = CheckQuoteTypeSplices(ctx, type_ctx, predicate.type, env);
    if (!checked.ok) {
      return checked;
    }
  }
  return OkCheckResult();
}

static CheckResult CheckQuoteContractClauseSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::ContractClause>& clause_opt,
    const TypeEnv& env) {
  if (!clause_opt.has_value()) {
    return OkCheckResult();
  }
  auto checked =
      CheckQuoteExprSplices(ctx, type_ctx, clause_opt->precondition, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteExprSplices(ctx, type_ctx, clause_opt->postcondition, env);
}

static CheckResult CheckQuoteTypeInvariantSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const std::optional<ast::TypeInvariant>& invariant_opt,
    const TypeEnv& env) {
  if (!invariant_opt.has_value()) {
    return OkCheckResult();
  }
  return CheckQuoteExprSplices(ctx, type_ctx, invariant_opt->predicate, env);
}

static CheckResult CheckQuoteReceiverSplices(const ScopeContext& ctx,
                                             const StmtTypeContext& type_ctx,
                                             const ast::Receiver& receiver,
                                             const TypeEnv& env) {
  if (const auto* explicit_recv = std::get_if<ast::ReceiverExplicit>(&receiver)) {
    return CheckQuoteTypeSplices(ctx, type_ctx, explicit_recv->type, env);
  }
  return OkCheckResult();
}

static CheckResult CheckQuoteFieldDeclSplices(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::FieldDecl& field,
                                              const TypeEnv& env) {
  auto checked = CheckQuoteTypeSplices(ctx, type_ctx, field.type, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteExprSplices(ctx, type_ctx, field.init_opt, env);
}

static CheckResult CheckQuoteMethodDeclSplices(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const ast::MethodDecl& method,
                                               const TypeEnv& env) {
  auto checked =
      CheckQuoteGenericParamsSplices(ctx, type_ctx, method.generic_params, env);
  if (!checked.ok) {
    return checked;
  }
  checked = CheckQuoteReceiverSplices(ctx, type_ctx, method.receiver, env);
  if (!checked.ok) {
    return checked;
  }
  for (const auto& param : method.params) {
    checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
    if (!checked.ok) {
      return checked;
    }
  }
  checked = CheckQuoteTypeSplices(ctx, type_ctx, method.return_type_opt, env);
  if (!checked.ok) {
    return checked;
  }
  checked = CheckQuoteContractClauseSplices(ctx, type_ctx, method.contract, env);
  if (!checked.ok) {
    return checked;
  }
  return CheckQuoteBlockSplices(ctx, type_ctx, *method.body, env);
}

static CheckResult CheckQuoteAssociatedTypeDeclSplices(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::AssociatedTypeDecl& assoc,
    const TypeEnv& env) {
  return CheckQuoteTypeSplices(ctx, type_ctx, assoc.default_type, env);
}

static CheckResult CheckQuoteRecordMemberSplices(const ScopeContext& ctx,
                                                 const StmtTypeContext& type_ctx,
                                                 const ast::RecordMember& member,
                                                 const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::FieldDecl>) {
          return CheckQuoteFieldDeclSplices(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::MethodDecl>) {
          return CheckQuoteMethodDeclSplices(ctx, type_ctx, node, env);
        } else {
          return CheckQuoteAssociatedTypeDeclSplices(ctx, type_ctx, node, env);
        }
      },
      member);
}

static CheckResult CheckQuoteStateMemberSplices(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const ast::StateMember& member,
                                                const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StateFieldDecl>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::StateMethodDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteReceiverSplices(ctx, type_ctx, node.receiver, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& param : node.params) {
            checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
            if (!checked.ok) {
              return checked;
            }
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.return_type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteContractClauseSplices(ctx, type_ctx, node.contract, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else {
          for (const auto& param : node.params) {
            auto checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        }
      },
      member);
}

static CheckResult CheckQuoteClassItemSplices(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::ClassItem& item,
                                              const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ClassFieldDecl>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else if constexpr (std::is_same_v<T, ast::ClassMethodDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteReceiverSplices(ctx, type_ctx, node.receiver, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& param : node.params) {
            checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
            if (!checked.ok) {
              return checked;
            }
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.return_type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteContractClauseSplices(ctx, type_ctx, node.contract, env);
          if (!checked.ok) {
            return checked;
          }
          if (!node.body_opt) {
            return OkCheckResult();
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body_opt, env);
        } else if constexpr (std::is_same_v<T, ast::AssociatedTypeDecl>) {
          return CheckQuoteAssociatedTypeDeclSplices(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::AbstractFieldDecl>) {
          return CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
        } else {
          for (const auto& field : node.fields) {
            auto checked = CheckQuoteTypeSplices(ctx, type_ctx, field.type, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        }
      },
      item);
}

static CheckResult CheckQuoteKeyPathSplices(const ScopeContext& ctx,
                                            const StmtTypeContext& type_ctx,
                                            const ast::KeyPathExpr& key_path,
                                            const TypeEnv& env) {
  for (const auto& seg : key_path.segs) {
    if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      auto checked = CheckQuoteExprSplices(ctx, type_ctx, index->expr, env);
      if (!checked.ok) {
        return checked;
      }
    }
  }
  return OkCheckResult();
}

static CheckResult CheckQuoteStmtSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::Stmt& stmt,
                                         const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.binding.pat, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.binding.type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.binding.pat, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.binding.type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt carries source/alias splices but no runtime exprs.
          auto checked =
              CheckSplicedIdentifier(ctx, type_ctx, node.source_splice_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckSplicedIdentifier(ctx, type_ctx, node.alias_splice_opt,
                                        env);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.place, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          auto checked = CheckQuoteExprSplices(ctx, type_ctx, node.opts_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckSplicedIdentifier(ctx, type_ctx, node.alias_splice_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return CheckQuoteExprSplices(ctx, type_ctx, node.value_opt, env);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::ComptimeStmt>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          for (const auto& path : node.paths) {
            auto checked = CheckQuoteKeyPathSplices(ctx, type_ctx, path, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else {
          return OkCheckResult();
        }
      },
      stmt);
}

static CheckResult CheckQuoteItemSplices(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ASTItem& item,
                                         const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> CheckResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl> ||
                      std::is_same_v<T, ast::ImportDecl> ||
                      std::is_same_v<T, ast::ErrorItem>) {
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& extern_item : node.items) {
            if (const auto* proc = std::get_if<ast::ExternProcDecl>(&extern_item)) {
              auto checked = CheckQuoteGenericParamsSplices(
                  ctx, type_ctx, proc->generic_params, env);
              if (!checked.ok) {
                return checked;
              }
              checked = CheckQuoteWhereClauseSplices(
                  ctx, type_ctx, proc->where_clause, env);
              if (!checked.ok) {
                return checked;
              }
              for (const auto& param : proc->params) {
                checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
                if (!checked.ok) {
                  return checked;
                }
              }
              checked =
                  CheckQuoteTypeSplices(ctx, type_ctx, proc->return_type_opt, env);
              if (!checked.ok) {
                return checked;
              }
              checked =
                  CheckQuoteContractClauseSplices(ctx, type_ctx, proc->contract, env);
              if (!checked.ok) {
                return checked;
              }
              if (proc->foreign_contracts_opt.has_value()) {
                for (const auto& clause : *proc->foreign_contracts_opt) {
                  for (const auto& predicate : clause.predicates) {
                    checked =
                        CheckQuoteExprSplices(ctx, type_ctx, predicate, env);
                    if (!checked.ok) {
                      return checked;
                    }
                  }
                }
              }
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          auto checked =
              CheckQuotePatternSplices(ctx, type_ctx, node.binding.pat, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.binding.type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteExprSplices(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& param : node.params) {
            checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
            if (!checked.ok) {
              return checked;
            }
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.return_type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteWhereClauseSplices(
                  ctx, type_ctx, node.predicate_clause_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteContractClauseSplices(ctx, type_ctx, node.contract, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& param : node.params) {
            checked = CheckQuoteParamSplices(ctx, type_ctx, param, env);
            if (!checked.ok) {
              return checked;
            }
          }
          checked =
              CheckQuoteTypeSplices(ctx, type_ctx, node.return_type_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteContractClauseSplices(ctx, type_ctx, node.contract, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteWhereClauseSplices(
                  ctx, type_ctx, node.predicate_clause_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeInvariantSplices(
                  ctx, type_ctx, node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& member : node.members) {
            checked = CheckQuoteRecordMemberSplices(ctx, type_ctx, member, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteWhereClauseSplices(
                  ctx, type_ctx, node.predicate_clause_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeInvariantSplices(
                  ctx, type_ctx, node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& variant : node.variants) {
            if (!variant.payload_opt.has_value()) {
              continue;
            }
            checked = std::visit(
                [&](const auto& payload) -> CheckResult {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                    for (const auto& elem : payload.elements) {
                      auto elem_checked =
                          CheckQuoteTypeSplices(ctx, type_ctx, elem, env);
                      if (!elem_checked.ok) {
                        return elem_checked;
                      }
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      auto field_checked = CheckQuoteFieldDeclSplices(
                          ctx, type_ctx, field, env);
                      if (!field_checked.ok) {
                        return field_checked;
                      }
                    }
                  }
                  return OkCheckResult();
                },
                *variant.payload_opt);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteWhereClauseSplices(ctx, type_ctx,
                                           node.predicate_clause_opt, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteTypeInvariantSplices(ctx, type_ctx,
                                             node.invariant_opt, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              checked = CheckQuoteStateMemberSplices(ctx, type_ctx, member, env);
              if (!checked.ok) {
                return checked;
              }
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked =
              CheckQuoteWhereClauseSplices(
                  ctx, type_ctx, node.predicate_clause_opt, env);
          if (!checked.ok) {
            return checked;
          }
          for (const auto& class_item : node.items) {
            checked = CheckQuoteClassItemSplices(ctx, type_ctx, class_item, env);
            if (!checked.ok) {
              return checked;
            }
          }
          return OkCheckResult();
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          auto checked =
              CheckQuoteGenericParamsSplices(ctx, type_ctx, node.generic_params, env);
          if (!checked.ok) {
            return checked;
          }
          checked = CheckQuoteTypeSplices(ctx, type_ctx, node.type, env);
          if (!checked.ok) {
            return checked;
          }
          return CheckQuoteWhereClauseSplices(ctx, type_ctx,
                                              node.predicate_clause_opt, env);
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          return CheckQuoteBlockSplices(ctx, type_ctx, *node.body, env);
        } else {
          return OkCheckResult();
        }
      },
      item);
}

static CheckResult ValidateQuoteSplicesStatic(const ScopeContext& ctx,
                                              const StmtTypeContext& type_ctx,
                                              const ast::QuoteExpr& quote,
                                              ast::QuoteKind kind,
                                              const TypeEnv& env,
                                              const core::Span& quote_span) {
  ast::Parser parser = MakeQuoteTokenParser(quote.tokens);
  switch (kind) {
    case ast::QuoteKind::Expr: {
      auto parsed = ast::ParseExpr(parser);
      if (!parsed.elem || !ast::AtEof(parsed.parser)) {
        return MakeDiagCheckResult("E-CTE-0220", quote_span);
      }
      return CheckQuoteExprSplices(ctx, type_ctx, parsed.elem, env);
    }
    case ast::QuoteKind::Stmt: {
      auto parsed = ast::ParseStmt(parser);
      if (!IsQuotedStatementForm(parsed.elem) || !ast::AtEof(parsed.parser)) {
        return MakeDiagCheckResult("E-CTE-0220", quote_span);
      }
      return CheckQuoteStmtSplices(ctx, type_ctx, parsed.elem, env);
    }
    case ast::QuoteKind::Unspecified:
      return MakeDiagCheckResult("E-CTE-0220", quote_span);
    case ast::QuoteKind::Item: {
      auto parsed = ast::ParseItem(parser);
      if (!ast::AtEof(parsed.parser)) {
        return MakeDiagCheckResult("E-CTE-0220", quote_span);
      }
      return CheckQuoteItemSplices(ctx, type_ctx, parsed.item, env);
    }
    case ast::QuoteKind::Type: {
      auto parsed = ast::ParseType(parser);
      if (!parsed.elem || !ast::AtEof(parsed.parser)) {
        return MakeDiagCheckResult("E-CTE-0220", quote_span);
      }
      return CheckQuoteTypeSplices(ctx, type_ctx, parsed.elem, env);
    }
    case ast::QuoteKind::Pattern: {
      auto parsed = ast::ParsePattern(parser);
      if (!parsed.elem || !ast::AtEof(parsed.parser)) {
        return MakeDiagCheckResult("E-CTE-0220", quote_span);
      }
      return CheckQuotePatternSplices(ctx, type_ctx, parsed.elem, env);
    }
  }
  return MakeDiagCheckResult("E-CTE-0220", quote_span);
}

static std::optional<std::string_view> CtForbiddenTypeDiag(
    const ScopeContext& ctx,
    const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }

  const auto caps =
      InferCapabilitiesFromType(ctx, ctx.current_module, type);
  if (!caps.IsEmpty()) {
    return "E-CTE-0012";
  }

  const auto stripped = StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (std::holds_alternative<TypeModalState>(stripped->node) ||
      std::holds_alternative<TypeDynamic>(stripped->node) ||
      std::holds_alternative<TypePtr>(stripped->node) ||
      std::holds_alternative<TypeRawPtr>(stripped->node) ||
      std::holds_alternative<TypeFunc>(stripped->node)) {
    return "E-CTE-0011";
  }
  return std::nullopt;
}

void EmitDeprecatedBindingReferenceWarning(
    const TypeBinding& binding,
    const StmtTypeContext& type_ctx,
    const std::optional<core::Span>& span) {
  if (!binding.deprecated || !type_ctx.diags) {
    return;
  }
  auto diag = core::MakeDiagnosticById("W-CNF-0601", span);
  if (!diag.has_value()) {
    return;
  }
  if (binding.deprecated_message.has_value() &&
      !binding.deprecated_message->empty()) {
    core::SubDiagnostic note;
    note.kind = core::SubDiagnosticKind::Note;
    note.message = "deprecated message: " + *binding.deprecated_message;
    diag->children.push_back(std::move(note));
  }
  core::Emit(*type_ctx.diags, *diag);
}

// Check if an expression only uses bindings from the environment
bool ExprUsesOnlyEnvBindings(const ast::ExprPtr& e, const TypeEnv& env) {
  if (!e) {
    return true;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&e->node)) {
    return BindOf(env, ident->name).has_value();
  }
  if (std::holds_alternative<ast::ResultExpr>(e->node)) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprUsesOnlyEnvBindings(node.lhs, env) &&
                 ExprUsesOnlyEnvBindings(node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprUsesOnlyEnvBindings(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprUsesOnlyEnvBindings(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprUsesOnlyEnvBindings(node.base, env) &&
                 ExprUsesOnlyEnvBindings(node.index, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (!ExprUsesOnlyEnvBindings(node.callee, env)) {
            return false;
          }
          for (const auto& arg : node.args) {
            if (!ExprUsesOnlyEnvBindings(arg.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (!ExprUsesOnlyEnvBindings(node.receiver, env)) {
            return false;
          }
          for (const auto& arg : node.args) {
            if (!ExprUsesOnlyEnvBindings(arg.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprUsesOnlyEnvBindings(node.lhs, env) &&
                 ExprUsesOnlyEnvBindings(node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprUsesOnlyEnvBindings(node.place, env);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprUsesOnlyEnvBindings(node.place, env);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (!ExprUsesOnlyEnvBindings(elem, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool uses_only_env_bindings = true;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (!uses_only_env_bindings) {
              return;
            }
            if (!ExprUsesOnlyEnvBindings(elem, env)) {
              uses_only_env_bindings = false;
            }
          });
          return uses_only_env_bindings;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env) &&
                 ExprUsesOnlyEnvBindings(node.count, env);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (!ExprUsesOnlyEnvBindings(field.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return true;
          }
          if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
            const auto& paren = std::get<ast::EnumPayloadParen>(*node.payload_opt);
            for (const auto& elem : paren.elements) {
              if (!ExprUsesOnlyEnvBindings(elem, env)) {
                return false;
              }
            }
            return true;
          }
          const auto& brace = std::get<ast::EnumPayloadBrace>(*node.payload_opt);
          for (const auto& field : brace.fields) {
            if (!ExprUsesOnlyEnvBindings(field.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprUsesOnlyEnvBindings(node.cond, env) &&
                 ExprUsesOnlyEnvBindings(node.then_expr, env) &&
                 ExprUsesOnlyEnvBindings(node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (!ExprUsesOnlyEnvBindings(node.scrutinee, env)) {
            return false;
          }
          for (const auto& arm : node.cases) {
            if (!ExprUsesOnlyEnvBindings(arm.body, env)) {
              return false;
            }
          }
          if (node.else_expr) {
            return ExprUsesOnlyEnvBindings(node.else_expr, env);
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprUsesOnlyEnvBindings(node.scrutinee, env) &&
                 ExprUsesOnlyEnvBindings(node.then_expr, env) &&
                 ExprUsesOnlyEnvBindings(node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprUsesOnlyEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprUsesOnlyEnvBindings(node.expr, env);
        } else {
          return true;
        }
      },
      e->node);
}

bool EntryExprHasCapabilityOp(const ast::ExprPtr& e);
bool EntryExprHasSideEffectOp(const ast::ExprPtr& e);

static bool EntryAnyArgHasCapability(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (EntryExprHasCapabilityOp(arg.value)) {
      return true;
    }
  }
  return false;
}

static bool EntryAnyArgHasSideEffect(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (EntryExprHasSideEffectOp(arg.value)) {
      return true;
    }
  }
  return false;
}

static bool IsCapabilityMethodName(std::string_view name) {
  return LookupContextMethodSig(name).has_value();
}

bool EntryExprHasCapabilityOp(const ast::ExprPtr& e) {
  if (!e) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          const auto* recv_ident =
              node.receiver ? std::get_if<ast::IdentifierExpr>(&node.receiver->node) : nullptr;
          const auto* recv_path =
              node.receiver ? std::get_if<ast::PathExpr>(&node.receiver->node) : nullptr;
          const bool recv_is_ctx =
              (recv_ident && recv_ident->name == "ctx") ||
              (recv_path && recv_path->path.empty() && recv_path->name == "ctx");
          if (recv_is_ctx && IsCapabilityMethodName(node.name)) {
            return true;
          }
          return EntryExprHasCapabilityOp(node.receiver) ||
                 EntryAnyArgHasCapability(node.args);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return EntryExprHasCapabilityOp(node.lhs) ||
                 EntryExprHasCapabilityOp(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base) ||
                 EntryExprHasCapabilityOp(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return EntryExprHasCapabilityOp(node.callee) ||
                 EntryAnyArgHasCapability(node.args);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return EntryExprHasCapabilityOp(node.cond) ||
                 EntryExprHasCapabilityOp(node.then_expr) ||
                 EntryExprHasCapabilityOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (EntryExprHasCapabilityOp(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool has_capability_op = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (has_capability_op) {
              return;
            }
            if (EntryExprHasCapabilityOp(elem)) {
              has_capability_op = true;
            }
          });
          return has_capability_op;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return EntryExprHasCapabilityOp(node.value) ||
                 EntryExprHasCapabilityOp(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (EntryExprHasCapabilityOp(field.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (EntryExprHasCapabilityOp(node.scrutinee)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (EntryExprHasCapabilityOp(arm.body)) {
              return true;
            }
          }
          return EntryExprHasCapabilityOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return EntryExprHasCapabilityOp(node.scrutinee) ||
                 EntryExprHasCapabilityOp(node.then_expr) ||
                 EntryExprHasCapabilityOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return EntryExprHasCapabilityOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return EntryExprHasCapabilityOp(node.handle);
        } else {
          return false;
        }
      },
      e->node);
}

bool EntryExprHasSideEffectOp(const ast::ExprPtr& e) {
  if (!e) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::SyncExpr> ||
                      std::is_same_v<T, ast::YieldExpr> ||
                      std::is_same_v<T, ast::YieldFromExpr> ||
                      std::is_same_v<T, ast::SpawnExpr> ||
                      std::is_same_v<T, ast::WaitExpr> ||
                      std::is_same_v<T, ast::FenceExpr> ||
                      std::is_same_v<T, ast::ParallelExpr> ||
                      std::is_same_v<T, ast::DispatchExpr> ||
                      std::is_same_v<T, ast::RaceExpr> ||
                      std::is_same_v<T, ast::AllExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return EntryExprHasSideEffectOp(node.lhs) ||
                 EntryExprHasSideEffectOp(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return EntryExprHasSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return EntryExprHasSideEffectOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return EntryExprHasSideEffectOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return EntryExprHasSideEffectOp(node.base) ||
                 EntryExprHasSideEffectOp(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return EntryExprHasSideEffectOp(node.callee) ||
                 EntryAnyArgHasSideEffect(node.args);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return EntryExprHasSideEffectOp(node.receiver) ||
                 EntryAnyArgHasSideEffect(node.args);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return EntryExprHasSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return EntryExprHasSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return EntryExprHasSideEffectOp(node.cond) ||
                 EntryExprHasSideEffectOp(node.then_expr) ||
                 EntryExprHasSideEffectOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (EntryExprHasSideEffectOp(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool has_side_effect = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (has_side_effect) {
              return;
            }
            if (EntryExprHasSideEffectOp(elem)) {
              has_side_effect = true;
            }
          });
          return has_side_effect;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return EntryExprHasSideEffectOp(node.value) ||
                 EntryExprHasSideEffectOp(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (EntryExprHasSideEffectOp(field.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (EntryExprHasSideEffectOp(node.scrutinee)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (EntryExprHasSideEffectOp(arm.body)) {
              return true;
            }
          }
          return EntryExprHasSideEffectOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return EntryExprHasSideEffectOp(node.scrutinee) ||
                 EntryExprHasSideEffectOp(node.then_expr) ||
                 EntryExprHasSideEffectOp(node.else_expr);
        } else {
          return false;
        }
      },
      e->node);
}

// Type a literal expression (wrapper to avoid name conflict with public function)
ExprTypeResult TypeLiteralExprLocal(const ScopeContext& ctx,
                                     const ast::LiteralExpr& lit) {
  return TypeLiteralExpr(ctx, lit);
}

static bool SharedAccessModeSufficient(ast::KeyMode held,
                                       ast::KeyMode required) {
  return held == ast::KeyMode::Write || held == required;
}

static std::optional<ast::KeyMode> CoveringSharedAccessMode(
    const StmtTypeContext& type_ctx,
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

static std::optional<std::string_view> CheckSharedAccessRequirement(
    const StmtTypeContext& type_ctx,
    const ast::ExprPtr& expr,
    const TypeRef& type) {
  if (!expr || !type || type_ctx.suppress_shared_access_check ||
      !type_ctx.shared_access_mode.has_value() ||
      PermOfType(type) != Permission::Shared || !IsPlaceExpression(expr)) {
    return std::nullopt;
  }

  const auto built = BuildKeyPath(expr);
  if (!built.success) {
    return "E-CON-0034";
  }

  const auto covering_mode = CoveringSharedAccessMode(type_ctx, built.path);
  if (!covering_mode.has_value()) {
    return std::nullopt;
  }

  if (SharedAccessModeSufficient(*covering_mode, *type_ctx.shared_access_mode)) {
    return std::nullopt;
  }

  return "E-CON-0005";
}

// Main expression typing dispatcher
static ExprTypeResult TypeExprImpl(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::ExprPtr& e,
                                   const TypeEnv& env) {
  SpecDefsTypeExpr();
  ExprTypeResult result;

  if (!e) {
    return result;
  }

  // Create callback functions for recursive typing
  expr::TypeExprFn type_expr_fn = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  expr::TypeIdentFn type_ident_fn = [&](std::string_view name) -> ExprTypeResult {
    return expr::TypeIdentifierExprImpl(ctx, ast::IdentifierExpr{std::string(name)},
                                        env);
  };
  expr::PlaceTypeFn type_place_fn = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };

  result = std::visit(
      [&](const auto& node) -> ExprTypeResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::ErrorExpr>) {
          return expr::TypeErrorExprImpl(ctx, node);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          if (type_ctx.in_speculative &&
              HasMemoryOrderAttribute(node.attrs)) {
            ExprTypeResult r;
            r.diag_id = "E-CON-0096";
            return r;
          }

          const auto attr_validation =
              ValidateAttributes(node.attrs, AttributeTarget::Expression);
          if (!attr_validation.ok) {
            ExprTypeResult r;
            r.diag_id = attr_validation.diag_id;
            return r;
          }
          if (CountMemoryOrderAttributes(node.attrs) > 1) {
            ExprTypeResult r;
            r.diag_id = "E-MOD-2450";
            return r;
          }

          StmtTypeContext inner_ctx = type_ctx;
          inner_ctx.contract_dynamic =
              e ? ComputeExprDynamicContext(*e, type_ctx.contract_dynamic)
                : type_ctx.contract_dynamic;
          TypeEnv inner_env = env;
          if (node.expr && std::holds_alternative<ast::ComptimeExpr>(node.expr->node)) {
            inner_env = ExtendComptimeEnv(env, &node.attrs);
          }
          auto inner = TypeExpr(ctx, inner_ctx, node.expr, inner_env);
          if (!inner.ok) {
            return inner;
          }
          if (const auto diag = ValidateMemoryOrderAttributePlacement(
                  ctx, inner_ctx, node.attrs, node.expr, inner_env)) {
            ExprTypeResult r;
            r.diag_id = *diag;
            return r;
          }
          return inner;
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return TypeLiteralExprLocal(ctx, node);
        } else if constexpr (std::is_same_v<T, ast::PtrNullExpr>) {
          SPEC_RULE("Syn-PtrNull-Err");
          ExprTypeResult r;
          r.diag_id = "PtrNull-Infer-Err";
          return r;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          auto r = expr::TypeIdentifierExprImpl(ctx, node, env);
          if (r.ok) {
            const auto binding = BindOf(env, node.name);
            if (binding.has_value()) {
              EmitDeprecatedBindingReferenceWarning(
                  *binding, type_ctx,
                  e ? std::optional<core::Span>(e->span) : std::nullopt);
            }
          }
          return r;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return expr::TypePathExprImpl(ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return expr::TypeFieldAccessExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return expr::TypeTupleAccessExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return expr::TypeIndexAccessExpr(ctx, type_ctx, node, env, type_expr_fn,
                                           type_place_fn, type_ident_fn);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return expr::TypeBinaryExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return expr::TypeUnaryExprImpl(ctx, type_ctx, node, env, e->span);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return expr::TypeCastExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return expr::TypeIfExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return TypeIfIsExpr(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          return TypeIfCaseExpr(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return expr::TypeRangeExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return expr::TypeAddressOfExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return expr::TypeDerefExprImpl(ctx, type_ctx, node, env, e->span);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return expr::TypeMoveExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return expr::TypePropagateExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          if (type_ctx.contract_phase != ContractPhase::Postcondition) {
            ExprTypeResult r;
            r.diag_id = "E-SEM-2806";
            return r;
          }
          ExprTypeResult r;
          r.ok = true;
          r.type = type_ctx.return_type ? type_ctx.return_type : MakeTypePrim("()");
          return r;
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          if (type_ctx.contract_phase != ContractPhase::Postcondition) {
            ExprTypeResult r;
            r.diag_id = "E-SEM-2852";
            return r;
          }
          if (!node.expr) {
            return ExprTypeResult{};
          }
          if (!ExprUsesOnlyEnvBindings(node.expr, env)) {
            ExprTypeResult r;
            r.diag_id = "E-SEM-2852";
            return r;
          }
          if (EntryExprHasCapabilityOp(node.expr)) {
            ExprTypeResult r;
            r.diag_id = "Entry-NoCapability-Err";
            return r;
          }
          if (EntryExprHasSideEffectOp(node.expr)) {
            ExprTypeResult r;
            r.diag_id = "Entry-SideEffect-Err";
            return r;
          }
          const auto typed = TypeExpr(ctx, type_ctx, node.expr, env);
          if (!typed.ok) {
            ExprTypeResult r;
            r.diag_id = typed.diag_id;
            return r;
          }
          if (!BitcopyType(ctx, typed.type) && !CloneType(ctx, typed.type)) {
            ExprTypeResult r;
            r.diag_id = "E-SEM-2805";
            return r;
          }
          ExprTypeResult r;
          r.ok = true;
          r.type = typed.type;
          return r;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          TypePath target_path;
          if (const auto* path = std::get_if<ast::TypePath>(&node.target)) {
            target_path = *path;
          }
          if (!target_path.empty() && IsSystemTypePath(target_path) &&
              !IsInUnsafeSpan(ctx, e->span)) {
            ExprTypeResult r;
            r.diag_id = "E-CON-0020";
            return r;
          }
          return expr::TypeRecordExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          return expr::TypeEnumLiteralExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          return expr::TypeTupleExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          return expr::TypeArrayExprImpl(ctx, node, type_expr_fn);
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return expr::TypeArrayRepeatExprImpl(ctx, node, type_expr_fn);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return expr::TypeSizeofExprImpl(ctx, node);
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return expr::TypeAlignofExprImpl(ctx, node);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return expr::TypeCallExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          return expr::TypeCallTypeArgsExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return expr::TypeMethodCallExprImpl(ctx, type_ctx, node, env, e->span);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return expr::TypeClosureExpr(node, ctx, type_ctx, env, type_expr_fn,
                                       type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return expr::TypeBlockExprImpl(ctx, type_ctx, node, env,
                                         type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return expr::TypeUnsafeBlockExprImpl(ctx, type_ctx, node, env,
                                               type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::TypeLiteralExpr>) {
          ExprTypeResult r;
          if (!IsComptimeEnv(env)) {
            r.diag_id = "E-CTE-0411";
            return r;
          }
          SPEC_RULE_AT("T-TypeLiteral", e ? e->span : core::Span{});
          r.ok = true;
          r.type = TypeMetaType();
          return r;
        } else if constexpr (std::is_same_v<T, ast::QuoteExpr>) {
          ExprTypeResult r;
          if (!IsComptimeEnv(env)) {
            r.diag_id = "E-CTE-0221";
            return r;
          }
          auto resolved_kind = ResolveQuoteKindStatic(node, nullptr);
          if (!resolved_kind.has_value()) {
            r.diag_id = "E-CTE-0220";
            return r;
          }
          const auto splice_check =
              ValidateQuoteSplicesStatic(ctx, type_ctx, node, *resolved_kind, env,
                                         e ? e->span : core::Span{});
          if (!splice_check.ok) {
            r.diag_id = splice_check.diag_id;
            r.diag_detail = splice_check.diag_detail;
            r.diag_span = splice_check.diag_span;
            return r;
          }
          r.ok = true;
          r.type = QuoteKindType(*resolved_kind);
          return r;
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          StmtTypeContext inner_ctx = type_ctx;
          inner_ctx.contract_dynamic =
              e ? ComputeExprDynamicContext(*e, type_ctx.contract_dynamic)
                : type_ctx.contract_dynamic;
          const TypeEnv comptime_env =
              ExtendComptimeEnv(env, &ast::AttrListOf(node.attrs_opt));
          auto typed = TypeExpr(ctx, inner_ctx, node.body, comptime_env);
          if (!typed.ok) {
            return typed;
          }
          if (const auto diag =
                  ComptimeTypeAvailabilityDiag(ctx, typed.type,
                                               "E-CTE-0021")) {
            typed.ok = false;
            typed.diag_id = *diag;
            if (!typed.diag_span.has_value()) {
              typed.diag_span =
                  node.body ? std::optional<core::Span>(node.body->span)
                            : std::optional<core::Span>(e->span);
            }
          }
          if (typed.ok) {
            SPEC_RULE_AT("T-CtExpr", e ? e->span : core::Span{});
          }
          return typed;
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          const TypeEnv comptime_env = ExtendComptimeEnv(env, nullptr);
          ast::IfExpr if_expr;
          if_expr.cond = node.cond;
          if_expr.then_expr = std::make_shared<ast::Expr>();
          if_expr.then_expr->span =
              node.then_block ? node.then_block->span : e->span;
          if_expr.then_expr->node = ast::BlockExpr{node.then_block};
          if (node.else_block_opt) {
            if_expr.else_expr = std::make_shared<ast::Expr>();
            if_expr.else_expr->span = node.else_block_opt->span;
            if_expr.else_expr->node = ast::BlockExpr{node.else_block_opt};
          }
          auto typed =
              expr::TypeIfExprImpl(ctx, type_ctx, if_expr, comptime_env);
          if (typed.ok) {
            SPEC_RULE_AT("T-CtIf", e ? e->span : core::Span{});
          }
          return typed;
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          const TypeEnv comptime_env = ExtendComptimeEnv(env, nullptr);
          return TypeCtLoopIterExpr(ctx, type_ctx, node, comptime_env,
                                    type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return TypeLoopInfiniteExpr(ctx, type_ctx, node, env,
                                      type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return TypeLoopConditionalExpr(ctx, type_ctx, node, env,
                                         type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return TypeLoopIterExpr(ctx, type_ctx, node, env,
                                  type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return TypePipelineExpr(node, ctx, type_ctx, env);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return expr::TypeAllocExprImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return expr::TypeTransmuteExprImpl(ctx, type_ctx, node, env, e->span);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return expr::TypeYieldExpr(ctx, type_ctx, node, env,
                                     type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return expr::TypeYieldFromExpr(ctx, type_ctx, node, env,
                                         type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return expr::TypeSyncExpr(ctx, type_ctx, node, env,
                                    type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          return expr::TypeRaceExpr(ctx, type_ctx, node, env,
                                    type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          return expr::TypeAllExprImpl(ctx, type_ctx, node, env, type_expr_fn);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          SPEC_RULE("Expr-Unresolved-Err");
          ExprTypeResult r;
          r.diag_id = "ResolveExpr-Ident-Err";
          return r;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          SPEC_RULE("Expr-Unresolved-Err");
          ExprTypeResult r;
          r.diag_id = "ResolveExpr-Ident-Err";
          return r;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          return TypeParallelExpr(ctx, type_ctx, node, env,
                                  type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          return expr::TypeSpawnExpr(ctx, type_ctx, node, env,
                                     type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return expr::TypeWaitExpr(ctx, type_ctx, node, env,
                                    type_expr_fn, type_ident_fn, type_place_fn);
        } else if constexpr (std::is_same_v<T, ast::FenceExpr>) {
          ExprTypeResult r;
          if (type_ctx.in_speculative) {
            r.diag_id = "E-CON-0096";
            return r;
          }
          r.ok = true;
          r.type = MakeTypePrim("()");
          return r;
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          return expr::TypeDispatchExprImpl(ctx, type_ctx, node, env,
                                            type_expr_fn, type_ident_fn, type_place_fn);
        } else {
          return ExprTypeResult{};
        }
      },
      e->node);

  if (result.ok) {
    if (const auto diag_id =
            CheckSharedAccessRequirement(type_ctx, e, result.type)) {
      result.ok = false;
      result.diag_id = *diag_id;
      return result;
    }
  }

  return result;
}

// Main place typing dispatcher
static PlaceTypeResult TypePlaceImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::ExprPtr& e,
                                     const TypeEnv& env) {
  SpecDefsTypeExpr();
  PlaceTypeResult result;

  if (!e) {
    return result;
  }

  expr::PlaceTypeFn type_place_fn = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  expr::TypeExprFn type_expr_fn = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  expr::TypeIdentFn type_ident_fn = [&](std::string_view name) -> ExprTypeResult {
    return expr::TypeIdentifierExprImpl(ctx, ast::IdentifierExpr{std::string(name)},
                                        env);
  };

  result = std::visit(
      [&](const auto& node) -> PlaceTypeResult {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          auto r = expr::TypeIdentifierPlaceImpl(ctx, node, env);
          if (r.ok) {
            const auto binding = BindOf(env, node.name);
            if (binding.has_value()) {
              EmitDeprecatedBindingReferenceWarning(
                  *binding, type_ctx,
                  e ? std::optional<core::Span>(e->span) : std::nullopt);
            }
          }
          return r;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          if (type_ctx.in_speculative &&
              HasMemoryOrderAttribute(node.attrs)) {
            PlaceTypeResult r;
            r.diag_id = "E-CON-0096";
            return r;
          }

          const auto attr_validation =
              ValidateAttributes(node.attrs, AttributeTarget::Expression);
          if (!attr_validation.ok) {
            PlaceTypeResult r;
            r.diag_id = attr_validation.diag_id;
            return r;
          }
          if (CountMemoryOrderAttributes(node.attrs) > 1) {
            PlaceTypeResult r;
            r.diag_id = "E-MOD-2450";
            return r;
          }

          StmtTypeContext inner_ctx = type_ctx;
          inner_ctx.contract_dynamic =
              e ? ComputeExprDynamicContext(*e, type_ctx.contract_dynamic)
                : type_ctx.contract_dynamic;
          auto inner = TypePlace(ctx, inner_ctx, node.expr, env);
          if (!inner.ok) {
            return inner;
          }
          if (const auto diag = ValidateMemoryOrderAttributePlacement(
                  ctx, inner_ctx, node.attrs, node.expr, env)) {
            PlaceTypeResult r;
            r.diag_id = *diag;
            return r;
          }
          return inner;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return expr::TypeFieldAccessPlaceImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return expr::TypeTupleAccessPlaceImpl(ctx, type_ctx, node, env);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return expr::TypeDerefPlaceImpl(ctx, type_ctx, node, env, e->span);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return expr::TypeIndexAccessPlace(ctx, type_ctx, node, env, type_expr_fn,
                                            type_place_fn, type_ident_fn);
        } else {
          return PlaceTypeResult{};
        }
      },
      e->node);

  if (result.ok) {
    if (const auto diag_id =
            CheckSharedAccessRequirement(type_ctx, e, result.type)) {
      result.ok = false;
      result.diag_id = *diag_id;
      return result;
    }
  }

  if (result.ok && ctx.expr_types && e) {
    (*ctx.expr_types)[e.get()] = result.type;
  }

  return result;
}

}  // namespace

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

static const ast::TypeAliasDecl* LookupTypeAliasDeclLocal(
    const ScopeContext& ctx,
    const TypePath& path) {
  PathKey key;
  key.reserve(path.size());
  for (const auto& seg : path) {
    key.push_back(IdKeyOf(seg));
  }

  auto it = ctx.sigma.types.find(key);
  if (it != ctx.sigma.types.end()) {
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }
  return nullptr;
}

static AliasExpandResult ExpandTypeAliasApplyLocal(const ScopeContext& ctx,
                                                   const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDeclLocal(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

static AliasExpandResult NormalizeAliasTypeLocal(const ScopeContext& ctx,
                                                 const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
      const auto* path = AppliedTypePath(*out.type);
      const auto* args = AppliedTypeArgs(*out.type);
      if (!path || !args) {
        return out;
      }
      const auto expanded =
          ExpandTypeAliasApplyLocal(ctx, TypePathType{*path, *args});
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = expanded.type;
    out.expanded = true;
  }
  return out;
}

static CheckResult TryDynamicRefinementFallback(const ScopeContext& ctx,
                                                const StmtTypeContext& type_ctx,
                                                const ast::ExprPtr& e,
                                                const TypeRef& expected,
                                                const TypeEnv& env) {
  CheckResult result;
  bool dynamic_context = type_ctx.contract_dynamic;
  if (e) {
    dynamic_context = ComputeExprDynamicContext(*e, type_ctx.contract_dynamic);
  }
  if (!dynamic_context || !e || !expected) {
    return result;
  }

  const auto norm = NormalizeAliasTypeLocal(ctx, expected);
  if (!norm.ok || !norm.type) {
    result.diag_id = norm.diag_id;
    return result;
  }
  const auto* refine =
      std::get_if<::cursive::analysis::TypeRefine>(&norm.type->node);
  if (!refine) {
    return result;
  }

  const auto base_check = CheckExprAgainst(ctx, type_ctx, e, refine->base, env);
  if (!base_check.ok) {
    return base_check;
  }

  if (ctx.dynamic_refine_checks && e) {
    (*ctx.dynamic_refine_checks)[e.get()].push_back(norm.type);
  }
  result.ok = true;
  return result;
}

// =============================================================================
// Public API
// =============================================================================

ExprTypeResult TypeExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& e,
                        const TypeEnv& env) {
  SpecDefsTypeExpr();
  auto result = TypeExprImpl(ctx, type_ctx, e, env);
  if (result.ok) {
    if (ctx.expr_types && e) {
      (*ctx.expr_types)[e.get()] = result.type;
    }
    SPEC_RULE("Lift-Expr");
  }
  return result;
}

CheckResult CheckExprAgainst(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::ExprPtr& e,
                             const TypeRef& expected,
                             const TypeEnv& env) {
  SpecDefsTypeExpr();
  CheckResult result;

  if (!e || !expected) {
    return result;
  }

  if (const auto diag = CheckEscapingClosureSpawn(e, env, expected);
      diag.has_value()) {
    result.diag_id = *diag;
    result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
    return result;
  }

  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&e->node)) {
    if (type_ctx.in_speculative &&
        HasMemoryOrderAttribute(attributed->attrs)) {
      result.diag_id = "E-CON-0096";
      result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
      return result;
    }

    const auto attr_validation =
        ValidateAttributes(attributed->attrs, AttributeTarget::Expression);
    if (!attr_validation.ok) {
      result.diag_id = attr_validation.diag_id;
      result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
      return result;
    }
    if (CountMemoryOrderAttributes(attributed->attrs) > 1) {
      result.diag_id = "E-MOD-2450";
      result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
      return result;
    }

    StmtTypeContext inner_ctx = type_ctx;
    inner_ctx.contract_dynamic =
        ComputeExprDynamicContext(*e, type_ctx.contract_dynamic);

    const auto checked_inner =
        CheckExprAgainst(ctx, inner_ctx, attributed->expr, expected, env);
    if (!checked_inner.ok) {
      return checked_inner;
    }
    if (const auto diag = ValidateMemoryOrderAttributePlacement(
            ctx, inner_ctx, attributed->attrs, attributed->expr, env)) {
      result.diag_id = *diag;
      result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
      return result;
    }

    result.ok = true;
    if (ctx.expr_types && e) {
      (*ctx.expr_types)[e.get()] = expected;
    }
    return result;
  }

  if (const auto* if_expr = std::get_if<ast::IfExpr>(&e->node)) {
    result = expr::CheckIfExprImpl(ctx, type_ctx, *if_expr, expected, env);
    if (!result.ok) {
      const auto dynamic_fallback =
          TryDynamicRefinementFallback(ctx, type_ctx, e, expected, env);
      if (!dynamic_fallback.ok) {
        return result;
      }
      result = dynamic_fallback;
    }
    if (ctx.expr_types && e) {
      (*ctx.expr_types)[e.get()] = expected;
    }
    return result;
  }

  if (const auto* quote = std::get_if<ast::QuoteExpr>(&e->node)) {
    if (!IsComptimeEnv(env)) {
      result.diag_id = "E-CTE-0221";
      result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
      return result;
    }

    const auto expected_kind = ExpectedQuoteKind(expected);
    if (IsAstMetaType(expected) || expected_kind.has_value()) {
      auto resolved_kind = ResolveQuoteKindStatic(*quote, &expected);
      if (!resolved_kind.has_value()) {
        result.diag_id = "E-CTE-0220";
        result.diag_span = e ? std::optional<core::Span>(e->span) : std::nullopt;
        return result;
      }
      const auto splice_check =
          ValidateQuoteSplicesStatic(ctx, type_ctx, *quote, *resolved_kind, env,
                                     e ? e->span : core::Span{});
      if (!splice_check.ok) {
        result.diag_id = splice_check.diag_id;
        result.diag_detail = splice_check.diag_detail;
        result.diag_span = splice_check.diag_span;
        return result;
      }
      if (IsAstMetaType(expected) ||
          (expected_kind.has_value() && *expected_kind == *resolved_kind)) {
        result.ok = true;
        if (ctx.expr_types && e) {
          (*ctx.expr_types)[e.get()] = QuoteKindType(*resolved_kind);
        }
        return result;
      }
    }
  }

  if (const auto* enum_literal = std::get_if<ast::EnumLiteralExpr>(&e->node)) {
    const auto enum_check = expr::CheckEnumLiteralExprAgainstImpl(
        ctx, type_ctx, *enum_literal, expected, env);
    if (enum_check.ok) {
      result.ok = true;
      if (ctx.expr_types && e) {
        (*ctx.expr_types)[e.get()] = expected;
      }
      return result;
    }
    if (enum_check.diag_id.has_value()) {
      result.diag_id = enum_check.diag_id;
      result.diag_detail = enum_check.diag_detail;
      result.diag_span = enum_check.diag_span;
      return result;
    }
  }

  expr::TypeExprFn type_expr_fn = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  expr::TypeIdentFn type_ident_fn = [&](std::string_view name) -> ExprTypeResult {
    return expr::TypeIdentifierExprImpl(ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  expr::PlaceTypeFn type_place_fn = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  auto if_case_check = [&](const ast::IfCaseExpr& match,
                         const TypeRef& expected_type) -> CheckResult {
    return CheckIfCaseExpr(ctx, type_ctx, match, env, expected_type);
  };

  const auto check =
      CheckExpr(ctx, e, expected, type_expr_fn, type_place_fn, type_ident_fn, if_case_check);
  if (!check.ok) {
    const auto dynamic_fallback =
        TryDynamicRefinementFallback(ctx, type_ctx, e, expected, env);
    if (!dynamic_fallback.ok) {
      result.diag_id = dynamic_fallback.diag_id.has_value()
                           ? dynamic_fallback.diag_id
                           : check.diag_id;
      result.diag_detail = dynamic_fallback.diag_detail.empty()
                               ? check.diag_detail
                               : dynamic_fallback.diag_detail;
      result.diag_span = dynamic_fallback.diag_span.has_value()
                             ? dynamic_fallback.diag_span
                             : check.diag_span;
      return result;
    }
    result.ok = true;
    if (ctx.expr_types && e) {
      (*ctx.expr_types)[e.get()] = expected;
    }
    return result;
  }

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&e->node)) {
    const auto binding = BindOf(env, ident->name);
    if (binding.has_value()) {
      EmitDeprecatedBindingReferenceWarning(*binding, type_ctx,
                                            std::optional<core::Span>(e->span));
    }
  }

  result.ok = true;
  if (ctx.expr_types && e) {
    (*ctx.expr_types)[e.get()] = expected;
  }
  return result;
}

CheckResult CheckPlaceAgainst(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::ExprPtr& e,
                              const TypeRef& expected,
                              const TypeEnv& env) {
  SpecDefsTypeExpr();
  CheckResult result;

  if (!e || !expected) {
    return result;
  }

  const auto place = TypePlace(ctx, type_ctx, e, env);
  if (!place.ok) {
    result.diag_id = place.diag_id;
    return result;
  }

  const auto sub = Subtyping(ctx, place.type, expected);
  if (!sub.ok) {
    result.diag_id = sub.diag_id;
    return result;
  }
  if (!sub.subtype) {
    return result;
  }

  SPEC_RULE("Place-Check");
  result.ok = true;
  return result;
}

PlaceTypeResult TypePlace(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::ExprPtr& e,
                          const TypeEnv& env) {
  return TypePlaceImpl(ctx, type_ctx, e, env);
}

// Individual expression form handlers (public wrappers)
ExprTypeResult TypeIdentifierExpr(const ScopeContext& ctx,
                                  const ast::IdentifierExpr& e,
                                  const TypeEnv& env) {
  return expr::TypeIdentifierExprImpl(ctx, e, env);
}

ExprTypeResult TypePathExpr(const ScopeContext& ctx,
                            const ast::PathExpr& e,
                            const TypeEnv& env) {
  return expr::TypePathExprImpl(ctx, e, env);
}

ExprTypeResult TypeFieldAccessExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::FieldAccessExpr& e,
                                   const TypeEnv& env) {
  return expr::TypeFieldAccessExprImpl(ctx, type_ctx, e, env);
}

PlaceTypeResult TypeFieldAccessPlace(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::FieldAccessExpr& e,
                                     const TypeEnv& env) {
  return expr::TypeFieldAccessPlaceImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeTupleAccessExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::TupleAccessExpr& e,
                                   const TypeEnv& env) {
  return expr::TypeTupleAccessExprImpl(ctx, type_ctx, e, env);
}

PlaceTypeResult TypeTupleAccessPlace(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::TupleAccessExpr& e,
                                     const TypeEnv& env) {
  return expr::TypeTupleAccessPlaceImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeBinaryExpr(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::BinaryExpr& e,
                              const TypeEnv& env) {
  return expr::TypeBinaryExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeUnaryExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::UnaryExpr& e,
                             const TypeEnv& env,
                             const core::Span& span) {
  return expr::TypeUnaryExprImpl(ctx, type_ctx, e, env, span);
}

ExprTypeResult TypeCastExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::CastExpr& e,
                            const TypeEnv& env) {
  return expr::TypeCastExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeIfExpr(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::IfExpr& e,
                          const TypeEnv& env) {
  return expr::TypeIfExprImpl(ctx, type_ctx, e, env);
}

CheckResult CheckIfExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::IfExpr& e,
                        const TypeRef& expected,
                        const TypeEnv& env) {
  return expr::CheckIfExprImpl(ctx, type_ctx, e, expected, env);
}

ExprTypeResult TypeRangeExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::RangeExpr& e,
                             const TypeEnv& env) {
  return expr::TypeRangeExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeAddressOfExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AddressOfExpr& e,
                                 const TypeEnv& env) {
  return expr::TypeAddressOfExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeDerefExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::DerefExpr& e,
                             const TypeEnv& env) {
  return expr::TypeDerefExprImpl(ctx, type_ctx, e, env, core::Span{});
}

PlaceTypeResult TypeDerefPlace(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::DerefExpr& e,
                               const TypeEnv& env) {
  return expr::TypeDerefPlaceImpl(ctx, type_ctx, e, env, core::Span{});
}

ExprTypeResult TypeMoveExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::MoveExpr& e,
                            const TypeEnv& env) {
  return expr::TypeMoveExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeAllocExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::AllocExpr& e,
                             const TypeEnv& env) {
  return expr::TypeAllocExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeTransmuteExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::TransmuteExpr& e,
                                 const TypeEnv& env) {
  return expr::TypeTransmuteExprImpl(ctx, type_ctx, e, env, core::Span{});
}

ExprTypeResult TypePropagateExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::PropagateExpr& e,
                                 const TypeEnv& env) {
  return expr::TypePropagateExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeRecordExpr(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::RecordExpr& e,
                              const TypeEnv& env) {
  return expr::TypeRecordExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeEnumLiteralExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::EnumLiteralExpr& e,
                                   const TypeEnv& env) {
  return expr::TypeEnumLiteralExprImpl(ctx, type_ctx, e, env);
}

ExprTypeResult TypeErrorExpr(const ScopeContext& ctx,
                             const ast::ErrorExpr& e) {
  return expr::TypeErrorExprImpl(ctx, e);
}

ExprTypeResult TypeMethodCallExpr(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::MethodCallExpr& e,
                                  const TypeEnv& env,
                                  const core::Span& span) {
  return expr::TypeMethodCallExprImpl(ctx, type_ctx, e, env, span);
}

bool IsInUnsafeSpan(const ScopeContext& ctx, const core::Span& span) {
  SpecDefsTypeExpr();
  if (span.file.empty()) {
    return false;
  }

  const auto file_it = ctx.sigma.unsafe_spans_by_file.find(span.file);
  if (file_it == ctx.sigma.unsafe_spans_by_file.end()) {
    return false;
  }

  const auto range = core::SpanRange(span);
  for (const auto& sp : file_it->second) {
    const auto sp_range = core::SpanRange(sp);
    if (range.first >= sp_range.first && range.second <= sp_range.second) {
      return true;
    }
  }

  return false;
}

bool IsPrimType(const TypeRef& type, std::string_view name) {
  if (!type) {
    return false;
  }
  // Strip permission qualifiers
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
  if (const auto* prim = std::get_if<TypePrim>(&cur->node)) {
    return prim->name == name;
  }
  return false;
}

// =============================================================================
// Permission and Purity helpers (from bootstrap type_expr.cpp)
// =============================================================================

std::optional<Permission> PermOfTypeOpt(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->perm;
  }
  return std::nullopt;
}

Permission PermOfType(const TypeRef& type) {
  if (const auto perm = PermOfTypeOpt(type); perm.has_value()) {
    return *perm;
  }
  return Permission::Const;
}

// StripPerm is defined in type_predicates.cpp (canonical location)

static ast::ClassPath AsClassPath(const TypePath& path) {
  ast::ClassPath out;
  out.reserve(path.size());
  for (const auto& seg : path) {
    out.push_back(seg);
  }
  return out;
}

bool IsCapabilityType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    const auto class_path = AsClassPath(dyn->path);
    return IsCapabilityClassPath(class_path) ||
           IsExecutionDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsContextTypePath(path->path) || IsSystemTypePath(path->path);
  }
  return false;
}

bool IsImpureType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  if (IsCapabilityType(type)) {
    return true;
  }
  return false;
}

bool ParamsPure(const ScopeContext& ctx,
                const std::vector<TypeFuncParam>& params) {
  (void)ctx;
  for (const auto& param : params) {
    if (IsImpureType(param.type)) {
      return false;
    }
  }
  return true;
}

bool ParamsPure(const ScopeContext& ctx,
                const std::vector<ast::Param>& params,
                const std::function<LowerTypeResult(
                    const std::shared_ptr<ast::Type>&)>& lower_type) {
  (void)ctx;
  for (const auto& param : params) {
    const auto lowered = lower_type(param.type);
    if (!lowered.ok) {
      return true;  // If we can't lower, assume pure (conservative)
    }
    if (IsImpureType(lowered.type)) {
      return false;
    }
  }
  return true;
}

std::optional<std::string_view> ComptimeTypeAvailabilityDiag(
    const ScopeContext& ctx,
    const TypeRef& type,
    std::string_view unavailable_diag_id) {
  if (const auto forbidden = CtForbiddenTypeDiag(ctx, type)) {
    return forbidden;
  }
  if (!CtAvailType(ctx, type)) {
    return unavailable_diag_id;
  }
  return std::nullopt;
}

}  // namespace cursive::analysis
