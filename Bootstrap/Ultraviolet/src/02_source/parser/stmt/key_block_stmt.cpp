// =============================================================================
// MIGRATION MAPPING: key_block_stmt.cpp
// =============================================================================
// This file should contain parsing logic for key block statements.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.10, Lines 6340-6343
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.8.13, Lines 5477-5592
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-KeyBlock-Stmt)** Lines 6340-6343
// IsOp(Tok(P), "#")
// Gamma |- ParseKeyPathList(Advance(P)) => (P_1, paths)
// Gamma |- ParseKeyBlockModsOpt(P_1) => (P_2, mods)
// Gamma |- ParseKeyModeOpt(P_2) => (P_3, mode_opt)
// Gamma |- ParseBlock(P_3) => (P_4, body)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_4, KeyBlockStmt(paths, mods, mode_opt, body))
//
// KEY PATH PARSING (Lines 5477-5530):
// -----------------------------------------------------------------------------
// **(Parse-KeyMarkerOpt-Yes)** Lines 5477-5480
// IsOp(Tok(P), "#")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyMarkerOpt(P) => (Advance(P), true)
//
// **(Parse-KeyMarkerOpt-No)** Lines 5482-5485
// NOT IsOp(Tok(P), "#")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyMarkerOpt(P) => (P, false)
//
// **(Parse-KeyField)** Lines 5487-5490
// Gamma |- ParseKeyMarkerOpt(P) => (P_1, marked)
// Gamma |- ParseIdent(P_1) => (P_2, name)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyField(P) => (P_2, Field(marked, name))
//
// **(Parse-KeyIndex)** Lines 5492-5495
// Gamma |- ParseKeyMarkerOpt(P) => (P_1, marked)
// Gamma |- ParseExpr(P_1) => (P_2, e)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyIndex(P) => (P_2, Index(marked, e))
//
// **(Parse-KeySegs-End)** Lines 5497-5500
// Tok(P) NOT IN {Punctuator("."), Punctuator("[")}
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeySegs(P, xs) => (P, xs)
//
// **(Parse-KeySegs-Field)** Lines 5502-5505
// IsPunc(Tok(P), ".")
// Gamma |- ParseKeyField(Advance(P)) => (P_1, seg)
// Gamma |- ParseKeySegs(P_1, xs ++ [seg]) => (P_2, ys)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeySegs(P, xs) => (P_2, ys)
//
// **(Parse-KeySegs-Index)** Lines 5507-5510
// IsPunc(Tok(P), "[")
// Gamma |- ParseKeyIndex(Advance(P)) => (P_1, seg)
// IsPunc(Tok(P_1), "]")
// Gamma |- ParseKeySegs(Advance(P_1), xs ++ [seg]) => (P_2, ys)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeySegs(P, xs) => (P_2, ys)
//
// **(Parse-KeyPathExpr)** Lines 5512-5515
// Gamma |- ParseIdent(P) => (P_1, root)
// Gamma |- ParseKeySegs(P_1, []) => (P_2, segs)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyPathExpr(P) => (P_2, <root, segs>)
//
// **(Parse-KeyPathList-Cons)** Lines 5517-5520
// Gamma |- ParseKeyPathExpr(P) => (P_1, kp)
// Gamma |- ParseKeyPathListTail(P_1, [kp]) => (P_2, ks)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyPathList(P) => (P_2, ks)
//
// **(Parse-KeyPathListTail-End)** Lines 5522-5525
// NOT IsPunc(Tok(P), ",")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyPathListTail(P, xs) => (P, xs)
//
// **(Parse-KeyPathListTail-Comma)** Lines 5527-5530
// IsPunc(Tok(P), ",")
// Gamma |- ParseKeyPathExpr(Advance(P)) => (P_1, kp)
// Gamma |- ParseKeyPathListTail(P_1, xs ++ [kp]) => (P_2, ys)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyPathListTail(P, xs) => (P_2, ys)
//
// KEY MODES AND MODIFIERS (Lines 5532-5582):
// -----------------------------------------------------------------------------
// **(Parse-KeyBlockMod-Dynamic)** Lines 5534-5537
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `dynamic`
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyBlockMod(P) => (Advance(P), Dynamic)
//
// **(Parse-KeyBlockMod-Speculative)** Lines 5539-5542
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `speculative`
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyBlockMod(P) => (Advance(P), Speculative)
//
// **(Parse-KeyBlockMod-Release)** Lines 5544-5547
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `release`
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyBlockMod(P) => (Advance(P), Release)
//
// **(Parse-KeyBlockModsOpt-None)** Lines 5549-5552
// NOT (IsIdent(Tok(P)) AND Lexeme(Tok(P)) IN {`dynamic`, `speculative`, `release`})
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyBlockModsOpt(P) => (P, [])
//
// **(Parse-KeyBlockModsOpt-Cons)** Lines 5554-5557
// Gamma |- ParseKeyBlockMod(P) => (P_1, m)
// Gamma |- ParseKeyBlockModsOpt(P_1) => (P_2, ms)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyBlockModsOpt(P) => (P_2, [m] ++ ms)
//
// **(Parse-KeyMode-Read)** Lines 5559-5562
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `read`
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyMode(P) => (Advance(P), Read)
//
// **(Parse-KeyMode-Write)** Lines 5564-5567
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `write`
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyMode(P) => (Advance(P), Write)
//
// **(Parse-KeyModeOpt-None)** Lines 5574-5577
// NOT (IsIdent(Tok(P)) AND Lexeme(Tok(P)) IN {`read`, `write`})
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyModeOpt(P) => (P, null)
//
// **(Parse-KeyModeOpt-Some)** Lines 5579-5582
// Gamma |- ParseKeyMode(P) => (P_1, mode)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseKeyModeOpt(P) => (P_1, mode)
//
// SEMANTICS:
// - `#path { ... }` acquires key for path, executes block, releases key
// - `#path1, path2 { ... }` acquires multiple keys atomically
// - `#path read { ... }` acquires read-only key
// - `#path write { ... }` acquires write key (default)
// - `#path dynamic { ... }` uses runtime key acquisition
// - `#path speculative { ... }` allows speculative execution
// - `#path release { ... }` releases keys after block completes
// - Key boundaries (marked with #) stop key propagation
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Key block statement parsing in ParseStmtCore (Lines 517-669)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 517-519: Check for # operator
//      - if (IsOp(parser, "#")) {
//          SPEC_RULE("Parse-Key-Block-Stmt");
//          Parser next = parser;
//          Advance(next);  // consume #
//
//    Lines 522-579: Parse key path list
//      - std::vector<KeyPathExpr> paths;
//        Parser path_start = next;
//        // Parse root identifier
//        if (const Token* root_tok = Tok(next); root_tok && IsIdentTok(*root_tok)) {
//          KeyPathExpr path_expr;
//          path_expr.root = root_tok->lexeme;
//          path_expr.span = root_tok->span;
//          Advance(next);
//
//          // Parse key segments (field/index)
//          while (true) {
//            if (IsPunc(next, ".")) {
//              // Field segment
//              Parser after_dot = next;
//              Advance(after_dot);
//              bool marked = false;
//              if (IsOp(after_dot, "#")) {
//                marked = true;
//                Advance(after_dot);
//              }
//              if (const Token* field_tok = Tok(after_dot); ...) {
//                KeySegField seg;
//                seg.marked = marked;
//                seg.name = field_tok->lexeme;
//                path_expr.segs.push_back(seg);
//                ...
//              }
//            } else if (IsPunc(next, "[")) {
//              // Index segment
//              ...
//            } else break;
//          }
//          paths.push_back(std::move(path_expr));
//
//          // Parse additional paths separated by comma
//          while (IsPunc(next, ",")) { ... }
//        }
//
//    Lines 628-643: Parse key block modifiers
//      - std::vector<KeyBlockMod> mods;
//        const Token* mod_tok = Tok(next);
//        while (mod_tok && mod_tok->kind == TokenKind::Identifier &&
//               (mod_tok->lexeme == "dynamic" || mod_tok->lexeme == "speculative" ||
//                mod_tok->lexeme == "release")) {
//          if (mod_tok->lexeme == "dynamic") {
//            mods.push_back(KeyBlockMod::Dynamic);
//          } else if (mod_tok->lexeme == "speculative") {
//            mods.push_back(KeyBlockMod::Speculative);
//          } else if (mod_tok->lexeme == "release") {
//            mods.push_back(KeyBlockMod::Release);
//          }
//          Advance(next);
//          mod_tok = Tok(next);
//        }
//
//    Lines 647-657: Parse key mode (read/write)
//      - std::optional<KeyMode> mode;
//        const Token* mode_tok = Tok(next);
//        if (mode_tok && mode_tok->kind == TokenKind::Identifier) {
//          if (mode_tok->lexeme == "read") {
//            mode = KeyMode::Read;
//            Advance(next);
//          } else if (mode_tok->lexeme == "write") {
//            mode = KeyMode::Write;
//            Advance(next);
//          }
//        }
//
//    Lines 659-668: Parse block and construct KeyBlockStmt
//      - ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
//        KeyBlockStmt stmt;
//        stmt.paths = std::move(paths);
//        stmt.mods = std::move(mods);
//        stmt.mode = mode;
//        stmt.body = block.elem;
//        stmt.span = SpanBetween(start, block.parser);
//        return {block.parser, stmt, true};
//
// KEY BLOCK DATA STRUCTURES:
// =============================================================================
// enum class KeyBlockMod {
//   Dynamic,      // Runtime key acquisition
//   Speculative,  // Allow speculative execution
//   Release       // Release keys after block
// };
//
// enum class KeyMode {
//   Read,   // Read-only access
//   Write   // Write access (default)
// };
//
// struct KeySegField {
//   bool marked;         // Has # marker (key boundary)
//   std::string name;    // Field name
// };
//
// struct KeySegIndex {
//   bool marked;         // Has # marker (key boundary)
//   ExprPtr expr;        // Index expression
// };
//
// using KeySeg = std::variant<KeySegField, KeySegIndex>;
//
// struct KeyPathExpr {
//   std::string root;           // Root identifier
//   std::vector<KeySeg> segs;   // Path segments
//   core::Span span;            // Source span
// };
//
// struct KeyBlockStmt {
//   std::vector<KeyPathExpr> paths;      // Key paths to acquire
//   std::vector<KeyBlockMod> mods;       // Block modifiers
//   std::optional<KeyMode> mode;         // Access mode (read/write)
//   std::shared_ptr<Block> body;         // Block body
//   core::Span span;                     // Source span
// };
//
// DEPENDENCIES:
// =============================================================================
// - IsOp, IsIdentTok, IsPunc helper functions
// - ParseExpr function (for index expressions)
// - ParseIdent function (for root and field names)
// - ParseBlock function
// - KeyPathExpr, KeyBlockStmt, KeyBlockMod, KeyMode AST types
// - ConsumeTerminatorOpt function (stmt_common.cpp)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Key block syntax: # path_list modifiers? mode? block
// - Modifiers (dynamic, speculative, release) are NOT keywords
// - Modes (read, write) are NOT keywords - they are contextual identifiers
// - Key boundaries use # marker within paths (e.g., data.#field)
// - Multiple paths are comma-separated
// - Path parsing includes field access (.) and index access ([])
// - Span covers from # to closing brace
// - No explicit terminator required (ends with block)
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
ParseElemResult<Identifier> ParseIdent(Parser parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// ParseKeyMarkerOpt - Parse optional # marker for key boundary
// =============================================================================
//
// SPEC: Lines 5477-5485 (Parse-KeyMarkerOpt-Yes/No)

struct KeyMarkerResult {
  Parser parser;
  bool marked;
};

KeyMarkerResult ParseKeyMarkerOpt(Parser parser) {
  if (IsOp(parser, "#")) {
    SPEC_RULE("Parse-KeyMarkerOpt-Yes");
    Parser next = parser;
    Advance(next);
    return {next, true};
  }
  SPEC_RULE("Parse-KeyMarkerOpt-No");
  return {parser, false};
}

ParseElemResult<KeySegField> ParseKeyField(Parser parser) {
  SPEC_RULE("Parse-KeyField");
  const KeyMarkerResult marker = ParseKeyMarkerOpt(parser);
  const ParseElemResult<Identifier> name = ParseIdent(marker.parser);

  KeySegField field;
  field.marked = marker.marked;
  field.name = name.elem;
  return {name.parser, field};
}

ParseElemResult<KeySegIndex> ParseKeyIndex(Parser parser) {
  SPEC_RULE("Parse-KeyIndex");
  const KeyMarkerResult marker = ParseKeyMarkerOpt(parser);
  const ParseElemResult<ExprPtr> expr = ParseExpr(marker.parser);

  KeySegIndex index;
  index.marked = marker.marked;
  index.expr = expr.elem;
  return {expr.parser, index};
}

ParseElemResult<std::vector<KeySeg>> ParseKeySegs(Parser parser,
                                                  std::vector<KeySeg> segs) {
  if (IsPunc(parser, ".")) {
    SPEC_RULE("Parse-KeySegs-Field");
    Parser after_dot = parser;
    Advance(after_dot);
    ParseElemResult<KeySegField> field = ParseKeyField(after_dot);
    segs.push_back(field.elem);
    return ParseKeySegs(field.parser, std::move(segs));
  }

  if (IsPunc(parser, "[")) {
    SPEC_RULE("Parse-KeySegs-Index");
    Parser after_l = parser;
    Advance(after_l);
    ParseElemResult<KeySegIndex> index = ParseKeyIndex(after_l);
    if (!IsPunc(index.parser, "]")) {
      EmitParseSyntaxErr(index.parser, TokSpan(index.parser));
      return {index.parser, std::move(segs)};
    }
    Parser after_r = index.parser;
    Advance(after_r);
    segs.push_back(index.elem);
    return ParseKeySegs(after_r, std::move(segs));
  }

  SPEC_RULE("Parse-KeySegs-End");
  return {parser, std::move(segs)};
}

// =============================================================================
// ParseKeyPathExpr - Parse complete key path (root.field[idx].field2)
// =============================================================================
//
// SPEC: Lines 5512-5515 (Parse-KeyPathExpr)

ParseElemResult<KeyPathExpr> ParseKeyPathExpr(Parser parser) {
  SPEC_RULE("Parse-KeyPathExpr");
  Parser start = parser;
  ParseElemResult<Identifier> root = ParseIdent(parser);
  ParseElemResult<std::vector<KeySeg>> tail =
      ParseKeySegs(root.parser, {});

  KeyPathExpr path;
  path.root = root.elem;
  path.segs = std::move(tail.elem);
  path.span = SpanBetween(start, tail.parser);
  return {tail.parser, path};
}

// =============================================================================
// ParseKeyPathListTail - Parse rest of key path list after first element
// =============================================================================
//
// SPEC: Lines 5522-5530 (Parse-KeyPathListTail-*)

ParseElemResult<std::vector<KeyPathExpr>> ParseKeyPathListTail(
    Parser parser, std::vector<KeyPathExpr> paths) {
  if (!IsPunc(parser, ",")) {
    SPEC_RULE("Parse-KeyPathListTail-End");
    return {parser, paths};
  }

  SPEC_RULE("Parse-KeyPathListTail-Comma");
  Parser after_comma = parser;
  Advance(after_comma);
  ParseElemResult<KeyPathExpr> next_path = ParseKeyPathExpr(after_comma);
  paths.push_back(std::move(next_path.elem));
  return ParseKeyPathListTail(next_path.parser, std::move(paths));
}

// =============================================================================
// ParseKeyPathList - Parse list of key paths
// =============================================================================
//
// SPEC: Lines 5517-5520 (Parse-KeyPathList-Cons)

ParseElemResult<std::vector<KeyPathExpr>> ParseKeyPathList(Parser parser) {
  SPEC_RULE("Parse-KeyPathList-Cons");
  ParseElemResult<KeyPathExpr> first = ParseKeyPathExpr(parser);
  std::vector<KeyPathExpr> paths;
  paths.push_back(std::move(first.elem));
  return ParseKeyPathListTail(first.parser, std::move(paths));
}

struct ModeModifiersResult {
  Parser parser;
  std::vector<KeyBlockMod> mods;
};

static bool HasKeyBlockMod(const std::vector<KeyBlockMod>& mods,
                           KeyBlockMod mod) {
  for (const auto candidate : mods) {
    if (candidate == mod) {
      return true;
    }
  }
  return false;
}

ParseElemResult<KeyMode> ParseKeyMode(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && tok->kind == TokenKind::Identifier && tok->lexeme == "read") {
    SPEC_RULE("Parse-KeyMode-Read");
    Parser next = parser;
    Advance(next);
    return {next, KeyMode::Read};
  }

  if (tok && tok->kind == TokenKind::Identifier && tok->lexeme == "write") {
    SPEC_RULE("Parse-KeyMode-Write");
    Parser next = parser;
    Advance(next);
    return {next, KeyMode::Write};
  }

  SPEC_RULE("Parse-KeyMode-Err");
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, KeyMode::Read};
}

ParseElemResult<std::optional<KeyMode>> ParseKeyModeOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier ||
      (tok->lexeme != "read" && tok->lexeme != "write")) {
    SPEC_RULE("Parse-KeyModeOpt-None");
    return {parser, std::nullopt};
  }

  ParseElemResult<KeyMode> mode = ParseKeyMode(parser);
  SPEC_RULE("Parse-KeyModeOpt-Some");
  return {mode.parser, mode.elem};
}

ModeModifiersResult ParseKeyBlockModsOpt(Parser parser) {
  std::vector<KeyBlockMod> mods;

  while (true) {
    const Token* tok = Tok(parser);
    if (!tok || tok->kind != TokenKind::Identifier) {
      break;
    }

    if (tok->lexeme == "dynamic") {
      SPEC_RULE("Parse-KeyBlockMod-Dynamic");
      mods.push_back(KeyBlockMod::Dynamic);
      Advance(parser);
      continue;
    }

    if (tok->lexeme == "ordered") {
      SPEC_RULE("Parse-KeyBlockMod-Ordered");
      mods.push_back(KeyBlockMod::Ordered);
      Advance(parser);
      continue;
    }

    if (tok->lexeme == "speculative") {
      SPEC_RULE("Parse-KeyBlockMod-Speculative");
      mods.push_back(KeyBlockMod::Speculative);
      Advance(parser);
      continue;
    }

    break;
  }

  if (mods.empty()) {
    SPEC_RULE("Parse-KeyBlockModsOpt-None");
  } else {
    SPEC_RULE("Parse-KeyBlockModsOpt-Cons");
  }
  return {parser, std::move(mods)};
}

struct KeyModeSpecOptResult {
  Parser parser;
  std::vector<KeyBlockMod> mods;
  std::optional<KeyMode> mode;
};

KeyModeSpecOptResult ParseKeyModeSpecOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier) {
    SPEC_RULE("Parse-KeyModeOpt-None");
    return {parser, {}, std::nullopt};
  }

  if (tok->lexeme == "release") {
    SPEC_RULE("Parse-KeyBlockMod-Release");
    Parser after_release = parser;
    Advance(after_release);

    ParseElemResult<KeyMode> release_mode = ParseKeyMode(after_release);
    if (release_mode.parser.index == after_release.index) {
      return {after_release, {}, std::nullopt};
    }

    return {release_mode.parser, {KeyBlockMod::Release}, release_mode.elem};
  }

  ParseElemResult<std::optional<KeyMode>> mode = ParseKeyModeOpt(parser);
  return {mode.parser, {}, mode.elem};
}

// =============================================================================
// ParseKeyBlockStmt - Parse key block statement (#path mode { })
// =============================================================================
//
// SPEC: Lines 6340-6343 (Parse-KeyBlock-Stmt)

ParseElemResult<Stmt> ParseKeyBlockStmt(Parser parser) {
  SPEC_RULE("Parse-KeyBlock-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "#"

  // Parse key path list
  ParseElemResult<std::vector<KeyPathExpr>> paths = ParseKeyPathList(next);

  // Parse key-block prefix modifiers.
  ModeModifiersResult modifiers = ParseKeyBlockModsOpt(paths.parser);

  // Parse the optional mode specifier: `read`, `write`, or `release read|write`.
  KeyModeSpecOptResult mode_spec = ParseKeyModeSpecOpt(modifiers.parser);

  // Parse block body
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(mode_spec.parser);

  KeyBlockStmt stmt;
  stmt.paths = std::move(paths.elem);
  stmt.mods = std::move(modifiers.mods);
  stmt.mods.insert(stmt.mods.end(), mode_spec.mods.begin(), mode_spec.mods.end());
  stmt.mode = mode_spec.mode;
  stmt.body = body.elem;
  stmt.span = SpanBetween(start, body.parser);
  return {body.parser, stmt};
}

// =============================================================================
// TryParseKeyBlockStmt - Try to parse key block statement
// =============================================================================
//
// Returns std::nullopt if not at # operator.

std::optional<ParseElemResult<Stmt>> TryParseKeyBlockStmt(Parser parser) {
  if (!IsOp(parser, "#")) {
    return std::nullopt;
  }
  return ParseKeyBlockStmt(parser);
}

}  // namespace ultraviolet::ast
