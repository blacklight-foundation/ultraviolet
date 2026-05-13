#include "03_comptime/comptime_internal.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "00_core/unicode.h"
#include "02_source/ast/ast_dump.h"
#include "02_source/module_paths.h"

namespace cursive::frontend::comptime_internal {

namespace {

bool IdEq(std::string_view lhs, std::string_view rhs) {
  return core::NFC(lhs) == core::NFC(rhs);
}

bool PathEq(const ast::ModulePath& lhs, const ast::ModulePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
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

bool SameAssembly(const ast::ModulePath& lhs, const ast::ModulePath& rhs) {
  return !lhs.empty() && !rhs.empty() && IdEq(lhs.front(), rhs.front());
}

std::string ModulePathText(const ast::ModulePath& path) {
  return core::StringOfPath(path);
}

std::string VisibilityText(ast::Visibility vis) {
  switch (vis) {
    case ast::Visibility::Public:
      return "public";
    case ast::Visibility::Internal:
      return "internal";
    case ast::Visibility::Private:
      return "private";
  }
  return "internal";
}

const ast::ASTModule* FindAvailableModule(const CtEnv& env,
                                          const ast::ModulePath& path) {
  for (const ast::ASTModule* module : env.available_modules) {
    if (module != nullptr && PathEq(module->path, path)) {
      return module;
    }
  }
  return nullptr;
}

const std::vector<ASTItem>* FindVisibleModuleItems(const CtEnv& env,
                                                   const ast::ModulePath& path) {
  if (PathEq(path, env.current_module) && env.current_module_items != nullptr) {
    return env.current_module_items;
  }
  const ast::ASTModule* module = FindAvailableModule(env, path);
  if (module == nullptr) {
    return nullptr;
  }
  return &module->items;
}

bool CanAccessVis(const ast::ModulePath& accessor_module,
                  const ast::ModulePath& decl_module,
                  ast::Visibility vis) {
  switch (vis) {
    case ast::Visibility::Public:
      return true;
    case ast::Visibility::Internal:
      return SameAssembly(accessor_module, decl_module);
    case ast::Visibility::Private:
      return PathEq(accessor_module, decl_module);
  }
  return true;
}

std::optional<std::string> ItemNameOf(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                      std::is_same_v<T, ast::EnumDecl> ||
                      std::is_same_v<T, ast::ModalDecl> ||
                      std::is_same_v<T, ast::ClassDecl> ||
                      std::is_same_v<T, ast::TypeAliasDecl>) {
          return node.name;
        }
        return std::nullopt;
      },
      item);
}

std::optional<ast::Visibility> ItemVisibilityOpt(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::optional<ast::Visibility> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ErrorItem> ||
                      std::is_same_v<T, ast::DeriveTargetDecl>) {
          return std::nullopt;
        } else {
          return node.vis;
        }
      },
      item);
}

std::optional<ast::ModulePath> ExpandImportAliasPrefix(const CtEnv& env,
                                                       const ast::ModulePath& path) {
  if (path.empty()) {
    return std::nullopt;
  }

  const std::vector<ASTItem>* current_items =
      FindVisibleModuleItems(env, env.current_module);
  if (current_items == nullptr) {
    return std::nullopt;
  }

  for (const ASTItem& item : *current_items) {
    const auto* import = std::get_if<ast::ImportDecl>(&item);
    if (import == nullptr) {
      continue;
    }

    const auto resolved = source::ResolveImportModulePath(
        env.current_module, env.available_module_names, import->path);
    if (!resolved.has_value() || FindAvailableModule(env, *resolved) == nullptr) {
      continue;
    }

    const ast::Identifier alias =
        import->alias_opt.value_or(resolved->empty() ? ast::Identifier{}
                                                     : resolved->back());
    if (alias.empty() || !IdEq(alias, path.front())) {
      continue;
    }

    ast::ModulePath expanded = *resolved;
    expanded.insert(expanded.end(), path.begin() + 1, path.end());
    return expanded;
  }
  return std::nullopt;
}

std::optional<ast::ModulePath> ResolveVisibleModulePath(const CtEnv& env,
                                                        ast::ModulePath path) {
  if (path.empty()) {
    return std::nullopt;
  }
  if (const auto expanded = ExpandImportAliasPrefix(env, path)) {
    path = *expanded;
  }

  const auto resolved = source::ResolveImportModulePath(
      env.current_module, env.available_module_names, path);
  if (!resolved.has_value() || FindAvailableModule(env, *resolved) == nullptr) {
    return std::nullopt;
  }
  return resolved;
}

bool ImportRequired(const ast::ModulePath& current_module,
                    const ast::ModulePath& path) {
  return !current_module.empty() && !path.empty() &&
         !SameAssembly(current_module, path);
}

bool ImportCovers(const CtEnv& env, const ast::ModulePath& path) {
  const std::vector<ASTItem>* current_items =
      FindVisibleModuleItems(env, env.current_module);
  if (current_items == nullptr) {
    return false;
  }

  for (const ASTItem& item : *current_items) {
    const auto* import = std::get_if<ast::ImportDecl>(&item);
    if (import == nullptr) {
      continue;
    }
    const auto resolved = source::ResolveImportModulePath(
        env.current_module, env.available_module_names, import->path);
    if (resolved.has_value() && PathPrefix(*resolved, path)) {
      return true;
    }
  }
  return false;
}

bool ImportOk(const CtEnv& env, const ast::ModulePath& path) {
  return !ImportRequired(env.current_module, path) || ImportCovers(env, path);
}

enum class NamedDeclKind {
  None,
  Record,
  Enum,
  Modal,
  TypeAlias,
  Class,
};

struct NamedDeclRef {
  NamedDeclKind kind = NamedDeclKind::None;
  ast::ModulePath module_path;
  const ast::RecordDecl* record = nullptr;
  const ast::EnumDecl* enum_decl = nullptr;
  const ast::ModalDecl* modal = nullptr;
  const ast::TypeAliasDecl* alias = nullptr;
  const ast::ClassDecl* class_decl = nullptr;
};

void MaybeAddNamedDecl(std::vector<NamedDeclRef>& out,
                       const ASTItem& item,
                       const ast::ModulePath& module_path,
                       const ast::ModulePath& accessor_module,
                       std::string_view lookup_name) {
  const auto name = ItemNameOf(item);
  const auto vis = ItemVisibilityOpt(item);
  if (!name.has_value() || !vis.has_value() || !IdEq(*name, lookup_name) ||
      !CanAccessVis(accessor_module, module_path, *vis)) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          out.push_back(
              NamedDeclRef{NamedDeclKind::Record, module_path, &node});
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          out.push_back(
              NamedDeclRef{NamedDeclKind::Enum, module_path, nullptr, &node});
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          out.push_back(NamedDeclRef{NamedDeclKind::Modal, module_path, nullptr,
                                     nullptr, &node});
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          out.push_back(NamedDeclRef{NamedDeclKind::TypeAlias, module_path,
                                     nullptr, nullptr, nullptr, &node});
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          out.push_back(NamedDeclRef{NamedDeclKind::Class, module_path, nullptr,
                                     nullptr, nullptr, nullptr, &node});
        }
      },
      item);
}

std::vector<NamedDeclRef> FindNamedDeclsInModule(
    const CtEnv& env,
    const ast::ModulePath& module_path,
    const ast::ModulePath& accessor_module,
    std::string_view name) {
  std::vector<NamedDeclRef> out;
  const std::vector<ASTItem>* items = FindVisibleModuleItems(env, module_path);
  if (items == nullptr) {
    return out;
  }
  for (const ASTItem& item : *items) {
    MaybeAddNamedDecl(out, item, module_path, accessor_module, name);
  }
  return out;
}

std::vector<NamedDeclRef> FindNamedDeclsInModule(const CtEnv& env,
                                                 const ast::ModulePath& module_path,
                                                 std::string_view name) {
  return FindNamedDeclsInModule(env, module_path, env.current_module, name);
}

void AppendUsingBoundNamedDecls(std::vector<NamedDeclRef>& out,
                                const CtEnv& env,
                                const ast::ModulePath& accessor_module,
                                const ast::UsingDecl& decl,
                                std::string_view lookup_name) {
  CtEnv scoped_env = env;
  scoped_env.current_module = accessor_module;
  std::visit(
      [&](const auto& clause) {
        using T = std::decay_t<decltype(clause)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          const auto resolved_module =
              ResolveVisibleModulePath(scoped_env, clause.module_path);
          if (!resolved_module.has_value() ||
              !ImportOk(scoped_env, *resolved_module)) {
            return;
          }
          const ast::Identifier bind_name = clause.alias_opt.value_or(clause.name);
          if (!IdEq(bind_name, lookup_name)) {
            return;
          }
          auto decls =
              FindNamedDeclsInModule(env, *resolved_module, accessor_module,
                                     clause.name);
          out.insert(out.end(), decls.begin(), decls.end());
        } else if constexpr (std::is_same_v<T, ast::UsingList>) {
          const auto resolved_module =
              ResolveVisibleModulePath(scoped_env, clause.module_path);
          if (!resolved_module.has_value() ||
              !ImportOk(scoped_env, *resolved_module)) {
            return;
          }
          for (const auto& spec : clause.specs) {
            const ast::Identifier bind_name = spec.alias_opt.value_or(spec.name);
            if (!IdEq(bind_name, lookup_name)) {
              continue;
            }
            auto decls =
                FindNamedDeclsInModule(env, *resolved_module, accessor_module,
                                       spec.name);
            out.insert(out.end(), decls.begin(), decls.end());
          }
        } else if constexpr (std::is_same_v<T, ast::UsingWildcard>) {
          const auto resolved_module =
              ResolveVisibleModulePath(scoped_env, clause.module_path);
          if (!resolved_module.has_value() ||
              !ImportOk(scoped_env, *resolved_module)) {
            return;
          }
          auto decls =
              FindNamedDeclsInModule(env, *resolved_module, accessor_module,
                                     lookup_name);
          out.insert(out.end(), decls.begin(), decls.end());
        }
      },
      decl.clause);
}

void AppendUsingBoundNamedDecls(std::vector<NamedDeclRef>& out,
                                const CtEnv& env,
                                const ast::UsingDecl& decl,
                                std::string_view lookup_name) {
  AppendUsingBoundNamedDecls(out, env, env.current_module, decl, lookup_name);
}

std::vector<NamedDeclRef> ResolveNamedDeclsInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const ast::TypePath& path) {
  std::vector<NamedDeclRef> out;
  if (path.empty()) {
    return out;
  }

  if (path.size() == 1) {
    auto current =
        FindNamedDeclsInModule(env, accessor_module, accessor_module, path.front());
    out.insert(out.end(), current.begin(), current.end());

    const std::vector<ASTItem>* items =
        FindVisibleModuleItems(env, accessor_module);
    if (items != nullptr) {
      for (const ASTItem& item : *items) {
        if (const auto* using_decl = std::get_if<ast::UsingDecl>(&item)) {
          AppendUsingBoundNamedDecls(out, env, accessor_module, *using_decl,
                                     path.front());
        }
      }
    }
    return out;
  }

  ast::ModulePath module_path(path.begin(), path.end() - 1);
  CtEnv scoped_env = env;
  scoped_env.current_module = accessor_module;
  const auto resolved_module = ResolveVisibleModulePath(scoped_env, module_path);
  if (!resolved_module.has_value() || !ImportOk(scoped_env, *resolved_module)) {
    return out;
  }
  return FindNamedDeclsInModule(env, *resolved_module, accessor_module,
                                path.back());
}

std::vector<NamedDeclRef> ResolveNamedDecls(const CtEnv& env,
                                            const ast::TypePath& path) {
  return ResolveNamedDeclsInContext(env, env.current_module, path);
}

std::optional<NamedDeclRef> ResolveUniqueNamedDeclInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const ast::TypePath& path) {
  auto decls = ResolveNamedDeclsInContext(env, accessor_module, path);
  if (decls.size() == 1) {
    return decls.front();
  }
  return std::nullopt;
}

std::optional<NamedDeclRef> ResolveUniqueNamedDecl(const CtEnv& env,
                                                   const ast::TypePath& path) {
  return ResolveUniqueNamedDeclInContext(env, env.current_module, path);
}

bool HasReflectAttribute(const NamedDeclRef& decl) {
  switch (decl.kind) {
    case NamedDeclKind::Record:
      return decl.record != nullptr && HasAttribute(decl.record->attrs, "reflect");
    case NamedDeclKind::Enum:
      return decl.enum_decl != nullptr &&
             HasAttribute(decl.enum_decl->attrs, "reflect");
    case NamedDeclKind::Modal:
      return decl.modal != nullptr && HasAttribute(decl.modal->attrs, "reflect");
    default:
      return false;
  }
}

std::optional<std::string> NamedDeclName(const NamedDeclRef& decl) {
  switch (decl.kind) {
    case NamedDeclKind::Record:
      return decl.record != nullptr
                 ? std::optional<std::string>(decl.record->name)
                 : std::nullopt;
    case NamedDeclKind::Enum:
      return decl.enum_decl != nullptr
                 ? std::optional<std::string>(decl.enum_decl->name)
                 : std::nullopt;
    case NamedDeclKind::Modal:
      return decl.modal != nullptr ? std::optional<std::string>(decl.modal->name)
                                   : std::nullopt;
    case NamedDeclKind::TypeAlias:
      return decl.alias != nullptr ? std::optional<std::string>(decl.alias->name)
                                   : std::nullopt;
    case NamedDeclKind::Class:
      return decl.class_decl != nullptr
                 ? std::optional<std::string>(decl.class_decl->name)
                 : std::nullopt;
    default:
      return std::nullopt;
  }
}

ast::TypePath NamedDeclFullPath(const NamedDeclRef& decl) {
  ast::TypePath path = decl.module_path;
  if (const auto name = NamedDeclName(decl)) {
    path.push_back(*name);
  }
  return path;
}

const std::optional<ast::GenericParams>* GenericParamsOptOf(
    const NamedDeclRef& decl) {
  switch (decl.kind) {
    case NamedDeclKind::Record:
      return decl.record != nullptr ? &decl.record->generic_params : nullptr;
    case NamedDeclKind::Enum:
      return decl.enum_decl != nullptr ? &decl.enum_decl->generic_params
                                       : nullptr;
    case NamedDeclKind::Modal:
      return decl.modal != nullptr ? &decl.modal->generic_params : nullptr;
    case NamedDeclKind::TypeAlias:
      return decl.alias != nullptr ? &decl.alias->generic_params : nullptr;
    default:
      return nullptr;
  }
}

TypePtr CloneType(const TypePtr& type);

TypePtr CloneType(const TypePtr& type) {
  if (!type) {
    return nullptr;
  }

  auto out = std::make_shared<ast::Type>();
  out->span = type->span;
  out->node = std::visit(
      [&](const auto& node) -> ast::TypeNode {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim> ||
                      std::is_same_v<T, ast::TypeString> ||
                      std::is_same_v<T, ast::TypeBytes> ||
                      std::is_same_v<T, ast::TypeDynamic> ||
                      std::is_same_v<T, ast::TypeOpaque> ||
                      std::is_same_v<T, ast::TypeRangeFull>) {
          return node;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return ast::TypePermType{node.perm, CloneType(node.base)};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          ast::TypeUnion copy;
          copy.types.reserve(node.types.size());
          for (const auto& member : node.types) {
            copy.types.push_back(CloneType(member));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          ast::TypeFunc copy;
          copy.params.reserve(node.params.size());
          for (const auto& param : node.params) {
            copy.params.push_back(
                ast::TypeFuncParam{param.mode, CloneType(param.type)});
          }
          copy.ret = CloneType(node.ret);
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          ast::TypeClosure copy;
          copy.params.reserve(node.params.size());
          for (const auto& param : node.params) {
            copy.params.push_back(
                ast::TypeFuncParam{param.mode, CloneType(param.type)});
          }
          copy.ret = CloneType(node.ret);
          if (node.deps_opt.has_value()) {
            std::vector<ast::SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              deps.push_back(ast::SharedDep{dep.name, CloneType(dep.type)});
            }
            copy.deps_opt = std::move(deps);
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          ast::TypeTuple copy;
          copy.elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            copy.elements.push_back(CloneType(elem));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return ast::TypeArray{CloneType(node.element), node.length};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return ast::TypeSlice{CloneType(node.element)};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return ast::TypeSafePtr{CloneType(node.element), node.state};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return ast::TypeRawPtr{node.qual, CloneType(node.element)};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          ast::TypeModalState copy;
          copy.path = node.path;
          copy.state = node.state;
          copy.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            copy.generic_args.push_back(CloneType(arg));
          }
          ast::SyncTypeModalStateFromFields(copy);
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          ast::TypePathType copy;
          copy.path = node.path;
          copy.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            copy.generic_args.push_back(CloneType(arg));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          ast::TypeApply copy;
          copy.path = node.path;
          copy.args.reserve(node.args.size());
          for (const auto& arg : node.args) {
            copy.args.push_back(CloneType(arg));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return ast::TypeRefine{CloneType(node.base), node.predicate};
        } else if constexpr (std::is_same_v<T, ast::TypeRange>) {
          return ast::TypeRange{CloneType(node.base)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeInclusive>) {
          return ast::TypeRangeInclusive{CloneType(node.base)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFrom>) {
          return ast::TypeRangeFrom{CloneType(node.base)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeTo>) {
          return ast::TypeRangeTo{CloneType(node.base)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return ast::TypeRangeToInclusive{CloneType(node.base)};
        } else {
          return node;
        }
      },
      type->node);
  return out;
}

using AstTypeSubst = std::unordered_map<std::string, TypePtr>;

TypePtr SubstituteAstType(const TypePtr& type, const AstTypeSubst& subst) {
  if (!type) {
    return nullptr;
  }

  if (const auto* path = std::get_if<ast::TypePathType>(&type->node)) {
    if (path->path.size() == 1 && path->generic_args.empty()) {
      const auto it = subst.find(path->path.front());
      if (it != subst.end()) {
        return CloneType(it->second);
      }
    }
  }

  auto out = CloneType(type);
  out->node = std::visit(
      [&](const auto& node) -> ast::TypeNode {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return ast::TypePermType{node.perm,
                                   SubstituteAstType(node.base, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          ast::TypeUnion copy;
          copy.types.reserve(node.types.size());
          for (const auto& member : node.types) {
            copy.types.push_back(SubstituteAstType(member, subst));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          ast::TypeFunc copy;
          copy.params.reserve(node.params.size());
          for (const auto& param : node.params) {
            copy.params.push_back(ast::TypeFuncParam{
                param.mode, SubstituteAstType(param.type, subst)});
          }
          copy.ret = SubstituteAstType(node.ret, subst);
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          ast::TypeClosure copy;
          copy.params.reserve(node.params.size());
          for (const auto& param : node.params) {
            copy.params.push_back(ast::TypeFuncParam{
                param.mode, SubstituteAstType(param.type, subst)});
          }
          copy.ret = SubstituteAstType(node.ret, subst);
          if (node.deps_opt.has_value()) {
            std::vector<ast::SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              deps.push_back(
                  ast::SharedDep{dep.name, SubstituteAstType(dep.type, subst)});
            }
            copy.deps_opt = std::move(deps);
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          ast::TypeTuple copy;
          copy.elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            copy.elements.push_back(SubstituteAstType(elem, subst));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return ast::TypeArray{SubstituteAstType(node.element, subst),
                                node.length};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return ast::TypeSlice{SubstituteAstType(node.element, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return ast::TypeSafePtr{SubstituteAstType(node.element, subst),
                                  node.state};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return ast::TypeRawPtr{node.qual,
                                 SubstituteAstType(node.element, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          ast::TypeModalState copy;
          copy.path = node.path;
          copy.state = node.state;
          copy.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            copy.generic_args.push_back(SubstituteAstType(arg, subst));
          }
          ast::SyncTypeModalStateFromFields(copy);
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          ast::TypePathType copy;
          copy.path = node.path;
          copy.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            copy.generic_args.push_back(SubstituteAstType(arg, subst));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          ast::TypeApply copy;
          copy.path = node.path;
          copy.args.reserve(node.args.size());
          for (const auto& arg : node.args) {
            copy.args.push_back(SubstituteAstType(arg, subst));
          }
          return copy;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return ast::TypeRefine{SubstituteAstType(node.base, subst),
                                 node.predicate};
        } else if constexpr (std::is_same_v<T, ast::TypeRange>) {
          return ast::TypeRange{SubstituteAstType(node.base, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeInclusive>) {
          return ast::TypeRangeInclusive{SubstituteAstType(node.base, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFrom>) {
          return ast::TypeRangeFrom{SubstituteAstType(node.base, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeTo>) {
          return ast::TypeRangeTo{SubstituteAstType(node.base, subst)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return ast::TypeRangeToInclusive{
              SubstituteAstType(node.base, subst)};
        } else {
          return node;
        }
      },
      type->node);
  return out;
}

std::optional<std::vector<TypePtr>> ResolveAstGenericArgs(
    const std::optional<ast::GenericParams>* generic_params_opt,
    const std::vector<TypePtr>& provided_args) {
  if (generic_params_opt == nullptr || !generic_params_opt->has_value() ||
      generic_params_opt->value().params.empty()) {
    return provided_args.empty() ? std::optional<std::vector<TypePtr>>(
                                       std::vector<TypePtr>{})
                                 : std::nullopt;
  }

  const auto& params = generic_params_opt->value().params;
  if (provided_args.size() > params.size()) {
    return std::nullopt;
  }

  std::vector<TypePtr> out;
  out.reserve(params.size());
  AstTypeSubst subst;
  for (std::size_t i = 0; i < params.size(); ++i) {
    TypePtr arg;
    if (i < provided_args.size()) {
      arg = SubstituteAstType(provided_args[i], subst);
    } else {
      if (!params[i].default_type) {
        return std::nullopt;
      }
      arg = SubstituteAstType(params[i].default_type, subst);
    }
    subst[params[i].name] = arg;
    out.push_back(arg);
  }
  return out;
}

TypePtr InstantiateAstDeclType(const TypePtr& type,
                               const std::optional<ast::GenericParams>* params,
                               const std::vector<TypePtr>& args) {
  if (!type) {
    return nullptr;
  }
  if (params == nullptr || !params->has_value() || params->value().params.empty()) {
    return CloneType(type);
  }
  AstTypeSubst subst;
  const auto& type_params = params->value().params;
  const std::size_t n = std::min(type_params.size(), args.size());
  for (std::size_t i = 0; i < n; ++i) {
    subst[type_params[i].name] = args[i];
  }
  return SubstituteAstType(type, subst);
}

TypePtr CanonicalizeReflectTypeInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const TypePtr& type,
    std::unordered_set<std::string>& alias_visiting);

TypePtr ExpandReflectAliasTypeInContext(const CtEnv& env,
                                        const ast::ModulePath& accessor_module,
                                        const ast::TypePath& path,
                                        const std::vector<TypePtr>& provided_args,
                                        std::unordered_set<std::string>& alias_visiting) {
  const auto decl = ResolveUniqueNamedDeclInContext(env, accessor_module, path);
  if (!decl.has_value() || decl->kind != NamedDeclKind::TypeAlias ||
      decl->alias == nullptr || !decl->alias->type) {
    return nullptr;
  }

  const std::string alias_key = core::StringOfPath(NamedDeclFullPath(*decl));
  if (!alias_visiting.insert(alias_key).second) {
    return nullptr;
  }
  const auto args = ResolveAstGenericArgs(GenericParamsOptOf(*decl), provided_args);
  if (!args.has_value()) {
    alias_visiting.erase(alias_key);
    return nullptr;
  }
  auto expanded =
      InstantiateAstDeclType(decl->alias->type, GenericParamsOptOf(*decl), *args);
  expanded = CanonicalizeReflectTypeInContext(env, decl->module_path, expanded,
                                              alias_visiting);
  alias_visiting.erase(alias_key);
  return expanded;
}

struct ReflectNominalTarget {
  NamedDeclRef decl;
  std::vector<TypePtr> generic_args;
};

std::optional<ReflectNominalTarget> ResolveReflectNominalTargetInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const TypePtr& type,
    std::unordered_set<std::string>& alias_visiting);

std::optional<ReflectNominalTarget> ResolvePathReflectTarget(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const ast::TypePath& path,
    const std::vector<TypePtr>& provided_args,
    std::unordered_set<std::string>& alias_visiting) {
  const auto decl = ResolveUniqueNamedDeclInContext(env, accessor_module, path);
  if (!decl.has_value()) {
    return std::nullopt;
  }

  if (decl->kind == NamedDeclKind::TypeAlias) {
    const auto expanded = ExpandReflectAliasTypeInContext(
        env, accessor_module, path, provided_args, alias_visiting);
    if (!expanded) {
      return std::nullopt;
    }
    const auto result = ResolveReflectNominalTargetInContext(
        env, decl->module_path, expanded, alias_visiting);
    return result;
  }

  if (decl->kind != NamedDeclKind::Record && decl->kind != NamedDeclKind::Enum &&
      decl->kind != NamedDeclKind::Modal) {
    return std::nullopt;
  }

  const auto args =
      ResolveAstGenericArgs(GenericParamsOptOf(*decl), provided_args);
  if (!args.has_value()) {
    return std::nullopt;
  }
  return ReflectNominalTarget{*decl, std::move(*args)};
}

std::optional<ReflectNominalTarget> ResolveReflectNominalTargetInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const TypePtr& type,
    std::unordered_set<std::string>& alias_visiting) {
  if (!type) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<ReflectNominalTarget> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return ResolveReflectNominalTargetInContext(env, accessor_module,
                                                      node.base, alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return ResolveReflectNominalTargetInContext(env, accessor_module,
                                                      node.base, alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          return ResolvePathReflectTarget(env, accessor_module, node.path,
                                          node.generic_args, alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          return ResolvePathReflectTarget(env, accessor_module, node.path,
                                          node.args, alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          return ResolvePathReflectTarget(env, accessor_module, node.path,
                                          node.generic_args, alias_visiting);
        } else {
          return std::nullopt;
        }
      },
      type->node);
}

std::optional<ReflectNominalTarget> ResolveReflectNominalTarget(
    const CtEnv& env,
    const TypePtr& type) {
  std::unordered_set<std::string> alias_visiting;
  return ResolveReflectNominalTargetInContext(env, env.current_module, type,
                                              alias_visiting);
}

TypePtr CanonicalizeReflectTypeInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const TypePtr& type,
    std::unordered_set<std::string>& alias_visiting) {
  if (!type) {
    return nullptr;
  }

  return std::visit(
      [&](const auto& node) -> TypePtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return CanonicalizeReflectTypeInContext(env, accessor_module, node.base,
                                                  alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return CanonicalizeReflectTypeInContext(env, accessor_module, node.base,
                                                  alias_visiting);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          if (auto expanded = ExpandReflectAliasTypeInContext(
                  env, accessor_module, node.path, node.generic_args,
                  alias_visiting)) {
            return expanded;
          }
          if (const auto target = ResolvePathReflectTarget(
                  env, accessor_module, node.path, node.generic_args,
                  alias_visiting)) {
            auto out = std::make_shared<ast::Type>();
            out->span = type->span;
            ast::TypePathType canonical;
            canonical.path = NamedDeclFullPath(target->decl);
            canonical.generic_args.reserve(target->generic_args.size());
            for (const auto& arg : target->generic_args) {
              canonical.generic_args.push_back(CloneType(arg));
            }
            out->node = std::move(canonical);
            return out;
          }
          return CloneType(type);
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          if (auto expanded = ExpandReflectAliasTypeInContext(
                  env, accessor_module, node.path, node.args, alias_visiting)) {
            return expanded;
          }
          if (const auto target = ResolvePathReflectTarget(
                  env, accessor_module, node.path, node.args, alias_visiting)) {
            auto out = std::make_shared<ast::Type>();
            out->span = type->span;
            ast::TypePathType canonical;
            canonical.path = NamedDeclFullPath(target->decl);
            canonical.generic_args.reserve(target->generic_args.size());
            for (const auto& arg : target->generic_args) {
              canonical.generic_args.push_back(CloneType(arg));
            }
            out->node = std::move(canonical);
            return out;
          }
          return CloneType(type);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          if (auto expanded = ExpandReflectAliasTypeInContext(
                  env, accessor_module, node.path, node.generic_args,
                  alias_visiting)) {
            return expanded;
          }
          if (const auto target = ResolvePathReflectTarget(
                  env, accessor_module, node.path, node.generic_args,
                  alias_visiting)) {
            auto out = std::make_shared<ast::Type>();
            out->span = type->span;
            ast::TypeModalState canonical;
            canonical.path = NamedDeclFullPath(target->decl);
            canonical.state = node.state;
            canonical.generic_args.reserve(target->generic_args.size());
            for (const auto& arg : target->generic_args) {
              canonical.generic_args.push_back(CloneType(arg));
            }
            ast::SyncTypeModalStateFromFields(canonical);
            out->node = std::move(canonical);
            return out;
          }
          return CloneType(type);
        } else {
          return CloneType(type);
        }
      },
      type->node);
}

TypePtr CanonicalizeReflectType(const CtEnv& env, const TypePtr& type) {
  std::unordered_set<std::string> alias_visiting;
  return CanonicalizeReflectTypeInContext(env, env.current_module, type,
                                          alias_visiting);
}

ast::ClassPath ResolveReflectClassPathInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const ast::ClassPath& path) {
  if (const auto decl =
          ResolveUniqueNamedDeclInContext(env, accessor_module, path);
      decl.has_value() && decl->kind == NamedDeclKind::Class) {
    return NamedDeclFullPath(*decl);
  }
  return path;
}

std::vector<ast::ClassPath> ResolveReflectClassPathListInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const std::vector<ast::ClassPath>& paths) {
  std::vector<ast::ClassPath> out;
  out.reserve(paths.size());
  for (const auto& path : paths) {
    out.push_back(ResolveReflectClassPathInContext(env, accessor_module, path));
  }
  return out;
}

std::string ReflectClassPathKey(const ast::ClassPath& path) {
  return core::StringOfPath(path);
}

std::optional<NamedDeclRef> ResolveReflectClassDeclInContext(
    const CtEnv& env,
    const ast::ModulePath& accessor_module,
    const ast::ClassPath& path) {
  const auto resolved = ResolveReflectClassPathInContext(env, accessor_module, path);
  const auto decl = ResolveUniqueNamedDeclInContext(env, accessor_module, resolved);
  if (decl.has_value() && decl->kind == NamedDeclKind::Class &&
      decl->class_decl != nullptr) {
    return decl;
  }
  return std::nullopt;
}

bool ReflectClassSubtypesInContext(const CtEnv& env,
                                   const ast::ModulePath& accessor_module,
                                   const ast::ClassPath& sub,
                                   const ast::ClassPath& sup,
                                   std::unordered_set<std::string>& visiting) {
  const auto resolved_sub =
      ResolveReflectClassPathInContext(env, accessor_module, sub);
  const auto resolved_sup =
      ResolveReflectClassPathInContext(env, accessor_module, sup);
  if (PathEq(resolved_sub, resolved_sup)) {
    return true;
  }

  const std::string visit_key = ReflectClassPathKey(resolved_sub);
  if (!visiting.insert(visit_key).second) {
    return false;
  }

  const auto decl =
      ResolveReflectClassDeclInContext(env, accessor_module, resolved_sub);
  if (!decl.has_value() || decl->class_decl == nullptr) {
    visiting.erase(visit_key);
    return false;
  }

  for (const auto& super : decl->class_decl->supers) {
    if (ReflectClassSubtypesInContext(env, decl->module_path, super,
                                      resolved_sup, visiting)) {
      visiting.erase(visit_key);
      return true;
    }
  }

  visiting.erase(visit_key);
  return false;
}

const std::vector<ast::ClassPath>* ReflectNominalImplementsList(
    const ReflectNominalTarget& target) {
  switch (target.decl.kind) {
    case NamedDeclKind::Record:
      return target.decl.record != nullptr ? &target.decl.record->implements
                                           : nullptr;
    case NamedDeclKind::Enum:
      return target.decl.enum_decl != nullptr
                 ? &target.decl.enum_decl->implements
                 : nullptr;
    case NamedDeclKind::Modal:
      return target.decl.modal != nullptr ? &target.decl.modal->implements
                                          : nullptr;
    default:
      return nullptr;
  }
}

bool ReflectNominalImplementsForm(const CtEnv& env,
                                  const ReflectNominalTarget& target,
                                  const ast::ClassPath& form_path) {
  const auto* impls = ReflectNominalImplementsList(target);
  if (impls == nullptr) {
    return false;
  }

  const auto resolved_form =
      ResolveReflectClassPathInContext(env, target.decl.module_path, form_path);
  for (const auto& impl : *impls) {
    std::unordered_set<std::string> visiting;
    if (ReflectClassSubtypesInContext(env, target.decl.module_path, impl,
                                      resolved_form, visiting)) {
      return true;
    }
  }
  return false;
}

CtValue MakeTypeCategoryValue(std::string_view variant) {
  auto value = std::make_shared<CtEnum>();
  value->path = {"TypeCategory"};
  value->variant = std::string(variant);
  value->payload = std::monostate{};
  return value;
}

CtValue MakeStringArrayValue(const std::vector<std::string>& values) {
  auto slice = std::make_shared<CtSlice>();
  slice->elements.reserve(values.size());
  for (const auto& value : values) {
    slice->elements.push_back(CtString{value});
  }
  return slice;
}

CtValue MakeTypeArrayValue(const std::vector<TypePtr>& values) {
  auto slice = std::make_shared<CtSlice>();
  slice->elements.reserve(values.size());
  for (const auto& value : values) {
    slice->elements.push_back(CtType{value});
  }
  return slice;
}

CtValue MakeFieldInfoValue(std::string_view name,
                           const TypePtr& type,
                           ast::Visibility vis,
                           std::size_t index,
                           const core::Span& span) {
  auto record = std::make_shared<CtRecord>();
  record->path = {"FieldInfo"};
  record->fields = {
      {"name", CtString{std::string(name)}},
      {"type", CtType{type}},
      {"visibility", CtString{VisibilityText(vis)}},
      {"index", MakeCtInt(static_cast<unsigned long long>(index), "usize")},
      {"span", MakeSpanValue(span)},
  };
  return record;
}

std::string PayloadKindText(const std::optional<ast::VariantPayload>& payload_opt) {
  if (!payload_opt.has_value()) {
    return "unit";
  }
  if (std::holds_alternative<ast::VariantPayloadTuple>(*payload_opt)) {
    return "tuple";
  }
  return "record";
}

std::vector<TypePtr> PayloadTypes(const std::optional<ast::VariantPayload>& payload_opt) {
  std::vector<TypePtr> out;
  if (!payload_opt.has_value()) {
    return out;
  }
  if (const auto* tuple = std::get_if<ast::VariantPayloadTuple>(&*payload_opt)) {
    out = tuple->elements;
  } else if (const auto* record =
                 std::get_if<ast::VariantPayloadRecord>(&*payload_opt)) {
    out.reserve(record->fields.size());
    for (const auto& field : record->fields) {
      out.push_back(field.type);
    }
  }
  return out;
}

std::vector<std::string> PayloadFieldNames(
    const std::optional<ast::VariantPayload>& payload_opt) {
  std::vector<std::string> out;
  if (!payload_opt.has_value()) {
    return out;
  }
  if (const auto* record =
          std::get_if<ast::VariantPayloadRecord>(&*payload_opt)) {
    out.reserve(record->fields.size());
    for (const auto& field : record->fields) {
      out.push_back(field.name);
    }
  }
  return out;
}

CtValue MakeVariantInfoValue(std::string_view name,
                             std::string payload_kind,
                             std::vector<TypePtr> payload_types,
                             std::vector<std::string> field_names,
                             const core::Span& span) {
  auto record = std::make_shared<CtRecord>();
  record->path = {"VariantInfo"};
  record->fields = {
      {"name", CtString{std::string(name)}},
      {"payload_kind", CtString{std::move(payload_kind)}},
      {"payload_types", MakeTypeArrayValue(payload_types)},
      {"field_names", MakeStringArrayValue(field_names)},
      {"span", MakeSpanValue(span)},
  };
  return record;
}

CtValue MakeStateInfoValue(const ast::StateBlock& state) {
  std::vector<std::string> field_names;
  std::vector<std::string> method_names;
  std::vector<std::string> transition_names;
  for (const auto& member : state.members) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::StateFieldDecl>) {
            field_names.push_back(node.name);
          } else if constexpr (std::is_same_v<T, ast::StateMethodDecl>) {
            method_names.push_back(node.name);
          } else if constexpr (std::is_same_v<T, ast::TransitionDecl>) {
            transition_names.push_back(node.name);
          }
        },
        member);
  }

  auto record = std::make_shared<CtRecord>();
  record->path = {"StateInfo"};
  record->fields = {
      {"name", CtString{state.name}},
      {"field_names", MakeStringArrayValue(field_names)},
      {"method_names", MakeStringArrayValue(method_names)},
      {"transition_names", MakeStringArrayValue(transition_names)},
      {"span", MakeSpanValue(state.span)},
  };
  return record;
}

std::optional<CtValue> CategoryOfNamedDecl(const NamedDeclRef& decl) {
  switch (decl.kind) {
    case NamedDeclKind::Record:
      return MakeTypeCategoryValue("Record");
    case NamedDeclKind::Enum:
      return MakeTypeCategoryValue("Enum");
    case NamedDeclKind::Modal:
      return MakeTypeCategoryValue("Modal");
    default:
      return std::nullopt;
  }
}

std::optional<CtValue> CategoryOfType(const CtEnv& env,
                                      const TypePtr& type,
                                      std::unordered_set<const ast::Type*>& visiting) {
  if (!type) {
    return std::nullopt;
  }
  if (!visiting.insert(type.get()).second) {
    return std::nullopt;
  }

  const auto result = std::visit(
      [&](const auto& node) -> std::optional<CtValue> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          return MakeTypeCategoryValue("Primitive");
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return CategoryOfType(env, node.base, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return CategoryOfType(env, node.base, visiting);
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          return MakeTypeCategoryValue("Tuple");
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return MakeTypeCategoryValue("Array");
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return MakeTypeCategoryValue("Slice");
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          return MakeTypeCategoryValue("Union");
        } else if constexpr (std::is_same_v<T, ast::TypeFunc> ||
                             std::is_same_v<T, ast::TypeClosure>) {
          return MakeTypeCategoryValue("Procedure");
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr> ||
                             std::is_same_v<T, ast::TypeRawPtr>) {
          return MakeTypeCategoryValue("Reference");
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return MakeTypeCategoryValue("Dynamic");
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          return MakeTypeCategoryValue("Opaque");
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          return MakeTypeCategoryValue("String");
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          return MakeTypeCategoryValue("Bytes");
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          return MakeTypeCategoryValue("Modal");
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          if (const auto decl = ResolveUniqueNamedDecl(env, node.path)) {
            if (decl->kind == NamedDeclKind::TypeAlias && decl->alias != nullptr) {
              std::unordered_set<std::string> alias_visiting;
              const auto expanded = ExpandReflectAliasTypeInContext(
                  env, env.current_module, node.path, node.generic_args,
                  alias_visiting);
              if (expanded) {
                return CategoryOfType(env, expanded, visiting);
              }
              return std::nullopt;
            }
            return CategoryOfNamedDecl(*decl);
          }
          if (node.path.size() == 1) {
            return MakeTypeCategoryValue("Generic");
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          if (const auto decl = ResolveUniqueNamedDecl(env, node.path)) {
            if (decl->kind == NamedDeclKind::TypeAlias && decl->alias != nullptr) {
              std::unordered_set<std::string> alias_visiting;
              const auto expanded = ExpandReflectAliasTypeInContext(
                  env, env.current_module, node.path, node.args, alias_visiting);
              if (expanded) {
                return CategoryOfType(env, expanded, visiting);
              }
              return std::nullopt;
            }
            return CategoryOfNamedDecl(*decl);
          }
          if (node.path.size() == 1) {
            return MakeTypeCategoryValue("Generic");
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFull>) {
          return MakeTypeCategoryValue("Range");
        } else {
          return std::nullopt;
        }
      },
      type->node);

  visiting.erase(type.get());
  return result;
}

bool ReflectableType(const CtEnv& env, const TypePtr& type) {
  if (!type) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim> ||
                      std::is_same_v<T, ast::TypeTuple> ||
                      std::is_same_v<T, ast::TypeArray> ||
                      std::is_same_v<T, ast::TypeSlice> ||
                      std::is_same_v<T, ast::TypeUnion>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return ReflectableType(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return ReflectableType(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::TypePathType> ||
                             std::is_same_v<T, ast::TypeApply> ||
                             std::is_same_v<T, ast::TypeModalState>) {
          const auto target = ResolveReflectNominalTarget(env, type);
          if (target.has_value()) {
            return HasReflectAttribute(target->decl);
          }
          if constexpr (std::is_same_v<T, ast::TypePathType>) {
            std::unordered_set<std::string> alias_visiting;
            if (const auto expanded = ExpandReflectAliasTypeInContext(
                    env, env.current_module, node.path, node.generic_args,
                    alias_visiting)) {
              return ReflectableType(env, expanded);
            }
          } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
            std::unordered_set<std::string> alias_visiting;
            if (const auto expanded = ExpandReflectAliasTypeInContext(
                    env, env.current_module, node.path, node.args,
                    alias_visiting)) {
              return ReflectableType(env, expanded);
            }
          } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
            std::unordered_set<std::string> alias_visiting;
            if (const auto expanded = ExpandReflectAliasTypeInContext(
                    env, env.current_module, node.path, node.generic_args,
                    alias_visiting)) {
              return ReflectableType(env, expanded);
            }
          }
          return false;
        } else {
          return false;
        }
      },
      type->node);
}

EvalResult MakeArrayResult(std::vector<CtValue> values) {
  EvalResult result;
  result.ok = true;
  auto slice = std::make_shared<CtSlice>();
  slice->elements = std::move(values);
  result.value = slice;
  return result;
}

EvalResult MakeBoolResult(bool value) {
  EvalResult result;
  result.ok = true;
  result.value = MakeCtBool(value);
  return result;
}

EvalResult MakeStringResult(std::string value) {
  EvalResult result;
  result.ok = true;
  result.value = CtString{std::move(value)};
  return result;
}

void EmitReflectionDeclDiag(CtEnv& env, const core::Span& span) {
  EmitComptimeDiag(env, "E-CTE-0053", span);
}

core::Span ReflectionDiagSpan(const ast::MethodCallExpr& call) {
  if (!call.args.empty() && call.args.front().value) {
    return call.args.front().value->span;
  }
  if (call.receiver) {
    return call.receiver->span;
  }
  return core::Span{};
}

}  // namespace

std::optional<EvalResult> EvalIntrospectMethod(const ast::MethodCallExpr& call,
                                               CtEnv& env) {
  if (!call.receiver ||
      !std::holds_alternative<ast::IdentifierExpr>(call.receiver->node)) {
    return std::nullopt;
  }

  const auto& recv = std::get<ast::IdentifierExpr>(call.receiver->node).name;
  if (!IdEq(recv, "introspect")) {
    return std::nullopt;
  }

  auto eval_type_arg = [&](std::size_t index) -> std::optional<TypePtr> {
    if (index >= call.args.size()) {
      return std::nullopt;
    }
    const auto value = EvalExpr(call.args[index].value, env);
    if (!value.ok) {
      return std::nullopt;
    }
    const auto* ct_type = std::get_if<CtType>(&value.value);
    if (!ct_type) {
      return std::nullopt;
    }
    return ct_type->type;
  };

  if (IdEq(call.name, "category")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    std::unordered_set<const ast::Type*> visiting;
    EvalResult result;
    if (const auto category = CategoryOfType(env, *type, visiting)) {
      SPEC_RULE("CtBuiltin-Reflect-Category");
      result.ok = true;
      result.value = *category;
    }
    return std::optional<EvalResult>{std::move(result)};
  }

  if (IdEq(call.name, "type_name")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    SPEC_RULE("CtBuiltin-Reflect-TypeName");
    return std::optional<EvalResult>{MakeStringResult(ast::to_string(**type))};
  }

  if (IdEq(call.name, "module_path")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    SPEC_RULE("CtBuiltin-Reflect-ModulePath");
    return std::optional<EvalResult>{std::visit(
        [&](const auto& node) -> EvalResult {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::TypePathType>) {
            if (node.path.size() >= 2) {
              return MakeStringResult(
                  ModulePathText(ast::ModulePath(node.path.begin(),
                                                 node.path.end() - 1)));
            }
          }
          return MakeStringResult("");
        },
        (*type)->node)};
  }

  if (IdEq(call.name, "fields")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    if (!ReflectableType(env, *type)) {
      EmitReflectionDeclDiag(env, ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    const auto target = ResolveReflectNominalTarget(env, *type);
    if (!target.has_value() || target->decl.kind != NamedDeclKind::Record ||
        target->decl.record == nullptr) {
      EmitComptimeDiag(env, "E-CTE-0050", ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    std::vector<CtValue> infos;
    std::size_t field_index = 0;
    for (const auto& member : target->decl.record->members) {
      if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
        const auto instantiated_type = InstantiateAstDeclType(
            field->type, GenericParamsOptOf(target->decl), target->generic_args);
        infos.push_back(MakeFieldInfoValue(field->name, instantiated_type,
                                           field->vis, field_index, field->span));
        ++field_index;
      }
    }
    SPEC_RULE("CtBuiltin-Reflect-Fields");
    return std::optional<EvalResult>{MakeArrayResult(std::move(infos))};
  }

  if (IdEq(call.name, "variants")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    if (!ReflectableType(env, *type)) {
      EmitReflectionDeclDiag(env, ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    const auto target = ResolveReflectNominalTarget(env, *type);
    if (!target.has_value() || target->decl.kind != NamedDeclKind::Enum ||
        target->decl.enum_decl == nullptr) {
      EmitComptimeDiag(env, "E-CTE-0051", ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    std::vector<CtValue> infos;
    infos.reserve(target->decl.enum_decl->variants.size());
    for (const auto& variant : target->decl.enum_decl->variants) {
      std::vector<TypePtr> payload_types;
      if (variant.payload_opt.has_value()) {
        if (const auto* tuple =
                std::get_if<ast::VariantPayloadTuple>(&*variant.payload_opt)) {
          payload_types.reserve(tuple->elements.size());
          for (const auto& elem : tuple->elements) {
            payload_types.push_back(InstantiateAstDeclType(
                elem, GenericParamsOptOf(target->decl), target->generic_args));
          }
        } else if (const auto* record =
                       std::get_if<ast::VariantPayloadRecord>(
                           &*variant.payload_opt)) {
          payload_types.reserve(record->fields.size());
          for (const auto& field : record->fields) {
            payload_types.push_back(InstantiateAstDeclType(
                field.type, GenericParamsOptOf(target->decl),
                target->generic_args));
          }
        }
      }
      infos.push_back(MakeVariantInfoValue(
          variant.name, PayloadKindText(variant.payload_opt),
          std::move(payload_types), PayloadFieldNames(variant.payload_opt),
          variant.span));
    }
    SPEC_RULE("CtBuiltin-Reflect-Variants");
    return std::optional<EvalResult>{MakeArrayResult(std::move(infos))};
  }

  if (IdEq(call.name, "states")) {
    const auto type = eval_type_arg(0);
    if (!type.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }
    if (!ReflectableType(env, *type)) {
      EmitReflectionDeclDiag(env, ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    const auto target = ResolveReflectNominalTarget(env, *type);
    if (!target.has_value() || target->decl.kind != NamedDeclKind::Modal ||
        target->decl.modal == nullptr) {
      EmitComptimeDiag(env, "E-CTE-0052", ReflectionDiagSpan(call));
      return std::optional<EvalResult>{EvalResult{}};
    }

    std::vector<CtValue> infos;
    infos.reserve(target->decl.modal->states.size());
    for (const auto& state : target->decl.modal->states) {
      infos.push_back(MakeStateInfoValue(state));
    }
    SPEC_RULE("CtBuiltin-Reflect-States");
    return std::optional<EvalResult>{MakeArrayResult(std::move(infos))};
  }

  if (IdEq(call.name, "implements_form")) {
    const auto type = eval_type_arg(0);
    const auto form = eval_type_arg(1);
    if (!type.has_value() || !form.has_value()) {
      return std::optional<EvalResult>{EvalResult{}};
    }

    ast::ClassPath form_path;
    if (const auto* form_node = std::get_if<ast::TypePathType>(&(*form)->node)) {
      form_path =
          ResolveReflectClassPathInContext(env, env.current_module, form_node->path);
    } else if (const auto* form_node =
                   std::get_if<ast::TypeApply>(&(*form)->node)) {
      form_path =
          ResolveReflectClassPathInContext(env, env.current_module, form_node->path);
    } else {
      return std::optional<EvalResult>{MakeBoolResult(false)};
    }
    const auto target = ResolveReflectNominalTarget(env, *type);
    if (!target.has_value()) {
      return std::optional<EvalResult>{MakeBoolResult(false)};
    }
    SPEC_RULE("CtBuiltin-Reflect-Form");
    return std::optional<EvalResult>{
        MakeBoolResult(ReflectNominalImplementsForm(env, *target, form_path))};
  }

  return std::nullopt;
}

}  // namespace cursive::frontend::comptime_internal
