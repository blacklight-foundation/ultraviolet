// =============================================================================
// refinement_clause.cpp - Type Refinement Clause Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4674-4686
//
// Parses type refinement clauses: T |: { predicate }
// - Adds compile-time predicates to base types
// - The predicate must be pure (no side effects, no capability access)
// - `self` refers to a value of the base type in the predicate
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseRefinementClause - Parse Type Refinement: |: { predicate }
// =============================================================================
// SPEC: Lines 4674-4686
// Recognizes `|: { predicate }` after a base type, then wraps the base type in
// a TypeRefine node.

ParseElemResult<ExprPtr> ParseRefinementOpt(Parser parser) {
  if (!IsOpType(parser, "|:")) {
    SPEC_RULE("Parse-RefinementOpt-None");
    return {parser, nullptr};
  }

  Parser after_clause = parser;
  Advance(after_clause);  // consume '|:'

  if (!IsPuncType(after_clause, "{")) {
    SPEC_RULE("Parse-RefinementOpt-None");
    return {parser, nullptr};
  }

  Parser after_l = after_clause;
  Advance(after_l);  // consume '{'

  ParseElemResult<ExprPtr> pred = ParsePredicateExpr(after_l);
  if (!IsPuncType(pred.parser, "}")) {
    EmitParseSyntaxErr(pred.parser, TokSpan(pred.parser));
    Parser sync = pred.parser;
    SyncType(sync);
    return {sync, nullptr};
  }

  SPEC_RULE("Parse-RefinementOpt-Yes");
  Parser after_r = pred.parser;
  Advance(after_r);  // consume '}'
  return {after_r, pred.elem};
}

ParseRefinementResult ParseRefinementClause(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> base) {
  ParseElemResult<ExprPtr> pred = ParseRefinementOpt(parser);
  if (!pred.elem) {
    return {pred.parser, base};
  }

  SPEC_RULE("Parse-Refinement-Type");

  TypeRefine refine;
  refine.base = std::move(base);
  refine.predicate = pred.elem;

  return {pred.parser, MakeTypeNode(SpanBetween(start, pred.parser), refine)};
}

}  // namespace ultraviolet::ast
