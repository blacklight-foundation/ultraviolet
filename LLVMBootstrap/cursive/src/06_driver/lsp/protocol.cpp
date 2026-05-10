#include "06_driver/lsp/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cursive::driver::lsp {

llvm::json::Object PositionJson(tooling::LinePosition position) {
  llvm::json::Object out;
  out["line"] = static_cast<std::int64_t>(position.line);
  out["character"] = static_cast<std::int64_t>(position.character);
  return out;
}

llvm::json::Object RangeJson(tooling::LineRange range) {
  llvm::json::Object out;
  out["start"] = PositionJson(range.start);
  out["end"] = PositionJson(range.end);
  return out;
}

std::optional<std::string> GetString(const llvm::json::Object& object,
                                     llvm::StringRef key) {
  if (const auto value = object.getString(key)) {
    return value->str();
  }
  return std::nullopt;
}

std::optional<std::int64_t> GetInteger(const llvm::json::Object& object,
                                       llvm::StringRef key) {
  if (const auto value = object.getInteger(key)) {
    return *value;
  }
  return std::nullopt;
}

int SymbolKindToLsp(analysis::LanguageSymbolKind kind) {
  switch (kind) {
    case analysis::LanguageSymbolKind::Module:
      return 2;
    case analysis::LanguageSymbolKind::Function:
      return 12;
    case analysis::LanguageSymbolKind::Method:
      return 6;
    case analysis::LanguageSymbolKind::Variable:
    case analysis::LanguageSymbolKind::Parameter:
      return 13;
    case analysis::LanguageSymbolKind::Constant:
      return 14;
    case analysis::LanguageSymbolKind::Field:
      return 8;
    case analysis::LanguageSymbolKind::Record:
      return 23;
    case analysis::LanguageSymbolKind::Enum:
      return 10;
    case analysis::LanguageSymbolKind::EnumMember:
      return 22;
    case analysis::LanguageSymbolKind::Modal:
      return 5;
    case analysis::LanguageSymbolKind::Class:
      return 11;
    case analysis::LanguageSymbolKind::TypeAlias:
      return 5;
    case analysis::LanguageSymbolKind::State:
      return 7;
  }
  return 13;
}

const std::vector<std::string>& SemanticTokenTypes() {
  static const std::vector<std::string> kTypes = {
      "namespace", "type",     "class",    "enum",       "struct",
      "function",  "method",   "variable", "parameter",  "property",
      "enumMember", "string",  "number",   "keyword",    "operator",
      "comment",   "macro"};
  return kTypes;
}

const std::vector<std::string>& SemanticTokenModifiers() {
  static const std::vector<std::string> kModifiers = {
      "declaration",
      "readonly",
      "documentation",
  };
  return kModifiers;
}

int SemanticTokenTypeIndex(std::string_view token_type) {
  const auto& types = SemanticTokenTypes();
  for (std::size_t i = 0; i < types.size(); ++i) {
    if (types[i] == token_type) {
      return static_cast<int>(i);
    }
  }
  return SemanticTokenTypeIndex("variable");
}

int SemanticTokenModifierMask(std::string_view token_modifier) {
  const auto& modifiers = SemanticTokenModifiers();
  for (std::size_t i = 0; i < modifiers.size(); ++i) {
    if (modifiers[i] == token_modifier) {
      return 1 << static_cast<int>(i);
    }
  }
  return 0;
}

}  // namespace cursive::driver::lsp
