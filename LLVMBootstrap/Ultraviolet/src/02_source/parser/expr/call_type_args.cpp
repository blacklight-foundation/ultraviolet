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
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);

namespace {

std::optional<ParseElemResult<std::vector<std::shared_ptr<Type>>>>
TryParseTypeArgs(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Operator || tok->lexeme != "<") {
    return std::nullopt;
  }

  Parser after_lt = parser;
  Advance(after_lt);

  std::vector<std::shared_ptr<Type>> targs;
  ParseElemResult<std::shared_ptr<Type>> first = ParseType(after_lt);
  if (first.parser.tokens == after_lt.tokens &&
      first.parser.index == after_lt.index) {
    return std::nullopt;
  }
  targs.push_back(first.elem);

  Parser cur = first.parser;
  const std::array<EndSetToken, 1> gt_end_set = {EndOperator(">")};
  while (IsPunc(cur, ",")) {
    Parser after_comma = cur;
    Advance(after_comma);
    SkipNewlines(after_comma);

    if (IsOp(after_comma, ">")) {
      if (TrailingCommaAllowed(cur, gt_end_set)) {
        SPEC_RULE("Parse-TypeListTail-TrailingComma");
      }
      EmitTrailingCommaErr(cur, gt_end_set);
      after_comma.diags = cur.diags;
      cur = after_comma;
      break;
    }

    ParseElemResult<std::shared_ptr<Type>> arg = ParseType(after_comma);
    if (arg.parser.tokens == after_comma.tokens &&
        arg.parser.index == after_comma.index) {
      return std::nullopt;
    }
    targs.push_back(arg.elem);
    cur = arg.parser;
  }

  if (IsOp(cur, ">>")) {
    cur = SplitShiftR(cur);
  }

  if (!IsOp(cur, ">")) {
    return std::nullopt;
  }
  Advance(cur);

  return ParseElemResult<std::vector<std::shared_ptr<Type>>>{cur,
                                                              std::move(targs)};
}

}  // namespace

bool CallTypeArgsStart(Parser parser) {
  if (!IsOp(parser, "<")) {
    return false;
  }

  Parser probe = Clone(parser);
  const auto parsed_targs = TryParseTypeArgs(probe);
  if (!parsed_targs.has_value()) {
    return false;
  }

  return IsPunc(parsed_targs->parser, "(");
}

ParseElemResult<ExprPtr> ParseCallTypeArgsStep(Parser parser, ExprPtr expr) {
  SPEC_RULE("Postfix-Call-TypeArgs");

  const auto parsed_targs = TryParseTypeArgs(parser);
  if (!parsed_targs.has_value() || !IsPunc(parsed_targs->parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser sync = parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  Parser after_l = parsed_targs->parser;
  Advance(after_l);

  ParseElemResult<std::vector<Arg>> args = ParseArgList(after_l);
  if (!IsPunc(args.parser, ")")) {
    EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
    Parser sync = args.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  core::Span end_span = TokSpan(args.parser);
  Parser after = args.parser;
  Advance(after);

  CallTypeArgsExpr call;
  call.callee = expr;
  call.type_args = parsed_targs->elem;
  call.args = std::move(args.elem);
  return {after, MakeExpr(SpanCover(expr->span, end_span), call)};
}

}  // namespace ultraviolet::ast
