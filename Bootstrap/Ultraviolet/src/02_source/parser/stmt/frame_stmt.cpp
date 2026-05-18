// =============================================================================
// MIGRATION MAPPING: frame_stmt.cpp
// =============================================================================
// This file should contain parsing logic for frame statements.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.10, Lines 6330-6338
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Frame-Stmt)** Lines 6330-6333
// IsKw(Tok(P), `frame`)
// Gamma |- ParseBlock(Advance(P)) => (P_1, b)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, FrameStmt(null, b))
//
// **(Parse-Frame-Explicit)** Lines 6335-6338
// IsIdent(Tok(P))
// IsPunc(Tok(Advance(P)), ".")
// IsKw(Tok(Advance(Advance(P))), `frame`)
// name = Lexeme(Tok(P))
// Gamma |- ParseBlock(Advance(Advance(Advance(P)))) => (P_1, b)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, FrameStmt(name, b))
//
// SEMANTICS:
// - `frame { ... }` creates a sub-frame within current region
// - `region_name.frame { ... }` creates frame in specific named region
// - Frame subdivides a region for temporary allocations
// - Allocations in frame are deallocated when frame exits
// - Parent region remains active after frame exits
// - Useful for temporary working memory within a larger region
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Simple frame statement parsing in ParseStmtCore (Lines 503-513)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 503-504: Check for frame keyword
//      - if (IsKw(parser, "frame"))
//
//    Lines 505-513: Parse simple frame
//      - SPEC_RULE("Parse-Frame-Stmt");
//        Parser next = parser;
//        Advance(next);  // consume `frame`
//        ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
//        FrameStmt stmt;
//        stmt.target_opt = std::nullopt;  // No explicit target
//        stmt.body = block.elem;
//        stmt.span = SpanBetween(start, block.parser);
//        return {block.parser, stmt, true};
//
// 2. Explicit frame statement parsing in ParseStmtCore (Lines 671-688)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 671-672: Check for identifier followed by `.frame`
//      - if (tok->kind == TokenKind::Identifier)
//
//    Lines 673-688: Parse explicit frame
//      - Parser after_name = parser;
//        Advance(after_name);
//        if (IsPunc(after_name, ".") &&
//            IsKw(AdvanceOrEOF(after_name), "frame")) {
//          SPEC_RULE("Parse-Frame-Explicit");
//          Identifier name = tok->lexeme;
//          Parser after_dot = after_name;
//          Advance(after_dot);
//          Parser after_frame = after_dot;
//          Advance(after_frame);
//          ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(after_frame);
//          FrameStmt stmt;
//          stmt.target_opt = name;  // Named region target
//          stmt.body = block.elem;
//          stmt.span = SpanBetween(start, block.parser);
//          return {block.parser, stmt, true};
//        }
//
// FRAME DATA STRUCTURE:
// =============================================================================
// struct FrameStmt {
//   std::optional<Identifier> target_opt;  // Optional region name to target
//   std::shared_ptr<Block> body;           // The frame body block
//   core::Span span;                       // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsKw helper function (parser utilities)
// - IsPunc helper function (parser utilities)
// - AdvanceOrEOF helper function (parser utilities)
// - ParseBlock function (block.cpp or stmt_common.cpp)
// - FrameStmt AST node type
// - Block AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - SpanBetween helper function
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Two forms: `frame { }` and `name.frame { }`
// - Simple frame: parser at `frame` keyword
// - Explicit frame: parser at identifier, look ahead for `.frame`
// - Explicit frame parsing happens BEFORE expression parsing in ParseStmtCore
// - This is because `name.frame { }` could be confused with field access
// - The look-ahead pattern `identifier "." "frame"` disambiguates
// - target_opt is std::nullopt for simple frame, Identifier for explicit
// - Span covers from start (keyword or name) to closing brace
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
bool IsPunc(const Parser& parser, std::string_view p);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// ParseFrameStmt - Parse simple frame statement
// =============================================================================
//
// SPEC: Lines 6330-6333 (Parse-Frame-Stmt)
// `frame { ... }` creates sub-frame in current region.

ParseElemResult<Stmt> ParseFrameStmt(Parser parser) {
  SPEC_RULE("Parse-Frame-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "frame"

  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);

  FrameStmt stmt;
  stmt.target_opt = std::nullopt;  // No explicit target
  stmt.body = block.elem;
  stmt.span = SpanBetween(start, block.parser);

  return {block.parser, stmt};
}

// =============================================================================
// ParseExplicitFrameStmt - Parse explicit frame statement (name.frame { })
// =============================================================================
//
// SPEC: Lines 6335-6338 (Parse-Frame-Explicit)
// `name.frame { ... }` creates frame in named region.

ParseElemResult<Stmt> ParseExplicitFrameStmt(Parser parser, Identifier name) {
  SPEC_RULE("Parse-Frame-Explicit");
  Parser start = parser;

  // Skip: name, ".", "frame"
  Parser next = parser;
  Advance(next);  // skip identifier
  Advance(next);  // skip "."
  Advance(next);  // skip "frame"

  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);

  FrameStmt stmt;
  stmt.target_opt = name;
  stmt.body = block.elem;
  stmt.span = SpanBetween(start, block.parser);

  return {block.parser, stmt};
}

// =============================================================================
// TryParseFrameStmt - Try to parse frame statement
// =============================================================================
//
// Returns std::nullopt if not at "frame" keyword or "name.frame" pattern.

std::optional<ParseElemResult<Stmt>> TryParseFrameStmt(Parser parser) {
  // Simple frame: `frame { }`
  if (IsKw(parser, "frame")) {
    return ParseFrameStmt(parser);
  }

  // Explicit frame: `name.frame { }`
  const Token* tok = Tok(parser);
  if (tok && IsIdentTok(*tok)) {
    Parser after_name = parser;
    Advance(after_name);
    if (IsPunc(after_name, ".")) {
      Parser after_dot = after_name;
      Advance(after_dot);
      if (IsKw(after_dot, "frame")) {
        Identifier name = std::string(tok->lexeme);
        return ParseExplicitFrameStmt(parser, name);
      }
    }
  }

  return std::nullopt;
}

}  // namespace ultraviolet::ast
