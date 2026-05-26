// =============================================================================
// parallel_expr.cpp - Parallel Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.8.6, Lines 5264-5267
// HELPER RULES: Section 3.3.8.7, Lines 5594-5639
//
// FORMAL RULE - Parse-Parallel-Expr (Lines 5264-5267):
// -----------------------------------------------------------------------------
// IsKw(Tok(P), `parallel`)
// Gamma |- ParseExpr_NoBrace(Advance(P)) => (P_1, domain)
// Gamma |- ParseParallelOptsOpt(P_1) => (P_2, opts)
// Gamma |- ParseBlock(P_2) => (P_3, body)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (P_3, ParallelExpr(domain, opts, body))
//
// SEMANTICS:
// - `parallel domain_expr [options]? { body }`
// - domain_expr: Expression yielding an ExecutionDomain ($cpu(), $gpu(), etc.)
// - Options: `cancel: token_expr`, `name: "string"`
// - Body: Block containing spawn/dispatch statements
// - All spawned work must complete before parallel block exits (fork-join)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations from other parser modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<ExprPtr> ParseLiteralExpr(Parser parser);

namespace {

bool IsStringLiteralToken(const Token* tok) {
  return tok && tok->kind == TokenKind::StringLiteral;
}

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

ParseElemResult<ParallelOption> ParseParallelOpt(Parser parser) {
  const Token* opt_tok = Tok(parser);
  if (!opt_tok || (opt_tok->kind != TokenKind::Identifier &&
                   opt_tok->kind != TokenKind::Keyword)) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    ParallelOption err;
    err.kind = ParallelOptionKind::Cancel;
    err.span = TokSpan(parser);
    return {parser, err};
  }

  core::Span opt_start = TokSpan(parser);
  ParallelOption opt;
  opt.span = opt_start;
  Parser cur = parser;

  if (opt_tok->lexeme == "cancel") {
    SPEC_RULE("Parse-ParallelOpt-Cancel");
    opt.kind = ParallelOptionKind::Cancel;
  } else if (opt_tok->lexeme == "name") {
    SPEC_RULE("Parse-ParallelOpt-Name");
    opt.kind = ParallelOptionKind::Name;
  } else if (opt_tok->lexeme == "workgroup") {
    SPEC_RULE("Parse-ParallelOpt-Workgroup");
    opt.kind = ParallelOptionKind::Workgroup;
  } else if (opt_tok->lexeme == "workgroups") {
    SPEC_RULE("Parse-ParallelOpt-Workgroups");
    opt.kind = ParallelOptionKind::Workgroups;
  } else {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    ParallelOption err;
    err.kind = ParallelOptionKind::Cancel;
    err.span = TokSpan(parser);
    return {parser, err};
  }

  Advance(cur);
  if (!IsPunc(cur, ":")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    opt.span = SpanCover(opt_start, TokSpan(cur));
    return {cur, opt};
  }
  Advance(cur);
  SkipNewlines(cur);

  if (opt.kind == ParallelOptionKind::Name) {
    if (!IsStringLiteralToken(Tok(cur))) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      opt.span = SpanCover(opt_start, TokSpan(cur));
      return {cur, opt};
    }
    ParseElemResult<ExprPtr> lit = ParseLiteralExpr(cur);
    opt.value = lit.elem;
    opt.span = SpanCover(opt_start, TokSpan(lit.parser));
    return {lit.parser, opt};
  }

  if (opt.kind == ParallelOptionKind::Workgroup ||
      opt.kind == ParallelOptionKind::Workgroups) {
    ParseElemResult<ExprPtr> dims = ParseDim3Const(cur);
    opt.value = dims.elem;
    opt.span = SpanCover(opt_start, TokSpan(dims.parser));
    return {dims.parser, opt};
  }

  ParseElemResult<ExprPtr> opt_val = ParseExpr(cur);
  opt.value = opt_val.elem;
  opt.span = SpanCover(opt_start, TokSpan(opt_val.parser));
  return {opt_val.parser, opt};
}

ParseElemResult<std::vector<ParallelOption>> ParseParallelOptListTail(
    Parser parser,
    std::vector<ParallelOption> opts) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-ParallelOptListTail-End");
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
    const EndSetToken end_set[] = {EndPunct("]")};
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-ParallelOptListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after_comma.diags = parser.diags;
    return {after_comma, std::move(opts)};
  }

  SPEC_RULE("Parse-ParallelOptListTail-Comma");
  ParseElemResult<ParallelOption> next = ParseParallelOpt(after_comma);
  opts.push_back(std::move(next.elem));
  return ParseParallelOptListTail(next.parser, std::move(opts));
}

ParseElemResult<std::vector<ParallelOption>> ParseParallelOptList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "]")) {
    SPEC_RULE("Parse-ParallelOptList-Empty");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  SPEC_RULE("Parse-ParallelOptList-Cons");
  ParseElemResult<ParallelOption> first = ParseParallelOpt(parser);
  std::vector<ParallelOption> opts;
  opts.push_back(std::move(first.elem));
  return ParseParallelOptListTail(first.parser, std::move(opts));
}

ParseElemResult<std::vector<ParallelOption>> ParseParallelOptsOpt(Parser parser) {
  if (!IsPunc(parser, "[")) {
    SPEC_RULE("Parse-ParallelOptsOpt-None");
    return {parser, {}};
  }

  SPEC_RULE("Parse-ParallelOptsOpt-Yes");
  Parser after_lbracket = parser;
  Advance(after_lbracket);
  ParseElemResult<std::vector<ParallelOption>> opts =
      ParseParallelOptList(after_lbracket);

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

// =============================================================================
// ParseParallelExpr - Parse parallel expression
// =============================================================================
//
// SPEC: Lines 5264-5267
// parallel domain_expr [options]? { body }
// Options: cancel, name

ParseElemResult<ExprPtr> ParseParallelExpr(Parser parser) {
  SPEC_RULE("Parse-Parallel-Expr");
  Parser next = parser;
  Advance(next);

  // Parse domain expression.
  Parser domain_start = next;
  domain_start.stop_before_parallel_options = true;
  ParseElemResult<ExprPtr> domain = ParseExprNoBrace(domain_start);
  Parser after_domain = domain.parser;
  after_domain.stop_before_parallel_options = false;

  ParseElemResult<std::vector<ParallelOption>> opts =
      ParseParallelOptsOpt(after_domain);

  // Parse block body
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(opts.parser);
  ParallelExpr par;
  par.domain = domain.elem;
  par.opts = std::move(opts.elem);
  par.body = body.elem;
  return {body.parser, MakeExpr(SpanBetween(parser, body.parser), par)};
}
}  // namespace ultraviolet::ast
