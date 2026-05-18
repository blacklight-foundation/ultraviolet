// =============================================================================
// closure_type.cpp - Closure Type Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4718-4756
//
// Closure types: |T1, T2| -> R [shared: {deps}]
//
// Grammar:
//   closure_type ::= "|" param_type_list? "|" "->" type closure_deps?
//   param_type_list ::= param_type ("," param_type)* ","?
//   param_type ::= "move"? type
//   closure_deps ::= "[" "shared" ":" "{" shared_dep_list "}" "]"
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// Closure Type Parsing
// =============================================================================
//
// Closure types represent callable values that may capture environment.
// Syntax: |params| -> return_type
//
// Example:
//   |i32, i32| -> i32       // Takes two i32s, returns i32
//   |move T| -> ()          // Takes T by move, returns unit
//   || -> i32               // No params, returns i32
//
// =============================================================================

namespace {

ParseElemResult<std::shared_ptr<Type>> ParseClosureParamTypeInner(Parser parser) {
  if (IsPuncType(parser, "(")) {
    SPEC_RULE("Parse-ClosureParamType-Grouped");
    Parser after_l = parser;
    Advance(after_l);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_l);
    if (!IsPuncType(ty.parser, ")")) {
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

ParseElemResult<TypeFuncParam> ParseClosureParamType(Parser parser) {
  if (IsKwType(parser, "move")) {
    SPEC_RULE("Parse-ParamType-Move");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseClosureParamTypeInner(next);
    TypeFuncParam param;
    param.mode = ParamMode::Move;
    param.type = ty.elem;
    return {ty.parser, param};
  }

  SPEC_RULE("Parse-ParamType-Plain");
  ParseElemResult<std::shared_ptr<Type>> ty = ParseClosureParamTypeInner(parser);
  TypeFuncParam param;
  param.mode = std::nullopt;
  param.type = ty.elem;
  return {ty.parser, param};
}

ParseElemResult<std::vector<TypeFuncParam>> ParseClosureParamTypeListTail(
    Parser parser, std::vector<TypeFuncParam> params) {
  SkipNewlinesType(parser);
  if (IsOpType(parser, "|")) {
    SPEC_RULE("Parse-ClosureParamTypeListTail-End");
    return {parser, params};
  }

  if (!IsPuncType(parser, ",")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, params};
  }

  const EndSetToken end_set[] = {EndOperator("|")};
  Parser after = parser;
  Advance(after);
  SkipNewlinesType(after);
  if (IsOpType(after, "|")) {
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-ClosureParamTypeListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after.diags = parser.diags;
    return {after, params};
  }

  SPEC_RULE("Parse-ClosureParamTypeListTail-Comma");
  ParseElemResult<TypeFuncParam> param = ParseClosureParamType(after);
  params.push_back(param.elem);
  return ParseClosureParamTypeListTail(param.parser, std::move(params));
}

ParseElemResult<std::vector<TypeFuncParam>> ParseClosureParamTypeList(
    Parser parser) {
  SkipNewlinesType(parser);
  if (IsOpType(parser, "|")) {
    SPEC_RULE("Parse-ClosureParamTypeList-Empty");
    return {parser, {}};
  }

  SPEC_RULE("Parse-ClosureParamTypeList-Cons");
  ParseElemResult<TypeFuncParam> first = ParseClosureParamType(parser);
  std::vector<TypeFuncParam> params;
  params.push_back(first.elem);
  return ParseClosureParamTypeListTail(first.parser, std::move(params));
}

ParseElemResult<SharedDep> ParseSharedDep(Parser parser) {
  SPEC_RULE("Parse-SharedDep");
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Identifier) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, SharedDep{Identifier{"_"}, nullptr}};
  }
  Identifier name{tok->lexeme};
  Parser next = parser;
  Advance(next);
  if (!IsPuncType(next, ":")) {
    EmitParseSyntaxErr(next, TokSpan(next));
  } else {
    Advance(next);
  }
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
  SharedDep dep;
  dep.name = name;
  dep.type = ty.elem;
  return {ty.parser, dep};
}

ParseElemResult<std::vector<SharedDep>> ParseSharedDepList(Parser parser) {
  if (IsPuncType(parser, "}")) {
    SPEC_RULE("Parse-SharedDepList-Empty");
    return {parser, {}};
  }

  ParseElemResult<SharedDep> first = ParseSharedDep(parser);
  if (!IsPuncType(first.parser, ",")) {
    SPEC_RULE("Parse-SharedDepList-Single");
    return {first.parser, {first.elem}};
  }

  SPEC_RULE("Parse-SharedDepList-Cons");
  Parser after = first.parser;
  Advance(after);
  ParseElemResult<std::vector<SharedDep>> rest = ParseSharedDepList(after);
  std::vector<SharedDep> deps;
  deps.reserve(1 + rest.elem.size());
  deps.push_back(first.elem);
  deps.insert(deps.end(), rest.elem.begin(), rest.elem.end());
  return {rest.parser, std::move(deps)};
}

ParseElemResult<std::optional<std::vector<SharedDep>>> ParseClosureDepsOpt(
    Parser parser) {
  if (!IsPuncType(parser, "[")) {
    SPEC_RULE("Parse-ClosureDepsOpt-None");
    return {parser, std::nullopt};
  }

  SPEC_RULE("Parse-ClosureDepsOpt-Some");
  Parser next = parser;
  Advance(next);  // consume [

  if (!IsKwType(next, "shared")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    return {next, std::nullopt};
  }
  Advance(next);  // consume shared

  if (!IsPuncType(next, ":")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    return {next, std::nullopt};
  }
  Advance(next);  // consume :

  if (!IsPuncType(next, "{")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    return {next, std::nullopt};
  }
  Parser after_l = next;
  Advance(after_l);  // consume {

  ParseElemResult<std::vector<SharedDep>> deps = ParseSharedDepList(after_l);
  Parser after_deps = deps.parser;
  if (!IsPuncType(after_deps, "}")) {
    EmitParseSyntaxErr(after_deps, TokSpan(after_deps));
    return {after_deps, std::nullopt};
  }
  Advance(after_deps);  // consume }

  if (!IsPuncType(after_deps, "]")) {
    EmitParseSyntaxErr(after_deps, TokSpan(after_deps));
    return {after_deps, std::nullopt};
  }
  Parser after_r = after_deps;
  Advance(after_r);  // consume ]

  return {after_r, deps.elem};
}

}  // namespace

ParseElemResult<std::shared_ptr<Type>> ParseClosureType(Parser parser) {
  Parser start = parser;
  if (IsOpType(parser, "||")) {
    SPEC_RULE("Parse-ClosureParamTypeList-Empty");
    SPEC_RULE("Parse-Closure-Type-Empty");
    Parser after_bar = parser;
    Advance(after_bar);
    if (!IsOpType(after_bar, "->")) {
      EmitParseSyntaxErr(after_bar, TokSpan(after_bar));
      return {after_bar, MakeTypePrim(SpanBetween(start, after_bar), "!")};
    }
    Parser after_arrow = after_bar;
    Advance(after_arrow);
    ParseElemResult<std::shared_ptr<Type>> ret = ParseType(after_arrow);
    ParseElemResult<std::optional<std::vector<SharedDep>>> deps =
        ParseClosureDepsOpt(ret.parser);
    TypeClosure closure;
    closure.params = {};
    closure.ret = ret.elem;
    closure.deps_opt = deps.elem;
    return {deps.parser, MakeTypeNode(SpanBetween(start, deps.parser), closure)};
  }

  if (!IsOpType(parser, "|")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakeTypePrim(TokSpan(parser), "!")};
  }

  Parser after_l = parser;
  Advance(after_l);  // consume |

  if (IsOpType(after_l, "|")) {
    SPEC_RULE("Parse-ClosureParamTypeList-Empty");
    SPEC_RULE("Parse-Closure-Type-Empty");
    Parser after_bar = after_l;
    Advance(after_bar);  // consume |
    if (!IsOpType(after_bar, "->")) {
      EmitParseSyntaxErr(after_bar, TokSpan(after_bar));
      return {after_bar, MakeTypePrim(SpanBetween(start, after_bar), "!")};
    }
    Parser after_arrow = after_bar;
    Advance(after_arrow);  // consume ->
    ParseElemResult<std::shared_ptr<Type>> ret = ParseType(after_arrow);
    ParseElemResult<std::optional<std::vector<SharedDep>>> deps =
        ParseClosureDepsOpt(ret.parser);
    TypeClosure closure;
    closure.params = {};
    closure.ret = ret.elem;
    closure.deps_opt = deps.elem;
    return {deps.parser, MakeTypeNode(SpanBetween(start, deps.parser), closure)};
  }

  SPEC_RULE("Parse-Closure-Type");
  ParseElemResult<std::vector<TypeFuncParam>> params =
      ParseClosureParamTypeList(after_l);
  if (!IsOpType(params.parser, "|")) {
    EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
    return {params.parser, MakeTypePrim(SpanBetween(start, params.parser), "!")};
  }
  Parser after_bar = params.parser;
  Advance(after_bar);  // consume |
  if (!IsOpType(after_bar, "->")) {
    EmitParseSyntaxErr(after_bar, TokSpan(after_bar));
    return {after_bar, MakeTypePrim(SpanBetween(start, after_bar), "!")};
  }
  Parser after_arrow = after_bar;
  Advance(after_arrow);  // consume ->
  ParseElemResult<std::shared_ptr<Type>> ret = ParseType(after_arrow);
  ParseElemResult<std::optional<std::vector<SharedDep>>> deps =
      ParseClosureDepsOpt(ret.parser);
  TypeClosure closure;
  closure.params = std::move(params.elem);
  closure.ret = ret.elem;
  closure.deps_opt = deps.elem;
  return {deps.parser, MakeTypeNode(SpanBetween(start, deps.parser), closure)};
}

}  // namespace ultraviolet::ast
