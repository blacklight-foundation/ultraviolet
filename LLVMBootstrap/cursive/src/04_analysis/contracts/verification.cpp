/*
 * =============================================================================
 * MIGRATION MAPPING: verification.cpp
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 14.8 "Static Verification" (lines 23610-23750)
 *   - CursiveSpecification.md, Section 14.8.1 "Proof Obligations" (lines 23620-23680)
 *   - CursiveSpecification.md, Section 14.8.2 "Verification Modes" (lines 23690-23750)
 *   - CursiveSpecification.md, Section 8.11 "E-VER Errors" (lines 22000-22100)
 *
 * SOURCE FILE:
 *   - cursive-bootstrap/src/03_analysis/contracts/verification.cpp (lines 1-1109)
 *
 * FUNCTIONS TO MIGRATE:
 *   - StaticProof(Contract* contract, Context* ctx) -> ProofResult [lines 50-150]
 *       Attempt to statically prove contract holds
 *   - EntTrue(Expr* expr, Context* ctx) -> bool                    [lines 155-250]
 *       Entailment: context entails expression is true
 *   - EntFact(Fact* fact, Context* ctx) -> bool                    [lines 255-350]
 *       Add fact to verification context
 *   - EntAnd(Expr* lhs, Expr* rhs, Context* ctx) -> bool           [lines 355-450]
 *       Entailment for conjunction
 *   - EntOr(Expr* lhs, Expr* rhs, Context* ctx) -> bool            [lines 455-550]
 *       Entailment for disjunction
 *   - EntLinear(Expr* linear, Context* ctx) -> bool                [lines 555-700]
 *       Linear arithmetic entailment
 *   - BuildVerificationContext(Proc* proc) -> Context              [lines 705-850]
 *       Build initial context from preconditions
 *   - PropagateAssumptions(Stmt* stmt, Context* ctx) -> Context    [lines 855-1000]
 *       Propagate assumptions through control flow
 *   - CheckPostconditionProof(Proc* proc, Context* ctx) -> bool    [lines 1005-1109]
 *       Verify postcondition holds at all return points
 *
 * DEPENDENCIES:
 *   - Contract AST nodes
 *   - Verification context
 *   - Linear arithmetic solver
 *   - Control flow graph
 *
 * REFACTORING NOTES:
 *   1. STATIC VERIFICATION IS DEFAULT - contracts must be provable
 *   2. If contract cannot be statically proven, program is ILL-FORMED
 *   3. [[dynamic]] attribute enables runtime verification instead
 *   4. Verification context accumulates known facts
 *   5. Control flow (if/if-case) creates branching contexts
 *   6. Linear arithmetic (comparisons, bounds) has decision procedure
 *   7. Consider SMT solver integration for complex contracts
 *   8. This is a large file - consider splitting:
 *      - entailment.cpp: Core entailment logic
 *      - linear.cpp: Linear arithmetic
 *      - context.cpp: Verification context management
 *      - proof.cpp: Proof search and generation
 *
 * VERIFICATION MODES:
 *   - [[static]] (default): Must prove at compile time
 *   - [[dynamic]]: Generate runtime checks
 *
 * DIAGNOSTIC CODES:
 *   - E-VER-0001: Contract cannot be statically verified
 *   - E-VER-0002: Postcondition not established
 *   - E-VER-0003: Precondition may not hold at call site
 *   - W-VER-0001: Using [[dynamic]] verification
 *
 * =============================================================================
 */

#include "04_analysis/contracts/verification.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include "00_core/assert_spec.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsVerification() {
  SPEC_DEF("StaticProof", "C0X.5.W");
  SPEC_DEF("Entailment", "C0X.5.W");
  SPEC_DEF("VerificationFact", "C0X.5.W");
  SPEC_DEF("Ent-True", "C0X.5.W");
  SPEC_DEF("Ent-Fact", "C0X.5.W");
  SPEC_DEF("Ent-And", "C0X.5.W");
  SPEC_DEF("Ent-Or", "C0X.5.W");
  SPEC_DEF("Ent-Linear", "C0X.5.W");
}

static ast::ExprPtr MakeExprNode(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static std::optional<std::string> NegatedComparisonOperator(
    std::string_view op) {
  if (op == "<") {
    return std::string(">=");
  }
  if (op == "<=") {
    return std::string(">");
  }
  if (op == ">") {
    return std::string("<=");
  }
  if (op == ">=") {
    return std::string("<");
  }
  if (op == "==") {
    return std::string("!=");
  }
  if (op == "!=") {
    return std::string("==");
  }
  return std::nullopt;
}

// Check if expression is a literal true
bool IsLiteralTrue(const ast::ExprPtr& expr) {
  if (!expr) return false;
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    return lit->literal.kind == ast::TokenKind::BoolLiteral &&
           lit->literal.lexeme == "true";
  }
  return false;
}

// Check if expression is a literal false
bool IsLiteralFalse(const ast::ExprPtr& expr) {
  if (!expr) return false;
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    return lit->literal.kind == ast::TokenKind::BoolLiteral &&
           lit->literal.lexeme == "false";
  }
  return false;
}

// Simple structural equality of expressions
bool ExprStructEqualInternal(const ast::ExprPtr& a,
                             const ast::ExprPtr& b) {
  if (!a && !b) return true;
  if (!a || !b) return false;

  return std::visit(
      [&](const auto& node_a) -> bool {
        using T = std::decay_t<decltype(node_a)>;
        const auto* node_b = std::get_if<T>(&b->node);
        if (!node_b) return false;

        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return node_a.literal.kind == node_b->literal.kind &&
                 node_a.literal.lexeme == node_b->literal.lexeme;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return node_a.name == node_b->name;
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return node_a.path == node_b->path && node_a.name == node_b->name;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return node_a.path == node_b->path && node_a.name == node_b->name;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return node_a.op == node_b->op &&
                 ExprStructEqualInternal(node_a.lhs, node_b->lhs) &&
                 ExprStructEqualInternal(node_a.rhs, node_b->rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return node_a.op == node_b->op &&
                 ExprStructEqualInternal(node_a.value, node_b->value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return node_a.name == node_b->name &&
                 ExprStructEqualInternal(node_a.base, node_b->base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ast::TupleIndexEqual(node_a.index, node_b->index) &&
                 ExprStructEqualInternal(node_a.base, node_b->base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprStructEqualInternal(node_a.base, node_b->base) &&
                 ExprStructEqualInternal(node_a.index, node_b->index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (!ExprStructEqualInternal(node_a.callee, node_b->callee)) {
            return false;
          }
          if (node_a.args.size() != node_b->args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node_a.args.size(); ++i) {
            if (node_a.args[i].moved != node_b->args[i].moved) {
              return false;
            }
            if (!ExprStructEqualInternal(node_a.args[i].value,
                                         node_b->args[i].value)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (node_a.path != node_b->path || node_a.name != node_b->name) {
            return false;
          }
          if (node_a.args.index() != node_b->args.index()) {
            return false;
          }
          if (std::holds_alternative<ast::ParenArgs>(node_a.args)) {
            const auto& lhs = std::get<ast::ParenArgs>(node_a.args);
            const auto& rhs = std::get<ast::ParenArgs>(node_b->args);
            if (lhs.args.size() != rhs.args.size()) {
              return false;
            }
            for (std::size_t i = 0; i < lhs.args.size(); ++i) {
              if (lhs.args[i].moved != rhs.args[i].moved) {
                return false;
              }
              if (!ExprStructEqualInternal(lhs.args[i].value,
                                           rhs.args[i].value)) {
                return false;
              }
            }
            return true;
          }
          const auto& lhs = std::get<ast::BraceArgs>(node_a.args);
          const auto& rhs = std::get<ast::BraceArgs>(node_b->args);
          if (lhs.fields.size() != rhs.fields.size()) {
            return false;
          }
          for (std::size_t i = 0; i < lhs.fields.size(); ++i) {
            if (lhs.fields[i].name != rhs.fields[i].name) {
              return false;
            }
            if (!ExprStructEqualInternal(lhs.fields[i].value,
                                         rhs.fields[i].value)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (node_a.name != node_b->name) {
            return false;
          }
          if (!ExprStructEqualInternal(node_a.receiver, node_b->receiver)) {
            return false;
          }
          if (node_a.args.size() != node_b->args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node_a.args.size(); ++i) {
            if (node_a.args[i].moved != node_b->args[i].moved) {
              return false;
            }
            if (!ExprStructEqualInternal(node_a.args[i].value,
                                         node_b->args[i].value)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprStructEqualInternal(node_a.expr, node_b->expr);
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          return true;
        }
        return false;
      },
      a->node);
}

struct LinearExpr {
  std::map<std::string, std::int64_t> terms;
  std::int64_t constant = 0;
};

struct LinearConstraint {
  std::map<std::string, std::int64_t> coeffs;
  std::int64_t rhs = 0;  // Sum(coeff_i * x_i) <= rhs
};

enum class ComparisonOp {
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
};

struct ParsedLinearComparison {
  bool ok = false;
  ComparisonOp op = ComparisonOp::Eq;
  LinearExpr diff;  // lhs - rhs
};

enum class SatStatus {
  Unsat,
  Sat,
  Unknown,
};

static bool CheckedAdd(std::int64_t a, std::int64_t b, std::int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_add_overflow(a, b, &out);
#else
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
      (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
    return false;
  }
  out = a + b;
  return true;
#endif
}

static bool CheckedSub(std::int64_t a, std::int64_t b, std::int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_sub_overflow(a, b, &out);
#else
  if ((b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
      (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)) {
    return false;
  }
  out = a - b;
  return true;
#endif
}

static bool CheckedMul(std::int64_t a, std::int64_t b, std::int64_t& out) {
#if defined(__GNUC__) || defined(__clang__)
  return !__builtin_mul_overflow(a, b, &out);
#else
  if (a == 0 || b == 0) {
    out = 0;
    return true;
  }
  if (a == -1 && b == std::numeric_limits<std::int64_t>::min()) {
    return false;
  }
  if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) {
    return false;
  }
  out = a * b;
  return (out / b) == a;
#endif
}

static bool AddTerm(LinearExpr& expr,
                    const std::string& var,
                    std::int64_t coeff) {
  if (coeff == 0) {
    return true;
  }
  auto it = expr.terms.find(var);
  if (it == expr.terms.end()) {
    expr.terms.emplace(var, coeff);
    return true;
  }
  std::int64_t sum = 0;
  if (!CheckedAdd(it->second, coeff, sum)) {
    return false;
  }
  if (sum == 0) {
    expr.terms.erase(it);
    return true;
  }
  it->second = sum;
  return true;
}

static bool MergeLinear(LinearExpr& dst,
                        const LinearExpr& src,
                        std::int64_t scale) {
  std::int64_t scaled_const = 0;
  if (!CheckedMul(src.constant, scale, scaled_const)) {
    return false;
  }
  if (!CheckedAdd(dst.constant, scaled_const, dst.constant)) {
    return false;
  }
  for (const auto& [var, coeff] : src.terms) {
    std::int64_t scaled_coeff = 0;
    if (!CheckedMul(coeff, scale, scaled_coeff)) {
      return false;
    }
    if (!AddTerm(dst, var, scaled_coeff)) {
      return false;
    }
  }
  return true;
}

static std::optional<std::int64_t> ParseIntLiteral(std::string_view lexeme) {
  std::string cleaned;
  cleaned.reserve(lexeme.size());
  for (const char ch : lexeme) {
    if (ch != '_') {
      cleaned.push_back(ch);
    }
  }
  try {
    std::size_t idx = 0;
    const auto value = std::stoll(cleaned, &idx, 0);
    if (idx != cleaned.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

static std::optional<std::string> ExprKey(const ast::ExprPtr& expr);

static void AppendPathKey(std::string& out,
                          const std::vector<std::string>& path,
                          std::string_view name) {
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      out.append("::");
    }
    out.append(path[i]);
  }
  if (!path.empty()) {
    out.append("::");
  }
  out.append(name);
}

static std::optional<std::string> ExprArgumentKey(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    return lit->literal.lexeme;
  }
  return ExprKey(expr);
}

static bool AppendParenArgKeys(std::string& out,
                               const std::vector<ast::Arg>& args) {
  out.push_back('(');
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    if (args[i].moved) {
      return false;
    }
    const auto arg_key = ExprArgumentKey(args[i].value);
    if (!arg_key.has_value()) {
      return false;
    }
    out.append(*arg_key);
  }
  out.push_back(')');
  return true;
}

static std::optional<std::string> ExprKey(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&expr->node)) {
    std::string out;
    AppendPathKey(out, path->path, path->name);
    return out;
  }
  if (const auto* qual = std::get_if<ast::QualifiedNameExpr>(&expr->node)) {
    std::string out;
    AppendPathKey(out, qual->path, qual->name);
    return out;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    const auto base_key = ExprKey(field->base);
    if (!base_key.has_value()) {
      return std::nullopt;
    }
    std::string out = *base_key;
    out.push_back('.');
    out.append(field->name);
    return out;
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    const auto base_key = ExprKey(tuple->base);
    if (!base_key.has_value()) {
      return std::nullopt;
    }
    std::string out = *base_key;
    out.push_back('.');
    out.append(ast::FormatTupleIndex(tuple->index));
    return out;
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    const auto base_key = ExprKey(index->base);
    if (!base_key.has_value()) {
      return std::nullopt;
    }
    if (!index->index) {
      return std::nullopt;
    }
    const auto* lit = std::get_if<ast::LiteralExpr>(&index->index->node);
    if (!lit || lit->literal.kind != ast::TokenKind::IntLiteral) {
      return std::nullopt;
    }
    const auto parsed = ParseIntLiteral(lit->literal.lexeme);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    std::string out = *base_key;
    out.push_back('[');
    out.append(std::to_string(*parsed));
    out.push_back(']');
    return out;
  }
  if (const auto* entry = std::get_if<ast::EntryExpr>(&expr->node)) {
    if (!entry->expr) {
      return std::nullopt;
    }
    const auto inner_key = ExprKey(entry->expr);
    if (!inner_key.has_value()) {
      return std::nullopt;
    }
    std::string out = "@entry(";
    out.append(*inner_key);
    out.push_back(')');
    return out;
  }
  if (std::holds_alternative<ast::ResultExpr>(expr->node)) {
    return std::string("@result");
  }
  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    const auto callee_key = ExprKey(call->callee);
    if (!callee_key.has_value()) {
      return std::nullopt;
    }
    std::string out = *callee_key;
    if (!AppendParenArgKeys(out, call->args)) {
      return std::nullopt;
    }
    return out;
  }
  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    const auto receiver_key = ExprKey(method->receiver);
    if (!receiver_key.has_value()) {
      return std::nullopt;
    }
    std::string out = *receiver_key;
    out.append("~>");
    out.append(method->name);
    if (!AppendParenArgKeys(out, method->args)) {
      return std::nullopt;
    }
    return out;
  }
  if (const auto* apply = std::get_if<ast::QualifiedApplyExpr>(&expr->node)) {
    if (!std::holds_alternative<ast::ParenArgs>(apply->args)) {
      return std::nullopt;
    }
    std::string out;
    AppendPathKey(out, apply->path, apply->name);
    if (!AppendParenArgKeys(out, std::get<ast::ParenArgs>(apply->args).args)) {
      return std::nullopt;
    }
    return out;
  }
  return std::nullopt;
}

static bool LinearizeExpr(const ast::ExprPtr& expr, LinearExpr& out) {
  if (!expr) {
    return false;
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    if (lit->literal.kind != ast::TokenKind::IntLiteral) {
      return false;
    }
    const auto parsed = ParseIntLiteral(lit->literal.lexeme);
    if (!parsed.has_value()) {
      return false;
    }
    out.constant = *parsed;
    return true;
  }

  if (const auto key = ExprKey(expr); key.has_value()) {
    if (!AddTerm(out, *key, 1)) {
      return false;
    }
    return true;
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    if (unary->op != "-" && unary->op != "+") {
      return false;
    }
    LinearExpr inner;
    if (!LinearizeExpr(unary->value, inner)) {
      return false;
    }
    const std::int64_t scale = (unary->op == "-") ? -1 : 1;
    if (!MergeLinear(out, inner, scale)) {
      return false;
    }
    return true;
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    if (binary->op == "+" || binary->op == "-") {
      LinearExpr left;
      LinearExpr right;
      if (!LinearizeExpr(binary->lhs, left) ||
          !LinearizeExpr(binary->rhs, right)) {
        return false;
      }
      if (!MergeLinear(out, left, 1)) {
        return false;
      }
      const std::int64_t scale = (binary->op == "-") ? -1 : 1;
      if (!MergeLinear(out, right, scale)) {
        return false;
      }
      return true;
    }
    if (binary->op == "*") {
      LinearExpr left;
      LinearExpr right;
      if (!LinearizeExpr(binary->lhs, left) ||
          !LinearizeExpr(binary->rhs, right)) {
        return false;
      }
      if (!left.terms.empty() && !right.terms.empty()) {
        return false;
      }
      if (left.terms.empty()) {
        if (!MergeLinear(out, right, left.constant)) {
          return false;
        }
        return true;
      }
      if (!MergeLinear(out, left, right.constant)) {
        return false;
      }
      return true;
    }
  }

  return false;
}

static bool NegateLinear(LinearExpr& expr) {
  if (!CheckedMul(expr.constant, -1, expr.constant)) {
    return false;
  }
  for (auto& [var, coeff] : expr.terms) {
    if (!CheckedMul(coeff, -1, coeff)) {
      return false;
    }
  }
  return true;
}

static bool EvaluateComparison(std::int64_t lhs,
                               std::int64_t rhs,
                               ComparisonOp op,
                               bool& value) {
  if (op == ComparisonOp::Eq) {
    value = lhs == rhs;
    return true;
  }
  if (op == ComparisonOp::Ne) {
    value = lhs != rhs;
    return true;
  }
  if (op == ComparisonOp::Lt) {
    value = lhs < rhs;
    return true;
  }
  if (op == ComparisonOp::Le) {
    value = lhs <= rhs;
    return true;
  }
  if (op == ComparisonOp::Gt) {
    value = lhs > rhs;
    return true;
  }
  if (op == ComparisonOp::Ge) {
    value = lhs >= rhs;
    return true;
  }
  return false;
}

static bool ParseComparisonOp(std::string_view op, ComparisonOp& out) {
  if (op == "==") {
    out = ComparisonOp::Eq;
    return true;
  }
  if (op == "!=") {
    out = ComparisonOp::Ne;
    return true;
  }
  if (op == "<") {
    out = ComparisonOp::Lt;
    return true;
  }
  if (op == "<=") {
    out = ComparisonOp::Le;
    return true;
  }
  if (op == ">") {
    out = ComparisonOp::Gt;
    return true;
  }
  if (op == ">=") {
    out = ComparisonOp::Ge;
    return true;
  }
  return false;
}

static bool MakeInequality(const LinearExpr& expr,
                           std::int64_t bound,
                           LinearConstraint& out) {
  out.coeffs = expr.terms;
  return CheckedSub(bound, expr.constant, out.rhs);
}

static bool ParseLinearComparison(const ast::ExprPtr& expr,
                                  ParsedLinearComparison& out) {
  const auto* binary = expr ? std::get_if<ast::BinaryExpr>(&expr->node) : nullptr;
  if (!binary) {
    return false;
  }
  ComparisonOp cmp = ComparisonOp::Eq;
  if (!ParseComparisonOp(binary->op, cmp)) {
    return false;
  }

  LinearExpr lhs;
  LinearExpr rhs;
  if (!LinearizeExpr(binary->lhs, lhs) || !LinearizeExpr(binary->rhs, rhs)) {
    return false;
  }
  if (!MergeLinear(lhs, rhs, -1)) {
    return false;
  }

  out.ok = true;
  out.op = cmp;
  out.diff = std::move(lhs);
  return true;
}

static bool AppendConjunctionConstraints(const ParsedLinearComparison& pred,
                                         std::vector<LinearConstraint>& out) {
  LinearConstraint ineq{};
  LinearExpr diff = pred.diff;
  switch (pred.op) {
    case ComparisonOp::Le:
      if (!MakeInequality(diff, 0, ineq)) {
        return false;
      }
      out.push_back(std::move(ineq));
      return true;
    case ComparisonOp::Lt:
      if (!MakeInequality(diff, -1, ineq)) {
        return false;
      }
      out.push_back(std::move(ineq));
      return true;
    case ComparisonOp::Ge:
      if (!NegateLinear(diff) || !MakeInequality(diff, 0, ineq)) {
        return false;
      }
      out.push_back(std::move(ineq));
      return true;
    case ComparisonOp::Gt:
      if (!NegateLinear(diff) || !MakeInequality(diff, -1, ineq)) {
        return false;
      }
      out.push_back(std::move(ineq));
      return true;
    case ComparisonOp::Eq: {
      LinearConstraint ineq_a{};
      if (!MakeInequality(diff, 0, ineq_a)) {
        return false;
      }
      out.push_back(std::move(ineq_a));
      if (!NegateLinear(diff)) {
        return false;
      }
      LinearConstraint ineq_b{};
      if (!MakeInequality(diff, 0, ineq_b)) {
        return false;
      }
      out.push_back(std::move(ineq_b));
      return true;
    }
    case ComparisonOp::Ne:
      // Disjunctive fact (x != y) is not representable as a single conjunction.
      // We conservatively skip it in the linear fact set.
      return true;
  }
  return false;
}

static bool BuildNegatedAlternatives(const ParsedLinearComparison& pred,
                                     std::vector<std::vector<LinearConstraint>>& out) {
  out.clear();
  auto push_single = [&](LinearExpr expr, std::int64_t bound) -> bool {
    LinearConstraint ineq{};
    if (!MakeInequality(expr, bound, ineq)) {
      return false;
    }
    out.push_back({std::move(ineq)});
    return true;
  };

  LinearExpr diff = pred.diff;
  switch (pred.op) {
    case ComparisonOp::Le:
      if (!NegateLinear(diff)) {
        return false;
      }
      return push_single(diff, -1);
    case ComparisonOp::Lt:
      if (!NegateLinear(diff)) {
        return false;
      }
      return push_single(diff, 0);
    case ComparisonOp::Ge:
      return push_single(diff, -1);
    case ComparisonOp::Gt:
      return push_single(diff, 0);
    case ComparisonOp::Eq: {
      LinearExpr alt_a = diff;
      if (!push_single(alt_a, -1)) {
        return false;
      }
      LinearExpr alt_b = pred.diff;
      if (!NegateLinear(alt_b)) {
        return false;
      }
      return push_single(alt_b, -1);
    }
    case ComparisonOp::Ne: {
      std::vector<LinearConstraint> eq_constraints;
      if (!AppendConjunctionConstraints(
              ParsedLinearComparison{true, ComparisonOp::Eq, pred.diff},
              eq_constraints)) {
        return false;
      }
      out.push_back(std::move(eq_constraints));
      return true;
    }
  }
  return false;
}

static void CollectLinearFacts(const ast::ExprPtr& expr,
                               std::vector<LinearConstraint>& facts,
                               bool& contradiction) {
  if (!expr || contradiction) {
    return;
  }

  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    if (lit->literal.kind == ast::TokenKind::BoolLiteral &&
        lit->literal.lexeme == "false") {
      contradiction = true;
    }
    return;
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node);
      binary && binary->op == "&&") {
    CollectLinearFacts(binary->lhs, facts, contradiction);
    CollectLinearFacts(binary->rhs, facts, contradiction);
    return;
  }

  ParsedLinearComparison pred{};
  if (!ParseLinearComparison(expr, pred)) {
    return;
  }

  if (pred.diff.terms.empty()) {
    bool value = false;
    if (EvaluateComparison(pred.diff.constant, 0, pred.op, value) && !value) {
      contradiction = true;
    }
    return;
  }

  std::vector<LinearConstraint> conj;
  if (!AppendConjunctionConstraints(pred, conj)) {
    return;
  }
  facts.insert(facts.end(), conj.begin(), conj.end());
}

class LPSolver {
 public:
  static constexpr long double kEps = 1e-12L;
  static constexpr long double kInf = 1e100L;

  LPSolver(const std::vector<std::vector<long double>>& A,
           const std::vector<long double>& b,
           const std::vector<long double>& c)
      : m_(static_cast<int>(b.size())),
        n_(static_cast<int>(c.size())),
        basis_(m_),
        non_basis_(n_ + 1),
        tableau_(m_ + 2, std::vector<long double>(n_ + 2, 0.0L)) {
    for (int i = 0; i < m_; ++i) {
      for (int j = 0; j < n_; ++j) {
        tableau_[i][j] = A[i][j];
      }
      basis_[i] = n_ + i;
      tableau_[i][n_] = -1.0L;
      tableau_[i][n_ + 1] = b[i];
    }
    for (int j = 0; j < n_; ++j) {
      non_basis_[j] = j;
      tableau_[m_][j] = -c[j];
    }
    non_basis_[n_] = -1;
    tableau_[m_ + 1][n_] = 1.0L;
  }

  // Returns std::nullopt if infeasible. Otherwise returns one feasible point.
  std::optional<std::vector<long double>> Solve(long double& optimum) {
    int r = 0;
    for (int i = 1; i < m_; ++i) {
      if (tableau_[i][n_ + 1] < tableau_[r][n_ + 1]) {
        r = i;
      }
    }
    if (tableau_[r][n_ + 1] < -kEps) {
      Pivot(r, n_);
      if (!Simplex(/*phase=*/1) || tableau_[m_ + 1][n_ + 1] < -kEps) {
        return std::nullopt;
      }
      if (tableau_[m_ + 1][n_ + 1] > kEps) {
        return std::nullopt;
      }
      auto it = std::find(basis_.begin(), basis_.end(), -1);
      if (it != basis_.end()) {
        r = static_cast<int>(it - basis_.begin());
        int s = 0;
        for (int j = 1; j <= n_; ++j) {
          if (std::fabs(tableau_[r][j]) > std::fabs(tableau_[r][s]) + kEps ||
              (std::fabs(tableau_[r][j] - tableau_[r][s]) <= kEps &&
               non_basis_[j] < non_basis_[s])) {
            s = j;
          }
        }
        Pivot(r, s);
      }
    }

    if (!Simplex(/*phase=*/2)) {
      // Unbounded objective still yields feasibility for c = 0.
      optimum = kInf;
      std::vector<long double> x(n_, 0.0L);
      for (int i = 0; i < m_; ++i) {
        if (basis_[i] >= 0 && basis_[i] < n_) {
          x[basis_[i]] = tableau_[i][n_ + 1];
        }
      }
      return x;
    }

    std::vector<long double> x(n_, 0.0L);
    for (int i = 0; i < m_; ++i) {
      if (basis_[i] >= 0 && basis_[i] < n_) {
        x[basis_[i]] = tableau_[i][n_ + 1];
      }
    }
    optimum = tableau_[m_][n_ + 1];
    return x;
  }

 private:
  void Pivot(int r, int s) {
    const long double inv = 1.0L / tableau_[r][s];
    for (int i = 0; i < m_ + 2; ++i) {
      if (i == r) {
        continue;
      }
      for (int j = 0; j < n_ + 2; ++j) {
        if (j == s) {
          continue;
        }
        tableau_[i][j] -= tableau_[r][j] * tableau_[i][s] * inv;
      }
    }
    for (int j = 0; j < n_ + 2; ++j) {
      if (j != s) {
        tableau_[r][j] *= inv;
      }
    }
    for (int i = 0; i < m_ + 2; ++i) {
      if (i != r) {
        tableau_[i][s] *= -inv;
      }
    }
    tableau_[r][s] = inv;
    std::swap(basis_[r], non_basis_[s]);
  }

  bool Simplex(int phase) {
    const int x = (phase == 1) ? m_ + 1 : m_;
    while (true) {
      int s = -1;
      for (int j = 0; j <= n_; ++j) {
        if (phase == 2 && non_basis_[j] == -1) {
          continue;
        }
        if (s == -1 ||
            tableau_[x][j] < tableau_[x][s] - kEps ||
            (std::fabs(tableau_[x][j] - tableau_[x][s]) <= kEps &&
             non_basis_[j] < non_basis_[s])) {
          s = j;
        }
      }
      if (s == -1 || tableau_[x][s] >= -kEps) {
        return true;
      }

      int r = -1;
      for (int i = 0; i < m_; ++i) {
        if (tableau_[i][s] <= kEps) {
          continue;
        }
        if (r == -1) {
          r = i;
          continue;
        }
        const long double lhs = tableau_[i][n_ + 1] / tableau_[i][s];
        const long double rhs = tableau_[r][n_ + 1] / tableau_[r][s];
        if (lhs < rhs - kEps ||
            (std::fabs(lhs - rhs) <= kEps && basis_[i] < basis_[r])) {
          r = i;
        }
      }
      if (r == -1) {
        return false;
      }
      Pivot(r, s);
    }
  }

  int m_ = 0;
  int n_ = 0;
  std::vector<int> basis_;
  std::vector<int> non_basis_;
  std::vector<std::vector<long double>> tableau_;
};

static bool IsAlmostInteger(long double v) {
  const long double rounded = std::round(v);
  return std::fabs(v - rounded) <= 1e-9L;
}

static bool LongDoubleToInt64(long double v, std::int64_t& out) {
  if (!std::isfinite(v)) {
    return false;
  }
  if (v < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
      v > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  out = static_cast<std::int64_t>(v);
  return true;
}

static SatStatus SolveLPRelaxation(
    const std::vector<LinearConstraint>& constraints,
    const std::vector<std::string>& vars,
    std::vector<long double>& values) {
  if (vars.empty()) {
    for (const auto& c : constraints) {
      if (0 > c.rhs) {
        return SatStatus::Unsat;
      }
    }
    values.clear();
    return SatStatus::Sat;
  }

  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(vars.size());
  for (std::size_t i = 0; i < vars.size(); ++i) {
    indices.emplace(vars[i], i);
  }

  const int m = static_cast<int>(constraints.size());
  const int n = static_cast<int>(vars.size() * 2);
  std::vector<std::vector<long double>> A(
      m, std::vector<long double>(n, 0.0L));
  std::vector<long double> b(m, 0.0L);
  std::vector<long double> c(n, 0.0L);

  for (int row = 0; row < m; ++row) {
    b[row] = static_cast<long double>(constraints[row].rhs);
    for (const auto& [name, coeff] : constraints[row].coeffs) {
      const auto it = indices.find(name);
      if (it == indices.end()) {
        continue;
      }
      const int col = static_cast<int>(it->second * 2);
      A[row][col] += static_cast<long double>(coeff);
      A[row][col + 1] -= static_cast<long double>(coeff);
    }
  }

  LPSolver solver(A, b, c);
  long double optimum = 0.0L;
  const auto lp_values = solver.Solve(optimum);
  if (!lp_values.has_value()) {
    return SatStatus::Unsat;
  }

  values.assign(vars.size(), 0.0L);
  for (std::size_t i = 0; i < vars.size(); ++i) {
    const long double pos = (*lp_values)[i * 2];
    const long double neg = (*lp_values)[i * 2 + 1];
    values[i] = pos - neg;
  }
  return SatStatus::Sat;
}

static SatStatus SolveIntegerFeasibility(
    const std::vector<LinearConstraint>& constraints,
    const std::vector<std::string>& vars,
    std::size_t& nodes_visited) {
  constexpr std::size_t kMaxBranchNodes = 20000;
  if (nodes_visited++ >= kMaxBranchNodes) {
    return SatStatus::Unknown;
  }

  std::vector<long double> lp_values;
  const SatStatus relax = SolveLPRelaxation(constraints, vars, lp_values);
  if (relax == SatStatus::Unsat) {
    return SatStatus::Unsat;
  }
  if (relax == SatStatus::Unknown) {
    return SatStatus::Unknown;
  }

  std::size_t frac_index = vars.size();
  for (std::size_t i = 0; i < lp_values.size(); ++i) {
    if (!IsAlmostInteger(lp_values[i])) {
      frac_index = i;
      break;
    }
  }
  if (frac_index == vars.size()) {
    return SatStatus::Sat;
  }

  const long double value = lp_values[frac_index];
  const long double floor_ld = std::floor(value);
  const long double ceil_ld = std::ceil(value);

  std::int64_t floor_i = 0;
  std::int64_t ceil_i = 0;
  if (!LongDoubleToInt64(floor_ld, floor_i) ||
      !LongDoubleToInt64(ceil_ld, ceil_i)) {
    return SatStatus::Unknown;
  }

  const std::string& split_var = vars[frac_index];
  bool saw_unknown = false;

  std::vector<LinearConstraint> left = constraints;
  LinearConstraint upper{};
  upper.coeffs.emplace(split_var, 1);
  upper.rhs = floor_i;
  left.push_back(std::move(upper));
  const SatStatus left_status =
      SolveIntegerFeasibility(left, vars, nodes_visited);
  if (left_status == SatStatus::Sat) {
    return SatStatus::Sat;
  }
  if (left_status == SatStatus::Unknown) {
    saw_unknown = true;
  }

  std::vector<LinearConstraint> right = constraints;
  LinearConstraint lower{};
  lower.coeffs.emplace(split_var, -1);
  if (!CheckedMul(ceil_i, -1, lower.rhs)) {
    return SatStatus::Unknown;
  }
  right.push_back(std::move(lower));
  const SatStatus right_status =
      SolveIntegerFeasibility(right, vars, nodes_visited);
  if (right_status == SatStatus::Sat) {
    return SatStatus::Sat;
  }
  if (right_status == SatStatus::Unknown) {
    saw_unknown = true;
  }

  if (saw_unknown) {
    return SatStatus::Unknown;
  }
  return SatStatus::Unsat;
}

static SatStatus IsSatisfiable(const std::vector<LinearConstraint>& constraints) {
  std::set<std::string> var_set;
  for (const auto& c : constraints) {
    for (const auto& [name, _] : c.coeffs) {
      var_set.insert(name);
    }
  }
  std::vector<std::string> vars(var_set.begin(), var_set.end());
  std::size_t nodes_visited = 0;
  return SolveIntegerFeasibility(constraints, vars, nodes_visited);
}

static bool EntailsParsedComparison(const std::vector<LinearConstraint>& facts,
                                    const ParsedLinearComparison& target) {
  if (target.diff.terms.empty()) {
    bool value = false;
    return EvaluateComparison(target.diff.constant, 0, target.op, value) && value;
  }

  std::vector<std::vector<LinearConstraint>> negated_alts;
  if (!BuildNegatedAlternatives(target, negated_alts) || negated_alts.empty()) {
    return false;
  }

  for (const auto& alt : negated_alts) {
    std::vector<LinearConstraint> combined = facts;
    combined.insert(combined.end(), alt.begin(), alt.end());
    const SatStatus sat = IsSatisfiable(combined);
    if (sat == SatStatus::Sat || sat == SatStatus::Unknown) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ExprStructEqual(const ast::ExprPtr& a, const ast::ExprPtr& b) {
  return ExprStructEqualInternal(a, b);
}

static bool EntFactAt(const StaticProofContext& ctx,
                      const core::Span& location,
                      const ast::ExprPtr& expr);
static bool EntAndAt(const StaticProofContext& ctx,
                     const core::Span& location,
                     const ast::ExprPtr& left,
                     const ast::ExprPtr& right);
static bool EntOrAt(const StaticProofContext& ctx,
                    const core::Span& location,
                    const ast::ExprPtr& left,
                    const ast::ExprPtr& right);
static bool EntLinearAt(const StaticProofContext& ctx,
                        const core::Span& location,
                        const ast::ExprPtr& expr);

StaticProofResult StaticProofAt(
    const StaticProofContext& ctx,
    const core::Span& location,
    const ast::ExprPtr& predicate) {
  SpecDefsVerification();
  SPEC_RULE("StaticProof");

  StaticProofResult result;

  // Try each entailment rule

  // Ent-True
  if (EntTrue(predicate)) {
    result.provable = true;
    result.explanation = "Trivially true";
    return result;
  }

  const auto const_eval = EvaluateConstant(predicate);
  if (const_eval.known && const_eval.is_bool) {
    if (const_eval.bool_value) {
      result.provable = true;
      result.explanation = "Constant evaluation";
      return result;
    }
    result.provable = false;
    return result;
  }

  // Ent-Fact
  if (EntFactAt(ctx, location, predicate)) {
    result.provable = true;
    result.explanation = "Matches known fact";
    return result;
  }

  // Ent-And
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&predicate->node)) {
    if (binary->op == "&&") {
      if (EntAndAt(ctx, location, binary->lhs, binary->rhs)) {
        result.provable = true;
        result.explanation = "Both conjuncts provable";
        return result;
      }
    }

    // Ent-Or
    if (binary->op == "||") {
      if (EntOrAt(ctx, location, binary->lhs, binary->rhs)) {
        result.provable = true;
        result.explanation = "At least one disjunct provable";
        return result;
      }
    }
  }

  // Ent-Linear
  if (EntLinearAt(ctx, location, predicate)) {
    result.provable = true;
    result.explanation = "Linear integer reasoning";
    return result;
  }

  result.provable = false;
  result.diag_id = "E-SEM-2850";  // Cannot prove predicate
  return result;
}

StaticProofResult StaticProof(
    const StaticProofContext& ctx,
    const ast::ExprPtr& predicate) {
  const auto location = predicate ? predicate->span : core::Span{};
  return StaticProofAt(ctx, location, predicate);
}

bool EntTrue(const ast::ExprPtr& expr) {
  SpecDefsVerification();
  SPEC_RULE("Ent-True");
  return IsLiteralTrue(expr);
}

static bool EntFactAt(const StaticProofContext& ctx,
                      const core::Span& location,
                      const ast::ExprPtr& expr) {
  SpecDefsVerification();
  SPEC_RULE("Ent-Fact");

  for (const auto& fact : ctx.facts) {
    if (FactDominates(fact, location) &&
        ExprStructEqual(fact.predicate, expr)) {
      return true;
    }
  }
  return false;
}

bool EntFact(const StaticProofContext& ctx, const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return EntFactAt(ctx, expr->span, expr);
}

static bool EntAndAt(const StaticProofContext& ctx,
                     const core::Span& location,
                     const ast::ExprPtr& left,
                     const ast::ExprPtr& right) {
  SpecDefsVerification();
  SPEC_RULE("Ent-And");

  auto left_proof = StaticProofAt(ctx, location, left);
  if (!left_proof.provable) return false;

  auto right_proof = StaticProofAt(ctx, location, right);
  return right_proof.provable;
}

bool EntAnd(const StaticProofContext& ctx,
            const ast::ExprPtr& left,
            const ast::ExprPtr& right) {
  if (!left || !right) {
    return false;
  }
  return EntAndAt(ctx, left->span, left, right);
}

static bool EntOrAt(const StaticProofContext& ctx,
                    const core::Span& location,
                    const ast::ExprPtr& left,
                    const ast::ExprPtr& right) {
  SpecDefsVerification();
  SPEC_RULE("Ent-Or");

  auto left_proof = StaticProofAt(ctx, location, left);
  if (left_proof.provable) return true;

  auto right_proof = StaticProofAt(ctx, location, right);
  return right_proof.provable;
}

bool EntOr(const StaticProofContext& ctx,
           const ast::ExprPtr& left,
           const ast::ExprPtr& right) {
  if (!left || !right) {
    return false;
  }
  return EntOrAt(ctx, left->span, left, right);
}

static bool EntLinearAt(const StaticProofContext& ctx,
                        const core::Span& location,
                        const ast::ExprPtr& expr) {
  SpecDefsVerification();
  SPEC_RULE("Ent-Linear");
  if (!expr) {
    return false;
  }

  ParsedLinearComparison target{};
  if (!ParseLinearComparison(expr, target) || !target.ok) {
    return false;
  }

  std::vector<LinearConstraint> facts;
  bool contradiction = false;
  for (const auto& fact : ctx.facts) {
    if (!FactDominates(fact, location)) {
      continue;
    }
    CollectLinearFacts(fact.predicate, facts, contradiction);
    if (contradiction) {
      return true;
    }
  }

  return EntailsParsedComparison(facts, target);
}

bool EntLinear(const StaticProofContext& ctx, const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return EntLinearAt(ctx, expr->span, expr);
}

ConstValue EvaluateConstant(const ast::ExprPtr& expr) {
  SpecDefsVerification();

  ConstValue result;
  if (!expr) {
    return result;
  }

  auto parse_int = [](std::string_view lexeme) -> std::optional<std::int64_t> {
    std::string cleaned;
    cleaned.reserve(lexeme.size());
    for (const char ch : lexeme) {
      if (ch != '_') {
        cleaned.push_back(ch);
      }
    }
    try {
      std::size_t idx = 0;
      const auto value = std::stoll(cleaned, &idx, 0);
      if (idx != cleaned.size()) {
        return std::nullopt;
      }
      return value;
    } catch (...) {
      return std::nullopt;
    }
  };

  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    if (lit->literal.kind == ast::TokenKind::IntLiteral) {
      const auto parsed = parse_int(lit->literal.lexeme);
      if (parsed.has_value()) {
        result.value = *parsed;
        result.known = true;
      }
      return result;
    }
    if (lit->literal.kind == ast::TokenKind::BoolLiteral) {
      result.bool_value = lit->literal.lexeme == "true";
      result.is_bool = true;
      result.known = true;
      return result;
    }
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    const auto inner = EvaluateConstant(unary->value);
    if (!inner.known) {
      return result;
    }
    if (unary->op == "-" && !inner.is_bool) {
      result.known = true;
      result.value = -inner.value;
      return result;
    }
    if (unary->op == "+" && !inner.is_bool) {
      return inner;
    }
    if (unary->op == "!" && inner.is_bool) {
      result.known = true;
      result.is_bool = true;
      result.bool_value = !inner.bool_value;
      return result;
    }
    return result;
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    const auto lhs = EvaluateConstant(binary->lhs);
    const auto rhs = EvaluateConstant(binary->rhs);
    if (!lhs.known || !rhs.known) {
      return result;
    }
    if (!lhs.is_bool && !rhs.is_bool) {
      if (binary->op == "+" || binary->op == "-" ||
          binary->op == "*" || binary->op == "/" ||
          binary->op == "%") {
        std::int64_t out = 0;
        if (binary->op == "+") {
          out = lhs.value + rhs.value;
        } else if (binary->op == "-") {
          out = lhs.value - rhs.value;
        } else if (binary->op == "*") {
          out = lhs.value * rhs.value;
        } else if (binary->op == "/") {
          if (rhs.value == 0) {
            return result;
          }
          out = lhs.value / rhs.value;
        } else if (binary->op == "%") {
          if (rhs.value == 0) {
            return result;
          }
          out = lhs.value % rhs.value;
        }
        result.known = true;
        result.value = out;
        return result;
      }
      if (binary->op == "==" || binary->op == "!=" ||
          binary->op == "<" || binary->op == "<=" ||
          binary->op == ">" || binary->op == ">=") {
        result.known = true;
        result.is_bool = true;
        if (binary->op == "==") {
          result.bool_value = lhs.value == rhs.value;
        } else if (binary->op == "!=") {
          result.bool_value = lhs.value != rhs.value;
        } else if (binary->op == "<") {
          result.bool_value = lhs.value < rhs.value;
        } else if (binary->op == "<=") {
          result.bool_value = lhs.value <= rhs.value;
        } else if (binary->op == ">") {
          result.bool_value = lhs.value > rhs.value;
        } else if (binary->op == ">=") {
          result.bool_value = lhs.value >= rhs.value;
        }
        return result;
      }
      return result;
    }

    if (lhs.is_bool && rhs.is_bool) {
      if (binary->op == "&&") {
        result.known = true;
        result.is_bool = true;
        result.bool_value = lhs.bool_value && rhs.bool_value;
        return result;
      }
      if (binary->op == "||") {
        result.known = true;
        result.is_bool = true;
        result.bool_value = lhs.bool_value || rhs.bool_value;
        return result;
      }
      if (binary->op == "==" || binary->op == "!=") {
        result.known = true;
        result.is_bool = true;
        result.bool_value =
            (binary->op == "==") ? (lhs.bool_value == rhs.bool_value)
                                 : (lhs.bool_value != rhs.bool_value);
        return result;
      }
    }
  }

  return result;
}

ast::ExprPtr SimplifyBoolean(const ast::ExprPtr& expr) {
  // Boolean algebra simplification
  // true && P -> P
  // false && P -> false
  // true || P -> true
  // false || P -> P
  // !true -> false
  // !false -> true
  // !!P -> P

  if (!expr) return expr;

  // Simplified implementation - return as-is
  return expr;
}

bool IsReachable(const StaticProofContext& ctx, const core::Span& location) {
  // Simplified: always reachable
  return true;
}

TypeBounds GetTypeBounds(const TypeRef& type) {
  TypeBounds bounds;

  if (!type) return bounds;

  // Check for primitive integer types
  if (const auto* prim = std::get_if<TypePrim>(&type->node)) {
    if (prim->name == "u8") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = 0;
      bounds.max = 255;
    } else if (prim->name == "u16") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = 0;
      bounds.max = 65535;
    } else if (prim->name == "u32") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = 0;
      bounds.max = 4294967295LL;
    } else if (prim->name == "i8") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = -128;
      bounds.max = 127;
    } else if (prim->name == "i16") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = -32768;
      bounds.max = 32767;
    } else if (prim->name == "i32") {
      bounds.has_min = bounds.has_max = true;
      bounds.min = -2147483648LL;
      bounds.max = 2147483647LL;
    }
    // i64/u64 bounds would overflow int64
  }

  return bounds;
}

void AddFact(StaticProofContext& ctx,
             const ast::ExprPtr& predicate,
             const core::Span& location) {
  VerificationFact fact;
  fact.predicate = predicate;
  fact.location = location;
  fact.scope_id = ctx.current_scope;
  ctx.facts.push_back(fact);
}

void AddPredicateFactsAt(StaticProofContext& ctx,
                         const ast::ExprPtr& predicate,
                         const core::Span& location) {
  if (!predicate) {
    return;
  }
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&predicate->node);
      binary && binary->op == "&&") {
    AddPredicateFactsAt(ctx, binary->lhs, location);
    AddPredicateFactsAt(ctx, binary->rhs, location);
    return;
  }
  AddFact(ctx, predicate, location);
}

void AddPredicateFacts(StaticProofContext& ctx,
                       const ast::ExprPtr& predicate) {
  AddPredicateFactsAt(ctx, predicate,
                      predicate ? predicate->span : core::Span{});
}

std::shared_ptr<StaticProofContext> ExtendProofContextWithPredicate(
    const std::shared_ptr<StaticProofContext>& base,
    const ast::ExprPtr& predicate) {
  return ExtendProofContextWithPredicateAt(
      base, predicate, predicate ? predicate->span : core::Span{});
}

std::shared_ptr<StaticProofContext> ExtendProofContextWithPredicateAt(
    const std::shared_ptr<StaticProofContext>& base,
    const ast::ExprPtr& predicate,
    const core::Span& location) {
  if (!base && !predicate) {
    return nullptr;
  }

  auto proof_ctx = std::make_shared<StaticProofContext>();
  if (base) {
    *proof_ctx = *base;
  }
  AddPredicateFactsAt(*proof_ctx, predicate, location);
  return proof_ctx;
}

std::optional<ast::ExprPtr> NegatedPredicate(const ast::ExprPtr& predicate) {
  if (!predicate) {
    return std::nullopt;
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&predicate->node)) {
    if (unary->op == "!" && unary->value) {
      return unary->value;
    }
    return std::nullopt;
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&predicate->node)) {
    const auto negated_op = NegatedComparisonOperator(binary->op);
    if (!negated_op.has_value()) {
      ast::UnaryExpr negated;
      negated.op = "!";
      negated.value = predicate;
      return MakeExprNode(predicate->span, std::move(negated));
    }
    ast::BinaryExpr negated = *binary;
    negated.op = *negated_op;
    return MakeExprNode(predicate->span, std::move(negated));
  }

  ast::UnaryExpr negated;
  negated.op = "!";
  negated.value = predicate;
  return MakeExprNode(predicate->span, std::move(negated));
}

bool FactDominates(const VerificationFact& fact, const core::Span& location) {
  // Simplified: fact dominates if it's in the same file and before location
  return fact.location.file == location.file &&
         fact.location.start_offset <= location.start_offset;
}

}  // namespace cursive::analysis
