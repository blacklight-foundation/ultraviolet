// =============================================================================
// MIGRATION MAPPING: tuple_access.cpp
// =============================================================================
// This file contains parsing logic for tuple element access (expr.0, expr.1).
//
// SPEC REFERENCE: CursiveSpecification.md, Lines 5418-5421
// -----------------------------------------------------------------------------
// **(Postfix-TupleIndex)** Lines 5418-5421
// IsPunc(Tok(P), ".")    t = Tok(Advance(P))    t.kind = IntLiteral    idx = IntValue(t)
// ──────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ PostfixStep(P, e) ⇓ (Advance(Advance(P)), TupleAccess(e, idx))
//
// SEMANTICS:
// - Tuple element access uses dot notation with integer literal: expr.0, expr.1
// - The base expression is evaluated first, then the indexed element is accessed
// - The index must be an integer literal token (not an expression)
// - Zero-indexed: first element is .0, second is .1, etc.
// - Returns TupleAccess AST node containing base expression and parsed index
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// 1. Tuple Access Branch (in PostfixStep function, after field access check)
//    Source: parser_expr.cpp, lines 752-761
//    ```cpp
//    if (tok && tok->kind == TokenKind::IntLiteral) {
//      SPEC_RULE("Postfix-TupleIndex");
//      auto index = *index_value;
//      Parser after = next;
//      Advance(after);
//      TupleAccessExpr access;
//      access.base = expr;
//      access.index = index;
//      return {after, MakeExpr(SpanCover(expr->span, tok->span), access)};
//    }
//    ```
//
// CONTEXT (lines 738-741 setup):
// The dot is checked first, shared with field access:
//    ```cpp
//    if (IsPunc(parser, ".")) {
//      Parser next = parser;
//      Advance(next);
//      const Token* tok = Tok(next);
//      // ... field access check first (identifier) ...
//      // ... then tuple access check (integer literal) ...
//    }
//    ```
//
// AST DEFINITIONS (from ast.h, lines 474-477):
// ```cpp
// struct TupleAccessExpr {
//   ExprPtr base;
//   TupleIndex index;
// };
// ```
//
// DEPENDENCIES:
// - Requires: IsPunc, Tok, Advance helpers
// - Requires: TokenKind::IntLiteral constant
// - Requires: TupleAccessExpr AST node type
// - Requires: MakeExpr, SpanCover helpers for AST construction
// - Produces: ExprPtr containing TupleAccessExpr
//
// REFACTORING NOTES:
// - Shares the initial dot check with field_access.cpp
// - The index is stored as its parsed integer value
// - Tuple bounds checking happens during semantic analysis, not parsing
// - Consider: combined field/tuple access module or keep separate?
// - Span covers from base expression start to index token end
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/numeric_literals.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPunc(const Parser& parser, std::string_view punc);

// Forward declaration from lexer
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// =============================================================================
// TryParseTupleAccess - Try to parse tuple index access after dot
// =============================================================================
//
// SPEC: Lines 5418-5421 (Postfix-TupleIndex)
//
// Called when the parser is positioned at a "." token. Attempts to parse
// a tuple index access if the next token is an integer literal.
//
// Parameters:
//   parser - Parser positioned at the "." token
//   base   - The base expression (left of the dot)
//
// Returns:
//   std::nullopt if next token is not an integer literal
//   ParseElemResult with TupleAccessExpr if successful
//
// NOTE: The index is stored as its parsed integer value. The outer expression
// span still covers the full `base.index` source range for diagnostics.

std::optional<ParseElemResult<ExprPtr>> TryParseTupleAccess(Parser parser,
                                                             ExprPtr base) {
  // Caller should have already verified IsPunc(parser, ".")
  Parser next = parser;
  Advance(next);
  const Token* tok = Tok(next);

  if (tok && tok->kind == TokenKind::IntLiteral) {
    const auto index_value =
        core::ParseIntCore(core::StripIntSuffix(tok->lexeme));
    if (index_value.has_value()) {
      SPEC_RULE("Postfix-TupleIndex");
      Parser after = next;
      Advance(after);
      TupleAccessExpr access;
      access.base = base;
      access.index = *index_value;
      return ParseElemResult<ExprPtr>{
          after, MakeExpr(SpanCover(base->span, tok->span), access)};
    }
  }

  // Not an integer literal - return nullopt so caller can try field access
  // or emit an error
  return std::nullopt;
}

}  // namespace cursive::ast
