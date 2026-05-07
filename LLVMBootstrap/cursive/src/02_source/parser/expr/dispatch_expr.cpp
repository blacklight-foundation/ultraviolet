// =============================================================================
// dispatch_expr.cpp - Dispatch Expression Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

using cursive::lexer::Ctx;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);
ParseElemResult<ExprPtr> ParseRange(Parser parser, bool allow_brace,
                                    bool allow_bracket);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<KeyPathExpr> ParseKeyPathExpr(Parser parser);

namespace {

ParseElemResult<ExprPtr> ParseDim3Const(Parser parser) {
  if (!IsPunc(parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakeExpr(TokSpan(parser), ErrorExpr{})};
  }

  Parser cur = parser;
  core::Span start = TokSpan(cur);
  Advance(cur);
  SkipNewlines(cur);

  std::vector<ExprPtr> elems;
  elems.reserve(3);
  for (int i = 0; i < 3; ++i) {
    ParseElemResult<ExprPtr> elem = ParseExpr(cur);
    elems.push_back(elem.elem);
    cur = elem.parser;
    SkipNewlines(cur);
    if (i < 2) {
      if (!IsPunc(cur, ",")) {
        EmitParseSyntaxErr(cur, TokSpan(cur));
        return {cur, MakeExpr(SpanCover(start, TokSpan(cur)), ErrorExpr{})};
      }
      Advance(cur);
      SkipNewlines(cur);
    }
  }

  if (!IsPunc(cur, ")")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    return {cur, MakeExpr(SpanCover(start, TokSpan(cur)), ErrorExpr{})};
  }
  core::Span end = TokSpan(cur);
  Advance(cur);

  TupleExpr tuple;
  tuple.elements = std::move(elems);
  return {cur, MakeExpr(SpanCover(start, end), tuple)};
}

ParseElemResult<KeyMode> ParseKeyMode(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier) {
    SPEC_RULE("Parse-KeyMode-Err");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, KeyMode::Read};
  }

  if (tok->lexeme == "read") {
    SPEC_RULE("Parse-KeyMode-Read");
    Parser next = parser;
    Advance(next);
    return {next, KeyMode::Read};
  }
  if (tok->lexeme == "write") {
    SPEC_RULE("Parse-KeyMode-Write");
    Parser next = parser;
    Advance(next);
    return {next, KeyMode::Write};
  }

  SPEC_RULE("Parse-KeyMode-Err");
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, KeyMode::Read};
}

ParseElemResult<std::optional<DispatchKeyClause>> ParseKeyClauseOpt(
    Parser parser) {
  if (!(Tok(parser) && Ctx(*Tok(parser), "key"))) {
    SPEC_RULE("Parse-KeyClauseOpt-None");
    return {parser, std::nullopt};
  }

  SPEC_RULE("Parse-KeyClauseOpt-Yes");
  core::Span key_start = TokSpan(parser);
  Parser after_key = parser;
  Advance(after_key);

  ParseElemResult<KeyPathExpr> key_path = ParseKeyPathExpr(after_key);
  ParseElemResult<KeyMode> mode = ParseKeyMode(key_path.parser);

  DispatchKeyClause clause;
  clause.key_path = std::move(key_path.elem);
  clause.mode = mode.elem;
  clause.span = SpanCover(key_start, TokSpan(mode.parser));
  return {mode.parser, clause};
}

ParseElemResult<ReduceOp> ParseReduceOp(Parser parser, Identifier* custom_name) {
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, ReduceOp::Add};
  }

  if (tok->kind == TokenKind::Operator &&
      (tok->lexeme == "+" || tok->lexeme == "*")) {
    SPEC_RULE("Parse-ReduceOp-Op");
    Parser next = parser;
    Advance(next);
    return {next, tok->lexeme == "+" ? ReduceOp::Add : ReduceOp::Mul};
  }

  if (tok->kind == TokenKind::Identifier) {
    SPEC_RULE("Parse-ReduceOp-Ident");
    Parser next = parser;
    Advance(next);
    if (tok->lexeme == "min") {
      return {next, ReduceOp::Min};
    }
    if (tok->lexeme == "max") {
      return {next, ReduceOp::Max};
    }
    if (tok->lexeme == "and") {
      return {next, ReduceOp::And};
    }
    if (tok->lexeme == "or") {
      return {next, ReduceOp::Or};
    }
    if (custom_name != nullptr) {
      *custom_name = tok->lexeme;
    }
    return {next, ReduceOp::Custom};
  }

  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, ReduceOp::Add};
}

ParseElemResult<DispatchOption> ParseDispatchOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    DispatchOption err;
    err.kind = DispatchOptionKind::Ordered;
    err.span = TokSpan(parser);
    return {parser, err};
  }

  core::Span opt_start = TokSpan(parser);
  DispatchOption opt;
  opt.span = opt_start;

  if (tok->lexeme == "reduce") {
    SPEC_RULE("Parse-DispatchOpt-Reduce");
    opt.kind = DispatchOptionKind::Reduce;
    Parser cur = parser;
    Advance(cur);
    if (!IsPunc(cur, ":")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      opt.span = SpanCover(opt_start, TokSpan(cur));
      return {cur, opt};
    }
    Advance(cur);

    Identifier custom_reduce_name;
    ParseElemResult<ReduceOp> reduce_op = ParseReduceOp(cur, &custom_reduce_name);
    opt.reduce_op = reduce_op.elem;
    if (reduce_op.elem == ReduceOp::Custom) {
      opt.custom_reduce_name = std::move(custom_reduce_name);
    }
    opt.span = SpanCover(opt_start, TokSpan(reduce_op.parser));
    return {reduce_op.parser, opt};
  }

  if (tok->lexeme == "ordered") {
    SPEC_RULE("Parse-DispatchOpt-Ordered");
    opt.kind = DispatchOptionKind::Ordered;
    Parser next = parser;
    Advance(next);
    opt.span = SpanCover(opt_start, TokSpan(next));
    return {next, opt};
  }

  if (tok->lexeme == "chunk") {
    SPEC_RULE("Parse-DispatchOpt-Chunk");
    opt.kind = DispatchOptionKind::Chunk;
    Parser cur = parser;
    Advance(cur);
    if (!IsPunc(cur, ":")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      opt.span = SpanCover(opt_start, TokSpan(cur));
      return {cur, opt};
    }
    Advance(cur);

    ParseElemResult<ExprPtr> chunk_expr = ParseExpr(cur);
    opt.chunk_expr = chunk_expr.elem;
    opt.span = SpanCover(opt_start, TokSpan(chunk_expr.parser));
    return {chunk_expr.parser, opt};
  }

  if (tok->lexeme == "workgroup") {
    SPEC_RULE("Parse-DispatchOpt-Workgroup");
    opt.kind = DispatchOptionKind::Workgroup;
    Parser cur = parser;
    Advance(cur);
    if (!IsPunc(cur, ":")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      opt.span = SpanCover(opt_start, TokSpan(cur));
      return {cur, opt};
    }
    Advance(cur);

    ParseElemResult<ExprPtr> dims = ParseDim3Const(cur);
    opt.workgroup_expr = dims.elem;
    opt.span = SpanCover(opt_start, TokSpan(dims.parser));
    return {dims.parser, opt};
  }

  EmitParseSyntaxErr(parser, TokSpan(parser));
  opt.kind = DispatchOptionKind::Ordered;
  return {parser, opt};
}

ParseElemResult<std::vector<DispatchOption>> ParseDispatchOptListTail(
    Parser parser,
    std::vector<DispatchOption> opts) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-DispatchOptListTail-End");
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
      SPEC_RULE("Parse-DispatchOptListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after_comma.diags = parser.diags;
    return {after_comma, std::move(opts)};
  }

  SPEC_RULE("Parse-DispatchOptListTail-Comma");
  ParseElemResult<DispatchOption> next = ParseDispatchOpt(after_comma);
  opts.push_back(std::move(next.elem));
  return ParseDispatchOptListTail(next.parser, std::move(opts));
}

ParseElemResult<std::vector<DispatchOption>> ParseDispatchOptList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-DispatchOptList-Empty");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  SPEC_RULE("Parse-DispatchOptList-Cons");
  ParseElemResult<DispatchOption> first = ParseDispatchOpt(parser);
  std::vector<DispatchOption> opts;
  opts.push_back(std::move(first.elem));
  return ParseDispatchOptListTail(first.parser, std::move(opts));
}

ParseElemResult<std::vector<DispatchOption>> ParseDispatchOptsOpt(Parser parser) {
  if (!IsPunc(parser, "[")) {
    SPEC_RULE("Parse-DispatchOptsOpt-None");
    return {parser, {}};
  }

  SPEC_RULE("Parse-DispatchOptsOpt-Yes");
  Parser after_lbracket = parser;
  Advance(after_lbracket);
  ParseElemResult<std::vector<DispatchOption>> opts =
      ParseDispatchOptList(after_lbracket);

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

ParseElemResult<ExprPtr> ParseDispatchExpr(Parser parser) {
  SPEC_RULE("Parse-Dispatch-Expr");
  Parser next = parser;
  Advance(next);

  ParseElemResult<std::shared_ptr<Pattern>> pat = ParsePattern(next);

  const Token* in_tok = Tok(pat.parser);
  if (!in_tok || !Ctx(*in_tok, "in")) {
    EmitParseSyntaxErr(pat.parser, TokSpan(pat.parser));
    Parser sync = pat.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_in = pat.parser;
  Advance(after_in);

  ParseElemResult<ExprPtr> range = ParseRange(after_in, false, false);
  ParseElemResult<std::optional<DispatchKeyClause>> key_clause =
      ParseKeyClauseOpt(range.parser);
  ParseElemResult<std::vector<DispatchOption>> opts =
      ParseDispatchOptsOpt(key_clause.parser);

  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(opts.parser);

  DispatchExpr dispatch;
  dispatch.pattern = pat.elem;
  dispatch.range = range.elem;
  dispatch.key_clause = key_clause.elem;
  dispatch.opts = std::move(opts.elem);
  dispatch.body = body.elem;
  return {body.parser, MakeExpr(SpanBetween(parser, body.parser), dispatch)};
}

}  // namespace cursive::ast
