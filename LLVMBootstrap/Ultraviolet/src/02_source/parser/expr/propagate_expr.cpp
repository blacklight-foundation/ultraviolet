// =============================================================================
// MIGRATION MAPPING: propagate_expr.cpp
// =============================================================================
// This file should contain parsing logic for propagate (?) expressions.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.8.7, Lines 5443-5446
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Postfix-Propagate)** Lines 5443-5446
// IsOp(Tok(P), "?")
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(P), Propagate(e))
//
// GRAMMAR:
// -----------------------------------------------------------------------------
// PropagateExpr = Expr "?"
//
// The "?" is a postfix operator with highest precedence (postfix level).
//
// SEMANTICS:
// - The "?" operator propagates non-success union variants to the caller
// - If the expression is the "success" variant, unwraps and continues
// - If the expression is an "error" variant, returns early from the function
// - Used for concise error handling without explicit if-case/return
// - Return type must be a union type containing the propagated variants
// - Example: `file~>read()?` propagates IOError to caller if read fails
//
// TYPE RULES:
// - Expression must be a union type T | E1 | E2 | ...
// - Returns T (the "success" type) after unwrapping
// - Enclosing function must return a type containing E1 | E2 | ...
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Propagate Expression Parsing (Lines 835-843)
//    ---------------------------------------------------------------------------
//    Located within PostfixStep function, which handles all postfix operators.
//
//    Line 835: Condition check
//      - Check: IsOp(parser, "?")
//
//    Line 836: Spec rule annotation
//      - SPEC_RULE("Postfix-Propagate");
//
//    Lines 837-839: Capture span and advance
//      - core::Span end_span = TokSpan(parser);
//      - Parser next = parser;
//      - Advance(next);
//
//    Lines 840-841: Construct AST node
//      - PropagateExpr prop;
//      - prop.value = expr;   // expr is the base expression parameter
//
//    Lines 842-843: Return result
//      - return {next, MakeExpr(SpanCover(expr->span, end_span), prop)};
//      - Span covers from start of base expression to the "?" operator
//
// COMPLETE SOURCE CODE (Lines 835-843):
// -----------------------------------------------------------------------------
//   if (IsOp(parser, "?")) {
//     SPEC_RULE("Postfix-Propagate");
//     core::Span end_span = TokSpan(parser);
//     Parser next = parser;
//     Advance(next);
//     PropagateExpr prop;
//     prop.value = expr;
//     return {next, MakeExpr(SpanCover(expr->span, end_span), prop)};
//   }
//
// 2. Context: PostfixStep Function (Lines 736-848)
//    ---------------------------------------------------------------------------
//    The propagate parsing is one case within PostfixStep, which handles:
//    - Field access (.)
//    - Tuple index (.)
//    - Index access ([])
//    - Function call (())
//    - Method call (~>)
//    - Propagate (?)
//
//    PostfixStep signature:
//    ParseElemResult<ExprPtr> PostfixStep(Parser parser, ExprPtr expr,
//                                         bool allow_bracket = true);
//
// 3. Context: ParsePostfixTail Function (Lines 721-733)
//    ---------------------------------------------------------------------------
//    Repeatedly applies PostfixStep until no more postfix operators found.
//
//    ParseElemResult<ExprPtr> ParsePostfixTail(Parser parser, ExprPtr expr,
//                                              bool allow_brace,
//                                              bool allow_bracket);
//
//    Flow: ParsePostfix -> ParsePrimary -> ParsePostfixTail -> PostfixStep*
//
// =============================================================================
// RELATED POSTFIX OPERATORS:
// =============================================================================
// All from PostfixStep (Lines 736-848):
//
// **(Postfix-Field)** Lines 5413-5416
// IsPunc(Tok(P), ".")    IsIdent(Tok(Advance(P)))    name = Lexeme(Tok(Advance(P)))
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(Advance(P)), FieldAccess(e, name))
//
// **(Postfix-TupleIndex)** Lines 5418-5421
// IsPunc(Tok(P), ".")    t = Tok(Advance(P))    t.kind = IntLiteral    idx = IntValue(t)
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(Advance(P)), TupleAccess(e, idx))
//
// **(Postfix-Index)** Lines 5423-5426
// IsPunc(Tok(P), "[")    G |- ParseExpr(Advance(P)) => (P_1, idx)    IsPunc(Tok(P_1), "]")
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(P_1), IndexAccess(e, idx))
//
// **(Postfix-Call)** Lines 5428-5431
// IsPunc(Tok(P), "(")    G |- ParseArgList(Advance(P)) => (P_1, args)    IsPunc(Tok(P_1), ")")
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(P_1), Call(e, args))
//
// **(Postfix-MethodCall)** Lines 5438-5441
// IsOp(Tok(P), "~>")
// G |- ParseIdent(Advance(P)) => (P_1, name)
// IsPunc(Tok(P_1), "(")
// G |- ParseArgList(Advance(P_1)) => (P_2, args)
// IsPunc(Tok(P_2), ")")
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(P_2), MethodCall(e, name, args))
//
// **(Postfix-Propagate)** Lines 5443-5446
// IsOp(Tok(P), "?")
// -----------------------------------------------------------------------
// G |- PostfixStep(P, e) => (Advance(P), Propagate(e))
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - IsOp(Parser, string_view) -> bool
//   Checks if current token is operator with given lexeme
// - TokSpan(Parser) -> Span
//   Gets span of current token
// - Advance(Parser&) -> void
//   Moves parser forward one token
// - MakeExpr(Span, ExprNode) -> ExprPtr
//   Constructs an expression node with the given span
// - SpanCover(Span start, Span end) -> Span
//   Creates span covering from start to end
// - PropagateExpr AST node type
//   Fields: value (ExprPtr - the expression being propagated)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - This is a postfix operator, parsed within PostfixStep
// - Simplest postfix operator: just consumes "?" and wraps expression
// - No sub-expressions to parse after the operator
// - Span handling: covers base expression through "?" operator
// - Consider keeping propagate as inline case in PostfixStep
// - Or extract all postfix operators to separate files for modularity
// - The propagate operator has no syntactic arguments (unlike call, method)
// - Semantic validation (union type, matching return type) is in later phases
//
// DESIGN NOTE:
// - Unlike Rust's `?` which uses From/Into traits, Ultraviolet's `?` directly
//   propagates union variants without conversion
// - The success type must be explicitly determined by union structure
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsOp(const Parser& parser, std::string_view op);

// =============================================================================
// ParsePropagate - Parse propagate (?) expression
// =============================================================================
//
// SPEC: Lines 5443-5446 (Postfix-Propagate)
//
// Called when the parser is positioned at a "?" operator. This is the simplest
// postfix operator - it just consumes "?" and wraps the base expression.
//
// Parameters:
//   parser - Parser positioned at the "?" operator
//   base   - The expression being propagated
//
// Returns:
//   ParseElemResult with PropagateExpr
//
// SEMANTICS:
// - The "?" operator propagates non-success union variants to the caller
// - If the expression is the "success" variant, unwraps and continues
// - If the expression is an "error" variant, returns early from the function
// - Used for concise error handling without explicit if-case/return
//
// NOTE: Unlike Rust's `?` which uses From/Into traits, Ultraviolet's `?` directly
// propagates union variants without conversion. Semantic validation (union
// type, matching return type) is performed in later phases.

ParseElemResult<ExprPtr> ParsePropagate(Parser parser, ExprPtr base) {
  SPEC_RULE("Postfix-Propagate");
  core::Span end_span = TokSpan(parser);
  Parser next = parser;
  Advance(next);  // consume "?"

  PropagateExpr prop;
  prop.value = base;
  return {next, MakeExpr(SpanCover(base->span, end_span), prop)};
}

}  // namespace ultraviolet::ast
