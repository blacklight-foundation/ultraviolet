// =============================================================================
// MIGRATION MAPPING: lexer.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 3.2.8 - Operator and Punctuator Lexing (lines 2394-2404)
//   Section 3.2.9 - Maximal-Munch Rule (lines 2405-2445)
//   Section 3.2.11 - Tokenization Small-Step (Lex-Newline at line 2519-2522)
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/lexer.cpp
//   Lines 1-407 (entire file)
//
// DEPENDENCIES:
//   - ultraviolet/include/02_source/lexer.h (NextTokenResult, etc.)
//   - ultraviolet/include/02_source/token.h (Token, TokenKind)
//   - ultraviolet/src/00_core/keywords.cpp (kUltravioletOperators, kUltravioletPunctuators)
//   - ultraviolet/src/00_core/unicode.cpp (IsIdentStart, Utf8Offsets)
//   - ultraviolet/src/02_source/lexer/lexer_ident.cpp (ScanIdentToken)
//   - ultraviolet/src/02_source/lexer/lexer_literals.cpp (ScanStringLiteral, etc.)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// INTERNAL HELPERS (anonymous namespace, source lines 14-191):
//
// 1. IsInRange() - Range check (lines 16-18)
// 2. IsSuppressed() - Check if index in suppressed ranges (lines 20-27)
//    Used for filtering newlines inside comments/literals
//
// 3. IsPunc() - Punctuator token check (lines 29-31)
// 4. BeginsOperand() - Check if token can start operand (lines 33-63)
//    Used for newline filtering heuristics
//    Includes keywords: if, loop, unsafe, comptime, quote, move,
//                       transmute, widen, parallel, spawn, dispatch,
//                       yield, sync, race, all
//
// 5. IsAmbigOp() - Ambiguous operator check (lines 65-68)
//    Operators that can be binary or unary: +, -, *, &, |
//
// 6. IsUnaryOnly() - Unary-only operator check (lines 70-72)
//    Operators: !, ~, ?
//
// 7. DeltaDepth() - Bracket depth delta (lines 74-85)
//    Returns +1 for (, [, {; -1 for ), ], }
//
// 8. Candidate struct - Lexer candidate (lines 87-91)
//    {kind, next, diags}
//
// 9. KindPriority() - Token kind priority for disambiguation (lines 93-112)
//    Implements spec KindPriority from lines 2419-2428:
//    - Literals: priority 3
//    - Identifier/Keyword: priority 2
//    - Operator: priority 1
//    - Punctuator: priority 0
//
// 10. IsDecDigit() - Decimal digit check (lines 114-116)
// 11. IsQuote() - Quote character check (lines 118-120)
//
// 12. MatchLexeme() - Match prefix against scalars (lines 150-163)
//
// 13. AppendOpCandidates() - Add operator candidates (lines 165-176)
//     Implements spec OpMatch(T, i) from line 2399
//     Iterates kUltravioletOperators, matches prefix
//
// 14. AppendPuncCandidates() - Add punctuator candidates (lines 178-189)
//     Implements spec PuncMatch(T, i) from line 2400
//     Iterates kUltravioletPunctuators, matches prefix
//
// MAIN FUNCTIONS:
//
// 15. NextToken() - Maximal munch token selection (lines 193-290)
//     Implements spec Max-Munch rule (lines 2432-2435):
//       PickLongest(C) = argmax_{(k, j) in C} <j, KindPriority(k)>
//
//     Steps:
//       a. Build candidate set based on first character:
//          - Quote: try string/char literals
//          - Digit: try float/int literals
//          - IdentStart: try identifier token
//          - Otherwise: try operators/punctuators
//       b. If no candidates: emit E-SRC-0309 (Max-Munch-Err)
//       c. Select longest match, break ties by KindPriority
//
// 16. LexNewlines() - Extract newline tokens (lines 292-318)
//     Implements spec Lex-Newline from line 2519-2522
//     Filters newlines in suppressed ranges (inside literals/comments)
//
// 17. FilterNewlines() - Newline continuation filtering (lines 320-405)
//     Implements SPECIFICATION.md §3.1.7 Continue(K, i) / Filter(K)
//
//     Filtering rules:
//     a. Inside ( ) or [ ]: filter (expression context)
//     b. Inside { }: keep (statement context)
//     c. After comma: filter (continuation)
//     d. After non-unary operators: filter; `..` / `..=` additionally require
//        the following token to begin an operand
//     e. Before ., ::, ~>: filter (method chaining)
//     f. Between } and else: filter (if/else continuation)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// - SPEC line 2112 defines OperatorSet (44 operators)
// - SPEC line 2115 defines PunctuatorSet (10 punctuators)
// - SPEC line 2117: OperatorSet INTERSECT PunctuatorSet = empty
//
// - Candidate generation follows spec Candidates(T, i) at line 2410-2415:
//   - IsQuote: StringTok + CharTok
//   - DecDigit: FloatTok + IntTok
//   - IdentStart: IdentToken
//   - Otherwise: OpTok + PuncTok
//
// - PickLongest implements spec Longest() from line 2417
// - KindPriority provides stable ordering for tie-breaking
//
// - FilterNewlines follows the normative Continue(K, i) / Filter(K) rules in
//   SPECIFICATION.md §3.1.7.
//
// - Tuple/decimal-dot disambiguation for `t.0.0` is handled in tokenization
//   (`LexSmallStep`) using previous-token context.
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/keywords.h"
#include "00_core/source_text.h"
#include "00_core/span.h"
#include "00_core/unicode.h"

namespace ultraviolet::lexer {

namespace {

bool IsInRange(std::size_t index, const ScalarRange& range) {
  return index >= range.start && index < range.end;
}

bool IsSuppressed(std::size_t index, const std::vector<ScalarRange>& ranges) {
  for (const auto& range : ranges) {
    if (IsInRange(index, range)) {
      return true;
    }
  }
  return false;
}

bool IsPunc(const Token& tok, std::string_view lexeme) {
  return tok.kind == TokenKind::Punctuator && tok.lexeme == lexeme;
}

bool IsKw(const Token& tok, std::string_view lexeme) {
  return tok.kind == TokenKind::Keyword && tok.lexeme == lexeme;
}

bool Adjacent(const Token& left, const Token& right) {
  return left.span.file == right.span.file &&
         left.span.end_offset == right.span.start_offset;
}

bool IsAdjacentPuncPair(const Token& left,
                        const Token& right,
                        std::string_view left_lexeme,
                        std::string_view right_lexeme) {
  return IsPunc(left, left_lexeme) && IsPunc(right, right_lexeme) &&
         Adjacent(left, right);
}

bool BeginsOperand(const Token& tok) {
  switch (tok.kind) {
    case TokenKind::Identifier:
    case TokenKind::IntLiteral:
    case TokenKind::FloatLiteral:
    case TokenKind::StringLiteral:
    case TokenKind::CharLiteral:
    case TokenKind::BoolLiteral:
    case TokenKind::NullLiteral:
      return true;
    case TokenKind::Punctuator:
      return tok.lexeme == "(" || tok.lexeme == "[" || tok.lexeme == "{";
    case TokenKind::Operator:
      return tok.lexeme == "!" || tok.lexeme == "-" || tok.lexeme == "&" ||
             tok.lexeme == "*" || tok.lexeme == "^";
    case TokenKind::Keyword:
      return tok.lexeme == "if" ||
             tok.lexeme == "loop" || tok.lexeme == "unsafe" ||
             tok.lexeme == "comptime" || tok.lexeme == "quote" ||
             tok.lexeme == "move" || tok.lexeme == "transmute" ||
             tok.lexeme == "widen" ||
             // UVX Extension: Structured Concurrency
             tok.lexeme == "parallel" || tok.lexeme == "spawn" ||
             tok.lexeme == "dispatch" ||
             // UVX Extension: Async expressions
             tok.lexeme == "yield" || tok.lexeme == "sync" ||
             tok.lexeme == "race" || tok.lexeme == "all";
    default:
      return false;
  }
}

bool IsAmbigOp(std::string_view lexeme) {
  return lexeme == "+" || lexeme == "-" || lexeme == "*" || lexeme == "&" ||
         lexeme == "|";
}

bool IsRangeContOp(std::string_view lexeme) {
  return lexeme == ".." || lexeme == "..=";
}

bool IsUnaryOnly(std::string_view lexeme) {
  return lexeme == "!" || lexeme == "~" || lexeme == "?";
}

int DeltaDepth(const Token& tok) {
  if (tok.kind != TokenKind::Punctuator) {
    return 0;
  }
  if (tok.lexeme == "(" || tok.lexeme == "[") {
    return 1;
  }
  if (tok.lexeme == ")" || tok.lexeme == "]") {
    return -1;
  }
  return 0;
}

struct Candidate {
  TokenKind kind = TokenKind::Unknown;
  std::size_t next = 0;
  core::DiagnosticStream diags;
};

struct NewlineContext {
  std::vector<int> paren_depth;
  std::vector<int> bracket_depth;
  std::vector<std::size_t> prev_index;
  std::vector<std::size_t> next_index;
};

int KindPriority(TokenKind kind) {
  switch (kind) {
    case TokenKind::IntLiteral:
    case TokenKind::FloatLiteral:
    case TokenKind::StringLiteral:
    case TokenKind::CharLiteral:
    case TokenKind::BoolLiteral:
    case TokenKind::NullLiteral:
      return 3;
    case TokenKind::Identifier:
    case TokenKind::Keyword:
      return 2;
    case TokenKind::Operator:
      return 1;
    case TokenKind::Punctuator:
      return 0;
    default:
      return -1;
  }
}

NewlineContext BuildNewlineContext(const std::vector<Token>& tokens) {
  const std::size_t n = tokens.size();
  NewlineContext ctx;
  ctx.paren_depth.assign(n + 1, 0);
  ctx.bracket_depth.assign(n + 1, 0);
  for (std::size_t i = 0; i < n; ++i) {
    ctx.paren_depth[i + 1] = ctx.paren_depth[i];
    ctx.bracket_depth[i + 1] = ctx.bracket_depth[i];
    if (tokens[i].kind == TokenKind::Punctuator) {
      if (tokens[i].lexeme == "(") ctx.paren_depth[i + 1]++;
      else if (tokens[i].lexeme == ")") ctx.paren_depth[i + 1]--;
      else if (tokens[i].lexeme == "[") ctx.bracket_depth[i + 1]++;
      else if (tokens[i].lexeme == "]") ctx.bracket_depth[i + 1]--;
    }
  }

  ctx.prev_index.assign(n, static_cast<std::size_t>(-1));
  std::size_t prev_non_newline = static_cast<std::size_t>(-1);
  for (std::size_t i = 0; i < n; ++i) {
    ctx.prev_index[i] = prev_non_newline;
    if (tokens[i].kind != TokenKind::Newline) {
      prev_non_newline = i;
    }
  }

  ctx.next_index.assign(n, static_cast<std::size_t>(-1));
  std::size_t next_non_newline = static_cast<std::size_t>(-1);
  for (std::size_t i = n; i-- > 0;) {
    ctx.next_index[i] = next_non_newline;
    if (tokens[i].kind != TokenKind::Newline) {
      next_non_newline = i;
    }
  }

  return ctx;
}

bool ContinuesLineImpl(const std::vector<Token>& tokens,
                       std::size_t i,
                       const NewlineContext& ctx) {
  if (i >= tokens.size() || tokens[i].kind != TokenKind::Newline) {
    return false;
  }

  bool cont = false;

  if (ctx.paren_depth[i] > 0 || ctx.bracket_depth[i] > 0) {
    cont = true;
  }

  if (!cont && ctx.prev_index[i] != static_cast<std::size_t>(-1)) {
    const Token& prev = tokens[ctx.prev_index[i]];
    if (IsPunc(prev, ",")) {
      cont = true;
    } else if (prev.kind == TokenKind::Operator) {
      if ((IsAmbigOp(prev.lexeme) || IsRangeContOp(prev.lexeme)) &&
          ctx.next_index[i] != static_cast<std::size_t>(-1)) {
        const Token& next = tokens[ctx.next_index[i]];
        if (BeginsOperand(next)) {
          cont = true;
        }
      }
      if (!cont && !IsUnaryOnly(prev.lexeme) &&
          !IsRangeContOp(prev.lexeme)) {
        cont = true;
      }
    }
  }

  if (!cont && ctx.prev_index[i] != static_cast<std::size_t>(-1) &&
      ctx.next_index[i] != static_cast<std::size_t>(-1)) {
    const std::size_t close_right_index = ctx.prev_index[i];
    const std::size_t close_left_index =
        close_right_index < ctx.prev_index.size()
            ? ctx.prev_index[close_right_index]
            : static_cast<std::size_t>(-1);
    if (close_left_index != static_cast<std::size_t>(-1) &&
        IsAdjacentPuncPair(tokens[close_left_index],
                           tokens[close_right_index],
                           "]",
                           "]")) {
      const Token& next = tokens[ctx.next_index[i]];
      if (BeginsOperand(next)) {
        cont = true;
      }
    }
  }

  if (!cont && ctx.prev_index[i] != static_cast<std::size_t>(-1) &&
      ctx.next_index[i] != static_cast<std::size_t>(-1)) {
    const Token& prev = tokens[ctx.prev_index[i]];
    const Token& next = tokens[ctx.next_index[i]];
    if (IsPunc(prev, "}") && IsKw(next, "else")) {
      SPEC_RULE("req.ElseContinuationAcrossNewline");
      cont = true;
    }
  }

  if (!cont && ctx.next_index[i] != static_cast<std::size_t>(-1)) {
    const Token& next = tokens[ctx.next_index[i]];
    if (next.lexeme == "." || next.lexeme == "::" || next.lexeme == "~>") {
      cont = true;
    }
  }

  return cont;
}

bool RequiredTerminatorImpl(const std::vector<Token>& tokens,
                            std::size_t i,
                            const NewlineContext& ctx) {
  return i < tokens.size() &&
         tokens[i].kind == TokenKind::Newline &&
         !ContinuesLineImpl(tokens, i, ctx);
}

bool IsDecDigit(core::UnicodeScalar c) {
  return c >= '0' && c <= '9';
}

bool IsQuote(core::UnicodeScalar c) {
  return c == '"' || c == '\'';
}

bool MatchLexeme(const std::vector<core::UnicodeScalar>& scalars,
                 std::size_t start,
                 std::string_view lexeme) {
  const std::size_t n = scalars.size();
  if (start + lexeme.size() > n) {
    return false;
  }
  for (std::size_t i = 0; i < lexeme.size(); ++i) {
    if (scalars[start + i] != static_cast<unsigned char>(lexeme[i])) {
      return false;
    }
  }
  return true;
}

void AppendOpCandidates(const std::vector<core::UnicodeScalar>& scalars,
                        std::size_t start,
                        std::vector<Candidate>& out) {
  for (std::string_view op : core::kUltravioletOperators) {
    if (MatchLexeme(scalars, start, op)) {
      Candidate cand;
      cand.kind = TokenKind::Operator;
      cand.next = start + op.size();
      out.push_back(cand);
    }
  }
}

void AppendPuncCandidates(const std::vector<core::UnicodeScalar>& scalars,
                          std::size_t start,
                          std::vector<Candidate>& out) {
  for (std::string_view punc : core::kUltravioletPunctuators) {
    if (MatchLexeme(scalars, start, punc)) {
      Candidate cand;
      cand.kind = TokenKind::Punctuator;
      cand.next = start + punc.size();
      out.push_back(cand);
    }
  }
}

}  // namespace

NextTokenResult NextToken(const core::SourceFile& source,
                          std::size_t start) {
  SPEC_RULE("Max-Munch");
  SPEC_RULE("Max-Munch-Err");
  NextTokenResult result;
  result.ok = false;
  result.next = start;
  result.kind = TokenKind::Unknown;

  const auto& scalars = source.scalars;
  if (start >= scalars.size()) {
    return result;
  }

  std::vector<Candidate> candidates;
  const core::UnicodeScalar first = scalars[start];

  if (IsQuote(first)) {
    LiteralScanResult str = ScanStringLiteral(source, start);
    if (str.ok) {
      Candidate cand;
      cand.kind = TokenKind::StringLiteral;
      cand.next = str.next;
      cand.diags = str.diags;
      candidates.push_back(cand);
    }
    LiteralScanResult chr = ScanCharLiteral(source, start);
    if (chr.ok) {
      Candidate cand;
      cand.kind = TokenKind::CharLiteral;
      cand.next = chr.next;
      cand.diags = chr.diags;
      candidates.push_back(cand);
    }
  } else if (IsDecDigit(first)) {
    LiteralScanResult flt = ScanFloatLiteral(source, start);
    // Float candidate participates directly when the float scanner consumes
    // input, including malformed numeric forms that carry diagnostics.
    // ScanFloatLiteral is responsible for disambiguating range punctuation
    // (e.g., `2..=5`) from decimal float forms.
    if (flt.ok || flt.next > start) {
      Candidate cand;
      cand.kind = TokenKind::FloatLiteral;
      cand.next = flt.next;
      cand.diags = flt.diags;
      candidates.push_back(cand);
    }
    LiteralScanResult integer = ScanIntLiteral(source, start);
    if (integer.ok || integer.next > start) {
      Candidate cand;
      cand.kind = TokenKind::IntLiteral;
      cand.next = integer.next;
      cand.diags = integer.diags;
      candidates.push_back(cand);
    }
  } else if (core::IsIdentStart(first)) {
    IdentScanResult ident = ScanIdentToken(source, start);
    if (ident.ok) {
      Candidate cand;
      cand.kind = ident.kind;
      cand.next = ident.next;
      cand.diags = ident.diags;
      candidates.push_back(cand);
    }
  } else {
    AppendOpCandidates(scalars, start, candidates);
    AppendPuncCandidates(scalars, start, candidates);
  }

  if (candidates.empty()) {
    const auto offsets = core::Utf8Offsets(scalars);
    if (start + 1 < offsets.size()) {
      const std::size_t start_offset = offsets[start];
      const std::size_t end_offset = offsets[start + 1];
      const auto span = core::SpanOf(source, start_offset, end_offset);
      if (auto diag = core::MakeDiagnosticById("E-SRC-0309", span)) {
        core::Emit(result.diags, *diag);
      }
    }
    return result;
  }

  const Candidate* best = &candidates[0];
  for (const Candidate& cand : candidates) {
    if (cand.next > best->next) {
      best = &cand;
      continue;
    }
    if (cand.next == best->next &&
        KindPriority(cand.kind) > KindPriority(best->kind)) {
      best = &cand;
    }
  }

  result.ok = true;
  result.kind = best->kind;
  result.next = best->next;
  result.diags = best->diags;
  return result;
}

std::vector<Token> LexNewlines(const core::SourceFile& source,
                               const std::vector<ScalarRange>& suppressed) {
  SPEC_RULE("Lex-Newline");
  const auto& scalars = source.scalars;
  std::vector<Token> out;
  if (scalars.empty()) {
    return out;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    if (scalars[i] != core::kLF) {
      continue;
    }
    if (IsSuppressed(i, suppressed)) {
      continue;
    }
    const std::size_t start = offsets[i];
    const std::size_t end = offsets[i + 1];
    Token tok;
    tok.kind = TokenKind::Newline;
    tok.lexeme = "\n";
    tok.span = core::SpanOf(source, start, end);
    out.push_back(tok);
  }
  return out;
}

std::vector<Token> FilterNewlines(const std::vector<Token>& tokens) {
  const std::size_t n = tokens.size();
  const NewlineContext ctx = BuildNewlineContext(tokens);

  std::vector<Token> out;
  out.reserve(tokens.size());

  for (std::size_t i = 0; i < n; ++i) {
    const Token& tok = tokens[i];
    if (tok.kind != TokenKind::Newline) {
      out.push_back(tok);
      continue;
    }
    if (RequiredTerminatorImpl(tokens, i, ctx)) {
      out.push_back(tok);
    }
  }

  return out;
}

bool RequiredTerminator(const std::vector<Token>& tokens,
                        std::size_t index) {
  return RequiredTerminatorImpl(tokens, index, BuildNewlineContext(tokens));
}

bool ContinuesLine(const std::vector<Token>& tokens,
                   std::size_t index) {
  return ContinuesLineImpl(tokens, index, BuildNewlineContext(tokens));
}

}  // namespace ultraviolet::lexer
