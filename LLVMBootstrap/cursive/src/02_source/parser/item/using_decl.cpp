// =============================================================================
// using_decl.cpp - Using Declaration Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.3 (Using Declaration Rules)
//
// This file implements using declaration parsing:
//   - ParseUsingModulePath: Parse the module path prefix for a using declaration
//   - ParseUsingSpec: Parse single item spec with optional alias
//   - ParseUsingList: Parse list of items { item1, item2 }
//   - ParseUsingDecl: Parse complete using declaration
//
// SYNTAX FORMS:
//   using path::item              -- single item
//   using path::item as alias     -- single with alias
//   using path::{item1, item2}    -- list
//   using path::*                 -- wildcard
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);

// =============================================================================
// ParseUsingModulePath - Parse module path for using declaration
// =============================================================================
//
// Parses path segments until the final `:: item` boundary or a list/wildcard
// suffix is encountered. The returned parser stays positioned on the `::`
// token that introduces the item/list/wildcard suffix.

ParseElemResult<ModulePath> ParseUsingModulePath(Parser parser) {
  SPEC_RULE("Parse-Using-Path");
  ParseElemResult<Identifier> head = ParseIdent(parser);
  ModulePath path;
  path.push_back(head.elem);
  Parser cur = head.parser;
  while (IsOp(cur, "::")) {
    Parser after_colons = cur;
    Advance(after_colons);
    if (IsPunc(after_colons, "{") || IsOp(after_colons, "*")) {
      break;
    }
    Parser probe = Clone(after_colons);
    ParseElemResult<Identifier> seg_probe = ParseIdent(probe);
    if (!IsOp(seg_probe.parser, "::")) {
      break;
    }
    ParseElemResult<Identifier> seg = ParseIdent(after_colons);
    path.push_back(seg.elem);
    cur = seg.parser;
  }
  return {cur, path};
}

// =============================================================================
// ParseUsingSpec - Parse single item specification
// =============================================================================
//
// Parses: identifier [as alias]

ParseElemResult<UsingSpec> ParseUsingSpec(Parser parser) {
  SPEC_RULE("Parse-UsingSpec");
  ParseElemResult<Identifier> name = ParseIdent(parser);
  ParseElemResult<std::optional<Identifier>> alias = ParseAliasOpt(name.parser);
  UsingSpec spec;
  spec.name = name.elem;
  spec.alias_opt = alias.elem;
  return {alias.parser, spec};
}

// =============================================================================
// ParseUsingListTail - Parse remaining items after first
// =============================================================================

ParseElemResult<std::vector<UsingSpec>> ParseUsingListTail(
    Parser parser,
    std::vector<UsingSpec> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-UsingListTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    const EndSetToken end_set[] = {EndPunct("}")};
    Parser after_comma = parser;
    Advance(after_comma);
    SkipNewlines(after_comma);
    if (IsPunc(after_comma, "}")) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-UsingListTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after_comma.diags = parser.diags;
      return {after_comma, xs};
    }
    SPEC_RULE("Parse-UsingListTail-Comma");
    ParseElemResult<UsingSpec> spec = ParseUsingSpec(after_comma);
    xs.push_back(spec.elem);
    return ParseUsingListTail(spec.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseUsingList - Parse list of items in braces
// =============================================================================
//
// Parses: { item1, item2 as alias, ... }

ParseElemResult<std::vector<UsingSpec>> ParseUsingList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-UsingList-Empty");
    Advance(parser);
    return {parser, {}};
  }
  SPEC_RULE("Parse-UsingList-Cons");
  ParseElemResult<UsingSpec> spec = ParseUsingSpec(parser);
  ParseElemResult<std::vector<UsingSpec>> tail =
      ParseUsingListTail(spec.parser, {spec.elem});
  if (!IsPunc(tail.parser, "}")) {
    EmitParseSyntaxErr(tail.parser, TokSpan(tail.parser));
    return {tail.parser, tail.elem};
  }
  Advance(tail.parser);
  return {tail.parser, tail.elem};
}

// =============================================================================
// ParseUsingDecl - Parse complete using declaration
// =============================================================================
//
// Called when "using" keyword is encountered.
// Determines variant (single item, list, wildcard) based on what follows the
// module path. Bare `using module` and `using module as alias` are rejected.

ParseItemResult ParseUsingDecl(Parser item_start, Parser parser, Visibility vis,
                               AttrOpt attrs_opt) {
  // Already know we're at "using" keyword
  Advance(parser);  // consume "using"

  ParseElemResult<ModulePath> module_path = ParseUsingModulePath(parser);
  parser = module_path.parser;

  if (!IsOp(parser, "::")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    SyncItem(parser);
    return {parser, ErrorItem{SpanBetween(item_start, parser), {}}};
  }
  Parser after_colons = parser;
  Advance(after_colons);

  if (IsPunc(after_colons, "{")) {
    SPEC_RULE("Parse-Using-List");
    Parser after_brace = after_colons;
    Advance(after_brace);
    ParseElemResult<std::vector<UsingSpec>> specs = ParseUsingList(after_brace);
    UsingDecl decl;
    decl.attrs_opt = std::move(attrs_opt);
    decl.vis = vis;
    decl.clause = UsingList{module_path.elem, std::move(specs.elem)};
    decl.span = SpanBetween(item_start, specs.parser);
    decl.doc = {};
    return {specs.parser, decl};
  }

  if (IsOp(after_colons, "*")) {
    SPEC_RULE("Parse-Using-Wildcard");
    Parser after_star = after_colons;
    Advance(after_star);
    UsingDecl decl;
    decl.attrs_opt = std::move(attrs_opt);
    decl.vis = vis;
    decl.clause = UsingWildcard{module_path.elem};
    decl.span = SpanBetween(item_start, after_star);
    decl.doc = {};
    return {after_star, decl};
  }

  if (!Tok(after_colons) ||
      (Tok(after_colons)->kind != TokenKind::Identifier &&
       Tok(after_colons)->kind != TokenKind::Keyword)) {
    EmitParseSyntaxErr(after_colons, TokSpan(after_colons));
    SyncItem(after_colons);
    return {after_colons, ErrorItem{SpanBetween(item_start, after_colons), {}}};
  }

  SPEC_RULE("Parse-Using-Item");
  ParseElemResult<Identifier> name = ParseIdent(after_colons);
  ParseElemResult<std::optional<Identifier>> alias = ParseAliasOpt(name.parser);
  UsingDecl decl;
  decl.attrs_opt = std::move(attrs_opt);
  decl.vis = vis;
  decl.clause = UsingItem{
      module_path.elem,
      name.elem,
      alias.elem,
  };
  decl.span = SpanBetween(item_start, alias.parser);
  decl.doc = {};
  return {alias.parser, decl};
}

}  // namespace cursive::ast
