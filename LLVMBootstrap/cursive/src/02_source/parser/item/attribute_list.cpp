// =============================================================================
// attribute_list.cpp - Attribute List Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.13 (Item Helper Parsing Rules)
//
// This file implements attribute list parsing:
//   - ParseAttributeItem: Parse single attribute item
//   - ParseAttributeListOpt: Parse optional attribute list [[...]]
//
// GRAMMAR:
//   AttributeList ::= ('[[' AttributeItem (',' AttributeItem)* ']]')*
//   AttributeItem ::= AttrName AttrArgsOpt
//   AttrArgsOpt ::= '(' AttrArgList ')' | ε
//   AttrArgList ::= AttrArg (',' AttrArg)* (','?)?
//   AttrArg ::= literal | identifier | identifier ':' literal
//             | identifier ':' identifier | identifier '(' AttrArgList ')'
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations for helper functions
bool IsPunc(const Parser& parser, std::string_view p);
bool IsOp(const Parser& parser, std::string_view op);
ParseElemResult<Identifier> ParseIdent(Parser parser);
void SkipNewlines(Parser& parser);

namespace {

// =============================================================================
// Helpers
// =============================================================================

static bool IsLiteralToken(const Token& tok) {
  return tok.kind == TokenKind::IntLiteral ||
         tok.kind == TokenKind::FloatLiteral ||
         tok.kind == TokenKind::StringLiteral ||
         tok.kind == TokenKind::CharLiteral ||
         tok.kind == TokenKind::BoolLiteral ||
         tok.kind == TokenKind::NullLiteral;
}

static void EmitAttrDiagById(Parser& parser,
                             std::string_view diag_id,
                             const core::Span& span) {
  if (auto diag = core::MakeDiagnosticById(diag_id, span)) {
    core::Emit(parser.diags, *diag);
    return;
  }
  EmitParseSyntaxErr(parser, span);
}

static void EmitAttrSyntaxErr(Parser& parser, const core::Span& span) {
  EmitAttrDiagById(parser, "E-MOD-2450", span);
}

// =============================================================================
// ParseAttrArgListTail - Parse remaining attribute args after first
// =============================================================================

ParseElemResult<std::vector<AttributeArg>> ParseAttrArgListTail(
    Parser parser, std::vector<AttributeArg> xs);
ParseElemResult<std::vector<AttributeArg>> ParseAttrArgList(Parser parser);
ParseElemResult<std::vector<AttributeArg>> ParseInlineArgList(Parser parser);
ParseElemResult<AttributeArg> ParseLayoutArg(Parser parser);
ParseElemResult<std::vector<AttributeArg>> ParseLayoutArgList(Parser parser);
ParseElemResult<std::vector<AttributeArg>> ParseLayoutArgListTail(
    Parser parser, std::vector<AttributeArg> xs);
ParseElemResult<AttributeItem> ParseAttributeItem(Parser parser);
ParseElemResult<AttributeList> ParseAttrList(Parser parser);
ParseElemResult<AttributeList> ParseAttrListTail(Parser parser, AttributeList xs);
ParseElemResult<std::vector<AttributeItem>> ParseAttrSpecListTail(
    Parser parser, std::vector<AttributeItem> xs);
ParseElemResult<std::vector<AttributeItem>> ParseAttrSpecList(Parser parser);

struct ParsedAttrName {
  AttrName name;
};

ParseElemResult<std::vector<Identifier>> ParseVendorPrefixTail(
    Parser parser, std::vector<Identifier> xs);
ParseElemResult<ParsedAttrName> ParseAttrName(Parser parser);

static bool RequiresAttrArgList(const AttrName& name) {
  // §9.3.1 gives the built-in layout attribute a concrete form:
  // [[layout(layout_args)]]. Vendor-qualified names remain schema-defined.
  return !name.vendor_prefix_opt.has_value() && name.leaf_name == "layout";
}

static bool UsesInlineArgGrammar(const AttrName& name) {
  return !name.vendor_prefix_opt.has_value() && name.leaf_name == "inline";
}

static bool UsesBareMarkerAttrGrammar(const AttrName& name) {
  return !name.vendor_prefix_opt.has_value() && name.leaf_name == "cold";
}

static bool IsLayoutIntTypeLexeme(std::string_view lexeme) {
  return lexeme == "i8" || lexeme == "i16" || lexeme == "i32" ||
         lexeme == "i64" || lexeme == "u8" || lexeme == "u16" ||
         lexeme == "u32" || lexeme == "u64";
}

static bool IsInlineModeLexeme(std::string_view lexeme) {
  return lexeme == "always" || lexeme == "never" || lexeme == "default";
}

ParseElemResult<Identifier> ParseAttrIdentSegment(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && tok->kind == TokenKind::Identifier) {
    Identifier name = tok->lexeme;
    Advance(parser);
    return {parser, name};
  }

  EmitAttrSyntaxErr(parser, TokSpan(parser));
  return {parser, Identifier{"_"}};
}

ParseElemResult<Identifier> ParseAttrLeafSegment(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && (tok->kind == TokenKind::Identifier ||
              tok->kind == TokenKind::Keyword)) {
    Identifier name = tok->lexeme;
    Advance(parser);
    return {parser, name};
  }

  EmitAttrSyntaxErr(parser, TokSpan(parser));
  return {parser, Identifier{"_"}};
}

// =============================================================================
// ParseAttrArg - Parse single attribute argument
// =============================================================================

ParseElemResult<AttributeArg> ParseAttrArg(Parser parser) {
  AttributeArg arg;
  SkipNewlines(parser);
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, arg};
  }

  // identifier ":" literal | identifier ":" identifier
  if (tok->kind == TokenKind::Identifier) {
    const Token ident_tok = *tok;
    ParseElemResult<Identifier> name = ParseIdent(parser);
    Parser after_name = name.parser;

    if (IsPunc(after_name, ":")) {
      Parser after_colon = after_name;
      Advance(after_colon);
      const Token* value = Tok(after_colon);
      if (!value) {
        EmitAttrSyntaxErr(after_colon, TokSpan(after_colon));
        return {after_colon, arg};
      }

      if (IsLiteralToken(*value)) {
        SPEC_RULE("Parse-AttrArg-Named-Literal");
        arg.key = name.elem;
        arg.value = *value;
        Advance(after_colon);
        return {after_colon, arg};
      }

      if (value->kind == TokenKind::Identifier) {
        SPEC_RULE("Parse-AttrArg-Named-Ident");
        arg.key = name.elem;
        arg.value = *value;
        Advance(after_colon);
        return {after_colon, arg};
      }

      EmitAttrSyntaxErr(after_colon, TokSpan(after_colon));
      return {after_colon, arg};
    }

    if (IsPunc(after_name, "(")) {
      SPEC_RULE("Parse-AttrArg-Named-Call");
      Parser after_open = after_name;
      Advance(after_open);
      ParseElemResult<std::vector<AttributeArg>> nested =
          ParseAttrArgList(after_open);
      Parser after_args = nested.parser;
      if (!IsPunc(after_args, ")")) {
        EmitAttrSyntaxErr(after_args, TokSpan(after_args));
        return {after_args, arg};
      }
      arg.key = name.elem;
      arg.value = std::move(nested.elem);
      Advance(after_args);
      return {after_args, arg};
    }

    SPEC_RULE("Parse-AttrArg-Ident");
    arg.value = ident_tok;
    return {after_name, arg};
  }

  if (IsLiteralToken(*tok)) {
    SPEC_RULE("Parse-AttrArg-Literal");
    Parser after = parser;
    Advance(after);
    arg.value = *tok;
    return {after, arg};
  }

  EmitAttrSyntaxErr(parser, TokSpan(parser));
  return {parser, arg};
}

// =============================================================================
// ParseAttrArgList - Parse list of attribute arguments
// =============================================================================

ParseElemResult<std::vector<AttributeArg>> ParseAttrArgList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }
  ParseElemResult<AttributeArg> first = ParseAttrArg(parser);
  SPEC_RULE("Parse-AttrArgList-Cons");
  std::vector<AttributeArg> xs;
  xs.push_back(first.elem);
  return ParseAttrArgListTail(first.parser, std::move(xs));
}

ParseElemResult<std::vector<AttributeArg>> ParseInlineArgList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  const Token* tok = Tok(parser);
  if (!tok ||
      (tok->kind != TokenKind::Identifier && tok->kind != TokenKind::Keyword) ||
      !IsInlineModeLexeme(tok->lexeme)) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  AttributeArg arg;
  arg.value = *tok;

  Parser after = parser;
  Advance(after);
  SkipNewlines(after);
  if (!IsPunc(after, ")")) {
    EmitAttrSyntaxErr(after, TokSpan(after));
    return {after, {}};
  }

  return {after, {arg}};
}

ParseElemResult<AttributeArg> ParseLayoutArg(Parser parser) {
  AttributeArg arg;
  SkipNewlines(parser);
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, arg};
  }

  if (tok->kind != TokenKind::Identifier && tok->kind != TokenKind::Keyword) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, arg};
  }

  if (tok->lexeme == "align") {
    Parser after_name = parser;
    Advance(after_name);
    SkipNewlines(after_name);
    if (!IsPunc(after_name, "(")) {
      EmitAttrSyntaxErr(after_name, TokSpan(after_name));
      return {after_name, arg};
    }

    Parser after_open = after_name;
    Advance(after_open);
    SkipNewlines(after_open);
    const Token* value = Tok(after_open);
    if (!value || value->kind != TokenKind::IntLiteral) {
      EmitAttrSyntaxErr(after_open, TokSpan(after_open));
      return {after_open, arg};
    }

    AttributeArg nested;
    nested.value = *value;

    Parser after_value = after_open;
    Advance(after_value);
    SkipNewlines(after_value);
    if (!IsPunc(after_value, ")")) {
      EmitAttrSyntaxErr(after_value, TokSpan(after_value));
      return {after_value, arg};
    }

    Parser after_close = after_value;
    Advance(after_close);
    arg.key = Identifier{"align"};
    arg.value = std::vector<AttributeArg>{nested};
    return {after_close, arg};
  }

  if (tok->lexeme == "C" || tok->lexeme == "packed" ||
      IsLayoutIntTypeLexeme(tok->lexeme)) {
    Parser after = parser;
    Advance(after);
    arg.value = *tok;
    return {after, arg};
  }

  EmitAttrSyntaxErr(parser, TokSpan(parser));
  return {parser, arg};
}

ParseElemResult<std::vector<AttributeArg>> ParseLayoutArgList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }

  ParseElemResult<AttributeArg> first = ParseLayoutArg(parser);
  std::vector<AttributeArg> xs;
  xs.push_back(first.elem);
  return ParseLayoutArgListTail(first.parser, std::move(xs));
}

ParseElemResult<std::vector<AttributeArg>> ParseLayoutArgListTail(
    Parser parser, std::vector<AttributeArg> xs) {
  SkipNewlines(parser);
  if (!IsPunc(parser, ",")) {
    return {parser, xs};
  }

  const EndSetToken end_set[] = {EndPunct(")")};
  Parser after = parser;
  Advance(after);
  SkipNewlines(after);
  if (IsPunc(after, ")")) {
    EmitTrailingCommaErr(parser, end_set);
    after.diags = parser.diags;
    return {after, xs};
  }

  ParseElemResult<AttributeArg> arg = ParseLayoutArg(after);
  xs.push_back(arg.elem);
  return ParseLayoutArgListTail(arg.parser, std::move(xs));
}

// =============================================================================
// ParseAttrArgListTail - Parse remaining attribute args after first
// =============================================================================

ParseElemResult<std::vector<AttributeArg>> ParseAttrArgListTail(
    Parser parser, std::vector<AttributeArg> xs) {
  SkipNewlines(parser);
  if (!IsPunc(parser, ",")) {
    SPEC_RULE("Parse-AttrArgListTail-End");
    return {parser, xs};
  }
  const EndSetToken end_set[] = {EndPunct(")")};
  Parser after = parser;
  Advance(after);
  SkipNewlines(after);
  if (IsPunc(after, ")")) {
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-AttrArgListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after.diags = parser.diags;
    return {after, xs};
  }
  SPEC_RULE("Parse-AttrArgListTail-Comma");
  ParseElemResult<AttributeArg> arg = ParseAttrArg(after);
  xs.push_back(arg.elem);
  return ParseAttrArgListTail(arg.parser, std::move(xs));
}

// =============================================================================
// ParseAttrSpecList - Parse list of attribute specs in a [[...]] block
// =============================================================================

ParseElemResult<std::vector<AttributeItem>> ParseAttrSpecList(Parser parser) {
  SkipNewlines(parser);
  ParseElemResult<AttributeItem> first = ParseAttributeItem(parser);
  SPEC_RULE("Parse-AttrSpecList-Cons");
  std::vector<AttributeItem> xs;
  xs.push_back(first.elem);
  return ParseAttrSpecListTail(first.parser, std::move(xs));
}

// =============================================================================
// ParseAttrSpecListTail - Parse remaining attribute specs after first
// =============================================================================

ParseElemResult<std::vector<AttributeItem>> ParseAttrSpecListTail(
    Parser parser, std::vector<AttributeItem> xs) {
  SkipNewlines(parser);
  if (!IsPunc(parser, ",")) {
    SPEC_RULE("Parse-AttrSpecListTail-End");
    return {parser, xs};
  }

  const EndSetToken end_set[] = {EndPunct("]]")};
  Parser after = parser;
  Advance(after);
  SkipNewlines(after);
  if (IsPunc(after, "]]")) {
    if (TrailingCommaAllowed(parser, end_set)) {
      SPEC_RULE("Parse-AttrSpecListTail-TrailingComma");
    }
    EmitTrailingCommaErr(parser, end_set);
    after.diags = parser.diags;
    return {after, xs};
  }

  SPEC_RULE("Parse-AttrSpecListTail-Comma");
  ParseElemResult<AttributeItem> item = ParseAttributeItem(after);
  xs.push_back(item.elem);
  return ParseAttrSpecListTail(item.parser, std::move(xs));
}

ParseElemResult<std::vector<Identifier>> ParseVendorPrefixTail(
    Parser parser, std::vector<Identifier> xs) {
  if (!IsOp(parser, "::")) {
    SPEC_RULE("Parse-VendorPrefixTail-End");
    return {parser, xs};
  }

  Parser after_colons = parser;
  Advance(after_colons);
  if (!Tok(after_colons) || Tok(after_colons)->kind != TokenKind::Identifier) {
    SPEC_RULE("Parse-VendorPrefixTail-End");
    return {parser, xs};
  }

  ParseElemResult<Identifier> seg = ParseAttrIdentSegment(after_colons);
  if (!IsOp(seg.parser, "::")) {
    SPEC_RULE("Parse-VendorPrefixTail-End");
    return {parser, xs};
  }

  SPEC_RULE("Parse-VendorPrefixTail-Cons");
  xs.push_back(seg.elem);
  return ParseVendorPrefixTail(seg.parser, std::move(xs));
}

ParseElemResult<ParsedAttrName> ParseAttrName(Parser parser) {
  const Token* first_tok = Tok(parser);
  ParseElemResult<Identifier> first = ParseAttrLeafSegment(parser);
  if (!IsPunc(first.parser, ".") && !IsOp(first.parser, "::")) {
    SPEC_RULE("Parse-AttrName-Plain");
    ParsedAttrName out;
    out.name.leaf_name = first.elem;
    out.name.full_name = first.elem;
    return {first.parser, std::move(out)};
  }

  if (IsPunc(first.parser, ".")) {
    EmitAttrSyntaxErr(first.parser, TokSpan(first.parser));
    ParsedAttrName out;
    out.name.leaf_name = first.elem;
    out.name.full_name = first.elem;
    return {first.parser, std::move(out)};
  }

  if (!first_tok || first_tok->kind != TokenKind::Identifier) {
    EmitAttrSyntaxErr(first.parser, TokSpan(first.parser));
    ParsedAttrName out;
    out.name.leaf_name = first.elem;
    out.name.full_name = first.elem;
    return {first.parser, std::move(out)};
  }

  SPEC_RULE("Parse-AttrName-Vendor");
  ParseElemResult<std::vector<Identifier>> pref =
      ParseVendorPrefixTail(first.parser, {first.elem});
  if (!IsOp(pref.parser, "::")) {
    EmitAttrSyntaxErr(pref.parser, TokSpan(pref.parser));
    ParsedAttrName out;
    out.name.leaf_name = first.elem;
    out.name.full_name = first.elem;
    return {pref.parser, std::move(out)};
  }

  Parser after_colons = pref.parser;
  Advance(after_colons);
  ParseElemResult<Identifier> leaf = ParseAttrLeafSegment(after_colons);

  ParsedAttrName out;
  out.name.vendor_prefix_opt = pref.elem;
  out.name.leaf_name = leaf.elem;
  out.name.full_name = pref.elem.front();
  for (std::size_t i = 1; i < pref.elem.size(); ++i) {
    out.name.full_name.append("::");
    out.name.full_name.append(pref.elem[i]);
  }
  out.name.full_name.append("::");
  out.name.full_name.append(leaf.elem);
  return {leaf.parser, std::move(out)};
}

// =============================================================================
// ParseAttributeItem - Parse single attribute item
// =============================================================================
//
// Parses: attr_name or attr_name(args)
// Attribute names can be keywords (e.g., "dynamic") or identifiers.

ParseElemResult<AttributeItem> ParseAttributeItem(Parser parser) {
  AttributeItem item;
  SPEC_RULE("Parse-AttrSpec");
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, item};
  }

  if (tok->kind != TokenKind::Identifier && tok->kind != TokenKind::Keyword) {
    EmitAttrSyntaxErr(parser, TokSpan(parser));
    return {parser, item};
  }

  ParseElemResult<ParsedAttrName> name = ParseAttrName(parser);
  Parser next = name.parser;
  item.name = std::move(name.elem.name);

  if (!IsPunc(next, "(")) {
    if (RequiresAttrArgList(item.name)) {
      item.span = SpanBetween(parser, next);
      EmitAttrSyntaxErr(next, item.span);
      return {next, item};
    }
    SPEC_RULE("Parse-AttrArgsOpt-None");
    item.span = SpanBetween(parser, next);
    return {next, item};
  }

  if (UsesBareMarkerAttrGrammar(item.name)) {
    Parser after_open = next;
    Advance(after_open);
    SkipNewlines(after_open);
    item.span = SpanBetween(parser, after_open);
    EmitAttrSyntaxErr(after_open, item.span);
    return {after_open, item};
  }

  SPEC_RULE("Parse-AttrArgsOpt-Yes");
  Parser after_open = next;
  Advance(after_open);
  ParseElemResult<std::vector<AttributeArg>> args;
  if (RequiresAttrArgList(item.name)) {
    args = ParseLayoutArgList(after_open);
  } else if (UsesInlineArgGrammar(item.name)) {
    args = ParseInlineArgList(after_open);
    if (args.elem.empty()) {
      item.span = SpanBetween(parser, args.parser);
      return {args.parser, item};
    }
  } else {
    args = ParseAttrArgList(after_open);
  }
  Parser after_args = args.parser;
  if (!IsPunc(after_args, ")")) {
    EmitAttrSyntaxErr(after_args, TokSpan(after_args));
    item.span = SpanBetween(parser, after_args);
    return {after_args, item};
  }
  Parser after_close = after_args;
  Advance(after_close);
  item.args = std::move(args.elem);
  next = after_close;

  item.span = SpanBetween(parser, next);
  return {next, item};
}

// =============================================================================
// IsAttrStart - Check if current position starts an attribute list
// =============================================================================

bool IsAttrStart(const Parser& parser) {
  return IsPunc(parser, "[[");
}

ParseElemResult<std::vector<AttributeItem>> ParseAttrBlock(Parser parser) {
  SPEC_RULE("Parse-AttrBlock");
  Parser next = parser;
  Advance(next);  // consume [[
  ParseElemResult<std::vector<AttributeItem>> specs = ParseAttrSpecList(next);
  if (!IsPunc(specs.parser, "]]")) {
    EmitAttrSyntaxErr(specs.parser, TokSpan(specs.parser));
    return specs;
  }
  Parser after = specs.parser;
  Advance(after);
  return {after, std::move(specs.elem)};
}

ParseElemResult<AttributeList> ParseAttrListTail(Parser parser, AttributeList xs) {
  if (!IsAttrStart(parser)) {
    SPEC_RULE("Parse-AttrListTail-End");
    return {parser, xs};
  }
  SPEC_RULE("Parse-AttrListTail-Cons");
  ParseElemResult<std::vector<AttributeItem>> block = ParseAttrBlock(parser);
  xs.insert(xs.end(), block.elem.begin(), block.elem.end());
  return ParseAttrListTail(block.parser, std::move(xs));
}

ParseElemResult<AttributeList> ParseAttrList(Parser parser) {
  SPEC_RULE("Parse-AttrList-Cons");
  ParseElemResult<std::vector<AttributeItem>> block = ParseAttrBlock(parser);
  AttributeList attrs = std::move(block.elem);
  return ParseAttrListTail(block.parser, std::move(attrs));
}

}  // namespace

// =============================================================================
// ParseAttributeListOpt - Parse optional attribute list
// =============================================================================
//
// SPEC: Parse-Attribute (lines 6437-6440)
//   IsAttrStart(P)    Γ ⊢ ParseAttrItem(Advance(Advance(P))) ⇓ (P_1, item)
//   Γ ⊢ ParseAttrItemTail(P_1, [item]) ⇓ (P_2, items)
//   IsOp(Tok(P_2), "]]")
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (Advance(P_2), items)
//
// SPEC: Parse-AttrListOpt-None
//   ¬ IsAttrStart(P)
//   ──────────────────────────────────────────
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P, ⊥)

ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser) {
  if (!IsAttrStart(parser)) {
    SPEC_RULE("Parse-AttrListOpt-None");
    return {parser, std::nullopt};
  }

  SPEC_RULE("Parse-AttrListOpt-Yes");
  ParseElemResult<AttributeList> attrs = ParseAttrList(parser);
  return {attrs.parser, std::move(attrs.elem)};
}

}  // namespace cursive::ast
