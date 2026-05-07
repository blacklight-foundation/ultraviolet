// =============================================================================
// MIGRATION: item/contract_clause.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.8: Contracts
//   - Contract syntax: |: P, |: P => Q, |: => Q
//   - Precondition context
//   - Postcondition context
//   - @result, @entry intrinsics
//   - Purity constraints
//   - Verification modes (static, [[dynamic]])
//
// SOURCE: cursive-bootstrap/src/03_analysis/contracts/contract_check.cpp
//
// =============================================================================

#include "04_analysis/contracts/contract_check.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_predicates.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis
{

  namespace
  {

    // =============================================================================
    // SPEC DEFINITIONS
    // =============================================================================

    static inline void SpecDefsContractClause()
    {
      SPEC_DEF("Contract-Syntax", "5.8");
      SPEC_DEF("Contract-Pre", "5.8");
      SPEC_DEF("Contract-Post", "5.8");
      SPEC_DEF("Contract-Both", "5.8");
      SPEC_DEF("Contract-Pure", "5.8");
      SPEC_DEF("Contract-Result", "5.8");
      SPEC_DEF("Contract-Entry", "5.8");
      SPEC_DEF("Contract-Static", "5.8");
      SPEC_DEF("Contract-Dynamic", "5.8");
    }

    // =============================================================================
    // PURITY CHECK
    // =============================================================================

    // Contract predicates must be pure:
    // - MUST NOT invoke procedures with capability parameters
    // - MUST NOT mutate observable state
    // - Built-in operators on primitives are always pure

    static bool ExprIsPure(const ast::ExprPtr &expr)
    {
      if (!expr)
      {
        return true;
      }

      return std::visit(
          [](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;

            // Literals are always pure
            if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                          std::is_same_v<T, ast::PtrNullExpr>)
            {
              return true;
            }

            // Empty tuple (unit) is pure
            if constexpr (std::is_same_v<T, ast::TupleExpr>)
            {
              if (node.elements.empty())
              {
                return true;
              }
              // Non-empty tuple is pure if all elements are pure
              for (const auto &elem : node.elements)
              {
                if (!ExprIsPure(elem))
                {
                  return false;
                }
              }
              return true;
            }

            // Identifiers are pure (reading is allowed)
            if constexpr (std::is_same_v<T, ast::IdentifierExpr>)
            {
              return true;
            }

            // Binary/unary operators on pure subexpressions are pure
            if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return ExprIsPure(node.lhs) && ExprIsPure(node.rhs);
            }
            if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return ExprIsPure(node.value);
            }

            // Field/tuple access is pure
            if constexpr (std::is_same_v<T, ast::FieldAccessExpr>)
            {
              return ExprIsPure(node.base);
            }
            if constexpr (std::is_same_v<T, ast::TupleAccessExpr>)
            {
              return ExprIsPure(node.base);
            }

            // Index access is pure (if base and index are pure)
            if constexpr (std::is_same_v<T, ast::IndexAccessExpr>)
            {
              return ExprIsPure(node.base) && ExprIsPure(node.index);
            }

            // Casts are pure
            if constexpr (std::is_same_v<T, ast::CastExpr>)
            {
              return ExprIsPure(node.value);
            }

            // If/if-case with pure branches
            if constexpr (std::is_same_v<T, ast::IfExpr>)
            {
              return ExprIsPure(node.cond) &&
                     ExprIsPure(node.then_expr) &&
                     ExprIsPure(node.else_expr);
            }

            // @result is pure (reads return value)
            if constexpr (std::is_same_v<T, ast::ResultExpr>)
            {
              return true;
            }

            // @entry captures entry value - requires Bitcopy or Clone
            if constexpr (std::is_same_v<T, ast::EntryExpr>)
            {
              return ExprIsPure(node.expr);
            }

            // Array literals are pure if elements are pure
            if constexpr (std::is_same_v<T, ast::ArrayExpr>)
            {
              for (const auto &elem : node.elements)
              {
                if (!ExprIsPure(elem))
                {
                  return false;
                }
              }
              return true;
            }

            // Method calls and function calls are NOT pure by default
            // (would need to analyze callee for purity)
            if constexpr (std::is_same_v<T, ast::CallExpr> ||
                          std::is_same_v<T, ast::MethodCallExpr>)
            {
              return false;
            }

            // Allocations, moves, deref writes are NOT pure
            if constexpr (std::is_same_v<T, ast::AllocExpr> ||
                          std::is_same_v<T, ast::MoveExpr>)
            {
              return false;
            }

            // Default: not pure
            return false;
          },
          expr->node);
    }

    static ContractContext ContractPurityContext(
        const ScopeContext &ctx,
        const TypeRef &return_type,
        const TypeEnv &env,
        bool is_postcondition)
    {
      ContractContext contract_ctx;
      contract_ctx.scope_ctx = &ctx;
      contract_ctx.return_type = return_type;
      contract_ctx.is_postcondition = is_postcondition;
      for (const auto &scope : env.scopes)
      {
        for (const auto &[name, binding] : scope)
        {
          if (IdEq(name, "self"))
          {
            contract_ctx.receiver_type = binding.type;
          }
          contract_ctx.params[name] = binding.type;
        }
      }
      return contract_ctx;
    }

    // =============================================================================
    // @ENTRY TYPE CHECK
    // =============================================================================

    // @entry(expr) requires the result type to be BitcopyType or CloneType
    static bool CheckEntryExprType(const ScopeContext &ctx,
                                   const ast::ExprPtr &expr,
                                   const TypeRef &type)
    {
      if (!expr)
      {
        return true;
      }

      // Find @entry expressions and check their types
      return std::visit(
          [&](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::EntryExpr>)
            {
              // The captured expression must be Bitcopy or Clone
              return BitcopyType(ctx, type) || CloneType(ctx, type);
            }

            // Recursively check subexpressions
            if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return CheckEntryExprType(ctx, node.lhs, type) &&
                     CheckEntryExprType(ctx, node.rhs, type);
            }
            if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return CheckEntryExprType(ctx, node.value, type);
            }
            if constexpr (std::is_same_v<T, ast::IfExpr>)
            {
              return CheckEntryExprType(ctx, node.cond, type) &&
                     CheckEntryExprType(ctx, node.then_expr, type) &&
                     CheckEntryExprType(ctx, node.else_expr, type);
            }

            return true;
          },
          expr->node);
    }

    // =============================================================================
    // @RESULT CHECK
    // =============================================================================

    // @result is only valid in postcondition context
    static bool ContainsResultExpr(const ast::ExprPtr &expr)
    {
      if (!expr)
      {
        return false;
      }

      return std::visit(
          [&](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::ResultExpr>)
            {
              return true;
            }

            if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return ContainsResultExpr(node.lhs) || ContainsResultExpr(node.rhs);
            }
            if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return ContainsResultExpr(node.value);
            }
            if constexpr (std::is_same_v<T, ast::FieldAccessExpr>)
            {
              return ContainsResultExpr(node.base);
            }
            if constexpr (std::is_same_v<T, ast::TupleAccessExpr>)
            {
              return ContainsResultExpr(node.base);
            }
            if constexpr (std::is_same_v<T, ast::IndexAccessExpr>)
            {
              return ContainsResultExpr(node.base) || ContainsResultExpr(node.index);
            }
            if constexpr (std::is_same_v<T, ast::IfExpr>)
            {
              return ContainsResultExpr(node.cond) ||
                     ContainsResultExpr(node.then_expr) ||
                     ContainsResultExpr(node.else_expr);
            }
            if constexpr (std::is_same_v<T, ast::EntryExpr>)
            {
              return ContainsResultExpr(node.expr);
            }

            return false;
          },
          expr->node);
    }

  } // namespace

  // =============================================================================
  // EXPORTED: CheckContractClause
  // =============================================================================

  ContractCheckResult CheckContractClause(
      const ScopeContext &ctx,
      const ast::ContractClause &contract,
      const TypeRef &return_type,
      const TypeEnv &env,
      bool is_dynamic)
  {
    SpecDefsContractClause();
    ContractCheckResult result;
    result.ok = true;

    // Check precondition if present
    if (contract.precondition)
    {
      SPEC_RULE("Contract-Pre");

      // Precondition cannot use @result
      if (ContainsResultExpr(contract.precondition))
      {
        SPEC_RULE("Contract-Pre-Result-Err");
        result.ok = false;
        result.diag_id = "E-CON-0201";
        return result;
      }

      // Check purity
      auto purity_ctx =
          ContractPurityContext(ctx, return_type, env, false);
      auto purity = CheckPurity(purity_ctx, contract.precondition);
      if (!purity.ok)
      {
        SPEC_RULE("Contract-Pure-Err");
        result.ok = false;
        result.diag_id = purity.diag_id.value_or("E-SEM-3004");
        return result;
      }

      // Precondition must have type bool
      // (This would be checked by type expression typing)
    }

    // Check postcondition if present
    if (contract.postcondition)
    {
      SPEC_RULE("Contract-Post");

      // Check purity
      auto purity_ctx =
          ContractPurityContext(ctx, return_type, env, true);
      auto purity = CheckPurity(purity_ctx, contract.postcondition);
      if (!purity.ok)
      {
        SPEC_RULE("Contract-Pure-Err");
        result.ok = false;
        result.diag_id = purity.diag_id.value_or("E-SEM-3004");
        return result;
      }

      // Check @entry expressions have Bitcopy or Clone type
      // (Simplified: we'd need to type the subexpression first)

      // Postcondition must have type bool
      // (This would be checked by type expression typing)
    }

    // Verification mode (noted for spec tracing)
    if (is_dynamic)
    {
      SPEC_RULE("Contract-Dynamic");
      // Dynamic verification mode
    }
    else
    {
      SPEC_RULE("Contract-Static");
      // Static verification mode (default)
    }

    (void)ctx;
    (void)return_type;
    (void)env;

    SPEC_RULE("Contract-Ok");
    return result;
  }

  // =============================================================================
  // EXPORTED: CheckTypeInvariant
  // =============================================================================

  ContractCheckResult CheckTypeInvariant(
      const ScopeContext &ctx,
      const ast::TypeInvariant &invariant,
      const TypeRef &self_type)
  {
    SpecDefsContractClause();
    ContractCheckResult result;
    result.ok = true;

    if (!invariant.predicate)
    {
      return result;
    }

    // Type invariant cannot use @result
    if (ContainsResultExpr(invariant.predicate))
    {
      SPEC_RULE("TypeInvariant-Result-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2854";
      return result;
    }

    // Check purity
    if (!ExprIsPure(invariant.predicate))
    {
      SPEC_RULE("TypeInvariant-Pure-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3004";
      return result;
    }

    (void)ctx;
    (void)self_type;

    SPEC_RULE("TypeInvariant-Ok");
    return result;
  }

  // =============================================================================
  // EXPORTED: CheckLoopInvariant
  // =============================================================================

  ContractCheckResult CheckLoopInvariant(
      const ScopeContext &ctx,
      const ast::ExprPtr &invariant,
      const TypeEnv &env)
  {
    SpecDefsContractClause();
    ContractCheckResult result;
    result.ok = true;

    if (!invariant)
    {
      return result;
    }

    // Loop invariant cannot use @result
    if (ContainsResultExpr(invariant))
    {
      SPEC_RULE("LoopInvariant-Result-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2854";
      return result;
    }

    // Check purity
    if (!ExprIsPure(invariant))
    {
      SPEC_RULE("LoopInvariant-Pure-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3004";
      return result;
    }

    (void)ctx;
    (void)env;

    SPEC_RULE("LoopInvariant-Ok");
    return result;
  }

} // namespace cursive::analysis
