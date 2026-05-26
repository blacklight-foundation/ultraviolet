// =============================================================================
// MIGRATION MAPPING: break_stmt.cpp
// =============================================================================
// This file should contain parsing logic for break statements.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.10, Lines 6285-6288
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Break-Stmt)** Lines 6285-6288
// IsKw(Tok(P), `break`)
// Gamma |- ParseExprOpt(Advance(P)) => (P_1, e_opt)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, BreakStmt(e_opt))
//
// TERMINATOR RULES:
// -----------------------------------------------------------------------------
// BreakStmt does NOT require terminator (it's a control flow statement)
// - Terminator is optional for break statement
// - If present, it is consumed; if absent, no error
//
// SEMANTICS:
// - `break` exits the innermost loop with unit value
// - `break expr` exits the innermost loop with given value
// - The loop expression evaluates to the break value
// - Break must appear within a loop context
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Break statement parsing in ParseStmtCore (Lines 445-454)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 445-446: Check for break keyword
//      - if (IsKw(parser, "break"))
//
//    Lines 447-454: Parse break statement
//      - SPEC_RULE("Parse-Break-Stmt");
//        Parser next = parser;
//        Advance(next);  // consume `break`
//        ParseElemResult<ExprPtr> expr = ParseExprOpt(next);
//        BreakStmt stmt;
//        stmt.value_opt = expr.elem;
//        stmt.span = SpanBetween(start, expr.parser);
//        return {expr.parser, stmt, true};
//
// 2. ApplyStmtAttrs for attributes (Lines 212-215)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 212-215: Apply attributes to break statement
//      - if (auto* br = std::get_if<BreakStmt>(&stmt)) {
//          br->value_opt = WrapAttrExpr(attrs, br->value_opt);
//          return;
//        }
//
// BREAK DATA STRUCTURE:
// =============================================================================
// struct BreakStmt {
//   ExprPtr value_opt;     // Optional break value (nullptr for unit break)
//   core::Span span;       // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsKw helper function (parser utilities)
// - ParseExprOpt function (expr/expr_common.cpp)
// - BreakStmt AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
// - SpanBetween helper function
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Break uses ParseExprOpt, not ParseExpr
// - ParseExprOpt handles the case where no expression follows
// - Span covers from `break` keyword to end of expression (or just keyword)
// - value_opt is nullptr for bare `break` (unit break)
// - The break value becomes the value of the loop expression
// - Attributes are applied to the break value expression if present
// - Break does not require terminator but will consume one if present
// - Semantic validation ensures break appears within loop context
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<ExprPtr> ParseExprOpt(Parser parser);

// =============================================================================
// ParseBreakStmt - Parse break statement
// =============================================================================
//
// SPEC: Lines 6285-6288 (Parse-Break-Stmt)
// `break` exits loop with unit, `break expr` exits with value.

ParseElemResult<Stmt> ParseBreakStmt(Parser parser) {
  SPEC_RULE("Parse-Break-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "break"

  // Parse optional break value
  ParseElemResult<ExprPtr> expr = ParseExprOpt(next);

  // Construct BreakStmt
  BreakStmt stmt;
  stmt.value_opt = expr.elem;
  stmt.span = SpanBetween(start, expr.parser);

  return {expr.parser, stmt};
}

// =============================================================================
// TryParseBreakStmt - Try to parse break statement
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseBreakStmt(Parser parser) {
  if (!IsKw(parser, "break")) {
    return std::nullopt;
  }
  return ParseBreakStmt(parser);
}

}  // namespace ultraviolet::ast
