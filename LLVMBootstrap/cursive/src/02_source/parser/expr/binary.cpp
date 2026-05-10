// =============================================================================
// MIGRATION MAPPING: binary.cpp
// =============================================================================
// This file should contain parsing logic for binary expressions using Pratt
// parsing (iterative precedence-climbing).
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.8.2, Lines 5032-5057
// =============================================================================
//
// OPERATOR DEFINITIONS (Lines 4967-4974):
// -----------------------------------------------------------------------------
//   LogicalOrOps  = {"||"}
//   LogicalAndOps = {"&&"}
//   ComparisonOps = {"==", "!=", "<", "<=", ">", ">="}
//   BitOrOps      = {"|"}
//   BitXorOps     = {"^"}
//   BitAndOps     = {"&"}
//   ShiftOps      = {"<<", ">>"}
//   AddOps        = {"+", "-"}
//   MulOps        = {"*", "/", "%"}
//
// PRECEDENCE CHAIN (Lines 5034-5042):
// -----------------------------------------------------------------------------
//   ParseLogicalOr  -> LogicalOrOps  -> ParseLogicalAnd  (prec 1)
//   ParseLogicalAnd -> LogicalAndOps -> ParseComparison  (prec 2)
//   ParseComparison -> ComparisonOps -> ParseBitOr       (prec 3)
//   ParseBitOr      -> BitOrOps      -> ParseBitXor      (prec 4)
//   ParseBitXor     -> BitXorOps     -> ParseBitAnd      (prec 5)
//   ParseBitAnd     -> BitAndOps     -> ParseShift       (prec 6)
//   ParseShift      -> ShiftOps      -> ParseAdd         (prec 7)
//   ParseAdd        -> AddOps        -> ParseMul         (prec 8)
//   ParseMul        -> MulOps        -> ParsePower       (prec 9)
//   ParsePower handles ** separately (right-associative)
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-LeftChain)** Lines 5044-5047
// Γ ⊢ ParseHigher(P) ⇓ (P_1, e_0)
// Γ ⊢ ParseLeftChainTail(P_1, e_0, OpSet, ParseHigher) ⇓ (P_2, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseLeftChain(P, OpSet, ParseHigher) ⇓ (P_2, e)
//
// **(Parse-LeftChain-Stop)** Lines 5049-5052
// Tok(P) ∉ OpSet
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseLeftChainTail(P, e, OpSet, ParseHigher) ⇓ (P, e)
//
// **(Parse-LeftChain-Cons)** Lines 5054-5057
// Tok(P) = op ∈ OpSet    Γ ⊢ ParseHigher(Advance(P)) ⇓ (P_1, e_1)
// e' = Binary(op, e, e_1)
// Γ ⊢ ParseLeftChainTail(P_1, e', OpSet, ParseHigher) ⇓ (P_2, e'')
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseLeftChainTail(P, e, OpSet, ParseHigher) ⇓ (P_2, e'')
//
// SEMANTICS:
// - Binary operators (levels 1-9): Left-associative via iterative Pratt parsing
// - Power (**): Right-associative via explicit recursion to ParsePower
// - Precedence: Higher level = tighter binding
// - Example: `a + b * c` parses as `a + (b * c)`
// - Example: `a + b + c` parses as `(a + b) + c` (left-assoc)
// - Example: `a ** b ** c` parses as `a ** (b ** c)` (right-assoc)
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Precedence Table (Include file: precedence_table.inc, Lines 1-33)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 3-15: BinaryPrecedence enum
//      - NONE = 0 (not a binary operator)
//      - LOGICAL_OR = 1
//      - LOGICAL_AND = 2
//      - COMPARISON = 3
//      - BIT_OR = 4
//      - BIT_XOR = 5
//      - BIT_AND = 6
//      - SHIFT = 7
//      - ADD = 8
//      - MUL = 9
//
//    Lines 18-32: GetBinaryPrecedence(string_view op)
//      - Returns appropriate BinaryPrecedence for given operator string
//      - Returns NONE if not a recognized binary operator
//      - Used by Pratt parser to determine precedence
//
// 2. ParseBinaryExpr - Pratt parser (Lines 562-599)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 564-565: Initial parse
//      - Call ParsePower to get first operand (highest precedence sub-expr)
//      - Store in result
//
//    Lines 567-577: Operator check loop
//      - Get current token
//      - If not an operator, break
//      - Get precedence via GetBinaryPrecedence
//      - If NONE or below min_prec, break
//
//    Lines 579-594: Build binary expression
//      - Extract operator from token
//      - Advance past operator
//      - For left-associativity: parse RHS at next higher precedence
//        next_prec = static_cast<int>(prec) + 1
//      - Recursively call ParseBinaryExpr with next_prec
//      - Create BinaryExpr with lhs, op, rhs
//      - Update result with new expression
//      - Span: SpanCover(result.elem->span, rhs.elem->span)
//      - SPEC_RULE: "Parse-LeftChain-Cons"
//
//    Lines 597-598: Return accumulated result
//      - SPEC_RULE: "Parse-LeftChain-Stop"
//
// 3. ParseLogicalOr - Entry point (Lines 601-605)
//    ─────────────────────────────────────────────────────────────────────────
//    - Entry point for all binary expression parsing
//    - Calls ParseBinaryExpr with min_prec = BinaryPrecedence::LOGICAL_OR
//    - Passes allow_brace, allow_bracket through
//
// -----------------------------------------------------------------------------
// POWER OPERATOR (Spec Section 3.3.8.3, Lines 5060-5075)
// -----------------------------------------------------------------------------
//
// FORMAL RULES:
// **(Parse-Power)** Lines 5062-5065
// Γ ⊢ ParseCast(P) ⇓ (P_1, e_0)    Γ ⊢ ParsePowerTail(P_1, e_0) ⇓ (P_2, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePower(P) ⇓ (P_2, e)
//
// **(Parse-PowerTail-None)** Lines 5067-5070
// ¬ IsOp(Tok(P), "**")
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePowerTail(P, e) ⇓ (P, e)
//
// **(Parse-PowerTail-Cons)** Lines 5072-5075
// IsOp(Tok(P), "**")    Γ ⊢ ParsePower(Advance(P)) ⇓ (P_1, e_1)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePowerTail(P, e_0) ⇓ (P_1, Binary("**", e_0, e_1))
//
// 4. ParsePower function (Lines 607-612)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 609-610: Initial parse
//      - Call ParseCast to get left operand
//      - Store in lhs
//
//    Line 611: Delegate to tail
//      - Call ParsePowerTail with parser position and lhs
//      - Return result from tail
//      - SPEC_RULE: "Parse-Power"
//
// 5. ParsePowerTail function (Lines 614-630)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 617-619: No power operator
//      - Check: !IsOp(parser, "**")
//      - If not "**", return expression unchanged
//      - SPEC_RULE: "Parse-PowerTail-None"
//
//    Lines 621-629: Process power operator
//      - Check: IsOp(parser, "**") (implicit from else branch)
//      - Advance past "**"
//      - Recursively call ParsePower (NOT ParsePowerTail) for RHS
//        This achieves RIGHT-ASSOCIATIVITY: a**b**c = a**(b**c)
//      - Create BinaryExpr with op="**", lhs, rhs
//      - Span: SpanCover(lhs->span, rhs.elem->span)
//      - SPEC_RULE: "Parse-PowerTail-Cons"
//
//    NOTE: Right-associativity achieved by calling ParsePower (which includes
//    ParsePowerTail), not calling ParsePowerTail directly. This means the
//    recursive call processes any subsequent ** operators before returning.
//
// DEPENDENCIES:
// =============================================================================
// - BinaryPrecedence enum (define or include from precedence_table.inc)
// - GetBinaryPrecedence(string_view) function
// - ParsePower function (power.cpp or this file)
// - MakeExpr, SpanCover helpers (expr_common.cpp)
// - IsOp, Tok, Advance helpers (parser utilities)
// - BinaryExpr AST node type
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Bootstrap uses Pratt parsing: reduces stack depth from O(prec_levels * nest)
//   to O(nesting) compared to recursive descent
// - ParseBinaryExpr takes min_prec parameter for precedence climbing
// - Left-associativity: RHS parsed at (prec + 1), ensuring tighter binding
// - The spec uses recursive ParseLeftChainTail; implementation is iterative
//   but produces identical parse trees
// - All functions pass allow_brace and allow_bracket parameters through
// - Span covers from leftmost operand to rightmost operand
// =============================================================================

#include "02_source/parser/parser.h"

#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::IsOpTok;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsOp(const Parser& parser, std::string_view op);
ParseElemResult<ExprPtr> ParseCast(Parser parser, bool allow_brace, bool allow_bracket);

// =============================================================================
// BinaryPrecedence - Precedence levels for binary operators
// =============================================================================
// Higher values = tighter binding (higher precedence)

enum class BinaryPrecedence : int {
  NONE = 0,
  LOGICAL_OR = 1,   // ||
  LOGICAL_AND = 2,  // &&
  COMPARISON = 3,   // ==, !=, <, <=, >, >=
  BIT_OR = 4,       // |
  BIT_XOR = 5,      // ^
  BIT_AND = 6,      // &
  SHIFT = 7,        // <<, >>
  ADD = 8,          // +, -
  MUL = 9,          // *, /, %
  // Power (**), Cast (as), Unary, Postfix handled separately
};

// =============================================================================
// GetBinaryPrecedence - Returns precedence of operator, or NONE if not binary
// =============================================================================

BinaryPrecedence GetBinaryPrecedence(std::string_view op) {
  if (op == "||") return BinaryPrecedence::LOGICAL_OR;
  if (op == "&&") return BinaryPrecedence::LOGICAL_AND;
  if (op == "==" || op == "!=" || op == "<" ||
      op == "<=" || op == ">" || op == ">=") {
    return BinaryPrecedence::COMPARISON;
  }
  if (op == "|") return BinaryPrecedence::BIT_OR;
  if (op == "^") return BinaryPrecedence::BIT_XOR;
  if (op == "&") return BinaryPrecedence::BIT_AND;
  if (op == "<<" || op == ">>") return BinaryPrecedence::SHIFT;
  if (op == "+" || op == "-") return BinaryPrecedence::ADD;
  if (op == "*" || op == "/" || op == "%") return BinaryPrecedence::MUL;
  return BinaryPrecedence::NONE;
}

// =============================================================================
// ParsePowerTail - Parse tail of power expression (right-associative)
// =============================================================================
//
// SPEC: Lines 5067-5075
// **(Parse-PowerTail-None)**: ¬ IsOp(Tok(P), "**") => (P, e)
// **(Parse-PowerTail-Cons)**: IsOp(Tok(P), "**") => ParsePower(Advance(P)) ...

ParseElemResult<ExprPtr> ParsePower(Parser parser, bool allow_brace, bool allow_bracket);

ParseElemResult<ExprPtr> ParsePowerTail(Parser parser, ExprPtr lhs,
                                        bool allow_brace, bool allow_bracket) {
  if (!IsOp(parser, "**")) {
    SPEC_RULE("Parse-PowerTail-None");
    return {parser, lhs};
  }
  SPEC_RULE("Parse-PowerTail-Cons");
  Parser next = parser;
  Advance(next);
  // Right-associative: call ParsePower (not ParsePowerTail) for RHS
  // This means a**b**c parses as a**(b**c)
  ParseElemResult<ExprPtr> rhs = ParsePower(next, allow_brace, allow_bracket);
  BinaryExpr bin;
  bin.op = "**";
  bin.lhs = lhs;
  bin.rhs = rhs.elem;
  return {rhs.parser, MakeExpr(SpanCover(lhs->span, rhs.elem->span), bin)};
}

// =============================================================================
// ParsePower - Parse power expression (**)
// =============================================================================
//
// SPEC: Lines 5062-5065
// Γ ⊢ ParseCast(P) ⇓ (P_1, e_0)    Γ ⊢ ParsePowerTail(P_1, e_0) ⇓ (P_2, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePower(P) ⇓ (P_2, e)

ParseElemResult<ExprPtr> ParsePower(Parser parser, bool allow_brace,
                                    bool allow_bracket) {
  SPEC_RULE("Parse-Power");
  ParseElemResult<ExprPtr> lhs = ParseCast(parser, allow_brace, allow_bracket);
  return ParsePowerTail(lhs.parser, lhs.elem, allow_brace, allow_bracket);
}

// =============================================================================
// ParseBinaryExpr - Pratt parser for left-associative binary operators
// =============================================================================
//
// SPEC: Lines 5044-5057
// Uses iterative precedence climbing instead of recursive descent.
// This reduces stack depth from O(precedence_levels * nesting) to O(nesting).

ParseElemResult<ExprPtr> ParseBinaryExpr(Parser parser, BinaryPrecedence min_prec,
                                         bool allow_brace, bool allow_bracket) {
  // Start with the highest-precedence sub-expression (power, cast, unary, postfix, primary)
  ParseElemResult<ExprPtr> result = ParsePower(parser, allow_brace, allow_bracket);

  // Iteratively consume binary operators at or above min_prec
  for (;;) {
    const Token* tok = Tok(result.parser);
    if (!tok || tok->kind != TokenKind::Operator) {
      break;
    }

    BinaryPrecedence prec = GetBinaryPrecedence(tok->lexeme);
    if (prec == BinaryPrecedence::NONE || prec < min_prec) {
      break;
    }

    SPEC_RULE("Parse-LeftChain-Cons");
    const Identifier op = std::string(tok->lexeme);
    Parser after_op = result.parser;
    Advance(after_op);

    // For left-associativity, parse RHS at next higher precedence
    // This ensures a + b + c parses as (a + b) + c
    BinaryPrecedence next_prec = static_cast<BinaryPrecedence>(static_cast<int>(prec) + 1);
    ParseElemResult<ExprPtr> rhs = ParseBinaryExpr(after_op, next_prec, allow_brace, allow_bracket);

    BinaryExpr bin;
    bin.op = op;
    bin.lhs = result.elem;
    bin.rhs = rhs.elem;
    result.elem = MakeExpr(SpanCover(result.elem->span, rhs.elem->span), bin);
    result.parser = rhs.parser;
  }

  SPEC_RULE("Parse-LeftChain-Stop");
  return result;
}

// =============================================================================
// ParseLogicalOr - Entry point for all binary expression parsing
// =============================================================================
//
// SPEC: Lines 5034-5042
// Entry point using Pratt parser with LOGICAL_OR as minimum precedence.

ParseElemResult<ExprPtr> ParseLogicalOr(Parser parser, bool allow_brace,
                                        bool allow_bracket) {
  return ParseBinaryExpr(parser, BinaryPrecedence::LOGICAL_OR, allow_brace, allow_bracket);
}

}  // namespace cursive::ast
