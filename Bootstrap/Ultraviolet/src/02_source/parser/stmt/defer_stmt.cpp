// =============================================================================
// MIGRATION MAPPING: defer_stmt.cpp
// =============================================================================
// This file should contain parsing logic for defer statements.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.10, Lines 6300-6303
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Defer-Stmt)** Lines 6300-6303
// IsKw(Tok(P), `defer`)
// Gamma |- ParseBlock(Advance(P)) => (P_1, b)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, DeferStmt(b))
//
// TERMINATOR RULES:
// -----------------------------------------------------------------------------
// DeferStmt does NOT require terminator (ends with block)
//
// **(ConsumeTerminatorOpt-Opt-Yes)** / **(ConsumeTerminatorOpt-Opt-No)**
// - Terminator is optional for defer statement
// - If present, it is consumed; if absent, no error
//
// SEMANTICS:
// - `defer { ... }` schedules block execution when scope exits
// - Deferred blocks execute in LIFO order (last-in, first-out)
// - Block is executed regardless of how scope exits (return, break, panic)
// - Typically used for cleanup (closing handles, releasing resources)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Defer statement parsing in ParseStmtCore (Lines 476-485)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 476-477: Check for defer keyword
//      - if (IsKw(parser, "defer"))
//
//    Lines 478-485: Parse defer block
//      - SPEC_RULE("Parse-Defer-Stmt");
//        Parser next = parser;
//        Advance(next);  // consume `defer`
//        ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
//        DeferStmt stmt;
//        stmt.body = block.elem;
//        stmt.span = SpanBetween(start, block.parser);
//        return {block.parser, stmt, true};
//
// 2. RequiresTerminator check
//    ─────────────────────────────────────────────────────────────────────────
//    - DeferStmt is NOT in RequiresTerminator list
//    - Block-ending statements have optional terminators
//
// DEFER DATA STRUCTURE:
// =============================================================================
// struct DeferStmt {
//   std::shared_ptr<Block> body;   // The block to execute on scope exit
//   core::Span span;               // Source span
// };
//
// struct Block {
//   std::vector<Stmt> stmts;       // Statements in the block
//   ExprPtr tail_opt;              // Optional tail expression (for block value)
//   core::Span span;               // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsKw helper function (parser utilities)
// - ParseBlock function (block.cpp or stmt_common.cpp)
// - DeferStmt AST node type
// - Block AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - SpanBetween helper function
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Defer is simple: keyword followed by block
// - No optional components (unlike region which has opts and alias)
// - Block must use braces `{ ... }`
// - Span covers from `defer` keyword to closing brace
// - No attributes handling specific to defer (standard stmt attr handling)
// - Defer body cannot reference variables declared after the defer
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// ParseDeferStmt - Parse defer statement
// =============================================================================
//
// SPEC: Lines 6300-6303 (Parse-Defer-Stmt)
// `defer { ... }` schedules block execution when scope exits.

ParseElemResult<Stmt> ParseDeferStmt(Parser parser) {
  SPEC_RULE("Parse-Defer-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "defer"

  // Parse the block
  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);

  // Construct DeferStmt
  DeferStmt stmt;
  stmt.body = block.elem;
  stmt.span = SpanBetween(start, block.parser);

  return {block.parser, stmt};
}

// =============================================================================
// TryParseDeferStmt - Try to parse defer statement
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseDeferStmt(Parser parser) {
  if (!IsKw(parser, "defer")) {
    return std::nullopt;
  }
  return ParseDeferStmt(parser);
}

}  // namespace ultraviolet::ast
