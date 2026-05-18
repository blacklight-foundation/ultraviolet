// =============================================================================
// pattern_common.cpp - Pattern Parsing Core
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.9 (Pattern Rules)
//
// This file implements the core pattern parsing infrastructure:
//   - MakePattern: Create pattern node with span
//   - IsLiteralToken, IsPatternStart: Token classification
//   - ParsePatternList/ListTail: Pattern list parsing
//   - ParseTuplePatternElems: Tuple element parsing
//   - ParseFieldPattern/List: Field pattern parsing
//   - ParsePatternAtom: Main pattern dispatch
//   - ParsePatternRange: Range pattern wrapper
//   - ParsePattern: Main entry point
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;
using ultraviolet::lexer::IsIdentTok;

// Forward declarations for helper functions
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void SkipNewlines(Parser& parser);

// Forward declarations for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<TypePath> ParseTypePath(Parser parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseLocalIdentResult ParseLocalIdent(Parser parser);

// Forward declaration for qualified names
ParseQualifiedHeadResult ParseQualifiedHead(Parser parser);

// =============================================================================
// Helper Functions
// =============================================================================

static bool IsLiteralToken(const Token& tok) {
  return tok.kind == TokenKind::IntLiteral ||
         tok.kind == TokenKind::FloatLiteral ||
         tok.kind == TokenKind::StringLiteral ||
         tok.kind == TokenKind::CharLiteral ||
         tok.kind == TokenKind::BoolLiteral ||
         tok.kind == TokenKind::NullLiteral;
}

static bool IsIdentifierSlotToken(const Token& tok) {
  return IsIdentTok(tok) || tok.kind == TokenKind::Keyword;
}

PatternPtr MakePattern(const core::Span& span, PatternNode node);

std::optional<ParseElemResult<PatternPtr>> TryParseSplicePattern(Parser parser) {
  if (!IsOp(parser, "$")) {
    return std::nullopt;
  }
  if (!parser.quote_mode) {
    Parser after_dollar = parser;
    Advance(after_dollar);
    if (IsPunc(after_dollar, "(")) {
      EmitSpliceOutsideQuoteErr(after_dollar, SpanBetween(parser, after_dollar));
      SyncStmt(after_dollar);
      return ParseElemResult<PatternPtr>{
          after_dollar,
          MakePattern(SpanBetween(parser, after_dollar), WildcardPattern{})};
    }
    return std::nullopt;
  }
  Parser after_dollar = parser;
  Advance(after_dollar);
  if (!IsPunc(after_dollar, "(")) {
    return std::nullopt;
  }

  Parser after_l = after_dollar;
  Advance(after_l);
  Parser inner = after_l;
  inner.quote_mode = false;
  ParseElemResult<ExprPtr> expr = ParseExpr(inner);
  Parser after_expr = expr.parser;
  after_expr.quote_mode = parser.quote_mode;
  if (!IsPunc(after_expr, ")")) {
    EmitParseSyntaxErr(after_expr, TokSpan(after_expr));
    return ParseElemResult<PatternPtr>{
        after_expr,
        MakePattern(SpanBetween(parser, after_expr), WildcardPattern{})};
  }

  Parser after_r = after_expr;
  Advance(after_r);
  return ParseElemResult<PatternPtr>{
      after_r,
      MakePattern(SpanBetween(parser, after_r),
                  SpliceExprNode{expr.elem, SpanBetween(parser, after_r)})};
}

bool IsPatternStart(const Token& tok) {
  if (IsLiteralToken(tok) || IsIdentifierSlotToken(tok)) {
    return true;
  }
  if (tok.kind == TokenKind::Punctuator) {
    return tok.lexeme == "(";
  }
  return tok.kind == TokenKind::Operator && tok.lexeme == "@";
}

PatternPtr MakePattern(const core::Span& span, PatternNode node) {
  auto pat = std::make_shared<Pattern>();
  pat->span = span;
  pat->node = std::move(node);
  return pat;
}

// =============================================================================
// Result Types
// =============================================================================

struct EnumPayloadOptResult {
  Parser parser;
  std::optional<EnumPayloadPattern> payload_opt;
};

struct ModalPayloadOptResult {
  Parser parser;
  std::optional<ModalRecordPayload> fields_opt;
};

struct FieldPatternTailOptResult {
  Parser parser;
  PatternPtr pattern_opt;
};

// =============================================================================
// Forward Declarations
// =============================================================================

ParseElemResult<PatternPtr> ParsePatternRange(Parser parser);
ParseElemResult<PatternPtr> ParsePatternAtom(Parser parser);
ParseElemResult<std::vector<PatternPtr>> ParsePatternListTail(
    Parser parser, std::vector<PatternPtr> xs);
ParseElemResult<FieldPattern> ParseFieldPattern(Parser parser);
ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternTail(
    Parser parser, std::vector<FieldPattern> xs);

// =============================================================================
// ParsePatternList - Parse comma-separated pattern list
// =============================================================================

ParseElemResult<std::vector<PatternPtr>> ParsePatternList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-PatternList-Empty");
    return {parser, {}};
  }
  SPEC_RULE("Parse-PatternList-Cons");
  ParseElemResult<PatternPtr> first = ParsePattern(parser);
  std::vector<PatternPtr> elems;
  elems.push_back(first.elem);
  return ParsePatternListTail(first.parser, std::move(elems));
}

// =============================================================================
// ParsePatternListTail - Parse remaining patterns after first
// =============================================================================

ParseElemResult<std::vector<PatternPtr>> ParsePatternListTail(
    Parser parser, std::vector<PatternPtr> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-PatternListTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    const EndSetToken end_set[] = {EndPunct(")")};
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, ")")) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-PatternListTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }
    SPEC_RULE("Parse-PatternListTail-Comma");
    ParseElemResult<PatternPtr> elem = ParsePattern(after);
    xs.push_back(elem.elem);
    return ParsePatternListTail(elem.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseTuplePatternElems - Parse tuple pattern elements
// =============================================================================
//
// Handles:
//   - () - empty tuple
//   - (p;) - single-element tuple
//   - (p1, p2, ...) - multi-element tuple

ParseElemResult<std::vector<PatternPtr>> ParseTuplePatternElems(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-TuplePatternElems-Empty");
    return {parser, {}};
  }
  ParseElemResult<PatternPtr> first = ParsePattern(parser);
  Parser after_first = first.parser;
  SkipNewlines(after_first);
  if (IsPunc(after_first, ";")) {
    SPEC_RULE("Parse-TuplePatternElems-Single");
    Parser after = after_first;
    Advance(after);
    return {after, {first.elem}};
  }
  if (IsPunc(after_first, ",")) {
    Parser after = after_first;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, ")")) {
      EmitParseSyntaxErr(after_first, TokSpan(after_first));
      return {after, {first.elem}};
    }
    SPEC_RULE("Parse-TuplePatternElems-Many");
    ParseElemResult<PatternPtr> second = ParsePattern(after);
    ParseElemResult<std::vector<PatternPtr>> tail =
        ParsePatternListTail(second.parser, {second.elem});
    std::vector<PatternPtr> elems;
    elems.reserve(1 + tail.elem.size());
    elems.push_back(first.elem);
    elems.insert(elems.end(), tail.elem.begin(), tail.elem.end());
    return {tail.parser, elems};
  }
  EmitParseSyntaxErr(after_first, TokSpan(after_first));
  return {after_first, {first.elem}};
}

// =============================================================================
// ParseEnumPayloadPatternElems - Parse enum tuple-payload elements
// =============================================================================
//
// Handles:
//   - () - empty payload
//   - (p) - single-element payload
//   - (p1, p2, ...) - multi-element payload
//
// Unlike tuple patterns, enum payload patterns do not use the single-element
// tuple disambiguation marker `;`.

ParseElemResult<std::vector<PatternPtr>> ParseEnumPayloadPatternElems(
    Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-EnumPayloadPatternElems-Empty");
    return {parser, {}};
  }

  ParseElemResult<PatternPtr> first = ParsePattern(parser);
  Parser after_first = first.parser;
  SkipNewlines(after_first);

  if (IsPunc(after_first, ",")) {
    const EndSetToken end_set[] = {EndPunct(")")};
    Parser after = after_first;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, ")")) {
      if (TrailingCommaAllowed(after_first, end_set)) {
        SPEC_RULE("Parse-EnumPayloadPatternElems-TrailingComma");
      }
      EmitTrailingCommaErr(after_first, end_set);
      after.diags = after_first.diags;
      return {after, {first.elem}};
    }
    SPEC_RULE("Parse-EnumPayloadPatternElems-Many");
    ParseElemResult<PatternPtr> second = ParsePattern(after);
    ParseElemResult<std::vector<PatternPtr>> tail =
        ParsePatternListTail(second.parser, {second.elem});
    std::vector<PatternPtr> elems;
    elems.reserve(1 + tail.elem.size());
    elems.push_back(first.elem);
    elems.insert(elems.end(), tail.elem.begin(), tail.elem.end());
    return {tail.parser, elems};
  }

  SPEC_RULE("Parse-EnumPayloadPatternElems-One");
  return {after_first, {first.elem}};
}

// =============================================================================
// Field Pattern Parsing
// =============================================================================

FieldPatternTailOptResult ParseFieldPatternTailOpt(Parser parser) {
  if (!IsPunc(parser, ":")) {
    SPEC_RULE("Parse-FieldPatternTailOpt-None");
    return {parser, nullptr};
  }
  SPEC_RULE("Parse-FieldPatternTailOpt-Yes");
  Parser after = parser;
  Advance(after);
  ParseElemResult<PatternPtr> pat = ParsePattern(after);
  return {pat.parser, pat.elem};
}

ParseElemResult<FieldPattern> ParseFieldPattern(Parser parser) {
  SPEC_RULE("Parse-FieldPattern");
  Parser start = parser;
  ParseElemResult<Identifier> name = ParseIdent(parser);
  FieldPatternTailOptResult tail = ParseFieldPatternTailOpt(name.parser);
  FieldPattern field;
  field.name = name.elem;
  field.pattern_opt = tail.pattern_opt;
  field.span = SpanBetween(start, tail.parser);
  return {tail.parser, field};
}

ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternTail(
    Parser parser, std::vector<FieldPattern> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldPatternTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    const EndSetToken end_set[] = {EndPunct("}")};
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, "}")) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-FieldPatternTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }
    SPEC_RULE("Parse-FieldPatternTail-Comma");
    ParseElemResult<FieldPattern> field = ParseFieldPattern(after);
    xs.push_back(field.elem);
    return ParseFieldPatternTail(field.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldPatternList-Empty");
    return {parser, {}};
  }
  SPEC_RULE("Parse-FieldPatternList-Cons");
  ParseElemResult<FieldPattern> first = ParseFieldPattern(parser);
  std::vector<FieldPattern> fields;
  fields.push_back(first.elem);
  return ParseFieldPatternTail(first.parser, std::move(fields));
}

// =============================================================================
// Enum/Modal Payload Parsing
// =============================================================================

EnumPayloadOptResult ParseEnumPatternPayloadOpt(Parser parser) {
  if (!IsPunc(parser, "(") && !IsPunc(parser, "{")) {
    SPEC_RULE("Parse-EnumPatternPayloadOpt-None");
    return {parser, std::nullopt};
  }
  if (IsPunc(parser, "(")) {
    SPEC_RULE("Parse-EnumPatternPayloadOpt-Tuple");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::vector<PatternPtr>> elems =
        ParseEnumPayloadPatternElems(next);
    if (!IsPunc(elems.parser, ")")) {
      EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
      return {elems.parser, std::nullopt};
    }
    Parser after = elems.parser;
    Advance(after);
    TuplePayloadPattern payload;
    payload.elements = std::move(elems.elem);
    return {after, EnumPayloadPattern{std::move(payload)}};
  }
  SPEC_RULE("Parse-EnumPatternPayloadOpt-Record");
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(next);
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    return {fields.parser, std::nullopt};
  }
  Parser after = fields.parser;
  Advance(after);
  RecordPayloadPattern payload;
  payload.fields = std::move(fields.elem);
  return {after, EnumPayloadPattern{std::move(payload)}};
}

ModalPayloadOptResult ParseModalPatternPayloadOpt(Parser parser) {
  if (!IsPunc(parser, "{")) {
    SPEC_RULE("Parse-ModalPatternPayloadOpt-None");
    return {parser, std::nullopt};
  }
  SPEC_RULE("Parse-ModalPatternPayloadOpt-Record");
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(next);
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    return {fields.parser, std::nullopt};
  }
  Parser after = fields.parser;
  Advance(after);
  ModalRecordPayload payload;
  payload.fields = std::move(fields.elem);
  return {after, payload};
}

// =============================================================================
// ParsePatternAtom - Main pattern dispatch
// =============================================================================
//
// SPEC: Parse-PatternAtom
// Checks token type and delegates to specific pattern parsers.
// Order is critical for disambiguation:
//   1. Literal patterns
//   2. TypedPattern (identifier + ":")
//   3. WildcardPattern ("_" alone)
//   4. EnumPattern (identifier + "::")
//   5. TuplePattern ("(")
//   6. RecordPattern (path + "{")
//   7. ModalPattern ("@")
//   8. IdentifierPattern (fallback)

ParseElemResult<PatternPtr> ParsePatternAtom(Parser parser) {
  if (auto splice = TryParseSplicePattern(parser)) {
    return *splice;
  }

  if (parser.quote_mode && IsOp(parser, "$")) {
    ParseLocalIdentResult name = ParseLocalIdent(parser);
    if (IsPunc(name.parser, ":")) {
      SPEC_RULE("Parse-Pattern-Typed");
      Parser after = name.parser;
      Advance(after);
      ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after);
      TypedPattern pat;
      pat.name = std::move(name.name);
      pat.type = ty.elem;
      pat.name_splice_opt = std::move(name.splice_opt);
      return {ty.parser, MakePattern(SpanBetween(parser, ty.parser), pat)};
    }

    SPEC_RULE("Parse-Pattern-Identifier");
    IdentifierPattern pat;
    pat.name = std::move(name.name);
    pat.name_splice_opt = std::move(name.splice_opt);
    return {name.parser, MakePattern(SpanBetween(parser, name.parser), pat)};
  }

  const Token* tok = Tok(parser);
  if (!tok) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakePattern(TokSpan(parser), WildcardPattern{})};
  }

  // 1. Literal patterns
  if (IsLiteralToken(*tok)) {
    SPEC_RULE("Parse-Pattern-Literal");
    Parser next = parser;
    Advance(next);
    LiteralPattern lit;
    lit.literal = *tok;
    return {next, MakePattern(tok->span, lit)};
  }

  // 2. Check typed pattern BEFORE wildcard - lookahead for ":" takes precedence
  // This allows `_: Type` to parse as TypedPattern rather than WildcardPattern
  if (IsIdentifierSlotToken(*tok)) {
    Parser next = parser;
    Advance(next);
    if (IsPunc(next, ":")) {
      SPEC_RULE("Parse-Pattern-Typed");
      ParseElemResult<Identifier> name = ParseIdent(parser);
      Parser after = name.parser;
      Advance(after);
      ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after);
      TypedPattern pat;
      pat.name = name.elem;
      pat.type = ty.elem;
      pat.name_splice_opt = std::nullopt;
      return {ty.parser, MakePattern(SpanBetween(parser, ty.parser), pat)};
    }
  }

  // 3. Wildcard pattern: `_` not followed by `:`
  if (IsIdentTok(*tok) && tok->lexeme == "_") {
    SPEC_RULE("Parse-Pattern-Wildcard");
    Parser next = parser;
    Advance(next);
    return {next, MakePattern(tok->span, WildcardPattern{})};
  }

  // 4. Enum pattern (identifier + "::")
  if (IsIdentifierSlotToken(*tok)) {
    Parser next = parser;
    Advance(next);
    if (IsOp(next, "::")) {
      SPEC_RULE("Parse-Pattern-Enum");
      ParseQualifiedHeadResult head = ParseQualifiedHead(parser);
      EnumPayloadOptResult payload = ParseEnumPatternPayloadOpt(head.parser);
      EnumPattern pat;
      pat.path = head.module_path;
      pat.name = head.name;
      pat.payload_opt = payload.payload_opt;
      return {payload.parser, MakePattern(SpanBetween(parser, payload.parser), pat)};
    }
  }

  // 5. Tuple pattern ("(")
  if (IsPunc(parser, "(")) {
    SPEC_RULE("Parse-Pattern-Tuple");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::vector<PatternPtr>> elems = ParseTuplePatternElems(next);
    if (!IsPunc(elems.parser, ")")) {
      EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
      return {elems.parser,
              MakePattern(SpanBetween(parser, elems.parser), WildcardPattern{})};
    }
    Parser after = elems.parser;
    Advance(after);
    TuplePattern pat;
    pat.elements = std::move(elems.elem);
    return {after, MakePattern(SpanBetween(parser, after), pat)};
  }

  // 6. Record pattern (path + "{")
  if (IsIdentifierSlotToken(*tok)) {
    Parser probe = Clone(parser);
    ParseElemResult<TypePath> path_probe = ParseTypePath(probe);
    if (IsPunc(path_probe.parser, "{")) {
      SPEC_RULE("Parse-Pattern-Record");
      Parser start = parser;
      ParseElemResult<TypePath> path = ParseTypePath(parser);
      Parser after = path.parser;
      Advance(after);
      ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(after);
      if (!IsPunc(fields.parser, "}")) {
        EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
        return {fields.parser,
                MakePattern(SpanBetween(start, fields.parser), WildcardPattern{})};
      }
      Parser done = fields.parser;
      Advance(done);
      RecordPattern pat;
      pat.path = path.elem;
      pat.fields = std::move(fields.elem);
      return {done, MakePattern(SpanBetween(start, done), pat)};
    }
  }

  // 7. Modal pattern ("@")
  if (IsOp(parser, "@")) {
    SPEC_RULE("Parse-Pattern-Modal");
    Parser next = parser;
    Advance(next);
    ParseElemResult<Identifier> name = ParseIdent(next);
    ModalPayloadOptResult payload = ParseModalPatternPayloadOpt(name.parser);
    ModalPattern pat;
    pat.state = name.elem;
    pat.fields_opt = payload.fields_opt;
    return {payload.parser, MakePattern(SpanBetween(parser, payload.parser), pat)};
  }

  // 8. Identifier pattern (fallback)
  if (IsIdentifierSlotToken(*tok)) {
    SPEC_RULE("Parse-Pattern-Identifier");
    ParseElemResult<Identifier> name = ParseIdent(parser);
    IdentifierPattern pat;
    pat.name = name.elem;
    pat.name_splice_opt = std::nullopt;
    return {name.parser, MakePattern(SpanBetween(parser, name.parser), pat)};
  }

  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, MakePattern(TokSpan(parser), WildcardPattern{})};
}

// =============================================================================
// ParsePatternRange - Handle range patterns
// =============================================================================
//
// SPEC: Parse-Pattern-Range
// Checks for `..` or `..=` operator after atom and creates RangePattern

ParseElemResult<PatternPtr> ParsePatternRange(Parser parser) {
  ParseElemResult<PatternPtr> lhs = ParsePatternAtom(parser);
  if (IsOp(lhs.parser, "..") || IsOp(lhs.parser, "..=")) {
    SPEC_RULE("Parse-Pattern-Range");
    Parser after = lhs.parser;
    const bool inclusive = IsOp(after, "..=");
    Advance(after);
    ParseElemResult<PatternPtr> rhs = ParsePatternAtom(after);
    RangePattern pat;
    pat.kind = inclusive ? RangeKind::Inclusive : RangeKind::Exclusive;
    pat.lo = lhs.elem;
    pat.hi = rhs.elem;
    return {rhs.parser, MakePattern(SpanBetween(parser, rhs.parser), pat)};
  }
  SPEC_RULE("Parse-Pattern-Range-None");
  return lhs;
}

// =============================================================================
// ParsePattern - Main entry point
// =============================================================================
//
// SPEC: Parse-Pattern
// Entry point for pattern parsing. Validates token is a valid pattern start.

ParseElemResult<PatternPtr> ParsePattern(Parser parser) {
  if (auto splice = TryParseSplicePattern(parser)) {
    SPEC_RULE("Parse-Pattern");
    return *splice;
  }

  if (parser.quote_mode && IsOp(parser, "$")) {
    SPEC_RULE("Parse-Pattern");
    return ParsePatternRange(parser);
  }

  const Token* tok = Tok(parser);
  if (!tok || !IsPatternStart(*tok)) {
    SPEC_RULE("Parse-Pattern-Err");
    EmitGenericParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakePattern(TokSpan(parser), WildcardPattern{})};
  }
  SPEC_RULE("Parse-Pattern");
  return ParsePatternRange(parser);
}

}  // namespace ultraviolet::ast
