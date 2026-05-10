#include "06_driver/lsp/semantic_tokens.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "00_core/source_load.h"
#include "02_source/lexer.h"
#include "06_driver/lsp/protocol.h"
#include "06_driver/tooling/line_index.h"
#include "06_driver/tooling/uri.h"

namespace cursive::driver::lsp {

namespace {

struct SemanticToken {
  std::size_t line = 0;
  std::size_t start = 0;
  std::size_t length = 0;
  int token_type = 0;
  int modifiers = 0;
};

struct SemanticTokenClass {
  std::string_view type;
  int modifiers = 0;
};

bool IsBuiltinType(std::string_view lexeme) {
  static constexpr std::string_view kBuiltinTypes[] = {
      "bool",            "bytes",      "char",      "f32",
      "f64",             "i8",         "i16",       "i32",
      "i64",             "isize",      "string",    "u8",
      "u16",             "u32",        "u64",       "usize",
      "Context",         "System",     "FileSystem", "Network",
      "HeapAllocator",   "ExecutionDomain", "Reactor", "Region",
      "RegionOptions",   "CancelToken", "File",      "DirIter",
      "DirEntry",        "FileKind",   "IoError",   "Async",
      "Future",          "Sequence",   "Stream",    "Pipe",
      "Exchange",        "Spawned",    "Tracked",   "Outcome",
  };
  for (const std::string_view builtin_type : kBuiltinTypes) {
    if (builtin_type == lexeme) {
      return true;
    }
  }
  return false;
}

bool IsAsciiUpper(char ch) {
  return ch >= 'A' && ch <= 'Z';
}

bool IsAsciiLower(char ch) {
  return ch >= 'a' && ch <= 'z';
}

bool IsReadonlyName(std::string_view lexeme) {
  bool has_letter = false;
  for (const char ch : lexeme) {
    if (IsAsciiLower(ch)) {
      return false;
    }
    if (IsAsciiUpper(ch)) {
      has_letter = true;
    }
  }
  return has_letter && lexeme.size() > 1;
}

SemanticTokenClass ClassifyIdentifierSyntaxOnly(std::string_view lexeme) {
  if (IsBuiltinType(lexeme)) {
    return SemanticTokenClass{"type", 0};
  }
  if (!lexeme.empty() && IsAsciiUpper(lexeme.front())) {
    if (IsReadonlyName(lexeme)) {
      return SemanticTokenClass{"variable",
                                SemanticTokenModifierMask("readonly")};
    }
    return SemanticTokenClass{"type", 0};
  }
  return SemanticTokenClass{"variable", 0};
}

std::string_view TypeForSymbolKind(analysis::LanguageSymbolKind kind) {
  switch (kind) {
    case analysis::LanguageSymbolKind::Module:
      return "namespace";
    case analysis::LanguageSymbolKind::Record:
      return "struct";
    case analysis::LanguageSymbolKind::Modal:
    case analysis::LanguageSymbolKind::TypeAlias:
      return "type";
    case analysis::LanguageSymbolKind::Class:
      return "class";
    case analysis::LanguageSymbolKind::Enum:
      return "enum";
    case analysis::LanguageSymbolKind::EnumMember:
      return "enumMember";
    case analysis::LanguageSymbolKind::Function:
      return "function";
    case analysis::LanguageSymbolKind::Method:
      return "method";
    case analysis::LanguageSymbolKind::Field:
    case analysis::LanguageSymbolKind::State:
      return "property";
    case analysis::LanguageSymbolKind::Parameter:
      return "parameter";
    case analysis::LanguageSymbolKind::Constant:
    case analysis::LanguageSymbolKind::Variable:
      return "variable";
  }
  return "variable";
}

const analysis::LanguageSymbolInfo* SymbolForIdentifier(
    const std::filesystem::path& path,
    const lexer::Token& token,
    const tooling::AnalysisSnapshot& snapshot,
    const analysis::LanguageReference** reference_out) {
  const auto* reference =
      snapshot.language_service.ReferenceAt(path, token.span.start_offset);
  if (reference_out != nullptr) {
    *reference_out = reference;
  }
  if (reference != nullptr) {
    if (const auto* symbol =
            snapshot.language_service.SymbolById(reference->symbol_id)) {
      return symbol;
    }
  }

  const auto* symbol =
      snapshot.language_service.SymbolAt(path, token.span.start_offset);
  if (symbol != nullptr && symbol->name == token.lexeme) {
    return symbol;
  }
  return nullptr;
}

SemanticTokenClass ClassifyIdentifier(
    const std::filesystem::path& path,
    const lexer::Token& token,
    const tooling::AnalysisSnapshot* snapshot) {
  if (IsBuiltinType(token.lexeme)) {
    return SemanticTokenClass{"type", 0};
  }
  if (snapshot == nullptr) {
    return ClassifyIdentifierSyntaxOnly(token.lexeme);
  }

  const analysis::LanguageReference* reference = nullptr;
  const auto* symbol =
      SymbolForIdentifier(path, token, *snapshot, &reference);
  if (symbol == nullptr) {
    return SemanticTokenClass{"variable", 0};
  }

  int modifiers = 0;
  if (reference != nullptr && reference->is_declaration &&
      symbol->name == token.lexeme) {
    modifiers |= SemanticTokenModifierMask("declaration");
  }
  if (symbol->kind == analysis::LanguageSymbolKind::Constant) {
    modifiers |= SemanticTokenModifierMask("readonly");
  }
  return SemanticTokenClass{TypeForSymbolKind(symbol->kind), modifiers};
}

SemanticTokenClass ClassifyToken(const std::filesystem::path& path,
                                 const lexer::Token& token,
                                 const tooling::AnalysisSnapshot* snapshot) {
  switch (token.kind) {
    case lexer::TokenKind::Keyword:
    case lexer::TokenKind::BoolLiteral:
    case lexer::TokenKind::NullLiteral:
      return SemanticTokenClass{"keyword", 0};
    case lexer::TokenKind::Identifier:
      return ClassifyIdentifier(path, token, snapshot);
    case lexer::TokenKind::IntLiteral:
    case lexer::TokenKind::FloatLiteral:
      return SemanticTokenClass{"number", 0};
    case lexer::TokenKind::StringLiteral:
    case lexer::TokenKind::CharLiteral:
      return SemanticTokenClass{"string", 0};
    case lexer::TokenKind::Operator:
    case lexer::TokenKind::Punctuator:
      return SemanticTokenClass{"operator", 0};
    default:
      return SemanticTokenClass{};
  }
}

void AddToken(std::vector<SemanticToken>& out,
              const tooling::LineIndex& index,
              const core::Span& span,
              int type_index,
              int modifiers) {
  const tooling::LineRange range = index.RangeFor(span);
  if (range.start.line != range.end.line ||
      range.end.character <= range.start.character) {
    return;
  }
  out.push_back(SemanticToken{
      range.start.line,
      range.start.character,
      range.end.character - range.start.character,
      type_index,
      modifiers,
  });
}

}  // namespace

llvm::json::Object SemanticTokensFull(
    const std::filesystem::path& path,
    const std::string& text_utf8,
    const tooling::AnalysisSnapshot* snapshot) {
  llvm::json::Array data;
  std::vector<std::uint8_t> bytes(text_utf8.begin(), text_utf8.end());
  core::SourceLoadResult loaded = core::LoadSource(path.generic_string(), bytes);
  if (!loaded.source.has_value()) {
    llvm::json::Object empty;
    empty["data"] = std::move(data);
    return empty;
  }

  tooling::LineIndex index(loaded.source->text);
  lexer::TokenizeDiagnosticResult tokenized =
      lexer::TokenizeWithDiagnostics(*loaded.source);

  std::vector<SemanticToken> tokens;
  if (tokenized.output.has_value()) {
    for (const auto& token : tokenized.output->tokens) {
      const SemanticTokenClass token_class =
          ClassifyToken(path, token, snapshot);
      if (token_class.type.empty()) {
        continue;
      }
      AddToken(tokens, index, token.span,
               SemanticTokenTypeIndex(token_class.type),
               token_class.modifiers);
    }
    for (const auto& doc : tokenized.output->docs) {
      AddToken(tokens, index, doc.span, SemanticTokenTypeIndex("comment"),
               SemanticTokenModifierMask("documentation"));
    }
  }

  std::sort(tokens.begin(), tokens.end(), [](const SemanticToken& lhs,
                                             const SemanticToken& rhs) {
    if (lhs.line != rhs.line) {
      return lhs.line < rhs.line;
    }
    return lhs.start < rhs.start;
  });

  std::size_t prev_line = 0;
  std::size_t prev_start = 0;
  bool first = true;
  for (const auto& token : tokens) {
    const std::size_t delta_line = first ? token.line : token.line - prev_line;
    const std::size_t delta_start =
        (first || delta_line != 0) ? token.start : token.start - prev_start;
    data.emplace_back(static_cast<std::int64_t>(delta_line));
    data.emplace_back(static_cast<std::int64_t>(delta_start));
    data.emplace_back(static_cast<std::int64_t>(token.length));
    data.emplace_back(static_cast<std::int64_t>(token.token_type));
    data.emplace_back(static_cast<std::int64_t>(token.modifiers));
    prev_line = token.line;
    prev_start = token.start;
    first = false;
  }

  llvm::json::Object result;
  result["data"] = std::move(data);
  return result;
}

llvm::json::Object SemanticTokensFull(
    const std::filesystem::path& path,
    const std::string& text_utf8,
    const tooling::AnalysisSnapshot& snapshot) {
  return SemanticTokensFull(path, text_utf8, &snapshot);
}

}  // namespace cursive::driver::lsp
