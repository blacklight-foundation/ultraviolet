// =============================================================================
// contract_clause.cpp - Contract Clause Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.13 (Contract Clause Rules)
//
// This file implements contract clause parsing:
//   - ParseContractClauseOpt: Parse optional contract clause |: pre => post
//
// SYNTAX FORMS:
//   |: precondition               -- precondition only
//   |: precondition => postcondition  -- both
//   |: => postcondition           -- postcondition only (pre is true)
//
// CONTRACT INTRINSICS:
//   @result      -- references return value (postcondition only)
//   @entry(expr) -- captures expr value at procedure entry
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast
{

  // Use lexer types
  using cursive::lexer::IsIdentTok;
  using cursive::lexer::Token;
  using cursive::lexer::TokenKind;

  // Forward declarations for helper functions
  bool IsOp(const Parser &parser, std::string_view op);

  // Forward declaration for predicate parsing
  ParseElemResult<ExprPtr> ParsePredicateExpr(Parser parser);

  namespace
  {

    // =============================================================================
    // IsForeignContractStart - Check if position starts a foreign contract
    // =============================================================================
    //
    // Foreign contracts have the form:
    //   |: @foreign_assumes(...)
    //   |: @foreign_ensures(...)
    //
    // This function checks for that pattern to avoid parsing foreign contracts
    // as regular contracts.

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
      if (!tok || !IsIdentTok(*tok))
      {
        return false;
      }
      return tok->lexeme == "foreign_assumes" ||
             tok->lexeme == "foreign_ensures";
    }

  } // namespace

  // =============================================================================
  // ParseContractClauseOpt - Parse optional contract clause
  // =============================================================================
  //
  // SPEC: Parse-ContractClauseOpt-None
  //   ¬ IsOp(Tok(P), "|:") ∨ ForeignContractStart(P)
  //   ──────────────────────────────────────────────
  //   Γ ⊢ ParseContractClauseOpt(P) ⇓ (P, ⊥)
  //
  // SPEC: Parse-ContractBody-PostOnly
  //   IsOp(Tok(P), "=>")    Γ ⊢ ParsePredicateExpr(Advance(P)) ⇓ (P_1, post)
  //   ────────────────────────────────────────────────────────────────
  //   Γ ⊢ ParseContractBody(P) ⇓ (P_1, ⟨⊥, post⟩)
  //
  // SPEC: Parse-ContractBody-PrePost
  //   Γ ⊢ ParsePredicateExpr(P) ⇓ (P_1, pre)    IsOp(Tok(P_1), "=>")
  //   Γ ⊢ ParsePredicateExpr(Advance(P_1)) ⇓ (P_2, post)
  //   ────────────────────────────────────────────────────────────────
  //   Γ ⊢ ParseContractBody(P) ⇓ (P_2, ⟨pre, post⟩)
  //
  // SPEC: Parse-ContractBody-PreOnly
  //   Γ ⊢ ParsePredicateExpr(P) ⇓ (P_1, pre)    ¬ IsOp(Tok(P_1), "=>")
  //   ────────────────────────────────────────────────────────────────
  //   Γ ⊢ ParseContractBody(P) ⇓ (P_1, ⟨pre, ⊥⟩)

  ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
      Parser parser)
  {
    // Probe past newlines, but do not consume them unless we actually parse
    // a contract clause. This preserves newline-as-terminator for callers.
    Parser probe = parser;
    while (Tok(probe) && Tok(probe)->kind == TokenKind::Newline)
    {
      Advance(probe);
    }
    if (!IsOp(probe, "|:"))
    {
      SPEC_RULE("Parse-ContractClauseOpt-None");
      return {parser, std::nullopt};
    }

    // Check for foreign contract
    if (IsForeignContractStart(probe))
    {
      SPEC_RULE("Parse-ContractClauseOpt-None");
      return {parser, std::nullopt};
    }

    parser = probe;
    SPEC_RULE("Parse-ContractClauseOpt-Yes");
    SPEC_RULE("Parse-Contract-Clause");
    Parser start = parser;
    Parser next = parser;
    Advance(next); // consume |:

    ContractClause clause;

    // Check for => (postcondition only case)
    if (IsOp(next, "=>"))
    {
      SPEC_RULE("Parse-ContractBody-PostOnly");
      Advance(next);
      ParseElemResult<ExprPtr> post = ParsePredicateExpr(next);
      clause.postcondition = post.elem;
      clause.span = SpanBetween(start, post.parser);
      return {post.parser, clause};
    }

    // Parse precondition
    Parser pre_parser = next;
    pre_parser.stop_before_contract_arrow = true;
    ParseElemResult<ExprPtr> pre = ParsePredicateExpr(pre_parser);
    clause.precondition = pre.elem;
    next = pre.parser;
    next.stop_before_contract_arrow = false;

    // Check for => postcondition
    if (IsOp(next, "=>"))
    {
      SPEC_RULE("Parse-ContractBody-PrePost");
      Advance(next);
      ParseElemResult<ExprPtr> post = ParsePredicateExpr(next);
      clause.postcondition = post.elem;
      next = post.parser;
    }
    else
    {
      SPEC_RULE("Parse-ContractBody-PreOnly");
    }

    clause.span = SpanBetween(start, next);
    return {next, clause};
  }

} // namespace cursive::ast
