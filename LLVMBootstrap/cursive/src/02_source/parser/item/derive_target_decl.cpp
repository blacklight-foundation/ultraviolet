// =============================================================================
// derive_target_decl.cpp - Derive Target Declaration Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

using cursive::lexer::Token;
using cursive::lexer::TokenKind;

ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);

namespace {

bool IsLexemeToken(const Parser& parser, std::string_view lexeme) {
  const Token* tok = Tok(parser);
  return tok && ((tok->kind == TokenKind::Identifier) ||
                 (tok->kind == TokenKind::Keyword)) &&
         tok->lexeme == lexeme;
}

ParseElemResult<DeriveClause> ParseDeriveClause(Parser parser) {
  Parser start = parser;
  DeriveClause clause{};
  if (IsLexemeToken(parser, "requires")) {
    SPEC_RULE("Parse-DeriveClause-Requires");
    clause.kind = DeriveClauseKind::Requires;
  } else if (IsLexemeToken(parser, "emits")) {
    SPEC_RULE("Parse-DeriveClause-Emits");
    clause.kind = DeriveClauseKind::Emits;
  } else {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    clause.kind = DeriveClauseKind::Requires;
    clause.name = Identifier{"_"};
    clause.span = TokSpan(start);
    return {parser, clause};
  }

  Advance(parser);
  ParseElemResult<Identifier> name = ParseIdent(parser);
  clause.name = name.elem;
  clause.span = SpanBetween(start, name.parser);
  return {name.parser, clause};
}

ParseElemResult<std::vector<DeriveClause>> ParseDeriveClauseTail(
    Parser parser, std::vector<DeriveClause> xs) {
  if (!IsPunc(parser, ",")) {
    SPEC_RULE("Parse-DeriveClauseTail-End");
    return {parser, xs};
  }

  SPEC_RULE("Parse-DeriveClauseTail-Comma");
  Advance(parser);
  ParseElemResult<DeriveClause> clause = ParseDeriveClause(parser);
  xs.push_back(clause.elem);
  return ParseDeriveClauseTail(clause.parser, std::move(xs));
}

ParseElemResult<std::vector<DeriveClause>> ParseDeriveClauseList(Parser parser) {
  ParseElemResult<DeriveClause> first = ParseDeriveClause(parser);
  SPEC_RULE("Parse-DeriveClauseList-Cons");
  std::vector<DeriveClause> xs;
  xs.push_back(first.elem);
  return ParseDeriveClauseTail(first.parser, std::move(xs));
}

ParseElemResult<std::vector<DeriveClause>> ParseDeriveContractOpt(Parser parser) {
  if (!IsOp(parser, "|:")) {
    SPEC_RULE("Parse-DeriveContractOpt-None");
    return {parser, {}};
  }

  Advance(parser);
  ParseElemResult<std::vector<DeriveClause>> clauses =
      ParseDeriveClauseList(parser);
  SPEC_RULE("Parse-DeriveContractOpt-Yes");
  return clauses;
}

}  // namespace

ParseItemResult ParseDeriveTargetDecl(Parser parser) {
  Parser start = parser;

  Advance(parser);
  if (!IsLexemeToken(parser, "target")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  Advance(parser);

  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  if (!IsPunc(parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }

  Advance(parser);
  if (!IsLexemeToken(parser, "target")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  Advance(parser);

  if (!IsPunc(parser, ":")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  Advance(parser);

  if (!IsLexemeToken(parser, "Type")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  Advance(parser);

  if (!IsPunc(parser, ")")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }
  Advance(parser);

  ParseElemResult<std::vector<DeriveClause>> contract_opt =
      ParseDeriveContractOpt(parser);
  parser = contract_opt.parser;

  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(parser);
  parser = body.parser;

  DeriveTargetDecl decl;
  decl.name = name.elem;
  decl.contract_opt = std::move(contract_opt.elem);
  decl.body = body.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};
  return {parser, decl};
}

}  // namespace cursive::ast
