// =============================================================================
// function_type.cpp - Function Type Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4713-4716
// Section 3.3.7.1 (Param Type Lists), Lines 4847-4879
//
// Parses function types: (T1, T2) -> R representing procedure pointer types.
// Also handles parameter type lists with optional 'move' modifier.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// HasFuncArrow - Lookahead Predicate
// =============================================================================
// Checks if the current position starts a function type by looking for
// matching parentheses followed by "->".

bool HasFuncArrow(const Parser& parser) {
  if (!IsPuncType(parser, "(")) {
    return false;
  }
  Parser cur = parser;
  int depth = 0;
  while (!AtEof(cur)) {
    const Token* tok = Tok(cur);
    if (!tok) {
      return false;
    }
    if (tok->kind == TokenKind::Punctuator) {
      if (tok->lexeme == "(") {
        ++depth;
      } else if (tok->lexeme == ")") {
        --depth;
        if (depth == 0) {
          Parser after = cur;
          Advance(after);
          return IsOpType(after, "->");
        }
      }
    }
    Advance(cur);
  }
  return false;
}

// =============================================================================
// ParseParamType - Parse Single Function Parameter Type
// =============================================================================
// SPEC: Lines 4871-4879

ParseElemResult<TypeFuncParam> ParseParamType(Parser parser) {
  // Parse-ParamType-Move
  if (IsKwType(parser, "move")) {
    SPEC_RULE("Parse-ParamType-Move");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
    TypeFuncParam param;
    param.mode = ParamMode::Move;
    param.type = ty.elem;
    return {ty.parser, param};
  }

  // Parse-ParamType-Plain
  SPEC_RULE("Parse-ParamType-Plain");
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(parser);
  TypeFuncParam param;
  param.mode = std::nullopt;
  param.type = ty.elem;
  return {ty.parser, param};
}

// =============================================================================
// ParseParamTypeListTail - Parse Remaining Parameter Types
// =============================================================================
// SPEC: Lines 4859-4867 and trailing-comma continuation rule in §5.5

ParseElemResult<std::vector<TypeFuncParam>> ParseParamTypeListTail(
    Parser parser, std::vector<TypeFuncParam> ps) {
  SkipNewlinesType(parser);

  // Parse-ParamTypeListTail-End
  if (IsPuncType(parser, ")")) {
    SPEC_RULE("Parse-ParamTypeListTail-End");
    return {parser, ps};
  }

  if (!IsPuncType(parser, ",")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, ps};
  }

  // Check for trailing comma
  const EndSetToken end_set[] = {EndPunct(")")};
  Parser after = parser;
  Advance(after);
  SkipNewlinesType(after);
  if (IsPuncType(after, ")")) {
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-ParamTypeListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after.diags = parser.diags;
    return {after, ps};
  }

  // Parse-ParamTypeListTail-Cons
  SPEC_RULE("Parse-ParamTypeListTail-Cons");
  ParseElemResult<TypeFuncParam> param = ParseParamType(after);
  ps.push_back(param.elem);
  return ParseParamTypeListTail(param.parser, std::move(ps));
}

// =============================================================================
// ParseParamTypeList - Parse Function Parameter Type List
// =============================================================================
// SPEC: Lines 4849-4857

ParseElemResult<std::vector<TypeFuncParam>> ParseParamTypeList(Parser parser) {
  SkipNewlinesType(parser);

  // Parse-ParamTypeList-Empty
  if (IsPuncType(parser, ")")) {
    SPEC_RULE("Parse-ParamTypeList-Empty");
    return {parser, {}};
  }

  // Parse-ParamTypeList-Cons
  SPEC_RULE("Parse-ParamTypeList-Cons");
  ParseElemResult<TypeFuncParam> first = ParseParamType(parser);
  std::vector<TypeFuncParam> params;
  params.push_back(first.elem);
  return ParseParamTypeListTail(first.parser, std::move(params));
}

// =============================================================================
// ParseFuncType - Parse Complete Function Type
// =============================================================================
// SPEC: Lines 4713-4716
// Syntax: (T1, T2) -> R

ParseElemResult<std::shared_ptr<Type>> ParseFuncType(Parser parser) {
  SPEC_RULE("Parse-Func-Type");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume (

  // Parse parameter list
  ParseElemResult<std::vector<TypeFuncParam>> params = ParseParamTypeList(next);

  // Expect closing )
  if (!IsPuncType(params.parser, ")")) {
    EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
    return {params.parser,
            MakeTypePrim(SpanBetween(start, params.parser), "!")};
  }
  Parser after_rparen = params.parser;
  Advance(after_rparen);  // consume )

  // Expect ->
  if (!IsOpType(after_rparen, "->")) {
    EmitParseSyntaxErr(after_rparen, TokSpan(after_rparen));
    return {after_rparen,
            MakeTypePrim(SpanBetween(start, after_rparen), "!")};
  }
  Parser after_arrow = after_rparen;
  Advance(after_arrow);  // consume ->

  // Parse return type
  ParseElemResult<std::shared_ptr<Type>> ret = ParseType(after_arrow);

  // Construct TypeFunc node
  TypeFunc func;
  func.params = std::move(params.elem);
  func.ret = ret.elem;
  return {ret.parser, MakeTypeNode(SpanBetween(start, ret.parser), func)};
}

}  // namespace ultraviolet::ast
