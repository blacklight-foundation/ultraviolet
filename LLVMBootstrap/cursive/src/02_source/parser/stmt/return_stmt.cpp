// =============================================================================
// MIGRATION MAPPING: return_stmt.cpp
// =============================================================================
// This file should contain parsing logic for return statements.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.10, Lines 6280-6283
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Return-Stmt)** Lines 6280-6283
// IsKw(Tok(P), `return`)
// Gamma |- ParseExprOpt(Advance(P)) => (P_1, e_opt)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, ReturnStmt(e_opt))
//
// TERMINATOR RULES:
// -----------------------------------------------------------------------------
// ReturnStmt does NOT require terminator (it's a control flow statement)
// - Terminator is optional for return statement
// - If present, it is consumed; if absent, no error
//
// **(ConsumeTerminatorOpt-Opt-Yes)** / **(ConsumeTerminatorOpt-Opt-No)**
// - Terminator is optional for control flow statements
//
// SEMANTICS:
// - `return` exits the current procedure with unit value ()
// - `return expr` exits the current procedure with given value
// - Return type must match procedure signature
// - Return from non-unit procedure requires expression
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Return statement parsing in ParseStmtCore (Lines 411-420)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 411-412: Check for return keyword
//      - if (IsKw(parser, "return"))
//
//    Lines 413-420: Parse return statement
//      - SPEC_RULE("Parse-Return-Stmt");
//        Parser next = parser;
//        Advance(next);  // consume `return`
//        ParseElemResult<ExprPtr> expr = ParseExprOpt(next);
//        ReturnStmt stmt;
//        stmt.value_opt = expr.elem;
//        stmt.span = SpanBetween(start, expr.parser);
//        return {expr.parser, stmt, true};
//
// 2. ApplyStmtAttrs for attributes (Lines 208-211)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 208-211: Apply attributes to return statement
//      - if (auto* ret = std::get_if<ReturnStmt>(&stmt)) {
//          ret->value_opt = WrapAttrExpr(attrs, ret->value_opt);
//          return;
//        }
//
// 3. ParseExprOpt helper (expression parsing)
//    ─────────────────────────────────────────────────────────────────────────
//    ParseExprOpt returns:
//      - nullptr if no expression follows (terminator or closing delimiter)
//      - The parsed expression otherwise
//
//    Used for optional expressions like `return` vs `return value`
//
// RETURN DATA STRUCTURE:
// =============================================================================
// struct ReturnStmt {
//   ExprPtr value_opt;     // Optional return value (nullptr for unit return)
//   core::Span span;       // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsKw helper function (parser utilities)
// - ParseExprOpt function (expr/expr_common.cpp)
// - ReturnStmt AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
// - SpanBetween helper function
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Return uses ParseExprOpt, not ParseExpr
// - ParseExprOpt handles the case where no expression follows
// - Span covers from `return` keyword to end of expression (or just keyword)
// - value_opt is nullptr for bare `return` (unit return)
// - Attributes are applied to the return value expression if present
// - Return does not require terminator but will consume one if present
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<ExprPtr> ParseExprOpt(Parser parser);

// =============================================================================
// ParseReturnStmt - Parse return statement
// =============================================================================
//
// SPEC: Lines 6280-6283 (Parse-Return-Stmt)
// `return` exits with unit, `return expr` exits with value.

ParseElemResult<Stmt> ParseReturnStmt(Parser parser) {
  SPEC_RULE("Parse-Return-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "return"

  // Parse optional return value
  ParseElemResult<ExprPtr> expr = ParseExprOpt(next);

  // Construct ReturnStmt
  ReturnStmt stmt;
  stmt.value_opt = expr.elem;
  stmt.span = SpanBetween(start, expr.parser);

  return {expr.parser, stmt};
}

// =============================================================================
// TryParseReturnStmt - Try to parse return statement
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseReturnStmt(Parser parser) {
  if (!IsKw(parser, "return")) {
    return std::nullopt;
  }
  return ParseReturnStmt(parser);
}

}  // namespace cursive::ast
