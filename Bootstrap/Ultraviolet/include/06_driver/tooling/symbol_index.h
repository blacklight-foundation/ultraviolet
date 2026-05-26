#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::driver::tooling {

enum class SymbolKind {
  Module,
  Function,
  Method,
  Variable,
  Constant,
  Field,
  Record,
  Enum,
  EnumMember,
  Modal,
  Class,
  TypeAlias,
  State,
};

struct SymbolInfo {
  std::string name;
  std::string qualified_name;
  std::string module_path;
  SymbolKind kind = SymbolKind::Variable;
  core::Span range;
  core::Span selection_range;
  std::string detail;
  std::string documentation;
};

class SymbolIndex {
 public:
  void Add(SymbolInfo symbol);
  const std::vector<SymbolInfo>& Symbols() const { return symbols_; }

  std::vector<const SymbolInfo*> SymbolsInFile(
      const std::filesystem::path& path) const;
  const SymbolInfo* SymbolAt(const std::filesystem::path& path,
                             std::size_t byte_offset) const;
  const SymbolInfo* ResolveNameNear(const std::filesystem::path& path,
                                    std::size_t byte_offset,
                                    std::string_view name) const;

 private:
  std::vector<SymbolInfo> symbols_;
};

SymbolIndex BuildSymbolIndex(const std::vector<ast::ASTModule>& modules);
std::string SymbolKindName(SymbolKind kind);

}  // namespace ultraviolet::driver::tooling
