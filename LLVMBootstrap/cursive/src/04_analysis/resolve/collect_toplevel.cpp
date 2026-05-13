// =============================================================================
// collect_toplevel.cpp - Top-Level Name Collection
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md Section 5.1.5 "Top-Level Name Collection" (Lines 6956-7309)
//   - BindKind, BindSource, NameInfo, NameMap definitions
//   - ItemNames, DeclNames, PatNames judgments
//   - CollectNameMaps fixed-point computation
//
// SOURCE FILE:
//   Migrated from cursive-bootstrap/src/03_analysis/resolve/collect_toplevel.cpp
//   (Lines 1-600)
//
// =============================================================================

#include "04_analysis/resolve/resolve_items.h"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>
#include <unordered_map>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/symbols.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/visibility.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCollect() {
  SPEC_DEF("BindKind", "5.1.5");
  SPEC_DEF("BindSource", "5.1.5");
  SPEC_DEF("NameInfo", "5.1.5");
  SPEC_DEF("NameMap", "5.1.5");
  SPEC_DEF("AliasMap", "5.1.5");
  SPEC_DEF("UsingMap", "5.1.5");
  SPEC_DEF("UsingValueMap", "5.1.5");
  SPEC_DEF("UsingTypeMap", "5.1.5");
  SPEC_DEF("TypeMap", "5.1.5");
  SPEC_DEF("ClassMap", "5.1.5");
  SPEC_DEF("ModuleNames", "5.1.5");
  SPEC_DEF("IsModulePath", "5.1.5");
  SPEC_DEF("SplitLast", "5.1.5");
  SPEC_DEF("ModuleByPath", "5.1.5");
  SPEC_DEF("ItemNames", "5.1.5");
  SPEC_DEF("UsingSpecName", "5.1.5");
  SPEC_DEF("UsingSpecNames", "5.1.5");
  SPEC_DEF("DeclNames", "5.1.5");
  SPEC_DEF("ImportRequired", "5.1.5");
  SPEC_DEF("ImportCovers", "5.1.5");
  SPEC_DEF("ImportOk", "5.1.5");
}

static inline void SpecDefsPatNames() {
  SPEC_DEF("PatNames", "5.1.5");
}

std::optional<core::Span> SpanOfItem(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& it) -> std::optional<core::Span> { return it.span; },
      item);
}

std::vector<IdKey> NamesOf(const BindingList& bindings) {
  std::vector<IdKey> names;
  names.reserve(bindings.size());
  for (const auto& binding : bindings) {
    names.push_back(binding.name);
  }
  return names;
}

bool NoDup(const BindingList& bindings) {
  std::vector<IdKey> names = NamesOf(bindings);
  std::sort(names.begin(), names.end());
  auto dup = std::adjacent_find(names.begin(), names.end());
  return dup == names.end();
}

bool DisjointNames(const BindingList& bindings, const NameMap& names) {
  for (const auto& binding : bindings) {
    if (names.find(binding.name) != names.end()) {
      return false;
    }
  }
  return true;
}

bool IsUsingImportSource(EntitySource source) {
  // Import bindings are tracked with `Using` source in this pass.
  return source == EntitySource::Using;
}

std::optional<IdKey> FreeProcedureOverloadName(const ast::ASTItem& item) {
  const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
  if (proc == nullptr || IdEq(proc->name, "main")) {
    return std::nullopt;
  }
  return IdKeyOf(proc->name);
}

bool BindingsAreSingleOverloadProcedure(const ast::ASTItem& item,
                                        const BindingList& bindings) {
  const auto overload_name = FreeProcedureOverloadName(item);
  return overload_name.has_value() && bindings.size() == 1 &&
         bindings.front().name == *overload_name &&
         bindings.front().ent.kind == EntityKind::Value &&
         bindings.front().ent.source == EntitySource::Decl;
}

bool HasPriorFreeProcedureOverload(
    const std::vector<ast::ASTItem>& items,
    std::size_t end_index,
    const IdKey& name) {
  for (std::size_t i = 0; i < end_index && i < items.size(); ++i) {
    const auto overload_name = FreeProcedureOverloadName(items[i]);
    if (overload_name.has_value() && *overload_name == name) {
      return true;
    }
  }
  return false;
}

bool UsingImportConflict(const BindingList& bindings, const NameMap& names) {
  std::unordered_map<IdKey, std::size_t> counts;
  std::unordered_map<IdKey, bool> has_using_import;
  counts.reserve(bindings.size());
  has_using_import.reserve(bindings.size());

  for (const auto& binding : bindings) {
    ++counts[binding.name];
    if (IsUsingImportSource(binding.ent.source)) {
      has_using_import[binding.name] = true;
    } else if (!has_using_import.contains(binding.name)) {
      has_using_import[binding.name] = false;
    }
  }

  for (const auto& [name, count] : counts) {
    if (count > 1 && has_using_import[name]) {
      return true;
    }
  }

  for (const auto& binding : bindings) {
    const auto it = names.find(binding.name);
    if (it == names.end()) {
      continue;
    }
    if (IsUsingImportSource(binding.ent.source) ||
        IsUsingImportSource(it->second.source)) {
      return true;
    }
  }
  return false;
}

void InsertBindings(NameMap& out, const BindingList& bindings) {
  for (const auto& binding : bindings) {
    out.emplace(binding.name, binding.ent);
  }
}

bool EntityEquals(const Entity& lhs, const Entity& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  if (lhs.source != rhs.source) {
    return false;
  }
  if (lhs.origin_opt.has_value() != rhs.origin_opt.has_value()) {
    return false;
  }
  if (lhs.origin_opt.has_value() &&
      !PathEq(*lhs.origin_opt, *rhs.origin_opt)) {
    return false;
  }
  if (lhs.target_opt.has_value() != rhs.target_opt.has_value()) {
    return false;
  }
  if (lhs.target_opt.has_value() &&
      !IdEq(*lhs.target_opt, *rhs.target_opt)) {
    return false;
  }
  return true;
}

bool NameMapEquals(const NameMap& lhs, const NameMap& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (const auto& [key, ent] : lhs) {
    const auto it = rhs.find(key);
    if (it == rhs.end()) {
      return false;
    }
    if (!EntityEquals(ent, it->second)) {
      return false;
    }
  }
  return true;
}

bool NameMapTableEquals(const NameMapTable& lhs, const NameMapTable& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (const auto& [key, names] : lhs) {
    const auto it = rhs.find(key);
    if (it == rhs.end()) {
      return false;
    }
    if (!NameMapEquals(names, it->second)) {
      return false;
    }
  }
  return true;
}

std::optional<std::string_view> CodeForCollectDiag(std::string_view diag_id) {
  if (diag_id == "Import-Using-Missing") {
    return "E-MOD-1201";
  }
  if (diag_id == "Resolve-Import-Err") {
    return "E-MOD-1202";
  }
  if (diag_id == "Resolve-Using-None") {
    return "E-MOD-1204";
  }
  if (diag_id == "Resolve-Using-Ambig") {
    return "E-MOD-1208";
  }
  if (diag_id == "Using-List-Dup") {
    return "E-MOD-1206";
  }
  if (diag_id == "Import-Using-Name-Conflict") {
    return "E-MOD-1203";
  }
  if (diag_id == "Using-Path-Item-Public-Err" ||
      diag_id == "Using-List-Public-Err") {
    return "E-MOD-1205";
  }
  if (diag_id == "Collect-Dup" || diag_id == "Names-Step-Dup") {
    return "E-MOD-1302";
  }
  if (diag_id == "Access-Err") {
    return "E-MOD-1207";
  }
  return std::nullopt;
}

core::DiagnosticStream EmitCollectDiag(core::DiagnosticStream diags,
                                       std::string_view diag_id,
                                       const std::optional<core::Span>& span) {
  const auto code = CodeForCollectDiag(diag_id);
  if (!code.has_value()) {
    return diags;
  }
  if (auto diag = core::MakeDiagnosticById(*code, span)) {
    core::Emit(diags, *diag);
  }
  return diags;
}

NameMap BuildDeclNameMap(const ast::ModulePath& module_path,
                         const std::vector<ast::ASTItem>& items) {
  NameMap names;
  for (const auto& item : items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                               std::is_same_v<T, ast::ComptimeProcedureDecl> ||
                               std::is_same_v<T, ast::DeriveTargetDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Value, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Type, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Type, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Type, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Class, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
            names.emplace(IdKeyOf(node.name),
                          Entity{EntityKind::Type, module_path, std::nullopt,
                                 EntitySource::Decl});
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            for (const auto& ext_item : node.items) {
              std::visit(
                  [&](const auto& ext_decl) {
                    using ET = std::decay_t<decltype(ext_decl)>;
                    if constexpr (std::is_same_v<ET, ast::ExternProcDecl>) {
                      names.emplace(IdKeyOf(ext_decl.name),
                                    Entity{EntityKind::Value, module_path,
                                           std::nullopt, EntitySource::Decl});
                    }
                  },
                  ext_item);
            }
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            if (node.binding.pat) {
              for (const auto& name : PatNames(*node.binding.pat)) {
                names.emplace(IdKeyOf(name),
                              Entity{EntityKind::Value, module_path,
                                     std::nullopt, EntitySource::Decl});
              }
            }
          }
        },
        item);
  }
  return names;
}

source::ModuleNames ModuleNamesFromContext(const ScopeContext& ctx) {
  if (ctx.project) {
    return ModuleNamesOf(*ctx.project);
  }
  source::ModuleNames names;
  names.reserve(ctx.sigma.mods.size());
  for (const auto& mod : ctx.sigma.mods) {
    names.insert(core::StringOfPath(mod.path));
  }
  return names;
}

NameMapTable DeclNameMaps(const ScopeContext& ctx) {
  NameMapTable maps;
  for (const auto& mod : ctx.sigma.mods) {
    maps.emplace(PathKeyOf(mod.path), BuildDeclNameMap(mod.path, mod.items));
  }
  return maps;
}

bool PathPrefix(const ast::ModulePath& prefix, const ast::ModulePath& path) {
  if (prefix.size() > path.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (!IdEq(prefix[i], path[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool ImportRequired(const ast::ModulePath& current_module,
                    const ast::ModulePath& path) {
  SpecDefsCollect();
  if (current_module.empty() || path.empty()) {
    return false;
  }
  SPEC_RULE("ImportRequired");
  return !IdEq(current_module.front(), path.front());
}

bool ImportCovers(const ScopeContext& ctx,
                  const source::ModuleNames& module_names,
                  const ast::ModulePath& current_module,
                  const ast::ModulePath& path) {
  SpecDefsCollect();
  const auto* module = FindContextModuleByPath(ctx, current_module);
  if (module == nullptr) {
    return false;
  }
  SPEC_RULE("ImportCovers");
  for (const auto& item : module->items) {
    const auto* import_decl = std::get_if<ast::ImportDecl>(&item);
    if (import_decl == nullptr) {
      continue;
    }
    const auto import_path =
        source::ResolveImportModulePath(current_module,
                                        module_names,
                                        import_decl->path);
    if (!import_path.has_value()) {
      continue;
    }
    if (PathPrefix(*import_path, path)) {
      return true;
    }
  }
  return false;
}

bool ImportOk(const ScopeContext& ctx,
              const source::ModuleNames& module_names,
              const ast::ModulePath& current_module,
              const ast::ModulePath& path) {
  SpecDefsCollect();
  if (!ImportRequired(current_module, path)) {
    SPEC_RULE("Import-Ok-Local");
    return true;
  }
  if (ImportCovers(ctx, module_names, current_module, path)) {
    SPEC_RULE("Import-Ok-Covered");
    return true;
  }
  SPEC_RULE("Import-Ok-Err");
  return false;
}

// =============================================================================
// PatNames - Extract pattern binding names
// =============================================================================

std::vector<ast::Identifier> PatNames(const ast::Pattern& pat) {
  SpecDefsPatNames();
  return std::visit(
      [&](const auto& node) -> std::vector<ast::Identifier> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return {node.name};
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          SPEC_RULE("Pat-Typed");
          if (node.name == "_") {
            return {};
          }
          return {node.name};
        } else if constexpr (std::is_same_v<T, ast::WildcardPattern>) {
          SPEC_RULE("Pat-Wild");
          return {};
        } else if constexpr (std::is_same_v<T, ast::LiteralPattern>) {
          SPEC_RULE("Pat-Lit");
          return {};
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          std::vector<ast::Identifier> out;
          for (const auto& elem : node.elements) {
            const auto names = PatNames(elem);
            out.insert(out.end(), names.begin(), names.end());
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          std::vector<ast::Identifier> out;
          for (const auto& field : node.fields) {
            if (field.pattern_opt) {
              SPEC_RULE("Pat-Record-Field-Explicit");
              const auto names = PatNames(field.pattern_opt);
              out.insert(out.end(), names.begin(), names.end());
            } else {
              SPEC_RULE("Pat-Record-Field-Implicit");
              out.push_back(field.name);
            }
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt) {
            SPEC_RULE("Pat-Enum-None");
            return {};
          }
          return std::visit(
              [&](const auto& payload) -> std::vector<ast::Identifier> {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  SPEC_RULE("Pat-Enum-Tuple");
                  std::vector<ast::Identifier> out;
                  for (const auto& elem : payload.elements) {
                    const auto names = PatNames(elem);
                    out.insert(out.end(), names.begin(), names.end());
                  }
                  return out;
                } else {
                  SPEC_RULE("Pat-Enum-Record");
                  std::vector<ast::Identifier> out;
                  for (const auto& field : payload.fields) {
                    if (field.pattern_opt) {
                      SPEC_RULE("Pat-Record-Field-Explicit");
                      const auto names = PatNames(field.pattern_opt);
                      out.insert(out.end(), names.begin(), names.end());
                    } else {
                      SPEC_RULE("Pat-Record-Field-Implicit");
                      out.push_back(field.name);
                    }
                  }
                  return out;
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt) {
            return {};
          }
          std::vector<ast::Identifier> out;
          for (const auto& field : node.fields_opt->fields) {
            if (field.pattern_opt) {
              const auto names = PatNames(field.pattern_opt);
              out.insert(out.end(), names.begin(), names.end());
            } else {
              out.push_back(field.name);
            }
          }
          return out;
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          SPEC_RULE("Pat-Range");
          std::vector<ast::Identifier> out = PatNames(node.lo);
          const auto high = PatNames(node.hi);
          out.insert(out.end(), high.begin(), high.end());
          return out;
        } else {
          return {};
        }
      },
      pat.node);
}

std::vector<ast::Identifier> PatNames(const ast::PatternPtr& pat) {
  SpecDefsPatNames();
  if (!pat) {
    return {};
  }
  return PatNames(*pat);
}

// =============================================================================
// DeclNames - Extract declaration names from items
// =============================================================================

std::vector<IdKey> DeclNames(const std::vector<ast::ASTItem>& items,
                             const ast::ModulePath& module_path) {
  SpecDefsCollect();
  if (items.empty()) {
    SPEC_RULE("DeclNames-Empty");
    return {};
  }
  std::set<IdKey> names;
  for (const auto& item : items) {
    if (std::holds_alternative<ast::UsingDecl>(item)) {
      SPEC_RULE("DeclNames-Using");
      continue;
    }
    BindingsResult bindings = ItemBindings(ScopeContext{}, NameMapTable{},
                                           source::ModuleNames{},
                                           item,
                                           module_path);
    if (!bindings.ok) {
      continue;
    }
    for (const auto& binding : bindings.bindings) {
      names.insert(binding.name);
    }
    SPEC_RULE("DeclNames-Item");
  }
  return std::vector<IdKey>(names.begin(), names.end());
}

std::vector<IdKey> DeclNames(const ast::ASTModule& module) {
  SpecDefsCollect();
  return DeclNames(module.items, module.path);
}

// =============================================================================
// ItemBindings - Extract bindings from a single item
// =============================================================================

BindingsResult ItemBindings(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::ASTItem& item,
                            const ast::ModulePath& module_path) {
  SpecDefsCollect();
  return std::visit(
      [&](const auto& node) -> BindingsResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          UsingNamesResult names = UsingNames(ctx, name_maps, module_names, node);
          if (!names.ok) {
            SPEC_RULE("Bind-Using-Err");
            return {false, names.diag_id, names.span, {}};
          }
          SPEC_RULE("Bind-Using");
          return {true, std::nullopt, std::nullopt, names.bindings};
        } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          SPEC_RULE("Bind-Import");
          if (node.path.empty()) {
            SPEC_RULE("Import-Path-Err");
            return {false, "Import-Using-Missing", node.span, {}};
          }
          const auto resolved_path =
              source::ResolveImportModulePath(module_path,
                                             module_names,
                                             node.path);
          if (!resolved_path.has_value()) {
            SPEC_RULE("Import-Path-Err");
            return {false, "Resolve-Import-Err", node.span, {}};
          }
          const ast::Identifier alias_name = node.alias_opt.value_or(
              resolved_path->empty() ? ast::Identifier{} : resolved_path->back());
          if (alias_name.empty()) {
            SPEC_RULE("Import-Path-Err");
            return {false, "Import-Using-Missing", node.span, {}};
          }
          SPEC_RULE("Import-Path");
          BindingList bindings;
          bindings.push_back(BoundName{
              IdKeyOf(alias_name),
              Entity{EntityKind::ModuleAlias, *resolved_path, std::nullopt,
                     EntitySource::Import},
              node.span,
          });
          return {true, std::nullopt, std::nullopt, bindings};
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                             std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          SPEC_RULE("Bind-Procedure");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Value, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          SPEC_RULE("Bind-Procedure");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Value, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          SPEC_RULE("Bind-Procedure");
          BindingList bindings;
          for (const auto& ext_item : node.items) {
            std::visit(
                [&](const auto& ext_decl) {
                  using ET = std::decay_t<decltype(ext_decl)>;
                  if constexpr (std::is_same_v<ET, ast::ExternProcDecl>) {
                    bindings.push_back(
                        BoundName{IdKeyOf(ext_decl.name),
                                  Entity{EntityKind::Value, module_path,
                                         std::nullopt, EntitySource::Decl},
                                  ext_decl.span});
                  }
                },
                ext_item);
          }
          return {true, std::nullopt, std::nullopt, bindings};
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          SPEC_RULE("Bind-Record");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Type, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          SPEC_RULE("Bind-Enum");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Type, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Type, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          SPEC_RULE("Bind-Class");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Class, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          SPEC_RULE("Bind-TypeAlias");
          return {true,
                  std::nullopt,
                  std::nullopt,
                  {BoundName{IdKeyOf(node.name),
                             Entity{EntityKind::Type, module_path, std::nullopt,
                                    EntitySource::Decl},
                             node.span}}};
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          SPEC_RULE("Bind-Static");
          BindingList bindings;
          if (node.binding.pat) {
            for (const auto& name : PatNames(*node.binding.pat)) {
              bindings.push_back(
                  BoundName{IdKeyOf(name),
                            Entity{EntityKind::Value, module_path, std::nullopt,
                                   EntitySource::Decl},
                            node.span});
            }
          }
          return {true, std::nullopt, std::nullopt, bindings};
        } else if constexpr (std::is_same_v<T, ast::ErrorItem>) {
          SPEC_RULE("Bind-ErrorItem");
          return {true, std::nullopt, std::nullopt, {}};
        } else {
          return {true, std::nullopt, std::nullopt, {}};
        }
      },
      item);
}

// =============================================================================
// CollectNames - Collect all names from a module
// =============================================================================

CollectNamesResult CollectNames(const ScopeContext& ctx,
                                const NameMapTable& name_maps,
                                const source::ModuleNames& module_names,
                                const ast::ASTModule& module) {
  SpecDefsCollect();
  NameMap names;
  std::unordered_map<IdKey, std::optional<core::Span>> name_spans;
  auto is_main_procedure = [](const ast::ASTItem& ast_item) -> bool {
    if (const auto* proc = std::get_if<ast::ProcedureDecl>(&ast_item)) {
      return IdEq(proc->name, "main");
    }
    return false;
  };
  for (const auto& item : module.items) {
    const auto bindings =
        ItemBindings(ctx, name_maps, module_names, item, module.path);
    if (!bindings.ok) {
      SPEC_RULE("Collect-Err");
      return {false, bindings.diag_id, bindings.span, {}};
    }
    const auto overload_name = FreeProcedureOverloadName(item);
    const bool merges_free_procedure_overload =
        BindingsAreSingleOverloadProcedure(item, bindings.bindings) &&
        overload_name.has_value() &&
        names.find(*overload_name) != names.end();
    if ((!DisjointNames(bindings.bindings, names) ||
         !NoDup(bindings.bindings)) &&
        !merges_free_procedure_overload) {
      if (is_main_procedure(item) &&
          names.find(IdKeyOf("main")) != names.end()) {
        SPEC_RULE("Main-Multiple");
        return {false, "E-MOD-2430", SpanOfItem(item), {}};
      }
      if (UsingImportConflict(bindings.bindings, names)) {
        SPEC_RULE("Collect-Using-Import-Dup");
        return {false, "Import-Using-Name-Conflict", SpanOfItem(item), {}};
      }
      SPEC_RULE("Collect-Dup");
      return {false, "Collect-Dup", SpanOfItem(item), {}};
    }
    SPEC_RULE("Collect-Scan");
    for (const auto& binding : bindings.bindings) {
      if (name_spans.find(binding.name) == name_spans.end()) {
        name_spans.emplace(binding.name, binding.span);
      }
    }
    if (!merges_free_procedure_overload) {
      InsertBindings(names, bindings.bindings);
    }
  }
  SPEC_RULE("Collect-Ok");
  return {true, std::nullopt, std::nullopt, names, std::move(name_spans)};
}

// =============================================================================
// NamesStart / NamesStep - Incremental name collection state machine
// =============================================================================

NamesState NamesStart(const ast::ASTModule& module) {
  SpecDefsCollect();
  SPEC_RULE("Names-Start");
  NamesState state;
  state.kind = NamesState::Kind::Scan;
  state.module = &module;
  state.index = 0;
  state.names = NameMap{};
  return state;
}

NamesState NamesStep(const ScopeContext& ctx,
                     const NameMapTable& name_maps,
                     const source::ModuleNames& module_names,
                     const NamesState& state) {
  SpecDefsCollect();
  if (state.kind == NamesState::Kind::Error ||
      state.kind == NamesState::Kind::Done) {
    return state;
  }
  if (state.kind == NamesState::Kind::Start) {
    return NamesStart(*state.module);
  }
  NamesState next = state;
  if (!state.module) {
    next.kind = NamesState::Kind::Error;
    next.diag_id = "Names-Step-Err";
    return next;
  }
  const auto& items = state.module->items;
  if (state.index >= items.size()) {
    SPEC_RULE("Names-Done");
    next.kind = NamesState::Kind::Done;
    return next;
  }
  const auto& item = items[state.index];
  const auto bindings =
      ItemBindings(ctx, name_maps, module_names, item, state.module->path);
  if (!bindings.ok) {
    SPEC_RULE("Names-Step-Err");
    next.kind = NamesState::Kind::Error;
    next.diag_id = bindings.diag_id;
    return next;
  }
  if (!DisjointNames(bindings.bindings, state.names) ||
      !NoDup(bindings.bindings)) {
    const auto overload_name = FreeProcedureOverloadName(item);
    const bool merges_free_procedure_overload =
        BindingsAreSingleOverloadProcedure(item, bindings.bindings) &&
        overload_name.has_value() &&
        state.names.find(*overload_name) != state.names.end() &&
        state.module &&
        HasPriorFreeProcedureOverload(state.module->items, state.index,
                                      *overload_name);
    if (merges_free_procedure_overload) {
      SPEC_RULE("Names-Step");
      next.index++;
      return next;
    }

    auto is_main_procedure = [](const ast::ASTItem& ast_item) -> bool {
      if (const auto* proc = std::get_if<ast::ProcedureDecl>(&ast_item)) {
        return IdEq(proc->name, "main");
      }
      return false;
    };

    bool duplicate_main_procedure = false;
    if (state.module && is_main_procedure(item)) {
      for (std::size_t i = 0; i < state.index; ++i) {
        if (is_main_procedure(state.module->items[i])) {
          duplicate_main_procedure = true;
          break;
        }
      }
    }

    next.kind = NamesState::Kind::Error;
    if (duplicate_main_procedure) {
      SPEC_RULE("Main-Multiple");
      next.diag_id = "E-MOD-2430";
    } else if (UsingImportConflict(bindings.bindings, state.names)) {
      SPEC_RULE("Names-Step-Using-Import-Dup");
      next.diag_id = "Import-Using-Name-Conflict";
    } else {
      SPEC_RULE("Names-Step-Dup");
      next.diag_id = "Names-Step-Dup";
    }
    return next;
  }
  SPEC_RULE("Names-Step");
  InsertBindings(next.names, bindings.bindings);
  next.index++;
  return next;
}

// =============================================================================
// CollectNameMaps - Fixed-point name collection across all modules
// =============================================================================

NameMapBuildResult CollectNameMaps(ScopeContext& ctx) {
  NameMapBuildResult result;
  const ast::ModulePath saved_module = ctx.current_module;
  const source::ModuleNames module_names = ModuleNamesFromContext(ctx);
  NameMapTable current = DeclNameMaps(ctx);
  std::map<PathKey, CollectNamesResult> last_results;
  bool changed = false;
  do {
    last_results.clear();
    NameMapTable next = current;
    for (const auto& module : ctx.sigma.mods) {
      ctx.current_module = module.path;
      const auto collected =
          CollectNames(ctx, current, module_names, module);
      const auto key = PathKeyOf(module.path);
      last_results.emplace(key, collected);
      if (!collected.ok) {
        continue;
      }
      auto it = next.find(key);
      if (it == next.end() || !NameMapEquals(it->second, collected.names)) {
        next[key] = collected.names;
      }
    }
    // Check convergence after all module updates are applied.
    // This avoids non-termination when multiple modules map to the same key
    // and intermediate per-module writes are overwritten in one iteration.
    changed = !NameMapTableEquals(current, next);
    current = std::move(next);
  } while (changed);

  for (const auto& module : ctx.sigma.mods) {
    const auto key = PathKeyOf(module.path);
    const auto it = last_results.find(key);
    if (it != last_results.end() && !it->second.ok &&
        it->second.diag_id.has_value()) {
      result.diags =
          EmitCollectDiag(result.diags, *it->second.diag_id, it->second.span);
    }
  }

  ctx.current_module = saved_module;
  result.name_maps = std::move(current);
  return result;
}

}  // namespace cursive::analysis
