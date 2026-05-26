#include "04_analysis/memory/return_responsibility.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

namespace {

using ResponsibilityEnv = std::unordered_map<std::string, bool>;

struct ActiveBodyGuard {
  explicit ActiveBodyGuard(const void* body) : body(body) {
    active().insert(body);
  }

  ~ActiveBodyGuard() { active().erase(body); }

  static bool IsActive(const void* body) {
    return active().find(body) != active().end();
  }

 private:
  const void* body;

  static std::unordered_set<const void*>& active() {
    thread_local std::unordered_set<const void*> bodies;
    return bodies;
  }
};

std::optional<TypeRef> CachedExprType(const ScopeContext& ctx,
                                      const ast::ExprPtr& expr) {
  if (!expr || !ctx.expr_types) {
    return std::nullopt;
  }
  const auto it = ctx.expr_types->find(expr.get());
  if (it == ctx.expr_types->end() || !it->second) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> PlaceRoot(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return IdKeyOf(ident->name);
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceRoot(field->base);
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceRoot(tuple->base);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceRoot(index->base);
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return PlaceRoot(deref->value);
  }
  return std::nullopt;
}

bool IsPlaceExprLocal(const ast::ExprPtr& expr) {
  return PlaceRoot(expr).has_value();
}

void CollectPatternNames(const ast::PatternPtr& pattern,
                         std::vector<std::string>& names) {
  if (!pattern) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          if (node.name != "_") {
            names.push_back(IdKeyOf(node.name));
          }
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name != "_") {
            names.push_back(IdKeyOf(node.name));
          }
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& element : node.elements) {
            CollectPatternNames(element, names);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            if (field.pattern_opt) {
              CollectPatternNames(field.pattern_opt, names);
            } else {
              names.push_back(IdKeyOf(field.name));
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt) {
            return;
          }
          std::visit(
              [&](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& element : payload.elements) {
                    CollectPatternNames(element, names);
                  }
                } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                  for (const auto& field : payload.fields) {
                    if (field.pattern_opt) {
                      CollectPatternNames(field.pattern_opt, names);
                    } else {
                      names.push_back(IdKeyOf(field.name));
                    }
                  }
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt) {
            return;
          }
          for (const auto& field : node.fields_opt->fields) {
            if (field.pattern_opt) {
              CollectPatternNames(field.pattern_opt, names);
            } else {
              names.push_back(IdKeyOf(field.name));
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          CollectPatternNames(node.lo, names);
          CollectPatternNames(node.hi, names);
        }
      },
      pattern->node);
}

std::optional<bool> CombineReturnResponsibilities(
    const std::vector<bool>& returns) {
  if (returns.empty()) {
    return std::nullopt;
  }
  return std::any_of(returns.begin(), returns.end(),
                     [](bool has_resp) { return has_resp; });
}

std::optional<bool> ExprResultHasResponsibilityWithEnv(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr,
    const ResponsibilityEnv& env);

void AnalyzeStmtReturns(const ScopeContext& ctx,
                        const ast::Stmt& stmt,
                        ResponsibilityEnv& env,
                        std::vector<bool>& returns);

std::optional<bool> AnalyzeBlockReturns(const ScopeContext& ctx,
                                        const ast::BlockPtr& body,
                                        ResponsibilityEnv env) {
  if (!body) {
    return std::nullopt;
  }
  std::vector<bool> returns;
  for (const auto& stmt : body->stmts) {
    AnalyzeStmtReturns(ctx, stmt, env, returns);
  }
  if (body->tail_opt) {
    returns.push_back(
        ExprResultHasResponsibilityWithEnv(ctx, body->tail_opt, env).value_or(true));
  }
  return CombineReturnResponsibilities(returns);
}

void AddBindingResponsibility(const ScopeContext& ctx,
                              const ast::Binding& binding,
                              ResponsibilityEnv& env) {
  std::vector<std::string> names;
  CollectPatternNames(binding.pat, names);
  if (names.empty()) {
    return;
  }
  const bool has_resp =
      ExprResultHasResponsibilityWithEnv(ctx, binding.init, env).value_or(true);
  for (const auto& name : names) {
    env[name] = has_resp;
  }
}

void AnalyzeStmtReturns(const ScopeContext& ctx,
                        const ast::Stmt& stmt,
                        ResponsibilityEnv& env,
                        std::vector<bool>& returns) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          AddBindingResponsibility(ctx, node.binding, env);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          returns.push_back(
              ExprResultHasResponsibilityWithEnv(ctx, node.value_opt, env)
                  .value_or(true));
        } else if constexpr (std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt> ||
                             std::is_same_v<T, ast::DeferStmt>) {
          if (auto block_result = AnalyzeBlockReturns(ctx, node.body, env)) {
            returns.push_back(*block_result);
          }
        }
      },
      stmt);
}

std::optional<bool> CallableReturnHasResponsibility(const ScopeContext& ctx,
                                                    const ast::BlockPtr& body,
                                                    ResponsibilityEnv env) {
  if (!body) {
    return std::nullopt;
  }
  if (ActiveBodyGuard::IsActive(body.get())) {
    return std::nullopt;
  }
  ActiveBodyGuard guard(body.get());
  return AnalyzeBlockReturns(ctx, body, std::move(env));
}

void AddParamsToEnv(const std::vector<ast::Param>& params,
                    ResponsibilityEnv& env) {
  for (const auto& param : params) {
    env[IdKeyOf(param.name)] = param.mode.has_value();
  }
}

std::optional<bool> MethodBodyReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::Receiver& receiver,
    const std::vector<ast::Param>& params,
    const ast::BlockPtr& body) {
  ResponsibilityEnv env;
  env[IdKeyOf("self")] = RecvModeOf(receiver).has_value();
  AddParamsToEnv(params, env);
  return CallableReturnHasResponsibility(ctx, body, std::move(env));
}

const ast::ProcedureDecl* FindProcedureInModule(const ast::ASTModule& module,
                                                std::string_view name) {
  for (const auto& item : module.items) {
    const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
    if (proc && IdEq(proc->name, name)) {
      return proc;
    }
  }
  return nullptr;
}

std::optional<const ast::ProcedureDecl*> ResolveNamedProcedure(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee) {
  if (!callee) {
    return std::nullopt;
  }

  ast::ModulePath module_path;
  std::string name;
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    module_path = ctx.current_module;
    name = ident->name;
  } else if (const auto* qname =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    module_path = qname->path.empty() ? ctx.current_module : qname->path;
    name = qname->name;
  } else if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
    module_path = path->path.empty() ? ctx.current_module : path->path;
    name = path->name;
  } else {
    return std::nullopt;
  }

  for (const auto& module : ctx.sigma.mods) {
    if (module.path != module_path) {
      continue;
    }
    if (const auto* proc = FindProcedureInModule(module, name)) {
      return proc;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

const ast::ModalDecl* LookupModalDeclForReturn(const ScopeContext& ctx,
                                               const TypePath& path) {
  const auto it = ctx.sigma.types.find(PathKeyOf(path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::ModalDecl>(&it->second);
}

std::optional<bool> MethodCallFromReceiverTypeHasResponsibility(
    const ScopeContext& ctx,
    const TypeRef& receiver_type,
    std::string_view name) {
  const TypeRef base = StripPerm(receiver_type);
  if (!base) {
    return std::nullopt;
  }

  if (const auto* modal = std::get_if<TypeModalState>(&base->node)) {
    const auto* modal_decl = LookupModalDeclForReturn(ctx, modal->path);
    if (!modal_decl) {
      return std::nullopt;
    }
    const auto* method =
        LookupStateMethodDecl(*modal_decl, modal->state, name);
    if (!method) {
      return std::nullopt;
    }
    return StateMethodReturnHasResponsibility(ctx, *method);
  }

  const auto lookup = LookupMethodStatic(ctx, base, name);
  if (lookup.record_method) {
    return MethodReturnHasResponsibility(ctx, *lookup.record_method);
  }
  if (lookup.class_method) {
    return ClassMethodReturnHasResponsibility(ctx, *lookup.class_method);
  }
  return std::nullopt;
}

std::optional<bool> ExprResultHasResponsibilityWithEnv(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr,
    const ResponsibilityEnv& env) {
  if (!expr) {
    return false;
  }
  if (const auto* attr = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return ExprResultHasResponsibilityWithEnv(ctx, attr->expr, env);
  }
  if (std::holds_alternative<ast::MoveExpr>(expr->node) ||
      std::holds_alternative<ast::CopyExpr>(expr->node)) {
    return true;
  }
  if (IsPlaceExprLocal(expr)) {
    if (const auto root = PlaceRoot(expr)) {
      const auto it = env.find(*root);
      if (it != env.end()) {
        return it->second;
      }
    }
    return true;
  }
  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    return CallResultHasResponsibility(ctx, *call).value_or(true);
  }
  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    return MethodCallResultHasResponsibility(ctx, *method).value_or(true);
  }
  if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    if (!if_expr->then_expr || !if_expr->else_expr) {
      return true;
    }
    const bool then_resp =
        ExprResultHasResponsibilityWithEnv(ctx, if_expr->then_expr, env)
            .value_or(true);
    const bool else_resp =
        ExprResultHasResponsibilityWithEnv(ctx, if_expr->else_expr, env)
            .value_or(true);
    return then_resp || else_resp;
  }
  if (const auto* block = std::get_if<ast::BlockExpr>(&expr->node)) {
    return AnalyzeBlockReturns(ctx, block->block, env).value_or(true);
  }
  if (const auto* unsafe_block =
          std::get_if<ast::UnsafeBlockExpr>(&expr->node)) {
    return AnalyzeBlockReturns(ctx, unsafe_block->block, env).value_or(true);
  }
  if (const auto* propagate = std::get_if<ast::PropagateExpr>(&expr->node)) {
    return ExprResultHasResponsibilityWithEnv(ctx, propagate->value, env);
  }
  return true;
}

}  // namespace

std::optional<bool> ExpressionResultHasResponsibility(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr) {
  return ExprResultHasResponsibilityWithEnv(ctx, expr, ResponsibilityEnv{});
}

std::optional<bool> CallResultHasResponsibility(const ScopeContext& ctx,
                                                const ast::CallExpr& call) {
  const ast::ProcedureDecl* proc = nullptr;
  if (ctx.selected_call_targets) {
    const auto selected = ctx.selected_call_targets->find(&call);
    if (selected != ctx.selected_call_targets->end() && selected->second.proc) {
      proc = selected->second.proc;
    }
  }
  if (!proc) {
    if (const auto resolved = ResolveNamedProcedure(ctx, call.callee)) {
      proc = *resolved;
    }
  }
  if (!proc) {
    return std::nullopt;
  }
  return ProcedureReturnHasResponsibility(ctx, *proc);
}

std::optional<bool> MethodCallResultHasResponsibility(
    const ScopeContext& ctx,
    const ast::MethodCallExpr& call) {
  if (const auto receiver_type = CachedExprType(ctx, call.receiver)) {
    return MethodCallFromReceiverTypeHasResponsibility(
        ctx, *receiver_type, call.name);
  }
  return std::nullopt;
}

std::optional<bool> ProcedureReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl) {
  ResponsibilityEnv env;
  AddParamsToEnv(decl.params, env);
  return CallableReturnHasResponsibility(ctx, decl.body, std::move(env));
}

std::optional<bool> MethodReturnHasResponsibility(const ScopeContext& ctx,
                                                  const ast::MethodDecl& decl) {
  return MethodBodyReturnHasResponsibility(
      ctx, decl.receiver, decl.params, decl.body);
}

std::optional<bool> StateMethodReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::StateMethodDecl& decl) {
  return MethodBodyReturnHasResponsibility(
      ctx, decl.receiver, decl.params, decl.body);
}

std::optional<bool> ClassMethodReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::ClassMethodDecl& decl) {
  return MethodBodyReturnHasResponsibility(
      ctx, decl.receiver, decl.params, decl.body_opt);
}

}  // namespace ultraviolet::analysis
