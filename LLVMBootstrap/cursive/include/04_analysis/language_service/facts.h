#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

enum class LanguageSymbolKind {
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
  Parameter,
};

struct LanguageParameterInfo {
  std::string name;
  std::string label;
};

struct LanguageSymbolInfo {
  std::string id;
  std::string name;
  std::string qualified_name;
  std::string module_path;
  LanguageSymbolKind kind = LanguageSymbolKind::Variable;
  core::Span range;
  core::Span selection_range;
  std::string detail;
  std::string documentation;
  TypeRef type;
  std::string signature_label;
  std::vector<LanguageParameterInfo> parameters;
  bool is_local = false;
  bool include_in_outline = true;
  bool include_in_workspace = true;
};

struct LanguageReference {
  std::string symbol_id;
  core::Span range;
  bool is_declaration = false;
};

class LanguageServiceIndex {
 public:
  void AddSymbol(LanguageSymbolInfo symbol);
  void AddReference(LanguageReference reference);

  const std::vector<LanguageSymbolInfo>& Symbols() const { return symbols_; }
  const std::vector<LanguageReference>& References() const {
    return references_;
  }

  const LanguageSymbolInfo* SymbolById(std::string_view id) const;
  std::vector<const LanguageSymbolInfo*> SymbolsInFile(
      const std::filesystem::path& path) const;
  const LanguageSymbolInfo* SymbolAt(const std::filesystem::path& path,
                                     std::size_t byte_offset) const;
  const LanguageReference* ReferenceAt(const std::filesystem::path& path,
                                       std::size_t byte_offset) const;
  const LanguageSymbolInfo* ResolvedSymbolAt(
      const std::filesystem::path& path,
      std::size_t byte_offset) const;
  std::vector<const LanguageReference*> ReferencesForSymbol(
      std::string_view symbol_id,
      bool include_declaration) const;
  std::vector<const LanguageSymbolInfo*> CompletionSymbols(
      const std::filesystem::path& path,
      std::size_t byte_offset) const;

 private:
  std::vector<LanguageSymbolInfo> symbols_;
  std::vector<LanguageReference> references_;
  std::unordered_map<std::string, std::size_t> symbol_offsets_;
};

LanguageServiceIndex BuildLanguageServiceDeclarations(
    const std::vector<ast::ASTModule>& modules);

std::string LanguageSymbolKindName(LanguageSymbolKind kind);

Entity MakeLanguageServiceLocalEntity(LanguageServiceIndex* index,
                                      const ScopeContext& ctx,
                                      std::string_view name,
                                      const core::Span& declaration_span,
                                      LanguageSymbolKind kind,
                                      std::string detail);

void RecordLanguageServiceReference(LanguageServiceIndex* index,
                                    const ScopeContext& ctx,
                                    std::string_view fallback_name,
                                    const core::Span& reference_span,
                                    const Entity& entity);

void RecordLanguageServiceTypePathReference(LanguageServiceIndex* index,
                                            const ScopeContext& ctx,
                                            const ast::TypePath& path,
                                            const core::Span& reference_span);

void RecordLanguageServiceMemberReference(LanguageServiceIndex* index,
                                          const ast::TypePath& owner_path,
                                          std::string_view member_name,
                                          const core::Span& reference_span);

TypeRef LanguageServiceTypeAt(const ExprTypeMap& expr_types,
                              const std::filesystem::path& path,
                              std::size_t byte_offset);

}  // namespace cursive::analysis
