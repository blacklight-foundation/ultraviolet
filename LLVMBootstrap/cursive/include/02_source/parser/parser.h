#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostics.h"
#include "00_core/source_text.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer.h"
#include "02_source/token.h"

namespace cursive::ast {

enum class TerminatorPolicy {
  Required,
  Optional,
};

struct Parser {
  const std::vector<Token>* tokens = nullptr;
  std::shared_ptr<std::vector<Token>> owned_tokens;
  const core::SourceFile* source = nullptr;
  std::size_t index = 0;
  const std::vector<DocComment>* docs = nullptr;
  std::size_t doc_index = 0;
  std::size_t depth = 0;
  bool quote_mode = false;
  bool stop_before_parallel_options = false;
  bool stop_before_contract_arrow = false;
  core::DiagnosticStream diags;
};

Parser MakeParser(const std::vector<Token>& tokens,
                  const std::vector<DocComment>& docs,
                  const core::SourceFile& source);

Parser MakeParser(const std::vector<Token>& tokens,
                  const core::SourceFile& source);

bool AtEof(const Parser& parser);
const Token* Tok(const Parser& parser);
const core::Span& TokSpan(const Parser& parser);
std::pair<std::size_t, std::size_t> TokensBetween(const Parser& start,
                                                  const Parser& end);
void Advance(Parser& parser);
Parser AdvanceOrEOF(const Parser& parser);

Parser Clone(const Parser& parser);
Parser MergeDiag(const Parser& base, const Parser& diag, const Parser& src);

bool PStateOk(const Parser& parser);
core::Span SpanBetween(const Parser& start, const Parser& end);

std::pair<core::Span, core::Span> SplitSpan2(const core::Span& sp);

Parser SplitShiftR(const Parser& parser);

int AngleDelta(const Token& tok);

struct AngleStepResult {
  Parser parser;
  int depth = 0;
};

AngleStepResult AngleStep(const Parser& parser, int depth);

Parser AngleScan(const Parser& start, const Parser& parser, int depth);

Parser SkipAngles(const Parser& parser);

void SyncStmt(Parser& parser);
void SyncItem(Parser& parser);
void SyncType(Parser& parser);
void EmitParseSyntaxErr(Parser& parser, const core::Span& span);
void EmitGenericParseSyntaxErr(Parser& parser, const core::Span& span);
void EmitSpliceOutsideQuoteErr(Parser& parser, const core::Span& span);

struct TokenKindMatch {
  TokenKind kind = TokenKind::Unknown;
  std::string_view lexeme;
};

inline TokenKindMatch MatchKind(TokenKind kind) { return {kind, {}}; }
inline TokenKindMatch MatchKeyword(std::string_view s) {
  return {TokenKind::Keyword, s};
}
inline TokenKindMatch MatchOperator(std::string_view s) {
  return {TokenKind::Operator, s};
}
inline TokenKindMatch MatchPunct(std::string_view s) {
  return {TokenKind::Punctuator, s};
}

// Models the spec's EndSet ⊆ TokenKind relation for list terminators.
struct EndSetToken {
  TokenKind kind = TokenKind::Unknown;
  std::string_view lexeme;
};

inline EndSetToken EndKind(TokenKind kind) { return {kind, {}}; }
inline EndSetToken EndKeyword(std::string_view s) {
  return {TokenKind::Keyword, s};
}
inline EndSetToken EndOperator(std::string_view s) {
  return {TokenKind::Operator, s};
}
inline EndSetToken EndPunct(std::string_view s) {
  return {TokenKind::Punctuator, s};
}

struct ConsumePendingState {
  Parser parser;
  TokenKindMatch expected;
};

struct ConsumeDoneState {
  Parser parser;
};

using ConsumeState = std::variant<ConsumePendingState, ConsumeDoneState>;

inline ConsumeState Consume(Parser parser, TokenKindMatch expected) {
  return ConsumePendingState{std::move(parser), expected};
}

inline ConsumeDoneState ConsumeDone(Parser parser) {
  return ConsumeDoneState{std::move(parser)};
}

std::optional<ConsumeDoneState> TryAdvanceConsume(
    const ConsumePendingState& state);
bool TokenMatches(const Token& tok, const TokenKindMatch& match);
bool TokenMatches(const Token& tok, const EndSetToken& match);
bool TokenInEndSet(const Token& tok, std::span<const TokenKindMatch> end_set);
bool TokenInEndSet(const Token& tok, std::span<const EndSetToken> end_set);
void RecordListStart();
void RecordListCons();
bool ListDone(const Parser& parser, std::span<const TokenKindMatch> end_set);
bool ListDone(const Parser& parser, std::span<const EndSetToken> end_set);

bool ConsumeKind(Parser& parser, TokenKind kind);
bool ConsumeKeyword(Parser& parser, std::string_view keyword);
bool ConsumeOperator(Parser& parser, std::string_view op);
bool ConsumePunct(Parser& parser, std::string_view punct);

bool TrailingComma(const Parser& parser, std::span<const EndSetToken> end_set);
bool TrailingCommaAllowed(const Parser& parser,
                          std::span<const EndSetToken> end_set);
bool EmitTrailingCommaErr(Parser& parser,
                          std::span<const EndSetToken> end_set);

void ConsumeTerminatorOpt(Parser& parser, TerminatorPolicy policy);
void ConsumeTerminatorReq(Parser& parser);

struct ParseItemResult {
  Parser parser;
  ASTItem item;
};

struct ParseItemsResult {
  Parser parser;
  std::vector<ASTItem> items;
  std::vector<DocComment> module_doc;
};

struct ParseFileResult {
  std::optional<ASTFile> file;
  std::vector<core::Span> unsafe_spans;
  core::DiagnosticStream diags;
};

const std::vector<DocComment>& DocSeq(const std::vector<DocComment>& docs);
std::vector<ASTItem> ItemSeq(std::vector<ASTItem> items);
std::vector<DocComment> ModuleDocs(const std::vector<DocComment>& docs);
void AttachLineDocs(std::vector<ASTItem>& items,
                    const std::vector<DocComment>& docs);

ParseItemResult ParseItem(Parser parser);
ParseItemsResult ParseItems(Parser parser);
ParseFileResult ParseFile(const core::SourceFile& source);

bool ParseFileBestEffort(const ParseFileResult& result);
bool ParseFileOk(const ParseFileResult& result);

template <typename Elem>
struct ParseElemResult {
  Parser parser;
  Elem elem;
};

ParseElemResult<Identifier> ParseIdent(Parser parser);
struct ParseLocalIdentResult {
  Parser parser;
  Identifier name;
  std::optional<SpliceIdentNode> splice_opt;
};

ParseLocalIdentResult ParseLocalIdent(Parser parser);
ParseElemResult<ModulePath> ParseModulePath(Parser parser);
ParseElemResult<TypePath> ParseTypePath(Parser parser);
ParseElemResult<ClassPath> ParseClassPath(Parser parser);

// Result type for speculative pattern-in parsing (used by loop_iter.cpp)
struct TryPatternInResult {
  Parser parser;
  std::shared_ptr<Pattern> pattern;
  bool ok = false;
};

TryPatternInResult TryParsePatternIn(Parser parser);

struct ParseQualifiedHeadResult {
  Parser parser;
  ModulePath module_path;
  Identifier name;
};

ParseQualifiedHeadResult ParseQualifiedHead(Parser parser);

ParseElemResult<Visibility> ParseVis(Parser parser);
ParseElemResult<bool> ParseKeyBoundaryOpt(Parser parser);
ParseElemResult<bool> ParseModalOpt(Parser parser);
ParseElemResult<std::optional<Identifier>> ParseAliasOpt(Parser parser);

ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseTypeNoUnion(Parser parser);
ParseElemResult<std::shared_ptr<Type>> ParseTypeAnnotOpt(Parser parser);
ParseElemResult<std::shared_ptr<Expr>> ParseExpr(Parser parser);
ParseElemResult<std::shared_ptr<Expr>> ParseExprOpt(Parser parser);
ParseElemResult<std::shared_ptr<Expr>> ParsePredicateExpr(Parser parser);
ParseElemResult<Binding> ParseBindingAfterLetVar(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<Stmt> ParseShadowBinding(Parser parser);
ParseElemResult<Stmt> ParseStmt(Parser parser);

enum class ListStateTag : std::uint8_t {
  Start,
  Scan,
  Done,
};

template <typename Elem>
struct ListState {
  ListStateTag tag = ListStateTag::Start;
  Parser parser;
  std::vector<Elem> elems;
};

template <typename Elem>
inline void EnsureListScan(ListState<Elem>& state) {
  if (state.tag == ListStateTag::Start) {
    RecordListStart();
    state.tag = ListStateTag::Scan;
  }
}

template <typename Elem>
inline ListState<Elem> ListStart(Parser parser) {
  ListState<Elem> state;
  state.tag = ListStateTag::Start;
  state.parser = parser;
  return state;
}

template <typename Elem>
inline ListState<Elem> MakeListScanState(Parser parser,
                                         std::vector<Elem> elems) {
  ListState<Elem> next;
  next.tag = ListStateTag::Scan;
  next.parser = parser;
  next.elems = std::move(elems);
  return next;
}

template <typename Elem, typename ParseElemFn>
inline ListState<Elem> ListCons(ListState<Elem> state,
                                ParseElemFn parse_elem) {
  EnsureListScan(state);
  if (state.tag != ListStateTag::Scan) {
    return state;
  }
  RecordListCons();
  ParseElemResult<Elem> parsed = parse_elem(state.parser);
  std::vector<Elem> elems = std::move(state.elems);
  elems.push_back(std::move(parsed.elem));
  return MakeListScanState(parsed.parser, std::move(elems));
}

template <typename Elem>
inline ListState<Elem> ListSeed(Parser parser, Elem elem) {
  RecordListStart();
  RecordListCons();
  ListState<Elem> state;
  state.tag = ListStateTag::Scan;
  state.parser = parser;
  state.elems.push_back(std::move(elem));
  return state;
}

template <typename Elem>
inline bool ListDone(ListState<Elem>& state,
                     std::span<const TokenKindMatch> end_set) {
  EnsureListScan(state);
  if (state.tag == ListStateTag::Done) {
    return true;
  }
  if (!ListDone(state.parser, end_set)) {
    return false;
  }
  state.tag = ListStateTag::Done;
  return true;
}

template <typename Elem>
inline bool ListDone(ListState<Elem>& state,
                     std::span<const EndSetToken> end_set) {
  EnsureListScan(state);
  if (state.tag == ListStateTag::Done) {
    return true;
  }
  if (!ListDone(state.parser, end_set)) {
    return false;
  }
  state.tag = ListStateTag::Done;
  return true;
}

}  // namespace cursive::ast
