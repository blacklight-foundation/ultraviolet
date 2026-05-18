// =============================================================================
// closure_expr.cpp - Closure Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - closure_expr ::= "|" closure_param_list? "|" ("->" type)? closure_body
// - closure_param_list ::= closure_param ("," closure_param)* ","?
// - closure_param ::= "move"? identifier (":" type)?
// - closure_body ::= expression | block_expr
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);

// Forward declarations from parser utilities
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);

// Forward declarations from other modules
ParseElemResult<Identifier> ParseIdent(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseTypeNoUnion(Parser parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

namespace {

ParseElemResult<std::shared_ptr<Type>> ParseClosureParamAnnotType(Parser parser) {
  if (IsPunc(parser, "(")) {
    SPEC_RULE("Parse-ClosureParamType-Grouped");
    Parser after_l = parser;
    Advance(after_l);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_l);
    if (!IsPunc(ty.parser, ")")) {
      EmitParseSyntaxErr(ty.parser, TokSpan(ty.parser));
      return {ty.parser, ty.elem};
    }
    Parser after_r = ty.parser;
    Advance(after_r);
    return {after_r, ty.elem};
  }

  SPEC_RULE("Parse-ClosureParamType-Plain");
  return ParseTypeNoUnion(parser);
}

}  // namespace

// =============================================================================
// ParseClosureParam - Parse a single closure parameter
// =============================================================================

ParseElemResult<ClosureParam> ParseClosureParam(Parser parser) {
  SPEC_RULE("Parse-ClosureParam");
  bool move_capture = false;
  if (IsKw(parser, "move")) {
    move_capture = true;
    Advance(parser);
  }
  ParseElemResult<Identifier> name = ParseIdent(parser);
  Parser cur = name.parser;
  std::shared_ptr<Type> type_opt = nullptr;
  if (IsPunc(cur, ":")) {
    Advance(cur);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseClosureParamAnnotType(cur);
    type_opt = ty.elem;
    cur = ty.parser;
  }
  if (move_capture) {
    if (type_opt) {
      SPEC_RULE("Parse-ClosureParam-MoveTyped");
    } else {
      SPEC_RULE("Parse-ClosureParam-MoveUntyped");
    }
  } else {
    if (type_opt) {
      SPEC_RULE("Parse-ClosureParam-Typed");
    } else {
      SPEC_RULE("Parse-ClosureParam-Untyped");
    }
  }
  ClosureParam param;
  param.move_capture = move_capture;
  param.name = name.elem;
  param.type_opt = type_opt;
  return {cur, param};
}

// =============================================================================
// ParseClosureParamListTail - Parse remaining parameters
// =============================================================================

ParseElemResult<std::vector<ClosureParam>> ParseClosureParamListTail(
    Parser parser, std::vector<ClosureParam> xs) {
  SkipNewlines(parser);
  if (IsOp(parser, "|")) {
    SPEC_RULE("Parse-ClosureParamListTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsOp(after, "|")) {
      const std::array<EndSetToken, 1> end_set = {EndOperator("|")};
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }
    SPEC_RULE("Parse-ClosureParams-Cons");
    ParseElemResult<ClosureParam> param = ParseClosureParam(after);
    xs.push_back(param.elem);
    return ParseClosureParamListTail(param.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseClosureParamList - Parse closure parameter list
// =============================================================================

ParseElemResult<std::vector<ClosureParam>> ParseClosureParamList(Parser parser) {
  SkipNewlines(parser);
  SPEC_RULE("Parse-ClosureParams-Single");
  ParseElemResult<ClosureParam> first = ParseClosureParam(parser);
  std::vector<ClosureParam> params;
  params.push_back(first.elem);
  return ParseClosureParamListTail(first.parser, std::move(params));
}

ParseElemResult<ExprPtr> ParseClosureBody(Parser parser) {
  if (IsPunc(parser, "{")) {
    SPEC_RULE("Parse-ClosureBody-Block");
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(parser);
    BlockExpr expr;
    expr.block = block.elem;
    return {block.parser, MakeExpr(SpanBetween(parser, block.parser), expr)};
  }
  SPEC_RULE("Parse-ClosureBody-Expr");
  return ParseExpr(parser);
}

// =============================================================================
// ParseClosureExpr - Parse closure expression
// =============================================================================

ParseElemResult<ExprPtr> ParseClosureExpr(Parser parser) {
  Parser start = parser;

  std::vector<ClosureParam> params;
  Parser after_bar;
  if (IsOp(parser, "||")) {
    SPEC_RULE("Parse-Closure-Expr-Empty");
    after_bar = parser;
    Advance(after_bar);
  } else {
    Parser next = parser;
    Advance(next);  // consume opening |

    if (IsOp(next, "|")) {
      SPEC_RULE("Parse-Closure-Expr-Empty");
      after_bar = next;
      Advance(after_bar);
    } else {
      SPEC_RULE("Parse-Closure-Expr");
      ParseElemResult<std::vector<ClosureParam>> parsed_params =
          ParseClosureParamList(next);
      if (!IsOp(parsed_params.parser, "|")) {
        EmitParseSyntaxErr(parsed_params.parser, TokSpan(parsed_params.parser));
        Parser sync = parsed_params.parser;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
      }
      params = std::move(parsed_params.elem);
      after_bar = parsed_params.parser;
      Advance(after_bar);  // consume closing |
    }
  }

  // Optional return type
  std::shared_ptr<Type> ret_type_opt = nullptr;
  Parser cur = after_bar;
  if (IsOp(cur, "->")) {
    SPEC_RULE("Parse-ClosureRetOpt-Some");
    Advance(cur);
    ParseElemResult<std::shared_ptr<Type>> ret = ParseType(cur);
    ret_type_opt = ret.elem;
    cur = ret.parser;
  } else {
    SPEC_RULE("Parse-ClosureRetOpt-None");
  }

  // Closure body (expression or block)
  ParseElemResult<ExprPtr> body = ParseClosureBody(cur);
  ClosureExpr clo;
  clo.params = std::move(params);
  clo.ret_type_opt = ret_type_opt;
  clo.body = body.elem;
  return {body.parser, MakeExpr(SpanBetween(start, body.parser), clo)};
}

// =============================================================================
// TryParseClosureExpr - Attempt to parse closure expression
// =============================================================================

std::optional<ParseElemResult<ExprPtr>> TryParseClosureExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || tok->kind != lexer::TokenKind::Operator ||
      (tok->lexeme != "|" && tok->lexeme != "||")) {
    return std::nullopt;
  }
  return ParseClosureExpr(parser);
}

}  // namespace ultraviolet::ast
