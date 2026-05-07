// =============================================================================
// MIGRATION MAPPING: unary.cpp
// =============================================================================
// This file should contain parsing logic for unary prefix expressions.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.8.5, Lines 5094-5124
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Unary-Prefix)** Lines 5096-5099
// Tok(P) = op ∈ {"!", "-"}    Γ ⊢ ParseUnary(Advance(P)) ⇓ (P_1, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, Unary(op, e))
//
// **(Parse-Unary-Deref)** Lines 5101-5104
// IsOp(Tok(P), "*")    Γ ⊢ ParseUnary(Advance(P)) ⇓ (P_1, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, Deref(e))
//
// **(Parse-Unary-AddressOf)** Lines 5106-5109
// IsOp(Tok(P), "&")    Γ ⊢ ParsePlace(Advance(P)) ⇓ (P_1, p)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, AddressOf(p))
//
// **(Parse-Unary-Move)** Lines 5111-5114
// IsKw(Tok(P), "move")    Γ ⊢ ParsePlace(Advance(P)) ⇓ (P_1, p)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, MoveExpr(p))
//
// **(Parse-Unary-Widen)** Lines 5116-5119
// IsKw(Tok(P), "widen")    Γ ⊢ ParseUnary(Advance(P)) ⇓ (P_1, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, Unary("widen", e))
//
// **(Parse-Unary-Postfix)** Lines 5121-5124
// Γ ⊢ ParsePostfix(P) ⇓ (P_1, e)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseUnary(P) ⇓ (P_1, e)
//
// SEMANTICS:
// - All unary prefix operators are RIGHT-ASSOCIATIVE
// - `!!x` parses as `!(!(x))`
// - `***p` parses as `*(*(*(p)))`
// - AddressOf (&) and Move require PLACE EXPRESSIONS (not general expressions)
// - Place expressions: identifiers, field accesses, tuple accesses, index accesses, derefs
// - allow_brace and allow_bracket propagate through recursive calls
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParseUnary function (Lines 654-712)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 656-667: Logical negation (!) and unary minus (-)
//      - Check: IsOp(parser, "!") || IsOp(parser, "-")
//      - Extract operator from token
//      - Advance parser
//      - Recursively call ParseUnary for operand (right-associativity)
//      - Create UnaryExpr with op and operand
//      - Span: SpanCover(TokSpan(parser), rhs.elem->span)
//      - SPEC_RULE: "Parse-Unary-Prefix"
//
//    Lines 669-677: Dereference (*)
//      - Check: IsOp(parser, "*")
//      - Advance parser
//      - Recursively call ParseUnary for operand
//      - Create DerefExpr with operand
//      - Span: SpanCover(TokSpan(parser), rhs.elem->span)
//      - SPEC_RULE: "Parse-Unary-Deref"
//
//    Lines 679-687: Address-of (&)
//      - Check: IsOp(parser, "&")
//      - Advance parser
//      - Call ParsePlace (NOT ParseUnary) for operand
//      - Create AddressOfExpr with place
//      - Span: SpanCover(TokSpan(parser), place.elem->span)
//      - SPEC_RULE: "Parse-Unary-AddressOf"
//
//    Lines 689-697: Move keyword
//      - Check: IsKw(parser, "move")
//      - Advance parser
//      - Call ParsePlace (NOT ParseUnary) for operand
//      - Create MoveExpr with place
//      - Span: SpanCover(TokSpan(parser), place.elem->span)
//      - SPEC_RULE: "Parse-Unary-Move"
//
//    Lines 699-708: Widen keyword
//      - Check: IsKw(parser, "widen")
//      - Advance parser
//      - Recursively call ParseUnary for operand
//      - Create UnaryExpr with "widen" as op
//      - Span: SpanCover(TokSpan(parser), rhs.elem->span)
//      - SPEC_RULE: "Parse-Unary-Widen"
//
//    Lines 710-711: Fallback to postfix
//      - If no prefix operator matched, delegate to ParsePostfix
//      - SPEC_RULE: "Parse-Unary-Postfix"
//
// 2. ParsePlace function (Lines 2540-2561)
//    ─────────────────────────────────────────────────────────────────────────
//    Purpose: Parses place expressions (required by & and move)
//
//    Lines 2541-2549: Dereference in place context
//      - Check: IsOp(parser, "*")
//      - Advance parser
//      - Recursively call ParsePlace for inner
//      - Create DerefExpr with inner
//      - Return result
//      - SPEC_RULE: "Parse-Place-Deref"
//
//    Lines 2551-2554: Postfix place expression
//      - Call ParsePostfix for expression
//      - Validate with IsPlace predicate
//      - If valid, return expression
//      - SPEC_RULE: "Parse-Place-Postfix"
//
//    Lines 2556-2560: Invalid place expression error
//      - If IsPlace returns false, emit syntax error
//      - Call SyncStmt for error recovery
//      - Return ErrorExpr
//      - SPEC_RULE: "Parse-Place-Err"
//
// 3. IsPlace predicate (Lines 157-180)
//    ─────────────────────────────────────────────────────────────────────────
//    Purpose: Validates if expression is a valid place expression
//
//    Returns true for:
//    - IdentifierExpr (variable names)
//    - FieldAccessExpr (x.field)
//    - TupleAccessExpr (x.0)
//    - IndexAccessExpr (x[i])
//    - AttributedExpr wrapping a place (recursively check inner)
//    - DerefExpr wrapping a place (recursively check inner)
//
//    Returns false for all other expression types.
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - MakeExpr, SpanCover, TokSpan helpers (expr_common.cpp)
// - IsOp, IsKw, Tok, Advance helpers (parser utilities)
// - ParsePostfix (postfix.cpp) for fallback
// - EmitParseSyntaxErr, SyncStmt for error handling
// - AST node types: UnaryExpr, DerefExpr, AddressOfExpr, MoveExpr, ErrorExpr
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Order of conditions matters: prefix ops checked before postfix fallback
// - AddressOf and Move call ParsePlace, NOT ParseUnary (semantic requirement)
// - ParsePlace validates result with IsPlace predicate; errors if invalid
// - Span construction: SpanCover(TokSpan(parser), rhs.elem->span)
// - Recursive calls for !, -, *, widen go to ParseUnary (right-associativity)
// - Consider: ParsePlace could be in a separate place.cpp file or here
// - Consider: IsPlace could be a static utility function
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsOp(const Parser& parser, std::string_view op);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsPlace(const ExprPtr& expr);

// Forward declarations from other modules
ParseElemResult<ExprPtr> ParsePostfix(Parser parser, bool allow_brace,
                                      bool allow_bracket);

// =============================================================================
// ParsePlace - Parse place expression
// =============================================================================
//
// SPEC: Lines 6041-6060
// Place expressions are valid targets for address-of (&) and move operations.
// Valid places: identifiers, field accesses, tuple accesses, index accesses, derefs.

ParseElemResult<ExprPtr> ParsePlace(Parser parser, bool allow_brace) {
  if (IsOp(parser, "*")) {
    SPEC_RULE("Parse-Place-Deref");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> inner = ParsePlace(next, allow_brace);
    DerefExpr deref;
    deref.value = inner.elem;
    return {inner.parser,
            MakeExpr(SpanCover(TokSpan(parser), inner.elem->span), deref)};
  }
  // allow_bracket defaults to true for ParsePlace since we need to parse
  // index accesses as place expressions
  ParseElemResult<ExprPtr> expr = ParsePostfix(parser, allow_brace, true);
  if (IsPlace(expr.elem)) {
    SPEC_RULE("Parse-Place-Postfix");
    return expr;
  }
  SPEC_RULE("Parse-Place-Err");
  EmitParseSyntaxErr(expr.parser, TokSpan(parser));
  Parser sync = expr.parser;
  SyncStmt(sync);
  return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
}

// =============================================================================
// ParseUnary - Parse unary prefix expressions
// =============================================================================
//
// SPEC: Lines 5094-5124
// Unary prefix operators: !, -, *, &, move, widen
// All are right-associative.

ParseElemResult<ExprPtr> ParseUnary(Parser parser, bool allow_brace,
                                    bool allow_bracket) {
  // Logical negation (!) and unary minus (-)
  if (IsOp(parser, "!") || IsOp(parser, "-")) {
    SPEC_RULE("Parse-Unary-Prefix");
    const Token* tok = Tok(parser);
    Identifier op = tok ? tok->lexeme : "";
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> rhs = ParseUnary(next, allow_brace, allow_bracket);
    UnaryExpr unary;
    unary.op = op;
    unary.value = rhs.elem;
    return {rhs.parser, MakeExpr(SpanCover(TokSpan(parser), rhs.elem->span),
                                 unary)};
  }

  // Dereference (*)
  if (IsOp(parser, "*")) {
    SPEC_RULE("Parse-Unary-Deref");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> rhs = ParseUnary(next, allow_brace, allow_bracket);
    DerefExpr deref;
    deref.value = rhs.elem;
    return {rhs.parser,
            MakeExpr(SpanCover(TokSpan(parser), rhs.elem->span), deref)};
  }

  // Address-of (&)
  if (IsOp(parser, "&")) {
    SPEC_RULE("Parse-Unary-AddressOf");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> place = ParsePlace(next, allow_brace);
    AddressOfExpr addr;
    addr.place = place.elem;
    return {place.parser,
            MakeExpr(SpanCover(TokSpan(parser), place.elem->span), addr)};
  }

  // Move keyword
  if (IsKw(parser, "move")) {
    SPEC_RULE("Parse-Unary-Move");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> place = ParsePlace(next, allow_brace);
    MoveExpr move;
    move.place = place.elem;
    return {place.parser,
            MakeExpr(SpanCover(TokSpan(parser), place.elem->span), move)};
  }

  // Widen keyword
  if (IsKw(parser, "widen")) {
    SPEC_RULE("Parse-Unary-Widen");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> rhs = ParseUnary(next, allow_brace, allow_bracket);
    UnaryExpr unary;
    unary.op = "widen";
    unary.value = rhs.elem;
    return {rhs.parser,
            MakeExpr(SpanCover(TokSpan(parser), rhs.elem->span), unary)};
  }

  // Fallback to postfix expression
  SPEC_RULE("Parse-Unary-Postfix");
  return ParsePostfix(parser, allow_brace, allow_bracket);
}

}  // namespace cursive::ast
