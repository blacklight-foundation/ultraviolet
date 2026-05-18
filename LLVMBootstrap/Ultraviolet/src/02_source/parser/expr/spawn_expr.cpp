// =============================================================================
// spawn_expr.cpp - Spawn Expression Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <memory>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<ExprPtr> ParseLiteralExpr(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

namespace {

bool IsStringLiteralToken(const Token* tok) {
  return tok && tok->kind == TokenKind::StringLiteral;
}

ParseElemResult<SpawnOption> ParseSpawnOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    SpawnOption err;
    err.kind = SpawnOptionKind::Name;
    err.span = TokSpan(parser);
    return {parser, err};
  }

  core::Span opt_start = TokSpan(parser);
  SpawnOption opt;

  if (tok->lexeme == "name") {
    SPEC_RULE("Parse-SpawnOpt-Name");
    opt.kind = SpawnOptionKind::Name;
  } else if (tok->lexeme == "affinity") {
    SPEC_RULE("Parse-SpawnOpt-Affinity");
    opt.kind = SpawnOptionKind::Affinity;
  } else if (tok->lexeme == "priority") {
    SPEC_RULE("Parse-SpawnOpt-Priority");
    opt.kind = SpawnOptionKind::Priority;
  } else {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    opt.kind = SpawnOptionKind::Name;
    opt.span = TokSpan(parser);
    return {parser, opt};
  }

  Parser cur = parser;
  Advance(cur);
  if (!IsPunc(cur, ":")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    opt.span = SpanCover(opt_start, TokSpan(cur));
    return {cur, opt};
  }
  Advance(cur);

  if (opt.kind == SpawnOptionKind::Name) {
    if (!IsStringLiteralToken(Tok(cur))) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      opt.span = SpanCover(opt_start, TokSpan(cur));
      return {cur, opt};
    }
    ParseElemResult<ExprPtr> value = ParseLiteralExpr(cur);
    opt.value = value.elem;
    opt.span = SpanCover(opt_start, TokSpan(value.parser));
    return {value.parser, opt};
  }

  ParseElemResult<ExprPtr> value = ParseExpr(cur);
  opt.value = value.elem;
  opt.span = SpanCover(opt_start, TokSpan(value.parser));
  return {value.parser, opt};
}

ParseElemResult<std::vector<SpawnOption>> ParseSpawnOptListTail(
    Parser parser,
    std::vector<SpawnOption> opts) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-SpawnOptListTail-End");
    return {parser, std::move(opts)};
  }

  if (!IsPunc(parser, ",")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, std::move(opts)};
  }

  Parser after_comma = parser;
  Advance(after_comma);
  SkipNewlines(after_comma);
  if (IsPunc(after_comma, "]")) {
    const std::array<EndSetToken, 1> end_set = {EndPunct("]")};
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-SpawnOptListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after_comma.diags = parser.diags;
    return {after_comma, std::move(opts)};
  }

  SPEC_RULE("Parse-SpawnOptListTail-Comma");
  ParseElemResult<SpawnOption> next = ParseSpawnOpt(after_comma);
  opts.push_back(std::move(next.elem));
  return ParseSpawnOptListTail(next.parser, std::move(opts));
}

ParseElemResult<std::vector<SpawnOption>> ParseSpawnOptList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-SpawnOptList-Empty");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  SPEC_RULE("Parse-SpawnOptList-Cons");
  ParseElemResult<SpawnOption> first = ParseSpawnOpt(parser);
  std::vector<SpawnOption> opts;
  opts.push_back(std::move(first.elem));
  return ParseSpawnOptListTail(first.parser, std::move(opts));
}

ParseElemResult<std::vector<SpawnOption>> ParseSpawnOptsOpt(Parser parser) {
  if (!IsPunc(parser, "[")) {
    SPEC_RULE("Parse-SpawnOptsOpt-None");
    return {parser, {}};
  }

  SPEC_RULE("Parse-SpawnOptsOpt-Yes");
  Parser after_lbracket = parser;
  Advance(after_lbracket);
  ParseElemResult<std::vector<SpawnOption>> opts =
      ParseSpawnOptList(after_lbracket);
  if (!IsPunc(opts.parser, "]")) {
    EmitParseSyntaxErr(opts.parser, TokSpan(opts.parser));
    Parser sync = opts.parser;
    while (Tok(sync) && !IsPunc(sync, "]") && !IsPunc(sync, "{")) {
      Advance(sync);
    }
    if (IsPunc(sync, "]")) {
      Advance(sync);
    }
    return {sync, std::move(opts.elem)};
  }

  Parser after_rbracket = opts.parser;
  Advance(after_rbracket);
  return {after_rbracket, std::move(opts.elem)};
}

}  // namespace

ParseElemResult<ExprPtr> ParseSpawnExpr(Parser parser) {
  SPEC_RULE("Parse-Spawn-Expr");
  Parser next = parser;
  Advance(next);

  ParseElemResult<std::vector<SpawnOption>> opts = ParseSpawnOptsOpt(next);
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(opts.parser);

  SpawnExpr spawn;
  spawn.opts = std::move(opts.elem);
  spawn.body = body.elem;
  return {body.parser, MakeExpr(SpanBetween(parser, body.parser), spawn)};
}

}  // namespace ultraviolet::ast
