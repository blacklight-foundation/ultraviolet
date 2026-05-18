// =============================================================================
// MIGRATION MAPPING: cast.cpp
// =============================================================================
// This file should contain parsing logic for cast expressions (expr as Type).
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.8.4, Lines 5077-5092
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Cast)** Lines 5079-5082
// Γ ⊢ ParseUnary(P) ⇓ (P_1, e)    Γ ⊢ ParseCastTail(P_1, e) ⇓ (P_2, e')
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseCast(P) ⇓ (P_2, e')
//
// **(Parse-CastTail-None)** Lines 5084-5087
// ¬ IsKw(Tok(P), "as")
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseCastTail(P, e) ⇓ (P, e)
//
// **(Parse-CastTail-As)** Lines 5089-5092
// IsKw(Tok(P), "as")    Γ ⊢ ParseType(Advance(P)) ⇓ (P_1, t)
// ────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseCastTail(P, e) ⇓ (P_1, Cast(e, t))
//
// SEMANTICS:
// - Cast expressions have the form: expr "as" Type
// - "as" is a keyword, checked via IsKw (not IsOp)
// - Cast is LEFT-ASSOCIATIVE: `a as T1 as T2` parses as `(a as T1) as T2`
// - Cast binds tighter than binary operators but looser than unary prefix
// - Precedence position: ParseMul -> ParsePower -> ParseCast -> ParseUnary
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParseCast function (Lines 632-637)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 634-635: Initial parse
//      - Call ParseUnary to get left-hand expression
//      - Store result in lhs
//
//    Line 636: Delegate to tail
//      - Call ParseCastTail with parser position and lhs expression
//      - Return result from tail
//      - SPEC_RULE: "Parse-Cast"
//
// 2. ParseCastTail function (Lines 639-652)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 640-643: No cast check
//      - Check: !IsKw(parser, "as")
//      - If not "as" keyword, return expression unchanged
//      - SPEC_RULE: "Parse-CastTail-None"
//
//    Lines 644-651: Process cast
//      - Check: IsKw(parser, "as") (implicit from else branch)
//      - Advance past "as" keyword
//      - Call ParseType to get target type
//      - Create CastExpr with:
//        - value: original expression (lhs)
//        - type: parsed type
//      - Span: SpanCover(lhs->span, ty.elem->span)
//      - Return new CastExpr
//      - SPEC_RULE: "Parse-CastTail-As"
//
//    NOTE: ParseCastTail does NOT recurse; single cast per call.
//    Left-associativity achieved because ParsePower calls ParseCast,
//    and ParseCast calls ParseUnary, so `a as T1 as T2` works as:
//    - ParsePower gets `a as T1`
//    - PowerTail sees `as` (not `**`), so returns
//    - ... actually, the spec has ParseCastTail non-recursive.
//
//    CORRECTION: Looking at the source more carefully:
//    - ParseCastTail is NOT recursive in the bootstrap
//    - Multiple casts would need to chain through the precedence hierarchy
//    - Actually, since casts are parsed after power and before unary,
//      `a as T1 as T2` would parse: ParsePower -> ParseCast(a) ->
//      ParseCastTail(a, as T1) = Cast(a, T1), then returns to ParsePowerTail
//      which sees `as` and stops. So only ONE cast is parsed.
//
//    SPEC ANALYSIS: The spec's ParseCastTail-As does NOT recurse either.
//    It only parses one `as Type` and returns. For multiple casts to parse
//    correctly, the caller (ParsePower) would need to re-enter ParseCast.
//    But it doesn't - ParsePower delegates to ParsePowerTail after ParseCast.
//
//    CONCLUSION: The current implementation matches spec - single cast per
//    ParseCast invocation. Multiple casts would require explicit parentheses
//    or grammar adjustment.
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - ParseUnary function (unary.cpp) for left operand
// - ParseType function (type parser) for target type
// - MakeExpr, SpanCover helpers (expr_common.cpp)
// - IsKw, Tok, Advance helpers (parser utilities)
// - CastExpr AST node type
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - "as" is checked via IsKw, NOT IsOp (it's a keyword, not operator)
// - ParseCastTail is non-recursive; single cast per call
// - Consider: Should multiple casts be supported? Spec allows only one.
// - Span construction: SpanCover(lhs->span, type->span)
// - No allow_brace/allow_bracket parameters in ParseCastTail (type parsing
//   handles its own delimiter rules)
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsKw(const Parser& parser, std::string_view kw);

// Forward declarations from other modules
ParseElemResult<ExprPtr> ParseUnary(Parser parser, bool allow_brace,
                                    bool allow_bracket);

// =============================================================================
// ParseCastTail - Parse optional cast suffix
// =============================================================================
//
// SPEC: Lines 5084-5092
// Handles: expr as Type
// Non-recursive - only parses one cast per invocation.

ParseElemResult<ExprPtr> ParseCastTail(Parser parser, ExprPtr lhs) {
  if (!IsKw(parser, "as")) {
    SPEC_RULE("Parse-CastTail-None");
    return {parser, lhs};
  }
  SPEC_RULE("Parse-CastTail-As");
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
  CastExpr cast;
  cast.value = lhs;
  cast.type = ty.elem;
  return {ty.parser, MakeExpr(SpanCover(lhs->span, ty.elem->span), cast)};
}

// =============================================================================
// ParseCast - Parse cast expression
// =============================================================================
//
// SPEC: Lines 5077-5082
// Parses: unary_expr [as Type]
// Left-associative (achieved through precedence hierarchy, not recursion).

ParseElemResult<ExprPtr> ParseCast(Parser parser, bool allow_brace,
                                   bool allow_bracket) {
  SPEC_RULE("Parse-Cast");
  ParseElemResult<ExprPtr> lhs = ParseUnary(parser, allow_brace, allow_bracket);
  return ParseCastTail(lhs.parser, lhs.elem);
}

}  // namespace ultraviolet::ast
