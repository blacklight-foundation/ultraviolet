// =============================================================================
// type_common.cpp - Common Type Parsing Utilities and Main Entry Points
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.7, Lines 4628-4780
// Lines 4630-4631 (TypeStart predicate)
// Lines 4633-4641 (Parse-Type, Parse-Type-Err)
// Lines 4695-4780 (NonPermType parsing)
//
// This file provides:
// - Shared helper functions used by all type parsing modules
// - ParseNonPermType: main dispatcher for non-permission type variants
// - ParseType: main entry point for type parsing
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/span.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// Exported Helper Functions
// =============================================================================
// These functions are declared in type_parse_internal.h and used by all
// type parsing modules.

void SkipNewlinesType(Parser& parser) {
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }
}

std::shared_ptr<Type> MakeTypeNode(const core::Span& span, TypeNode node) {
  auto ty = std::make_shared<Type>();
  ty->span = span;
  ty->node = std::move(node);
  return ty;
}

std::shared_ptr<Type> MakeTypePrim(const core::Span& span,
                                   std::string_view name) {
  return MakeTypeNode(span, TypePrim{Identifier{name}});
}

bool IsOpType(const Parser& parser, std::string_view op) {
  const Token* tok = Tok(parser);
  return tok && IsOpTok(*tok, op);
}

bool IsPuncType(const Parser& parser, std::string_view punc) {
  const Token* tok = Tok(parser);
  return tok && IsPuncTok(*tok, punc);
}

bool IsKwType(const Parser& parser, std::string_view kw) {
  const Token* tok = Tok(parser);
  return tok && IsKwTok(*tok, kw);
}

// =============================================================================
// Local Helper Functions
// =============================================================================

namespace {

std::optional<ParseElemResult<std::shared_ptr<Type>>> TryParseSpliceType(
    Parser parser) {
  if (!IsOpType(parser, "$")) {
    return std::nullopt;
  }
  if (!parser.quote_mode) {
    Parser after_dollar = parser;
    Advance(after_dollar);
    if (IsPuncType(after_dollar, "(")) {
      EmitSpliceOutsideQuoteErr(after_dollar, SpanBetween(parser, after_dollar));
      SyncType(after_dollar);
      return ParseElemResult<std::shared_ptr<Type>>{
          after_dollar, MakeTypePrim(SpanBetween(parser, after_dollar), "!")};
    }
    return std::nullopt;
  }
  Parser after_dollar = parser;
  Advance(after_dollar);
  if (!IsPuncType(after_dollar, "(")) {
    return std::nullopt;
  }

  Parser after_l = after_dollar;
  Advance(after_l);
  Parser inner = after_l;
  inner.quote_mode = false;
  ParseElemResult<ExprPtr> expr = ParseExpr(inner);
  Parser after_expr = expr.parser;
  after_expr.quote_mode = parser.quote_mode;
  if (!IsPuncType(after_expr, ")")) {
    EmitParseSyntaxErr(after_expr, TokSpan(after_expr));
    return ParseElemResult<std::shared_ptr<Type>>{
        after_expr, MakeTypePrim(SpanBetween(parser, after_expr), "!")};
  }

  Parser after_r = after_expr;
  Advance(after_r);
  return ParseElemResult<std::shared_ptr<Type>>{
      after_r,
      MakeTypeNode(SpanBetween(parser, after_r),
                   SpliceExprNode{expr.elem, SpanBetween(parser, after_r)})};
}

bool TypeArgsUnsupported(const TypePath& path, const Parser& parser) {
  // Generic type arguments are supported on all types
  (void)path;
  (void)parser;
  return false;
}

bool C0TypeRestricted(const Token* tok, const TypePath& path,
                      const Parser& parser) {
  if (tok && OpaqueTypeTok(*tok)) {
    return true;
  }
  return TypeArgsUnsupported(path, parser);
}

std::shared_ptr<Type> BuildBuiltinRangeType(
    const Parser& start,
    const Parser& end,
    const TypePath& path,
    const std::vector<std::shared_ptr<Type>>& args) {
  if (path.size() != 1) {
    return nullptr;
  }

  const std::string_view name = path.front();
  auto with_base = [&](auto node_ctor) -> std::shared_ptr<Type> {
    if (args.size() != 1) {
      return nullptr;
    }
    return MakeTypeNode(SpanBetween(start, end), node_ctor(args.front()));
  };

  if (name == "Range") {
    return with_base([](const std::shared_ptr<Type>& base) {
      return TypeRange{base};
    });
  }
  if (name == "RangeInclusive") {
    return with_base([](const std::shared_ptr<Type>& base) {
      return TypeRangeInclusive{base};
    });
  }
  if (name == "RangeFrom") {
    return with_base([](const std::shared_ptr<Type>& base) {
      return TypeRangeFrom{base};
    });
  }
  if (name == "RangeTo") {
    return with_base([](const std::shared_ptr<Type>& base) {
      return TypeRangeTo{base};
    });
  }
  if (name == "RangeToInclusive") {
    return with_base([](const std::shared_ptr<Type>& base) {
      return TypeRangeToInclusive{base};
    });
  }
  if (name == "RangeFull") {
    if (!args.empty()) {
      return nullptr;
    }
    return MakeTypeNode(SpanBetween(start, end), TypeRangeFull{});
  }
  return nullptr;
}

}  // namespace

// =============================================================================
// ParseNonPermType - Main Type Dispatch
// =============================================================================
// SPEC: Lines 4674-4780
// Dispatches to appropriate type parser based on current token.

ParseElemResult<std::shared_ptr<Type>> ParseNonPermType(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakeTypePrim(TokSpan(parser), "!")};
  }

  // Closure type: |T| -> R or || -> R
  if (IsOpTok(*tok, "|") || IsOpTok(*tok, "||")) {
    return ParseClosureType(parser);
  }

  // Tuple or function type: (...)
  if (IsPuncTok(*tok, "(")) {
    if (HasFuncArrow(parser)) {
      return ParseFuncType(parser);
    }
    Parser after = parser;
    Advance(after);
    ParseElemResult<std::vector<std::shared_ptr<Type>>> elems =
        ParseTupleTypeElems(after);
    if (elems.elem.empty()) {
      if (!IsPuncType(elems.parser, ")")) {
        EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
        return {elems.parser,
                MakeTypePrim(SpanBetween(parser, elems.parser), "!")};
      }
      SPEC_RULE("Parse-Unit-Type");
      Parser after_r = elems.parser;
      Advance(after_r);
      return {after_r,
              MakeTypeNode(SpanBetween(parser, after_r),
                           TypePrim{Identifier{"()"}})};
    }
    if (!IsPuncType(elems.parser, ")")) {
      EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
      return {elems.parser,
              MakeTypePrim(SpanBetween(parser, elems.parser), "!")};
    }
    SPEC_RULE("Parse-Tuple-Type");
    Parser after_r = elems.parser;
    Advance(after_r);
    TypeTuple tuple;
    tuple.elements = std::move(elems.elem);
    return {after_r, MakeTypeNode(SpanBetween(parser, after_r), tuple)};
  }

  // Array or slice type: [T; n] or [T]
  if (IsPuncTok(*tok, "[")) {
    Parser start = parser;
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Type>> elem = ParseType(next);
    if (IsPuncType(elem.parser, ";")) {
      return ParseArrayType(elem.parser, start, elem.elem);
    }
    if (IsPuncType(elem.parser, "]")) {
      return ParseSliceType(elem.parser, start, elem.elem);
    }
    EmitParseSyntaxErr(elem.parser, TokSpan(elem.parser));
    return {elem.parser, MakeTypePrim(SpanBetween(start, elem.parser), "!")};
  }

  // Never type: !
  if (IsOpTok(*tok, "!")) {
    SPEC_RULE("Parse-Never-Type");
    Parser next = parser;
    Advance(next);
    return {next,
            MakeTypeNode(SpanBetween(parser, next),
                         TypePrim{Identifier{"!"}})};
  }

  // Raw pointer type: *imm T, *mut T
  if (IsOpTok(*tok, "*")) {
    return ParseRawPtrType(parser);
  }

  if (auto splice = TryParseSpliceType(parser)) {
    return *splice;
  }

  // Dynamic type: $ClassName
  if (IsOpTok(*tok, "$")) {
    return ParseDynamicType(parser);
  }

  // Identifier-based types
  if (IsIdentTok(*tok)) {
    // Opaque type: opaque Path
    if (OpaqueTypeTok(*tok)) {
      return ParseOpaqueType(parser);
    }

    const std::string_view lexeme = tok->lexeme;

    // Primitive type: i32, bool, char, etc.
    if (IsPrimLexemeSet(lexeme)) {
      SPEC_RULE("Parse-Prim-Type");
      Parser next = parser;
      Advance(next);
      return {next,
              MakeTypeNode(SpanBetween(parser, next),
                           TypePrim{Identifier{lexeme}})};
    }

    // String type: string@State
    if (lexeme == "string") {
      SPEC_RULE("Parse-String-Type");
      Parser start = parser;
      Parser next = parser;
      Advance(next);
      ParseElemResult<std::optional<StringState>> st = ParseStringState(next);
      TypeString str;
      str.state = st.elem;
      return {st.parser, MakeTypeNode(SpanBetween(start, st.parser), str)};
    }

    // Bytes type: bytes@State
    if (lexeme == "bytes") {
      SPEC_RULE("Parse-Bytes-Type");
      Parser start = parser;
      Parser next = parser;
      Advance(next);
      ParseElemResult<std::optional<BytesState>> st = ParseBytesState(next);
      TypeBytes bytes;
      bytes.state = st.elem;
      return {st.parser, MakeTypeNode(SpanBetween(start, st.parser), bytes)};
    }

    // Safe pointer type: Ptr<T>@State
    if (lexeme == "Ptr") {
      Parser after_ident = parser;
      Advance(after_ident);
      if (IsOpType(after_ident, "<")) {
        return ParseSafePointerType(parser);
      }
    }

    // Type path with optional generic args and modal state
    Parser start = parser;
    ParseElemResult<TypePath> path = ParseTypePath(parser);
    if (C0TypeRestricted(tok, path.elem, path.parser) &&
        TypeArgsUnsupported(path.elem, path.parser)) {
      SPEC_RULE("Parse-Type-Generic-Unsupported");
      Parser skip = SkipAngles(path.parser);
      SyncType(skip);
      return {skip,
              MakeTypeNode(SpanBetween(start, skip),
                           TypePathType{std::move(path.elem)})};
    }

    // Parse optional generic args
    ParseGenericArgsResult gen = ParseGenericArgsOpt(path.parser);
    std::vector<std::shared_ptr<Type>> generic_args =
        gen.args.has_value()
            ? std::move(*gen.args)
            : std::vector<std::shared_ptr<Type>>{};

    // Check for modal state: @StateName
    const Token* after_args = Tok(gen.parser);
    if (after_args && IsOpTok(*after_args, "@")) {
      return ParseModalStateType(gen.parser, start, std::move(path.elem),
                                 std::move(generic_args));
    }

    // Builtin range-family constructors are represented as dedicated AST nodes.
    if (auto builtin_range =
            BuildBuiltinRangeType(start, gen.parser, path.elem, generic_args)) {
      return {gen.parser, builtin_range};
    }

    if (gen.args.has_value()) {
      SPEC_RULE("Parse-Type-Apply");
      TypeApply apply;
      apply.path = std::move(path.elem);
      apply.args = std::move(generic_args);
      return {gen.parser, MakeTypeNode(SpanBetween(start, gen.parser), apply)};
    }

    // Regular type path
    SPEC_RULE("Parse-Type-Path");
    TypePathType ty_path;
    ty_path.path = std::move(path.elem);
    return {gen.parser, MakeTypeNode(SpanBetween(start, gen.parser), ty_path)};
  }

  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, MakeTypePrim(TokSpan(parser), "!")};
}

// =============================================================================
// ParseType - Main Type Parsing Entry Point
// =============================================================================
// SPEC: Lines 4633-4641 (Parse-Type, Parse-Type-Err)

ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser) {
  Parser start = parser;
  PermOptResult perm = ParsePermOpt(parser);
  Parser after_perm = perm.parser;
  const Token* tok = Tok(after_perm);

  // Error case: no valid type start (EOF or invalid token)
  if (!tok) {
    SPEC_RULE("Parse-Type-Err");
    EmitGenericParseSyntaxErr(after_perm, TokSpan(after_perm));
    std::shared_ptr<Type> base =
        MakeTypePrim(SpanBetween(start, after_perm), "!");
    if (perm.perm.has_value()) {
      TypePermType perm_type;
      perm_type.perm = *perm.perm;
      perm_type.base = base;
      return {after_perm,
              MakeTypeNode(SpanBetween(start, after_perm), perm_type)};
    }
    return {after_perm, base};
  }

  // Check for valid non-perm type start
  const bool non_perm_start = tok->kind == TokenKind::Identifier ||
                              (tok->kind == TokenKind::Punctuator &&
                               (tok->lexeme == "(" || tok->lexeme == "[")) ||
                              (tok->kind == TokenKind::Operator &&
                               (tok->lexeme == "*" || tok->lexeme == "$" ||
                                tok->lexeme == "!" || tok->lexeme == "|" ||
                                tok->lexeme == "||"));
  if (!non_perm_start) {
    SPEC_RULE("Parse-Type-Err");
    EmitGenericParseSyntaxErr(after_perm, TokSpan(after_perm));
    std::shared_ptr<Type> base =
        MakeTypePrim(SpanBetween(start, after_perm), "!");
    if (perm.perm.has_value()) {
      TypePermType perm_type;
      perm_type.perm = *perm.perm;
      perm_type.base = base;
      return {after_perm,
              MakeTypeNode(SpanBetween(start, after_perm), perm_type)};
    }
    return {after_perm, base};
  }

  // Parse base type
  ParseElemResult<std::shared_ptr<Type>> base = ParseNonPermType(after_perm);

  // Parse union tail
  ParseElemResult<std::vector<std::shared_ptr<Type>>> tail =
      ParseUnionTail(base.parser);

  Parser out = tail.parser;

  // Construct final type
  SPEC_RULE("Parse-Type");
  std::shared_ptr<Type> merged = base.elem;

  // Merge into union if tail is non-empty
  if (!tail.elem.empty()) {
    TypeUnion uni;
    uni.types.reserve(1 + tail.elem.size());
    uni.types.push_back(base.elem);
    uni.types.insert(uni.types.end(), tail.elem.begin(), tail.elem.end());
    merged = MakeTypeNode(SpanBetween(start, out), uni);
  }

  // Apply permission if present
  if (perm.perm.has_value()) {
    TypePermType perm_type;
    perm_type.perm = *perm.perm;
    perm_type.base = merged;
    merged = MakeTypeNode(SpanBetween(start, out), perm_type);
  }

  ParseRefinementResult refine_result = ParseRefinementClause(out, start, merged);
  return refine_result;
}

// =============================================================================
// ParseTypeNoUnion - Parse Type Without Union Tail
// =============================================================================
//
// Used in contexts where '|' is a delimiter (e.g., closure parameter lists).

ParseElemResult<std::shared_ptr<Type>> ParseTypeNoUnion(Parser parser) {
  Parser start = parser;
  PermOptResult perm = ParsePermOpt(parser);
  Parser after_perm = perm.parser;
  const Token* tok = Tok(after_perm);

  if (!tok) {
    SPEC_RULE("Parse-Type-Err");
    EmitGenericParseSyntaxErr(after_perm, TokSpan(after_perm));
    std::shared_ptr<Type> base =
        MakeTypePrim(SpanBetween(start, after_perm), "!");
    if (perm.perm.has_value()) {
      TypePermType perm_type;
      perm_type.perm = *perm.perm;
      perm_type.base = base;
      return {after_perm,
              MakeTypeNode(SpanBetween(start, after_perm), perm_type)};
    }
    return {after_perm, base};
  }

  const bool non_perm_start = tok->kind == TokenKind::Identifier ||
                              (tok->kind == TokenKind::Punctuator &&
                               (tok->lexeme == "(" || tok->lexeme == "[")) ||
                              (tok->kind == TokenKind::Operator &&
                               (tok->lexeme == "*" || tok->lexeme == "$" ||
                                tok->lexeme == "!" || tok->lexeme == "|" ||
                                tok->lexeme == "||"));
  if (!non_perm_start) {
    SPEC_RULE("Parse-Type-Err");
    EmitGenericParseSyntaxErr(after_perm, TokSpan(after_perm));
    std::shared_ptr<Type> base =
        MakeTypePrim(SpanBetween(start, after_perm), "!");
    if (perm.perm.has_value()) {
      TypePermType perm_type;
      perm_type.perm = *perm.perm;
      perm_type.base = base;
      return {after_perm,
              MakeTypeNode(SpanBetween(start, after_perm), perm_type)};
    }
    return {after_perm, base};
  }

  // Parse base type (no union tail)
  ParseElemResult<std::shared_ptr<Type>> base = ParseNonPermType(after_perm);
  Parser out = base.parser;

  // Emit Parse-UnionTail-None for spec trace consistency.
  ParseUnionTail(out, false);

  SPEC_RULE("Parse-Type");
  std::shared_ptr<Type> merged = base.elem;

  if (perm.perm.has_value()) {
    TypePermType perm_type;
    perm_type.perm = *perm.perm;
    perm_type.base = merged;
    merged = MakeTypeNode(SpanBetween(start, out), perm_type);
  }

  ParseRefinementResult refine_result = ParseRefinementClause(out, start, merged);
  return refine_result;
}

// =============================================================================
// ParseTypeAnnotOpt - Parse optional type annotation (: Type)
// =============================================================================
//
// SPEC: Parse-TypeAnnotOpt
// Checks for a colon followed by a type. If not present, returns nullptr.
// Used in variable declarations, function parameters, etc.

ParseElemResult<std::shared_ptr<Type>> ParseTypeAnnotOpt(Parser parser) {
  if (!IsPuncType(parser, ":")) {
    SPEC_RULE("Parse-TypeAnnotOpt-None");
    return {parser, nullptr};
  }
  SPEC_RULE("Parse-TypeAnnotOpt-Yes");
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
  return {ty.parser, ty.elem};
}

}  // namespace cursive::ast
