// =============================================================================
// type_literal.cpp - Compile-Time Type Literal Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

using cursive::lexer::IsIdentTok;
using cursive::lexer::Token;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);

bool IsOp(const Parser& parser, std::string_view op);

std::optional<ParseElemResult<ExprPtr>> TryParseTypeLiteralExpr(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok) || tok->lexeme != "Type") {
    return std::nullopt;
  }

  Parser start = parser;
  Parser cur = parser;
  Advance(cur);
  if (!IsOp(cur, "::")) {
    return std::nullopt;
  }

  Advance(cur);
  if (!IsOp(cur, "<")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser sync = cur;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Advance(cur);
  ParseElemResult<std::shared_ptr<Type>> parsed_type = ParseType(cur);
  cur = parsed_type.parser;
  if (IsOp(cur, ">>")) {
    cur = SplitShiftR(cur);
  }
  if (!IsOp(cur, ">")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser sync = cur;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Advance(cur);
  TypeLiteralExpr lit;
  lit.type = parsed_type.elem;
  SPEC_RULE("Parse-TypeLiteral");
  return ParseElemResult<ExprPtr>{cur, MakeExpr(SpanBetween(start, cur), lit)};
}

}  // namespace cursive::ast
