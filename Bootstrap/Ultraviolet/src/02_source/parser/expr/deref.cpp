// =============================================================================
// MIGRATION MAPPING: deref.cpp
// =============================================================================
// This file should contain parsing logic for dereference expressions (*expr).
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.8.5, Lines 5101-5104
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Unary-Deref)** Lines 5101-5104
// IsOp(Tok(P), "*")    Gamma |- ParseUnary(Advance(P)) => (P_1, e)
// ----------------------------------------------------------------------------
// Gamma |- ParseUnary(P) => (P_1, Deref(e))
//
// SEMANTICS:
// - Dereference operator (*) accesses the value pointed to by a pointer
// - Operand is parsed via ParseUnary (general expression, not just place)
// - Right-associative: ***p parses as *(*(*(p)))
// - Result is a place expression (can be target of & or assignment)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Dereference parsing in ParseUnary (Lines 669-677)
//    ---------------------------------------------------------------------------
//    Lines 669-670: Operator detection
//      - Check: IsOp(parser, "*")
//      - If true, enter dereference parsing branch
//      - SPEC_RULE: "Parse-Unary-Deref"
//
//    Lines 671-672: Advance parser
//      - Create next parser state
//      - Advance(next) past the "*" operator
//
//    Lines 673-674: Parse operand expression
//      - Recursively call ParseUnary(next, allow_brace, allow_bracket)
//      - Recursive call enables right-associativity: **p = *(*p)
//      - Stores result in `rhs`
//
//    Lines 674-677: Build DerefExpr
//      - Create DerefExpr node
//      - Set deref.value = rhs.elem
//      - Return result with:
//        - Parser: rhs.parser
//        - Expr: MakeExpr(SpanCover(TokSpan(parser), rhs.elem->span), deref)
//
// SOURCE CODE (Lines 669-678):
// -----------------------------------------------------------------------------
//   if (IsOp(parser, "*")) {
//     SPEC_RULE("Parse-Unary-Deref");
//     Parser next = parser;
//     Advance(next);
//     ParseElemResult<ExprPtr> rhs = ParseUnary(next, allow_brace, allow_bracket);
//     DerefExpr deref;
//     deref.value = rhs.elem;
//     return {rhs.parser,
//             MakeExpr(SpanCover(TokSpan(parser), rhs.elem->span), deref)};
//   }
//
// -----------------------------------------------------------------------------
// 2. Dereference in ParsePlace context (Lines 2541-2549)
//    ---------------------------------------------------------------------------
//    For place expressions, there's a separate dereference rule.
//
//    Lines 2541-2542: Operator detection
//      - Check: IsOp(parser, "*")
//      - SPEC_RULE: "Parse-Place-Deref"
//
//    Lines 2543-2545: Parse inner place
//      - Advance past "*"
//      - Recursively call ParsePlace (NOT ParseUnary)
//      - This ensures the entire chain is validated as place expressions
//
//    Lines 2546-2549: Build DerefExpr
//      - Create DerefExpr node
//      - Set deref.value = inner.elem
//      - Return with MakeExpr(SpanCover(TokSpan(parser), inner.elem->span), deref)
//
// SOURCE CODE (Lines 2541-2549):
// -----------------------------------------------------------------------------
//   if (IsOp(parser, "*")) {
//     SPEC_RULE("Parse-Place-Deref");
//     Parser next = parser;
//     Advance(next);
//     ParseElemResult<ExprPtr> inner = ParsePlace(next, allow_brace);
//     DerefExpr deref;
//     deref.value = inner.elem;
//     return {inner.parser,
//             MakeExpr(SpanCover(TokSpan(parser), inner.elem->span), deref)};
//   }
//
// =============================================================================
// SPEC REFERENCE FOR PLACE CONTEXT:
// =============================================================================
// **(Parse-Place-Deref)** Lines 6050-6053
// IsOp(Tok(P), "*")    Gamma |- ParsePlace(Advance(P)) => (P_1, p)
// ----------------------------------------------------------------------------
// Gamma |- ParsePlace(P) => (P_1, Deref(p))
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - ParseUnary function (unary.cpp) - for recursive deref in general context
// - ParsePlace function (place.cpp or unary.cpp) - for place context deref
// - MakeExpr, SpanCover, TokSpan helpers (expr_common.cpp)
// - IsOp, Advance helpers (parser utilities)
// - DerefExpr AST node type
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Dereference appears in TWO contexts:
//   1. General expression context (ParseUnary) - operand via ParseUnary
//   2. Place expression context (ParsePlace) - operand via ParsePlace
// - The AST node is the same (DerefExpr), but parsing differs
// - Consider: Keep both in deref.cpp, or split general vs place
// - Consider: deref.cpp may just be documentation, with implementation in
//   unary.cpp and place.cpp respectively
// - Right-associativity achieved via recursive call to ParseUnary
// - Span: covers from "*" operator to end of operand expression
// - allow_brace and allow_bracket parameters propagate through
// =============================================================================
