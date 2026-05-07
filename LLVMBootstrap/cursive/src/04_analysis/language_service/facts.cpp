#include "04_analysis/language_service/facts.h"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "00_core/symbols.h"
#include "02_source/ast/ast_dump.h"
#include "04_analysis/resolve/resolve_items.h"

namespace cursive::analysis {

namespace {

std::string FileKey(const std::filesystem::path& path) {
  std::string key = path.lexically_normal().generic_string();
#ifdef _WIN32
  for (char& ch : key) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
#endif
  return key;
}

bool SameFile(const core::Span& span, const std::filesystem::path& path) {
  return FileKey(span.file) == FileKey(path);
}

bool Contains(const core::Span& span,
              const std::filesystem::path& path,
              std::size_t byte_offset) {
  return SameFile(span, path) && span.start_offset <= byte_offset &&
         byte_offset <= span.end_offset;
}

std::size_t Width(const core::Span& span) {
  return span.end_offset >= span.start_offset
             ? span.end_offset - span.start_offset
             : 0;
}

std::string DocText(const ast::DocList& docs) {
  std::string out;
  for (const auto& doc : docs) {
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += doc.text;
  }
  return out;
}

std::string DocText(const std::optional<ast::DocList>& docs) {
  return docs.has_value() ? DocText(*docs) : std::string();
}

std::string AstTypeText(const ast::TypePtr& type) {
  if (!type) {
    return "()";
  }
  ast::DumpOptions options;
  options.include_spans = false;
  options.include_docs = false;
  return ast::to_string(*type, options);
}

std::string ParamLabel(const ast::Param& param) {
  std::string out;
  if (param.mode.has_value()) {
    out += "move ";
  }
  out += param.name;
  out += ": ";
  out += AstTypeText(param.type);
  return out;
}

std::vector<LanguageParameterInfo> ParameterInfo(
    const std::vector<ast::Param>& params) {
  std::vector<LanguageParameterInfo> out;
  out.reserve(params.size());
  for (const auto& param : params) {
    out.push_back(LanguageParameterInfo{param.name, ParamLabel(param)});
  }
  return out;
}

std::string CallableSignatureLabel(std::string_view name,
                                   const std::vector<ast::Param>& params,
                                   const ast::TypePtr& return_type_opt) {
  std::string out(name);
  out += "(";
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += ParamLabel(params[i]);
  }
  out += ")";
  if (return_type_opt) {
    out += " -> ";
    out += AstTypeText(return_type_opt);
  }
  return out;
}

std::string Qualified(std::string_view module_path, std::string_view name) {
  std::string out(module_path);
  if (!out.empty()) {
    out += "::";
  }
  out += name;
  return out;
}

std::string Qualified(const ast::ModulePath& module_path,
                      std::string_view name) {
  return Qualified(core::StringOfPath(module_path), name);
}

std::string EntityKindPrefix(EntityKind kind) {
  switch (kind) {
    case EntityKind::Value:
      return "value";
    case EntityKind::Type:
      return "type";
    case EntityKind::Class:
      return "class";
    case EntityKind::ModuleAlias:
      return "module";
  }
  return "entity";
}

EntityKind EntityKindForSymbolKind(LanguageSymbolKind kind) {
  switch (kind) {
    case LanguageSymbolKind::Record:
    case LanguageSymbolKind::Enum:
    case LanguageSymbolKind::Modal:
    case LanguageSymbolKind::TypeAlias:
    case LanguageSymbolKind::State:
      return EntityKind::Type;
    case LanguageSymbolKind::Class:
      return EntityKind::Class;
    case LanguageSymbolKind::Module:
      return EntityKind::ModuleAlias;
    default:
      return EntityKind::Value;
  }
}

std::string TopLevelSymbolId(EntityKind kind,
                             const ast::ModulePath& module_path,
                             std::string_view name) {
  std::string out = EntityKindPrefix(kind);
  out += ":";
  out += Qualified(module_path, name);
  return out;
}

std::string MemberSymbolId(std::string_view module_path,
                           std::string_view owner,
                           std::string_view name) {
  std::string out = "member:";
  if (!module_path.empty()) {
    out += module_path;
    out += "::";
  }
  out += owner;
  out += "::";
  out += name;
  return out;
}

std::string LocalSymbolId(std::string_view name, const core::Span& span) {
  return "local:" + FileKey(span.file) + ":" +
         std::to_string(span.start_offset) + ":" +
         std::to_string(span.end_offset) + ":" + std::string(name);
}

std::optional<std::string> SymbolIdForEntity(const Entity& entity,
                                             std::string_view fallback_name) {
  if (!entity.language_symbol_id.empty()) {
    return entity.language_symbol_id;
  }
  if (!entity.origin_opt.has_value()) {
    return std::nullopt;
  }
  const std::string name = entity.target_opt.value_or(std::string(fallback_name));
  if (name.empty()) {
    return std::nullopt;
  }
  return TopLevelSymbolId(entity.kind, *entity.origin_opt, name);
}

std::optional<std::string> ModulePathAt(
    const std::vector<LanguageSymbolInfo>& symbols,
    const std::filesystem::path& path,
    std::size_t byte_offset) {
  const LanguageSymbolInfo* best = nullptr;
  for (const auto& symbol : symbols) {
    if (symbol.module_path.empty() || !Contains(symbol.range, path, byte_offset)) {
      continue;
    }
    if (best == nullptr || Width(symbol.range) < Width(best->range)) {
      best = &symbol;
    }
  }
  if (best != nullptr) {
    return best->module_path;
  }
  for (const auto& symbol : symbols) {
    if (symbol.module_path.empty() || !SameFile(symbol.range, path)) {
      continue;
    }
    return symbol.module_path;
  }
  return std::nullopt;
}

void AddDeclaration(LanguageServiceIndex& index,
                    std::string id,
                    const ast::ModulePath& module,
                    std::string name,
                    LanguageSymbolKind kind,
                    const core::Span& range,
                    std::string detail,
                    std::string documentation,
                    std::string signature_label = {},
                    std::vector<LanguageParameterInfo> parameters = {}) {
  const std::string module_path = core::StringOfPath(module);
  LanguageSymbolInfo symbol;
  symbol.id = std::move(id);
  symbol.name = std::move(name);
  symbol.qualified_name = Qualified(module_path, symbol.name);
  symbol.module_path = module_path;
  symbol.kind = kind;
  symbol.range = range;
  symbol.selection_range = range;
  symbol.detail = std::move(detail);
  symbol.documentation = std::move(documentation);
  symbol.signature_label = std::move(signature_label);
  symbol.parameters = std::move(parameters);
  const std::string symbol_id = symbol.id;
  index.AddSymbol(std::move(symbol));
  index.AddReference(LanguageReference{symbol_id, range, true});
}

void AddMemberDeclaration(LanguageServiceIndex& index,
                          const ast::ModulePath& module,
                          std::string owner,
                          std::string name,
                          LanguageSymbolKind kind,
                          const core::Span& range,
                          std::string detail,
                          std::string documentation,
                          std::string signature_label = {},
                          std::vector<LanguageParameterInfo> parameters = {}) {
  const std::string module_path = core::StringOfPath(module);
  const std::string id = MemberSymbolId(module_path, owner, name);
  LanguageSymbolInfo symbol;
  symbol.id = id;
  symbol.name = std::move(name);
  const std::string member_path = owner + "::" + symbol.name;
  symbol.qualified_name = Qualified(module_path, member_path);
  symbol.module_path = module_path;
  symbol.kind = kind;
  symbol.range = range;
  symbol.selection_range = range;
  symbol.detail = std::move(detail);
  symbol.documentation = std::move(documentation);
  symbol.signature_label = std::move(signature_label);
  symbol.parameters = std::move(parameters);
  const std::string symbol_id = symbol.id;
  index.AddSymbol(std::move(symbol));
  index.AddReference(LanguageReference{symbol_id, range, true});
}

void AddTopLevelDeclaration(LanguageServiceIndex& index,
                            const ast::ModulePath& module,
                            std::string name,
                            EntityKind entity_kind,
                            LanguageSymbolKind symbol_kind,
                            const core::Span& range,
                            std::string detail,
                            std::string documentation,
                            std::string signature_label = {},
                            std::vector<LanguageParameterInfo> parameters = {}) {
  const std::string id = TopLevelSymbolId(entity_kind, module, name);
  AddDeclaration(index, id, module, std::move(name), symbol_kind, range,
                 std::move(detail), std::move(documentation),
                 std::move(signature_label), std::move(parameters));
}

void AddPatternDeclarations(LanguageServiceIndex& index,
                            const ast::ModulePath& module,
                            const ast::PatternPtr& pattern,
                            LanguageSymbolKind kind,
                            std::string_view detail) {
  if (!pattern) {
    return;
  }
  for (const auto& name : PatNames(pattern)) {
    AddTopLevelDeclaration(index, module, name, EntityKind::Value, kind,
                           pattern->span, std::string(detail), {});
  }
}

void AddLocalDeclaration(LanguageServiceIndex& index,
                         const ScopeContext& ctx,
                         std::string_view name,
                         const core::Span& span,
                         LanguageSymbolKind kind,
                         std::string detail,
                         std::string id) {
  const std::string module_path = core::StringOfPath(CurrentModule(ctx));
  LanguageSymbolInfo symbol;
  symbol.id = std::move(id);
  symbol.name = std::string(name);
  symbol.qualified_name = Qualified(module_path, symbol.name);
  symbol.module_path = module_path;
  symbol.kind = kind;
  symbol.range = span;
  symbol.selection_range = span;
  symbol.detail = std::move(detail);
  symbol.is_local = true;
  symbol.include_in_outline = kind != LanguageSymbolKind::Parameter;
  symbol.include_in_workspace = false;
  const std::string symbol_id = symbol.id;
  index.AddSymbol(std::move(symbol));
  index.AddReference(LanguageReference{symbol_id, span, true});
}

}  // namespace

void LanguageServiceIndex::AddSymbol(LanguageSymbolInfo symbol) {
  if (symbol.id.empty()) {
    return;
  }
  if (symbol_offsets_.find(symbol.id) != symbol_offsets_.end()) {
    return;
  }
  symbol_offsets_.emplace(symbol.id, symbols_.size());
  symbols_.push_back(std::move(symbol));
}

void LanguageServiceIndex::AddReference(LanguageReference reference) {
  if (reference.symbol_id.empty()) {
    return;
  }
  references_.push_back(std::move(reference));
}

const LanguageSymbolInfo* LanguageServiceIndex::SymbolById(
    std::string_view id) const {
  const auto it = symbol_offsets_.find(std::string(id));
  if (it == symbol_offsets_.end()) {
    return nullptr;
  }
  return &symbols_[it->second];
}

std::vector<const LanguageSymbolInfo*> LanguageServiceIndex::SymbolsInFile(
    const std::filesystem::path& path) const {
  std::vector<const LanguageSymbolInfo*> out;
  for (const auto& symbol : symbols_) {
    if (SameFile(symbol.range, path)) {
      out.push_back(&symbol);
    }
  }
  std::sort(out.begin(), out.end(), [](const LanguageSymbolInfo* lhs,
                                       const LanguageSymbolInfo* rhs) {
    return lhs->range.start_offset < rhs->range.start_offset;
  });
  return out;
}

const LanguageSymbolInfo* LanguageServiceIndex::SymbolAt(
    const std::filesystem::path& path,
    std::size_t byte_offset) const {
  const LanguageSymbolInfo* best = nullptr;
  for (const auto& symbol : symbols_) {
    if (!Contains(symbol.selection_range, path, byte_offset)) {
      continue;
    }
    if (best == nullptr || Width(symbol.selection_range) <
                               Width(best->selection_range)) {
      best = &symbol;
    }
  }
  return best;
}

const LanguageReference* LanguageServiceIndex::ReferenceAt(
    const std::filesystem::path& path,
    std::size_t byte_offset) const {
  const LanguageReference* best = nullptr;
  for (const auto& reference : references_) {
    if (!Contains(reference.range, path, byte_offset)) {
      continue;
    }
    if (best == nullptr || Width(reference.range) < Width(best->range)) {
      best = &reference;
    }
  }
  return best;
}

const LanguageSymbolInfo* LanguageServiceIndex::ResolvedSymbolAt(
    const std::filesystem::path& path,
    std::size_t byte_offset) const {
  if (const auto* reference = ReferenceAt(path, byte_offset)) {
    if (const auto* symbol = SymbolById(reference->symbol_id)) {
      return symbol;
    }
  }
  return SymbolAt(path, byte_offset);
}

std::vector<const LanguageReference*>
LanguageServiceIndex::ReferencesForSymbol(std::string_view symbol_id,
                                          bool include_declaration) const {
  std::vector<const LanguageReference*> out;
  for (const auto& reference : references_) {
    if (reference.symbol_id != symbol_id) {
      continue;
    }
    if (!include_declaration && reference.is_declaration) {
      continue;
    }
    out.push_back(&reference);
  }
  std::sort(out.begin(), out.end(), [](const LanguageReference* lhs,
                                       const LanguageReference* rhs) {
    const int file_cmp = FileKey(lhs->range.file).compare(FileKey(rhs->range.file));
    if (file_cmp != 0) {
      return file_cmp < 0;
    }
    return lhs->range.start_offset < rhs->range.start_offset;
  });
  return out;
}

std::vector<const LanguageSymbolInfo*> LanguageServiceIndex::CompletionSymbols(
    const std::filesystem::path& path,
    std::size_t byte_offset) const {
  std::vector<const LanguageSymbolInfo*> local_symbols;
  std::vector<const LanguageSymbolInfo*> module_symbols;
  std::vector<const LanguageSymbolInfo*> workspace_symbols;
  const auto module_path = ModulePathAt(symbols_, path, byte_offset);

  for (const auto& symbol : symbols_) {
    if (symbol.is_local) {
      if (SameFile(symbol.range, path) &&
          symbol.range.start_offset <= byte_offset) {
        local_symbols.push_back(&symbol);
      }
      continue;
    }
    if (module_path.has_value() && symbol.module_path == *module_path) {
      module_symbols.push_back(&symbol);
      continue;
    }
    if (symbol.include_in_workspace) {
      workspace_symbols.push_back(&symbol);
    }
  }

  std::sort(local_symbols.begin(), local_symbols.end(),
            [](const LanguageSymbolInfo* lhs,
               const LanguageSymbolInfo* rhs) {
              return lhs->range.start_offset > rhs->range.start_offset;
            });

  std::vector<const LanguageSymbolInfo*> out;
  out.reserve(local_symbols.size() + module_symbols.size() +
              workspace_symbols.size());
  std::unordered_set<std::string> seen_ids;
  const auto append = [&](const std::vector<const LanguageSymbolInfo*>& bucket) {
    for (const auto* symbol : bucket) {
      if (symbol == nullptr || !seen_ids.insert(symbol->id).second) {
        continue;
      }
      out.push_back(symbol);
    }
  };
  append(local_symbols);
  append(module_symbols);
  append(workspace_symbols);
  return out;
}

LanguageServiceIndex BuildLanguageServiceDeclarations(
    const std::vector<ast::ASTModule>& modules) {
  LanguageServiceIndex index;
  for (const auto& module : modules) {
    for (const auto& item : module.items) {
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Value,
                                     LanguageSymbolKind::Function, node.span,
                                     "procedure", DocText(node.doc),
                                     CallableSignatureLabel(
                                         node.name, node.params,
                                         node.return_type_opt),
                                     ParameterInfo(node.params));
            } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Value,
                                     LanguageSymbolKind::Function, node.span,
                                     "comptime procedure", DocText(node.doc),
                                     CallableSignatureLabel(
                                         node.name, node.params,
                                         node.return_type_opt),
                                     ParameterInfo(node.params));
            } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
              AddPatternDeclarations(index, module.path, node.binding.pat,
                                     LanguageSymbolKind::Constant,
                                     "module storage");
            } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Type,
                                     LanguageSymbolKind::Record, node.span,
                                     "record", DocText(node.doc));
              for (const auto& member : node.members) {
                std::visit(
                    [&](const auto& member_node) {
                      using M = std::decay_t<decltype(member_node)>;
                      if constexpr (std::is_same_v<M, ast::FieldDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, member_node.name,
                            LanguageSymbolKind::Field, member_node.span,
                            "record field", DocText(member_node.doc_opt));
                      } else if constexpr (std::is_same_v<M, ast::MethodDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, member_node.name,
                            LanguageSymbolKind::Method, member_node.span,
                            "record method", DocText(member_node.doc_opt),
                            CallableSignatureLabel(
                                member_node.name, member_node.params,
                                member_node.return_type_opt),
                            ParameterInfo(member_node.params));
                      } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, member_node.name,
                            LanguageSymbolKind::TypeAlias, member_node.span,
                            "associated type", DocText(member_node.doc_opt));
                      }
                    },
                    member);
              }
            } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Type,
                                     LanguageSymbolKind::Enum, node.span,
                                     "enum", DocText(node.doc));
              for (const auto& variant : node.variants) {
                AddMemberDeclaration(index, module.path, node.name,
                                     variant.name,
                                     LanguageSymbolKind::EnumMember,
                                     variant.span, "enum variant",
                                     DocText(variant.doc_opt));
              }
            } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Type,
                                     LanguageSymbolKind::Modal, node.span,
                                     "modal", DocText(node.doc));
              for (const auto& state : node.states) {
                AddMemberDeclaration(index, module.path, node.name, state.name,
                                     LanguageSymbolKind::State, state.span,
                                     "modal state", DocText(state.doc_opt));
                const std::string state_owner = node.name + "::" + state.name;
                for (const auto& member : state.members) {
                  std::visit(
                      [&](const auto& member_node) {
                        using M = std::decay_t<decltype(member_node)>;
                        if constexpr (std::is_same_v<M, ast::StateFieldDecl>) {
                          AddMemberDeclaration(
                              index, module.path, state_owner, member_node.name,
                              LanguageSymbolKind::Field, member_node.span,
                              "modal state field", DocText(member_node.doc_opt));
                        } else if constexpr (
                            std::is_same_v<M, ast::StateMethodDecl>) {
                          AddMemberDeclaration(
                              index, module.path, state_owner, member_node.name,
                              LanguageSymbolKind::Method, member_node.span,
                              "modal state method", DocText(member_node.doc_opt),
                              CallableSignatureLabel(
                                  member_node.name, member_node.params,
                                  member_node.return_type_opt),
                              ParameterInfo(member_node.params));
                        } else if constexpr (std::is_same_v<M, ast::TransitionDecl>) {
                          AddMemberDeclaration(
                              index, module.path, state_owner, member_node.name,
                              LanguageSymbolKind::Method, member_node.span,
                              "modal transition", DocText(member_node.doc_opt),
                              CallableSignatureLabel(member_node.name,
                                                     member_node.params,
                                                     nullptr),
                              ParameterInfo(member_node.params));
                        }
                      },
                      member);
                }
              }
            } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Class,
                                     LanguageSymbolKind::Class, node.span,
                                     node.modal ? "modal class" : "class",
                                     DocText(node.doc));
              for (const auto& item : node.items) {
                std::visit(
                    [&](const auto& item_node) {
                      using C = std::decay_t<decltype(item_node)>;
                      if constexpr (std::is_same_v<C, ast::ClassFieldDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, item_node.name,
                            LanguageSymbolKind::Field, item_node.span,
                            "class field", DocText(item_node.doc_opt));
                      } else if constexpr (
                          std::is_same_v<C, ast::ClassMethodDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, item_node.name,
                            LanguageSymbolKind::Method, item_node.span,
                            "class method", DocText(item_node.doc_opt),
                            CallableSignatureLabel(
                                item_node.name, item_node.params,
                                item_node.return_type_opt),
                            ParameterInfo(item_node.params));
                      } else if constexpr (
                          std::is_same_v<C, ast::AssociatedTypeDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, item_node.name,
                            LanguageSymbolKind::TypeAlias, item_node.span,
                            "associated type", DocText(item_node.doc_opt));
                      } else if constexpr (
                          std::is_same_v<C, ast::AbstractFieldDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, item_node.name,
                            LanguageSymbolKind::Field, item_node.span,
                            "abstract field", DocText(item_node.doc_opt));
                      } else if constexpr (
                          std::is_same_v<C, ast::AbstractStateDecl>) {
                        AddMemberDeclaration(
                            index, module.path, node.name, item_node.name,
                            LanguageSymbolKind::State, item_node.span,
                            "abstract state", DocText(item_node.doc_opt));
                        const std::string state_owner =
                            node.name + "::" + item_node.name;
                        for (const auto& field : item_node.fields) {
                          AddMemberDeclaration(
                              index, module.path, state_owner, field.name,
                              LanguageSymbolKind::Field, field.span,
                              "abstract state field", DocText(field.doc_opt));
                        }
                      }
                    },
                    item);
              }
            } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
              AddTopLevelDeclaration(index, module.path, node.name,
                                     EntityKind::Type,
                                     LanguageSymbolKind::TypeAlias, node.span,
                                     "type alias", DocText(node.doc));
            }
          },
          item);
    }
  }
  return index;
}

std::string LanguageSymbolKindName(LanguageSymbolKind kind) {
  switch (kind) {
    case LanguageSymbolKind::Module:
      return "module";
    case LanguageSymbolKind::Function:
      return "procedure";
    case LanguageSymbolKind::Method:
      return "method";
    case LanguageSymbolKind::Variable:
      return "variable";
    case LanguageSymbolKind::Constant:
      return "constant";
    case LanguageSymbolKind::Field:
      return "field";
    case LanguageSymbolKind::Record:
      return "record";
    case LanguageSymbolKind::Enum:
      return "enum";
    case LanguageSymbolKind::EnumMember:
      return "enum variant";
    case LanguageSymbolKind::Modal:
      return "modal";
    case LanguageSymbolKind::Class:
      return "class";
    case LanguageSymbolKind::TypeAlias:
      return "type alias";
    case LanguageSymbolKind::State:
      return "state";
    case LanguageSymbolKind::Parameter:
      return "parameter";
  }
  return "symbol";
}

Entity MakeLanguageServiceLocalEntity(LanguageServiceIndex* index,
                                      const ScopeContext& ctx,
                                      std::string_view name,
                                      const core::Span& declaration_span,
                                      LanguageSymbolKind kind,
                                      std::string detail) {
  const std::string id = LocalSymbolId(name, declaration_span);
  if (index != nullptr) {
    AddLocalDeclaration(*index, ctx, name, declaration_span, kind, detail, id);
  }
  Entity entity{EntityKindForSymbolKind(kind), std::nullopt, std::nullopt,
                EntitySource::Decl};
  entity.declaration_span = declaration_span;
  entity.language_symbol_id = id;
  return entity;
}

void RecordLanguageServiceReference(LanguageServiceIndex* index,
                                    const ScopeContext& ctx,
                                    std::string_view fallback_name,
                                    const core::Span& reference_span,
                                    const Entity& entity) {
  (void)ctx;
  if (index == nullptr) {
    return;
  }
  const auto symbol_id = SymbolIdForEntity(entity, fallback_name);
  if (!symbol_id.has_value()) {
    return;
  }
  index->AddReference(LanguageReference{*symbol_id, reference_span, false});
}

void RecordLanguageServiceTypePathReference(LanguageServiceIndex* index,
                                            const ScopeContext& ctx,
                                            const ast::TypePath& path,
                                            const core::Span& reference_span) {
  if (index == nullptr || path.empty()) {
    return;
  }
  Entity entity;
  entity.kind = EntityKind::Type;
  entity.source = EntitySource::Decl;
  entity.origin_opt = ast::ModulePath(path.begin(), path.end() - 1);
  entity.target_opt = path.back();
  RecordLanguageServiceReference(index, ctx, path.back(), reference_span,
                                 entity);
}

void RecordLanguageServiceMemberReference(LanguageServiceIndex* index,
                                          const ast::TypePath& owner_path,
                                          std::string_view member_name,
                                          const core::Span& reference_span) {
  if (index == nullptr || owner_path.empty()) {
    return;
  }
  const ast::ModulePath module_path(owner_path.begin(), owner_path.end() - 1);
  const std::string symbol_id =
      MemberSymbolId(core::StringOfPath(module_path), owner_path.back(),
                     member_name);
  index->AddReference(LanguageReference{symbol_id, reference_span, false});
}

TypeRef LanguageServiceTypeAt(const ExprTypeMap& expr_types,
                              const std::filesystem::path& path,
                              std::size_t byte_offset) {
  const ast::Expr* best_expr = nullptr;
  TypeRef best_type;
  for (const auto& [expr, type] : expr_types) {
    if (expr == nullptr || !type || !Contains(expr->span, path, byte_offset)) {
      continue;
    }
    if (best_expr == nullptr || Width(expr->span) < Width(best_expr->span)) {
      best_expr = expr;
      best_type = type;
    }
  }
  return best_type;
}

}  // namespace cursive::analysis
