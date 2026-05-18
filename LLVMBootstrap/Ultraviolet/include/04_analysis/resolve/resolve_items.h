#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/diagnostics.h"
#include "02_source/module_paths.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct BoundName {
  IdKey name;
  Entity ent;
  std::optional<core::Span> span;
};

using BindingList = std::vector<BoundName>;

struct BindingsResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  BindingList bindings;
};

struct ResolveUsingPathResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool is_module = false;
  ast::ModulePath module_path;
  std::optional<ast::Identifier> item;
};

struct UsingNamesResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  BindingList bindings;
};

struct ItemOfPathResult {
  bool ok = false;
  ast::ModulePath module_path;
  ast::Identifier name;
};

struct CollectNamesResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  NameMap names;
  std::unordered_map<IdKey, std::optional<core::Span>> name_spans;
};

struct NamesState {
  enum class Kind {
    Start,
    Scan,
    Done,
    Error,
  };

  Kind kind = Kind::Start;
  const ast::ASTModule* module = nullptr;
  std::size_t index = 0;
  NameMap names;
  std::optional<std::string_view> diag_id;
};

struct NameMapBuildResult {
  NameMapTable name_maps;
  core::DiagnosticStream diags;
};

bool ImportRequired(const ast::ModulePath& current_module,
                    const ast::ModulePath& path);

bool ImportCovers(const ScopeContext& ctx,
                  const source::ModuleNames& module_names,
                  const ast::ModulePath& current_module,
                  const ast::ModulePath& path);

bool ImportOk(const ScopeContext& ctx,
              const source::ModuleNames& module_names,
              const ast::ModulePath& current_module,
              const ast::ModulePath& path);

std::vector<ast::Identifier> PatNames(const ast::Pattern& pat);
std::vector<ast::Identifier> PatNames(const ast::PatternPtr& pat);

std::vector<IdKey> DeclNames(const std::vector<ast::ASTItem>& items,
                             const ast::ModulePath& module_path);

std::vector<IdKey> DeclNames(const ast::ASTModule& module);

ResolveUsingPathResult ResolveUsingPath(const ScopeContext& ctx,
                                        const NameMapTable& name_maps,
                                        const source::ModuleNames& module_names,
                                        const ast::ModulePath& path);

ItemOfPathResult ItemOfPath(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::ModulePath& path);

UsingNamesResult UsingNames(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::UsingDecl& decl);

BindingsResult ItemBindings(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::ASTItem& item,
                            const ast::ModulePath& module_path);

CollectNamesResult CollectNames(const ScopeContext& ctx,
                                const NameMapTable& name_maps,
                                const source::ModuleNames& module_names,
                                const ast::ASTModule& module);

NamesState NamesStart(const ast::ASTModule& module);

NamesState NamesStep(const ScopeContext& ctx,
                     const NameMapTable& name_maps,
                     const source::ModuleNames& module_names,
                     const NamesState& state);

NameMapBuildResult CollectNameMaps(ScopeContext& ctx);

}  // namespace ultraviolet::analysis
