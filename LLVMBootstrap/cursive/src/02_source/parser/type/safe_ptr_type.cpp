// =============================================================================
// safe_ptr_type.cpp - Safe Pointer Type Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.7, Lines 4773-4781
// Section 3.3.7.1 (Pointer State), Lines 4915-4935
//
// Parses safe pointer types: Ptr<T>@State
// States: Valid, Null, Expired
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace cursive::ast {

// =============================================================================
// ParsePtrState - Parse Optional Pointer State Annotation
// =============================================================================
// SPEC: Lines 4917-4935

ParseElemResult<std::optional<PtrState>> ParsePtrState(Parser parser) {
  // Parse-PtrState-None: No @ annotation
  if (!IsOpType(parser, "@")) {
    SPEC_RULE("Parse-PtrState-None");
    return {parser, std::nullopt};
  }

  Parser next = parser;
  Advance(next);  // consume @
  const Token* tok = Tok(next);

  // Parse-PtrState-Valid
  if (tok && IsIdentTok(*tok) && tok->lexeme == "Valid") {
    SPEC_RULE("Parse-PtrState-Valid");
    Advance(next);
    return {next, PtrState::Valid};
  }

  // Parse-PtrState-Null
  if (tok && IsIdentTok(*tok) && tok->lexeme == "Null") {
    SPEC_RULE("Parse-PtrState-Null");
    Advance(next);
    return {next, PtrState::Null};
  }

  // Parse-PtrState-Expired
  if (tok && IsIdentTok(*tok) && tok->lexeme == "Expired") {
    SPEC_RULE("Parse-PtrState-Expired");
    Advance(next);
    return {next, PtrState::Expired};
  }

  // Error: Invalid pointer state
  EmitParseSyntaxErr(next, TokSpan(next));
  return {next, std::nullopt};
}

// =============================================================================
// ParseSafePointerType - Parse Complete Safe Pointer Type
// =============================================================================
// SPEC: Lines 4773-4781
// Syntax: Ptr<T>, Ptr<T>@Valid, Ptr<T>@Null, Ptr<T>@Expired

ParseElemResult<std::shared_ptr<Type>> ParseSafePointerType(Parser parser) {
  Parser start = parser;
  Parser after_ident = parser;
  Advance(after_ident);  // consume Ptr

  // Expect <
  if (!IsOpType(after_ident, "<")) {
    EmitParseSyntaxErr(after_ident, TokSpan(after_ident));
    return {after_ident, MakeTypePrim(SpanBetween(start, after_ident), "!")};
  }

  Parser after_lt = after_ident;
  Advance(after_lt);  // consume <

  // Parse element type
  ParseElemResult<std::shared_ptr<Type>> elem = ParseType(after_lt);
  const Token* close = Tok(elem.parser);

  // Handle >> case (nested generics like Ptr<Ptr<T>>)
  if (close && IsOpTok(*close, ">>")) {
    SPEC_RULE("Parse-Safe-Pointer-Type-ShiftSplit");
    Parser split = SplitShiftR(elem.parser);
    Parser after_left = split;
    Advance(after_left);

    // Parse optional state
    ParseElemResult<std::optional<PtrState>> st = ParsePtrState(after_left);

    TypeSafePtr ptr;
    ptr.element = elem.elem;
    ptr.state = st.elem;
    return {st.parser, MakeTypeNode(SpanBetween(start, st.parser), ptr)};
  }

  // Handle > case (normal closing)
  if (close && IsOpTok(*close, ">")) {
    SPEC_RULE("Parse-Safe-Pointer-Type");
    Parser after_gt = elem.parser;
    Advance(after_gt);  // consume >

    // Parse optional state
    ParseElemResult<std::optional<PtrState>> st = ParsePtrState(after_gt);

    TypeSafePtr ptr;
    ptr.element = elem.elem;
    ptr.state = st.elem;
    return {st.parser, MakeTypeNode(SpanBetween(start, st.parser), ptr)};
  }

  // Error: Expected > or >>
  EmitParseSyntaxErr(elem.parser, TokSpan(elem.parser));
  return {elem.parser, MakeTypePrim(SpanBetween(start, elem.parser), "!")};
}

}  // namespace cursive::ast
