// =============================================================================
// MIGRATION MAPPING: move_expr.cpp
// =============================================================================
// This file should contain parsing logic for move expressions.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.8.5, Lines 5111-5114
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Unary-Move)** Lines 5111-5114
// IsKw(Tok(P), `move`)    G |- ParsePlace(Advance(P)) => (P_1, p)
// -----------------------------------------------------------------------
// G |- ParseUnary(P) => (P_1, MoveExpr(p))
//
// GRAMMAR:
// -----------------------------------------------------------------------------
// MoveExpr = "move" Place
//
// Place expressions are defined as:
// - Identifier expressions
// - Field access expressions (base.field)
// - Tuple access expressions (base.0)
// - Index access expressions (base[idx])
// - Dereference expressions (*ptr)
// - Attributed expressions containing places
//
// SEMANTICS:
// - The `move` keyword transfers ownership of a value
// - The operand must be a "place" (lvalue/assignable location)
// - After move, the source binding becomes Moved state (unusable)
// - Move is distinct from copy: only one valid reference exists after
// - Used in: function arguments, return, assignments, pattern matching
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Move Expression Parsing (Lines 689-697)
//    ---------------------------------------------------------------------------
//    Line 689: Condition check
//      - Check: IsKw(parser, "move")
//      - This appears within ParseUnary function
//
//    Line 690: Spec rule annotation
//      - SPEC_RULE("Parse-Unary-Move");
//
//    Lines 691-692: Advance past keyword
//      - Parser next = parser;
//      - Advance(next);
//
//    Line 693: Parse place expression
//      - ParseElemResult<ExprPtr> place = ParsePlace(next, allow_brace);
//      - ParsePlace validates that the expression is a valid place
//
//    Lines 694-695: Construct AST node
//      - MoveExpr move;
//      - move.place = place.elem;
//
//    Lines 696-697: Return result
//      - return {place.parser,
//                MakeExpr(SpanCover(TokSpan(parser), place.elem->span), move)};
//      - Span covers from 'move' keyword to end of place expression
//
// COMPLETE SOURCE CODE (Lines 689-697):
// -----------------------------------------------------------------------------
//   if (IsKw(parser, "move")) {
//     SPEC_RULE("Parse-Unary-Move");
//     Parser next = parser;
//     Advance(next);
//     ParseElemResult<ExprPtr> place = ParsePlace(next, allow_brace);
//     MoveExpr move;
//     move.place = place.elem;
//     return {place.parser,
//             MakeExpr(SpanCover(TokSpan(parser), place.elem->span), move)};
//   }
//
// 2. IsPlace Helper Function (Lines 157-180)
//    ---------------------------------------------------------------------------
//    Used to validate place expressions for move, address-of, etc.
//
//    bool IsPlace(const ExprPtr& expr) {
//      if (!expr) return false;
//      if (std::holds_alternative<IdentifierExpr>(expr->node)) return true;
//      if (std::holds_alternative<FieldAccessExpr>(expr->node)) return true;
//      if (std::holds_alternative<TupleAccessExpr>(expr->node)) return true;
//      if (std::holds_alternative<IndexAccessExpr>(expr->node)) return true;
//      if (const auto* attr = std::get_if<AttributedExpr>(&expr->node)) {
//        return IsPlace(attr->expr);
//      }
//      if (const auto* deref = std::get_if<DerefExpr>(&expr->node)) {
//        return IsPlace(deref->value);
//      }
//      return false;
//    }
//
// 3. ParsePlace Function (Lines 2200+ in parser_expr.cpp, or separate file)
//    ---------------------------------------------------------------------------
//    ParseElemResult<ExprPtr> ParsePlace(Parser parser, bool allow_brace) {
//      ParseElemResult<ExprPtr> expr = ParsePostfix(parser, allow_brace, true);
//      // Additional validation may occur
//      return expr;
//    }
//
// =============================================================================
// RELATED EXPRESSIONS:
// =============================================================================
// Move expressions interact with:
// - Address-of (&): Also requires place expression (Lines 680-687)
// - Argument pass: Function arguments can use `move` or `copy` prefixes
//
// **(Parse-Unary-AddressOf)** Lines 5106-5109 (for comparison)
// IsOp(Tok(P), "&")    G |- ParsePlace(Advance(P)) => (P_1, p)
// -----------------------------------------------------------------------
// G |- ParseUnary(P) => (P_1, AddressOf(p))
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - ParsePlace(Parser, bool allow_brace) -> ParseElemResult<ExprPtr>
//   Parses a place expression (lvalue)
// - IsPlace(ExprPtr) -> bool
//   Validates that expression is a valid place
// - MakeExpr(Span, ExprNode) -> ExprPtr
//   Constructs an expression node with the given span
// - SpanCover(Span start, Span end) -> Span
//   Creates span covering from start to end
// - TokSpan(Parser) -> Span
//   Gets span of current token
// - IsKw(Parser, string_view) -> bool
//   Checks if current token is keyword with given lexeme
// - Advance(Parser&) -> void
//   Moves parser forward one token
// - MoveExpr AST node type
//   Fields: place (ExprPtr - the expression being moved)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - This is a unary expression parsed within ParseUnary
// - Similar structure to Parse-Unary-AddressOf
// - Consider extracting to standalone function for modularity:
//     ParseElemResult<ExprPtr> ParseMoveExpr(Parser parser, bool allow_brace);
// - Would be called from ParseUnary when 'move' keyword detected
// - The operand MUST be a place; non-place is a semantic error (not syntax)
// - Spec defines place expressions precisely; implementation should match
// - Note: allow_brace parameter controls whether record literals are allowed
// =============================================================================
