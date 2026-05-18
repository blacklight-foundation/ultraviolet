// =============================================================================
// MIGRATION MAPPING: addr_of.cpp
// =============================================================================
// This file should contain parsing logic for address-of expressions (&expr).
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.8.5, Lines 5106-5109
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Unary-AddressOf)** Lines 5106-5109
// IsOp(Tok(P), "&")    Gamma |- ParsePlace(Advance(P)) => (P_1, p)
// ----------------------------------------------------------------------------
// Gamma |- ParseUnary(P) => (P_1, AddressOf(p))
//
// SEMANTICS:
// - Address-of operator (&) takes the address of a PLACE EXPRESSION
// - Place expressions are: identifiers, field accesses, tuple accesses,
//   index accesses, and dereferences of place expressions
// - Returns a pointer to the place
// - Unlike Rust, Ultraviolet uses ParsePlace, not ParseUnary, for the operand
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Address-of parsing (Lines 679-687)
//    ---------------------------------------------------------------------------
//    Lines 679-680: Operator detection
//      - Check: IsOp(parser, "&")
//      - If true, enter address-of parsing branch
//
//    Lines 681-682: Advance parser
//      - Create next parser state
//      - Advance(next) past the "&" operator
//
//    Lines 683-684: Parse place expression
//      - Call ParsePlace(next, allow_brace) for operand
//      - CRITICAL: Uses ParsePlace, NOT ParseUnary
//      - ParsePlace validates that result is a valid place expression
//      - Stores result in `place`
//
//    Lines 684-686: Build AddressOfExpr
//      - Create AddressOfExpr node
//      - Set addr.place = place.elem
//      - Return result with:
//        - Parser: place.parser
//        - Expr: MakeExpr(SpanCover(TokSpan(parser), place.elem->span), addr)
//      - SPEC_RULE: "Parse-Unary-AddressOf"
//
// SOURCE CODE:
// -----------------------------------------------------------------------------
//   if (IsOp(parser, "&")) {
//     SPEC_RULE("Parse-Unary-AddressOf");
//     Parser next = parser;
//     Advance(next);
//     ParseElemResult<ExprPtr> place = ParsePlace(next, allow_brace);
//     AddressOfExpr addr;
//     addr.place = place.elem;
//     return {place.parser,
//             MakeExpr(SpanCover(TokSpan(parser), place.elem->span), addr)};
//   }
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - ParsePlace function (place.cpp or unary.cpp, Lines 2540-2561)
// - MakeExpr, SpanCover, TokSpan helpers (expr_common.cpp)
// - IsOp, Advance helpers (parser utilities)
// - AddressOfExpr AST node type
//
// RELATED SPEC CONTENT:
// -----------------------------------------------------------------------------
// Place Expression Rules (Lines 6045-6063):
//   IsPlace(e) <=> e in {Identifier(_), FieldAccess(_, _), TupleAccess(_, _),
//                        IndexAccess(_, _)} OR (exists p. e = Deref(p) AND IsPlace(p))
//
// **(Parse-Place-Deref)** Lines 6050-6053
// IsOp(Tok(P), "*")    Gamma |- ParsePlace(Advance(P)) => (P_1, p)
// ----------------------------------------------------------------------------
// Gamma |- ParsePlace(P) => (P_1, Deref(p))
//
// **(Parse-Place-Postfix)** Lines 6055-6058
// Gamma |- ParsePostfix(P) => (P_1, e)    IsPlace(e)
// ----------------------------------------------------------------------------
// Gamma |- ParsePlace(P) => (P_1, e)
//
// **(Parse-Place-Err)** Lines 6060-6063
// Gamma |- ParsePostfix(P) => (P_1, e)    NOT IsPlace(e)
// c = Code(PlaceExprParseErr)    Gamma |- Emit(c, Tok(P).span)
// Gamma |- SyncStmt(P_1) => P_2
// ----------------------------------------------------------------------------
// Gamma |- ParsePlace(P) => (P_2, ErrorExpr(SpanBetween(P, P_2)))
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Address-of is part of the ParseUnary dispatch in the bootstrap
// - Consider: Keep as part of unary.cpp or extract to separate file
// - The key semantic constraint is that operand MUST be a place expression
// - Span: covers from "&" operator to end of place expression
// - allow_brace parameter propagates to ParsePlace
// - This is NOT like Rust's &ref or &mut - Ultraviolet uses permission qualifiers
//   on types, not on the address-of operator
// =============================================================================
