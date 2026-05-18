// =============================================================================
// pipeline_expr.cpp - Pipeline Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Pipeline (Lines 5080-5083)
// - Parse-PipelineTail-Stop (Lines 5085-5087)
// - Parse-PipelineTail-Cons (Lines 5089-5093)
// - Parse-BasePostfix (Lines 5095-5098)
//
// Grammar:
//   pipeline_expr ::= base_postfix_expr ("=>" base_postfix_expr)*
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);

// Forward declarations from parser utilities
bool IsOp(const Parser& parser, std::string_view op);

// Forward declaration from postfix.cpp (base_postfix_expr)
ParseElemResult<ExprPtr> ParseBasePostfix(Parser parser, bool allow_brace,
                                          bool allow_bracket);

// =============================================================================
// ParsePipelineTail - Parse remaining pipeline segments
// =============================================================================

ParseElemResult<ExprPtr> ParsePipelineTail(Parser parser, ExprPtr lhs,
                                           bool allow_brace,
                                           bool allow_bracket) {
  if (parser.stop_before_contract_arrow && IsOp(parser, "=>")) {
    SPEC_RULE("Parse-PipelineTail-Stop");
    return {parser, lhs};
  }

  if (!IsOp(parser, "=>")) {
    SPEC_RULE("Parse-PipelineTail-Stop");
    return {parser, lhs};
  }

  SPEC_RULE("Parse-PipelineTail-Cons");
  Parser next = parser;
  Advance(next);  // consume =>
  ParseElemResult<ExprPtr> rhs = ParseBasePostfix(next, allow_brace, allow_bracket);
  PipelineExpr pipe;
  pipe.lhs = lhs;
  pipe.rhs = rhs.elem;
  ExprPtr combined =
      MakeExpr(SpanCover(lhs->span, rhs.elem ? rhs.elem->span : lhs->span), pipe);
  return ParsePipelineTail(rhs.parser, combined, allow_brace, allow_bracket);
}

// =============================================================================
// ParsePipeline - Parse pipeline expression
// =============================================================================

ParseElemResult<ExprPtr> ParsePipeline(Parser parser, bool allow_brace,
                                       bool allow_bracket) {
  SPEC_RULE("Parse-Pipeline");
  ParseElemResult<ExprPtr> base =
      ParseBasePostfix(parser, allow_brace, allow_bracket);
  return ParsePipelineTail(base.parser, base.elem, allow_brace, allow_bracket);
}

}  // namespace ultraviolet::ast
