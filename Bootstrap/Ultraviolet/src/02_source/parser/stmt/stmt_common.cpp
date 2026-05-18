// =============================================================================
// MIGRATION MAPPING: stmt_common.cpp
// =============================================================================
// This file should contain common helper functions for statement parsing.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.10, Lines 6255-6431
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.12, Lines 6452-6499
// =============================================================================
//
// OVERVIEW:
// This file consolidates helper functions shared across statement parsing:
// - Terminator handling (ConsumeTerminatorOpt, ConsumeTerminatorReq)
// - Statement sequence parsing (ParseStmtSeq)
// - Block parsing (ParseBlock)
// - Error recovery (SyncStmt)
// - Attribute application (ApplyStmtAttrs, WrapAttrExpr)
// - Place expression validation (IsPlaceExpr)
// - Expression start detection (IsExprStartToken)
// - Block-ending detection (EndsWithBlock)
// - RequiresTerminator predicate
//
// =============================================================================
// TERMINATOR HANDLING
// =============================================================================
//
// FORMAL RULES FROM SPEC (Lines 6360-6390):
// -----------------------------------------------------------------------------
// IsTerm(tok) <=> tok IN {Punctuator(";"), Newline}
// ReqTerm(s) <=> s IN {LetStmt, VarStmt, ShadowLetStmt, ShadowVarStmt,
//                      AssignStmt, CompoundAssignStmt, ExprStmt*}
// *ExprStmt requires terminator UNLESS expression ends with block
//
// **(ConsumeTerminatorOpt-Req-Yes)** Lines 6362-6365
// ReqTerm(s)    IsTerm(Tok(P))
// -> Advance(P)
//
// **(ConsumeTerminatorOpt-Req-No)** Lines 6367-6370
// ReqTerm(s)    NOT IsTerm(Tok(P))
// -> Emit error, SyncStmt(P)
//
// **(ConsumeTerminatorOpt-Opt-Yes)** Lines 6372-6375
// NOT ReqTerm(s)    IsTerm(Tok(P))
// -> Advance(P)
//
// **(ConsumeTerminatorOpt-Opt-No)** Lines 6377-6380
// NOT ReqTerm(s)    NOT IsTerm(Tok(P))
// -> P (no change)
//
// SOURCE CONTENT TO MIGRATE (Lines 34-38, 112-120, 145-159, 834-868):
// -----------------------------------------------------------------------------
// bool IsTerminatorToken(const Token& tok) {
//   return tok.kind == TokenKind::Newline ||
//          (tok.kind == TokenKind::Punctuator && tok.lexeme == ";");
// }
//
// bool EndsWithBlock(const ExprPtr& expr) {
//   if (!expr) return false;
//   if (std::holds_alternative<LoopInfiniteExpr>(expr->node) ||
//       std::holds_alternative<LoopConditionalExpr>(expr->node) ||
//       std::holds_alternative<LoopIterExpr>(expr->node) ||
//       std::holds_alternative<IfCaseExpr>(expr->node) ||
//       std::holds_alternative<BlockExpr>(expr->node) ||
//       std::holds_alternative<UnsafeBlockExpr>(expr->node)) {
//     return true;
//   }
//   if (const auto* if_expr = std::get_if<IfExpr>(&expr->node)) {
//     if (if_expr->else_expr) {
//       return EndsWithBlock(if_expr->else_expr);
//     }
//     return true;
//   }
//   return false;
// }
//
// bool RequiresTerminator(const Stmt& stmt) {
//   if (const auto* expr_stmt = std::get_if<ExprStmt>(&stmt)) {
//     if (EndsWithBlock(expr_stmt->value)) {
//       return false;
//     }
//     return true;
//   }
//   return std::holds_alternative<LetStmt>(stmt) ||
//          std::holds_alternative<VarStmt>(stmt) ||
//          std::holds_alternative<ShadowLetStmt>(stmt) ||
//          std::holds_alternative<ShadowVarStmt>(stmt) ||
//          std::holds_alternative<AssignStmt>(stmt) ||
//          std::holds_alternative<CompoundAssignStmt>(stmt);
// }
//
// void ConsumeTerminatorOpt(Parser& parser, TerminatorPolicy policy) {
//   const Token* tok = Tok(parser);
//   const bool is_term = tok && IsTerminatorToken(*tok);
//
//   if (policy == TerminatorPolicy::Required) {
//     if (is_term) {
//       SPEC_RULE("ConsumeTerminatorOpt-Req-Yes");
//       Advance(parser);
//       return;
//     }
//     SPEC_RULE("ConsumeTerminatorOpt-Req-No");
//     EmitMissingTerminator(parser.diags, TokSpan(parser));
//     SyncStmt(parser);
//     return;
//   }
//
//   if (is_term) {
//     SPEC_RULE("ConsumeTerminatorOpt-Opt-Yes");
//     Advance(parser);
//   } else {
//     SPEC_RULE("ConsumeTerminatorOpt-Opt-No");
//   }
// }
//
// void ConsumeTerminatorReq(Parser& parser) {
//   const Token* tok = Tok(parser);
//   if (tok && IsTerminatorToken(*tok)) {
//     SPEC_RULE("ConsumeTerminatorReq-Yes");
//     Advance(parser);
//     return;
//   }
//   SPEC_RULE("ConsumeTerminatorReq-No");
//   EmitMissingTerminator(parser.diags, TokSpan(parser));
//   SyncStmt(parser);
// }
//
// =============================================================================
// BLOCK PARSING
// =============================================================================
//
// FORMAL RULES FROM SPEC (Lines 6350-6355):
// -----------------------------------------------------------------------------
// **(Parse-Block)** Lines 6352-6355
// IsPunc(Tok(P), "{")
// Gamma |- ParseStmtSeq(Advance(P)) => (P_1, stmts, tail)
// IsPunc(Tok(P_1), "}")
// -> (Advance(P_1), BlockExpr(stmts, tail))
//
// SOURCE CONTENT TO MIGRATE (Lines 902-928):
// -----------------------------------------------------------------------------
// ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser) {
//   // Skip newlines before opening brace
//   while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
//     Advance(parser);
//   }
//   Parser start = parser;
//   if (!IsPunc(parser, "{")) {
//     EmitParseSyntaxErr(parser, TokSpan(parser));
//     Parser next = AdvanceOrEOF(parser);
//     core::Span span = SpanBetween(start, next);
//     return {next, MakeBlockNode(span, {}, nullptr)};
//   }
//
//   SPEC_RULE("Parse-Block");
//   Parser next = parser;
//   Advance(next);  // consume "{"
//   ParseStmtSeqResult seq = ParseStmtSeq(next);
//   if (!IsPunc(seq.parser, "}")) {
//     EmitParseSyntaxErr(seq.parser, TokSpan(seq.parser));
//     core::Span span = SpanBetween(start, seq.parser);
//     return {seq.parser, MakeBlockNode(span, std::move(seq.stmts), seq.tail_opt)};
//   }
//   Parser after = seq.parser;
//   Advance(after);  // consume "}"
//   core::Span span = SpanBetween(start, after);
//   return {after, MakeBlockNode(span, std::move(seq.stmts), seq.tail_opt)};
// }
//
// =============================================================================
// STATEMENT SEQUENCE PARSING
// =============================================================================
//
// FORMAL RULES FROM SPEC (Lines 6416-6431):
// -----------------------------------------------------------------------------
// **(ParseStmtSeq-End)** Lines 6418-6421
// Tok(P) = Punctuator("}")
// -> (P, [], null)
//
// **(ParseStmtSeq-TailExpr)** Lines 6423-6426
// Tok(P) != Punctuator("}")
// Gamma |- ParseExpr(P) => (P_1, e)
// Tok(P_1) = Punctuator("}")
// -> (P_1, [], e)
//
// **(ParseStmtSeq-Cons)** Lines 6428-6431
// Gamma |- ParseStmt(P) => (P_1, s)
// Gamma |- ParseStmtSeq(P_1) => (P_2, ss, tail)
// -> (P_2, [s] ++ ss, tail)
//
// SOURCE CONTENT TO MIGRATE (Lines 735-781):
// -----------------------------------------------------------------------------
// ParseStmtSeqResult ParseStmtSeq(Parser parser) {
//   // Skip leading newlines
//   while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
//     Advance(parser);
//   }
//
//   if (IsPunc(parser, "}")) {
//     SPEC_RULE("ParseStmtSeq-End");
//     return {parser, {}, nullptr};
//   }
//
//   // Try parsing as tail expression
//   Parser probe = Clone(parser);
//   ParseElemResult<ExprPtr> tail = ParseExpr(probe);
//   Parser tail_end = Clone(tail.parser);
//   while (Tok(tail_end) && Tok(tail_end)->kind == TokenKind::Newline) {
//     Advance(tail_end);
//   }
//   if (tail.parser.diags.empty() && IsPunc(tail_end, "}")) {
//     SPEC_RULE("ParseStmtSeq-TailExpr");
//     Parser merged = MergeDiag(parser, tail.parser, tail_end);
//     return {merged, {}, tail.elem};
//   }
//
//   // Parse as statement + rest
//   SPEC_RULE("ParseStmtSeq-Cons");
//   ParseElemResult<Stmt> head = ParseStmt(parser);
//   ParseStmtSeqResult tail_seq = ParseStmtSeq(head.parser);
//   std::vector<Stmt> stmts;
//   stmts.reserve(1 + tail_seq.stmts.size());
//   stmts.push_back(std::move(head.elem));
//   stmts.insert(stmts.end(),
//                std::make_move_iterator(tail_seq.stmts.begin()),
//                std::make_move_iterator(tail_seq.stmts.end()));
//   tail_seq.stmts = std::move(stmts);
//   return tail_seq;
// }
//
// =============================================================================
// ERROR RECOVERY
// =============================================================================
//
// FORMAL RULES FROM SPEC (Lines 6466-6479):
// -----------------------------------------------------------------------------
// **(Sync-Stmt-Stop)** Lines 6466-6469
// Tok(P) IN {Punctuator("}"), EOF}
// -> P
//
// **(Sync-Stmt-Consume)** Lines 6471-6474
// Tok(P) IN {Punctuator(";"), Newline}
// -> Advance(P)
//
// **(Sync-Stmt-Advance)** Lines 6476-6479
// Tok(P) NOT IN SyncStmt
// -> SyncStmt(Advance(P))
//
// SOURCE CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// void SyncStmt(Parser& parser);  // Implementation in parser utilities
//
// =============================================================================
// ATTRIBUTE APPLICATION
// =============================================================================
//
// SOURCE CONTENT TO MIGRATE (Lines 161-224):
// -----------------------------------------------------------------------------
// ExprPtr WrapAttrExpr(const AttributeList& attrs, const ExprPtr& expr) {
//   if (!expr || attrs.empty()) {
//     return expr;
//   }
//   AttributedExpr node;
//   node.attrs = attrs;
//   node.expr = expr;
//   auto out = std::make_shared<Expr>();
//   out->span = expr->span;
//   out->node = std::move(node);
//   return out;
// }
//
// void ApplyStmtAttrs(const AttributeList& attrs, Stmt& stmt) {
//   if (attrs.empty()) return;
//   // Dispatch based on statement type
//   if (auto* let_stmt = std::get_if<LetStmt>(&stmt)) { ... }
//   if (auto* var_stmt = std::get_if<VarStmt>(&stmt)) { ... }
//   if (auto* shadow_let = std::get_if<ShadowLetStmt>(&stmt)) { ... }
//   if (auto* shadow_var = std::get_if<ShadowVarStmt>(&stmt)) { ... }
//   if (auto* assign = std::get_if<AssignStmt>(&stmt)) { ... }
//   if (auto* assign = std::get_if<CompoundAssignStmt>(&stmt)) { ... }
//   if (auto* expr_stmt = std::get_if<ExprStmt>(&stmt)) { ... }
//   if (auto* ret = std::get_if<ReturnStmt>(&stmt)) { ... }
//   if (auto* br = std::get_if<BreakStmt>(&stmt)) { ... }
//   if (auto* region = std::get_if<RegionStmt>(&stmt)) { ... }
//   if (auto* stat = std::get_if<StaticAssertStmt>(&stmt)) { ... }
// }
//
// =============================================================================
// PLACE EXPRESSION VALIDATION
// =============================================================================
//
// SOURCE CONTENT TO MIGRATE (Lines 90-110):
// -----------------------------------------------------------------------------
// bool IsPlaceExpr(const ExprPtr& expr) {
//   if (!expr) return false;
//   if (std::holds_alternative<IdentifierExpr>(expr->node)) return true;
//   if (std::holds_alternative<FieldAccessExpr>(expr->node)) return true;
//   if (std::holds_alternative<TupleAccessExpr>(expr->node)) return true;
//   if (std::holds_alternative<IndexAccessExpr>(expr->node)) return true;
//   if (const auto* deref = std::get_if<DerefExpr>(&expr->node)) {
//     return IsPlaceExpr(deref->value);
//   }
//   return false;
// }
//
// =============================================================================
// EXPRESSION START DETECTION
// =============================================================================
//
// SOURCE CONTENT TO MIGRATE (Lines 55-88):
// -----------------------------------------------------------------------------
// bool IsLiteralToken(const Token& tok) {
//   return tok.kind == TokenKind::IntLiteral ||
//          tok.kind == TokenKind::FloatLiteral ||
//          tok.kind == TokenKind::StringLiteral ||
//          tok.kind == TokenKind::CharLiteral ||
//          tok.kind == TokenKind::BoolLiteral ||
//          tok.kind == TokenKind::NullLiteral;
// }
//
// bool IsExprStartToken(const Token& tok) {
//   if (IsIdentTok(tok) || IsLiteralToken(tok)) return true;
//   if (tok.kind == TokenKind::Punctuator) {
//     return tok.lexeme == "(" || tok.lexeme == "[" || tok.lexeme == "{";
//   }
//   if (tok.kind == TokenKind::Operator) {
//     return tok.lexeme == "!" || tok.lexeme == "-" || tok.lexeme == "&" ||
//            tok.lexeme == "*" || tok.lexeme == "^";
//   }
//   if (tok.kind == TokenKind::Keyword) {
//     return tok.lexeme == "if" ||
//            tok.lexeme == "loop" || tok.lexeme == "unsafe" ||
//            tok.lexeme == "move" || tok.lexeme == "transmute" ||
//            tok.lexeme == "widen" || tok.lexeme == "comptime" ||
//            tok.lexeme == "parallel" || tok.lexeme == "spawn" ||
//            tok.lexeme == "dispatch" || tok.lexeme == "yield" ||
//            tok.lexeme == "sync" || tok.lexeme == "race" ||
//            tok.lexeme == "all";
//   }
//   return false;
// }
//
// =============================================================================
// ASSIGNMENT OPERATOR DETECTION
// =============================================================================
//
// SOURCE CONTENT TO MIGRATE (Lines 39-53):
// -----------------------------------------------------------------------------
// bool IsAssignOp(const Token& tok) {
//   if (tok.kind != TokenKind::Operator) return false;
//   return tok.lexeme == "=" || tok.lexeme == "+=" || tok.lexeme == "-=" ||
//          tok.lexeme == "*=" || tok.lexeme == "/=" || tok.lexeme == "%=";
// }
//
// bool IsCompoundAssignOp(const Token& tok) {
//   if (tok.kind != TokenKind::Operator) return false;
//   return tok.lexeme == "+=" || tok.lexeme == "-=" || tok.lexeme == "*=" ||
//          tok.lexeme == "/=" || tok.lexeme == "%=";
// }
//
// =============================================================================
// DATA STRUCTURES
// =============================================================================
//
// enum class TerminatorPolicy {
//   Required,   // Must have terminator; error if missing
//   Optional    // Consume terminator if present; no error if absent
// };
//
// struct ParseStmtSeqResult {
//   Parser parser;
//   std::vector<Stmt> stmts;
//   ExprPtr tail_opt;
// };
//
// struct ParseStmtCoreResult {
//   Parser parser;
//   Stmt stmt;
//   bool matched = false;  // true if a statement was parsed
// };
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
// - Tok, Advance, TokSpan, AtEof helpers (parser utilities)
// - IsPunc, IsKw, IsOp, IsIdentTok helpers
// - ParseExpr, ParseExprOpt functions (expr/*.cpp)
// - ParseStmt function (dispatches to individual statement parsers)
// - EmitMissingTerminator, EmitParseSyntaxErr (diagnostics)
// - SpanBetween helper function
// - MakeBlockNode helper function
// - All statement AST node types
// - All expression AST node types (for EndsWithBlock checks)
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
// - This file centralizes common statement parsing infrastructure
// - Individual statement files (let_stmt.cpp, etc.) call these helpers
// - ParseStmtCore is the main dispatcher (could live here or separately)
// - Block parsing handles both statement sequences and tail expressions
// - Tail expression detection uses speculative parsing with backtrack
// - Error recovery uses SyncStmt to find statement boundaries
// - Terminator handling is policy-based (required vs optional)
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations from other modules
bool IsPunc(const Parser& parser, std::string_view p);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
void SkipNewlines(Parser& parser);
bool IsExprStartToken(const Token& tok);

// =============================================================================
// Terminator Policy
// =============================================================================

// TerminatorPolicy is defined in parser.h

// =============================================================================
// EndsWithBlock - Check if expression ends with a block
// =============================================================================
//
// Forward declaration - defined in expr_stmt.cpp
bool EndsWithBlock(const ExprPtr& expr);

// =============================================================================
// RequiresTerminator - Check if statement requires terminator
// =============================================================================
//
// SPEC: ReqTerm(s) predicate

bool RequiresTerminator(const Stmt& stmt) {
  if (const auto* expr_stmt = std::get_if<ExprStmt>(&stmt)) {
    return !EndsWithBlock(expr_stmt->value);
  }
  // These statement types require terminators
  return std::holds_alternative<LetStmt>(stmt) ||
         std::holds_alternative<VarStmt>(stmt) ||
         std::holds_alternative<UsingLocalStmt>(stmt) ||
         std::holds_alternative<AssignStmt>(stmt) ||
         std::holds_alternative<CompoundAssignStmt>(stmt);
}

// =============================================================================
// WrapAttrExpr - Wrap expression with attributes
// =============================================================================

ExprPtr WrapAttrExpr(const AttributeList& attrs, const ExprPtr& expr) {
  if (!expr || attrs.empty()) {
    return expr;
  }
  AttributedExpr node;
  node.attrs = attrs;
  node.expr = expr;
  auto out = std::make_shared<Expr>();
  out->span = expr->span;
  out->node = std::move(node);
  return out;
}

// =============================================================================
// MakeBlockNode - Create a block node
// =============================================================================

std::shared_ptr<Block> MakeBlockNode(const core::Span& span,
                                     std::vector<Stmt> stmts,
                                     ExprPtr tail_opt) {
  auto block = std::make_shared<Block>();
  block->span = span;
  block->stmts = std::move(stmts);
  block->tail_opt = tail_opt;
  return block;
}

// =============================================================================
// ParseStmtSeqResult - Result of parsing statement sequence
// =============================================================================

struct ParseStmtSeqResult {
  Parser parser;
  std::vector<Stmt> stmts;
  ExprPtr tail_opt;
};

// Forward declaration
ParseElemResult<Stmt> ParseStmt(Parser parser);
ParseStmtSeqResult ParseStmtSeq(Parser parser);

// =============================================================================
// ParseStmtSeq - Parse sequence of statements
// =============================================================================
//
// SPEC: Lines 6416-6431

ParseStmtSeqResult ParseStmtSeq(Parser parser) {
  std::vector<Stmt> stmts;
  Parser cur = parser;

  for (;;) {
    // Skip leading/inter-statement newlines
    while (Tok(cur) && Tok(cur)->kind == TokenKind::Newline) {
      Advance(cur);
    }

    if (IsPunc(cur, "}")) {
      SPEC_RULE("ParseStmtSeq-End");
      return {cur, std::move(stmts), nullptr};
    }

    const Token* tok = Tok(cur);
    if (tok && IsExprStartToken(*tok)) {
      // Try parsing as tail expression using speculative parsing.
      // Clone parser to avoid polluting diagnostics with speculative parse
      // failures.
      Parser probe = Clone(cur);
      ParseElemResult<ExprPtr> tail = ParseExpr(probe);
      const bool tail_advanced =
          tail.parser.tokens != cur.tokens || tail.parser.index > cur.index;
      const bool tail_is_error =
          tail.elem && std::holds_alternative<ErrorExpr>(tail.elem->node);
      Parser tail_end = Clone(tail.parser);
      while (Tok(tail_end) && Tok(tail_end)->kind == TokenKind::Newline) {
        Advance(tail_end);
      }
      // Only accept as tail expression if parse succeeded without errors
      // AND the result is followed by closing brace.
      if (tail_advanced && !tail_is_error && tail.parser.diags.empty() &&
          IsPunc(tail_end, "}")) {
        SPEC_RULE("ParseStmtSeq-TailExpr");
        Parser merged = MergeDiag(cur, tail.parser, tail_end);
        return {merged, std::move(stmts), tail.elem};
      }
    }

    // Parse one statement and continue.
    SPEC_RULE("ParseStmtSeq-Cons");
    ParseElemResult<Stmt> head = ParseStmt(cur);
    stmts.push_back(std::move(head.elem));
    cur = head.parser;
  }
}

// =============================================================================
// ParseBlock - Parse a block { stmts; tail? }
// =============================================================================
//
// SPEC: Lines 6352-6355

ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser) {
  // Skip newlines before opening brace
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  Parser start = parser;
  if (!IsPunc(parser, "{")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    core::Span span = TokSpan(parser);
    return {parser, MakeBlockNode(span, {}, nullptr)};
  }

  SPEC_RULE("Parse-Block");
  Parser next = parser;
  Advance(next);  // consume "{"

  ParseStmtSeqResult seq = ParseStmtSeq(next);
  if (!IsPunc(seq.parser, "}")) {
    EmitParseSyntaxErr(seq.parser, TokSpan(seq.parser));
    core::Span span = SpanBetween(start, seq.parser);
    return {seq.parser, MakeBlockNode(span, std::move(seq.stmts), seq.tail_opt)};
  }

  Parser after = seq.parser;
  Advance(after);  // consume "}"
  core::Span span = SpanBetween(start, after);
  return {after, MakeBlockNode(span, std::move(seq.stmts), seq.tail_opt)};
}

// =============================================================================
// ApplyStmtAttrs - Apply attributes to statement
// =============================================================================

void ApplyStmtAttrs(const AttributeList& attrs, Stmt& stmt) {
  if (attrs.empty()) return;

  if (auto* let_stmt = std::get_if<LetStmt>(&stmt)) {
    let_stmt->binding.attrs = attrs;
    return;
  }
  if (auto* var_stmt = std::get_if<VarStmt>(&stmt)) {
    var_stmt->binding.attrs = attrs;
    return;
  }
  if (auto* using_local = std::get_if<UsingLocalStmt>(&stmt)) {
    // UsingLocalStmt has no attributes in the AST; attrs are silently dropped.
    (void)using_local;
    return;
  }
  if (auto* assign = std::get_if<AssignStmt>(&stmt)) {
    assign->place = WrapAttrExpr(attrs, assign->place);
    assign->value = WrapAttrExpr(attrs, assign->value);
    return;
  }
  if (auto* compound = std::get_if<CompoundAssignStmt>(&stmt)) {
    compound->place = WrapAttrExpr(attrs, compound->place);
    compound->value = WrapAttrExpr(attrs, compound->value);
    return;
  }
  if (auto* expr_stmt = std::get_if<ExprStmt>(&stmt)) {
    expr_stmt->value = WrapAttrExpr(attrs, expr_stmt->value);
    return;
  }
  if (auto* ret = std::get_if<ReturnStmt>(&stmt)) {
    ret->value_opt = WrapAttrExpr(attrs, ret->value_opt);
    return;
  }
  if (auto* br = std::get_if<BreakStmt>(&stmt)) {
    br->value_opt = WrapAttrExpr(attrs, br->value_opt);
    return;
  }
  if (auto* comptime = std::get_if<ComptimeStmt>(&stmt)) {
    comptime->attrs = attrs;
    return;
  }
  if (auto* key_block = std::get_if<KeyBlockStmt>(&stmt)) {
    key_block->attrs = attrs;
    return;
  }
}

}  // namespace ultraviolet::ast
