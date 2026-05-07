// =================================================================
// File: 04_analysis/typing/expr/contract_entry.cpp
// Construct: Contract Entry (@entry) Intrinsic Type Checking
// Spec Section: 5.8
// Spec Rules: @entry intrinsic semantics
// =================================================================
//
// @ENTRY INTRINSIC (@entry(expr)):
//   1. Valid only in postcondition context (right of =>)
//   2. Type the inner expression at entry state
//   3. Result type must satisfy BitcopyType or CloneType
//   4. Value captured at procedure entry
//   5. Available for comparison in postcondition
//
// TYPING:
//   InPostcondition = true
//   Gamma_entry |- expr : T
//   BitcopyType(T) or CloneType(T)
//   --------------------------------------------------
//   Gamma |- @entry(expr) : T
//
// CONTEXT REQUIREMENTS:
//   - ONLY valid in postcondition context
//   - expr evaluated in entry environment (parameters at call time)
//   - Cannot reference @result
//   - Cannot reference post-state bindings
//
// TYPE CONSTRAINTS:
//   - Result type must be BitcopyType or CloneType
//   - This ensures value can be preserved across procedure body
//   - Prevents capturing unique/move-only types
//
// =================================================================

#include "04_analysis/typing/expr/contract_entry.h"

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsContractEntry() {
  SPEC_DEF("@entry-Intrinsic", "5.8");
  SPEC_DEF("@entry-Context", "5.8");
  SPEC_DEF("@entry-BitcopyOrClone", "5.8");
}

static TypeEnv EntryEnvWithPatternBindings(const TypeEnv& env,
                                          const ast::PatternPtr& pattern) {
  if (!pattern) {
    return env;
  }
  TypeEnv extended = env;
  if (extended.scopes.empty()) {
    extended.scopes.emplace_back();
  }
  std::vector<IdKey> names;
  CollectPatNames(*pattern, names);
  for (const auto& name : names) {
    extended.scopes.back().emplace(name,
                                   TypeBinding{ast::Mutability::Let, nullptr});
  }
  return extended;
}

static bool IsCapabilityMethodName(std::string_view name) {
  return LookupContextMethodSig(name).has_value();
}

static bool IsCapabilityReceiverName(std::string_view name) {
  if (name == "ctx" || name == "context") {
    return true;
  }
  if (ContextFieldType(name).has_value()) {
    return true;
  }
  return name == "system";
}

static bool IsLikelyCapabilityReceiver(const ast::ExprPtr& receiver) {
  if (!receiver) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&receiver->node)) {
    return IsCapabilityReceiverName(ident->name);
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&receiver->node)) {
    return path->path.empty() && IsCapabilityReceiverName(path->name);
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&receiver->node)) {
    if (const auto* base_ident =
            std::get_if<ast::IdentifierExpr>(&field->base->node)) {
      if ((base_ident->name == "ctx" || base_ident->name == "context") &&
          ContextFieldType(field->name).has_value()) {
        return true;
      }
    }
    if (const auto* base_path = std::get_if<ast::PathExpr>(&field->base->node)) {
      if (base_path->path.empty() &&
          (base_path->name == "ctx" || base_path->name == "context") &&
          ContextFieldType(field->name).has_value()) {
        return true;
      }
    }
    return IsCapabilityReceiverName(field->name);
  }
  return false;
}

static bool ExprContainsCapabilityOp(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const TypeEnv& env,
                                     const ast::ExprPtr& expr);
static bool ExprContainsSideEffectOp(const ast::ExprPtr& expr);

static bool AnyArgContainsCapability(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const TypeEnv& env,
                                     const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (ExprContainsCapabilityOp(ctx, type_ctx, env, arg.value)) {
      return true;
    }
  }
  return false;
}

static bool AnyArgContainsSideEffect(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (ExprContainsSideEffectOp(arg.value)) {
      return true;
    }
  }
  return false;
}

static bool ExprContainsCapabilityOp(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const TypeEnv& env,
                                     const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (IsLikelyCapabilityReceiver(node.receiver)) {
            return true;
          }
          const auto* recv_ident = node.receiver
                                       ? std::get_if<ast::IdentifierExpr>(
                                             &node.receiver->node)
                                       : nullptr;
          const auto* recv_path = node.receiver
                                      ? std::get_if<ast::PathExpr>(
                                            &node.receiver->node)
                                      : nullptr;
          const bool recv_is_ctx =
              (recv_ident &&
               (recv_ident->name == "ctx" || recv_ident->name == "context")) ||
              (recv_path && recv_path->path.empty() &&
               (recv_path->name == "ctx" || recv_path->name == "context"));
          if (recv_is_ctx && IsCapabilityMethodName(node.name)) {
            return true;
          }
          if (node.receiver) {
            const auto recv_typed = TypeExpr(ctx, type_ctx, node.receiver, env);
            if (recv_typed.ok && TypeContainsCapability(recv_typed.type)) {
              return true;
            }
          }
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.receiver) ||
                 AnyArgContainsCapability(ctx, type_ctx, env, node.args);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.lhs) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.base) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprContainsCapabilityOp(ctx, type_ctx, env, node.callee)) {
            return true;
          }
          return AnyArgContainsCapability(ctx, type_ctx, env, node.args);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.cond) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.then_expr) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsCapabilityOp(ctx, type_ctx, env, elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_capability_op = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_capability_op) {
              return;
            }
            if (ExprContainsCapabilityOp(ctx, type_ctx, env, elem)) {
              contains_capability_op = true;
            }
          });
          return contains_capability_op;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsCapabilityOp(ctx, type_ctx, env, field.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsCapabilityOp(ctx, type_ctx, env, node.scrutinee)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ExprContainsCapabilityOp(ctx, type_ctx, env,
                                         case_clause.body)) {
              return true;
            }
          }
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.scrutinee) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.then_expr) ||
                 ExprContainsCapabilityOp(ctx, type_ctx, env, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.expr);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.place);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.expr);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.value);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return ExprContainsCapabilityOp(ctx, type_ctx, env, node.handle);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool ExprContainsSideEffectOp(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (
            std::is_same_v<T, ast::SyncExpr> ||
            std::is_same_v<T, ast::YieldExpr> ||
            std::is_same_v<T, ast::YieldFromExpr> ||
            std::is_same_v<T, ast::SpawnExpr> ||
            std::is_same_v<T, ast::WaitExpr> ||
            std::is_same_v<T, ast::ParallelExpr> ||
            std::is_same_v<T, ast::DispatchExpr> ||
            std::is_same_v<T, ast::RaceExpr> ||
            std::is_same_v<T, ast::AllExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsSideEffectOp(node.lhs) ||
                 ExprContainsSideEffectOp(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsSideEffectOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsSideEffectOp(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsSideEffectOp(node.base) ||
                 ExprContainsSideEffectOp(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprContainsSideEffectOp(node.callee)) {
            return true;
          }
          return AnyArgContainsSideEffect(node.args);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsSideEffectOp(node.receiver)) {
            return true;
          }
          return AnyArgContainsSideEffect(node.args);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsSideEffectOp(node.cond) ||
                 ExprContainsSideEffectOp(node.then_expr) ||
                 ExprContainsSideEffectOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsSideEffectOp(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_side_effect_op = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_side_effect_op) {
              return;
            }
            if (ExprContainsSideEffectOp(elem)) {
              contains_side_effect_op = true;
            }
          });
          return contains_side_effect_op;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsSideEffectOp(node.value) ||
                 ExprContainsSideEffectOp(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsSideEffectOp(field.value)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsSideEffectOp(node.scrutinee)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            if (ExprContainsSideEffectOp(case_clause.body)) {
              return true;
            }
          }
          return ExprContainsSideEffectOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsSideEffectOp(node.scrutinee) ||
                 ExprContainsSideEffectOp(node.then_expr) ||
                 ExprContainsSideEffectOp(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprContainsSideEffectOp(node.expr);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsSideEffectOp(node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsSideEffectOp(node.place);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprContainsSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsSideEffectOp(node.value);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsSideEffectOp(node.expr);
        } else {
          return false;
        }
      },
      expr->node);
}

// Check if expression uses only bindings available in the entry environment
static bool ExprUsesOnlyEntryEnvBindings(const ast::ExprPtr& expr,
                                          const TypeEnv& env) {
  if (!expr) {
    return true;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return BindOf(env, ident->name).has_value();
  }
  // @result is not available in @entry context
  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    return false;
  }
  // Recursively check subexpressions
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.lhs, env) &&
                 ExprUsesOnlyEntryEnvBindings(node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.base, env) &&
                 ExprUsesOnlyEntryEnvBindings(node.index, env);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (!ExprUsesOnlyEntryEnvBindings(node.callee, env)) {
            return false;
          }
          for (const auto& arg : node.args) {
            if (!ExprUsesOnlyEntryEnvBindings(arg.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (!ExprUsesOnlyEntryEnvBindings(node.receiver, env)) {
            return false;
          }
          for (const auto& arg : node.args) {
            if (!ExprUsesOnlyEntryEnvBindings(arg.value, env)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.value, env);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprUsesOnlyEntryEnvBindings(node.cond, env) &&
                 ExprUsesOnlyEntryEnvBindings(node.then_expr, env) &&
                 ExprUsesOnlyEntryEnvBindings(node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (!ExprUsesOnlyEntryEnvBindings(node.scrutinee, env)) {
            return false;
          }
          for (const auto& case_clause : node.cases) {
            const auto arm_env = EntryEnvWithPatternBindings(env,
                                                             case_clause.pattern);
            if (!ExprUsesOnlyEntryEnvBindings(case_clause.body, arm_env)) {
              return false;
            }
          }
          return ExprUsesOnlyEntryEnvBindings(node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          const auto then_env = EntryEnvWithPatternBindings(env, node.pattern);
          return ExprUsesOnlyEntryEnvBindings(node.scrutinee, env) &&
                 ExprUsesOnlyEntryEnvBindings(node.then_expr, then_env) &&
                 ExprUsesOnlyEntryEnvBindings(node.else_expr, env);
        } else {
          // For literals and other non-recursive forms
          return true;
        }
      },
      expr->node);
}

}  // namespace

ExprTypeResult TypeEntryExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::EntryExpr& expr,
                                 const TypeEnv& env) {
  SpecDefsContractEntry();
  ExprTypeResult result;

  // @entry only valid in postcondition context
  if (type_ctx.contract_phase != ContractPhase::Postcondition) {
    SPEC_RULE("@entry-Context");
    result.diag_id = "E-SEM-2852";  // @entry outside postcondition
    return result;
  }

  if (!expr.expr) {
    return result;
  }

  // Expression must only reference bindings available at entry
  if (!ExprUsesOnlyEntryEnvBindings(expr.expr, env)) {
    result.diag_id = "E-SEM-2852";
    return result;
  }

  if (ExprContainsCapabilityOp(ctx, type_ctx, env, expr.expr)) {
    SPEC_RULE("Entry-NoCapability-Err");
    result.diag_id = "Entry-NoCapability-Err";
    return result;
  }
  if (ExprContainsSideEffectOp(expr.expr)) {
    SPEC_RULE("Entry-SideEffect-Err");
    result.diag_id = "Entry-SideEffect-Err";
    return result;
  }

  // Type the inner expression
  const auto typed = TypeExpr(ctx, type_ctx, expr.expr, env);
  if (!typed.ok) {
    result.diag_id = typed.diag_id;
    return result;
  }

  // Result type must be Bitcopy or Clone
  if (!BitcopyType(ctx, typed.type) && !CloneType(ctx, typed.type)) {
    SPEC_RULE("@entry-BitcopyOrClone");
    result.diag_id = "E-SEM-2805";  // @entry requires Bitcopy or Clone
    return result;
  }

  SPEC_RULE("@entry-Intrinsic");
  result.ok = true;
  result.type = typed.type;
  return result;
}

}  // namespace cursive::analysis::expr
