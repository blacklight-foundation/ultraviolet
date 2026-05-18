// =============================================================================
// File: 04_analysis/typing/expr/if_expr.cpp
// If Expression Typing
// Spec Section: 5.2.12
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.12: Expression Typing
//   - T-If-Else: if cond { then_branch } else { else_branch }
//   - T-If-Unit: if cond { then_branch } (no else, result is unit)
//
// =============================================================================

#include "04_analysis/typing/expr/if_expr.h"

#include <optional>
#include <string>
#include <string_view>
#include <memory>
#include <type_traits>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/expr/path.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsIfExpr() {
  SPEC_DEF("T-If", "5.2.12");
  SPEC_DEF("T-If-Else", "5.2.12");
  SPEC_DEF("T-If-Unit", "5.2.12");
  SPEC_DEF("T-If-No-Else", "5.2.12");
  SPEC_DEF("Chk-If", "5.2.12");
  SPEC_DEF("Chk-If-No-Else", "5.2.12");
  SPEC_DEF("If-Cond-NotBool", "5.2.12");
  SPEC_DEF("If-Branch-Mismatch", "5.2.12");
}

static ast::ExprPtr MakeExprNode(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static bool IsComparisonOperator(std::string_view op) {
  return IdEq(op, "==") || IdEq(op, "!=") || IdEq(op, "<") ||
         IdEq(op, "<=") || IdEq(op, ">") || IdEq(op, ">=");
}

static std::optional<std::string> FlipComparisonOperator(std::string_view op) {
  if (IdEq(op, "<")) {
    return std::string(">");
  }
  if (IdEq(op, "<=")) {
    return std::string(">=");
  }
  if (IdEq(op, ">")) {
    return std::string("<");
  }
  if (IdEq(op, ">=")) {
    return std::string("<=");
  }
  if (IdEq(op, "==") || IdEq(op, "!=")) {
    return std::string(op);
  }
  return std::nullopt;
}

static ast::ExprPtr ConjoinPredicates(const ast::ExprPtr& lhs,
                                      const ast::ExprPtr& rhs) {
  if (!lhs) {
    return rhs;
  }
  if (!rhs) {
    return lhs;
  }
  ast::BinaryExpr and_expr;
  and_expr.op = "&&";
  and_expr.lhs = lhs;
  and_expr.rhs = rhs;
  return MakeExprNode(lhs->span, and_expr);
}

static void CollectThenBranchFacts(
    const ast::ExprPtr& cond,
    std::vector<std::pair<IdKey, ast::ExprPtr>>& out) {
  if (!cond) {
    return;
  }
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&cond->node)) {
    if (IdEq(binary->op, "&&")) {
      CollectThenBranchFacts(binary->lhs, out);
      CollectThenBranchFacts(binary->rhs, out);
      return;
    }
    if (!IsComparisonOperator(binary->op)) {
      return;
    }

    auto make_predicate = [&](std::string_view op,
                              const ast::ExprPtr& rhs,
                              const IdKey& binding_name) {
      if (!rhs) {
        return;
      }
      ast::BinaryExpr pred;
      pred.op = std::string(op);
      pred.lhs = MakeExprNode(cond->span, ast::IdentifierExpr{"self"});
      pred.rhs = rhs;
      out.emplace_back(binding_name, MakeExprNode(cond->span, pred));
    };

    if (binary->lhs) {
      if (const auto* ident_lhs =
              std::get_if<ast::IdentifierExpr>(&binary->lhs->node)) {
        make_predicate(binary->op, binary->rhs, IdKeyOf(ident_lhs->name));
        return;
      }
    }
    if (binary->rhs) {
      if (const auto* ident_rhs =
              std::get_if<ast::IdentifierExpr>(&binary->rhs->node)) {
        const auto flipped = FlipComparisonOperator(binary->op);
        if (!flipped.has_value()) {
          return;
        }
        make_predicate(*flipped, binary->lhs, IdKeyOf(ident_rhs->name));
      }
    }
  }
}

static TypeRef AddRefinementFactToBindingType(const TypeRef& original_type,
                                              const ast::ExprPtr& predicate) {
  if (!original_type || !predicate) {
    return original_type;
  }
  if (const auto* refine = std::get_if<TypeRefine>(&original_type->node)) {
    return MakeTypeRefine(refine->base,
                          ConjoinPredicates(refine->predicate, predicate));
  }
  return MakeTypeRefine(original_type, predicate);
}

static TypeEnv RefineEnvFromConditionFacts(const TypeEnv& env,
                                           const ast::ExprPtr& cond) {
  TypeEnv out = env;
  std::vector<std::pair<IdKey, ast::ExprPtr>> facts;
  CollectThenBranchFacts(cond, facts);
  for (const auto& [binding_name, predicate] : facts) {
    for (auto scope_it = out.scopes.rbegin(); scope_it != out.scopes.rend();
         ++scope_it) {
      auto found = scope_it->find(binding_name);
      if (found == scope_it->end()) {
        continue;
      }
      found->second.type =
          AddRefinementFactToBindingType(found->second.type, predicate);
      break;
    }
  }
  return out;
}

static bool ConditionCanContributeProofFacts(const ScopeContext& ctx,
                                             const ast::ExprPtr& cond) {
  ContractContext contract_ctx;
  contract_ctx.scope_ctx = &ctx;
  return CheckPurity(contract_ctx, cond).ok;
}

static ast::ExprPtr ElseFactCondition(const ast::ExprPtr& cond) {
  const auto negated = NegatedPredicate(cond);
  if (!negated.has_value()) {
    return nullptr;
  }
  return *negated;
}

// Check if a type is the never type (!)
static bool IsNeverType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<TypePrim>(&type->node)) {
    return prim->name == "!";
  }
  return false;
}

// Check if type is bool
static bool IsBoolType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  // Strip permission and refinement wrappers
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
    return prim->name == "bool";
  }
  return false;
}

static TypeRef StripPermDeepLocal(const TypeRef& type) {
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

static bool IsGpuDomainType(const TypeRef& type) {
  const auto stripped = StripPermDeepLocal(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
}

static bool IsGpuBarrierCall(const ast::ExprPtr& callee) {
  if (!callee) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    return analysis::IsGpuExecutionBarrierName(ident->name);
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
    return analysis::IsGpuExecutionBarrierName(path->name);
  }
  return false;
}

static bool ContainsGpuBarrierCall(const ast::ExprPtr& expr);

static bool ContainsGpuBarrierCallInStmt(const ast::Stmt& stmt) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          return ContainsGpuBarrierCall(node.binding.init);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          return ContainsGpuBarrierCall(node.binding.init);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          return ContainsGpuBarrierCall(node.place) ||
                 ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          return ContainsGpuBarrierCall(node.place) ||
                 ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (!node.body) {
            return false;
          }
          for (const auto& nested : node.body->stmts) {
            if (ContainsGpuBarrierCallInStmt(nested)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.body->tail_opt);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (!node.body) {
            return false;
          }
          for (const auto& nested : node.body->stmts) {
            if (ContainsGpuBarrierCallInStmt(nested)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.body->tail_opt);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (!node.body) {
            return false;
          }
          for (const auto& nested : node.body->stmts) {
            if (ContainsGpuBarrierCallInStmt(nested)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.body->tail_opt);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (!node.body) {
            return false;
          }
          for (const auto& nested : node.body->stmts) {
            if (ContainsGpuBarrierCallInStmt(nested)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.body->tail_opt);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (!node.body) {
            return false;
          }
          for (const auto& nested : node.body->stmts) {
            if (ContainsGpuBarrierCallInStmt(nested)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.body->tail_opt);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          return ContainsGpuBarrierCall(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return ContainsGpuBarrierCall(node.value_opt);
        }
        return false;
      },
      stmt);
}

static bool ContainsGpuBarrierCallInBlock(const ast::BlockPtr& block) {
  if (!block) {
    return false;
  }
  for (const auto& stmt : block->stmts) {
    if (ContainsGpuBarrierCallInStmt(stmt)) {
      return true;
    }
  }
  return ContainsGpuBarrierCall(block->tail_opt);
}

static bool ContainsGpuBarrierCall(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (IsGpuBarrierCall(node.callee)) {
            return true;
          }
          if (ContainsGpuBarrierCall(node.callee)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ContainsGpuBarrierCall(arg.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ContainsGpuBarrierCall(node.receiver)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ContainsGpuBarrierCall(arg.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ContainsGpuBarrierCall(node.cond) ||
                 ContainsGpuBarrierCall(node.then_expr) ||
                 ContainsGpuBarrierCall(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return ContainsGpuBarrierCallInBlock(node.block);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return ContainsGpuBarrierCallInBlock(node.block);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ContainsGpuBarrierCall(node.expr);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ContainsGpuBarrierCall(node.lhs) ||
                 ContainsGpuBarrierCall(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ContainsGpuBarrierCall(node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ContainsGpuBarrierCall(node.place);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ContainsGpuBarrierCall(node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ContainsGpuBarrierCall(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_gpu_barrier_call = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_gpu_barrier_call) {
              return;
            }
            if (ContainsGpuBarrierCall(elem)) {
              contains_gpu_barrier_call = true;
            }
          });
          return contains_gpu_barrier_call;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ContainsGpuBarrierCall(node.value) ||
                 ContainsGpuBarrierCall(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ContainsGpuBarrierCall(field.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ContainsGpuBarrierCall(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ContainsGpuBarrierCall(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ContainsGpuBarrierCall(node.base) ||
                 ContainsGpuBarrierCall(node.index);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ContainsGpuBarrierCall(node.lhs) ||
                 ContainsGpuBarrierCall(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ContainsGpuBarrierCall(node.scrutinee)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ContainsGpuBarrierCall(case_clause.body)) {
              return true;
            }
          }
          return ContainsGpuBarrierCall(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ContainsGpuBarrierCall(node.scrutinee) ||
                 ContainsGpuBarrierCall(node.then_expr) ||
                 ContainsGpuBarrierCall(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ContainsGpuBarrierCall(node.lhs) ||
                 ContainsGpuBarrierCall(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ContainsGpuBarrierCall(node.value);
        }
        return false;
      },
      expr->node);
}

// Unify branch types - handles never type absorption
static TypeRef UnifyBranchTypes(const ScopeContext& ctx,
                                const TypeRef& then_type,
                                const TypeRef& else_type) {
  // If one branch is never (!), the result is the other type
  if (IsNeverType(then_type)) {
    return else_type;
  }
  if (IsNeverType(else_type)) {
    return then_type;
  }

  // Check for type equivalence
  const auto equiv_result = TypeEquiv(then_type, else_type);
  if (equiv_result.ok && equiv_result.equiv) {
    return then_type;
  }

  // Try subtyping: find common supertype
  const auto sub1 = Subtyping(ctx, then_type, else_type);
  if (sub1.ok && sub1.subtype) {
    return else_type;
  }

  const auto sub2 = Subtyping(ctx, else_type, then_type);
  if (sub2.ok && sub2.subtype) {
    return then_type;
  }

  // Types are incompatible - return null
  return nullptr;
}

static std::optional<std::string_view> IfNoElseDiagOrFallback(
    std::optional<std::string_view> diag_id) {
  if (diag_id.has_value() && *diag_id != "E-SEM-2526") {
    return diag_id;
  }
  return std::optional<std::string_view>{"If-Branch-Mismatch"};
}

}  // namespace

ExprTypeResult TypeIfExpr(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::IfExpr& expr,
                          const TypeEnv& env,
                          const ExprTypeFn& type_expr);

CheckResult CheckIfExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::IfExpr& expr,
                        const TypeRef& expected,
                        const TypeEnv& env,
                        const ExprTypeFn& type_expr);

ExprTypeResult TypeIfExprImpl(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::IfExpr& expr,
                              const TypeEnv& env) {
  SpecDefsIfExpr();
  const ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  return TypeIfExpr(ctx, type_ctx, expr, env, type_expr);
}

CheckResult CheckIfExprImpl(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::IfExpr& expr,
                            const TypeRef& expected,
                            const TypeEnv& env) {
  SpecDefsIfExpr();

  const ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  return CheckIfExpr(ctx, type_ctx, expr, expected, env, type_expr);
}

// Full implementation with type_expr callback
ExprTypeResult TypeIfExpr(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::IfExpr& expr,
                          const TypeEnv& env,
                          const ExprTypeFn& type_expr) {
  SpecDefsIfExpr();
  ExprTypeResult result;

  if (!expr.cond || !expr.then_expr) {
    return result;
  }

  // 1. Type the condition
  const auto cond_type = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.cond, env);
  if (!cond_type.ok) {
    result.diag_id = cond_type.diag_id;
    result.diag_detail = cond_type.diag_detail;
    return result;
  }

  // Verify condition is bool
  if (!IsBoolType(cond_type.type)) {
    SPEC_RULE("If-Cond-NotBool");
    result.diag_id = "If-Cond-NotBool";
    return result;
  }

  if (GpuContext(env)) {
    const bool then_has_barrier = ContainsGpuBarrierCall(expr.then_expr);
    const bool else_has_barrier = ContainsGpuBarrierCall(expr.else_expr);
    if (then_has_barrier != else_has_barrier) {
      SPEC_RULE("Barrier-Divergence-Err");
      result.diag_id = "E-CON-0158";
      return result;
    }
  }

  const bool condition_has_proof_facts =
      ConditionCanContributeProofFacts(ctx, expr.cond);
  TypeEnv then_env =
      condition_has_proof_facts ? RefineEnvFromConditionFacts(env, expr.cond) : env;
  TypeEnv else_env = env;
  if (condition_has_proof_facts) {
    if (const auto else_fact_cond = ElseFactCondition(expr.cond)) {
      else_env = RefineEnvFromConditionFacts(env, else_fact_cond);
    }
  }
  StmtTypeContext then_ctx = type_ctx;
  if (condition_has_proof_facts) {
    then_ctx.proof_ctx =
        ExtendProofContextWithPredicate(type_ctx.proof_ctx, expr.cond);
  }
  StmtTypeContext else_ctx = type_ctx;
  if (condition_has_proof_facts) {
    if (const auto else_fact_cond = ElseFactCondition(expr.cond)) {
      else_ctx.proof_ctx =
          ExtendProofContextWithPredicate(type_ctx.proof_ctx, else_fact_cond);
    }
  }

  // 3. Handle else branch
  if (!expr.else_expr) {
    const auto then_check =
        CheckExprAgainst(ctx, then_ctx, expr.then_expr, MakeTypePrim("()"),
                         then_env);
    if (!then_check.ok) {
      SPEC_RULE("T-If-No-Else");
      result.diag_id = IfNoElseDiagOrFallback(then_check.diag_id);
      result.diag_detail = then_check.diag_detail;
      result.diag_span = then_check.diag_span.has_value()
                             ? then_check.diag_span
                             : std::optional<core::Span>(expr.then_expr->span);
      return result;
    }
    SPEC_RULE("T-If-Unit");
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }

  // 2. Type the then branch
  const auto then_type = TypeExpr(ctx, then_ctx, expr.then_expr, then_env);
  if (!then_type.ok) {
    result.diag_id = then_type.diag_id;
    result.diag_detail = then_type.diag_detail;
    return result;
  }

  // 4. Type else branch
  const auto else_type = TypeExpr(ctx, else_ctx, expr.else_expr, else_env);
  if (!else_type.ok) {
    result.diag_id = else_type.diag_id;
    result.diag_detail = else_type.diag_detail;
    return result;
  }

  // 5. Unify branch types
  const auto unified = UnifyBranchTypes(ctx, then_type.type, else_type.type);
  if (!unified) {
    SPEC_RULE("If-Branch-Mismatch");
    result.diag_id = "If-Branch-Mismatch";
    return result;
  }

  SPEC_RULE("T-If");
  SPEC_RULE("T-If-Else");
  result.ok = true;
  result.type = unified;
  return result;
}

// Full check implementation
CheckResult CheckIfExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::IfExpr& expr,
                        const TypeRef& expected,
                        const TypeEnv& env,
                        const ExprTypeFn& type_expr) {
  SpecDefsIfExpr();
  CheckResult result;

  if (!expr.cond || !expr.then_expr || !expected) {
    return result;
  }

  // 1. Type the condition
  const auto cond_type = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.cond, env);
  if (!cond_type.ok) {
    result.diag_id = cond_type.diag_id;
    result.diag_detail = cond_type.diag_detail;
    return result;
  }

  // Verify condition is bool
  if (!IsBoolType(cond_type.type)) {
    SPEC_RULE("If-Cond-NotBool");
    result.diag_id = "If-Cond-NotBool";
    return result;
  }

  if (GpuContext(env)) {
    const bool then_has_barrier = ContainsGpuBarrierCall(expr.then_expr);
    const bool else_has_barrier = ContainsGpuBarrierCall(expr.else_expr);
    if (then_has_barrier != else_has_barrier) {
      SPEC_RULE("Barrier-Divergence-Err");
      result.diag_id = "E-CON-0158";
      return result;
    }
  }

  const bool condition_has_proof_facts =
      ConditionCanContributeProofFacts(ctx, expr.cond);
  TypeEnv then_env =
      condition_has_proof_facts ? RefineEnvFromConditionFacts(env, expr.cond) : env;
  TypeEnv else_env = env;
  if (condition_has_proof_facts) {
    if (const auto else_fact_cond = ElseFactCondition(expr.cond)) {
      else_env = RefineEnvFromConditionFacts(env, else_fact_cond);
    }
  }
  StmtTypeContext then_ctx = type_ctx;
  if (condition_has_proof_facts) {
    then_ctx.proof_ctx =
        ExtendProofContextWithPredicate(type_ctx.proof_ctx, expr.cond);
  }
  StmtTypeContext else_ctx = type_ctx;
  if (condition_has_proof_facts) {
    if (const auto else_fact_cond = ElseFactCondition(expr.cond)) {
      else_ctx.proof_ctx =
          ExtendProofContextWithPredicate(type_ctx.proof_ctx, else_fact_cond);
    }
  }

  // 2. Check then branch
  const auto then_check =
      CheckExprAgainst(ctx, then_ctx, expr.then_expr, expected, then_env);
  if (!then_check.ok) {
    result.diag_id = then_check.diag_id;
    result.diag_detail = then_check.diag_detail;
    return result;
  }

  // 3. Handle else branch
  if (!expr.else_expr) {
    const auto unit_type = MakeTypePrim("()");
    const auto then_check =
        CheckExprAgainst(ctx, then_ctx, expr.then_expr, unit_type, then_env);
    if (!then_check.ok) {
      result.diag_id = IfNoElseDiagOrFallback(then_check.diag_id);
      result.diag_detail = then_check.diag_detail;
      result.diag_span = then_check.diag_span.has_value()
                             ? then_check.diag_span
                             : std::optional<core::Span>(expr.then_expr->span);
      return result;
    }

    const auto sub = Subtyping(ctx, unit_type, expected);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (!sub.subtype) {
      result.diag_id = IfNoElseDiagOrFallback(sub.diag_id);
      return result;
    }

    SPEC_RULE("Chk-If-No-Else");
    result.ok = true;
    return result;
  }

  // 4. Check else branch
  const auto else_check =
      CheckExprAgainst(ctx, else_ctx, expr.else_expr, expected, else_env);
  if (!else_check.ok) {
    result.diag_id = else_check.diag_id;
    result.diag_detail = else_check.diag_detail;
    return result;
  }

  SPEC_RULE("Chk-If");
  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis::expr
