// Parser for key block statements.
//
// Surface grammar:
//   key_block_stmt ::= key_block_head key_path_list key_options? block_expr
//   key_block_head ::= "%read" | "%write" | "%release" key_mode | "%speculative" "write"
//   key_options ::= "[" key_option ("," key_option)* ","? "]"
//
// The # operator remains valid inside key paths as the acquisition-boundary
// marker; it is not the key-block statement introducer.

#include "02_source/parser/parser.h"

#include <optional>
#include <string_view>
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
void SkipNewlines(Parser& parser);
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

bool IsKeyOptionsAhead(Parser parser) {
  if (!IsPunc(parser, "[")) {
    return false;
  }

  Parser probe = parser;
  Advance(probe);
  bool saw_option = false;
  while (true) {
    const Token* tok = Tok(probe);
    if (!tok || tok->kind != TokenKind::Identifier ||
        tok->lexeme != "ordered") {
      return false;
    }
    saw_option = true;
    Advance(probe);

    if (IsPunc(probe, ",")) {
      Advance(probe);
      if (IsPunc(probe, "]")) {
        break;
      }
      continue;
    }
    break;
  }

  if (!saw_option || !IsPunc(probe, "]")) {
    return false;
  }

  Advance(probe);
  SkipNewlines(probe);
  return IsPunc(probe, "{");
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

  if (IsPunc(parser, "[") && !IsKeyOptionsAhead(parser)) {
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

struct KeyBlockHeadResult {
  Parser parser;
  KeyBlockKind kind = KeyBlockKind::Read;
  KeyMode mode = KeyMode::Read;
};

static bool Adjacent(const Token& left, const Token& right) {
  return left.span.file == right.span.file &&
         left.span.end_offset == right.span.start_offset;
}

KeyBlockHeadResult ParseKeyBlockHead(Parser parser) {
  const Token* percent = Tok(parser);
  if (!percent || percent->kind != TokenKind::Operator ||
      percent->lexeme != "%") {
    SPEC_RULE("Parse-KeyBlockHead-Err");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, KeyBlockKind::Read, KeyMode::Read};
  }

  Parser after_percent = parser;
  Advance(after_percent);
  const Token* head = Tok(after_percent);
  if (!head || head->kind != TokenKind::Identifier ||
      !Adjacent(*percent, *head)) {
    SPEC_RULE("Parse-KeyBlockHead-Err");
    EmitParseSyntaxErr(after_percent, TokSpan(after_percent));
    return {after_percent, KeyBlockKind::Read, KeyMode::Read};
  }

  if (head->lexeme == "read") {
    SPEC_RULE("Parse-KeyBlockHead-Read");
    Parser next = after_percent;
    Advance(next);
    return {next, KeyBlockKind::Read, KeyMode::Read};
  }

  if (head->lexeme == "write") {
    SPEC_RULE("Parse-KeyBlockHead-Write");
    Parser next = after_percent;
    Advance(next);
    return {next, KeyBlockKind::Write, KeyMode::Write};
  }

  if (head->lexeme == "release") {
    SPEC_RULE("Parse-KeyBlockHead-Release");
    Parser after_release = after_percent;
    Advance(after_release);
    ParseElemResult<KeyMode> release_mode = ParseKeyMode(after_release);
    return {release_mode.parser, KeyBlockKind::Release, release_mode.elem};
  }

  if (head->lexeme == "speculative") {
    SPEC_RULE("Parse-KeyBlockHead-SpeculativeWrite");
    Parser after_speculative = after_percent;
    Advance(after_speculative);
    const Token* mode = Tok(after_speculative);
    if (!mode || mode->kind != TokenKind::Identifier ||
        mode->lexeme != "write") {
      SPEC_RULE("Parse-KeyBlockHead-Err");
      EmitParseSyntaxErr(after_speculative, TokSpan(after_speculative));
      return {after_speculative, KeyBlockKind::SpeculativeWrite,
              KeyMode::Write};
    }
    Parser next = after_speculative;
    Advance(next);
    return {next, KeyBlockKind::SpeculativeWrite, KeyMode::Write};
  }

  SPEC_RULE("Parse-KeyBlockHead-Err");
  EmitParseSyntaxErr(after_percent, TokSpan(after_percent));
  return {after_percent, KeyBlockKind::Read, KeyMode::Read};
}

ParseElemResult<KeyBlockOptions> ParseKeyOptionsOpt(Parser parser) {
  KeyBlockOptions options;
  if (!IsPunc(parser, "[")) {
    SPEC_RULE("Parse-KeyOptionsOpt-None");
    return {parser, options};
  }

  Parser probe = parser;
  Advance(probe);
  bool saw_option = false;
  while (true) {
    const Token* tok = Tok(probe);
    if (!tok || tok->kind != TokenKind::Identifier ||
        tok->lexeme != "ordered") {
      EmitParseSyntaxErr(probe, TokSpan(probe));
      return {probe, options};
    }
    saw_option = true;
    options.ordered = true;
    SPEC_RULE("Parse-KeyOption-Ordered");
    Advance(probe);

    if (IsPunc(probe, ",")) {
      Advance(probe);
      if (IsPunc(probe, "]")) {
        break;
      }
      continue;
    }
    break;
  }

  if (!saw_option || !IsPunc(probe, "]")) {
    EmitParseSyntaxErr(probe, TokSpan(probe));
    return {probe, options};
  }

  Parser after_close = probe;
  Advance(after_close);
  SPEC_RULE("Parse-KeyOptionsOpt-Some");
  return {after_close, options};
}

// =============================================================================
// ParseKeyBlockStmt - Parse key block statement (%read path { })
// =============================================================================
//
// SPEC: Lines 6340-6343 (Parse-KeyBlock-Stmt)

ParseElemResult<Stmt> ParseKeyBlockStmt(Parser parser) {
  SPEC_RULE("Parse-KeyBlock-Stmt");
  Parser start = parser;
  KeyBlockHeadResult head = ParseKeyBlockHead(parser);

  // Parse key path list
  ParseElemResult<std::vector<KeyPathExpr>> paths = ParseKeyPathList(head.parser);

  ParseElemResult<KeyBlockOptions> options = ParseKeyOptionsOpt(paths.parser);

  // Parse block body
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(options.parser);

  KeyBlockStmt stmt;
  stmt.kind = head.kind;
  stmt.paths = std::move(paths.elem);
  stmt.mode = head.mode;
  stmt.options = options.elem;
  stmt.body = body.elem;
  stmt.span = SpanBetween(start, body.parser);
  return {body.parser, stmt};
}

// =============================================================================
// TryParseKeyBlockStmt - Try to parse key block statement
// =============================================================================
//
// Returns std::nullopt if not at % operator.

std::optional<ParseElemResult<Stmt>> TryParseKeyBlockStmt(Parser parser) {
  if (!IsOp(parser, "%")) {
    return std::nullopt;
  }
  return ParseKeyBlockStmt(parser);
}

}  // namespace ultraviolet::ast
