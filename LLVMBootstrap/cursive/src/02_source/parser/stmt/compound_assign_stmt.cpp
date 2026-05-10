// =============================================================================
// MIGRATION MAPPING: compound_assign_stmt.cpp
// =============================================================================
// This file should contain parsing logic for compound assignment statements.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.10, Lines 6275-6278, 6411-6414
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Assign-Stmt)** Lines 6275-6278
// Gamma |- ParsePlace(P) => (P_1, p)
// Tok(P_1) in {Operator("="), Operator("+="), Operator("-="), Operator("*="),
//              Operator("/="), Operator("%=")}
// Gamma |- ParseExpr(Advance(P_1)) => (P_2, e)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_2, AssignOrCompound(P_1, p, e))
//
// **(AssignOrCompound-Compound)** Lines 6411-6414
// Tok(P_1) = Operator(op)    op in {"+=", "-=", "*=", "/=", "%="}
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- AssignOrCompound(P_1, p, e) => CompoundAssignStmt(p, op, e)
//
// TERMINATOR RULES (Lines 6360-6370):
// -----------------------------------------------------------------------------
// ReqTerm(s) <=> s in {..., CompoundAssignStmt(_, _, _), ...}
//
// **(ConsumeTerminatorOpt-Req-Yes)** Lines 6362-6365
// ReqTerm(s)    IsTerm(Tok(P))
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ConsumeTerminatorOpt(P, s) => Advance(P)
//
// SEMANTICS:
// - Compound assignment `place op= expr` is shorthand for `place = place op expr`
// - Supported operators: +=, -=, *=, /=, %=
// - Place expression must be valid L-value
// - Statements require terminator (`;` or newline)
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. IsCompoundAssignOp helper function (Lines 47-53)
//    ─────────────────────────────────────────────────────────────────────────
//    bool IsCompoundAssignOp(const Token& tok) {
//      if (tok.kind != TokenKind::Operator) {
//        return false;
//      }
//      return tok.lexeme == "+=" || tok.lexeme == "-=" || tok.lexeme == "*=" ||
//             tok.lexeme == "/=" || tok.lexeme == "%=";
//    }
//
// 2. Compound assignment parsing in ParseStmtCore (Lines 694-726)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 695-698: Parse left-hand side as expression
//      - ParseElemResult<ExprPtr> expr = ParseExpr(parser);
//
//    Lines 699-700: Check for assignment operator (including compound)
//      - const Token* op = Tok(expr.parser);
//        if (op && IsAssignOp(*op))
//
//    Lines 701-707: Validate place expression (shared with simple assign)
//      - SPEC_RULE("Parse-Assign-Stmt");
//        if (!IsPlaceExpr(expr.elem)) {
//          EmitParseSyntaxErr(expr.parser, TokSpan(parser));
//          Parser sync = expr.parser;
//          SyncStmt(sync);
//          return {sync, ErrorStmt{SpanBetween(start, sync)}, true};
//        }
//
//    Lines 708-710: Parse right-hand side
//      - Parser next = expr.parser;
//        Advance(next);  // consume operator
//        ParseElemResult<ExprPtr> rhs = ParseExpr(next);
//
//    Lines 711-718: Construct CompoundAssignStmt (when op is compound)
//      - if (IsCompoundAssignOp(*op)) {
//          SPEC_RULE("AssignOrCompound-Compound");
//          CompoundAssignStmt stmt;
//          stmt.place = expr.elem;
//          stmt.op = op->lexeme;
//          stmt.value = rhs.elem;
//          stmt.span = SpanBetween(start, rhs.parser);
//          return {rhs.parser, stmt, true};
//        }
//
// 3. RequiresTerminator check (Lines 145-158)
//    ─────────────────────────────────────────────────────────────────────────
//    Line 158: CompoundAssignStmt requires terminator
//      - return ... || std::holds_alternative<CompoundAssignStmt>(stmt);
//
// 4. ApplyStmtAttrs for attributes (Lines 199-204)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 199-204: Apply attributes to compound assign statement
//      - if (auto* assign = std::get_if<CompoundAssignStmt>(&stmt)) {
//          assign->place = WrapAttrExpr(attrs, assign->place);
//          assign->value = WrapAttrExpr(attrs, assign->value);
//          return;
//        }
//
// COMPOUND ASSIGN DATA STRUCTURE:
// =============================================================================
// struct CompoundAssignStmt {
//   ExprPtr place;         // Place expression (L-value)
//   std::string op;        // Compound operator ("+=", "-=", "*=", "/=", "%=")
//   ExprPtr value;         // Value expression (R-value)
//   core::Span span;       // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsAssignOp helper function (stmt_common.cpp or assign_stmt.cpp)
// - IsCompoundAssignOp helper function (stmt_common.cpp or here)
// - IsPlaceExpr validation function (stmt_common.cpp or assign_stmt.cpp)
// - ParseExpr function (expr/*.cpp)
// - CompoundAssignStmt AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
// - SyncStmt recovery function (stmt_common.cpp)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Compound assignment shares parsing logic with simple assignment
// - The operator determines which statement type to produce
// - Compound operators: +=, -=, *=, /=, %=
// - Simple = produces AssignStmt; compound ops produce CompoundAssignStmt
// - Place validation is shared with assign_stmt.cpp
// - The operator string is stored in the AST for code generation
// - Span covers from place expression to value expression
// - Attributes can be applied to both place and value
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <string>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from other modules
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
bool IsPlaceExpr(const ExprPtr& expr);  // From assign_stmt.cpp

// =============================================================================
// IsCompoundAssignOp - Check if token is a compound assignment operator
// =============================================================================

bool IsCompoundAssignOp(const Token& tok) {
  if (tok.kind != TokenKind::Operator) {
    return false;
  }
  return tok.lexeme == "+=" || tok.lexeme == "-=" || tok.lexeme == "*=" ||
         tok.lexeme == "/=" || tok.lexeme == "%=";
}

// =============================================================================
// ParseCompoundAssignStmt - Parse compound assignment (place op= expr)
// =============================================================================
//
// SPEC: Lines 6275-6278 (Parse-Assign-Stmt), 6411-6414 (AssignOrCompound-Compound)
// Assumes lhs expression and compound operator have been identified.

ParseElemResult<Stmt> ParseCompoundAssignStmt(Parser start, ExprPtr place,
                                               Parser at_op) {
  SPEC_RULE("Parse-Assign-Stmt");
  SPEC_RULE("AssignOrCompound-Compound");

  const Token* op = Tok(at_op);
  std::string op_str = std::string(op->lexeme);

  Parser next = at_op;
  Advance(next);  // consume operator

  ParseElemResult<ExprPtr> rhs = ParseExpr(next);

  CompoundAssignStmt stmt;
  stmt.place = place;
  stmt.op = op_str;
  stmt.value = rhs.elem;
  stmt.span = SpanBetween(start, rhs.parser);

  return {rhs.parser, stmt};
}

// =============================================================================
// TryParseCompoundAssignStmt - Check and parse compound assignment
// =============================================================================
//
// Called after expression is parsed and compound operator detected.

std::optional<ParseElemResult<Stmt>> TryParseCompoundAssignStmt(
    Parser start, ExprPtr place, Parser at_op) {
  const Token* op = Tok(at_op);
  if (!op || !IsCompoundAssignOp(*op)) {
    return std::nullopt;
  }
  if (!IsPlaceExpr(place)) {
    EmitParseSyntaxErr(at_op, TokSpan(start));
    ErrorStmt err;
    err.span = SpanBetween(start, at_op);
    return ParseElemResult<Stmt>{at_op, err};
  }
  return ParseCompoundAssignStmt(start, place, at_op);
}

}  // namespace cursive::ast
