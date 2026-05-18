// =============================================================================
// if_expr.cpp - If expression parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

using ultraviolet::lexer::IsIdentTok;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
ParseElemResult<Identifier> ParseIdent(Parser parser);
ParseQualifiedHeadResult ParseQualifiedHead(Parser parser);
ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<ExprPtr> ParsePrimary(Parser parser, bool allow_brace);

namespace {

ExprPtr WrapBlockExpr(Parser start, const ParseElemResult<std::shared_ptr<Block>>& block) {
  BlockExpr blk;
  blk.block = block.elem;
  return MakeExpr(SpanBetween(start, block.parser), blk);
}

void SkipCaseListNewlines(Parser& parser) {
  while (const Token* tok = Tok(parser)) {
    if (tok->kind != TokenKind::Newline) {
      break;
    }
    Advance(parser);
  }
}

struct ElseOptResult {
  Parser parser;
  ExprPtr else_opt;
};

enum class PendingIfKind {
  Plain,
  IsSingle,
};

struct PendingIfArm {
  Parser start;
  PendingIfKind kind;
  ExprPtr cond_or_scrutinee;
  PatternPtr pattern_opt;
  ExprPtr then_expr;
};

struct IfHeadResult {
  Parser parser;
  ExprPtr completed_expr;
  std::optional<PendingIfArm> pending;
};

ExprPtr MaterializePendingIfArm(const PendingIfArm& arm, const Parser& end, ExprPtr else_opt) {
  if (arm.kind == PendingIfKind::Plain) {
    IfExpr ifexpr;
    ifexpr.cond = arm.cond_or_scrutinee;
    ifexpr.then_expr = arm.then_expr;
    ifexpr.else_expr = else_opt;
    return MakeExpr(SpanBetween(arm.start, end), ifexpr);
  }

  IfIsExpr if_is;
  if_is.scrutinee = arm.cond_or_scrutinee;
  if_is.pattern = arm.pattern_opt;
  if_is.then_expr = arm.then_expr;
  if_is.else_expr = else_opt;
  return MakeExpr(SpanBetween(arm.start, end), if_is);
}

struct IfCaseListResult {
  Parser parser;
  std::vector<IfCaseClause> cases;
  ExprPtr else_opt;
};

ParseElemResult<ExprPtr> ParseCaseBodyBlock(Parser parser) {
  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(parser);
  return {block.parser, WrapBlockExpr(parser, block)};
}

std::optional<ParseElemResult<PatternPtr>>
TryParseBraceDisambiguatedIfCasePattern(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok) {
    return std::nullopt;
  }

  if (IsOp(parser, "@")) {
    SPEC_RULE("Parse-IfCase-Pattern-ModalHead");
    Parser start = parser;
    Parser next = parser;
    Advance(next);
    ParseElemResult<Identifier> name = ParseIdent(next);
    ModalPattern pat;
    pat.state = name.elem;
    pat.fields_opt = std::nullopt;
    return ParseElemResult<PatternPtr>{
        name.parser,
        MakePattern(SpanBetween(start, name.parser), pat)};
  }

  if (!IsIdentTok(*tok)) {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);
  if (!IsOp(next, "::")) {
    SPEC_RULE("Parse-Pattern-Identifier");
    IdentifierPattern pat;
    pat.name = std::string(tok->lexeme);
    pat.name_splice_opt = std::nullopt;
    return ParseElemResult<PatternPtr>{
        next,
        MakePattern(tok->span, pat)};
  }

  SPEC_RULE("Parse-IfCase-Pattern-EnumHead");
  Parser start = parser;
  ParseQualifiedHeadResult head = ParseQualifiedHead(parser);
  EnumPattern pat;
  pat.path = head.module_path;
  pat.name = head.name;
  pat.payload_opt = std::nullopt;
  return ParseElemResult<PatternPtr>{
      head.parser,
      MakePattern(SpanBetween(start, head.parser), pat)};
}

ParseElemResult<IfCaseClause> ParseIfCaseClause(Parser parser) {
  if (IsPunc(parser, ":")) {
    SPEC_RULE("Parse-If-Is-TypeTest");
    Parser after_colon = parser;
    Advance(after_colon);
    ParseElemResult<std::shared_ptr<Type>> type = ParseType(after_colon);

    TypedPattern pat;
    pat.name = "_";
    pat.type = type.elem;
    pat.name_splice_opt = std::nullopt;

    ParseElemResult<ExprPtr> body = ParseCaseBodyBlock(type.parser);
    IfCaseClause clause;
    clause.pattern = MakePattern(SpanBetween(parser, type.parser), std::move(pat));
    clause.body = body.elem;
    return {body.parser, std::move(clause)};
  }

  Parser speculative = Clone(parser);
  ParseElemResult<PatternPtr> full_pattern = ParsePattern(speculative);
  if (IsPunc(full_pattern.parser, "{")) {
    ParseElemResult<ExprPtr> body = ParseCaseBodyBlock(full_pattern.parser);
    IfCaseClause clause;
    clause.pattern = full_pattern.elem;
    clause.body = body.elem;
    return {body.parser, std::move(clause)};
  }

  if (std::optional<ParseElemResult<PatternPtr>> fallback =
          TryParseBraceDisambiguatedIfCasePattern(Clone(parser));
      fallback.has_value() && IsPunc(fallback->parser, "{")) {
    ParseElemResult<ExprPtr> body = ParseCaseBodyBlock(fallback->parser);
    IfCaseClause clause;
    clause.pattern = fallback->elem;
    clause.body = body.elem;
    return {body.parser, std::move(clause)};
  }

  EmitParseSyntaxErr(full_pattern.parser, TokSpan(full_pattern.parser));
  return {full_pattern.parser, IfCaseClause{}};
}

IfCaseListResult ParseIfCaseList(Parser parser) {
  Parser cur = parser;
  std::vector<IfCaseClause> cases;
  ExprPtr else_opt;

  SkipCaseListNewlines(cur);
  while (!AtEof(cur) && !IsPunc(cur, "}")) {
    if (IsKw(cur, "else")) {
      if (cases.empty()) {
        EmitParseSyntaxErr(cur, TokSpan(cur));
      } else {
        SPEC_RULE("Parse-IfCasesTail-Else");
      }
      Parser after_else = cur;
      Advance(after_else);
      ParseElemResult<std::shared_ptr<Block>> else_block = ParseBlock(after_else);
      else_opt = WrapBlockExpr(after_else, else_block);
      cur = else_block.parser;
      SkipCaseListNewlines(cur);
      break;
    }

    if (cases.empty()) {
      SPEC_RULE("Parse-IfCases-Cons");
    } else {
      SPEC_RULE("Parse-IfCasesTail-Cons");
    }
    SPEC_RULE("Parse-IfCase");
    ParseElemResult<IfCaseClause> clause = ParseIfCaseClause(cur);
    if (clause.parser.index <= cur.index) {
      Parser sync = cur;
      SyncStmt(sync);
      if (sync.index <= cur.index) {
        break;
      }
      cur = sync;
      SkipCaseListNewlines(cur);
      continue;
    }
    cases.push_back(std::move(clause.elem));

    cur = clause.parser;
    SkipCaseListNewlines(cur);
  }

  if (!cases.empty() && !else_opt && IsPunc(cur, "}")) {
    SPEC_RULE("Parse-IfCasesTail-End");
  }

  return {cur, std::move(cases), else_opt};
}

IfHeadResult ParseIfHeadNoElse(Parser parser) {
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "if"

  ParseElemResult<ExprPtr> first = ParseExprNoBrace(next);

  if (IsKw(first.parser, "is")) {
    Parser after_is = first.parser;
    Advance(after_is);  // consume "is"

    if (IsPunc(after_is, "{")) {
      SPEC_RULE("Parse-If-Is-CaseList");
      Parser after_lbrace = after_is;
      Advance(after_lbrace);
      IfCaseListResult case_list = ParseIfCaseList(after_lbrace);
      if (!IsPunc(case_list.parser, "}")) {
        EmitParseSyntaxErr(case_list.parser, TokSpan(case_list.parser));
        Parser sync = case_list.parser;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{}), std::nullopt};
      }
      Parser after_rbrace = case_list.parser;
      Advance(after_rbrace);

      if (case_list.cases.empty()) {
        EmitParseSyntaxErr(after_is, TokSpan(after_is));
        Parser sync = after_rbrace;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{}), std::nullopt};
      }

      IfCaseExpr if_case;
      if_case.scrutinee = first.elem;
      if_case.cases = std::move(case_list.cases);
      if_case.else_expr = case_list.else_opt;

      return {after_rbrace, MakeExpr(SpanBetween(start, after_rbrace), if_case), std::nullopt};
    }

    SPEC_RULE("Parse-If-Is-Single");
    ParseElemResult<IfCaseClause> clause = ParseIfCaseClause(after_is);

    PendingIfArm arm;
    arm.start = start;
    arm.kind = PendingIfKind::IsSingle;
    arm.cond_or_scrutinee = first.elem;
    arm.pattern_opt = clause.elem.pattern;
    arm.then_expr = clause.elem.body;
    return {clause.parser, nullptr, std::move(arm)};
  }

  ParseElemResult<std::shared_ptr<Block>> then_block = ParseBlock(first.parser);
  ExprPtr then_node = WrapBlockExpr(first.parser, then_block);

  PendingIfArm arm;
  arm.start = start;
  arm.kind = PendingIfKind::Plain;
  arm.cond_or_scrutinee = first.elem;
  arm.pattern_opt = nullptr;
  arm.then_expr = then_node;
  return {then_block.parser, nullptr, std::move(arm)};
}

}  // namespace

ParseElemResult<ExprPtr> ParseIfExpr(Parser parser) {
  SPEC_RULE("ControlExpressionParsingRemainderFamily");
  SPEC_RULE("Parse-If-Expr");
  IfHeadResult head = ParseIfHeadNoElse(parser);
  if (head.completed_expr) {
    return {head.parser, head.completed_expr};
  }

  std::vector<PendingIfArm> chain;
  if (head.pending.has_value()) {
    chain.push_back(std::move(*head.pending));
  }

  Parser cur = head.parser;
  ExprPtr else_opt;

  while (IsKw(cur, "else")) {
    Parser after_else = cur;
    Advance(after_else);
    if (!IsKw(after_else, "if")) {
      SPEC_RULE("Parse-ElseOpt-Block");
      ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(after_else);
      else_opt = WrapBlockExpr(after_else, block);
      cur = block.parser;
      break;
    }

    SPEC_RULE("Parse-ElseOpt-If");
    IfHeadResult next_head = ParseIfHeadNoElse(after_else);
    if (next_head.completed_expr) {
      else_opt = next_head.completed_expr;
      cur = next_head.parser;
      break;
    }
    if (next_head.pending.has_value()) {
      chain.push_back(std::move(*next_head.pending));
    }
    cur = next_head.parser;
  }

  if (!IsKw(cur, "else")) {
    SPEC_RULE("Parse-ElseOpt-None");
  }

  ExprPtr result = else_opt;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    result = MaterializePendingIfArm(*it, cur, result);
  }

  return {cur, result};
}

}  // namespace ultraviolet::ast
