// =============================================================================
// generic_params.cpp - Generic Parameter Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.13 (Generic Parameter Rules)
//
// This file implements generic type parameter parsing:
//   - ParseTypeBounds: Parse type bounds <: Class1, Class2
//   - ParseTypeParam: Parse single type parameter T <: Bound = Default
//   - ParseGenericParamsOpt: Parse optional generic parameters <T; U; V>
//
// CRITICAL DISTINCTION:
//   - Generic PARAMETERS: <T; U; V>  (SEMICOLONS)
//   - Generic ARGUMENTS:  <T, U, V>  (COMMAS)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/parser/type/type_parse_internal.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);

// Forward declaration for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<ClassPath> ParseClassPath(Parser parser);

namespace {

std::string GenericParamsPayload(std::string_view params_opt,
                                 std::size_t param_count,
                                 std::string_view terminator = {}) {
  std::string payload;
  payload.reserve(params_opt.size() + terminator.size() + 80);
  payload += "params_opt=";
  payload += params_opt;
  payload += ";param_count=";
  payload += std::to_string(param_count);
  if (!terminator.empty()) {
    payload += ";terminator=";
    payload += terminator;
  }
  return payload;
}

std::string_view VariancePayload(const std::optional<Variance>& variance) {
  if (!variance.has_value()) {
    return "none";
  }

  switch (*variance) {
    case Variance::Covariant:
      return "Covariant";
    case Variance::Contravariant:
      return "Contravariant";
    case Variance::Invariant:
      return "Invariant";
    case Variance::Bivariant:
      return "Bivariant";
  }

  return "unknown";
}

std::string TypeParamPayload(const TypeParam& param) {
  std::string payload;
  payload.reserve(param.name.size() + 96);
  payload += "name=";
  payload += param.name;
  payload += ";bound_count=";
  payload += std::to_string(param.bounds.size());
  payload += ";default_opt=";
  payload += param.default_type ? "some" : "none";
  payload += ";variance=";
  payload += VariancePayload(param.variance);
  return payload;
}

std::string ClassBoundPayload(const ClassPath& path,
                              std::string_view args_opt,
                              std::size_t arg_count) {
  std::string payload;
  payload.reserve(args_opt.size() + 96);
  payload += "path=";
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      payload += "::";
    }
    payload += path[i];
  }
  payload += ";args_opt=";
  payload += args_opt;
  payload += ";arg_count=";
  payload += std::to_string(arg_count);
  return payload;
}

void RecordGenericParamsRule(std::string_view rule_id,
                             const core::Span& span,
                             std::string_view params_opt,
                             std::size_t param_count,
                             std::string_view terminator = {}) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record(rule_id, span,
                            GenericParamsPayload(params_opt, param_count,
                                                 terminator));
}

void RecordClassBoundRule(std::string_view rule_id,
                          const core::Span& span,
                          const ClassPath& path,
                          std::string_view args_opt,
                          std::size_t arg_count) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record(rule_id, span,
                            ClassBoundPayload(path, args_opt, arg_count));
}

void RecordTypeParamRule(const TypeParam& param) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record("TypeParam", param.span, TypeParamPayload(param));
}

}  // namespace

// =============================================================================
// ParseTypeBounds - Parse type bounds: <: Class1, Class2
// =============================================================================
//
// SPEC: Parse-TypeBoundsOpt-None
//   ¬ IsOp(Tok(P), "<:")
//   ──────────────────────────────────────────────
//   Γ ⊢ ParseTypeBoundsOpt(P) ⇓ (P, [])
//
// SPEC: Parse-TypeBoundsOpt-Yes
//   IsOp(Tok(P), "<:")    Γ ⊢ ParseClassBoundList(Advance(P)) ⇓ (P_1, bounds)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseTypeBoundsOpt(P) ⇓ (P_1, bounds)

ParseElemResult<std::vector<TypeBound>> ParseTypeBounds(Parser parser) {
  auto parse_class_bound = [&](Parser p) -> ParseElemResult<TypeBound> {
    ParseElemResult<ClassPath> class_path = ParseClassPath(p);
    ParseGenericArgsResult args = ParseGenericArgsOpt(class_path.parser);

    TypeBound bound;
    bound.class_path = class_path.elem;
    if (args.args.has_value()) {
      bound.generic_args = std::move(*args.args);
    }
    RecordClassBoundRule("Parse-ClassBound", SpanBetween(p, args.parser),
                         bound.class_path,
                         bound.generic_args.empty() ? "none" : "some",
                         bound.generic_args.size());
    return {args.parser, std::move(bound)};
  };

  std::vector<TypeBound> bounds;
  if (!IsOp(parser, "<:")) {
    SPEC_RULE("Parse-TypeBoundsOpt-None");
    return {parser, bounds};
  }
  Parser next = parser;
  Advance(next);  // consume <:

  // Parse first bound
  ParseElemResult<TypeBound> first_bound = parse_class_bound(next);
  bounds.push_back(first_bound.elem);
  next = first_bound.parser;
  SPEC_RULE("Parse-ClassBoundList-Cons");

  // Parse additional bounds separated by ","
  while (IsPunc(next, ",")) {
    SPEC_RULE("Parse-ClassBoundListTail-Cons");
    Advance(next);
    ParseElemResult<TypeBound> bound = parse_class_bound(next);
    bounds.push_back(bound.elem);
    next = bound.parser;
  }
  SPEC_RULE("Parse-ClassBoundListTail-End");
  SPEC_RULE("Parse-TypeBoundsOpt-Yes");

  return {next, bounds};
}

// =============================================================================
// ParseTypeParam - Parse single type parameter
// =============================================================================
//
// Parses: T <: Bound = DefaultType
//
// Components:
//   - name: identifier
//   - bounds: optional <: clause
//   - default_type: optional = Type

ParseElemResult<TypeParam> ParseTypeParam(Parser parser) {
  Parser start = parser;

  // Parse name
  ParseElemResult<Identifier> name = ParseIdent(parser);

  // Parse optional bounds
  ParseElemResult<std::vector<TypeBound>> bounds = ParseTypeBounds(name.parser);

  // Parse optional default type
  std::shared_ptr<Type> default_type;
  Parser after_bounds = bounds.parser;
  if (IsOp(after_bounds, "=")) {
    SPEC_RULE("Parse-TypeDefaultOpt-Yes");
    Advance(after_bounds);
    ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_bounds);
    default_type = ty.elem;
    after_bounds = ty.parser;
  } else {
    SPEC_RULE("Parse-TypeDefaultOpt-None");
  }

  TypeParam param;
  param.name = name.elem;
  param.bounds = bounds.elem;
  param.default_type = default_type;
  param.variance = std::nullopt;
  param.span = SpanBetween(start, after_bounds);
  RecordTypeParamRule(param);

  return {after_bounds, param};
}

// =============================================================================
// ParseTypeParamTail - Parse type parameter tail after the first parameter
// =============================================================================
//
// SPEC: Parse-TypeParamTail-End
//   ¬ IsPunc(Tok(P), ";")
//   ──────────────────────────────────────────────
//   Γ ⊢ ParseTypeParamTail(P, ps) ⇓ (P, ps)
//
// SPEC: Parse-TypeParamTail-Cons
//   IsPunc(Tok(P), ";")    Γ ⊢ ParseTypeParam(Advance(P)) ⇓ (P_1, p)
//   Γ ⊢ ParseTypeParamTail(P_1, ps ++ [p]) ⇓ (P_2, ps')
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseTypeParamTail(P, ps) ⇓ (P_2, ps')
//
// CRITICAL: Parameters are separated by SEMICOLONS (;), not commas!
// This is different from generic arguments which use commas.

ParseElemResult<std::vector<TypeParam>> ParseTypeParamTail(
    Parser parser,
    std::vector<TypeParam> params) {
  if (!IsPunc(parser, ";")) {
    const Token* terminator = Tok(parser);
    RecordGenericParamsRule("Parse-TypeParamTail-End", TokSpan(parser),
                            "tail_end", params.size(),
                            terminator ? std::string_view(terminator->lexeme)
                                       : std::string_view{});
    return {parser, std::move(params)};
  }

  Parser after_semicolon = parser;
  Advance(after_semicolon);
  ParseElemResult<TypeParam> param = ParseTypeParam(after_semicolon);
  params.push_back(param.elem);
  ParseElemResult<std::vector<TypeParam>> tail =
      ParseTypeParamTail(param.parser, std::move(params));
  RecordGenericParamsRule("Parse-TypeParamTail-Cons",
                          SpanBetween(parser, tail.parser), "tail_cons",
                          tail.elem.size());
  return tail;
}

// =============================================================================
// ParseGenericParams - Parse required generic parameters
// =============================================================================
//
// SPEC: Parse-GenericParams
//   IsOp(Tok(P), "<")    Γ ⊢ ParseTypeParam(Advance(P)) ⇓ (P_1, p_1)
//   Γ ⊢ ParseTypeParamTail(P_1, [p_1]) ⇓ (P_2, ps)    IsOp(Tok(P_2), ">")
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseGenericParams(P) ⇓ (Advance(P_2), ps)

ParseElemResult<GenericParams> ParseGenericParams(Parser parser) {
  Parser start = parser;
  Parser next = parser;
  if (!IsOp(next, "<")) {
    EmitParseSyntaxErr(next, TokSpan(next));
  } else {
    Advance(next);
  }

  ParseElemResult<TypeParam> first = ParseTypeParam(next);
  std::vector<TypeParam> params;
  params.push_back(first.elem);
  ParseElemResult<std::vector<TypeParam>> tail =
      ParseTypeParamTail(first.parser, std::move(params));
  next = tail.parser;

  // Detect Rust-style comma separators in generic parameter lists
  if (IsPunc(next, ",")) {
    auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(next));
    if (diag) {
      diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                "replace `,` with `;`",
                                TokSpan(next), ";"});
      // Emit on the active parser state so the diagnostic propagates.
      core::Emit(next.diags, *diag);
    }
  }

  if (!IsOp(next, ">")) {
    EmitParseSyntaxErr(next, TokSpan(next));
  } else {
    Advance(next);
  }

  GenericParams params_node;
  params_node.params = std::move(tail.elem);
  params_node.span = SpanBetween(start, next);
  TypeParamNames(params_node.params, params_node.span);
  RecordGenericParamsRule("Parse-GenericParams", params_node.span, "required",
                          params_node.params.size());
  return {next, std::move(params_node)};
}

// =============================================================================
// ParseGenericParamsOpt - Parse optional generic parameters
// =============================================================================
//
// SPEC: Parse-GenericParamsOpt-None
//   ¬ IsOp(Tok(P), "<")
//   ──────────────────────────────────────────────
//   Γ ⊢ ParseGenericParamsOpt(P) ⇓ (P, ⊥)
//
// SPEC: Parse-GenericParamsOpt-Yes
//   Γ ⊢ ParseGenericParams(P) ⇓ (P_1, params)
//   ──────────────────────────────────────────────
//   Γ ⊢ ParseGenericParamsOpt(P) ⇓ (P_1, params)

ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser) {
  if (!IsOp(parser, "<")) {
    RecordGenericParamsRule("Parse-GenericParamsOpt-None", TokSpan(parser),
                            "none", 0);
    return {parser, std::nullopt};
  }

  ParseElemResult<GenericParams> params = ParseGenericParams(parser);
  RecordGenericParamsRule("Parse-GenericParamsOpt-Yes",
                          SpanBetween(parser, params.parser), "some",
                          params.elem.params.size());

  return {params.parser, std::move(params.elem)};
}

}  // namespace cursive::ast
