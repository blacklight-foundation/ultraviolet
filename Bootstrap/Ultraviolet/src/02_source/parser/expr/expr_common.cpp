// =============================================================================
// MIGRATION MAPPING: expr_common.cpp
// =============================================================================
// This file should contain shared helper functions and predicates used across
// expression parsing modules.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, various sections
// =============================================================================
//
// OVERVIEW:
// -----------------------------------------------------------------------------
// These helpers are used throughout expression parsing:
// - MakeExpr: Creates Expr AST nodes
// - SpanCover: Combines two spans into one covering both
// - SpanBetween: Creates span from parser start to parser end positions
// - IsLiteralToken: Checks if token is a literal type
// - IsExprStart: Checks if token can begin an expression
// - IsPostfixStart: Checks if token can begin a postfix operation
// - IsPlace: Validates place expression for &/move
// - SkipBracedBlock: Error recovery helper
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. MakeExpr function (Lines 150-155)
//    ---------------------------------------------------------------------------
//    Purpose: Factory function for creating expression AST nodes
//
//    Parameters:
//      - span: Source location span for the expression
//      - node: ExprNode variant (the actual expression data)
//
//    Returns: ExprPtr (shared_ptr<Expr>)
//
//    Implementation:
//      - Create new shared_ptr<Expr>
//      - Set expr->span = span
//      - Set expr->node = std::move(node)
//      - Return expr
//
// SOURCE CODE (Lines 150-155):
// -----------------------------------------------------------------------------
//   ExprPtr MakeExpr(const core::Span& span, ExprNode node) {
//     auto expr = std::make_shared<Expr>();
//     expr->span = span;
//     expr->node = std::move(node);
//     return expr;
//   }
//
// -----------------------------------------------------------------------------
// 2. SpanCover function (Lines 142-148)
//    ---------------------------------------------------------------------------
//    Purpose: Creates a span covering from start to end of two spans
//
//    Parameters:
//      - start: The starting span (provides start position)
//      - end: The ending span (provides end position)
//
//    Returns: core::Span covering both input spans
//
//    Implementation:
//      - Copy start span
//      - Update end_offset, end_line, end_col from end span
//      - Return combined span
//
// SOURCE CODE (Lines 142-148):
// -----------------------------------------------------------------------------
//   core::Span SpanCover(const core::Span& start, const core::Span& end) {
//     core::Span span = start;
//     span.end_offset = end.end_offset;
//     span.end_line = end.end_line;
//     span.end_col = end.end_col;
//     return span;
//   }
//
// -----------------------------------------------------------------------------
// 3. SpanBetween function (parser.cpp Lines 76-85)
//    ---------------------------------------------------------------------------
//    NOTE: This is in parser.cpp, not parser_expr.cpp, but is heavily used
//
//    Purpose: Creates span from one parser position to another
//
//    Parameters:
//      - start: Parser at start position
//      - end: Parser at end position
//
//    Returns: core::Span from start token to token before end
//
//    Implementation:
//      - Get start token (or EOF token if none)
//      - Get end token (token at end.index - 1)
//      - Call SpanFrom to combine
//
// SOURCE CODE (parser.cpp Lines 76-85):
// -----------------------------------------------------------------------------
//   core::Span SpanBetween(const Parser& start, const Parser& end) {
//     Token start_tok = Tok(start) ? *Tok(start) : EofAsToken(start);
//     const std::vector<Token>* tokens =
//         end.tokens ? end.tokens : start.tokens;
//     Token end_tok = start_tok;
//     if (tokens && end.index > start.index && end.index - 1 < tokens->size()) {
//       end_tok = (*tokens)[end.index - 1];
//     }
//     return SpanFrom(start_tok, end_tok);
//   }
//
// -----------------------------------------------------------------------------
// 4. IsLiteralToken function (Lines 49-56)
//    ---------------------------------------------------------------------------
//    Purpose: Checks if a token is a literal (for ParsePrimary)
//
//    Parameters:
//      - tok: Token to check
//
//    Returns: true if token is one of the literal kinds
//
//    Literal kinds:
//      - IntLiteral
//      - FloatLiteral
//      - StringLiteral
//      - CharLiteral
//      - BoolLiteral
//      - NullLiteral
//
// SOURCE CODE (Lines 49-56):
// -----------------------------------------------------------------------------
//   bool IsLiteralToken(const Token& tok) {
//     return tok.kind == TokenKind::IntLiteral ||
//            tok.kind == TokenKind::FloatLiteral ||
//            tok.kind == TokenKind::StringLiteral ||
//            tok.kind == TokenKind::CharLiteral ||
//            tok.kind == TokenKind::BoolLiteral ||
//            tok.kind == TokenKind::NullLiteral;
//   }
//
// -----------------------------------------------------------------------------
// 5. IsExprStart function (Lines 74-105)
//    ---------------------------------------------------------------------------
//    Purpose: Checks if a token can begin an expression
//    Used for: Determining if expression follows, lookahead decisions
//
//    Returns true for:
//      - Identifiers (variable names, type names)
//      - Literals (all types via IsLiteralToken)
//      - Punctuators: "(", "[", "{"
//      - Operators: "!", "-", "&", "*", "^", "@"
//      - Keywords: if, loop, unsafe, move, transmute, widen,
//                  sizeof, alignof, parallel, spawn, dispatch,
//                  yield, sync, race, all
//      - Contextual keyword: "wait" (when TokenKind::Identifier)
//
// SOURCE CODE (Lines 74-105):
// -----------------------------------------------------------------------------
//   bool IsExprStart(const Token& tok) {
//     if (IsIdentTok(tok) || IsLiteralToken(tok)) {
//       return true;
//     }
//     if (tok.kind == TokenKind::Punctuator) {
//       return tok.lexeme == "(" || tok.lexeme == "[" ||
//              tok.lexeme == "{";
//     }
//     if (tok.kind == TokenKind::Operator) {
//       return tok.lexeme == "!" || tok.lexeme == "-" || tok.lexeme == "&" ||
//              tok.lexeme == "*" || tok.lexeme == "^" || tok.lexeme == "@";
//     }
//     if (tok.kind == TokenKind::Keyword) {
//       return tok.lexeme == "if" ||
//              tok.lexeme == "loop" || tok.lexeme == "unsafe" ||
//              tok.lexeme == "move" || tok.lexeme == "transmute" ||
//              tok.lexeme == "widen" ||
//              // Layout intrinsics
//              tok.lexeme == "sizeof" || tok.lexeme == "alignof" ||
//              // UVX Extension: Structured Concurrency
//              tok.lexeme == "parallel" || tok.lexeme == "spawn" ||
//              tok.lexeme == "dispatch" ||
//              // UVX Extension: Async expressions
//              tok.lexeme == "yield" || tok.lexeme == "sync" ||
//              tok.lexeme == "race" || tok.lexeme == "all";
//     }
//     // UVX Extension: "wait" is a contextual keyword
//     if (tok.kind == TokenKind::Identifier && tok.lexeme == "wait") {
//       return true;
//     }
//     return false;
//   }
//
// -----------------------------------------------------------------------------
// 6. IsPostfixStart function (Lines 107-115)
//    ---------------------------------------------------------------------------
//    Purpose: Checks if a token can begin a postfix operation
//    Used for: ParsePostfixTail loop termination
//
//    Returns true for:
//      - Punctuators: ".", "[", "("
//      - Operators: "~>", "?"
//
// SOURCE CODE (Lines 107-115):
// -----------------------------------------------------------------------------
//   bool IsPostfixStart(const Token& tok) {
//     if (tok.kind == TokenKind::Punctuator) {
//       return tok.lexeme == "." || tok.lexeme == "[" || tok.lexeme == "(";
//     }
//     if (tok.kind == TokenKind::Operator) {
//       return tok.lexeme == "~>" || tok.lexeme == "?";
//     }
//     return false;
//   }
//
// -----------------------------------------------------------------------------
// 7. IsPlace predicate (Lines 157-180)
//    ---------------------------------------------------------------------------
//    Purpose: Validates if expression is a valid place expression
//    Used for: ParsePlace validation, address-of and move operand checking
//
//    SPEC REFERENCE: Docs/SPECIFICATION.md, Lines 6045-6047
//    IsPlace(e) <=> e in {Identifier(_), FieldAccess(_, _), TupleAccess(_, _),
//                         IndexAccess(_, _)}
//                   OR (exists p. e = Deref(p) AND IsPlace(p))
//
//    Returns true for:
//      - IdentifierExpr (variable names)
//      - FieldAccessExpr (x.field)
//      - TupleAccessExpr (x.0)
//      - IndexAccessExpr (x[i])
//      - AttributedExpr wrapping a place (recursively check inner)
//      - DerefExpr wrapping a place (recursively check inner)
//
// SOURCE CODE (Lines 157-180):
// -----------------------------------------------------------------------------
//   bool IsPlace(const ExprPtr& expr) {
//     if (!expr) {
//       return false;
//     }
//     if (std::holds_alternative<IdentifierExpr>(expr->node)) {
//       return true;
//     }
//     if (std::holds_alternative<FieldAccessExpr>(expr->node)) {
//       return true;
//     }
//     if (std::holds_alternative<TupleAccessExpr>(expr->node)) {
//       return true;
//     }
//     if (std::holds_alternative<IndexAccessExpr>(expr->node)) {
//       return true;
//     }
//     if (const auto* attr = std::get_if<AttributedExpr>(&expr->node)) {
//       return IsPlace(attr->expr);
//     }
//     if (const auto* deref = std::get_if<DerefExpr>(&expr->node)) {
//       return IsPlace(deref->value);
//     }
//     return false;
//   }
//
// -----------------------------------------------------------------------------
// 8. SkipBracedBlock function (Lines 117-140)
//    ---------------------------------------------------------------------------
//    Purpose: Skip over a braced block for error recovery
//    Used for: Recovering from parse errors within blocks
//
//    Parameters:
//      - parser: Current parser state (should be at "{")
//
//    Returns: Parser positioned after matching "}"
//
//    Implementation:
//      - Track brace depth
//      - Scan tokens, incrementing on "{", decrementing on "}"
//      - Return when depth returns to 0
//
// SOURCE CODE (Lines 117-140):
// -----------------------------------------------------------------------------
//   Parser SkipBracedBlock(Parser parser) {
//     if (!IsPunc(parser, "{")) {
//       return parser;
//     }
//     Parser cur = parser;
//     int depth = 0;
//     while (!AtEof(cur)) {
//       const Token* tok = Tok(cur);
//       if (tok && tok->kind == TokenKind::Punctuator) {
//         if (tok->lexeme == "{") {
//           depth += 1;
//         } else if (tok->lexeme == "}") {
//           depth -= 1;
//           Advance(cur);
//           if (depth == 0) {
//             return cur;
//           }
//           continue;
//         }
//       }
//       Advance(cur);
//     }
//     return cur;
//   }
//
// -----------------------------------------------------------------------------
// 9. SkipNewlines function (Lines 28-32)
//    ---------------------------------------------------------------------------
//    Purpose: Skip over newline tokens
//    Used for: Handling optional newlines in grammar
//
// SOURCE CODE (Lines 28-32):
// -----------------------------------------------------------------------------
//   void SkipNewlines(Parser& parser) {
//     while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
//       Advance(parser);
//     }
//   }
//
// -----------------------------------------------------------------------------
// 10. NormalizeBindingPattern function (Lines 58-72)
//    ---------------------------------------------------------------------------
//    Purpose: Normalize TypedPattern to IdentifierPattern + separate type
//    Used for: let/var binding parsing where type annotation is separate
//
// SOURCE CODE (Lines 58-72):
// -----------------------------------------------------------------------------
//   void NormalizeBindingPattern(std::shared_ptr<Pattern>& pat,
//                                std::shared_ptr<Type>& type_opt) {
//     if (!pat || type_opt) {
//       return;
//     }
//     const auto* typed = std::get_if<TypedPattern>(&pat->node);
//     if (!typed) {
//       return;
//     }
//     auto normalized = std::make_shared<Pattern>();
//     normalized->span = pat->span;
//     normalized->node = IdentifierPattern{typed->name};
//     type_opt = typed->type;
//     pat = std::move(normalized);
//   }
//
// =============================================================================
// PARSER UTILITY FUNCTIONS (from parser.cpp/parser.h)
// =============================================================================
// The following functions are in the core parser module but are used heavily:
//
// - Tok(parser) -> const Token*: Get current token
// - Advance(parser): Move to next token (modifies parser)
// - AtEof(parser) -> bool: Check if at end of file
// - TokSpan(parser) -> core::Span: Get span of current token
// - IsKw(parser, kw) -> bool: Check if current token is keyword
// - IsOp(parser, op) -> bool: Check if current token is operator
// - IsPunc(parser, punc) -> bool: Check if current token is punctuator
// - IsIdentTok(tok) -> bool: Check if token is identifier
// - IsKwTok(tok, kw) -> bool: Check if token is specific keyword
// - IsOpTok(tok, op) -> bool: Check if token is specific operator
// - IsPuncTok(tok, punc) -> bool: Check if token is specific punctuator
// - EmitParseSyntaxErr(parser, span): Emit syntax error diagnostic
// - SyncStmt(parser): Error recovery - skip to statement boundary
//
// =============================================================================
// DEPENDENCIES:
// =============================================================================
// - core::Span structure
// - Parser structure
// - Token structure and TokenKind enum
// - Expr, ExprPtr, ExprNode types
// - Pattern, TypedPattern, IdentifierPattern types
// - Various expression node types for IsPlace checks
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Consider: Move SpanBetween from parser.cpp to here for expression-specific use
// - Consider: Group related predicates (IsLiteralToken, IsExprStart, IsPostfixStart)
// - Consider: Make IsPlace a method on Expr class instead of free function
// - NormalizeBindingPattern is specific to statement parsing (let/var bindings)
//   - May belong in stmt_common.cpp instead
// - SkipBracedBlock is error recovery - may belong in parser_common.cpp
// - All these functions are in anonymous namespace in bootstrap
//   - Consider visibility: internal linkage vs module-level helpers
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>
#include <variant>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::IsIdentTok;
using ultraviolet::lexer::IsKwTok;
using ultraviolet::lexer::IsOpTok;
using ultraviolet::lexer::IsPuncTok;
using ultraviolet::lexer::Ctx;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// =============================================================================
// Parser Predicates - Check token properties at current position
// =============================================================================

static bool IsKwLocal(const Parser& parser, std::string_view kw) {
  const Token* tok = Tok(parser);
  return tok && IsKwTok(*tok, kw);
}

static bool IsOpLocal(const Parser& parser, std::string_view op) {
  const Token* tok = Tok(parser);
  return tok && IsOpTok(*tok, op);
}

static bool IsPuncLocal(const Parser& parser, std::string_view punc) {
  const Token* tok = Tok(parser);
  return tok && IsPuncTok(*tok, punc);
}

// =============================================================================
// SkipNewlines - Skip over newline tokens
// =============================================================================

static void SkipNewlinesLocal(Parser& parser) {
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }
}

// Forward declarations for externally defined functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);

// =============================================================================
// IsLiteralToken - Check if token is a literal type
// =============================================================================

bool IsLiteralToken(const Token& tok) {
  return tok.kind == TokenKind::IntLiteral ||
         tok.kind == TokenKind::FloatLiteral ||
         tok.kind == TokenKind::StringLiteral ||
         tok.kind == TokenKind::CharLiteral ||
         tok.kind == TokenKind::BoolLiteral ||
         tok.kind == TokenKind::NullLiteral;
}

// =============================================================================
// IsExprStart - Check if token can begin an expression
// =============================================================================

bool IsExprStart(const Token& tok) {
  if (IsIdentTok(tok) || IsLiteralToken(tok)) {
    return true;
  }
  if (tok.kind == TokenKind::Punctuator) {
    return tok.lexeme == "(" || tok.lexeme == "[" || tok.lexeme == "{";
  }
  if (tok.kind == TokenKind::Operator) {
    return tok.lexeme == "!" || tok.lexeme == "-" || tok.lexeme == "&" ||
           tok.lexeme == "*" || tok.lexeme == "^" || tok.lexeme == "@" ||
           tok.lexeme == "|";
  }
  if (tok.kind == TokenKind::Keyword) {
    return tok.lexeme == "if" ||
          tok.lexeme == "loop" || tok.lexeme == "unsafe" ||
          tok.lexeme == "move" || tok.lexeme == "transmute" ||
          tok.lexeme == "widen" || tok.lexeme == "comptime" ||
          tok.lexeme == "quote" ||
           // Layout intrinsics
           tok.lexeme == "sizeof" || tok.lexeme == "alignof" ||
           // UVX Extension: Structured Concurrency
           tok.lexeme == "parallel" || tok.lexeme == "spawn" ||
           tok.lexeme == "dispatch" ||
           // UVX Extension: Async expressions
           tok.lexeme == "yield" || tok.lexeme == "sync" ||
           tok.lexeme == "race" || tok.lexeme == "all";
  }
  // UVX Extension: "wait" is a contextual keyword
  if (Ctx(tok, "wait")) {
    return true;
  }
  return false;
}

// =============================================================================
// IsPostfixStart - Check if token can begin a postfix operation
// =============================================================================

bool IsPostfixStart(const Token& tok) {
  if (tok.kind == TokenKind::Punctuator) {
    return tok.lexeme == "." || tok.lexeme == "[" || tok.lexeme == "(";
  }
  if (tok.kind == TokenKind::Operator) {
    return tok.lexeme == "~>" || tok.lexeme == "?";
  }
  return false;
}

// =============================================================================
// SkipBracedBlock - Skip over a braced block for error recovery
// =============================================================================

Parser SkipBracedBlock(Parser parser) {
  if (!IsPunc(parser, "{")) {
    return parser;
  }
  Parser cur = parser;
  int depth = 0;
  while (!AtEof(cur)) {
    const Token* tok = Tok(cur);
    if (tok && tok->kind == TokenKind::Punctuator) {
      if (tok->lexeme == "{") {
        depth += 1;
      } else if (tok->lexeme == "}") {
        depth -= 1;
        Advance(cur);
        if (depth == 0) {
          return cur;
        }
        continue;
      }
    }
    Advance(cur);
  }
  return cur;
}

// =============================================================================
// SpanCover - Create span covering from start to end of two spans
// =============================================================================

core::Span SpanCover(const core::Span& start, const core::Span& end) {
  core::Span span = start;
  span.end_offset = end.end_offset;
  span.end_line = end.end_line;
  span.end_col = end.end_col;
  return span;
}

// =============================================================================
// MakeExpr - Factory function for creating expression AST nodes
// =============================================================================

ExprPtr MakeExpr(const core::Span& span, ExprNode node) {
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

// =============================================================================
// IsPlace - Check if expression is a valid place expression
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Lines 6045-6047
// IsPlace(e) <=> e in {Identifier(_), FieldAccess(_, _), TupleAccess(_, _),
//                      IndexAccess(_, _)}
//                OR (exists p. e = Deref(p) AND IsPlace(p))

bool IsPlace(const ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (std::holds_alternative<IdentifierExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<FieldAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<TupleAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<IndexAccessExpr>(expr->node)) {
    return true;
  }
  if (const auto* attr = std::get_if<AttributedExpr>(&expr->node)) {
    return IsPlace(attr->expr);
  }
  if (const auto* deref = std::get_if<DerefExpr>(&expr->node)) {
    return IsPlace(deref->value);
  }
  return false;
}

// NormalizeBindingPattern is defined in item_common.cpp (canonical location)

// =============================================================================
// ParseExpr - Main expression parsing entry point
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// Parse-Expr (Lines 5600-5604)
//
// Parses a complete expression. This is the main entry point for expression
// parsing, delegating to ParseRange which handles the precedence hierarchy.

// Forward declaration for ParseRange
ParseElemResult<ExprPtr> ParseRange(Parser parser, bool allow_brace,
                                    bool allow_struct);
ParseElemResult<ExprPtr> ParseLogicalOr(Parser parser, bool allow_brace,
                                        bool allow_bracket);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

namespace {

template <typename ParseBodyFn>
ParseElemResult<ExprPtr> ParseExprWithLeadingAttrs(Parser parser,
                                                   ParseBodyFn&& parse_body) {
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  Parser next = attrs.parser;

  ParseElemResult<ExprPtr> expr = parse_body(next);
  if (attrs.elem.has_value()) {
    return {expr.parser,
            AttachExprAttrs(expr.elem, *attrs.elem,
                            SpanBetween(parser, expr.parser))};
  }
  return expr;
}

}  // namespace

ParseElemResult<ExprPtr> ParseExpr(Parser parser) {
  SPEC_RULE("Parse-Expr");
  return ParseExprWithLeadingAttrs(
      parser, [](Parser next) { return ParseRange(next, true, true); });
}

// =============================================================================
// ParseExprNoBrace - Parse expression without allowing braced expressions
// =============================================================================
//
// Used in contexts where braces could be ambiguous (e.g., if conditions).

ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser) {
  SPEC_RULE("Parse-Expr-NoBrace");
  return ParseExprWithLeadingAttrs(
      parser, [](Parser next) { return ParseRange(next, false, true); });
}

// =============================================================================
// ParsePredicateExpr - Parse predicate expressions (for where/contract/invariant)
// =============================================================================
//
// Spec: ParsePredicateExpr delegates to ParseExpr. Contract purity rules later
// decide which expression forms are valid predicates.

ParseElemResult<ExprPtr> ParsePredicateExpr(Parser parser) {
  SPEC_RULE("ParsePredicateExpr");
  SPEC_RULE("Parse-Predicate-Expr");
  return ParseExprWithLeadingAttrs(
      parser, [](Parser next) { return ParseRange(next, true, true); });
}

// =============================================================================
// ParseExprOpt - Optional expression parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// Parse-ExprOpt-* (Lines 5606-5612)
//
// Parses an expression if one is present, otherwise returns nullptr.
// Used where expressions are optional (e.g., return statements).

ParseElemResult<ExprPtr> ParseExprOpt(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || tok->kind == TokenKind::Newline ||
      (tok->kind == TokenKind::Punctuator &&
       (tok->lexeme == ";" || tok->lexeme == "}")) ||
      AtEof(parser)) {
    SPEC_RULE("Parse-ExprOpt-None");
    return {parser, nullptr};
  }
  SPEC_RULE("Parse-ExprOpt-Yes");
  return ParseExpr(parser);
}

}  // namespace ultraviolet::ast
