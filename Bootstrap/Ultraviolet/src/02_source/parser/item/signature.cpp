// =============================================================================
// signature.cpp - Procedure Signature Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.6.13 (Signature Rules)
//
// This file implements signature parsing for procedures and methods:
//   - ParseReturnOpt: Parse optional return type (-> Type)
//   - ParseParamModeOpt: Parse optional parameter mode (move)
//   - ParseParam: Parse single parameter
//   - ParseParamList: Parse parameter list
//   - ParseReceiver: Parse method receiver (~, ~!, ~%, or explicit)
//   - ParseMethodParams: Parse parameters after receiver
//   - ParseSignature: Parse regular procedure signature
//   - ParseMethodSignature: Parse method signature with receiver
//
// =============================================================================

#include "02_source/parser/parser.h"

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

// Forward declaration for type parsing
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseLocalIdentResult ParseLocalIdent(Parser parser);

// Signature result types
struct SignatureResult {
  Parser parser;
  std::vector<Param> params;
  TypePtr return_type_opt;
};

struct MethodSignatureResult {
  Parser parser;
  Receiver receiver;
  std::vector<Param> params;
  TypePtr return_type_opt;
};

// Forward declaration for ParseSignature (used in class/state method parsing)
SignatureResult ParseSignature(Parser parser);

// =============================================================================
// ParseReturnOpt - Parse optional return type
// =============================================================================
//
// SPEC: Parse-ReturnOpt-None
//   ¬ IsOp(Tok(P), "->")
//   ──────────────────────────────────────────────
//   Γ ⊢ ParseReturnOpt(P) ⇓ (P, ⊥)
//
// SPEC: Parse-ReturnOpt-Arrow
//   IsOp(Tok(P), "->")    Γ ⊢ ParseType(Advance(P)) ⇓ (P_1, ty)
//   ────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseReturnOpt(P) ⇓ (P_1, ty)

ParseElemResult<std::shared_ptr<Type>> ParseReturnOpt(Parser parser) {
  if (!IsOp(parser, "->")) {
    SPEC_RULE("Parse-ReturnOpt-None");
    return {parser, nullptr};
  }
  SPEC_RULE("Parse-ReturnOpt-Arrow");
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(next);
  return {ty.parser, ty.elem};
}

// =============================================================================
// ParseParamModeOpt - Parse optional parameter mode
// =============================================================================
//
// SPEC: Parse-ParamMode-Move
// SPEC: Parse-ParamMode-None

ParseElemResult<std::optional<ParamMode>> ParseParamModeOpt(Parser parser) {
  if (IsKw(parser, "move")) {
    SPEC_RULE("Parse-ParamMode-Move");
    Parser next = parser;
    Advance(next);
    return {next, ParamMode::Move};
  }
  SPEC_RULE("Parse-ParamMode-None");
  return {parser, std::nullopt};
}

// =============================================================================
// ParseParam - Parse single parameter
// =============================================================================
//
// Parses: [move] name: Type

ParseElemResult<Param> ParseParam(Parser parser) {
  SPEC_RULE("Parse-Param");
  Parser start = parser;
  ParseElemResult<std::optional<ParamMode>> mode = ParseParamModeOpt(parser);
  ParseLocalIdentResult name = ParseLocalIdent(mode.parser);
  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);
  Param param;
  param.mode = mode.elem;
  param.name = std::move(name.name);
  param.name_splice_opt = std::move(name.splice_opt);
  param.type = ty.elem;
  param.span = SpanBetween(start, ty.parser);
  return {ty.parser, param};
}

// =============================================================================
// ParseParamTail - Parse remaining parameters after first
// =============================================================================

ParseElemResult<std::vector<Param>> ParseParamTail(Parser parser,
                                                   std::vector<Param> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-ParamTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    const EndSetToken end_set[] = {EndPunct(")")};
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, ")")) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-ParamTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }
    SPEC_RULE("Parse-ParamTail-Comma");
    ParseElemResult<Param> param = ParseParam(after);
    xs.push_back(param.elem);
    return ParseParamTail(param.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseParamList - Parse parameter list
// =============================================================================

ParseElemResult<std::vector<Param>> ParseParamList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-ParamList-Empty");
    return {parser, {}};
  }
  SPEC_RULE("Parse-ParamList-Cons");
  ParseElemResult<Param> param = ParseParam(parser);
  std::vector<Param> params;
  params.push_back(param.elem);
  return ParseParamTail(param.parser, std::move(params));
}

// =============================================================================
// ParseMethodParams - Parse parameters after receiver
// =============================================================================

ParseElemResult<std::vector<Param>> ParseMethodParams(Parser parser) {
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-MethodParams-None");
    return {parser, {}};
  }
  if (IsPunc(parser, ",")) {
    SPEC_RULE("Parse-MethodParams-Comma");
    Parser next = parser;
    Advance(next);
    return ParseParamList(next);
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, {}};
}

// =============================================================================
// ParseReceiver - Parse method receiver
// =============================================================================
//
// Shorthand receivers:
//   ~ (const) - read-only access, expands to self: const Self
//   ~! (unique) - exclusive mutable, expands to self: unique Self
//   ~% (shared) - synchronized shared, expands to self: shared Self
//
// Explicit receiver:
//   [move] self: Type

ParseElemResult<Receiver> ParseReceiver(Parser parser) {
  if (IsOp(parser, "~")) {
    SPEC_RULE("Parse-Receiver-Short-Const");
    Parser next = parser;
    Advance(next);
    return {next, ReceiverShorthand{ReceiverPerm::Const}};
  }
  if (IsOp(parser, "~!")) {
    SPEC_RULE("Parse-Receiver-Short-Unique");
    Parser next = parser;
    Advance(next);
    return {next, ReceiverShorthand{ReceiverPerm::Unique}};
  }
  if (IsOp(parser, "~%")) {
    SPEC_RULE("Parse-Receiver-Short-Shared");
    Parser next = parser;
    Advance(next);
    return {next, ReceiverShorthand{ReceiverPerm::Shared}};
  }

  SPEC_RULE("Parse-Receiver-Explicit");
  Parser start = parser;
  ParseElemResult<std::optional<ParamMode>> mode = ParseParamModeOpt(parser);
  ParseElemResult<Identifier> name = ParseIdent(mode.parser);
  if (!(name.elem == "self")) {
    EmitParseSyntaxErr(start, TokSpan(start));
  }
  if (!IsPunc(name.parser, ":")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
  } else {
    Advance(name.parser);
  }
  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(name.parser);
  ReceiverExplicit recv;
  recv.mode_opt = mode.elem;
  recv.type = ty.elem;
  return {ty.parser, recv};
}

// =============================================================================
// ParseMethodSignature - Parse method signature with receiver
// =============================================================================
//
// Format: (receiver, param1, param2) -> ReturnType

MethodSignatureResult ParseMethodSignature(Parser parser) {
  SPEC_RULE("Parse-MethodSignature");
  if (!IsPunc(parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Receiver default_recv = ReceiverShorthand{ReceiverPerm::Const};
    return {parser, default_recv, {}, nullptr};
  }
  Parser next = parser;
  Advance(next);
  SkipNewlines(next);
  ParseElemResult<Receiver> receiver = ParseReceiver(next);
  ParseElemResult<std::vector<Param>> params =
      ParseMethodParams(receiver.parser);
  if (!IsPunc(params.parser, ")")) {
    EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
  } else {
    Advance(params.parser);
  }
  ParseElemResult<std::shared_ptr<Type>> ret = ParseReturnOpt(params.parser);
  return {ret.parser, receiver.elem, params.elem, ret.elem};
}

// =============================================================================
// ParseClassMethodSignature - Parse class method signature
// =============================================================================
//
// Supports:
//   - (receiver, params...) -> Ret
//   - (params...) -> Ret   (defaults receiver to const)

MethodSignatureResult ParseClassMethodSignature(Parser parser) {
  if (!IsPunc(parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Receiver default_recv = ReceiverShorthand{ReceiverPerm::Const};
    return {parser, default_recv, {}, nullptr};
  }

  Parser after_l = parser;
  Advance(after_l);
  SkipNewlines(after_l);

  if (IsOp(after_l, "~") || IsOp(after_l, "~!") || IsOp(after_l, "~%")) {
    SPEC_RULE("Parse-ClassMethodSignature-Receiver");
    ParseElemResult<Receiver> receiver = ParseReceiver(after_l);
    ParseElemResult<std::vector<Param>> params =
        ParseMethodParams(receiver.parser);
    if (!IsPunc(params.parser, ")")) {
      EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
    } else {
      Advance(params.parser);
    }
    ParseElemResult<std::shared_ptr<Type>> ret = ParseReturnOpt(params.parser);
    return {ret.parser, receiver.elem, params.elem, ret.elem};
  }

  const Token* tok = Tok(after_l);
  if (tok && ((tok->kind == TokenKind::Identifier && tok->lexeme == "self") ||
              IsKw(after_l, "move"))) {
    SPEC_RULE("Parse-ClassMethodSignature-Explicit");
    ParseElemResult<Receiver> receiver = ParseReceiver(after_l);
    ParseElemResult<std::vector<Param>> params =
        ParseMethodParams(receiver.parser);
    if (!IsPunc(params.parser, ")")) {
      EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
    } else {
      Advance(params.parser);
    }
    ParseElemResult<std::shared_ptr<Type>> ret = ParseReturnOpt(params.parser);
    return {ret.parser, receiver.elem, params.elem, ret.elem};
  }

  SPEC_RULE("Parse-ClassMethodSignature-Default");
  SignatureResult sig = ParseSignature(parser);
  Receiver default_recv = ReceiverShorthand{ReceiverPerm::Const};
  return {sig.parser, default_recv, sig.params, sig.return_type_opt};
}

// =============================================================================
// ParseStateMethodSignature - Parse modal state method signature
// =============================================================================
//
// SPEC: Parse-StateMethodSignature-Receiver
// Syntax:
//   - ( ~ | ~! | ~% , params... ) -> Ret
//   - ( self: T , params... ) -> Ret
//   - ( move self: T , params... ) -> Ret

MethodSignatureResult ParseStateMethodSignature(Parser parser) {
  SPEC_RULE("Parse-StateMethodSignature-Receiver");
  return ParseMethodSignature(parser);
}

// =============================================================================
// ParseSignature - Parse regular procedure signature
// =============================================================================
//
// Format: (param1, param2) -> ReturnType

SignatureResult ParseSignature(Parser parser) {
  SPEC_RULE("Parse-Signature");
  if (!IsPunc(parser, "(")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}, nullptr};
  }
  Parser next = parser;
  Advance(next);
  ParseElemResult<std::vector<Param>> params = ParseParamList(next);
  if (!IsPunc(params.parser, ")")) {
    EmitParseSyntaxErr(params.parser, TokSpan(params.parser));
  } else {
    Advance(params.parser);
  }
  ParseElemResult<std::shared_ptr<Type>> ret = ParseReturnOpt(params.parser);
  return {ret.parser, params.elem, ret.elem};
}

}  // namespace ultraviolet::ast
