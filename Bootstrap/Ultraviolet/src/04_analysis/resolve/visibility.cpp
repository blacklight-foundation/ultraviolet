#include "04_analysis/resolve/visibility.h"

#include <optional>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsVisibility() {
  SPEC_DEF("DeclOf", "5.1.4");
  SPEC_DEF("ModuleOf", "5.1.4");
  SPEC_DEF("ModuleOfPath", "5.1.4");
  SPEC_DEF("Vis", "5.1.4");
  SPEC_DEF("TopLevelDecl", "5.1.4");
  SPEC_DEF("AsmOfPath", "5.1.4");
  SPEC_DEF("SameAssembly", "5.1.4");
}

// §5.1.4: AsmOfPath(p) = p[0] - first path component is assembly name
// §5.1.4: SameAssembly(m₁, m₂) ⇔ AsmOfModule(m₁) = AsmOfModule(m₂)
bool SameAssembly(const ast::ModulePath& m1, const ast::ModulePath& m2) {
  if (m1.empty() || m2.empty()) {
    return false;
  }
  return m1[0] == m2[0];
}

std::optional<ast::Visibility> VisOpt(const ast::ASTItem& item) {
  SpecDefsVisibility();
  return std::visit(
      [](const auto& it) -> std::optional<ast::Visibility> {
        using T = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<T, ast::ErrorItem>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          return ast::Visibility::Internal;
        } else {
          return it.vis;
        }
      },
      item);
}

bool NameMatches(const IdKey& key, std::string_view candidate) {
  return key == IdKeyOf(candidate);
}

bool PatternBindsName(const ast::Pattern& pattern, const IdKey& key);

bool PatternBindsName(const ast::PatternPtr& pattern, const IdKey& key) {
  if (!pattern) {
    return false;
  }
  return PatternBindsName(*pattern, key);
}

bool FieldPatternBindsName(const ast::FieldPattern& field,
                           const IdKey& key) {
  if (field.pattern_opt) {
    return PatternBindsName(field.pattern_opt, key);
  }
  return NameMatches(key, field.name);
}

bool RecordFieldsBindName(const std::vector<ast::FieldPattern>& fields,
                          const IdKey& key) {
  for (const auto& field : fields) {
    if (FieldPatternBindsName(field, key)) {
      return true;
    }
  }
  return false;
}

bool PatternBindsName(const ast::Pattern& pattern, const IdKey& key) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name == "_") {
            return false;
          }
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::WildcardPattern> ||
                             std::is_same_v<T, ast::LiteralPattern>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (PatternBindsName(elem, key)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          return RecordFieldsBindName(node.fields, key);
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt) {
            return false;
          }
          return std::visit(
              [&](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    if (PatternBindsName(elem, key)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  return RecordFieldsBindName(payload.fields, key);
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt) {
            return false;
          }
          return RecordFieldsBindName(node.fields_opt->fields, key);
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return PatternBindsName(node.lo, key) || PatternBindsName(node.hi, key);
        } else {
          return false;
        }
      },
      pattern.node);
}

bool UsingClauseBindsName(const ast::UsingClause& clause,
                          const IdKey& key) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          if (node.alias_opt) {
            return NameMatches(key, *node.alias_opt);
          }
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::UsingWildcard>) {
          // Wildcard binds all visible names from the module.
          (void)node;
          return true;
        } else {
          for (const auto& spec : node.specs) {
            if (spec.alias_opt) {
              if (NameMatches(key, *spec.alias_opt)) {
                return true;
              }
              continue;
            }
            if (NameMatches(key, spec.name)) {
              return true;
            }
          }
          return false;
        }
      },
      clause);
}

bool ItemBindsName(const ast::ASTItem& item, const IdKey& key) {
  return std::visit(
      [&](const auto& it) -> bool {
        using T = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          return UsingClauseBindsName(it.clause, key);
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& extern_item : it.items) {
            const auto* proc = std::get_if<ast::ExternProcDecl>(&extern_item);
            if (proc && NameMatches(key, proc->name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          if (!it.binding.pat) {
            return false;
          }
          return PatternBindsName(*it.binding.pat, key);
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                             std::is_same_v<T, ast::ComptimeProcedureDecl> ||
                             std::is_same_v<T, ast::DeriveTargetDecl> ||
                             std::is_same_v<T, ast::RecordDecl> ||
                             std::is_same_v<T, ast::EnumDecl> ||
                             std::is_same_v<T, ast::ModalDecl> ||
                             std::is_same_v<T, ast::ClassDecl> ||
                             std::is_same_v<T, ast::TypeAliasDecl>) {
          return NameMatches(key, it.name);
        } else {
          return false;
        }
      },
      item);
}

std::optional<ast::Visibility> FindDeclVisibilityByName(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    std::string_view name) {
  const auto* module = FindContextModuleByPath(ctx, module_path);
  if (!module) {
    return std::nullopt;
  }

  const auto key = IdKeyOf(name);
  for (const auto& item : module->items) {
    if (const auto* block = std::get_if<ast::ExternBlock>(&item)) {
      for (const auto& extern_item : block->items) {
        const auto* proc = std::get_if<ast::ExternProcDecl>(&extern_item);
        if (proc && NameMatches(key, proc->name)) {
          return proc->vis;
        }
      }
    }

    if (ItemBindsName(item, key)) {
      return VisOpt(item);
    }
  }

  return std::nullopt;
}

bool HasModulePath(const ScopeContext& ctx, const ast::ModulePath& path) {
  return FindContextModuleByPath(ctx, path) != nullptr;
}

std::optional<ast::ModulePath> ResolveVisibleModulePath(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path) {
  if (module_path.empty()) {
    return std::nullopt;
  }
  if (HasModulePath(ctx, module_path)) {
    return module_path;
  }
  if (CurrentModule(ctx).empty()) {
    return std::nullopt;
  }
  ast::ModulePath candidate;
  candidate.reserve(module_path.size() + 1);
  candidate.push_back(CurrentModule(ctx).front());
  candidate.insert(candidate.end(), module_path.begin(), module_path.end());
  if (HasModulePath(ctx, candidate)) {
    return candidate;
  }
  return std::nullopt;
}

const ast::ASTItem* FindDeclByName(const ScopeContext& ctx,
                                   const ast::ModulePath& module_path,
                                   std::string_view name) {
  const auto* module = FindContextModuleByPath(ctx, module_path);
  if (!module) {
    return nullptr;
  }
  const auto key = IdKeyOf(name);
  const ast::ASTItem* using_fallback = nullptr;
  for (const auto& item : module->items) {
    if (ItemBindsName(item, key)) {
      if (std::holds_alternative<ast::UsingDecl>(item)) {
        if (!using_fallback) {
          using_fallback = &item;
        }
        continue;
      }
      return &item;
    }
  }
  return using_fallback;
}

std::optional<core::Span> SpanOfItem(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& it) -> std::optional<core::Span> { return it.span; },
      item);
}

void EmitDiag(core::DiagnosticStream& diags,
              std::string_view code,
              const std::optional<core::Span>& span) {
  if (auto diag = core::MakeDiagnosticById(code, span)) {
    core::Emit(diags, *diag);
  }
}

void CheckExpr(const ScopeContext& ctx,
               const ast::Expr& expr,
               core::DiagnosticStream& diags);

void CheckExpr(const ScopeContext& ctx,
               const ast::ExprPtr& expr,
               core::DiagnosticStream& diags) {
  if (!expr) {
    return;
  }
  CheckExpr(ctx, *expr, diags);
}

void CheckArgs(const ScopeContext& ctx,
               const std::vector<ast::Arg>& args,
               core::DiagnosticStream& diags) {
  for (const auto& arg : args) {
    CheckExpr(ctx, arg.value, diags);
  }
}

void CheckFieldInits(const ScopeContext& ctx,
                     const std::vector<ast::FieldInit>& fields,
                     core::DiagnosticStream& diags) {
  for (const auto& field : fields) {
    CheckExpr(ctx, field.value, diags);
  }
}

void CheckApplyArgs(const ScopeContext& ctx,
                    const ast::ApplyArgs& args,
                    core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          CheckArgs(ctx, node.args, diags);
        } else {
          CheckFieldInits(ctx, node.fields, diags);
        }
      },
      args);
}

void CheckBlock(const ScopeContext& ctx,
                const ast::Block& block,
                core::DiagnosticStream& diags);

void CheckStmt(const ScopeContext& ctx,
               const ast::Stmt& stmt,
               core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          CheckExpr(ctx, node.binding.init, diags);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          (void)node;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          CheckExpr(ctx, node.place, diags);
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          CheckExpr(ctx, node.opts_opt, diags);
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          CheckExpr(ctx, node.value_opt, diags);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          CheckExpr(ctx, node.value_opt, diags);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else {
          return;
        }
      },
      stmt);
}

void CheckBlock(const ScopeContext& ctx,
                const ast::Block& block,
                core::DiagnosticStream& diags) {
  for (const auto& stmt : block.stmts) {
    CheckStmt(ctx, stmt, diags);
  }
  CheckExpr(ctx, block.tail_opt, diags);
}

void CheckExpr(const ScopeContext& ctx,
               const ast::Expr& expr,
               core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          const auto access = CanAccess(ctx, node.path, node.name);
          if (!access.ok && access.diag_id == "Access-Err") {
            EmitDiag(diags, "E-MOD-1207", expr.span);
          }
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          const auto access = CanAccess(ctx, node.path, node.name);
          if (!access.ok && access.diag_id == "Access-Err") {
            EmitDiag(diags, "E-MOD-1207", expr.span);
          }
          CheckApplyArgs(ctx, node.args, diags);
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt) {
            return;
          }
          std::visit(
              [&](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& elem : payload.elements) {
                    CheckExpr(ctx, elem, diags);
                  }
                } else if constexpr (std::is_same_v<P,
                                                    ast::EnumPayloadBrace>) {
                  CheckFieldInits(ctx, payload.fields, diags);
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          CheckExpr(ctx, node.lhs, diags);
          CheckExpr(ctx, node.rhs, diags);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CheckExpr(ctx, node.lhs, diags);
          CheckExpr(ctx, node.rhs, diags);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          CheckExpr(ctx, node.place, diags);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          CheckExpr(ctx, node.place, diags);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            CheckExpr(ctx, elem, diags);
          }
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
          CheckExpr(ctx, elem, diags);
        });
      } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
        CheckExpr(ctx, node.value, diags);
        CheckExpr(ctx, node.count, diags);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          // sizeof(type) - type visibility is checked during type resolution
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          // alignof(type) - type visibility is checked during type resolution
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          CheckFieldInits(ctx, node.fields, diags);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          CheckExpr(ctx, node.cond, diags);
          CheckExpr(ctx, node.then_expr, diags);
          CheckExpr(ctx, node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          CheckExpr(ctx, node.scrutinee, diags);
          for (const auto& case_clause : node.cases) {
            CheckExpr(ctx, case_clause.body, diags);
          }
          CheckExpr(ctx, node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          CheckExpr(ctx, node.scrutinee, diags);
          CheckExpr(ctx, node.then_expr, diags);
          CheckExpr(ctx, node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          CheckExpr(ctx, node.cond, diags);
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          CheckExpr(ctx, node.iter, diags);
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            CheckBlock(ctx, *node.block, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            CheckBlock(ctx, *node.block, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CheckExpr(ctx, node.base, diags);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          CheckExpr(ctx, node.base, diags);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CheckExpr(ctx, node.base, diags);
          CheckExpr(ctx, node.index, diags);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CheckExpr(ctx, node.callee, diags);
          CheckArgs(ctx, node.args, diags);
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          CheckExpr(ctx, node.callee, diags);
          CheckArgs(ctx, node.args, diags);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CheckExpr(ctx, node.receiver, diags);
          CheckArgs(ctx, node.args, diags);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          CheckExpr(ctx, node.expr, diags);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          CheckExpr(ctx, node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            CheckExpr(ctx, arm.expr, diags);
            CheckExpr(ctx, arm.handler.value, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& elem : node.exprs) {
            CheckExpr(ctx, elem, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          return;
        } else {
          return;
        }
      },
      expr.node);
}

void CheckItem(const ScopeContext& ctx,
               const ast::ASTItem& item,
               core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          CheckExpr(ctx, node.binding.init, diags);
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          if (node.body) {
            CheckBlock(ctx, *node.body, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
          for (const auto& member : node.members) {
            if (const auto* field =
                    std::get_if<ast::FieldDecl>(&member)) {
              CheckExpr(ctx, field->init_opt, diags);
            } else if (const auto* method =
                           std::get_if<ast::MethodDecl>(&member)) {
              if (method->body) {
                CheckBlock(ctx, *method->body, diags);
              }
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          if (node.invariant_opt.has_value()) {
            CheckExpr(ctx, node.invariant_opt->predicate, diags);
          }
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              if (const auto* method =
                      std::get_if<ast::StateMethodDecl>(&member)) {
                if (method->body) {
                  CheckBlock(ctx, *method->body, diags);
                }
              } else if (const auto* transition =
                             std::get_if<ast::TransitionDecl>(&member)) {
                if (transition->body) {
                  CheckBlock(ctx, *transition->body, diags);
                }
              }
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          for (const auto& item : node.items) {
            if (const auto* method =
                    std::get_if<ast::ClassMethodDecl>(&item)) {
              if (method->body_opt) {
                CheckBlock(ctx, *method->body_opt, diags);
              }
            }
          }
        } else {
          return;
        }
      },
      item);
}

}  // namespace

AccessResult CanAccessVis(const ast::ModulePath& accessor_module,
                          const ast::ModulePath& decl_module,
                          ast::Visibility vis) {
  SpecDefsVisibility();
  switch (vis) {
    case ast::Visibility::Public:
      SPEC_RULE("Access-Public");
      return {true, std::nullopt};
    case ast::Visibility::Internal:
      if (SameAssembly(accessor_module, decl_module)) {
        SPEC_RULE("Access-Internal");
        return {true, std::nullopt};
      }
      SPEC_RULE("Access-Internal-Err");
      return {false, "Access-Err"};
    case ast::Visibility::Private:
      if (PathEq(accessor_module, decl_module)) {
        SPEC_RULE("Access-Private");
        return {true, std::nullopt};
      }
      SPEC_RULE("Access-Err");
      return {false, "Access-Err"};
  }
  return {true, std::nullopt};
}

AccessResult CanAccess(const ScopeContext& ctx,
                       const ast::ModulePath& module_path,
                       std::string_view name) {
  SpecDefsVisibility();
  const auto resolved_module = ResolveVisibleModulePath(ctx, module_path);
  if (!resolved_module.has_value()) {
    // Access checks are only meaningful once the module path is determined.
    // Unresolved paths are handled by qualified-name resolution diagnostics.
    return {true, std::nullopt};
  }
  const auto vis = FindDeclVisibilityByName(ctx, *resolved_module, name);
  if (!vis) {
    return {true, std::nullopt};
  }
  return CanAccessVis(CurrentModule(ctx), *resolved_module, *vis);
}

AccessResult TopLevelVis(const ast::ASTItem& item) {
  SpecDefsVisibility();
  const auto vis = VisOpt(item);
  if (!vis.has_value()) {
    return {true, std::nullopt};
  }
  SPEC_RULE("TopLevelVis-Ok");
  return {true, std::nullopt};
}

core::DiagnosticStream CheckModuleVisibility(const ScopeContext& ctx,
                                             const ast::ASTModule& module) {
  SpecDefsVisibility();
  core::DiagnosticStream diags;
  bool public_api = false;
  for (const auto& item : module.items) {
    const auto vis = VisOpt(item);
    if (vis.has_value() && *vis == ast::Visibility::Public) {
      public_api = true;
      break;
    }
  }
  for (const auto& item : module.items) {
    if (public_api) {
      if (const auto* using_decl = std::get_if<ast::UsingDecl>(&item)) {
        if (std::holds_alternative<ast::UsingWildcard>(using_decl->clause)) {
          SPEC_RULE("Using-Wildcard-Warn");
          EmitDiag(diags, "W-MOD-1201", SpanOfItem(item));
        }
      }
    }
    CheckItem(ctx, item, diags);
  }
  return diags;
}

}  // namespace ultraviolet::analysis
