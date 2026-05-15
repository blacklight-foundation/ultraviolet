// =============================================================================
// comptime_expr.cpp - Compile-Time Expression Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

using cursive::lexer::Ctx;
using cursive::lexer::IsKwTok;
using cursive::lexer::Token;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser);
ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseTypeAnnotOpt(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseComptimeExpr(Parser parser);
void NormalizeBindingPattern(std::shared_ptr<Pattern>& pat,
                             std::shared_ptr<Type>& type_opt);

bool IsPunc(const Parser& parser, std::string_view punc);
bool IsKw(const Parser& parser, std::string_view kw);
void SkipNewlines(Parser& parser);

namespace {

BlockPtr MakeElseIfBlock(const ExprPtr& expr, const core::Span& span) {
  auto block = std::make_shared<Block>();
  block->span = span;
  block->tail_opt = expr;
  return block;
}

ParseElemResult<BlockPtr> ParseCtElseOpt(Parser parser) {
  if (!IsKw(parser, "else")) {
    SPEC_RULE("Parse-CtElseOpt-None");
    return {parser, nullptr};
  }

  Parser after_else = parser;
  Advance(after_else);
  if (IsKw(after_else, "comptime")) {
    Parser after_comptime = after_else;
    Advance(after_comptime);
    if (IsKw(after_comptime, "if")) {
      SPEC_RULE("Parse-CtElseOpt-ElseIf");
      auto nested = TryParseComptimeExpr(after_else);
      if (!nested.has_value()) {
        EmitParseSyntaxErr(after_else, TokSpan(after_else));
        Parser sync = after_else;
        SyncStmt(sync);
        return {sync, MakeElseIfBlock(MakeExpr(SpanBetween(parser, sync), ErrorExpr{}),
                                      SpanBetween(parser, sync))};
      }
      const core::Span span =
          nested->elem ? nested->elem->span : SpanBetween(parser, nested->parser);
      return {nested->parser, MakeElseIfBlock(nested->elem, span)};
    }
  }

  SPEC_RULE("Parse-CtElseOpt-Block");
  ParseElemResult<BlockPtr> block = ParseBlock(after_else);
  return {block.parser, block.elem};
}

ParseElemResult<ExprPtr> ParseCtBlockExpr(Parser parser) {
  Parser start = parser;
  Parser next = parser;
  Advance(next);

  if (!IsPunc(next, "{")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Parser after_l = next;
  Advance(after_l);
  SkipNewlines(after_l);
  ParseElemResult<ExprPtr> value = ParseExpr(after_l);
  Parser before_r = value.parser;
  SkipNewlines(before_r);
  if (!IsPunc(before_r, "}")) {
    EmitParseSyntaxErr(before_r, TokSpan(before_r));
    Parser sync = before_r;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Parser after_r = before_r;
  Advance(after_r);

  SPEC_RULE("Parse-CtExpr");
  ComptimeExpr comptime;
  comptime.body = value.elem;
  comptime.attrs_opt = std::nullopt;
  return {after_r, MakeExpr(SpanBetween(start, after_r), comptime)};
}

ParseElemResult<ExprPtr> ParseCtIfExpr(Parser parser) {
  SPEC_RULE("Parse-CtIf");
  Parser start = parser;
  Parser after_comptime = parser;
  Advance(after_comptime);
  Parser after_if = after_comptime;
  Advance(after_if);

  ParseElemResult<ExprPtr> cond = ParseExprNoBrace(after_if);
  ParseElemResult<BlockPtr> then_block = ParseBlock(cond.parser);
  ParseElemResult<BlockPtr> else_block = ParseCtElseOpt(then_block.parser);

  CtIfExpr ct_if;
  ct_if.cond = cond.elem;
  ct_if.then_block = then_block.elem;
  ct_if.else_block_opt = else_block.elem;
  return {else_block.parser, MakeExpr(SpanBetween(start, else_block.parser), ct_if)};
}

ParseElemResult<ExprPtr> ParseCtLoopIterExpr(Parser parser) {
  SPEC_RULE("Parse-CtLoopIter");
  Parser start = parser;
  Parser after_comptime = parser;
  Advance(after_comptime);
  Parser after_loop = after_comptime;
  Advance(after_loop);

  TryPatternInResult try_in = TryParsePatternIn(after_loop);
  if (!try_in.ok) {
    EmitParseSyntaxErr(after_loop, TokSpan(after_loop));
    Parser sync = after_loop;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  ParseElemResult<TypePtr> ty = ParseTypeAnnotOpt(try_in.parser);
  const Token* in_tok = Tok(ty.parser);
  if (!in_tok || !Ctx(*in_tok, "in")) {
    EmitParseSyntaxErr(ty.parser, TokSpan(ty.parser));
    Parser sync = ty.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Parser after_in = ty.parser;
  Advance(after_in);
  ParseElemResult<ExprPtr> iter = ParseExprNoBrace(after_in);
  ParseElemResult<BlockPtr> body = ParseBlock(iter.parser);

  auto pattern = try_in.pattern;
  auto type_opt = ty.elem;
  NormalizeBindingPattern(pattern, type_opt);

  CtLoopIterExpr ct_loop;
  ct_loop.pattern = pattern;
  ct_loop.type_opt = type_opt;
  ct_loop.iter = iter.elem;
  ct_loop.body = body.elem;
  return {body.parser, MakeExpr(SpanBetween(start, body.parser), ct_loop)};
}

}  // namespace

std::optional<ParseElemResult<ExprPtr>> TryParseComptimeExpr(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok) {
    return std::nullopt;
  }

  const bool is_comptime =
      IsKwTok(*tok, "comptime") ||
      (IsIdentTok(*tok) && tok->lexeme == "comptime");
  if (!is_comptime) {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);
  if (IsPunc(next, "{")) {
    return ParseCtBlockExpr(parser);
  }
  if (IsKw(next, "if")) {
    return ParseCtIfExpr(parser);
  }
  if (IsKw(next, "loop")) {
    return ParseCtLoopIterExpr(parser);
  }
  return std::nullopt;
}

}  // namespace cursive::ast
