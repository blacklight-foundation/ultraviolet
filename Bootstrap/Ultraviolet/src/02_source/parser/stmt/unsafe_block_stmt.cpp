// =============================================================================
// MIGRATION MAPPING: unsafe_block_stmt.cpp
// =============================================================================
// This file should contain parsing logic for unsafe block statements.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.10, Lines 6295-6298
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Unsafe-Block)** Lines 6295-6298
// IsKw(Tok(P), `unsafe`)
// Gamma |- ParseBlock(Advance(P)) => (P_1, b)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, UnsafeBlockStmt(b))
//
// TERMINATOR RULES:
// -----------------------------------------------------------------------------
// UnsafeBlockStmt does NOT require terminator (ends with block)
// - Terminator is optional for unsafe block statement
// - If present, it is consumed; if absent, no error
//
// SEMANTICS:
// - `unsafe { ... }` allows unsafe operations within the block
// - Required for: raw pointer dereference, transmute, FFI calls
// - Unsafe blocks disable certain safety checks
// - Unsafe does not mean "incorrect" - it means "manually verified"
// - The programmer takes responsibility for memory safety
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Unsafe block statement parsing in ParseStmtCore (Lines 465-474)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 465-466: Check for unsafe keyword
//      - if (IsKw(parser, "unsafe"))
//
//    Lines 467-474: Parse unsafe block
//      - SPEC_RULE("Parse-Unsafe-Block");
//        Parser next = parser;
//        Advance(next);  // consume `unsafe`
//        ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
//        UnsafeBlockStmt stmt;
//        stmt.body = block.elem;
//        stmt.span = SpanBetween(start, block.parser);
//        return {block.parser, stmt, true};
//
// NOTE: There is also UnsafeBlockExpr (expression form)
// -----------------------------------------------------------------------------
// UnsafeBlockStmt vs UnsafeBlockExpr:
// - UnsafeBlockStmt: statement context (parsed in ParseStmtCore)
// - UnsafeBlockExpr: expression context (parsed in expression parsing)
// - Both have same syntax: `unsafe { ... }`
// - Context determines which is produced
//
// In statement context (this file):
//   `unsafe { body }` -> UnsafeBlockStmt
//
// In expression context (expr/unsafe_block.cpp):
//   `let x = unsafe { value }` -> UnsafeBlockExpr
//
// UNSAFE BLOCK DATA STRUCTURE:
// =============================================================================
// struct UnsafeBlockStmt {
//   std::shared_ptr<Block> body;   // The unsafe block body
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
// - UnsafeBlockStmt AST node type
// - Block AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - SpanBetween helper function
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Unsafe block is simple: keyword followed by block
// - Block must use braces `{ ... }`
// - Span covers from `unsafe` keyword to closing brace
// - No attributes handling specific to unsafe (standard stmt attr handling)
// - UnsafeBlockStmt is parsed when `unsafe` appears at statement position
// - When `unsafe` appears in expression position, UnsafeBlockExpr is used
// - The check for unsafe keyword happens before expression parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// ParseUnsafeBlockStmt - Parse unsafe block statement
// =============================================================================
//
// SPEC: Lines 6295-6298 (Parse-Unsafe-Block)
// `unsafe { ... }` enables unsafe operations within block.

ParseElemResult<Stmt> ParseUnsafeBlockStmt(Parser parser) {
  SPEC_RULE("Parse-Unsafe-Block");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "unsafe"

  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);

  UnsafeBlockStmt stmt;
  stmt.body = block.elem;
  stmt.span = SpanBetween(start, block.parser);

  return {block.parser, stmt};
}

// =============================================================================
// TryParseUnsafeBlockStmt - Try to parse unsafe block statement
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseUnsafeBlockStmt(Parser parser) {
  if (!IsKw(parser, "unsafe")) {
    return std::nullopt;
  }
  return ParseUnsafeBlockStmt(parser);
}

}  // namespace ultraviolet::ast
