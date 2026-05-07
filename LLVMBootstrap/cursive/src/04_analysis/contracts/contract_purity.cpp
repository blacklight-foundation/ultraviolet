/*
 * =============================================================================
 * contract_purity.cpp - Contract Purity Checking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 14.7 "Purity Constraints"
 *   - CursiveSpecification.md, Section 14.7.1 "Pure Expressions"
 *   - CursiveSpecification.md, Section 14.7.2 "Prohibited Operations"
 *
 * This file implements purity checking for contract predicates. Contract
 * predicates MUST be pure according to the spec:
 *   1. MUST NOT invoke procedures accepting capability parameters
 *   2. MUST NOT mutate state observable outside expression evaluation
 *   3. Built-in operators on primitive types are always pure
 *   4. Method calls must be to pure methods only
 *   5. No I/O, no allocation (unless provably temporary)
 *
 * PURE OPERATIONS:
 *   - Arithmetic: +, -, *, /, %, **
 *   - Comparison: ==, !=, <, <=, >, >=
 *   - Logical: &&, ||, !
 *   - Bitwise: &, |, ^, <<, >>
 *   - Field access on const receiver
 *   - Array/tuple indexing on const receiver
 *   - Contract intrinsics: @result, @entry(expr)
 *
 * IMPURE OPERATIONS (PROHIBITED):
 *   - Capability method calls (ctx.fs~>read_file, ctx.heap~>alloc, etc.)
 *   - Assignment and mutation
 *   - I/O operations
 *   - Memory allocation (^)
 *   - Procedures with side effects
 *   - Async operations (yield, spawn, wait, etc.)
 *
 * DIAGNOSTIC CODES:
 *   - E-SEM-2802: Impure expression in contract predicate
 *   - E-SEM-3004: Impure expression in contract clause
 *
 * =============================================================================
 */

#include "04_analysis/contracts/contract_check.h"

#include <string_view>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsContractPurity() {
  SPEC_DEF("ContractPure", "C0X.5.W");
  SPEC_DEF("PureExpr", "C0X.5.W");
  SPEC_DEF("CapabilityCall", "C0X.5.W");
  SPEC_DEF("SideEffect", "C0X.5.W");
  SPEC_DEF("PureOp", "C0X.5.W");
  SPEC_DEF("MutationFree", "C0X.5.W");
}

// Pure binary operators on primitive types
static const std::unordered_set<std::string_view> kPureBinaryOps = {
    // Arithmetic
    "+", "-", "*", "/", "%", "**",
    // Comparison
    "==", "!=", "<", "<=", ">", ">=",
    // Logical
    "&&", "||",
    // Bitwise
    "&", "|", "^", "<<", ">>",
    // Range
    "..", "..="
};

// Pure unary operators
static const std::unordered_set<std::string_view> kPureUnaryOps = {
    "-", "+", "!", "~"
};

// Forward declarations for recursive purity checking
struct PurityCheckState {
  ContractCheckResult result;
  bool has_error = false;
};

void CheckExprPurity(const ast::ExprPtr& expr, PurityCheckState& state);

// Check if an identifier name suggests a capability receiver
bool IsCapabilityReceiverName(std::string_view name) {
  if (name == "ctx" || name == "context") {
    return true;
  }
  if (ContextFieldType(name).has_value()) {
    return true;
  }
  return name == "system";
}

// Check if a binary operator is pure
bool IsPureBinaryOp(std::string_view op) {
  return kPureBinaryOps.find(op) != kPureBinaryOps.end();
}

// Check if a unary operator is pure
bool IsPureUnaryOp(std::string_view op) {
  return kPureUnaryOps.find(op) != kPureUnaryOps.end();
}

// Check if a method call appears to be on a capability receiver
// This is a conservative syntactic check
bool IsLikelyCapabilityCall(const ast::MethodCallExpr& call) {
  if (!call.receiver) return false;

  // Check if receiver is a direct identifier that looks like a capability
  if (const auto* ident =
          std::get_if<ast::IdentifierExpr>(&call.receiver->node)) {
    return IsCapabilityReceiverName(ident->name);
  }

  // Check if receiver is a field access on ctx (e.g., ctx.fs, ctx.heap)
  if (const auto* field =
          std::get_if<ast::FieldAccessExpr>(&call.receiver->node)) {
    if (const auto* base =
            std::get_if<ast::IdentifierExpr>(&field->base->node)) {
      if (base->name == "ctx") {
        return true;  // Any method call on ctx.* is a capability call
      }
    }
    // Also check if the field name is a known capability
    return IsCapabilityReceiverName(field->name);
  }

  return false;
}

// Check purity of a literal expression (always pure)
void CheckLiteralPurity(const ast::LiteralExpr& lit, PurityCheckState& state) {
  // Literals are always pure
}

// Check purity of an identifier expression (always pure for reading)
void CheckIdentifierPurity(const ast::IdentifierExpr& ident,
                           PurityCheckState& state) {
  // Reading an identifier is pure
}

// Check purity of a binary expression
void CheckBinaryPurity(const ast::BinaryExpr& binary, PurityCheckState& state) {
  if (state.has_error) return;

  // Check if the operator is pure
  if (!IsPureBinaryOp(binary.op)) {
    state.has_error = true;
    state.result.ok = false;
    state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
    return;
  }

  // Recursively check operands
  CheckExprPurity(binary.lhs, state);
  if (state.has_error) return;
  CheckExprPurity(binary.rhs, state);
}

// Check purity of a unary expression
void CheckUnaryPurity(const ast::UnaryExpr& unary, PurityCheckState& state) {
  if (state.has_error) return;

  // Dereference (*) is impure in certain contexts but allowed for reading
  // Address-of (&) is handled separately as AddressOfExpr

  if (!IsPureUnaryOp(unary.op) && unary.op != "*") {
    state.has_error = true;
    state.result.ok = false;
    state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
    return;
  }

  CheckExprPurity(unary.value, state);
}

// Check purity of a method call expression
void CheckMethodCallPurity(const ast::MethodCallExpr& call,
                           PurityCheckState& state) {
  if (state.has_error) return;

  // Check for capability calls
  if (IsLikelyCapabilityCall(call)) {
    state.has_error = true;
    state.result.ok = false;
    state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
    return;
  }

  // Method-call purity requires resolved method metadata. This pass does not
  // have that information, so it must conservatively reject the call.
  state.has_error = true;
  state.result.ok = false;
  state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
}

// Check purity of a call expression
void CheckCallPurity(const ast::CallExpr& call, PurityCheckState& state) {
  if (state.has_error) return;
  (void)call;

  // Procedure-call purity requires resolved callee metadata. Without that
  // proof, this checker must conservatively treat calls as impure.
  state.has_error = true;
  state.result.ok = false;
  state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
}

// Check purity of a field access expression (pure for reading)
void CheckFieldAccessPurity(const ast::FieldAccessExpr& access,
                            PurityCheckState& state) {
  if (state.has_error) return;
  CheckExprPurity(access.base, state);
}

// Check purity of a tuple access expression (pure for reading)
void CheckTupleAccessPurity(const ast::TupleAccessExpr& access,
                            PurityCheckState& state) {
  if (state.has_error) return;
  CheckExprPurity(access.base, state);
}

// Check purity of an index access expression (pure for reading)
void CheckIndexAccessPurity(const ast::IndexAccessExpr& access,
                            PurityCheckState& state) {
  if (state.has_error) return;
  CheckExprPurity(access.base, state);
  CheckExprPurity(access.index, state);
}

// Check purity of an if expression
void CheckIfPurity(const ast::IfExpr& if_expr, PurityCheckState& state) {
  if (state.has_error) return;

  CheckExprPurity(if_expr.cond, state);
  if (state.has_error) return;

  CheckExprPurity(if_expr.then_expr, state);
  if (state.has_error) return;

  if (if_expr.else_expr) {
    CheckExprPurity(if_expr.else_expr, state);
  }
}

// Check purity of a cast expression (pure)
void CheckCastPurity(const ast::CastExpr& cast, PurityCheckState& state) {
  if (state.has_error) return;
  CheckExprPurity(cast.value, state);
}

// Check purity of a range expression (pure)
void CheckRangePurity(const ast::RangeExpr& range, PurityCheckState& state) {
  if (state.has_error) return;

  if (range.lhs) {
    CheckExprPurity(range.lhs, state);
    if (state.has_error) return;
  }

  if (range.rhs) {
    CheckExprPurity(range.rhs, state);
  }
}

// Check purity of a tuple expression (pure)
void CheckTuplePurity(const ast::TupleExpr& tuple, PurityCheckState& state) {
  if (state.has_error) return;

  for (const auto& elem : tuple.elements) {
    CheckExprPurity(elem, state);
    if (state.has_error) return;
  }
}

// Check purity of an array expression (pure)
void CheckArrayPurity(const ast::ArrayExpr& array, PurityCheckState& state) {
  if (state.has_error) return;

  for (const auto& elem : array.elements) {
    CheckExprPurity(elem, state);
    if (state.has_error) return;
  }
}

// Check purity of an @entry expression (pure - captures entry value)
void CheckEntryPurity(const ast::EntryExpr& entry, PurityCheckState& state) {
  if (state.has_error) return;

  // @entry(expr) captures the entry value of expr
  // The inner expression must also be pure
  CheckExprPurity(entry.expr, state);
}

// Check purity of an @result expression (pure - references return value)
void CheckResultPurity(const ast::ResultExpr& result, PurityCheckState& state) {
  // @result is always pure - it just references the return value
}

// Check if expression contains any impure operations
// Main recursive function
void CheckExprPurity(const ast::ExprPtr& expr, PurityCheckState& state) {
  if (!expr || state.has_error) return;

  std::visit(
      [&state, &expr](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        // Error node - skip
        if constexpr (std::is_same_v<T, ast::ErrorExpr>) {
          return;
        }

        // Pure expressions
        else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          CheckLiteralPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          CheckIdentifierPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          // Qualified names are pure for reading
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          // Path expressions are pure for reading
        }

        // Operators
        else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CheckBinaryPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CheckUnaryPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          CheckCastPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          CheckRangePurity(node, state);
        }

        // Access expressions (pure for reading)
        else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CheckFieldAccessPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          CheckTupleAccessPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CheckIndexAccessPurity(node, state);
        }

        // Call expressions
        else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CheckCallPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CheckMethodCallPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          // Qualified apply - check arguments
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& paren = std::get<ast::ParenArgs>(node.args);
            for (const auto& arg : paren.args) {
              CheckExprPurity(arg.value, state);
              if (state.has_error) return;
            }
          } else {
            const auto& brace = std::get<ast::BraceArgs>(node.args);
            for (const auto& field : brace.fields) {
              CheckExprPurity(field.value, state);
              if (state.has_error) return;
            }
          }
        }

        // Control flow (conditionals only)
        else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CheckIfPurity(node, state);
        }

        // Aggregate expressions
        else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          CheckTuplePurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          CheckArrayPurity(node, state);
        }

        // Contract intrinsics (pure)
        else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          CheckResultPurity(node, state);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          CheckEntryPurity(node, state);
        }

        // Intrinsics (sizeof, alignof are pure)
        else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          // sizeof is pure
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          // alignof is pure
        }

        // Address-of is pure (reading address)
        else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          CheckExprPurity(node.place, state);
        }

        // Dereference for reading is allowed
        else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          CheckExprPurity(node.value, state);
        }

        // IMPURE OPERATIONS - these are prohibited in contracts

        // Allocation is impure
        else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Move is impure (transfers ownership)
        else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Transmute is impure
        else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Propagate (?) is impure (control flow)
        else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Async operations are impure
        else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Async in contract
          state.result.span = expr->span;
        }

        // Concurrency operations are impure
        else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Concurrency in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Concurrency in contract
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-3004";  // Concurrency in contract
          state.result.span = expr->span;
        }

        // Loop expressions are impure (may not terminate)
        else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Block expressions need checking
        else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          // Blocks may contain impure statements - conservative
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Unsafe blocks are impure
        else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }

        // Match expressions - check all arms
        else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          CheckExprPurity(node.scrutinee, state);
          if (state.has_error) return;
          for (const auto& arm : node.cases) {
            CheckExprPurity(arm.body, state);
            if (state.has_error) return;
          }
          if (node.else_expr) {
            CheckExprPurity(node.else_expr, state);
            if (state.has_error) return;
          }
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CheckExprPurity(node.scrutinee, state);
          if (state.has_error) return;
          CheckExprPurity(node.then_expr, state);
          if (state.has_error) return;
          if (node.else_expr) {
            CheckExprPurity(node.else_expr, state);
            if (state.has_error) return;
          }
        }

        // Record expressions - check field values
        else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            CheckExprPurity(field.value, state);
            if (state.has_error) return;
          }
        }

        // Enum literal expressions
        else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&state](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      CheckExprPurity(elem, state);
                      if (state.has_error) return;
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      CheckExprPurity(field.value, state);
                      if (state.has_error) return;
                    }
                  }
                },
                *node.payload_opt);
          }
        }

        // Array repeat expression
        else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          CheckExprPurity(node.value, state);
          if (state.has_error) return;
          CheckExprPurity(node.count, state);
        }

        // Attributed expression
        else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          CheckExprPurity(node.expr, state);
        }

        // Null pointer expression (pure)
        else if constexpr (std::is_same_v<T, ast::PtrNullExpr>) {
          // Ptr::null() is pure
        }

        // Catch-all for any unhandled cases - conservative
        else {
          state.has_error = true;
          state.result.ok = false;
          state.result.diag_id = "E-SEM-2802";  // Impure expression in contract predicate
          state.result.span = expr->span;
        }
      },
      expr->node);
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

ContractCheckResult CheckContractPurity(const ast::ExprPtr& expr) {
  SpecDefsContractPurity();
  SPEC_RULE("ContractPure");

  PurityCheckState state;
  CheckExprPurity(expr, state);

  if (state.has_error) {
    if (expr && !state.result.span.has_value()) {
      state.result.span = expr->span;
    }
    return state.result;
  }

  return state.result;  // ok = true by default
}

bool IsCapabilityCall(const ast::ExprPtr& expr) {
  SpecDefsContractPurity();
  SPEC_RULE("CapabilityCall");

  if (!expr) return false;

  // Check if this is a method call on a capability receiver
  if (const auto* call = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    return IsLikelyCapabilityCall(*call);
  }

  // Check if this is a call where the callee is a capability method
  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    // Check if callee is a field access on ctx
    if (const auto* field =
            std::get_if<ast::FieldAccessExpr>(&call->callee->node)) {
      if (const auto* base =
              std::get_if<ast::IdentifierExpr>(&field->base->node)) {
        return base->name == "ctx";
      }
    }
  }

  return false;
}

bool HasSideEffects(const ast::ExprPtr& expr) {
  SpecDefsContractPurity();
  SPEC_RULE("SideEffect");

  if (!expr) return false;

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        // Definitely has side effects
        if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return true;  // Allocation
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return true;  // Ownership transfer
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return true;  // Async effect
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return true;  // Async effect
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          return true;  // Concurrency effect
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return true;  // Blocking effect
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return true;  // Blocking effect
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          return true;  // Concurrency effect
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          return true;  // Concurrency effect
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return true;  // Unsafe operation
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return true;  // Unsafe block
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          // Conservative: method calls may have side effects
          return true;
        }

        return false;
      },
      expr->node);
}

bool IsPureExpression(const ast::ExprPtr& expr) {
  SpecDefsContractPurity();
  SPEC_RULE("PureExpr");

  auto result = CheckContractPurity(expr);
  return result.ok;
}

bool IsPureOperator(std::string_view op, bool is_binary) {
  SpecDefsContractPurity();
  SPEC_RULE("PureOp");

  if (is_binary) {
    return IsPureBinaryOp(op);
  } else {
    return IsPureUnaryOp(op);
  }
}

bool CheckMutationFree(const ast::ExprPtr& expr) {
  SpecDefsContractPurity();
  SPEC_RULE("MutationFree");

  if (!expr) return true;

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        // Move is a form of mutation (ownership transfer)
        if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return false;
        }

        // Method calls may mutate their receiver
        if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return false;  // Conservative
        }

        // Blocks may contain mutations
        if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return false;  // Conservative
        }

        if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return false;
        }

        // Loops may contain mutations
        if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          return false;
        }
        if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          return false;
        }
        if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          return false;
        }

        return true;
      },
      expr->node);
}

}  // namespace cursive::analysis

