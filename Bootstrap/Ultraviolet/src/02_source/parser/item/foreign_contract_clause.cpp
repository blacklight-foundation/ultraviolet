// =============================================================================
// foreign_contract_clause.cpp - Foreign Contract Clause Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.6.13 (Foreign Contract Rules)
//
// This file implements foreign contract clause parsing for extern declarations:
//   - IsForeignContractStart: Check if position starts foreign contract
//   - ParseForeignContractClauseListOpt: Parse optional foreign contract clause list
//   - ParseForeignContractClause: Parse single foreign contract clause
//
// SYNTAX:
//   |: @foreign_assumes(pred && pred)
//   |: @foreign_ensures(pred && pred)
//   |: @foreign_ensures(@error: pred)
//   |: @foreign_ensures(@null_result: pred)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast
{

  // Use lexer types
  using ultraviolet::lexer::IsIdentTok;
  using ultraviolet::lexer::Token;
  using ultraviolet::lexer::TokenKind;

  // Forward declarations for helper functions
  bool IsOp(const Parser &parser, std::string_view op);
  bool IsPunc(const Parser &parser, std::string_view p);
  void SkipNewlines(Parser& parser);

  // Forward declaration for predicate parsing
  ParseElemResult<ExprPtr> ParsePredicateExpr(Parser parser);

  // =============================================================================
  // IsForeignContractStart - Check if position starts a foreign contract
  // =============================================================================
  //
  // Foreign contracts have the form:
  //   |: @foreign_assumes(predicate_expr)
  //   |: @foreign_ensures(ensures_predicate)

  bool IsForeignContractStart(const Parser &parser)
  {
    if (!IsOp(parser, "|:"))
    {
      return false;
    }
    Parser probe = parser;
    Advance(probe); // skip |:

    const Token *tok = Tok(probe);
    if (!tok || tok->kind != TokenKind::Operator || tok->lexeme != "@")
    {
      return false;
    }
    Advance(probe); // skip @

    tok = Tok(probe);
    return tok && IsIdentTok(*tok) &&
           (tok->lexeme == "foreign_assumes" ||
            tok->lexeme == "foreign_ensures");
  }

  namespace
  {
    ParseElemResult<ExprPtr> ParseForeignPredicate(Parser parser)
    {
      return ParsePredicateExpr(parser);
    }

    struct EnsuresPredicate
    {
      ForeignContractKind kind = ForeignContractKind::Ensures;
      ExprPtr pred;
    };

    ParseElemResult<EnsuresPredicate> ParseEnsuresPredicate(Parser parser)
    {
      EnsuresPredicate out;
      if (IsOp(parser, "@"))
      {
        Parser next = parser;
        Advance(next);
        const Token *name = Tok(next);
        if (name && IsIdentTok(*name) &&
            (name->lexeme == "error" || name->lexeme == "null_result"))
        {
          out.kind = (name->lexeme == "error")
                         ? ForeignContractKind::EnsuresError
                         : ForeignContractKind::EnsuresNullResult;
          if (name->lexeme == "error")
          {
            SPEC_RULE("Parse-EnsuresPredicate-Error");
          }
          else
          {
            SPEC_RULE("Parse-EnsuresPredicate-NullResult");
          }
          Advance(next);
          if (!IsPunc(next, ":"))
          {
            EmitParseSyntaxErr(next, TokSpan(next));
          }
          else
          {
            Advance(next);
          }
          ParseElemResult<ExprPtr> pred = ParseForeignPredicate(next);
          out.pred = pred.elem;
          return {pred.parser, out};
        }
      }

      SPEC_RULE("Parse-EnsuresPredicate-Plain");
      ParseElemResult<ExprPtr> pred = ParseForeignPredicate(parser);
      out.kind = ForeignContractKind::Ensures;
      out.pred = pred.elem;
      return {pred.parser, out};
    }

    ParseElemResult<ForeignContractClause> ParseForeignContractClause(
        Parser parser)
    {
      SkipNewlines(parser);
      Parser start = parser;
      ForeignContractClause clause;

      if (!IsOp(parser, "|:"))
      {
        EmitParseSyntaxErr(parser, TokSpan(parser));
        clause.span = TokSpan(parser);
        return {parser, clause};
      }
      Advance(parser); // |:

      if (!IsOp(parser, "@"))
      {
        EmitParseSyntaxErr(parser, TokSpan(parser));
        clause.span = SpanBetween(start, parser);
        return {parser, clause};
      }
      Advance(parser); // @

      const Token* head = Tok(parser);
      if (!head || !IsIdentTok(*head))
      {
        EmitParseSyntaxErr(parser, TokSpan(parser));
        clause.span = SpanBetween(start, parser);
        return {parser, clause};
      }

      const std::string_view head_name = head->lexeme;
      Advance(parser); // foreign_assumes / foreign_ensures

      if (!IsPunc(parser, "("))
      {
        EmitParseSyntaxErr(parser, TokSpan(parser));
        clause.span = SpanBetween(start, parser);
        return {parser, clause};
      }
      Advance(parser); // (

      if (head_name == "foreign_assumes")
      {
        SPEC_RULE("Parse-ForeignContractClause-Assumes");
        ParseElemResult<ExprPtr> pred = ParseForeignPredicate(parser);
        parser = pred.parser;
        if (!IsPunc(parser, ")"))
        {
          EmitParseSyntaxErr(parser, TokSpan(parser));
          clause.span = SpanBetween(start, parser);
          return {parser, clause};
        }
        Advance(parser); // )

        clause.kind = ForeignContractKind::Assumes;
        clause.predicates.push_back(pred.elem);
        clause.span = SpanBetween(start, parser);
        return {parser, clause};
      }

      if (head_name == "foreign_ensures")
      {
        SPEC_RULE("Parse-ForeignContractClause-Ensures");
        ParseElemResult<EnsuresPredicate> pred = ParseEnsuresPredicate(parser);
        parser = pred.parser;
        if (!IsPunc(parser, ")"))
        {
          EmitParseSyntaxErr(parser, TokSpan(parser));
          clause.span = SpanBetween(start, parser);
          return {parser, clause};
        }
        Advance(parser); // )

        clause.kind = pred.elem.kind;
        clause.predicates.push_back(pred.elem.pred);
        clause.span = SpanBetween(start, parser);
        return {parser, clause};
      }

      EmitParseSyntaxErr(parser, TokSpan(parser));
      clause.span = SpanBetween(start, parser);
      return {parser, clause};
    }

    ParseElemResult<std::vector<ForeignContractClause>>
    ParseForeignContractClauseListTail(Parser parser,
                                      std::vector<ForeignContractClause> xs)
    {
      Parser probe = parser;
      SkipNewlines(probe);
      if (!IsForeignContractStart(probe))
      {
        SPEC_RULE("Parse-ForeignContractClauseListTail-End");
        return {parser, xs};
      }

      SPEC_RULE("Parse-ForeignContractClauseListTail-Cons");
      ParseElemResult<ForeignContractClause> clause =
          ParseForeignContractClause(probe);
      xs.push_back(std::move(clause.elem));
      return ParseForeignContractClauseListTail(clause.parser, std::move(xs));
    }

    ParseElemResult<std::vector<ForeignContractClause>>
    ParseForeignContractClauseList(Parser parser)
    {
      SPEC_RULE("Parse-ForeignContractClauseList-Cons");
      ParseElemResult<ForeignContractClause> first =
          ParseForeignContractClause(parser);
      std::vector<ForeignContractClause> clauses;
      clauses.push_back(std::move(first.elem));
      return ParseForeignContractClauseListTail(first.parser, std::move(clauses));
    }
  } // namespace

  ParseElemResult<std::optional<std::vector<ForeignContractClause>>>
  ParseForeignContractClauseListOpt(Parser parser)
  {
    Parser probe = parser;
    SkipNewlines(probe);
    if (!IsForeignContractStart(probe))
    {
      SPEC_RULE("Parse-ForeignContractClauseListOpt-None");
      return {parser, std::nullopt};
    }

    SPEC_RULE("Parse-ForeignContractClauseListOpt-Yes");
    ParseElemResult<std::vector<ForeignContractClause>> clauses =
        ParseForeignContractClauseList(probe);
    return {clauses.parser, std::move(clauses.elem)};
  }

} // namespace ultraviolet::ast
