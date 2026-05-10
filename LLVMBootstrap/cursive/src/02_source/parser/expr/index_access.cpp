// =============================================================================
// MIGRATION MAPPING: index_access.cpp
// =============================================================================
// This file contains parsing logic for index/subscript access (expr[index]).
//
// SPEC REFERENCE: CursiveSpecification.md, Lines 5423-5426
// -----------------------------------------------------------------------------
// **(Postfix-Index)** Lines 5423-5426
// IsPunc(Tok(P), "[")    Γ ⊢ ParseExpr(Advance(P)) ⇓ (P_1, idx)    IsPunc(Tok(P_1), "]")
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ PostfixStep(P, e) ⇓ (Advance(P_1), IndexAccess(e, idx))
//
// SEMANTICS:
// - Index access uses bracket notation: expr[index_expr]
// - The base expression is evaluated first, then the index expression
// - The index is a full expression (can be any valid expression)
// - Requires matching closing bracket "]"
// - Returns IndexAccess AST node containing base expression and index expression
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// 1. Index Access Branch (in PostfixStep function)
//    Source: parser_expr.cpp, lines 767-785
//    ```cpp
//    // C0X: Skip bracket index parsing when allow_bracket is false
//    if (IsPunc(parser, "[") && allow_bracket) {
//      SPEC_RULE("Postfix-Index");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<ExprPtr> index = ParseExpr(next);
//      if (!IsPunc(index.parser, "]")) {
//        EmitParseSyntaxErr(index.parser, TokSpan(index.parser));
//        Parser sync = index.parser;
//        SyncStmt(sync);
//        return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
//      }
//      core::Span end_span = TokSpan(index.parser);
//      Parser after = index.parser;
//      Advance(after);
//      IndexAccessExpr access;
//      access.base = expr;
//      access.index = index.elem;
//      return {after, MakeExpr(SpanCover(expr->span, end_span), access)};
//    }
//    ```
//
// IMPORTANT: allow_bracket Parameter
// The allow_bracket flag is used for disambiguation in certain contexts:
// - When parsing type bounds or generic contexts, brackets may indicate
//   array/slice types rather than index access
// - The flag is propagated through ParsePostfix and ParsePostfixTail
//
// AST DEFINITIONS (from ast.h, lines 479-482):
// ```cpp
// struct IndexAccessExpr {
//   ExprPtr base;
//   ExprPtr index;   // Full expression, not just literal
// };
// ```
//
// DEPENDENCIES:
// - Requires: IsPunc, Tok, TokSpan, Advance helpers
// - Requires: ParseExpr (recursive expression parsing)
// - Requires: IndexAccessExpr AST node type
// - Requires: MakeExpr, SpanCover, SpanBetween helpers
// - Requires: EmitParseSyntaxErr, SyncStmt for error handling
// - Produces: ExprPtr containing IndexAccessExpr
//
// REFACTORING NOTES:
// - The allow_bracket parameter needs careful handling in the refactored API
// - Error handling: missing "]" emits syntax error and syncs to statement
// - Span covers from base expression start to closing bracket
// - Index expression can be arbitrarily complex (calls, operators, etc.)
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPunc(const Parser& parser, std::string_view punc);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// ParseIndexAccess - Parse index/subscript access expression: base[index]
// =============================================================================
//
// SPEC: Lines 5423-5426 (Postfix-Index)
//
// Called when the parser is positioned at a "[" token. Parses the index
// expression and expects a closing "]".
//
// Parameters:
//   parser - Parser positioned at the "[" token
//   base   - The base expression (left of the bracket)
//
// Returns:
//   ParseElemResult with IndexAccessExpr if successful
//   ParseElemResult with ErrorExpr on missing "]"
//
// NOTE: The allow_bracket parameter from PostfixStep is handled by the caller.
// When allow_bracket is false, this function should not be called.

ParseElemResult<ExprPtr> ParseIndexAccess(Parser parser, ExprPtr base) {
  SPEC_RULE("Postfix-Index");
  Parser next = parser;
  Advance(next);  // consume "["

  // Parse the index expression (can be any valid expression)
  ParseElemResult<ExprPtr> index = ParseExpr(next);

  // Expect closing "]"
  if (!IsPunc(index.parser, "]")) {
    EmitParseSyntaxErr(index.parser, TokSpan(index.parser));
    Parser sync = index.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  core::Span end_span = TokSpan(index.parser);
  Parser after = index.parser;
  Advance(after);  // consume "]"

  IndexAccessExpr access;
  access.base = base;
  access.index = index.elem;
  return {after, MakeExpr(SpanCover(base->span, end_span), access)};
}

}  // namespace cursive::ast
