#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "llvm/Support/JSON.h"

#include "04_analysis/language_service/facts.h"
#include "06_driver/tooling/line_index.h"

namespace cursive::driver::lsp {

llvm::json::Object PositionJson(tooling::LinePosition position);
llvm::json::Object RangeJson(tooling::LineRange range);

std::optional<std::string> GetString(const llvm::json::Object& object,
                                     llvm::StringRef key);
std::optional<std::int64_t> GetInteger(const llvm::json::Object& object,
                                       llvm::StringRef key);

int SymbolKindToLsp(analysis::LanguageSymbolKind kind);

const std::vector<std::string>& SemanticTokenTypes();
const std::vector<std::string>& SemanticTokenModifiers();
int SemanticTokenTypeIndex(std::string_view token_type);
int SemanticTokenModifierMask(std::string_view token_modifier);

}  // namespace cursive::driver::lsp
