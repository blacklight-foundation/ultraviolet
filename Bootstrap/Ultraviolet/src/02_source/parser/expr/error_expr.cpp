// =============================================================================
// MIGRATION MAPPING: error_expr.cpp
// =============================================================================
// This file should contain parsing logic for error recovery expressions and
// the ErrorExpr sentinel node used when parsing fails.
//
// SPEC REFERENCE: SPECIFICATION.md
// -----------------------------------------------------------------------------
// **(Parse-Primary-Err)** Lines 5390-5393
//   c = Code(Parse-Syntax-Err)    Γ ⊢ Emit(c, Tok(P).span)
//   P_1 = AdvanceOrEOF(P)    Γ ⊢ SyncStmt(P_1) ⇓ P_2
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParsePrimary(P) ⇓ (P_2, ErrorExpr(SpanBetween(P, P_2)))
//
// **(Parse-Comptime-Unsupported)** Lines 5153-5156
//   IsIdent(Tok(P))    Lexeme(Tok(P)) = `comptime`    IsPunc(Tok(Advance(P)), "{")
//   Γ ⊢ Emit(Code(Unsupported-Construct))    Γ ⊢ SyncStmt(P) ⇓ P_1
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParsePrimary(P) ⇓ (P_1, ErrorExpr(SpanBetween(P, P_1)))
//
// **(Parse-Place-Err)** Lines 6061-6063
//   Γ ⊢ ParsePostfix(P) ⇓ (P_1, e)    ¬ IsPlace(e)
//   c = Code(PlaceExprParseErr)    Γ ⊢ Emit(c, Tok(P).span)    Γ ⊢ SyncStmt(P_1) ⇓ P_2
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParsePlace(P) ⇓ (P_2, ErrorExpr(SpanBetween(P, P_2)))
//
// ErrorExpr Semantics (Lines 9883-9887, 16071-16073, 19745-19747):
// - Type: ErrorExpr has type `!` (never type)
// - Lowering: ErrorExpr lowers to panic IR
// - Evaluation: ErrorExpr evaluates to Ctrl(Panic)
//
// SEMANTICS:
// - ErrorExpr is a sentinel AST node for parse failures
// - Created when no valid primary expression can be parsed
// - Carries span information for error reporting
// - Has type `!` (never/bottom type) to not interfere with type checking
// - Triggers panic at runtime if reached
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// 1. ErrorExpr creation on parse failure
//    Source: parser_expr.cpp (scattered throughout ParsePrimary)
//    Purpose: Create ErrorExpr node when parsing cannot continue
//    Example locations:
//    - Line 1134: Unsupported lexeme handling
//    - Line 1154: Unsupported qualified generic
//    - Line 1165: Failed QualifiedApply paren parsing
//    - Line 1184: Failed QualifiedApply brace parsing
//    - Line 1216: Failed parenthesized expr parsing
//    - Line 1230: Failed tuple literal parsing
//    - Line 1271, 1302, 1312: Failed array literal parsing
//    - Line 1363, 1373, 1395: Failed modal/record literal parsing
//    (and many more throughout the file)
//
// 2. Error emission helpers
//    Source: parser_expr.cpp, EmitParseSyntaxErr calls
//    Purpose: Emit E-SRC-0520 diagnostic before creating ErrorExpr
//
// 3. SyncStmt recovery
//    Source: parser_sync.cpp or parser_common.cpp
//    Purpose: Advance parser to synchronization point after error
//
// DEPENDENCIES:
// - Requires: ErrorExpr AST node type (variant in Expr)
// - Requires: EmitParseSyntaxErr function (diagnostic emission)
// - Requires: SyncStmt function (error recovery synchronization)
// - Requires: SpanBetween helper for span computation
// - Requires: MakeExpr factory function
// - Requires: AdvanceOrEOF helper (advance but don't go past EOF)
//
// REFACTORING NOTES:
// - ErrorExpr creation is scattered throughout parsing code
// - Consider a helper function: MakeErrorExpr(Parser start, Parser end)
// - The pattern is consistent: emit diagnostic -> sync -> create ErrorExpr
// - This file may not need standalone parsing logic; instead it provides
//   the error recovery pattern used by other expression parsers
// - Consider implementing as a utility header with inline helpers
// =============================================================================
