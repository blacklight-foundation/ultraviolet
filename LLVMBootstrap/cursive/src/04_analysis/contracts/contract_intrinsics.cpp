/*
 * =============================================================================
 * contract_intrinsics.cpp - Contract Intrinsics Handling
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 14.5.1 "@result Intrinsic"
 *   - CursiveSpecification.md, Section 14.5.2 "@entry Intrinsic"
 *   - CursiveSpecification.md, Section 14.9 "Foreign Contracts"
 *
 * This file implements handling for contract intrinsic expressions:
 *
 * @result:
 *   - Available only in postcondition context
 *   - Type matches procedure return type
 *   - Cannot appear in precondition
 *
 * @entry(expr):
 *   - Captures value of expr at procedure entry time
 *   - expr must be BitcopyType
 *   - Used to compare post-state with pre-state
 *   - Example: @result > @entry(self.value)
 *
 * Foreign Contracts (@foreign_*):
 *   - @foreign_assumes: Predicates caller must establish
 *   - @foreign_ensures: Predicates callee guarantees
 *   - @foreign_ensures(@error: pred): Error condition predicate
 *   - @foreign_ensures(@null_result: pred): Null result predicate
 *
 * DIAGNOSTIC CODES:
 *   - E-SEM-2806: @result used outside postcondition
 *   - E-SEM-2805: @entry() result type not BitcopyType
 *   - E-SEM-2852: Predicate references out-of-scope value
 *   - E-CON-0415: Capability-requiring operation in @entry expression
 *   - E-CON-0416: Side-effecting operation in @entry expression
 *
 * =============================================================================
 */

#include "04_analysis/contracts/contract_check.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsContractIntrinsics() {
  SPEC_DEF("ResultIntrinsic", "C0X.5.W");
  SPEC_DEF("EntryIntrinsic", "C0X.5.W");
  SPEC_DEF("EntryType", "C0X.5.W");
  SPEC_DEF("ForeignAssumes", "C0X.5.W");
  SPEC_DEF("ForeignEnsures", "C0X.5.W");
  SPEC_DEF("ForeignError", "C0X.5.W");
  SPEC_DEF("ForeignNull", "C0X.5.W");
}

// Known Bitcopy types (primitives that can be bitwise copied)
static const std::unordered_set<std::string_view> kBitcopyTypes = {
    // Integers
    "i8",  "i16",  "i32",  "i64",  "i128", "isize",
    "u8",  "u16",  "u32",  "u64",  "u128", "usize",
    // Floats
    "f16", "f32",  "f64",
    // Others
    "bool", "char", "()"
};

// Check if a type is a Bitcopy type
bool IsBitcopyTypeName(std::string_view name) {
  return kBitcopyTypes.find(name) != kBitcopyTypes.end();
}

static bool IsCapabilityMethod(std::string_view name) {
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

static bool EntryExprHasCapabilityOp(const ast::ExprPtr& expr,
                                     const ContractContext* ctx = nullptr);
static bool EntryExprHasSideEffectOp(const ast::ExprPtr& expr);
static bool EntryExprReferencesMovedParam(
    const ast::ExprPtr& expr,
    const std::unordered_set<std::string>& moved_params,
    std::optional<core::Span>* offending_span);

static bool AnyArgHasCapability(const std::vector<ast::Arg>& args,
                                const ContractContext* ctx) {
  for (const auto& arg : args) {
    if (EntryExprHasCapabilityOp(arg.value, ctx)) {
      return true;
    }
  }
  return false;
}

static bool AnyArgHasSideEffect(const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    if (EntryExprHasSideEffectOp(arg.value)) {
      return true;
    }
  }
  return false;
}

static bool EntryExprHasCapabilityOp(const ast::ExprPtr& expr,
                                     const ContractContext* ctx) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          const auto* ident = node.receiver
                                  ? std::get_if<ast::IdentifierExpr>(
                                        &node.receiver->node)
                                  : nullptr;
          const auto* path =
              node.receiver ? std::get_if<ast::PathExpr>(&node.receiver->node)
                            : nullptr;
          if (ctx && ident) {
            const auto param_it = ctx->params.find(ident->name);
            if (param_it != ctx->params.end() &&
                TypeContainsCapability(param_it->second)) {
              return true;
            }
          }
          if (ctx && path && path->path.empty()) {
            const auto param_it = ctx->params.find(path->name);
            if (param_it != ctx->params.end() &&
                TypeContainsCapability(param_it->second)) {
              return true;
            }
          }
          if (IsLikelyCapabilityReceiver(node.receiver)) {
            return true;
          }
          const bool recv_is_ctx =
              (ident && (ident->name == "ctx" || ident->name == "context")) ||
              (path && path->path.empty() &&
               (path->name == "ctx" || path->name == "context"));
          if (recv_is_ctx && IsCapabilityMethod(node.name)) {
            return true;
          }
          return EntryExprHasCapabilityOp(node.receiver, ctx) ||
                 AnyArgHasCapability(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return EntryExprHasCapabilityOp(node.lhs, ctx) ||
                 EntryExprHasCapabilityOp(node.rhs, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return EntryExprHasCapabilityOp(node.base, ctx) ||
                 EntryExprHasCapabilityOp(node.index, ctx);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return EntryExprHasCapabilityOp(node.callee, ctx) ||
                 AnyArgHasCapability(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return EntryExprHasCapabilityOp(node.cond, ctx) ||
                 EntryExprHasCapabilityOp(node.then_expr, ctx) ||
                 EntryExprHasCapabilityOp(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (EntryExprHasCapabilityOp(elem, ctx)) {
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
            if (EntryExprHasCapabilityOp(elem, ctx)) {
              has_capability_op = true;
            }
          });
          return has_capability_op;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx) ||
                 EntryExprHasCapabilityOp(node.count, ctx);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (EntryExprHasCapabilityOp(field.value, ctx)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (EntryExprHasCapabilityOp(node.scrutinee, ctx)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (EntryExprHasCapabilityOp(arm.body, ctx)) {
              return true;
            }
          }
          return EntryExprHasCapabilityOp(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return EntryExprHasCapabilityOp(node.scrutinee, ctx) ||
                 EntryExprHasCapabilityOp(node.then_expr, ctx) ||
                 EntryExprHasCapabilityOp(node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return EntryExprHasCapabilityOp(node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return EntryExprHasCapabilityOp(node.handle, ctx);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool EntryExprHasSideEffectOp(const ast::ExprPtr& expr) {
  if (!expr) {
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
                 AnyArgHasSideEffect(node.args);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return EntryExprHasSideEffectOp(node.receiver) ||
                 AnyArgHasSideEffect(node.args);
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
        } else if constexpr (std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::MoveExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::LoopInfiniteExpr> ||
                             std::is_same_v<T, ast::LoopConditionalExpr> ||
                             std::is_same_v<T, ast::LoopIterExpr> ||
                             std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return true;
        } else {
          return false;
        }
      },
      expr->node);
}

static bool EntryExprReferencesMovedParam(
    const ast::ExprPtr& expr,
    const std::unordered_set<std::string>& moved_params,
    std::optional<core::Span>* offending_span) {
  if (!expr || moved_params.empty()) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (moved_params.find(node.name) != moved_params.end()) {
            if (offending_span) {
              *offending_span = expr->span;
            }
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (node.path.empty() &&
              moved_params.find(node.name) != moved_params.end()) {
            if (offending_span) {
              *offending_span = expr->span;
            }
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return EntryExprReferencesMovedParam(
                     node.lhs, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.rhs, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return EntryExprReferencesMovedParam(
              node.value, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return EntryExprReferencesMovedParam(
              node.base, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return EntryExprReferencesMovedParam(
              node.base, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return EntryExprReferencesMovedParam(
                     node.base, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.index, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (EntryExprReferencesMovedParam(
                  node.callee, moved_params, offending_span)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (EntryExprReferencesMovedParam(
                    arg.value, moved_params, offending_span)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (EntryExprReferencesMovedParam(
                  node.receiver, moved_params, offending_span)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (EntryExprReferencesMovedParam(
                    arg.value, moved_params, offending_span)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return EntryExprReferencesMovedParam(
              node.value, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return EntryExprReferencesMovedParam(
              node.value, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return EntryExprReferencesMovedParam(
                     node.cond, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.then_expr, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.else_expr, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (EntryExprReferencesMovedParam(
                    elem, moved_params, offending_span)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool references_moved_param = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (references_moved_param) {
              return;
            }
            if (EntryExprReferencesMovedParam(
                    elem, moved_params, offending_span)) {
              references_moved_param = true;
            }
          });
          return references_moved_param;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return EntryExprReferencesMovedParam(
                     node.value, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.count, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (EntryExprReferencesMovedParam(
                    field.value, moved_params, offending_span)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (EntryExprReferencesMovedParam(
                  node.scrutinee, moved_params, offending_span)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (EntryExprReferencesMovedParam(
                    arm.body, moved_params, offending_span)) {
              return true;
            }
          }
          return EntryExprReferencesMovedParam(
              node.else_expr, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return EntryExprReferencesMovedParam(
                     node.scrutinee, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.then_expr, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.else_expr, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return EntryExprReferencesMovedParam(
              node.expr, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return EntryExprReferencesMovedParam(
              node.place, moved_params, offending_span);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return EntryExprReferencesMovedParam(
                     node.lhs, moved_params, offending_span) ||
                 EntryExprReferencesMovedParam(
                     node.rhs, moved_params, offending_span);
        } else {
          return false;
        }
      },
      expr->node);
}

// Find all @result expressions in an expression tree
void FindResultExprs(const ast::ExprPtr& expr,
                     std::vector<const ast::Expr*>& results) {
  if (!expr) return;

  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    results.push_back(expr.get());
    return;
  }

  std::visit(
      [&results](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          FindResultExprs(node.lhs, results);
          FindResultExprs(node.rhs, results);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          FindResultExprs(node.value, results);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          FindResultExprs(node.callee, results);
          for (const auto& arg : node.args) {
            FindResultExprs(arg.value, results);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          FindResultExprs(node.receiver, results);
          for (const auto& arg : node.args) {
            FindResultExprs(arg.value, results);
          }
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          FindResultExprs(node.base, results);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          FindResultExprs(node.base, results);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          FindResultExprs(node.base, results);
          FindResultExprs(node.index, results);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          FindResultExprs(node.cond, results);
          FindResultExprs(node.then_expr, results);
          FindResultExprs(node.else_expr, results);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          FindResultExprs(node.value, results);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          FindResultExprs(node.expr, results);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          FindResultExprs(node.scrutinee, results);
          for (const auto& arm : node.cases) {
            FindResultExprs(arm.body, results);
          }
          FindResultExprs(node.else_expr, results);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          FindResultExprs(node.scrutinee, results);
          FindResultExprs(node.then_expr, results);
          FindResultExprs(node.else_expr, results);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            FindResultExprs(elem, results);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            FindResultExprs(elem, results);
          });
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          FindResultExprs(node.lhs, results);
          FindResultExprs(node.rhs, results);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          FindResultExprs(node.place, results);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          FindResultExprs(node.value, results);
        }
        // Other expression types don't contain nested expressions that matter
      },
      expr->node);
}

// Find all @entry expressions in an expression tree
void FindEntryExprs(const ast::ExprPtr& expr,
                    std::vector<const ast::EntryExpr*>& entries) {
  if (!expr) return;

  if (const auto* entry = std::get_if<ast::EntryExpr>(&expr->node)) {
    entries.push_back(entry);
  }

  std::visit(
      [&entries](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          FindEntryExprs(node.lhs, entries);
          FindEntryExprs(node.rhs, entries);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          FindEntryExprs(node.value, entries);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          FindEntryExprs(node.callee, entries);
          for (const auto& arg : node.args) {
            FindEntryExprs(arg.value, entries);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          FindEntryExprs(node.receiver, entries);
          for (const auto& arg : node.args) {
            FindEntryExprs(arg.value, entries);
          }
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          FindEntryExprs(node.base, entries);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          FindEntryExprs(node.base, entries);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          FindEntryExprs(node.base, entries);
          FindEntryExprs(node.index, entries);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          FindEntryExprs(node.cond, entries);
          FindEntryExprs(node.then_expr, entries);
          FindEntryExprs(node.else_expr, entries);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          FindEntryExprs(node.value, entries);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          FindEntryExprs(node.scrutinee, entries);
          for (const auto& arm : node.cases) {
            FindEntryExprs(arm.body, entries);
          }
          FindEntryExprs(node.else_expr, entries);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          FindEntryExprs(node.scrutinee, entries);
          FindEntryExprs(node.then_expr, entries);
          FindEntryExprs(node.else_expr, entries);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            FindEntryExprs(elem, entries);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            FindEntryExprs(elem, entries);
          });
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          FindEntryExprs(node.lhs, entries);
          FindEntryExprs(node.rhs, entries);
        }
        // Note: We don't recurse into @entry's inner expression to avoid
        // finding nested @entry (which would be invalid anyway)
      },
      expr->node);
}

// Check if an expression is deterministic (suitable for @entry capture)
bool IsDeterministicExpr(const ast::ExprPtr& expr) {
  if (!expr) return true;

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        // Simple values are deterministic
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          return true;
        }

        // Field/tuple/index access is deterministic if base is
        else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return IsDeterministicExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return IsDeterministicExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return IsDeterministicExpr(node.base) &&
                 IsDeterministicExpr(node.index);
        }

        // Dereference of a deterministic pointer is deterministic
        else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return IsDeterministicExpr(node.value);
        }

        // Pure binary/unary operators on deterministic values are deterministic
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return IsDeterministicExpr(node.lhs) && IsDeterministicExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return IsDeterministicExpr(node.value);
        }

        // Cast is deterministic if value is
        else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return IsDeterministicExpr(node.value);
        }

        // Conditionals are deterministic if all parts are
        else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return IsDeterministicExpr(node.cond) &&
                 IsDeterministicExpr(node.then_expr) &&
                 (!node.else_expr || IsDeterministicExpr(node.else_expr));
        }

        // Calls are NOT deterministic (may have side effects)
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return false;
        }

        // Async/concurrency operations are not deterministic
        else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return false;
        }

        // Default: assume deterministic for simple expressions
        return true;
      },
      expr->node);
}

}  // namespace

// ============================================================================
// @result Intrinsic Validation
// ============================================================================

IntrinsicValidationResult ValidateResultIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("ResultIntrinsic");

  IntrinsicValidationResult result;

  // @result must only appear in postcondition context
  if (!ctx.is_postcondition) {
    result.ok = false;
    result.diag_id = "E-SEM-2806";
    if (expr) {
      result.span = expr->span;
    }
    return result;
  }

  // Find all @result occurrences
  std::vector<const ast::Expr*> result_exprs;
  FindResultExprs(expr, result_exprs);

  // Each @result must have the return type
  // Note: actual type checking happens during type analysis;
  // here we just validate the intrinsic usage context
  for (const auto* r : result_exprs) {
    // @result found in postcondition - this is valid
    result.result_occurrences.push_back(r->span);
  }

  return result;
}

// ============================================================================
// @entry Intrinsic Validation
// ============================================================================

IntrinsicValidationResult ValidateEntryIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("EntryIntrinsic");

  IntrinsicValidationResult result;

  // @entry should only appear in postcondition context
  // (it is only valid in postconditions in this checker)
  if (!ctx.is_postcondition) {
    result.ok = false;
    result.diag_id = "E-SEM-2852";
    if (expr) {
      result.span = expr->span;
    }
    return result;
  }

  // Find all @entry occurrences
  std::vector<const ast::EntryExpr*> entry_exprs;
  FindEntryExprs(expr, entry_exprs);

  for (const auto* entry : entry_exprs) {
    // Validate the inner expression
    if (!entry->expr) {
      result.ok = false;
      result.diag_id = "E-SEM-2852";
      return result;
    }

    std::optional<core::Span> moved_param_span;
    if (EntryExprReferencesMovedParam(
            entry->expr, ctx.moved_params, &moved_param_span)) {
      result.ok = false;
      result.diag_id = "E-SEM-2807";
      result.span = moved_param_span.value_or(entry->expr->span);
      return result;
    }

    if (EntryExprHasCapabilityOp(entry->expr, &ctx)) {
      result.ok = false;
      result.diag_id = "E-CON-0415";
      result.span = entry->expr->span;
      return result;
    }

    if (EntryExprHasSideEffectOp(entry->expr)) {
      result.ok = false;
      result.diag_id = "E-CON-0416";
      result.span = entry->expr->span;
      return result;
    }

    // Check that inner expression is deterministic
    if (!IsDeterministicExpr(entry->expr)) {
      result.ok = false;
      result.diag_id = "E-CON-0416";
      result.span = entry->expr->span;
      return result;
    }

    // The inner expression's type must be Bitcopy.
    // This is checked during type analysis - we record the occurrence here.
    result.entry_occurrences.push_back({entry->expr, entry->expr->span});
  }

  return result;
}

// ============================================================================
// @entry Type Validation
// ============================================================================

bool ValidateEntryType(const TypeRef& type) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("EntryType");

  if (!type) return false;

  // Check if it's a primitive Bitcopy type
  if (const auto* prim = std::get_if<TypePrim>(&type->node)) {
    return IsBitcopyTypeName(prim->name);
  }

  // Check for tuple of Bitcopy types
  if (const auto* tuple = std::get_if<TypeTuple>(&type->node)) {
    for (const auto& elem : tuple->elements) {
      if (!ValidateEntryType(elem)) {
        return false;
      }
    }
    return true;
  }

  // Check for array of Bitcopy types
  if (const auto* array = std::get_if<TypeArray>(&type->node)) {
    return ValidateEntryType(array->element);
  }

  // For user-defined types, we would need to check if they implement
  // User-defined types require type resolution, which happens later.

  return true;
}

static bool IsUnitTypeForForeignContracts(const TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return IsUnitTypeForForeignContracts(perm->base);
  }
  const auto* prim = std::get_if<TypePrim>(&type->node);
  return prim && prim->name == "()";
}

static bool IsNullableForeignPtrType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return IsNullableForeignPtrType(perm->base);
  }
  if (const auto* ptr = std::get_if<TypePtr>(&type->node)) {
    return ptr->state.has_value() && *ptr->state == PtrState::Null;
  }
  return std::holds_alternative<TypeRawPtr>(type->node);
}

// ============================================================================
// Foreign Contract Validation
// ============================================================================

ForeignContractResult ResolveForeignAssumes(
    const ast::ForeignContractClause& clause) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("ForeignAssumes");

  ForeignContractResult result;

  if (clause.kind != ast::ForeignContractKind::Assumes) {
    result.ok = false;
    result.diag_id = "E-SEM-2851";
    return result;
  }

  // Collect all predicates as assumptions
  for (const auto& pred : clause.predicates) {
    if (!pred) continue;

    ForeignAssumption assumption;
    assumption.predicate = pred;
    assumption.span = pred->span;
    result.assumptions.push_back(assumption);
  }

  return result;
}

ForeignContractResult ResolveForeignEnsures(
    const ast::ForeignContractClause& clause,
    const TypeRef& return_type) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("ForeignEnsures");

  ForeignContractResult result;

  if (clause.kind != ast::ForeignContractKind::Ensures &&
      clause.kind != ast::ForeignContractKind::EnsuresError &&
      clause.kind != ast::ForeignContractKind::EnsuresNullResult) {
    result.ok = false;
    result.diag_id = "E-SEM-2853";
    return result;
  }

  if (clause.kind == ast::ForeignContractKind::EnsuresError &&
      IsUnitTypeForForeignContracts(return_type)) {
    result.ok = false;
    result.diag_id = "E-SEM-2855";
    return result;
  }

  if (clause.kind == ast::ForeignContractKind::EnsuresNullResult &&
      !IsNullableForeignPtrType(return_type)) {
    result.ok = false;
    result.diag_id = "E-SEM-2853";
    return result;
  }

  // Collect all predicates as guarantees
  for (const auto& pred : clause.predicates) {
    if (!pred) continue;

    ForeignGuarantee guarantee;
    guarantee.predicate = pred;
    guarantee.span = pred->span;

    // Mark error/null conditions
    if (clause.kind == ast::ForeignContractKind::EnsuresError) {
      guarantee.is_error_condition = true;
    } else if (clause.kind == ast::ForeignContractKind::EnsuresNullResult) {
      guarantee.is_null_condition = true;
    }

    result.guarantees.push_back(guarantee);
  }

  return result;
}

ForeignContractResult HandleForeignError(
    const ast::ForeignContractClause& clause) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("ForeignError");

  ForeignContractResult result;

  if (clause.kind != ast::ForeignContractKind::EnsuresError) {
    result.ok = false;
    result.diag_id = "E-SEM-2853";
    return result;
  }

  // Process @foreign_ensures(@error: pred)
  for (const auto& pred : clause.predicates) {
    if (!pred) continue;

    ForeignGuarantee guarantee;
    guarantee.predicate = pred;
    guarantee.span = pred->span;
    guarantee.is_error_condition = true;
    result.guarantees.push_back(guarantee);
  }

  result.has_error_clause = true;
  return result;
}

ForeignContractResult HandleForeignNull(
    const ast::ForeignContractClause& clause) {
  SpecDefsContractIntrinsics();
  SPEC_RULE("ForeignNull");

  ForeignContractResult result;

  if (clause.kind != ast::ForeignContractKind::EnsuresNullResult) {
    result.ok = false;
    result.diag_id = "E-SEM-2853";
    return result;
  }

  // Process @foreign_ensures(@null_result: pred)
  for (const auto& pred : clause.predicates) {
    if (!pred) continue;

    ForeignGuarantee guarantee;
    guarantee.predicate = pred;
    guarantee.span = pred->span;
    guarantee.is_null_condition = true;
    result.guarantees.push_back(guarantee);
  }

  result.has_null_clause = true;
  return result;
}

// ============================================================================
// Contract Intrinsic Evaluation (for constant contracts)
// ============================================================================

IntrinsicEvalResult EvaluateContractIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx) {
  SpecDefsContractIntrinsics();

  IntrinsicEvalResult result;

  if (!expr) {
    result.ok = false;
    return result;
  }

  // Check for @result
  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    if (!ctx.is_postcondition) {
      result.ok = false;
      result.diag_id = "E-SEM-2806";
      result.span = expr->span;
      return result;
    }

    result.kind = IntrinsicKind::Result;
    result.resolved_type = ctx.return_type;
    return result;
  }

  // Check for @entry
  if (const auto* entry = std::get_if<ast::EntryExpr>(&expr->node)) {
    if (!ctx.is_postcondition) {
      // @entry typically used in postconditions
      result.ok = false;
      result.diag_id = "E-SEM-2852";
      result.span = expr->span;
      return result;
    }

    result.kind = IntrinsicKind::Entry;
    result.captured_expr = entry->expr;
    // Type resolution happens during type checking
    return result;
  }

  // Not an intrinsic
  result.kind = IntrinsicKind::None;
  return result;
}

// ============================================================================
// Helper: Check if expression contains contract intrinsics
// ============================================================================

bool ContainsContractIntrinsics(const ast::ExprPtr& expr) {
  if (!expr) return false;

  // Direct check
  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::EntryExpr>(expr->node)) {
    return true;
  }

  // Recursive check
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ContainsContractIntrinsics(node.lhs) ||
                 ContainsContractIntrinsics(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ContainsContractIntrinsics(node.value);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ContainsContractIntrinsics(node.callee)) return true;
          for (const auto& arg : node.args) {
            if (ContainsContractIntrinsics(arg.value)) return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ContainsContractIntrinsics(node.receiver)) return true;
          for (const auto& arg : node.args) {
            if (ContainsContractIntrinsics(arg.value)) return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ContainsContractIntrinsics(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ContainsContractIntrinsics(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ContainsContractIntrinsics(node.base) ||
                 ContainsContractIntrinsics(node.index);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ContainsContractIntrinsics(node.cond) ||
                 ContainsContractIntrinsics(node.then_expr) ||
                 ContainsContractIntrinsics(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ContainsContractIntrinsics(node.value);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ContainsContractIntrinsics(node.scrutinee)) return true;
          for (const auto& arm : node.cases) {
            if (ContainsContractIntrinsics(arm.body)) return true;
          }
          return ContainsContractIntrinsics(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ContainsContractIntrinsics(node.scrutinee) ||
                 ContainsContractIntrinsics(node.then_expr) ||
                 ContainsContractIntrinsics(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ContainsContractIntrinsics(elem)) return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool contains_contract_intrinsics = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            if (contains_contract_intrinsics) {
              return;
            }
            if (ContainsContractIntrinsics(elem)) {
              contains_contract_intrinsics = true;
            }
          });
          return contains_contract_intrinsics;
        }

        return false;
      },
      expr->node);
}

// ============================================================================
// Helper: Validate all intrinsics in a contract clause
// ============================================================================

ContractCheckResult ValidateContractIntrinsics(
    const ast::ContractClause& contract,
    const ContractContext& ctx) {
  SpecDefsContractIntrinsics();

  ContractCheckResult result;

  // Check precondition - should NOT contain @result or @entry
  if (contract.precondition) {
    ContractContext pre_ctx = ctx;
    pre_ctx.is_postcondition = false;

    // @result in precondition is an error
    std::vector<const ast::Expr*> result_exprs;
    FindResultExprs(contract.precondition, result_exprs);
    if (!result_exprs.empty()) {
      result.ok = false;
      result.diag_id = "E-SEM-2806";
      result.span = result_exprs[0]->span;
      return result;
    }

    // @entry in precondition is also an error (no entry state to compare)
    std::vector<const ast::EntryExpr*> entry_exprs;
    FindEntryExprs(contract.precondition, entry_exprs);
    if (!entry_exprs.empty()) {
      result.ok = false;
      result.diag_id = "E-SEM-2852";
      if (entry_exprs[0]->expr) {
        result.span = entry_exprs[0]->expr->span;
      }
      return result;
    }
  }

  // Check postcondition - @result and @entry are allowed
  if (contract.postcondition) {
    ContractContext post_ctx = ctx;
    post_ctx.is_postcondition = true;

    // Validate @result usage
    auto result_validation =
        ValidateResultIntrinsic(contract.postcondition, post_ctx);
    if (!result_validation.ok) {
      result.ok = false;
      result.diag_id = result_validation.diag_id;
      result.span = result_validation.span;
      return result;
    }

    // Validate @entry usage
    auto entry_validation =
        ValidateEntryIntrinsic(contract.postcondition, post_ctx);
    if (!entry_validation.ok) {
      result.ok = false;
      result.diag_id = entry_validation.diag_id;
      result.span = entry_validation.span;
      return result;
    }
  }

  return result;
}

}  // namespace cursive::analysis
