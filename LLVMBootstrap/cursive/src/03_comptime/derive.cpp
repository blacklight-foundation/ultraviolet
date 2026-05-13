#include "03_comptime/comptime_internal.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/symbols.h"

namespace cursive::frontend::comptime_internal {

namespace {

struct DeriveRequest {
  std::string name;
  const ast::DeriveTargetDecl* target = nullptr;
};

struct RunDeriveResult {
  CtEnv env;
  std::vector<ASTItem> items;
};

class DeriveBodyRestrictionFinder {
 public:
  std::optional<core::Span> Find(const Block& block) {
    found_span_ = std::nullopt;
    VisitBlock(block);
    return found_span_;
  }

 private:
  bool Found() const { return found_span_.has_value(); }

  void Mark(core::Span span) {
    if (!Found()) {
      found_span_ = span;
    }
  }

  void VisitBlock(const Block& block) {
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
      if (Found()) {
        return;
      }
    }
    VisitExpr(block.tail_opt);
  }

  void VisitStmt(const Stmt& stmt) {
    if (Found()) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::RegionStmt> ||
                        std::is_same_v<T, ast::FrameStmt> ||
                        std::is_same_v<T, ast::UnsafeBlockStmt> ||
                        std::is_same_v<T, ast::KeyBlockStmt>) {
            Mark(node.span);
          } else if constexpr (std::is_same_v<T, ast::LetStmt> ||
                               std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::CtStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else {
            (void)node;
          }
        },
        stmt);
  }

  void VisitExpr(const ExprPtr& expr) {
    if (!expr || Found()) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr> ||
                        std::is_same_v<T, ast::DerefExpr> ||
                        std::is_same_v<T, ast::TransmuteExpr> ||
                        std::is_same_v<T, ast::YieldExpr> ||
                        std::is_same_v<T, ast::YieldFromExpr> ||
                        std::is_same_v<T, ast::SyncExpr> ||
                        std::is_same_v<T, ast::RaceExpr> ||
                        std::is_same_v<T, ast::AllExpr> ||
                        std::is_same_v<T, ast::ParallelExpr> ||
                        std::is_same_v<T, ast::SpawnExpr> ||
                        std::is_same_v<T, ast::WaitExpr> ||
                        std::is_same_v<T, ast::DispatchExpr>) {
            Mark(expr->span);
          } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
            VisitExpr(node.body);
          } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
            VisitExpr(node.cond);
            if (node.then_block) {
              VisitBlock(*node.then_block);
            }
            if (node.else_block_opt) {
              VisitBlock(*node.else_block_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
            VisitExpr(node.iter);
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr> ||
                               std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::PropagateExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                               std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              VisitExpr(elem);
            }
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            ast::ForEachArrayExprSubexpr(node, [&](const ExprPtr& elem) {
              VisitExpr(elem);
            });
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            VisitExpr(node.value);
            VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& arm : node.cases) {
              VisitExpr(arm.body);
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            VisitExpr(node.body);
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else {
            (void)node;
          }
        },
        expr->node);
  }

  std::optional<core::Span> found_span_;
};

std::optional<std::vector<std::string>> DeriveTargetsOf(const AttributeList& attrs) {
  const auto* derive = FindAttribute(attrs, "derive");
  if (!derive) {
    return std::vector<std::string>{};
  }

  std::vector<std::string> out;
  out.reserve(derive->args.size());
  for (const auto& arg : derive->args) {
    const auto* tok = std::get_if<ast::Token>(&arg.value);
    // Malformed [[derive(... )]] arguments are rejected earlier by the shared
    // attribute validator before Phase 2 derive expansion begins.
    if (arg.key.has_value() || !tok || tok->kind != ast::TokenKind::Identifier) {
      return std::nullopt;
    }
    out.push_back(tok->lexeme);
  }
  return out;
}

std::optional<ast::TypePtr> TargetTypeOf(const ASTItem& item,
                                         const ast::ModulePath& module_path) {
  return std::visit(
      [&](const auto& node) -> std::optional<ast::TypePtr> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                      std::is_same_v<T, ast::EnumDecl> ||
                      std::is_same_v<T, ast::ModalDecl>) {
          ast::TypePathType path_type{};
          path_type.path = module_path;
          path_type.path.push_back(node.name);
          auto ty = std::make_shared<ast::Type>();
          ty->span = node.span;
          ty->node = std::move(path_type);
          return ty;
        }
        return std::nullopt;
      },
      item);
}

bool PathEq(const ast::ModulePath& lhs, const ast::ModulePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) {
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
    if (prefix[i] != path[i]) {
      return false;
    }
  }
  return true;
}

bool SameAssembly(const ast::ModulePath& lhs, const ast::ModulePath& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  return lhs.front() == rhs.front();
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

std::size_t VisibleItemLimit(const CtEnv& env, const ast::ASTModule& module) {
  if (PathEq(module.path, env.current_module)) {
    return std::min(env.current_item_index, module.items.size());
  }
  return module.items.size();
}

std::optional<ast::Visibility> ItemVisibilityOpt(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::optional<ast::Visibility> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ErrorItem>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          return ast::Visibility::Internal;
        } else {
          return node.vis;
        }
      },
      item);
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

const ast::DeriveTargetDecl* FindVisibleDeriveTargetDeclInModule(
    const CtEnv& env,
    const ast::ModulePath& module_path,
    std::string_view name) {
  const ast::ASTModule* module = FindAvailableModule(env, module_path);
  if (module == nullptr) {
    return nullptr;
  }

  const std::size_t limit = VisibleItemLimit(env, *module);
  for (std::size_t i = 0; i < limit; ++i) {
    const auto* derive = std::get_if<ast::DeriveTargetDecl>(&module->items[i]);
    if (derive != nullptr && derive->name == name) {
      return derive;
    }
  }
  return nullptr;
}

std::vector<const ast::DeriveTargetDecl*> VisibleDeriveTargetDeclsInModule(
    const CtEnv& env,
    const ast::ModulePath& module_path) {
  std::vector<const ast::DeriveTargetDecl*> out;
  const ast::ASTModule* module = FindAvailableModule(env, module_path);
  if (module == nullptr) {
    return out;
  }

  const std::size_t limit = VisibleItemLimit(env, *module);
  for (std::size_t i = 0; i < limit; ++i) {
    if (const auto* derive = std::get_if<ast::DeriveTargetDecl>(&module->items[i])) {
      out.push_back(derive);
    }
  }
  return out;
}

std::optional<ast::ModulePath> ExpandImportAliasPrefix(const CtEnv& env,
                                                       const ast::ModulePath& path) {
  if (path.empty()) {
    return std::nullopt;
  }

  const ast::ASTModule* current = FindAvailableModule(env, env.current_module);
  if (current == nullptr) {
    return std::nullopt;
  }

  const std::size_t limit = VisibleItemLimit(env, *current);
  for (std::size_t i = 0; i < limit; ++i) {
    const auto* import = std::get_if<ast::ImportDecl>(&current->items[i]);
    if (import == nullptr) {
      continue;
    }
    const auto resolved = source::ResolveImportModulePath(
        env.current_module, env.available_module_names, import->path);
    if (!resolved.has_value() ||
        FindAvailableModule(env, *resolved) == nullptr) {
      continue;
    }

    const ast::Identifier alias =
        import->alias_opt.value_or(resolved->empty() ? ast::Identifier{}
                                                     : resolved->back());
    if (alias.empty() || alias != path.front()) {
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
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  if (FindAvailableModule(env, *resolved) == nullptr) {
    return std::nullopt;
  }
  return resolved;
}

bool ImportRequired(const ast::ModulePath& current_module,
                    const ast::ModulePath& path) {
  if (current_module.empty() || path.empty()) {
    return false;
  }
  return !SameAssembly(current_module, path);
}

bool ImportCovers(const CtEnv& env, const ast::ModulePath& path) {
  const ast::ASTModule* current = FindAvailableModule(env, env.current_module);
  if (current == nullptr) {
    return false;
  }

  const std::size_t limit = VisibleItemLimit(env, *current);
  for (std::size_t i = 0; i < limit; ++i) {
    const auto* import = std::get_if<ast::ImportDecl>(&current->items[i]);
    if (import == nullptr) {
      continue;
    }
    const auto resolved = source::ResolveImportModulePath(
        env.current_module, env.available_module_names, import->path);
    if (!resolved.has_value()) {
      continue;
    }
    if (PathPrefix(*resolved, path)) {
      return true;
    }
  }
  return false;
}

bool ImportOk(const CtEnv& env, const ast::ModulePath& path) {
  if (!ImportRequired(env.current_module, path)) {
    return true;
  }
  return ImportCovers(env, path);
}

struct DeriveTargetRef {
  ast::ModulePath module_path;
  const ast::DeriveTargetDecl* decl = nullptr;
};

void AddDeriveTargetRef(std::vector<DeriveTargetRef>& out,
                        std::unordered_set<std::string>& seen,
                        const ast::ModulePath& module_path,
                        const ast::DeriveTargetDecl* decl) {
  if (decl == nullptr) {
    return;
  }
  const std::string key = core::StringOfPath(module_path) + "::" + decl->name;
  if (!seen.insert(key).second) {
    return;
  }
  out.push_back(DeriveTargetRef{module_path, decl});
}

void AppendUsingBoundDeriveTargets(std::vector<DeriveTargetRef>& out,
                                   std::unordered_set<std::string>& seen,
                                   std::string_view lookup_name,
                                   const CtEnv& env,
                                   const ast::UsingDecl& decl) {
  std::visit(
      [&](const auto& clause) {
        using T = std::decay_t<decltype(clause)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          ast::ModulePath raw_path = clause.module_path;
          raw_path.push_back(clause.name);
          if (raw_path.size() < 2) {
            return;
          }
          ast::ModulePath module_path(raw_path.begin(), raw_path.end() - 1);
          const auto resolved_module = ResolveVisibleModulePath(env, module_path);
          if (!resolved_module.has_value() || !ImportOk(env, *resolved_module)) {
            return;
          }
          const auto* target = FindVisibleDeriveTargetDeclInModule(
              env, *resolved_module, raw_path.back());
          if (target == nullptr ||
              !CanAccessVis(env.current_module, *resolved_module,
                            ast::Visibility::Internal)) {
            return;
          }
          const ast::Identifier bind_name = clause.alias_opt.value_or(raw_path.back());
          if (bind_name == lookup_name) {
            AddDeriveTargetRef(out, seen, *resolved_module, target);
          }
        } else if constexpr (std::is_same_v<T, ast::UsingList>) {
          const auto resolved_module =
              ResolveVisibleModulePath(env, clause.module_path);
          if (!resolved_module.has_value() || !ImportOk(env, *resolved_module)) {
            return;
          }
          for (const auto& spec : clause.specs) {
            const auto* target =
                FindVisibleDeriveTargetDeclInModule(env, *resolved_module, spec.name);
            if (target == nullptr ||
                !CanAccessVis(env.current_module, *resolved_module,
                              ast::Visibility::Internal)) {
              continue;
            }
            const ast::Identifier bind_name = spec.alias_opt.value_or(spec.name);
            if (bind_name == lookup_name) {
              AddDeriveTargetRef(out, seen, *resolved_module, target);
            }
          }
        } else {
          const auto resolved_module =
              ResolveVisibleModulePath(env, clause.module_path);
          if (!resolved_module.has_value() || !ImportOk(env, *resolved_module)) {
            return;
          }
          if (!CanAccessVis(env.current_module, *resolved_module,
                            ast::Visibility::Internal)) {
            return;
          }
          for (const ast::DeriveTargetDecl* target :
               VisibleDeriveTargetDeclsInModule(env, *resolved_module)) {
            if (target->name == lookup_name) {
              AddDeriveTargetRef(out, seen, *resolved_module, target);
            }
          }
        }
      },
      decl.clause);
}

const ast::DeriveTargetDecl* VisibleDeriveTarget(std::string_view name,
                                                 const CtEnv& env) {
  std::vector<DeriveTargetRef> visible;
  std::unordered_set<std::string> seen;

  AddDeriveTargetRef(
      visible, seen, env.current_module,
      FindVisibleDeriveTargetDeclInModule(env, env.current_module, name));

  const ast::ASTModule* current = FindAvailableModule(env, env.current_module);
  if (current != nullptr) {
    const std::size_t limit = VisibleItemLimit(env, *current);
    for (std::size_t i = 0; i < limit; ++i) {
      const auto* using_decl = std::get_if<ast::UsingDecl>(&current->items[i]);
      if (using_decl == nullptr) {
        continue;
      }
      AppendUsingBoundDeriveTargets(visible, seen, name, env, *using_decl);
    }
  }

  if (visible.size() != 1) {
    return nullptr;
  }
  return visible.front().decl;
}

std::optional<CtEnv> BindDeriveTargetInputs(const CtEnv& env,
                                            const ASTItem& item) {
  const auto target_type = TargetTypeOf(item, env.current_module);
  if (!target_type.has_value()) {
    return std::nullopt;
  }
  CtEnv derive_env = WithCtCaps(env, {}, true);
  derive_env.values["target"] = CtType{*target_type};
  return derive_env;
}

std::unordered_set<std::string> DeclaredImplNames(const ASTItem& item) {
  return std::visit(
      [&](const auto& node) -> std::unordered_set<std::string> {
        using T = std::decay_t<decltype(node)>;
        std::unordered_set<std::string> out;
        if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                      std::is_same_v<T, ast::EnumDecl> ||
                      std::is_same_v<T, ast::ModalDecl>) {
          for (const auto& cls : node.implements) {
            if (!cls.empty()) {
              out.insert(cls.back());
            }
          }
        }
        return out;
      },
      item);
}

bool ValidateDeriveContracts(const ASTItem& item,
                             const std::vector<DeriveRequest>& requests,
                             core::DiagnosticStream& diags) {
  const std::unordered_set<std::string> declared = DeclaredImplNames(item);
  const core::Span span = SpanOfItem(item);
  for (const auto& request : requests) {
    for (const auto& req : DeriveReqs(*request.target)) {
      if (!declared.contains(req)) {
        if (auto diag = core::MakeDiagnosticById("E-CTE-0330", span)) {
          core::Emit(diags, *diag);
        }
        return false;
      }
    }
    for (const auto& emit : DeriveEmits(*request.target)) {
      if (!declared.contains(emit)) {
        if (auto diag = core::MakeDiagnosticById("E-CTE-0331", span)) {
          core::Emit(diags, *diag);
        }
        return false;
      }
    }
  }
  return true;
}

bool DeriveEdge(const DeriveRequest& lhs, const DeriveRequest& rhs) {
  const std::vector<ast::Identifier> lhs_requires = DeriveReqs(*lhs.target);
  const std::vector<ast::Identifier> rhs_emits = DeriveEmits(*rhs.target);
  for (const auto& req : lhs_requires) {
    for (const auto& emit : rhs_emits) {
      if (req == emit) {
        return true;
      }
    }
  }
  return false;
}

bool HasErrorsSince(const core::DiagnosticStream* diags, std::size_t baseline) {
  if (diags == nullptr || baseline >= diags->size()) {
    return false;
  }
  for (std::size_t i = baseline; i < diags->size(); ++i) {
    if ((*diags)[i].severity == core::Severity::Error) {
      return true;
    }
  }
  return false;
}

std::optional<std::vector<DeriveRequest>> ResolveDeriveRequests(
    const ASTItem& item, const CtEnv& env) {
  const auto names_opt = std::visit(
      [&](const auto& node) -> std::optional<std::vector<std::string>> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                      std::is_same_v<T, ast::EnumDecl> ||
                      std::is_same_v<T, ast::ModalDecl>) {
          return DeriveTargetsOf(node.attrs);
        }
        return std::vector<std::string>{};
      },
      item);
  if (!names_opt.has_value()) {
    return std::nullopt;
  }

  std::vector<DeriveRequest> requests;
  requests.reserve(names_opt->size());
  for (const auto& name : *names_opt) {
    const ast::DeriveTargetDecl* target = VisibleDeriveTarget(name, env);
    if (target == nullptr) {
      if (auto diag = core::MakeDiagnosticById("E-CTE-0310", SpanOfItem(item))) {
        core::Emit(*env.diags, *diag);
      }
      return std::nullopt;
    }
    requests.push_back(DeriveRequest{name, target});
  }
  if (!ValidateDeriveContracts(item, requests, *env.diags)) {
    return std::nullopt;
  }
  return requests;
}

std::optional<RunDeriveResult> RunDeriveTarget(const ASTItem& item,
                                               const ast::DeriveTargetDecl& target,
                                               const CtEnv& env) {
  SPEC_RULE_AT("RunDeriveTarget", target.span);
  const std::size_t diag_count_before =
      env.diags == nullptr ? 0 : env.diags->size();
  auto derive_env_opt = BindDeriveTargetInputs(env, item);
  if (!derive_env_opt.has_value()) {
    return std::nullopt;
  }

  std::vector<ASTItem> target_emits;
  CtEnv derive_env = std::move(*derive_env_opt);
  derive_env.pending_emits = &target_emits;
  if (const auto span = DeriveBodyRestrictionFinder{}.Find(*target.body)) {
    if (auto diag = core::MakeDiagnosticById("E-CTE-0320", *span)) {
      core::Emit(*env.diags, *diag);
    }
    return std::nullopt;
  }
  const EvalResult exec = EvalBlock(*target.body, derive_env);
  if (!exec.ok || HasErrorsSince(env.diags, diag_count_before)) {
    return std::nullopt;
  }

  return RunDeriveResult{env, std::move(target_emits)};
}

std::optional<RunDeriveResult> RunDeriveSet(const ASTItem& item,
                                            const std::vector<std::string>& order,
                                            CtEnv env) {
  RunDeriveResult result{env, {}};
  if (order.empty()) {
    SPEC_RULE_AT("RunDeriveSet-Empty", SpanOfItem(item));
  }
  for (const auto& name : order) {
    SPEC_RULE_AT("RunDeriveSet-Cons", SpanOfItem(item));
    const ast::DeriveTargetDecl* target = VisibleDeriveTarget(name, result.env);
    if (target == nullptr) {
      if (auto diag = core::MakeDiagnosticById("E-CTE-0310", SpanOfItem(item))) {
        core::Emit(*result.env.diags, *diag);
      }
      return std::nullopt;
    }

    auto target_result = RunDeriveTarget(item, *target, result.env);
    if (!target_result.has_value()) {
      return std::nullopt;
    }

    result.items.insert(result.items.end(), target_result->items.begin(),
                        target_result->items.end());
    result.env = std::move(target_result->env);
  }
  return result;
}

std::optional<std::vector<std::string>> DeriveOrderFor(const ASTItem& item,
                                                       const CtEnv& env) {
  const auto requests_opt = ResolveDeriveRequests(item, env);
  if (!requests_opt.has_value()) {
    return std::nullopt;
  }

  const std::vector<DeriveRequest>& requests = *requests_opt;
  const std::size_t n = requests.size();
  std::vector<std::vector<std::size_t>> outgoing(n);
  std::vector<std::size_t> indegree(n, 0);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      if (i == j || !DeriveEdge(requests[i], requests[j])) {
        continue;
      }
      outgoing[i].push_back(j);
      ++indegree[j];
    }
  }

  std::vector<bool> emitted(n, false);
  std::vector<std::string> order;
  order.reserve(n);
  while (order.size() < n) {
    bool progressed = false;
    for (std::size_t i = 0; i < n; ++i) {
      if (emitted[i] || indegree[i] != 0) {
        continue;
      }
      emitted[i] = true;
      order.push_back(requests[i].name);
      for (const std::size_t next : outgoing[i]) {
        if (indegree[next] > 0) {
          --indegree[next];
        }
      }
      progressed = true;
      break;
    }
    if (!progressed) {
      if (auto diag = core::MakeDiagnosticById("E-CTE-0340", SpanOfItem(item))) {
        core::Emit(*env.diags, *diag);
      }
      return std::nullopt;
    }
  }

  return order;
}

}  // namespace

bool IsDeriveAnnotatedItem(const ASTItem& item) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                      std::is_same_v<T, ast::EnumDecl> ||
                      std::is_same_v<T, ast::ModalDecl>) {
          return HasAttribute(node.attrs, "derive");
        }
        return false;
      },
      item);
}

std::optional<std::vector<ASTItem>> ExpandDerives(const ASTItem& item, CtEnv& env) {
  const auto names_opt = DeriveOrderFor(item, env);
  if (!names_opt.has_value()) {
    return std::nullopt;
  }

  auto run = RunDeriveSet(item, *names_opt, env);
  if (!run.has_value()) {
    return std::nullopt;
  }
  env = std::move(run->env);
  return std::move(run->items);
}

}  // namespace cursive::frontend::comptime_internal
