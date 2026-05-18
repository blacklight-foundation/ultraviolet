// =============================================================================
// enum_decl.cpp - Enum Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.6.7 (Enum Declaration Rules)
//
// This file implements enum declaration parsing:
//   - ParseVariantPayloadOpt: Parse optional variant payload
//   - ParseVariantDiscriminantOpt: Parse optional explicit discriminant
//   - ParseVariant: Parse single variant
//   - ParseVariantSep/Members: Parse item-separated enum variants
//   - ParseEnumBody: Parse enum body in braces
//   - ParseEnumDecl: Parse complete enum declaration
//
// SYNTAX:
//   public enum Direction {
//     North
//     South
//     East
//     West
//   }
//   public enum Status {
//     Active = 1
//     Inactive = 0
//   }
//   public enum Result {
//     Ok(i32)
//     Err(string@View)
//   }
//   public enum Event {
//     Click{ x: i32, y: i32 }
//   }
//
// =============================================================================

#include "02_source/parser/parser.h"
#include "02_source/parser/type/type_parse_internal.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

// Forward declarations for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);

// Forward declarations for generic params and where clause parsing
ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser);
ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser);

// Forward declarations from record_decl.cpp
ParseElemResult<std::vector<ClassPath>> ParseImplementsOpt(Parser parser);
ParseElemResult<std::optional<TypeInvariant>> ParseInvariantOpt(Parser parser);

// =============================================================================
// ParseTypeList - Parse comma-separated list of types
// =============================================================================

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeList(
    Parser parser) {
  SkipNewlines(parser);

  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-TypeList-Empty");
    return {parser, {}};
  }

  SPEC_RULE("Parse-TypeList-Cons");
  ParseElemResult<std::shared_ptr<Type>> first = ParseType(parser);
  std::vector<std::shared_ptr<Type>> types;
  types.push_back(first.elem);
  return ParseTypeListTail(first.parser, std::move(types));
}

// =============================================================================
// ParseFieldDecl - Parse field declaration for record payloads
// =============================================================================
//
// Simple field without key boundary for enum variants

ParseElemResult<FieldDecl> ParseFieldDecl(Parser parser) {
  SPEC_RULE("Parse-FieldDecl");
  Parser start = parser;

  ParseElemResult<Visibility> vis = ParseVis(parser);
  ParseElemResult<bool> boundary = ParseKeyBoundaryOpt(vis.parser);
  ParseElemResult<Identifier> name = ParseIdent(boundary.parser);

  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);

  FieldDecl field;
  field.attrs = {};
  field.vis = vis.elem;
  field.key_boundary = boundary.elem;
  field.name = name.elem;
  field.type = ty.elem;
  field.init_opt = nullptr;
  field.span = SpanBetween(start, ty.parser);
  field.doc_opt = std::nullopt;

  return {ty.parser, field};
}

// =============================================================================
// ParseFieldDeclTail/List - Parse field list for record payloads
// =============================================================================

ParseElemResult<std::vector<FieldDecl>> ParseFieldDeclTail(
    Parser parser, std::vector<FieldDecl> xs) {
  SkipNewlines(parser);

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldDeclTail-End");
    return {parser, xs};
  }

  if (IsPunc(parser, ",")) {
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);

    if (IsPunc(after, "}")) {
      SPEC_RULE("Parse-FieldDeclTail-TrailingComma");
      return {after, xs};
    }

    SPEC_RULE("Parse-FieldDeclTail-Comma");
    ParseElemResult<FieldDecl> field = ParseFieldDecl(after);
    xs.push_back(field.elem);
    return ParseFieldDeclTail(field.parser, std::move(xs));
  }

  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

ParseElemResult<std::vector<FieldDecl>> ParseFieldDeclList(Parser parser) {
  SkipNewlines(parser);

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldDeclList-Empty");
    return {parser, {}};
  }

  SPEC_RULE("Parse-FieldDeclList-Cons");
  ParseElemResult<FieldDecl> field = ParseFieldDecl(parser);
  std::vector<FieldDecl> fields;
  fields.push_back(field.elem);
  return ParseFieldDeclTail(field.parser, std::move(fields));
}

// =============================================================================
// ParseVariantPayloadOpt - Parse optional variant payload
// =============================================================================
//
// Variant payloads can be:
//   1. None (unit variant)
//   2. Tuple: Ok(i32, bool)
//   3. Record: Error{ code: i32, message: string@View }

ParseElemResult<VariantPayload> ParseVariantPayloadOpt(Parser parser,
                                                       bool& has_payload) {
  if (!IsPunc(parser, "(") && !IsPunc(parser, "{")) {
    SPEC_RULE("Parse-VariantPayloadOpt-None");
    has_payload = false;
    VariantPayload payload = VariantPayloadTuple{};
    return {parser, payload};
  }

  if (IsPunc(parser, "(")) {
    SPEC_RULE("Parse-VariantPayloadOpt-Tuple");
    Parser next = parser;
    Advance(next);

    ParseElemResult<std::vector<std::shared_ptr<Type>>> types =
        ParseTypeList(next);

    if (!IsPunc(types.parser, ")")) {
      EmitParseSyntaxErr(types.parser, TokSpan(types.parser));
    } else {
      Advance(types.parser);
    }

    VariantPayloadTuple tuple;
    tuple.elements = std::move(types.elem);
    has_payload = true;
    return {types.parser, tuple};
  }

  SPEC_RULE("Parse-VariantPayloadOpt-Record");
  Parser next = parser;
  Advance(next);

  ParseElemResult<std::vector<FieldDecl>> fields = ParseFieldDeclList(next);

  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
  } else {
    Advance(fields.parser);
  }

  VariantPayloadRecord record;
  record.fields = std::move(fields.elem);
  has_payload = true;
  return {fields.parser, record};
}

// =============================================================================
// ParseVariantDiscriminantOpt - Parse optional explicit discriminant
// =============================================================================
//
// Format: = token
// Example: Active = 1

struct VariantDiscriminantResult {
  Parser parser;
  std::optional<Token> disc_opt;
};

VariantDiscriminantResult ParseVariantDiscriminantOpt(Parser parser) {
  if (!IsOp(parser, "=")) {
    SPEC_RULE("Parse-VariantDiscriminantOpt-None");
    return {parser, std::nullopt};
  }

  SPEC_RULE("Parse-VariantDiscriminantOpt-Yes");
  Parser next = parser;
  Advance(next);

  const Token* tok = Tok(next);
  if (!tok) {
    EmitParseSyntaxErr(next, TokSpan(next));
    return {next, std::nullopt};
  }

  if (tok->kind != TokenKind::IntLiteral) {
    EmitParseSyntaxErr(next, TokSpan(next));
    return {next, std::nullopt};
  }

  Token lit = *tok;
  Advance(next);
  return {next, lit};
}

// =============================================================================
// ParseVariant - Parse single variant
// =============================================================================
//
// Format: Name [Payload] [= discriminant]

ParseElemResult<VariantDecl> ParseVariant(Parser parser) {
  SPEC_RULE("Parse-Variant");
  Parser start = parser;

  ParseElemResult<Identifier> name = ParseIdent(parser);

  bool has_payload = false;
  ParseElemResult<VariantPayload> payload =
      ParseVariantPayloadOpt(name.parser, has_payload);

  VariantDiscriminantResult disc =
      ParseVariantDiscriminantOpt(payload.parser);

  VariantDecl var;
  var.name = name.elem;
  if (has_payload) {
    var.payload_opt = payload.elem;
  }
  var.discriminant_opt = disc.disc_opt;
  var.span = SpanBetween(start, disc.parser);
  var.doc_opt = std::nullopt;

  return {disc.parser, var};
}

// =============================================================================
// ParseVariantSep - Parse separator after an enum variant
// =============================================================================

void ParseVariantSep(Parser& parser) {
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-VariantSep-End");
    return;
  }
  SPEC_RULE("Parse-VariantSep-Terminator");
  ConsumeTerminatorReq(parser);
}

// =============================================================================
// ParseVariantMembers - Parse item-separated enum variants
// =============================================================================

ParseElemResult<std::vector<VariantDecl>> ParseVariantMembers(Parser parser) {
  // Skip leading newlines
  SkipNewlines(parser);

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-VariantMembers-Empty");
    return {parser, {}};
  }

  std::vector<VariantDecl> vars;
  Parser cur = parser;

  while (!IsPunc(cur, "}")) {
    SkipNewlines(cur);
    if (IsPunc(cur, "}")) {
      break;
    }

    // Enum variants are item-separated; commas are invalid separators.
    if (IsPunc(cur, ",")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
      continue;
    }

    SPEC_RULE("Parse-VariantMembers-Cons");
    Parser before = cur;
    ParseElemResult<VariantDecl> var = ParseVariant(cur);
    vars.push_back(var.elem);
    cur = var.parser;

    if (IsPunc(cur, ",")) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
    } else {
      ParseVariantSep(cur);
    }

    if (cur.tokens == before.tokens && cur.index == before.index) {
      EmitParseSyntaxErr(cur, TokSpan(cur));
      cur = AdvanceOrEOF(cur);
    }
  }

  return {cur, vars};
}

// =============================================================================
// ParseEnumBody - Parse enum body in braces
// =============================================================================

ParseElemResult<std::vector<VariantDecl>> ParseEnumBody(Parser parser) {
  SPEC_RULE("Parse-EnumBody");

  // Skip newlines before opening brace
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (!IsPunc(parser, "{")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  Parser next = parser;
  Advance(next);

  ParseElemResult<std::vector<VariantDecl>> vars = ParseVariantMembers(next);

  if (!IsPunc(vars.parser, "}")) {
    EmitParseSyntaxErr(vars.parser, TokSpan(vars.parser));
    return {vars.parser, vars.elem};
  }

  Advance(vars.parser);
  return {vars.parser, vars.elem};
}

// =============================================================================
// ParseEnumDecl - Parse complete enum declaration
// =============================================================================
//
// SPEC: Parse-Enum
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   IsKw(Tok(P_1), `enum`)
//   Γ ⊢ ParseIdent(Advance(P_1)) ⇓ (P_2, name)
//   Γ ⊢ ParseGenericParamsOpt(P_2) ⇓ (P_3, gen_params_opt)
//   Γ ⊢ ParseImplementsOpt(P_3) ⇓ (P_4, impls)
//   Γ ⊢ ParseWhereClauseOpt(P_4) ⇓ (P_5, where_clause_opt)
//   Γ ⊢ ParseEnumBody(P_5) ⇓ (P_6, variants)
//   Γ ⊢ ParseInvariantOpt(P_6) ⇓ (P_7, invariant_opt)
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_7, ⟨EnumDecl, ...⟩)

ParseItemResult ParseEnumDecl(Parser parser, Visibility vis,
                              AttributeList attrs) {
  SPEC_RULE("Parse-Enum");
  Parser start = parser;

  // Already know we're at "enum" keyword
  Advance(parser);  // consume "enum"

  // Parse enum name
  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  // Parse optional generic parameters
  ParseElemResult<std::optional<GenericParams>> gen_params =
      ParseGenericParamsOpt(parser);
  parser = gen_params.parser;

  // Parse optional implements list
  ParseElemResult<std::vector<ClassPath>> impls = ParseImplementsOpt(parser);
  parser = impls.parser;

  // Parse optional predicate clause
  ParseElemResult<std::optional<PredicateClause>> predicate_clause_opt =
      ParsePredicateClauseOpt(parser);
  parser = predicate_clause_opt.parser;

  // Parse enum body
  ParseElemResult<std::vector<VariantDecl>> vars = ParseEnumBody(parser);
  parser = vars.parser;

  // Parse optional type invariant
  ParseElemResult<std::optional<TypeInvariant>> invariant =
      ParseInvariantOpt(parser);
  parser = invariant.parser;

  EnumDecl decl;
  decl.attrs = attrs;
  decl.vis = vis;
  decl.name = name.elem;
  decl.generic_params = gen_params.elem;
  decl.predicate_clause_opt = predicate_clause_opt.elem;
  decl.implements = std::move(impls.elem);
  decl.variants = std::move(vars.elem);
  decl.invariant_opt = invariant.elem;
  decl.span = SpanBetween(start, parser);
  decl.doc = {};

  RecordGenericPredicateOwnerClause("EnumDecl", decl.name, decl.generic_params,
                                    decl.predicate_clause_opt, decl.span);
  RecordNominalRelationFormOnOwnerDecl("EnumDecl", decl.name, "implements",
                                       decl.implements, decl.span);

  return {parser, decl};
}

}  // namespace ultraviolet::ast
