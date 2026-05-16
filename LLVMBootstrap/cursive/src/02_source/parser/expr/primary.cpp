// =============================================================================
// primary.cpp - Primary Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.8.3, Lines 5158-5320
//
// This file implements:
// - ParsePrimary: Main entry point for primary expression parsing
//
// Primary expressions are the atoms of the expression grammar:
// - Literals (int, float, string, char, bool, null)
// - Identifiers
// - Qualified paths (module::item)
// - Parenthesized expressions and tuples
// - Block expressions
// - If/loop expressions (delegated)
// - Array/record literals
// - Contract intrinsics (@result, @entry)
// - Concurrency expressions (spawn, dispatch, race, all, sync, yield, wait)
// - Special keywords (transmute, unsafe)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/keywords.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::IsIdentTok;
using cursive::lexer::IsKwTok;
using cursive::lexer::IsOpTok;
using cursive::lexer::IsPuncTok;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsLiteralToken(const Token& tok);

// Forward declarations from parser utilities
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);

// Forward declarations from other modules
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// Forward declarations from individual expression parsers
std::optional<ParseElemResult<ExprPtr>> TryParseLiteralExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParsePtrNullExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseIdentifierExpr(Parser parser,
                                                               bool allow_brace);
std::optional<ParseElemResult<ExprPtr>> TryParseReceiverRef(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseWaitExpr(Parser parser);
ParseElemResult<std::vector<ExprPtr>> ParseTupleExprElems(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseClosureExpr(Parser parser);
ParseElemResult<ExprPtr> ParseIfExpr(Parser parser);
ParseElemResult<ExprPtr> ParseQualifiedApply(Parser parser, bool allow_brace);

// Loop expression parsers (from individual loop files)
// TryPatternInResult and TryParsePatternIn are declared in parser.h
ParseElemResult<ExprPtr> ParseLoopInfiniteExpr(Parser parser);
ParseElemResult<ExprPtr> ParseLoopConditionalExpr(Parser parser);
ParseElemResult<ExprPtr> ParseLoopIterExpr(Parser parser, TryPatternInResult try_in);
ParseElemResult<ExprPtr> ParseRecordLiteral(Parser parser, bool allow_brace);
ParseElemResult<ExprPtr> ParseArrayLiteralExpr(Parser parser);
ParseElemResult<ExprPtr> ParseRaceExpr(Parser parser);
ParseElemResult<ExprPtr> ParseAllExpr(Parser parser);
ParseElemResult<ExprPtr> ParseDispatchExpr(Parser parser);
ParseElemResult<ExprPtr> ParseParallelExpr(Parser parser);
ParseElemResult<ExprPtr> ParseSpawnExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseTransmuteExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseAllocExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseComptimeExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseTypeLiteralExpr(Parser parser);
std::optional<ParseElemResult<ExprPtr>> TryParseQuoteExpr(Parser parser);
ParseElemResult<ExprPtr> ParseExplicitAllocExpr(Parser parser,
                                                const Identifier& region_name,
                                                const Parser& start);

namespace {

struct TupleScanResult {
  bool is_tuple = false;
};

struct RecordLiteralStart {
  bool is_record_literal = false;
  std::size_t path_segments = 0;
};

int ParenDelta(const Token& tok) {
  if (tok.kind != TokenKind::Punctuator) {
    return 0;
  }
  if (tok.lexeme == "(") {
    return 1;
  }
  if (tok.lexeme == ")") {
    return -1;
  }
  return 0;
}

struct TupleScanDepth {
  int paren = 1;
  int bracket = 0;
  int brace = 0;
};

void StepNonParenNesting(const Token& tok, TupleScanDepth& depth) {
  if (tok.kind != TokenKind::Punctuator) {
    return;
  }
  if (tok.lexeme == "[") {
    depth.bracket += 1;
    return;
  }
  if (tok.lexeme == "]" && depth.bracket > 0) {
    depth.bracket -= 1;
    return;
  }
  if (tok.lexeme == "{") {
    depth.brace += 1;
    return;
  }
  if (tok.lexeme == "}" && depth.brace > 0) {
    depth.brace -= 1;
    return;
  }
}

TupleScanResult TupleScan(Parser parser) {
  TupleScanResult result;
  Parser cur = parser;
  TupleScanDepth depth;
  for (;;) {
    if (AtEof(cur)) {
      result.is_tuple = false;
      return result;
    }
    const Token* tok = Tok(cur);
    if (!tok) {
      result.is_tuple = false;
      return result;
    }
    if (tok->kind == TokenKind::Punctuator && tok->lexeme == ")" &&
        depth.paren == 1) {
      result.is_tuple = false;
      return result;
    }
    if (tok->kind == TokenKind::Punctuator &&
        (tok->lexeme == "," || tok->lexeme == ";") && depth.paren == 1 &&
        depth.bracket == 0 && depth.brace == 0) {
      if (tok->lexeme == ",") {
        Parser after_sep = AdvanceOrEOF(cur);
        SkipNewlines(after_sep);
        const Token* next_tok = Tok(after_sep);
        if (next_tok && next_tok->kind == TokenKind::Punctuator &&
            next_tok->lexeme == ")") {
          result.is_tuple = false;
          return result;
        }
      }
      result.is_tuple = true;
      return result;
    }
    depth.paren += ParenDelta(*tok);
    StepNonParenNesting(*tok, depth);
    Advance(cur);
  }
}

bool TupleParen(Parser parser) {
  if (!IsPunc(parser, "(")) return false;
  Parser next = parser;
  Advance(next);
  if (IsPunc(next, ")")) return true;
  TupleScanResult scan = TupleScan(next);
  return scan.is_tuple;
}

RecordLiteralStart ScanRecordLiteralStart(Parser parser) {
  RecordLiteralStart result;
  const Token* head = Tok(parser);
  if (!head || !IsIdentTok(*head)) {
    return result;
  }

  Parser after_path = parser;
  Advance(after_path);
  result.path_segments = 1;

  while (const Token* separator = Tok(after_path)) {
    if (!IsOpTok(*separator, "::")) {
      break;
    }

    Parser after_separator = after_path;
    Advance(after_separator);
    const Token* segment = Tok(after_separator);
    if (!segment || !IsIdentTok(*segment)) {
      return result;
    }

    after_path = after_separator;
    Advance(after_path);
    ++result.path_segments;
  }

  const Token* look = Tok(after_path);
  if (look && IsOpTok(*look, "@")) {
    result.is_record_literal = true;
    return result;
  }

  if (look && IsPuncTok(*look, "{") && result.path_segments == 1) {
    result.is_record_literal = true;
    return result;
  }

  if (look && IsOpTok(*look, "<")) {
    Parser after_angles = SkipAngles(after_path);
    const Token* after = Tok(after_angles);
    if (after && IsOpTok(*after, "@")) {
      result.is_record_literal = true;
    }
  }

  return result;
}

std::optional<ParseElemResult<ExprPtr>> TryParseSpliceExpr(Parser parser) {
  if (!IsOp(parser, "$")) {
    return std::nullopt;
  }
  if (!parser.quote_mode) {
    Parser after_dollar = parser;
    Advance(after_dollar);
    if (IsPunc(after_dollar, "(")) {
      EmitSpliceOutsideQuoteErr(after_dollar, SpanBetween(parser, after_dollar));
      SyncStmt(after_dollar);
      return ParseElemResult<ExprPtr>{
          after_dollar, MakeExpr(SpanBetween(parser, after_dollar), ErrorExpr{})};
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
  ParseElemResult<ExprPtr> splice_expr = ParseExpr(inner);
  Parser after_inner = splice_expr.parser;
  after_inner.quote_mode = parser.quote_mode;
  if (!IsPunc(after_inner, ")")) {
    EmitParseSyntaxErr(after_inner, TokSpan(after_inner));
    Parser sync = after_inner;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  Parser after_r = after_inner;
  Advance(after_r);
  SpliceExprNode splice;
  splice.expr = splice_expr.elem;
  splice.span = SpanBetween(parser, after_r);
  return ParseElemResult<ExprPtr>{
      after_r, MakeExpr(SpanBetween(parser, after_r), splice)};
}

std::optional<ParseElemResult<ExprPtr>> TryParseSpliceIdentExpr(Parser parser) {
  if (!IsOp(parser, "$")) {
    return std::nullopt;
  }
  if (!parser.quote_mode) {
    Parser after_dollar = parser;
    Advance(after_dollar);
    const Token* next = Tok(after_dollar);
    if (next && IsIdentTok(*next)) {
      Parser after_ident = after_dollar;
      Advance(after_ident);
      EmitSpliceOutsideQuoteErr(after_ident, SpanBetween(parser, after_ident));
      return ParseElemResult<ExprPtr>{
          after_ident, MakeExpr(SpanBetween(parser, after_ident), ErrorExpr{})};
    }
    return std::nullopt;
  }
  Parser after_dollar = parser;
  Advance(after_dollar);
  const Token* tok = Tok(after_dollar);
  if (!tok || !IsIdentTok(*tok)) {
    return std::nullopt;
  }

  Parser after_ident = after_dollar;
  Advance(after_ident);

  IdentifierExpr ident;
  ident.name = tok->lexeme;

  SpliceIdentNode splice;
  splice.name_expr = MakeExpr(tok->span, ident);
  splice.span = SpanBetween(parser, after_ident);
  return ParseElemResult<ExprPtr>{
      after_ident, MakeExpr(SpanBetween(parser, after_ident), splice)};
}

}  // namespace

// =============================================================================
// ParseLoopExpr - Parse loop expression (dispatcher)
// =============================================================================
//
// SPEC: Lines 5259-5262
// Dispatches to infinite, iterator, or conditional loop based on lookahead.

ParseElemResult<ExprPtr> ParseLoopExpr(Parser parser) {
  SPEC_RULE("Parse-Loop-Expr");
  Parser next = parser;
  Advance(next);  // consume "loop" keyword

  // Infinite loop: loop { body } or loop |: { invariant } { body }
  if (IsPunc(next, "{") || IsOp(next, "|:")) {
    return ParseLoopInfiniteExpr(next);
  }

  // Try iterator loop: loop pattern in iterable { body }
  TryPatternInResult try_in = TryParsePatternIn(next);
  if (try_in.ok) {
    return ParseLoopIterExpr(next, try_in);
  }

  // Conditional loop: loop condition { body }
  return ParseLoopConditionalExpr(next);
}

// =============================================================================
// ParsePrimary - Main primary expression entry point
// =============================================================================
//
// SPEC: Lines 5158-5320

ParseElemResult<ExprPtr> ParsePrimary(Parser parser, bool allow_brace) {
  const Token* tok = Tok(parser);
  if (!tok) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakeExpr(TokSpan(parser), ErrorExpr{})};
  }

  if (auto splice = TryParseSpliceExpr(parser)) {
    return *splice;
  }
  if (auto splice_ident = TryParseSpliceIdentExpr(parser)) {
    return *splice_ident;
  }

  // Contract intrinsics: @result, @entry(expr)
  if (IsOpTok(*tok, "@")) {
    Parser start = parser;
    Parser next = parser;
    Advance(next);
    const Token* name_tok = Tok(next);
    if (name_tok &&
        (IsIdentTok(*name_tok) || name_tok->kind == TokenKind::Keyword)) {
      const std::string_view name = name_tok->lexeme;
      Parser after_name = next;
      Advance(after_name);
      if (name == "result") {
        SPEC_RULE("Parse-Contract-Result");
        ResultExpr res;
        return {after_name, MakeExpr(SpanBetween(start, after_name), res)};
      }
      if (name == "entry") {
        SPEC_RULE("Parse-Contract-Entry");
        if (!IsPunc(after_name, "(")) {
          EmitParseSyntaxErr(after_name, TokSpan(after_name));
          Parser sync = after_name;
          SyncStmt(sync);
          return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
        }
        Parser after_l = after_name;
        Advance(after_l);
        ParseElemResult<ExprPtr> expr = ParseExpr(after_l);
        if (!IsPunc(expr.parser, ")")) {
          EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
          Parser sync = expr.parser;
          SyncStmt(sync);
          return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
        }
        Parser after_r = expr.parser;
        Advance(after_r);
        EntryExpr entry;
        entry.expr = expr.elem;
        return {after_r, MakeExpr(SpanBetween(start, after_r), entry)};
      }
    }
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  // yield expression
  if (IsKwTok(*tok, "yield")) {
    Parser start = parser;
    Parser next = parser;
    Advance(next);
    bool release = false;
    const Token* maybe_release = Tok(next);
    if (maybe_release && IsIdentTok(*maybe_release) &&
        maybe_release->lexeme == "release") {
      release = true;
      Advance(next);
    }
    if (IsKw(next, "from")) {
      SPEC_RULE("Parse-Yield-From-Expr");
      Parser after_from = next;
      Advance(after_from);
      ParseElemResult<ExprPtr> expr = ParseExpr(after_from);
      YieldFromExpr yf;
      yf.release = release;
      yf.value = expr.elem;
      return {expr.parser, MakeExpr(SpanBetween(start, expr.parser), yf)};
    }
    SPEC_RULE("Parse-Yield-Expr");
    ParseElemResult<ExprPtr> expr = ParseExpr(next);
    YieldExpr y;
    y.release = release;
    y.value = expr.elem;
    return {expr.parser, MakeExpr(SpanBetween(start, expr.parser), y)};
  }

  // sync expression
  if (IsKwTok(*tok, "sync")) {
    SPEC_RULE("Parse-Sync-Expr");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> expr = ParseExpr(next);
    SyncExpr sync;
    sync.value = expr.elem;
    return {expr.parser, MakeExpr(SpanBetween(parser, expr.parser), sync)};
  }

  // race expression
  if (IsKwTok(*tok, "race")) {
    SPEC_RULE("Parse-Race-Expr");
    return ParseRaceExpr(parser);
  }

  // all expression
  if (IsKwTok(*tok, "all")) {
    SPEC_RULE("Parse-All-Expr");
    return ParseAllExpr(parser);
  }

  // spawn expression
  if (IsKwTok(*tok, "spawn")) {
    SPEC_RULE("Parse-Spawn-Expr");
    return ParseSpawnExpr(parser);
  }

  // dispatch expression
  if (IsKwTok(*tok, "dispatch")) {
    SPEC_RULE("Parse-Dispatch-Expr");
    return ParseDispatchExpr(parser);
  }

  // parallel expression
  if (IsKwTok(*tok, "parallel")) {
    SPEC_RULE("Parse-Parallel-Expr");
    return ParseParallelExpr(parser);
  }

  // wait expression (contextual keyword)
  if (auto wait = TryParseWaitExpr(parser)) {
    return *wait;
  }

  // fence expression (contextual intrinsic form parsed on the ordinary path)
  if (tok->kind == TokenKind::Identifier && tok->lexeme == "fence") {
    Parser after_name = parser;
    Advance(after_name);
    if (IsPunc(after_name, "(")) {
      Parser start = parser;
      Parser cur = after_name;
      Advance(cur);  // consume "("

      const Token* order_tok = Tok(cur);
      if (!order_tok || order_tok->kind != TokenKind::Identifier) {
        EmitParseSyntaxErr(cur, TokSpan(cur));
        Parser sync = cur;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
      }

      FenceOrder order = FenceOrder::SeqCst;
      if (order_tok->lexeme == "acquire") {
        order = FenceOrder::Acquire;
      } else if (order_tok->lexeme == "release") {
        order = FenceOrder::Release;
      } else if (order_tok->lexeme == "seqcst") {
        order = FenceOrder::SeqCst;
      } else {
        EmitParseSyntaxErr(cur, TokSpan(cur));
        Parser sync = cur;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
      }

      Advance(cur);
      if (!IsPunc(cur, ")")) {
        EmitParseSyntaxErr(cur, TokSpan(cur));
        Parser sync = cur;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
      }

      Parser after_rparen = cur;
      Advance(after_rparen);

      FenceExpr fence;
      fence.order = order;
      return {after_rparen, MakeExpr(SpanBetween(start, after_rparen), fence)};
    }
  }

  // closure expression
  if (auto closure = TryParseClosureExpr(parser)) {
    return *closure;
  }

  // Receiver reference (~)
  if (auto recv = TryParseReceiverRef(parser)) {
    return *recv;
  }

  // Literal expressions
  if (auto lit = TryParseLiteralExpr(parser)) {
    return *lit;
  }

  // Ptr::null() expression
  if (auto ptr_null = TryParsePtrNullExpr(parser)) {
    return *ptr_null;
  }

  // if expression
  if (IsKwTok(*tok, "if")) {
    return ParseIfExpr(parser);
  }

  // loop expression
  if (IsKwTok(*tok, "loop")) {
    return ParseLoopExpr(parser);
  }

  // sizeof/alignof expressions
  // These are contextual intrinsics. They parse in primary position even when
  // lexed as identifiers, which keeps `sizeof`/`alignof` usable as ordinary
  // identifiers outside intrinsic form.
  const bool is_sizeof_kw = IsKwTok(*tok, "sizeof");
  const bool is_alignof_kw = IsKwTok(*tok, "alignof");
  const bool is_sizeof_ident = IsIdentTok(*tok) && tok->lexeme == "sizeof";
  const bool is_alignof_ident = IsIdentTok(*tok) && tok->lexeme == "alignof";
  Parser intrinsic_probe = parser;
  Advance(intrinsic_probe);
  const bool intrinsic_call_form = IsPunc(intrinsic_probe, "(");
  const bool is_contextual_intrinsic =
      intrinsic_call_form && (is_sizeof_ident || is_alignof_ident);
  if (is_sizeof_kw || is_alignof_kw || is_contextual_intrinsic) {
    const bool is_sizeof = is_sizeof_kw || is_sizeof_ident;
    Parser next = parser;
    Advance(next);  // past intrinsic head token
    if (!IsPunc(next, "(")) {
      EmitParseSyntaxErr(next, TokSpan(next));
      Parser sync = next;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after_lparen = next;
    Advance(after_lparen);
    ParseElemResult<std::shared_ptr<Type>> type_arg = ParseType(after_lparen);
    if (!IsPunc(type_arg.parser, ")")) {
      EmitParseSyntaxErr(type_arg.parser, TokSpan(type_arg.parser));
      Parser sync = type_arg.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after_rparen = type_arg.parser;
    Advance(after_rparen);
    if (is_sizeof) {
      SizeofExpr sizeof_expr;
      sizeof_expr.type = type_arg.elem;
      return {after_rparen,
              MakeExpr(SpanBetween(parser, after_rparen), sizeof_expr)};
    }
    AlignofExpr alignof_expr;
    alignof_expr.type = type_arg.elem;
    return {after_rparen,
            MakeExpr(SpanBetween(parser, after_rparen), alignof_expr)};
  }

  // transmute expression
  if (auto transmute = TryParseTransmuteExpr(parser)) {
    return *transmute;
  }

  // unsafe block expression
  if (IsKwTok(*tok, "unsafe")) {
    SPEC_RULE("Parse-Unsafe-Block-Expr");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Block>> blk = ParseBlock(next);
    UnsafeBlockExpr unsafe_block;
    unsafe_block.block = blk.elem;
    return {blk.parser,
            MakeExpr(SpanBetween(parser, blk.parser), unsafe_block)};
  }

  // comptime expression
  if (auto comptime = TryParseComptimeExpr(parser)) {
    SPEC_RULE("Parse-Comptime-Expr");
    return *comptime;
  }

  // compile-time type literal
  if (auto type_lit = TryParseTypeLiteralExpr(parser)) {
    SPEC_RULE("Parse-Type-Literal");
    return *type_lit;
  }

  // compile-time quote forms
  if (auto quote = TryParseQuoteExpr(parser)) {
    SPEC_RULE("Parse-Quote");
    return *quote;
  }

  // Allocation expression: ^value
  if (auto alloc = TryParseAllocExpr(parser)) {
    return *alloc;
  }

  // Block expression (when allow_brace is true)
  if (allow_brace && IsPuncTok(*tok, "{")) {
    SPEC_RULE("Parse-Block-Expr");
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(parser);
    BlockExpr blk;
    blk.block = block.elem;
    return {block.parser, MakeExpr(SpanBetween(parser, block.parser), blk)};
  }

  // Parenthesized expression or tuple
  if (IsPuncTok(*tok, "(")) {
    // Check if it's a tuple
    if (TupleParen(parser)) {
      SPEC_RULE("Parse-Tuple-Literal");
      Parser next = parser;
      Advance(next);
      ParseElemResult<std::vector<ExprPtr>> elems = ParseTupleExprElems(next);
      if (!IsPunc(elems.parser, ")")) {
        EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
        Parser sync = elems.parser;
        SyncStmt(sync);
        return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
      }
      Parser after = elems.parser;
      Advance(after);
      TupleExpr tuple;
      tuple.elements = std::move(elems.elem);
      return {after, MakeExpr(SpanBetween(parser, after), tuple)};
    }
    // Parenthesized expression
    SPEC_RULE("Parse-Parenthesized-Expr");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> inner = ParseExpr(next);
    if (!IsPunc(inner.parser, ")")) {
      EmitParseSyntaxErr(inner.parser, TokSpan(inner.parser));
      Parser sync = inner.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after = inner.parser;
    Advance(after);
    return {after, inner.elem};
  }

  // Array literal
  if (IsPuncTok(*tok, "[")) {
    SPEC_RULE("Parse-Array-Literal");
    return ParseArrayLiteralExpr(parser);
  }

  // Identifier or qualified path
  if (IsIdentTok(*tok)) {
    // Explicit region allocation: region ^ value
    {
      ParseElemResult<Identifier> alloc_region = ParseIdent(parser);
      if (IsOp(alloc_region.parser, "^")) {
        return ParseExplicitAllocExpr(alloc_region.parser, alloc_region.elem, parser);
      }
    }

    // Record literal (TypeName{...} or ModalType@State{...})
    if (allow_brace) {
      RecordLiteralStart record_start = ScanRecordLiteralStart(parser);
      if (record_start.is_record_literal) {
        return ParseRecordLiteral(parser, allow_brace);
      }
    }
    // Try simple identifier after record-literal detection.
    // This preserves Parse-Record-Literal-ModalState for generic forms like
    // Type<T>@State{...}, which would otherwise be consumed as Identifier.
    if (auto ident = TryParseIdentifierExpr(parser, allow_brace)) {
      return *ident;
    }
    // Must be qualified path
    return ParseQualifiedApply(parser, allow_brace);
  }

  // Error: unexpected token
  EmitParseSyntaxErr(parser, TokSpan(parser));
  Parser sync = parser;
  SyncStmt(sync);
  return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
}

}  // namespace cursive::ast
