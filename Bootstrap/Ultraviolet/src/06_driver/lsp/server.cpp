#include "06_driver/lsp/server.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/source_load.h"
#include "02_source/lexer.h"
#include "06_driver/lsp/diagnostic_adapter.h"
#include "06_driver/lsp/protocol.h"
#include "06_driver/lsp/semantic_tokens.h"
#include "06_driver/tooling/line_index.h"
#include "06_driver/tooling/uri.h"

namespace ultraviolet::driver::lsp {

namespace {

llvm::json::Object JsonRpcBase() {
  llvm::json::Object out;
  out["jsonrpc"] = "2.0";
  return out;
}

std::optional<std::string> TextDocumentUri(const llvm::json::Object* params) {
  if (params == nullptr) {
    return std::nullopt;
  }
  const auto* text_document = params->getObject("textDocument");
  if (text_document == nullptr) {
    return std::nullopt;
  }
  return GetString(*text_document, "uri");
}

std::optional<tooling::LinePosition> PositionParam(
    const llvm::json::Object* params) {
  if (params == nullptr) {
    return std::nullopt;
  }
  const auto* position = params->getObject("position");
  if (position == nullptr) {
    return std::nullopt;
  }
  const auto line = GetInteger(*position, "line");
  const auto character = GetInteger(*position, "character");
  if (!line.has_value() || !character.has_value() || *line < 0 ||
      *character < 0) {
    return std::nullopt;
  }
  return tooling::LinePosition{static_cast<std::size_t>(*line),
                               static_cast<std::size_t>(*character)};
}

bool IsIdentByte(unsigned char ch) {
  return std::isalnum(ch) != 0 || ch == '_' || ch >= 0x80;
}

struct WordRange {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string text;
};

std::optional<WordRange> WordAt(std::string_view text, std::size_t offset) {
  if (text.empty()) {
    return std::nullopt;
  }
  std::size_t pos = std::min(offset, text.size() - 1);
  if (!IsIdentByte(static_cast<unsigned char>(text[pos])) && pos > 0 &&
      IsIdentByte(static_cast<unsigned char>(text[pos - 1]))) {
    --pos;
  }
  if (!IsIdentByte(static_cast<unsigned char>(text[pos]))) {
    return std::nullopt;
  }
  std::size_t start = pos;
  while (start > 0 &&
         IsIdentByte(static_cast<unsigned char>(text[start - 1]))) {
    --start;
  }
  std::size_t end = pos + 1;
  while (end < text.size() &&
         IsIdentByte(static_cast<unsigned char>(text[end]))) {
    ++end;
  }
  return WordRange{start, end, std::string(text.substr(start, end - start))};
}

llvm::json::Object LocationForSymbol(const analysis::LanguageSymbolInfo& symbol,
                                     const std::string& text) {
  tooling::LineIndex index(text);
  llvm::json::Object location;
  location["uri"] = tooling::PathToFileUri(symbol.selection_range.file);
  location["range"] = RangeJson(index.RangeFor(symbol.selection_range));
  return location;
}

bool ContainsText(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

std::string LowerAscii(std::string_view text) {
  std::string out(text);
  for (char& ch : out) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return out;
}

bool NameMatchesQuery(std::string_view name, std::string_view query) {
  if (query.empty()) {
    return true;
  }
  return ContainsText(LowerAscii(name), LowerAscii(query));
}

llvm::json::Object DiagnosticAtStart(std::string message) {
  llvm::json::Object diagnostic;
  diagnostic["range"] = RangeJson(tooling::LineRange{});
  diagnostic["severity"] = static_cast<std::int64_t>(1);
  diagnostic["source"] = "ultraviolet";
  diagnostic["message"] = std::move(message);
  return diagnostic;
}

int CompletionKindForSymbol(analysis::LanguageSymbolKind kind) {
  switch (kind) {
    case analysis::LanguageSymbolKind::Function:
      return 3;
    case analysis::LanguageSymbolKind::Method:
      return 2;
    case analysis::LanguageSymbolKind::Record:
    case analysis::LanguageSymbolKind::Class:
      return 7;
    case analysis::LanguageSymbolKind::Enum:
      return 13;
    case analysis::LanguageSymbolKind::Field:
      return 5;
    case analysis::LanguageSymbolKind::EnumMember:
      return 20;
    case analysis::LanguageSymbolKind::Variable:
    case analysis::LanguageSymbolKind::Parameter:
      return 6;
    case analysis::LanguageSymbolKind::Constant:
      return 21;
    case analysis::LanguageSymbolKind::Modal:
    case analysis::LanguageSymbolKind::TypeAlias:
    case analysis::LanguageSymbolKind::State:
      return 25;
    case analysis::LanguageSymbolKind::Module:
      return 9;
  }
  return 1;
}

llvm::json::Object TextEdit(const tooling::LineRange& range,
                            std::string new_text) {
  llvm::json::Object edit;
  edit["range"] = RangeJson(range);
  edit["newText"] = std::move(new_text);
  return edit;
}

std::optional<tooling::LineRange> RangeFromJson(
    const llvm::json::Object* range) {
  if (range == nullptr) {
    return std::nullopt;
  }
  const auto* start = range->getObject("start");
  const auto* end = range->getObject("end");
  if (start == nullptr || end == nullptr) {
    return std::nullopt;
  }
  const auto start_line = GetInteger(*start, "line");
  const auto start_character = GetInteger(*start, "character");
  const auto end_line = GetInteger(*end, "line");
  const auto end_character = GetInteger(*end, "character");
  if (!start_line.has_value() || !start_character.has_value() ||
      !end_line.has_value() || !end_character.has_value() ||
      *start_line < 0 || *start_character < 0 || *end_line < 0 ||
      *end_character < 0) {
    return std::nullopt;
  }
  return tooling::LineRange{
      tooling::LinePosition{static_cast<std::size_t>(*start_line),
                            static_cast<std::size_t>(*start_character)},
      tooling::LinePosition{static_cast<std::size_t>(*end_line),
                            static_cast<std::size_t>(*end_character)},
  };
}

std::optional<lexer::LexerOutput> TokenizeText(
    const std::filesystem::path& path,
    const std::string& text) {
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  core::SourceLoadResult loaded =
      core::LoadSource(path.generic_string(), bytes);
  if (!loaded.source.has_value()) {
    return std::nullopt;
  }
  lexer::TokenizeDiagnosticResult tokenized =
      lexer::TokenizeWithDiagnostics(*loaded.source);
  return tokenized.output;
}

bool IsCallableSymbolKind(analysis::LanguageSymbolKind kind) {
  return kind == analysis::LanguageSymbolKind::Function ||
         kind == analysis::LanguageSymbolKind::Method;
}

bool IsValidRenameName(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  std::vector<std::uint8_t> bytes(name.begin(), name.end());
  core::SourceLoadResult loaded = core::LoadSource("<rename>", bytes);
  if (!loaded.source.has_value()) {
    return false;
  }
  lexer::IdentScanResult ident = lexer::ScanIdentToken(*loaded.source, 0);
  return ident.ok && ident.next == loaded.source->scalars.size() &&
         ident.kind == lexer::TokenKind::Identifier && ident.lexeme == name;
}

std::optional<std::size_t> PreviousSignificantToken(
    const std::vector<lexer::Token>& tokens,
    std::size_t index) {
  while (index > 0) {
    --index;
    if (tokens[index].kind != lexer::TokenKind::Newline &&
        tokens[index].kind != lexer::TokenKind::Eof) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> CalleeTokenIndex(
    const std::vector<lexer::Token>& tokens,
    std::size_t lparen_index) {
  const auto previous = PreviousSignificantToken(tokens, lparen_index);
  if (!previous.has_value()) {
    return std::nullopt;
  }
  const lexer::Token& token = tokens[*previous];
  if (token.kind != lexer::TokenKind::Identifier) {
    return std::nullopt;
  }
  return previous;
}

struct CallContext {
  std::size_t lparen_index = 0;
  std::size_t callee_index = 0;
  std::size_t active_parameter = 0;
};

std::size_t ActiveParameterIndex(const std::vector<lexer::Token>& tokens,
                                 std::size_t lparen_index,
                                 std::size_t cursor_offset) {
  std::size_t active = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  for (std::size_t i = lparen_index + 1; i < tokens.size(); ++i) {
    const lexer::Token& token = tokens[i];
    if (token.span.start_offset >= cursor_offset) {
      break;
    }
    if (token.kind == lexer::TokenKind::Punctuator) {
      if (token.lexeme == "(") {
        ++paren_depth;
      } else if (token.lexeme == ")") {
        if (paren_depth == 0) {
          break;
        }
        --paren_depth;
      } else if (token.lexeme == "[") {
        ++bracket_depth;
      } else if (token.lexeme == "]") {
        if (bracket_depth > 0) {
          --bracket_depth;
        }
      } else if (token.lexeme == "{") {
        ++brace_depth;
      } else if (token.lexeme == "}") {
        if (brace_depth > 0) {
          --brace_depth;
        }
      } else if (token.lexeme == "," && paren_depth == 0 &&
                 bracket_depth == 0 && brace_depth == 0) {
        ++active;
      }
    }
  }
  return active;
}

std::optional<CallContext> CallContextAt(
    const std::vector<lexer::Token>& tokens,
    std::size_t cursor_offset) {
  std::vector<std::size_t> lparen_stack;
  std::optional<CallContext> best;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const lexer::Token& token = tokens[i];
    if (token.span.start_offset > cursor_offset) {
      break;
    }
    if (token.kind != lexer::TokenKind::Punctuator) {
      continue;
    }
    if (token.lexeme == "(") {
      lparen_stack.push_back(i);
      continue;
    }
    if (token.lexeme == ")") {
      if (!lparen_stack.empty()) {
        const std::size_t lparen = lparen_stack.back();
        lparen_stack.pop_back();
        if (token.span.start_offset >= cursor_offset) {
          if (const auto callee = CalleeTokenIndex(tokens, lparen)) {
            best = CallContext{
                lparen,
                *callee,
                ActiveParameterIndex(tokens, lparen, cursor_offset),
            };
          }
          break;
        }
      }
    }
  }
  for (auto it = lparen_stack.rbegin(); it != lparen_stack.rend(); ++it) {
    if (const auto callee = CalleeTokenIndex(tokens, *it)) {
      best = CallContext{
          *it,
          *callee,
          ActiveParameterIndex(tokens, *it, cursor_offset),
      };
      break;
    }
  }
  return best;
}

std::optional<std::size_t> MatchingRightParen(
    const std::vector<lexer::Token>& tokens,
    std::size_t lparen_index) {
  int depth = 0;
  for (std::size_t i = lparen_index; i < tokens.size(); ++i) {
    const lexer::Token& token = tokens[i];
    if (token.kind != lexer::TokenKind::Punctuator) {
      continue;
    }
    if (token.lexeme == "(") {
      ++depth;
    } else if (token.lexeme == ")") {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::nullopt;
}

std::vector<std::size_t> ArgumentStartTokenIndices(
    const std::vector<lexer::Token>& tokens,
    std::size_t lparen_index,
    std::size_t rparen_index) {
  std::vector<std::size_t> out;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  bool expect_argument = true;
  for (std::size_t i = lparen_index + 1; i < rparen_index; ++i) {
    const lexer::Token& token = tokens[i];
    if (token.kind == lexer::TokenKind::Newline ||
        token.kind == lexer::TokenKind::Eof) {
      continue;
    }
    if (token.kind == lexer::TokenKind::Punctuator) {
      if (token.lexeme == "," && paren_depth == 0 && bracket_depth == 0 &&
          brace_depth == 0) {
        expect_argument = true;
        continue;
      }
      if (expect_argument) {
        out.push_back(i);
        expect_argument = false;
      }
      if (token.lexeme == "(") {
        ++paren_depth;
      } else if (token.lexeme == ")") {
        if (paren_depth > 0) {
          --paren_depth;
        }
      } else if (token.lexeme == "[") {
        ++bracket_depth;
      } else if (token.lexeme == "]") {
        if (bracket_depth > 0) {
          --bracket_depth;
        }
      } else if (token.lexeme == "{") {
        ++brace_depth;
      } else if (token.lexeme == "}") {
        if (brace_depth > 0) {
          --brace_depth;
        }
      }
      continue;
    }
    if (expect_argument) {
      out.push_back(i);
      expect_argument = false;
    }
  }
  return out;
}

bool PositionInRange(std::size_t offset,
                     std::size_t start_offset,
                     std::size_t end_offset) {
  return offset >= start_offset && offset <= end_offset;
}

bool SpanContains(const core::Span& outer, const core::Span& inner) {
  return tooling::PathKey(outer.file) == tooling::PathKey(inner.file) &&
         outer.start_offset <= inner.start_offset &&
         inner.end_offset <= outer.end_offset;
}

std::optional<core::Span> NarrowIdentifierSpan(
    const std::filesystem::path& path,
    const std::string& text,
    const core::Span& search_span,
    std::string_view name) {
  const auto tokenized = TokenizeText(path, text);
  if (!tokenized.has_value()) {
    return std::nullopt;
  }
  for (const auto& token : tokenized->tokens) {
    if (token.kind != lexer::TokenKind::Identifier || token.lexeme != name) {
      continue;
    }
    if (SpanContains(search_span, token.span)) {
      return token.span;
    }
  }
  return std::nullopt;
}

std::optional<core::Span> RenameSpanForReference(
    const std::filesystem::path& path,
    const std::string& text,
    const analysis::LanguageSymbolInfo& symbol,
    const analysis::LanguageReference& reference) {
  if (!reference.is_declaration) {
    return reference.range;
  }
  return NarrowIdentifierSpan(path, text, reference.range, symbol.name);
}

}  // namespace

LspServer::LspServer(LspServerOptions options)
    : server_options_(std::move(options)) {
  if (server_options_.log_file.has_value()) {
    log_.open(*server_options_.log_file, std::ios::app);
  }
  analysis_worker_ = std::thread([this]() { AnalysisWorkerLoop(); });
}

LspServer::~LspServer() {
  StopAnalysisWorker();
}

int LspServer::Run() {
  Log("server start");
  while (running_) {
    std::optional<llvm::json::Value> message = rpc_.ReadMessage();
    if (!message.has_value()) {
      break;
    }
    const auto* object = message->getAsObject();
    if (object == nullptr) {
      continue;
    }
    HandleMessage(*object);
  }
  StopAnalysisWorker();
  Log("server stop");
  return shutdown_requested_ ? 0 : 1;
}

void LspServer::HandleMessage(const llvm::json::Object& message) {
  const auto method = message.getString("method");
  if (!method.has_value()) {
    return;
  }

  if (const auto* id = message.get("id")) {
    HandleRequest(message, *id, *method);
  } else {
    HandleNotification(message, *method);
  }
}

void LspServer::HandleRequest(const llvm::json::Object& message,
                              const llvm::json::Value& id,
                              llvm::StringRef method) {
  const auto* params = message.getObject("params");
  if (method == "initialize") {
    SendResponse(id, HandleInitialize(params));
    return;
  }
  if (method == "shutdown") {
    shutdown_requested_ = true;
    SendResponse(id, nullptr);
    return;
  }
  if (method == "textDocument/documentSymbol") {
    SendResponse(id, HandleDocumentSymbol(params));
    return;
  }
  if (method == "textDocument/hover") {
    SendResponse(id, HandleHover(params));
    return;
  }
  if (method == "textDocument/definition") {
    SendResponse(id, HandleDefinition(params));
    return;
  }
  if (method == "textDocument/semanticTokens/full") {
    SendResponse(id, HandleSemanticTokens(params));
    return;
  }
  if (method == "workspace/symbol") {
    SendResponse(id, HandleWorkspaceSymbol(params));
    return;
  }
  if (method == "textDocument/documentHighlight") {
    SendResponse(id, HandleDocumentHighlight(params));
    return;
  }
  if (method == "textDocument/references") {
    SendResponse(id, HandleReferences(params));
    return;
  }
  if (method == "textDocument/completion") {
    SendResponse(id, HandleCompletion(params));
    return;
  }
  if (method == "textDocument/codeAction") {
    SendResponse(id, HandleCodeAction(params));
    return;
  }
  if (method == "textDocument/prepareRename") {
    SendResponse(id, HandlePrepareRename(params));
    return;
  }
  if (method == "textDocument/rename") {
    SendResponse(id, HandleRename(params));
    return;
  }
  if (method == "textDocument/signatureHelp") {
    SendResponse(id, HandleSignatureHelp(params));
    return;
  }
  if (method == "textDocument/inlayHint") {
    SendResponse(id, HandleInlayHint(params));
    return;
  }

  SendError(id, -32601, "Method not found");
}

void LspServer::HandleNotification(const llvm::json::Object& message,
                                   llvm::StringRef method) {
  const auto* params = message.getObject("params");
  if (method == "initialized") {
    AnalyzeAndPublish();
    return;
  }
  if (method == "exit") {
    running_ = false;
    return;
  }
  if (method == "textDocument/didOpen") {
    DidOpen(params);
    return;
  }
  if (method == "textDocument/didChange") {
    DidChange(params);
    return;
  }
  if (method == "textDocument/didSave") {
    DidSave(params);
    return;
  }
  if (method == "textDocument/didClose") {
    DidClose(params);
    return;
  }
  if (method == "workspace/didChangeWatchedFiles") {
    DidChangeWatchedFiles();
    return;
  }
}

llvm::json::Value LspServer::HandleInitialize(
    const llvm::json::Object* params) {
  workspace_roots_.clear();
  if (params != nullptr) {
    if (const auto folders = params->getArray("workspaceFolders")) {
      for (const auto& folder : *folders) {
        if (const auto* first = folder.getAsObject()) {
          if (const auto uri = GetString(*first, "uri")) {
            if (const auto path = tooling::FileUriToPath(*uri)) {
              workspace_roots_.push_back(tooling::NormalizePath(*path));
            }
          }
        }
      }
    } else if (const auto root_uri = GetString(*params, "rootUri")) {
      if (const auto path = tooling::FileUriToPath(*root_uri)) {
        workspace_roots_.push_back(tooling::NormalizePath(*path));
      }
    } else if (const auto root_path = GetString(*params, "rootPath")) {
      workspace_roots_.push_back(tooling::NormalizePath(*root_path));
    }
  }
  if (workspace_roots_.empty()) {
    workspace_roots_.push_back(tooling::NormalizePath(std::filesystem::current_path()));
  }

  llvm::json::Array token_types;
  for (const auto& token_type : SemanticTokenTypes()) {
    token_types.emplace_back(token_type);
  }
  llvm::json::Array token_modifiers;
  for (const auto& token_modifier : SemanticTokenModifiers()) {
    token_modifiers.emplace_back(token_modifier);
  }
  llvm::json::Object legend;
  legend["tokenTypes"] = std::move(token_types);
  legend["tokenModifiers"] = std::move(token_modifiers);

  llvm::json::Object semantic_tokens;
  semantic_tokens["legend"] = std::move(legend);
  semantic_tokens["full"] = true;

  llvm::json::Object capabilities;
  capabilities["textDocumentSync"] = 1;
  capabilities["hoverProvider"] = true;
  capabilities["definitionProvider"] = true;
  capabilities["documentSymbolProvider"] = true;
  capabilities["workspaceSymbolProvider"] = true;
  capabilities["documentHighlightProvider"] = true;
  capabilities["referencesProvider"] = true;
  capabilities["completionProvider"] = llvm::json::Object{
      {"resolveProvider", false},
      {"triggerCharacters", llvm::json::Array{".", ":", "~", " "}},
  };
  capabilities["codeActionProvider"] = llvm::json::Object{
      {"codeActionKinds", llvm::json::Array{"quickfix"}},
  };
  capabilities["renameProvider"] = llvm::json::Object{
      {"prepareProvider", true},
  };
  capabilities["signatureHelpProvider"] = llvm::json::Object{
      {"triggerCharacters", llvm::json::Array{"(", ","}},
      {"retriggerCharacters", llvm::json::Array{","}},
  };
  capabilities["inlayHintProvider"] = llvm::json::Object{
      {"resolveProvider", false},
  };
  capabilities["semanticTokensProvider"] = std::move(semantic_tokens);

  llvm::json::Object server_info;
  server_info["name"] = "uv-lsp";

  llvm::json::Object result;
  result["capabilities"] = std::move(capabilities);
  result["serverInfo"] = std::move(server_info);
  return result;
}

llvm::json::Value LspServer::HandleDocumentSymbol(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  if (!path.has_value()) {
    return llvm::json::Array();
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return llvm::json::Array();
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  llvm::json::Array symbols;
  for (const auto* symbol :
       project->snapshot.language_service.SymbolsInFile(*path)) {
    if (!symbol->include_in_outline) {
      continue;
    }
    llvm::json::Object item;
    item["name"] = symbol->name;
    if (!symbol->detail.empty()) {
      item["detail"] = symbol->detail;
    }
    item["kind"] = static_cast<std::int64_t>(SymbolKindToLsp(symbol->kind));
    item["range"] = RangeJson(index.RangeFor(symbol->range));
    item["selectionRange"] = RangeJson(index.RangeFor(symbol->selection_range));
    symbols.emplace_back(std::move(item));
  }
  return symbols;
}

llvm::json::Value LspServer::HandleHover(const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return nullptr;
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return nullptr;
  }
  const auto* symbol =
      project->snapshot.language_service.ResolvedSymbolAt(*path, offset);
  const analysis::TypeRef hover_type =
      symbol != nullptr && symbol->type
          ? symbol->type
          : analysis::LanguageServiceTypeAt(project->snapshot.expr_types, *path,
                                            offset);
  if (symbol == nullptr && !hover_type) {
    return nullptr;
  }
  const auto* reference =
      project->snapshot.language_service.ReferenceAt(*path, offset);
  const auto word = WordAt(text, offset);

  std::string markdown = "```ultraviolet\n";
  if (symbol != nullptr) {
    markdown += analysis::LanguageSymbolKindName(symbol->kind);
    markdown += " ";
    markdown += symbol->qualified_name;
  } else {
    markdown += "type";
  }
  if (hover_type) {
    markdown += ": ";
    markdown += analysis::TypeToString(hover_type);
  }
  markdown += "\n```";
  if (symbol != nullptr && !symbol->documentation.empty()) {
    markdown += "\n";
    markdown += symbol->documentation;
  }

  llvm::json::Object contents;
  contents["kind"] = "markdown";
  contents["value"] = markdown;

  llvm::json::Object result;
  result["contents"] = std::move(contents);
  if (reference != nullptr) {
    result["range"] = RangeJson(index.RangeFor(reference->range));
  } else if (word.has_value()) {
    result["range"] = RangeJson(tooling::LineRange{
        index.PositionAt(word->start),
        index.PositionAt(word->end),
    });
  }
  return result;
}

llvm::json::Value LspServer::HandleDefinition(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return nullptr;
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return nullptr;
  }
  const auto* symbol =
      project->snapshot.language_service.ResolvedSymbolAt(*path, offset);
  if (symbol == nullptr) {
    return nullptr;
  }
  return LocationForSymbol(*symbol, TextForPath(symbol->selection_range.file));
}

llvm::json::Value LspServer::HandleSemanticTokens(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  if (!path.has_value()) {
    llvm::json::Object empty;
    empty["data"] = llvm::json::Array();
    return empty;
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return SemanticTokensFull(*path, TextForPath(*path),
                              static_cast<const tooling::AnalysisSnapshot*>(
                                  nullptr));
  }
  return SemanticTokensFull(*path, TextForPath(*path), &project->snapshot);
}

llvm::json::Value LspServer::HandleWorkspaceSymbol(
    const llvm::json::Object* params) {
  const std::string query =
      params != nullptr ? GetString(*params, "query").value_or("") : "";
  llvm::json::Array results;
  for (const auto& project : ProjectSnapshots()) {
    for (const auto& symbol : project->snapshot.language_service.Symbols()) {
      if (!symbol.include_in_workspace) {
        continue;
      }
      if (!NameMatchesQuery(symbol.name, query) &&
          !NameMatchesQuery(symbol.qualified_name, query)) {
        continue;
      }
      llvm::json::Object item;
      item["name"] = symbol.name;
      item["kind"] = static_cast<std::int64_t>(SymbolKindToLsp(symbol.kind));
      if (!symbol.module_path.empty()) {
        item["containerName"] = symbol.module_path;
      }
      item["location"] =
          LocationForSymbol(symbol, TextForPath(symbol.selection_range.file));
      results.emplace_back(std::move(item));
    }
  }
  return results;
}

llvm::json::Value LspServer::HandleDocumentHighlight(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return llvm::json::Array();
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return llvm::json::Array();
  }
  const auto* reference =
      project->snapshot.language_service.ReferenceAt(*path, offset);
  const auto* symbol =
      project->snapshot.language_service.ResolvedSymbolAt(*path, offset);
  if (symbol == nullptr) {
    return llvm::json::Array();
  }
  llvm::json::Array highlights;
  for (const auto* ref :
       project->snapshot.language_service.ReferencesForSymbol(symbol->id, true)) {
    if (tooling::PathKey(ref->range.file) != tooling::PathKey(*path)) {
      continue;
    }
    llvm::json::Object highlight;
    highlight["range"] = RangeJson(index.RangeFor(ref->range));
    highlight["kind"] = static_cast<std::int64_t>(1);
    highlights.emplace_back(std::move(highlight));
  }
  if (highlights.empty() && reference != nullptr) {
    llvm::json::Object highlight;
    highlight["range"] = RangeJson(index.RangeFor(reference->range));
    highlight["kind"] = static_cast<std::int64_t>(1);
    highlights.emplace_back(std::move(highlight));
  }
  return highlights;
}

llvm::json::Value LspServer::HandleReferences(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return llvm::json::Array();
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return llvm::json::Array();
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex local_index(text);
  const std::size_t offset = local_index.ByteOffsetAt(*position);
  const auto* symbol =
      project->snapshot.language_service.ResolvedSymbolAt(*path, offset);
  if (symbol == nullptr) {
    return llvm::json::Array();
  }

  bool include_declaration = true;
  if (params != nullptr) {
    const auto* context = params->getObject("context");
    if (context != nullptr) {
      if (const auto value = context->getBoolean("includeDeclaration")) {
        include_declaration = *value;
      }
    }
  }

  llvm::json::Array locations;
  for (const auto* ref :
       project->snapshot.language_service.ReferencesForSymbol(
           symbol->id, include_declaration)) {
    const std::filesystem::path file(ref->range.file);
    const std::string file_text = TextForPath(file);
    tooling::LineIndex index(file_text);
    llvm::json::Object location;
    location["uri"] = tooling::PathToFileUri(file);
    location["range"] = RangeJson(index.RangeFor(ref->range));
    locations.emplace_back(std::move(location));
  }
  return locations;
}

llvm::json::Value LspServer::HandleCompletion(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto project = path.has_value() ? SnapshotForPath(*path) : nullptr;
  llvm::json::Array items;

  static constexpr std::string_view kKeywords[] = {
      "public", "internal", "private", "procedure", "record", "class",
      "modal",  "enum",     "let",     "var",       "return", "move",
      "const",  "shared",   "unique",  "if",        "is",     "else",
      "loop",   "break",    "continue", "unsafe",   "extern", "using",
      "import",
  };
  for (const auto keyword : kKeywords) {
    llvm::json::Object item;
    item["label"] = std::string(keyword);
    item["kind"] = static_cast<std::int64_t>(14);
    items.emplace_back(std::move(item));
  }

  if (project != nullptr) {
    std::vector<const analysis::LanguageSymbolInfo*> completion_symbols;
    if (path.has_value()) {
      if (const auto position = PositionParam(params)) {
        const std::string text = TextForPath(*path);
        tooling::LineIndex index(text);
        completion_symbols = project->snapshot.language_service.CompletionSymbols(
            *path, index.ByteOffsetAt(*position));
      }
    }
    if (completion_symbols.empty()) {
      for (const auto& symbol : project->snapshot.language_service.Symbols()) {
        completion_symbols.push_back(&symbol);
      }
    }
    std::unordered_set<std::string> seen;
    for (const auto* symbol : completion_symbols) {
      if (symbol == nullptr || !seen.insert(symbol->name).second) {
        continue;
      }
      llvm::json::Object item;
      item["label"] = symbol->name;
      item["kind"] =
          static_cast<std::int64_t>(CompletionKindForSymbol(symbol->kind));
      item["detail"] = symbol->detail.empty()
                           ? analysis::LanguageSymbolKindName(symbol->kind)
                           : symbol->detail;
      items.emplace_back(std::move(item));
    }
  }

  llvm::json::Object result;
  result["isIncomplete"] = false;
  result["items"] = std::move(items);
  return result;
}

llvm::json::Value LspServer::HandleCodeAction(
    const llvm::json::Object* params) {
  if (params == nullptr) {
    return llvm::json::Array();
  }
  const auto uri = TextDocumentUri(params);
  if (!uri.has_value()) {
    return llvm::json::Array();
  }
  const auto* context = params->getObject("context");
  const auto* diagnostics =
      context != nullptr ? context->getArray("diagnostics") : nullptr;
  if (diagnostics == nullptr) {
    return llvm::json::Array();
  }

  llvm::json::Array actions;
  for (const auto& diag_value : *diagnostics) {
    const auto* diag = diag_value.getAsObject();
    if (diag == nullptr) {
      continue;
    }
    const auto* data = diag->getObject("data");
    const auto* fixits = data != nullptr ? data->getArray("fixits") : nullptr;
    if (fixits == nullptr) {
      continue;
    }
    for (const auto& fixit_value : *fixits) {
      const auto* fixit = fixit_value.getAsObject();
      if (fixit == nullptr) {
        continue;
      }
      const auto title = GetString(*fixit, "title").value_or("Apply fix");
      const auto fix_uri = GetString(*fixit, "uri").value_or(*uri);
      const auto new_text = GetString(*fixit, "newText");
      const auto range = RangeFromJson(fixit->getObject("range"));
      if (!new_text.has_value() || !range.has_value()) {
        continue;
      }

      llvm::json::Object edit;
      edit["changes"] = llvm::json::Object{
          {fix_uri, llvm::json::Array{TextEdit(*range, *new_text)}},
      };

      llvm::json::Object action;
      action["title"] = title;
      action["kind"] = "quickfix";
      action["edit"] = std::move(edit);
      actions.emplace_back(std::move(action));
    }
  }
  return actions;
}

llvm::json::Value LspServer::HandlePrepareRename(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return nullptr;
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return nullptr;
  }

  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto* reference =
      project->snapshot.language_service.ReferenceAt(*path, offset);
  if (reference == nullptr) {
    return nullptr;
  }
  const auto* symbol =
      project->snapshot.language_service.SymbolById(reference->symbol_id);
  if (symbol == nullptr || symbol->name.empty()) {
    return nullptr;
  }
  const auto rename_span =
      RenameSpanForReference(*path, text, *symbol, *reference);
  if (!rename_span.has_value() ||
      !PositionInRange(offset, rename_span->start_offset,
                       rename_span->end_offset)) {
    return nullptr;
  }

  llvm::json::Object result;
  result["range"] = RangeJson(index.RangeFor(*rename_span));
  result["placeholder"] = symbol->name;
  return result;
}

llvm::json::Value LspServer::HandleRename(const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value() || params == nullptr) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }
  const auto new_name = GetString(*params, "newName");
  if (!new_name.has_value() || !IsValidRenameName(*new_name)) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }

  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto* reference =
      project->snapshot.language_service.ReferenceAt(*path, offset);
  if (reference == nullptr) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }
  const auto* symbol =
      project->snapshot.language_service.SymbolById(reference->symbol_id);
  if (symbol == nullptr || symbol->name.empty()) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }
  const auto initial_span =
      RenameSpanForReference(*path, text, *symbol, *reference);
  if (!initial_span.has_value() ||
      !PositionInRange(offset, initial_span->start_offset,
                       initial_span->end_offset)) {
    return llvm::json::Object{{"changes", llvm::json::Object()}};
  }

  std::map<std::string, llvm::json::Array> changes_by_uri;
  for (const auto* ref :
       project->snapshot.language_service.ReferencesForSymbol(symbol->id,
                                                              true)) {
    if (ref == nullptr) {
      continue;
    }
    const std::filesystem::path file(ref->range.file);
    const std::string file_text = TextForPath(file);
    const auto rename_span =
        RenameSpanForReference(file, file_text, *symbol, *ref);
    if (!rename_span.has_value()) {
      continue;
    }
    tooling::LineIndex file_index(file_text);
    const std::string uri = tooling::PathToFileUri(file);
    changes_by_uri[uri].emplace_back(
        TextEdit(file_index.RangeFor(*rename_span), *new_name));
  }

  llvm::json::Object changes;
  for (auto& [uri, edits] : changes_by_uri) {
    changes[uri] = std::move(edits);
  }
  llvm::json::Object result;
  result["changes"] = std::move(changes);
  return result;
}

llvm::json::Value LspServer::HandleSignatureHelp(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  const auto position = PositionParam(params);
  if (!path.has_value() || !position.has_value()) {
    return nullptr;
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return nullptr;
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const std::size_t offset = index.ByteOffsetAt(*position);
  const auto tokenized = TokenizeText(*path, text);
  if (!tokenized.has_value()) {
    return nullptr;
  }
  const auto context = CallContextAt(tokenized->tokens, offset);
  if (!context.has_value()) {
    return nullptr;
  }

  const lexer::Token& callee = tokenized->tokens[context->callee_index];
  const auto* symbol = project->snapshot.language_service.ResolvedSymbolAt(
      *path, callee.span.start_offset);
  if (symbol == nullptr || !IsCallableSymbolKind(symbol->kind) ||
      symbol->signature_label.empty()) {
    return nullptr;
  }

  llvm::json::Array parameters;
  for (const auto& parameter : symbol->parameters) {
    llvm::json::Object item;
    item["label"] = parameter.label;
    parameters.emplace_back(std::move(item));
  }

  llvm::json::Object signature;
  signature["label"] = symbol->signature_label;
  signature["parameters"] = std::move(parameters);
  if (!symbol->documentation.empty()) {
    signature["documentation"] = llvm::json::Object{
        {"kind", "markdown"},
        {"value", symbol->documentation},
    };
  }

  const std::size_t active =
      symbol->parameters.empty()
          ? 0
          : std::min(context->active_parameter, symbol->parameters.size() - 1);
  llvm::json::Object result;
  result["signatures"] = llvm::json::Array{std::move(signature)};
  result["activeSignature"] = static_cast<std::int64_t>(0);
  result["activeParameter"] = static_cast<std::int64_t>(active);
  return result;
}

llvm::json::Value LspServer::HandleInlayHint(
    const llvm::json::Object* params) {
  const auto path = PathFromTextDocument(params);
  if (!path.has_value()) {
    return llvm::json::Array();
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    return llvm::json::Array();
  }
  const std::string text = TextForPath(*path);
  tooling::LineIndex index(text);
  const auto tokenized = TokenizeText(*path, text);
  if (!tokenized.has_value()) {
    return llvm::json::Array();
  }

  std::size_t range_start = 0;
  std::size_t range_end = text.size();
  if (params != nullptr) {
    if (const auto range = RangeFromJson(params->getObject("range"))) {
      range_start = index.ByteOffsetAt(range->start);
      range_end = index.ByteOffsetAt(range->end);
    }
  }

  llvm::json::Array hints;
  const auto& tokens = tokenized->tokens;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const lexer::Token& token = tokens[i];
    if (token.kind != lexer::TokenKind::Punctuator || token.lexeme != "(") {
      continue;
    }
    const auto callee_index = CalleeTokenIndex(tokens, i);
    if (!callee_index.has_value()) {
      continue;
    }
    const auto rparen = MatchingRightParen(tokens, i);
    if (!rparen.has_value()) {
      continue;
    }
    const auto* symbol = project->snapshot.language_service.ResolvedSymbolAt(
        *path, tokens[*callee_index].span.start_offset);
    if (symbol == nullptr || !IsCallableSymbolKind(symbol->kind) ||
        symbol->parameters.empty()) {
      continue;
    }
    const std::vector<std::size_t> arg_starts =
        ArgumentStartTokenIndices(tokens, i, *rparen);
    const std::size_t hint_count =
        std::min(arg_starts.size(), symbol->parameters.size());
    for (std::size_t arg_index = 0; arg_index < hint_count; ++arg_index) {
      const auto& parameter = symbol->parameters[arg_index];
      if (parameter.name.empty() || parameter.name == "_") {
        continue;
      }
      const lexer::Token& arg_token = tokens[arg_starts[arg_index]];
      if (!PositionInRange(arg_token.span.start_offset, range_start,
                           range_end)) {
        continue;
      }
      llvm::json::Object hint;
      hint["position"] = PositionJson(index.PositionAt(
          arg_token.span.start_offset));
      hint["label"] = parameter.name + ":";
      hint["kind"] = static_cast<std::int64_t>(2);
      hint["paddingRight"] = true;
      hints.emplace_back(std::move(hint));
    }
  }
  return hints;
}

void LspServer::DidOpen(const llvm::json::Object* params) {
  if (params == nullptr) {
    return;
  }
  const auto* text_document = params->getObject("textDocument");
  if (text_document == nullptr) {
    return;
  }
  const auto uri = GetString(*text_document, "uri");
  const auto text = GetString(*text_document, "text");
  if (!uri.has_value() || !text.has_value()) {
    return;
  }
  const auto path = tooling::FileUriToPath(*uri);
  if (!path.has_value()) {
    return;
  }
  const auto version = GetInteger(*text_document, "version").value_or(0);
  documents_.Open(*uri, *path, version, *text);
  if (ManifestRootForPath(*path).has_value()) {
    ScheduleProjectAnalysisForPath(*path);
  } else {
    PublishNoManifestDiagnostic(*uri, *path);
  }
}

void LspServer::DidChange(const llvm::json::Object* params) {
  if (params == nullptr) {
    return;
  }
  const auto uri = TextDocumentUri(params);
  if (!uri.has_value()) {
    return;
  }
  const auto* text_document = params->getObject("textDocument");
  const auto version = text_document != nullptr
                           ? GetInteger(*text_document, "version").value_or(0)
                           : 0;
  const auto* changes = params->getArray("contentChanges");
  if (changes == nullptr || changes->empty()) {
    return;
  }
  const auto* last = changes->back().getAsObject();
  if (last == nullptr) {
    return;
  }
  const auto text = GetString(*last, "text");
  if (!text.has_value()) {
    return;
  }
  documents_.ChangeFull(*uri, version, *text);
  if (const auto path = tooling::FileUriToPath(*uri)) {
    ScheduleProjectAnalysisForPath(*path);
  }
}

void LspServer::DidSave(const llvm::json::Object* params) {
  if (params != nullptr) {
    const auto uri = TextDocumentUri(params);
    const auto text = GetString(*params, "text");
    if (uri.has_value() && text.has_value()) {
      if (const auto existing = documents_.FindByUri(*uri)) {
        documents_.ChangeFull(*uri, existing->version, *text);
      }
    }
  }
  if (const auto uri = TextDocumentUri(params)) {
    if (const auto path = tooling::FileUriToPath(*uri)) {
      ScheduleProjectAnalysisForPath(*path);
    }
  } else {
    AnalyzeAndPublish();
  }
}

void LspServer::DidClose(const llvm::json::Object* params) {
  const auto uri = TextDocumentUri(params);
  if (!uri.has_value()) {
    return;
  }
  documents_.Close(*uri);
  llvm::json::Object publish;
  publish["uri"] = *uri;
  publish["diagnostics"] = llvm::json::Array();
  SendNotification("textDocument/publishDiagnostics", std::move(publish));
}

void LspServer::DidChangeWatchedFiles() {
  AnalyzeAndPublish();
}

void LspServer::AnalyzeAndPublish() {
  std::set<std::string> roots;
  for (const auto& overlay : documents_.Overlays()) {
    if (const auto root = ManifestRootForPath(overlay.path)) {
      roots.insert(tooling::PathKey(*root));
    }
  }
  for (const auto& root_key : roots) {
    for (const auto& overlay : documents_.Overlays()) {
      const auto root = ManifestRootForPath(overlay.path);
      if (root.has_value() && tooling::PathKey(*root) == root_key) {
        ScheduleProjectAnalysis(*root);
        break;
      }
    }
  }
  PublishDiagnosticsForOpenDocuments();
}

void LspServer::ScheduleProjectAnalysis(
    const std::filesystem::path& project_root) {
  tooling::ToolingAnalysisOptions options;
  options.project_root = tooling::NormalizePath(project_root);
  options.target_profile = server_options_.target_profile;
  const auto overlays = OverlaysForRoot(options.project_root);
  const std::string root_key = tooling::PathKey(options.project_root);

  {
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    const std::uint64_t generation = ++analysis_generations_[root_key];
    pending_analyses_.push_back(PendingAnalysis{
        root_key,
        generation,
        std::move(options),
        overlays,
    });
  }
  analysis_cv_.notify_one();
}

void LspServer::ScheduleProjectAnalysisForPath(
    const std::filesystem::path& path) {
  const auto root = ManifestRootForPath(path);
  if (!root.has_value()) {
    Log("no Ultraviolet.toml for " + tooling::NormalizePath(path).generic_string());
    return;
  }
  ScheduleProjectAnalysis(*root);
}

void LspServer::AnalysisWorkerLoop() {
  while (true) {
    PendingAnalysis pending;
    {
      std::unique_lock<std::mutex> lock(analysis_mutex_);
      analysis_cv_.wait(lock, [this]() {
        return analysis_worker_stop_ || !pending_analyses_.empty();
      });
      if (analysis_worker_stop_ && pending_analyses_.empty()) {
        return;
      }
      pending = std::move(pending_analyses_.front());
      pending_analyses_.pop_front();
      const auto current = analysis_generations_.find(pending.root_key);
      if (current != analysis_generations_.end() &&
          pending.generation < current->second) {
        continue;
      }
    }

    auto project = std::make_shared<ProjectSnapshot>();
    project->options = pending.options;
    project->snapshot =
        tooling::AnalyzeWorkspace(pending.options, pending.overlays);

    bool install = false;
    {
      std::lock_guard<std::mutex> lock(analysis_mutex_);
      const auto current = analysis_generations_.find(pending.root_key);
      install = current != analysis_generations_.end() &&
                pending.generation == current->second;
    }
    if (!install) {
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      projects_by_root_[pending.root_key] = project;
    }
    Log("analyzed " + pending.options.project_root.generic_string());
    PublishDiagnosticsForOverlays(pending.overlays, project->snapshot);
  }
}

void LspServer::StopAnalysisWorker() {
  {
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    analysis_worker_stop_ = true;
    pending_analyses_.clear();
  }
  analysis_cv_.notify_all();
  if (analysis_worker_.joinable()) {
    analysis_worker_.join();
  }
}

void LspServer::PublishDiagnosticsForOverlays(
    const std::vector<tooling::DocumentOverlay>& overlays,
    const tooling::AnalysisSnapshot& snapshot) {
  tooling::DocumentStore overlay_documents;
  for (const auto& overlay : overlays) {
    overlay_documents.Open(overlay.uri, overlay.path, overlay.version,
                           overlay.text_utf8);
  }
  for (const auto& overlay : overlays) {
    llvm::json::Object params;
    params["uri"] = overlay.uri;
    params["diagnostics"] =
        DiagnosticsForPath(snapshot, overlay_documents, overlay.path);
    SendNotification("textDocument/publishDiagnostics", std::move(params));
  }
}

void LspServer::PublishDiagnosticsForOpenDocuments() {
  for (const auto& overlay : documents_.Overlays()) {
    PublishDiagnosticsForUri(overlay.uri);
  }
}

void LspServer::PublishDiagnosticsForUri(const std::string& uri) {
  const auto path = tooling::FileUriToPath(uri);
  if (!path.has_value()) {
    return;
  }
  const auto project = SnapshotForPath(*path);
  if (project == nullptr) {
    if (ManifestRootForPath(*path).has_value()) {
      llvm::json::Object params;
      params["uri"] = uri;
      params["diagnostics"] = llvm::json::Array();
      SendNotification("textDocument/publishDiagnostics", std::move(params));
    } else {
      PublishNoManifestDiagnostic(uri, *path);
    }
    return;
  }
  llvm::json::Object params;
  params["uri"] = uri;
  params["diagnostics"] =
      DiagnosticsForPath(project->snapshot, documents_, *path);
  SendNotification("textDocument/publishDiagnostics", std::move(params));
}

void LspServer::PublishNoManifestDiagnostic(
    const std::string& uri,
    const std::filesystem::path& path) {
  llvm::json::Object params;
  params["uri"] = uri;
  llvm::json::Array diagnostics;
  diagnostics.emplace_back(DiagnosticAtStart(
      "No Ultraviolet.toml manifest found for " +
      tooling::NormalizePath(path).generic_string()));
  params["diagnostics"] = std::move(diagnostics);
  SendNotification("textDocument/publishDiagnostics", std::move(params));
}

void LspServer::SendResponse(const llvm::json::Value& id,
                             llvm::json::Value result) {
  llvm::json::Object response = JsonRpcBase();
  response["id"] = id;
  response["result"] = std::move(result);
  std::lock_guard<std::mutex> lock(output_mutex_);
  rpc_.WriteMessage(llvm::json::Value(std::move(response)));
}

void LspServer::SendError(const llvm::json::Value& id,
                          int code,
                          std::string message) {
  llvm::json::Object error;
  error["code"] = static_cast<std::int64_t>(code);
  error["message"] = std::move(message);

  llvm::json::Object response = JsonRpcBase();
  response["id"] = id;
  response["error"] = std::move(error);
  std::lock_guard<std::mutex> lock(output_mutex_);
  rpc_.WriteMessage(llvm::json::Value(std::move(response)));
}

void LspServer::SendNotification(std::string method, llvm::json::Value params) {
  llvm::json::Object notification = JsonRpcBase();
  notification["method"] = std::move(method);
  notification["params"] = std::move(params);
  std::lock_guard<std::mutex> lock(output_mutex_);
  rpc_.WriteMessage(llvm::json::Value(std::move(notification)));
}

void LspServer::Log(std::string_view message) {
  std::lock_guard<std::mutex> lock(output_mutex_);
  if (log_.is_open()) {
    log_ << message << '\n';
    log_.flush();
  }
}

std::optional<std::filesystem::path> LspServer::PathFromTextDocument(
    const llvm::json::Object* params) const {
  const auto uri = TextDocumentUri(params);
  if (!uri.has_value()) {
    return std::nullopt;
  }
  return tooling::FileUriToPath(*uri);
}

std::string LspServer::TextForPath(const std::filesystem::path& path) const {
  if (const auto overlay = documents_.FindByPath(path)) {
    std::vector<std::uint8_t> bytes(overlay->text_utf8.begin(),
                                    overlay->text_utf8.end());
    core::SourceLoadResult loaded =
        core::LoadSource(path.generic_string(), bytes);
    if (loaded.source.has_value()) {
      return loaded.source->text;
    }
    return overlay->text_utf8;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return {};
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  core::SourceLoadResult loaded = core::LoadSource(path.generic_string(), bytes);
  if (!loaded.source.has_value()) {
    return {};
  }
  return loaded.source->text;
}

std::optional<std::filesystem::path> LspServer::ManifestRootForPath(
    const std::filesystem::path& path) const {
  std::filesystem::path current = tooling::NormalizePath(path);
  std::error_code ec;
  if (!std::filesystem::is_directory(current, ec)) {
    current = current.parent_path();
  }

  while (!current.empty()) {
    if (std::filesystem::exists(current / "Ultraviolet.toml", ec) && !ec) {
      return tooling::NormalizePath(current);
    }
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }

  for (const auto& workspace_root : workspace_roots_) {
    if (std::filesystem::exists(workspace_root / "Ultraviolet.toml", ec) && !ec) {
      return tooling::NormalizePath(workspace_root);
    }
  }
  return std::nullopt;
}

std::vector<tooling::DocumentOverlay> LspServer::OverlaysForRoot(
    const std::filesystem::path& root) const {
  std::vector<tooling::DocumentOverlay> overlays;
  const std::string root_key = tooling::PathKey(root);
  for (const auto& overlay : documents_.Overlays()) {
    const auto overlay_root = ManifestRootForPath(overlay.path);
    if (overlay_root.has_value() && tooling::PathKey(*overlay_root) == root_key) {
      overlays.push_back(overlay);
    }
  }
  return overlays;
}

std::shared_ptr<const LspServer::ProjectSnapshot> LspServer::SnapshotForPath(
    const std::filesystem::path& path) const {
  const auto root = ManifestRootForPath(path);
  if (!root.has_value()) {
    return nullptr;
  }
  const std::string key = tooling::PathKey(*root);
  std::lock_guard<std::mutex> lock(state_mutex_);
  const auto it = projects_by_root_.find(key);
  return it == projects_by_root_.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<const LspServer::ProjectSnapshot>>
LspServer::ProjectSnapshots() const {
  std::vector<std::shared_ptr<const ProjectSnapshot>> snapshots;
  std::lock_guard<std::mutex> lock(state_mutex_);
  snapshots.reserve(projects_by_root_.size());
  for (const auto& [_, project] : projects_by_root_) {
    snapshots.push_back(project);
  }
  return snapshots;
}

}  // namespace ultraviolet::driver::lsp
