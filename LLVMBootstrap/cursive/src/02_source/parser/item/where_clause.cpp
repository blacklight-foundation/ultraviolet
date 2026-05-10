// =============================================================================
// where_clause.cpp - Generic Predicate Clause Parsing
// =============================================================================
//
// This file parses generic constraint clauses introduced by `|:`.
//
// Syntax:
//   |: Bitcopy(T)
//   |: Clone(U)
//
// Predicates are separated by semicolons or newlines.
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;
using cursive::lexer::IsIdentTok;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);

// Forward declaration for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
std::shared_ptr<Type> MakeTypePrim(const core::Span& span, std::string_view name);

// =============================================================================
// ParseWhereClauseOpt - Parse optional generic constraint clause
// =============================================================================
//
// Accepted:
//   |: Pred(Type)
//
namespace {

bool IsPredicateReqName(std::string_view name) {
  return name == "Bitcopy" || name == "Clone" || name == "Drop" ||
         name == "FfiSafe";
}

bool IsPredicateReqTerminator(Parser parser) {
  return Tok(parser) && (Tok(parser)->kind == TokenKind::Newline ||
                         IsPunc(parser, ";"));
}

std::string PredicateReqTailPayload(std::size_t predicate_count,
                                    std::string_view terminator = {}) {
  std::string payload;
  payload.reserve(terminator.size() + 48);
  payload += "predicate_count=";
  payload += std::to_string(predicate_count);
  if (!terminator.empty()) {
    payload += ";terminator=";
    payload += terminator;
  }
  return payload;
}

std::string_view PredicateReqTerminatorLabel(const Token& token) {
  if (token.kind == TokenKind::Newline) {
    return "\\n";
  }
  return token.lexeme;
}

void RecordPredicateReqTailRule(std::string_view rule_id,
                                const core::Span& span,
                                std::size_t predicate_count,
                                std::string_view terminator = {}) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record(rule_id, span,
                            PredicateReqTailPayload(predicate_count,
                                                    terminator));
}

std::string PredicateReqPathPayload(const TypePath& path) {
  std::string payload;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      payload += "::";
    }
    payload += path[i];
  }
  return payload;
}

std::string PredicateReqTypePayload(const TypePtr& type) {
  if (!type) {
    return "none";
  }

  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return "TypePrim:" + std::string(node.name);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          return "TypePath:" + PredicateReqPathPayload(node.path);
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          return "TypeApply:" + PredicateReqPathPayload(node.path) +
                 ":args=" + std::to_string(node.args.size());
        } else {
          return "other";
        }
      },
      type->node);
}

std::string PredicateReqPayload(const PredicateReq& req) {
  std::string payload;
  payload.reserve(req.pred.size() + 96);
  payload += "ast_node=PredicateReq;fields=pred,type;pred=";
  payload += req.pred;
  payload += ";type=";
  payload += PredicateReqTypePayload(req.type);
  return payload;
}

std::string PredicateClausePayload(const PredicateClause& predicates) {
  std::string payload;
  payload.reserve(96);
  payload += "ast_node=PredicateClause;representation=list;";
  payload += "fields=[PredicateReq];span_field=absent;predicate_count=";
  payload += std::to_string(predicates.size());
  return payload;
}

void RecordPredicateReqRule(std::string_view rule_id,
                            const core::Span& span,
                            const PredicateReq& req) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record(rule_id, span, PredicateReqPayload(req));
}

void RecordPredicateClauseRule(const core::Span& span,
                               const PredicateClause& predicates) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record("PredicateClause", span,
                            PredicateClausePayload(predicates));
}

bool StartsPredicateReq(Parser parser) {
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  const Token* pred_tok = Tok(parser);
  if (!pred_tok || !IsIdentTok(*pred_tok) ||
      !IsPredicateReqName(pred_tok->lexeme)) {
    return false;
  }

  Parser after_name = parser;
  Advance(after_name);
  return IsPunc(after_name, "(");
}

ParseElemResult<PredicateReq> ParsePredicateReq(Parser parser) {
  Parser pred_start = parser;
  const Token* pred_tok = Tok(parser);
  if (!pred_tok || !IsIdentTok(*pred_tok)) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
  }
  ParseElemResult<Identifier> pred_name = ParseIdent(parser);
  Parser after_name = pred_name.parser;

  if (!IsPredicateReqName(pred_name.elem) || !IsPunc(after_name, "(")) {
    SPEC_RULE("Parse-PredicateReq-Err");
    EmitParseSyntaxErr(after_name, TokSpan(after_name));

    PredicateReq pred;
    pred.pred = pred_name.elem;
    pred.type = MakeTypePrim(SpanBetween(pred_start, after_name), "!");
    RecordPredicateReqRule("Parse-PredicateReq-Err",
                           SpanBetween(pred_start, after_name), pred);
    return {after_name, pred};
  }

  SPEC_RULE("Parse-PredicateReq-Predicate");
  Advance(after_name);

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_name);
  Parser after_type = ty.parser;

  if (!IsPunc(after_type, ")")) {
    EmitParseSyntaxErr(after_type, TokSpan(after_type));
  } else {
    Advance(after_type);
  }

  PredicateReq pred;
  pred.pred = pred_name.elem;
  pred.type = ty.elem;
  RecordPredicateReqRule("Parse-PredicateReq-Predicate",
                         SpanBetween(pred_start, after_type), pred);
  return {after_type, pred};
}

ParseElemResult<std::vector<PredicateReq>> ParsePredicateReqListTail(
    Parser parser,
    std::vector<PredicateReq> predicates) {
  if (!IsPredicateReqTerminator(parser)) {
    RecordPredicateReqTailRule("Parse-PredicateReqListTail-End",
                               TokSpan(parser), predicates.size());
    return {parser, std::move(predicates)};
  }

  const Token* terminator = Tok(parser);
  Parser after_term = parser;
  Advance(after_term);

  const Token* next_tok = Tok(after_term);
  if (!next_tok || !IsIdentTok(*next_tok)) {
    RecordPredicateReqTailRule(
        "Parse-PredicateReqListTail-TrailingTerminator",
        SpanBetween(parser, after_term), predicates.size(),
        terminator ? PredicateReqTerminatorLabel(*terminator)
                   : std::string_view{});
    return {after_term, std::move(predicates)};
  }

  ParseElemResult<PredicateReq> pred = ParsePredicateReq(after_term);
  predicates.push_back(pred.elem);
  ParseElemResult<std::vector<PredicateReq>> tail =
      ParsePredicateReqListTail(pred.parser, std::move(predicates));
  RecordPredicateReqTailRule(
      "Parse-PredicateReqListTail-Cons", SpanBetween(parser, tail.parser),
      tail.elem.size(),
      terminator ? PredicateReqTerminatorLabel(*terminator)
                 : std::string_view{});
  return tail;
}

ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseImpl(Parser parser) {
  // Skip any newlines before clause start.
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (!IsOp(parser, "|:")) {
    const std::optional<PredicateClause> none = std::nullopt;
    (void)PredicateReqs(none, TokSpan(parser));
    return {parser, std::nullopt};
  }

  Parser after_clause = parser;
  Advance(after_clause);
  if (!StartsPredicateReq(after_clause)) {
    const std::optional<PredicateClause> none = std::nullopt;
    (void)PredicateReqs(none, SpanBetween(parser, after_clause));
    return {parser, std::nullopt};
  }

  SPEC_RULE("Parse-Where-Clause");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume |:

  // Skip newlines after |:
  while (Tok(next) && Tok(next)->kind == TokenKind::Newline) {
    Advance(next);
  }

  ParseElemResult<PredicateReq> first = ParsePredicateReq(next);
  std::vector<PredicateReq> predicates;
  predicates.push_back(first.elem);

  ParseElemResult<std::vector<PredicateReq>> tail =
      ParsePredicateReqListTail(first.parser, std::move(predicates));

  next = tail.parser;

  PredicateClause clause = std::move(tail.elem);
  RecordPredicateClauseRule(SpanBetween(start, next), clause);
  std::optional<PredicateClause> clause_opt = std::move(clause);
  (void)PredicateReqs(clause_opt, SpanBetween(start, next));
  return {next, std::move(clause_opt)};
}

}  // namespace

ParseElemResult<std::optional<PredicateClause>> ParseWhereClauseOpt(Parser parser) {
  return ParsePredicateClauseImpl(parser);
}

ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser) {
  return ParsePredicateClauseImpl(parser);
}

}  // namespace cursive::ast
