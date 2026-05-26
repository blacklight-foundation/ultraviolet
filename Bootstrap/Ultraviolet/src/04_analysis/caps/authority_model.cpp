// =============================================================================
// authority_model.cpp - Authority Model and Capability Flow Tracking
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 5.9 "Capabilities" (line 13048)
//   - Section 5.9.1 "Authority Model" (lines 13060-13150)
//   - Section 5.9.2 "No Ambient Authority" (lines 13160-13200)
//   - Section 19 "Capability Safety" (lines 24200-24400)
//
// SOURCE FILES:
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_system.cpp
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_io.cpp
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_heap.cpp
//   - ultraviolet-bootstrap/src/03_analysis/caps/cap_concurrency.cpp
//
// FUNCTIONS IMPLEMENTED:
//   - ValidateCapabilityAccess() - Ensure capability accessed through valid path
//   - CheckAmbientAuthority() - Verify no ambient authority usage
//   - TraceCapabilityFlow() - Track capability from Context to usage
//   - ValidateCapabilityPassing() - Ensure explicit capability passing
//   - CheckCapabilityIsolation() - Verify extern doesn't receive capabilities
//
// =============================================================================

#include "04_analysis/caps/authority_model.h"

#include <sstream>
#include <unordered_set>
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsAuthorityModel() {
  SPEC_DEF("AuthorityModel", "5.9.1");
  SPEC_DEF("NoAmbientAuthority", "5.9.2");
  SPEC_DEF("CapabilityIsolation", "19");
  SPEC_DEF("CapabilityFlow", "19.1");
}

/// Check if an identifier refers to a Context binding
bool IsContextBinding(std::string_view name) {
  // Common names for Context parameter
  return IdEq(name, "ctx") || IdEq(name, "context");
}

/// Check if a type is the Context type
bool IsContextType(const TypeRef& type) {
  if (!type) return false;
  if (const auto* path = std::get_if<TypePathType>(&type->node)) {
    return IsContextTypePath(path->path);
  }
  return false;
}

std::optional<CapabilityKind> CapabilityKindFromTypeRef(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&type->node)) {
    return CapabilityKindFromDynamic(*dyn);
  }
  if (const auto* path = std::get_if<TypePathType>(&type->node)) {
    return CapabilityKindFromPath(path->path);
  }
  return std::nullopt;
}

struct AmbientAuthorityContext {
  const ExprTypeMap* expr_types = nullptr;
  const ScopeContext* scope_ctx = nullptr;
  const ast::ModulePath* current_module = nullptr;
  std::vector<std::unordered_set<std::string>> local_scopes;
};

void PushLocalScope(AmbientAuthorityContext& ctx) {
  ctx.local_scopes.emplace_back();
}

void PopLocalScope(AmbientAuthorityContext& ctx) {
  if (!ctx.local_scopes.empty()) {
    ctx.local_scopes.pop_back();
  }
}

void BindLocalName(AmbientAuthorityContext& ctx, const std::string& name) {
  if (ctx.local_scopes.empty()) {
    PushLocalScope(ctx);
  }
  ctx.local_scopes.back().insert(name);
}

bool IsLocallyBound(const AmbientAuthorityContext& ctx, std::string_view name) {
  const std::string key(name);
  for (auto it = ctx.local_scopes.rbegin(); it != ctx.local_scopes.rend();
       ++it) {
    if (it->find(key) != it->end()) {
      return true;
    }
  }
  return false;
}

bool ExprHasCapabilityType(const ast::Expr& expr,
                           const AmbientAuthorityContext& ctx) {
  if (!ctx.expr_types) {
    return false;
  }
  const auto it = ctx.expr_types->find(&expr);
  if (it == ctx.expr_types->end()) {
    return false;
  }
  // Function symbols may mention capability-typed parameters (for example
  // `(Context) -> i32`) without being capability *values*. Treating these as
  // ambient would incorrectly reject ordinary call expressions.
  if (std::holds_alternative<TypeFunc>(it->second->node) ||
      std::holds_alternative<TypeClosure>(it->second->node)) {
    return false;
  }
  // Ambient-authority checks must track capability *values* in expression
  // positions, not callable signatures that merely mention capability-typed
  // parameters.
  if (ctx.scope_ctx && ctx.current_module) {
    return !InferCapabilitiesFromType(*ctx.scope_ctx, *ctx.current_module,
                                      it->second)
                .IsEmpty();
  }
  return !InferCapabilitiesFromType(it->second).IsEmpty();
}

bool CheckExprForAmbientAuthority(const ast::Expr& expr,
                                  AmbientAuthorityContext& ctx);
bool CheckBlockForAmbientAuthority(const ast::Block& block,
                                   AmbientAuthorityContext& ctx);
bool CheckStmtForAmbientAuthority(const ast::Stmt& stmt,
                                  AmbientAuthorityContext& ctx);

void BindPatternLocals(const ast::Pattern& pattern, AmbientAuthorityContext& ctx);

void BindFieldPatternLocals(const ast::FieldPattern& field,
                            AmbientAuthorityContext& ctx) {
  if (field.pattern_opt) {
    BindPatternLocals(*field.pattern_opt, ctx);
  } else {
    BindLocalName(ctx, field.name);
  }
}

void BindPatternLocals(const ast::Pattern& pattern, AmbientAuthorityContext& ctx) {
  std::visit(
      [&ctx](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          BindLocalName(ctx, node.name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name == "_") {
            return;
          }
          BindLocalName(ctx, node.name);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& element : node.elements) {
            if (element) {
              BindPatternLocals(*element, ctx);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            BindFieldPatternLocals(field, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return;
          }
          std::visit(
              [&ctx](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& element : payload.elements) {
                    if (element) {
                      BindPatternLocals(*element, ctx);
                    }
                  }
                } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                  for (const auto& field : payload.fields) {
                    BindFieldPatternLocals(field, ctx);
                  }
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return;
          }
          for (const auto& field : node.fields_opt->fields) {
            BindFieldPatternLocals(field, ctx);
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          if (node.lo) {
            BindPatternLocals(*node.lo, ctx);
          }
          if (node.hi) {
            BindPatternLocals(*node.hi, ctx);
          }
        }
      },
      pattern.node);
}

bool CheckCallArgListForAmbientAuthority(const std::vector<ast::Arg>& args,
                                         AmbientAuthorityContext& ctx) {
  for (const auto& arg : args) {
    if (arg.value && CheckExprForAmbientAuthority(*arg.value, ctx)) {
      return true;
    }
  }
  return false;
}

bool CheckApplyArgsForAmbientAuthority(const ast::ApplyArgs& args,
                                       AmbientAuthorityContext& ctx) {
  return std::visit(
      [&ctx](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          return CheckCallArgListForAmbientAuthority(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::BraceArgs>) {
          for (const auto& field : node.fields) {
            if (field.value && CheckExprForAmbientAuthority(*field.value, ctx)) {
              return true;
            }
          }
          return false;
        } else {
          return false;
        }
      },
      args);
}

bool CheckKeyPathForAmbientAuthority(const ast::KeyPathExpr& key_path,
                                     AmbientAuthorityContext& ctx) {
  (void)key_path.root;
  for (const auto& seg : key_path.segs) {
    if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      if (index->expr && CheckExprForAmbientAuthority(*index->expr, ctx)) {
        return true;
      }
    }
  }
  return false;
}

bool CheckExprForAmbientAuthority(const ast::Expr& expr,
                                  AmbientAuthorityContext& ctx) {
  return std::visit(
      [&expr, &ctx](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (IsLocallyBound(ctx, node.name)) {
            return false;
          }
          return ExprHasCapabilityType(expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return ExprHasCapabilityType(expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return ExprHasCapabilityType(expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (ExprHasCapabilityType(expr, ctx)) {
            return true;
          }
          return CheckApplyArgsForAmbientAuthority(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          if (node.lhs && CheckExprForAmbientAuthority(*node.lhs, ctx)) {
            return true;
          }
          if (node.rhs && CheckExprForAmbientAuthority(*node.rhs, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          if (node.lhs && CheckExprForAmbientAuthority(*node.lhs, ctx)) {
            return true;
          }
          if (node.rhs && CheckExprForAmbientAuthority(*node.rhs, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return node.place && CheckExprForAmbientAuthority(*node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return node.place && CheckExprForAmbientAuthority(*node.place, ctx);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& element : node.elements) {
            if (element && CheckExprForAmbientAuthority(*element, ctx)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool has_ambient_authority = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& element) {
            if (has_ambient_authority || !element) {
              return;
            }
            if (CheckExprForAmbientAuthority(*element, ctx)) {
              has_ambient_authority = true;
            }
          });
          return has_ambient_authority;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          if (node.value && CheckExprForAmbientAuthority(*node.value, ctx)) {
            return true;
          }
          if (node.count && CheckExprForAmbientAuthority(*node.count, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (field.value && CheckExprForAmbientAuthority(*field.value, ctx)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          return std::visit(
              [&ctx](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& elem : payload.elements) {
                    if (elem && CheckExprForAmbientAuthority(*elem, ctx)) {
                      return true;
                    }
                  }
                  return false;
                } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                  for (const auto& field : payload.fields) {
                    if (field.value &&
                        CheckExprForAmbientAuthority(*field.value, ctx)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  return false;
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          if (node.cond && CheckExprForAmbientAuthority(*node.cond, ctx)) {
            return true;
          }
          if (node.then_expr &&
              CheckExprForAmbientAuthority(*node.then_expr, ctx)) {
            return true;
          }
          if (node.else_expr &&
              CheckExprForAmbientAuthority(*node.else_expr, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (node.scrutinee &&
              CheckExprForAmbientAuthority(*node.scrutinee, ctx)) {
            return true;
          }
          for (const auto& case_clause : node.cases) {
            PushLocalScope(ctx);
            if (case_clause.pattern) {
              BindPatternLocals(*case_clause.pattern, ctx);
            }
            if (case_clause.body &&
                CheckExprForAmbientAuthority(*case_clause.body, ctx)) {
              PopLocalScope(ctx);
              return true;
            }
            PopLocalScope(ctx);
          }
          return node.else_expr &&
                 CheckExprForAmbientAuthority(*node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          if (node.scrutinee &&
              CheckExprForAmbientAuthority(*node.scrutinee, ctx)) {
            return true;
          }
          PushLocalScope(ctx);
          if (node.pattern) {
            BindPatternLocals(*node.pattern, ctx);
          }
          if (node.then_expr &&
              CheckExprForAmbientAuthority(*node.then_expr, ctx)) {
            PopLocalScope(ctx);
            return true;
          }
          PopLocalScope(ctx);
          return node.else_expr &&
                 CheckExprForAmbientAuthority(*node.else_expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value() &&
              node.invariant_opt->predicate &&
              CheckExprForAmbientAuthority(*node.invariant_opt->predicate,
                                           ctx)) {
            return true;
          }
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          if (node.cond && CheckExprForAmbientAuthority(*node.cond, ctx)) {
            return true;
          }
          if (node.invariant_opt.has_value() &&
              node.invariant_opt->predicate &&
              CheckExprForAmbientAuthority(*node.invariant_opt->predicate,
                                           ctx)) {
            return true;
          }
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          if (node.iter && CheckExprForAmbientAuthority(*node.iter, ctx)) {
            return true;
          }
          PushLocalScope(ctx);
          if (node.pattern) {
            BindPatternLocals(*node.pattern, ctx);
          }
          if (node.invariant_opt.has_value() &&
              node.invariant_opt->predicate &&
              CheckExprForAmbientAuthority(*node.invariant_opt->predicate,
                                           ctx)) {
            PopLocalScope(ctx);
            return true;
          }
          const bool has_ambient =
              node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
          PopLocalScope(ctx);
          return has_ambient;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return node.block && CheckBlockForAmbientAuthority(*node.block, ctx);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block && CheckBlockForAmbientAuthority(*node.block, ctx);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return node.expr && CheckExprForAmbientAuthority(*node.expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          PushLocalScope(ctx);
          for (const auto& param : node.params) {
            BindLocalName(ctx, param.name);
          }
          const bool has_ambient =
              node.body && CheckExprForAmbientAuthority(*node.body, ctx);
          PopLocalScope(ctx);
          return has_ambient;
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          if (node.lhs && CheckExprForAmbientAuthority(*node.lhs, ctx)) {
            return true;
          }
          if (node.rhs && CheckExprForAmbientAuthority(*node.rhs, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return node.base && CheckExprForAmbientAuthority(*node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return node.base && CheckExprForAmbientAuthority(*node.base, ctx);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          if (node.base && CheckExprForAmbientAuthority(*node.base, ctx)) {
            return true;
          }
          if (node.index && CheckExprForAmbientAuthority(*node.index, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (node.callee && CheckExprForAmbientAuthority(*node.callee, ctx)) {
            return true;
          }
          return CheckCallArgListForAmbientAuthority(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (node.receiver &&
              CheckExprForAmbientAuthority(*node.receiver, ctx)) {
            return true;
          }
          return CheckCallArgListForAmbientAuthority(node.args, ctx);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return node.expr && CheckExprForAmbientAuthority(*node.expr, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            if (arm.expr && CheckExprForAmbientAuthority(*arm.expr, ctx)) {
              return true;
            }
            PushLocalScope(ctx);
            if (arm.pattern) {
              BindPatternLocals(*arm.pattern, ctx);
            }
            if (arm.handler.value &&
                CheckExprForAmbientAuthority(*arm.handler.value, ctx)) {
              PopLocalScope(ctx);
              return true;
            }
            PopLocalScope(ctx);
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& value : node.exprs) {
            if (value && CheckExprForAmbientAuthority(*value, ctx)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          if (node.domain && CheckExprForAmbientAuthority(*node.domain, ctx)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (opt.value && CheckExprForAmbientAuthority(*opt.value, ctx)) {
              return true;
            }
          }
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            if (opt.value && CheckExprForAmbientAuthority(*opt.value, ctx)) {
              return true;
            }
          }
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return node.handle && CheckExprForAmbientAuthority(*node.handle, ctx);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          if (node.range && CheckExprForAmbientAuthority(*node.range, ctx)) {
            return true;
          }
          if (node.key_clause.has_value() &&
              CheckKeyPathForAmbientAuthority(node.key_clause->key_path, ctx)) {
            return true;
          }
          for (const auto& opt : node.opts) {
            if (opt.chunk_expr &&
                CheckExprForAmbientAuthority(*opt.chunk_expr, ctx)) {
              return true;
            }
            if (opt.workgroup_expr &&
                CheckExprForAmbientAuthority(*opt.workgroup_expr, ctx)) {
              return true;
            }
          }
          PushLocalScope(ctx);
          if (node.pattern) {
            BindPatternLocals(*node.pattern, ctx);
          }
          const bool has_ambient =
              node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
          PopLocalScope(ctx);
          return has_ambient;
        } else {
          return false;
        }
      },
      expr.node);
}
bool CheckStmtForAmbientAuthority(const ast::Stmt& stmt,
                                  AmbientAuthorityContext& ctx) {
  return std::visit(
      [&ctx](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          if (node.binding.init &&
              CheckExprForAmbientAuthority(*node.binding.init, ctx)) {
            return true;
          }
          if (node.binding.pat) {
            BindPatternLocals(*node.binding.pat, ctx);
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          if (node.binding.init &&
              CheckExprForAmbientAuthority(*node.binding.init, ctx)) {
            return true;
          }
          if (node.binding.pat) {
            BindPatternLocals(*node.binding.pat, ctx);
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expressions to
          // check, but the alias name must be visible in the enclosing scope.
          BindLocalName(ctx, node.alias);
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          if (node.place && CheckExprForAmbientAuthority(*node.place, ctx)) {
            return true;
          }
          if (node.value && CheckExprForAmbientAuthority(*node.value, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          if (node.place && CheckExprForAmbientAuthority(*node.place, ctx)) {
            return true;
          }
          if (node.value && CheckExprForAmbientAuthority(*node.value, ctx)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return node.value && CheckExprForAmbientAuthority(*node.value, ctx);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.opts_opt && CheckExprForAmbientAuthority(*node.opts_opt, ctx)) {
            return true;
          }
          PushLocalScope(ctx);
          if (node.alias_opt.has_value()) {
            BindLocalName(ctx, *node.alias_opt);
          }
          const bool has_ambient =
              node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
          PopLocalScope(ctx);
          return has_ambient;
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          return node.value_opt &&
                 CheckExprForAmbientAuthority(*node.value_opt, ctx);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return node.value_opt &&
                 CheckExprForAmbientAuthority(*node.value_opt, ctx);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          for (const auto& path : node.paths) {
            if (CheckKeyPathForAmbientAuthority(path, ctx)) {
              return true;
            }
          }
          return node.body && CheckBlockForAmbientAuthority(*node.body, ctx);
        } else {
          return false;
        }
      },
      stmt);
}

bool CheckBlockForAmbientAuthority(const ast::Block& block,
                                   AmbientAuthorityContext& ctx) {
  PushLocalScope(ctx);
  for (const auto& stmt : block.stmts) {
    if (CheckStmtForAmbientAuthority(stmt, ctx)) {
      PopLocalScope(ctx);
      return true;
    }
  }
  if (block.tail_opt && CheckExprForAmbientAuthority(*block.tail_opt, ctx)) {
    PopLocalScope(ctx);
    return true;
  }
  PopLocalScope(ctx);
  return false;
}

}  // namespace

// =============================================================================
// Capability path tracking
// =============================================================================

std::string CapabilityPath::ToString() const {
  std::ostringstream oss;
  bool first = true;
  for (const auto& comp : path) {
    if (!first) oss << ".";
    oss << comp;
    first = false;
  }
  return oss.str();
}

// =============================================================================
// Capability access validation
// =============================================================================

std::optional<CapabilityPath> ValidateCapabilityAccess(
    const ast::Expr& expr,
    const TypeRef& expr_type) {
  SpecDefsAuthorityModel();

  CapabilityPath path = TraceCapabilityFlow(expr, expr_type);
  if (path.valid) {
    return path;
  }
  return std::nullopt;
}

CapabilityPath TraceCapabilityFlow(
    const ast::Expr& expr,
    const TypeRef& expr_type) {
  SpecDefsAuthorityModel();

  CapabilityPath result{};
  result.valid = false;

  return std::visit(
      [&result, &expr_type](const auto& node) -> CapabilityPath {
        using T = std::decay_t<decltype(node)>;

        // Field access on Context
        if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          // Check if base is an identifier that looks like Context
          if (node.base) {
            if (const auto* ident =
                    std::get_if<ast::IdentifierExpr>(&node.base->node)) {
              if (IsContextBinding(ident->name)) {
                result.root = ident->name;
                result.path = {ident->name, node.name};

                if (const auto field_type = ContextFieldType(node.name)) {
                  if (const auto cap = CapabilityKindFromTypeRef(*field_type)) {
                    result.capability = *cap;
                    result.valid = true;
                  }
                }
              }
            }
          }
          return result;
        }
        // Method call on Context (cpu(), gpu(), inline())
        else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (node.receiver) {
            if (const auto* ident =
                    std::get_if<ast::IdentifierExpr>(&node.receiver->node)) {
              if (IsContextBinding(ident->name)) {
                result.root = ident->name;
                result.path = {ident->name, node.name + "()"};

                if (LookupContextMethodSig(node.name)) {
                  result.capability = CapabilityKind::ExecutionDomain;
                  result.valid = true;
                }
              }
            }
          }
          return result;
        }
        // Identifier that is the Context parameter itself
        else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (IsContextBinding(node.name) && IsContextType(expr_type)) {
            result.root = node.name;
            result.path = {node.name};
            result.capability = CapabilityKind::Context;
            result.valid = true;
          }
          return result;
        }
        else {
          return result;
        }
      },
      expr.node);
}

// =============================================================================
// Ambient authority detection
// =============================================================================

AuthorityValidationResult CheckAmbientAuthority(
    const ast::ProcedureDecl& proc,
    const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = proc.span;

  if (!proc.body) {
    return result;
  }

  AmbientAuthorityContext ambient_ctx;
  ambient_ctx.expr_types = expr_types;
  PushLocalScope(ambient_ctx);
  for (const auto& param : proc.params) {
    BindLocalName(ambient_ctx, param.name);
  }

  if (CheckBlockForAmbientAuthority(*proc.body, ambient_ctx)) {
    result.valid = false;
    result.error_code = "E-CON-0020";
    result.error_message =
        "Procedure '" + proc.name + "' uses ambient authority; " +
        "all capabilities must be passed explicitly through parameters";
  }

  PopLocalScope(ambient_ctx);
  return result;
}

AuthorityValidationResult CheckAmbientAuthority(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ProcedureDecl& proc,
    const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = proc.span;

  if (!proc.body) {
    return result;
  }

  AmbientAuthorityContext ambient_ctx;
  ambient_ctx.expr_types = expr_types;
  ambient_ctx.scope_ctx = &ctx;
  ambient_ctx.current_module = &current_module;
  PushLocalScope(ambient_ctx);
  for (const auto& param : proc.params) {
    BindLocalName(ambient_ctx, param.name);
  }

  if (CheckBlockForAmbientAuthority(*proc.body, ambient_ctx)) {
    result.valid = false;
    result.error_code = "E-CON-0020";
    result.error_message =
        "Procedure '" + proc.name + "' uses ambient authority; " +
        "all capabilities must be passed explicitly through parameters";
  }

  PopLocalScope(ambient_ctx);
  return result;
}

bool ExpressionUsesAmbientAuthority(const ast::Expr& expr,
                                    const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  AmbientAuthorityContext ambient_ctx;
  ambient_ctx.expr_types = expr_types;
  return CheckExprForAmbientAuthority(expr, ambient_ctx);
}

// =============================================================================
// Capability passing validation
// =============================================================================

AuthorityValidationResult ValidateCapabilityPassing(
    const ast::CallExpr& call,
    const CapabilitySet& caller_caps,
    const CapabilitySet& callee_needs) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  // CallExpr doesn't have span - leave result.span empty

  if (callee_needs.IsSubsetOf(caller_caps)) {
    result.valid = true;
    return result;
  }

  result.valid = false;
  result.error_code = "E-CON-0020";

  std::ostringstream msg;
  msg << "Call requires capabilities " << callee_needs.ToString()
      << " but caller only has " << caller_caps.ToString();
  result.error_message = msg.str();

  return result;
}

AuthorityValidationResult ValidateCapabilityPassing(
    const ast::MethodCallExpr& call,
    const CapabilitySet& caller_caps,
    const CapabilitySet& callee_needs) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  // MethodCallExpr doesn't have span - leave result.span empty

  if (callee_needs.IsSubsetOf(caller_caps)) {
    result.valid = true;
    return result;
  }

  result.valid = false;
  result.error_code = "E-CON-0020";

  std::ostringstream msg;
  msg << "Method call '" << call.name << "' requires capabilities "
      << callee_needs.ToString() << " but caller only has "
      << caller_caps.ToString();
  result.error_message = msg.str();

  return result;
}

// =============================================================================
// FFI capability isolation
// =============================================================================

AuthorityValidationResult CheckCapabilityIsolation(
    const ast::ExternProcDecl& proc) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = proc.span;

  // Check each parameter for capability types
  for (const auto& param : proc.params) {
    if (param.type) {
      auto caps = InferCapabilitiesFromAstType(*param.type);
      if (!caps.IsEmpty()) {
        result.valid = false;
        result.error_code = "E-TYP-2623";
        result.error_message =
            "Extern procedure '" + proc.name +
            "' has capability-bearing parameter '" + param.name +
            "'; foreign code cannot receive capabilities";
        return result;
      }
    }
  }

  // Check return type for capability types
  if (proc.return_type_opt) {
    auto caps = InferCapabilitiesFromAstType(*proc.return_type_opt);
    if (!caps.IsEmpty()) {
      result.valid = false;
      result.error_code = "E-TYP-2623";
      result.error_message =
          "Extern procedure '" + proc.name +
          "' returns capability-bearing type; " +
          "foreign code cannot return capabilities";
      return result;
    }
  }

  return result;
}

AuthorityValidationResult CheckCapabilityIsolation(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ExternProcDecl& proc) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = proc.span;

  for (const auto& param : proc.params) {
    if (!param.type) {
      continue;
    }
    const auto caps =
        InferCapabilitiesFromAstType(ctx, current_module, *param.type);
    if (caps.IsEmpty()) {
      continue;
    }
    result.valid = false;
    result.error_code = "E-TYP-2623";
    result.error_message =
        "Extern procedure '" + proc.name +
        "' has capability-bearing parameter '" + param.name +
        "'; foreign code cannot receive capabilities";
    return result;
  }

  if (proc.return_type_opt) {
    const auto caps = InferCapabilitiesFromAstType(
        ctx, current_module, *proc.return_type_opt);
    if (!caps.IsEmpty()) {
      result.valid = false;
      result.error_code = "E-TYP-2623";
      result.error_message =
          "Extern procedure '" + proc.name +
          "' returns capability-bearing type; " +
          "foreign code cannot return capabilities";
      return result;
    }
  }

  return result;
}

AuthorityValidationResult CheckExternBlockIsolation(
    const ast::ExternBlock& block) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = block.span;

  for (const auto& item : block.items) {
    // ExternItem is std::variant<ExternProcDecl> directly
    if (const auto* proc = std::get_if<ast::ExternProcDecl>(&item)) {
      auto proc_result = CheckCapabilityIsolation(*proc);
      if (!proc_result.valid) {
        return proc_result;
      }
    }
  }

  return result;
}

AuthorityValidationResult CheckExternBlockIsolation(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const ast::ExternBlock& block) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = block.span;

  for (const auto& item : block.items) {
    if (const auto* proc = std::get_if<ast::ExternProcDecl>(&item)) {
      auto proc_result = CheckCapabilityIsolation(ctx, current_module, *proc);
      if (!proc_result.valid) {
        return proc_result;
      }
    }
  }

  return result;
}

AuthorityValidationResult ValidateExternCall(
    const ast::CallExpr& call,
    const ast::ExternProcDecl& target,
    const std::vector<TypeRef>& arg_types) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  // CallExpr doesn't have span - leave result.span empty

  // Check that no capability types are passed as arguments
  for (std::size_t i = 0; i < arg_types.size(); ++i) {
    if (TypeContainsCapability(arg_types[i])) {
      result.valid = false;
      result.error_code = "E-TYP-2623";
      std::ostringstream msg;
      msg << "Call to extern procedure '" << target.name
          << "' passes capability type in argument " << (i + 1)
          << "; foreign code cannot receive capabilities";
      result.error_message = msg.str();
      return result;
    }
  }

  return result;
}

// =============================================================================
// Context field access validation
// =============================================================================

AuthorityValidationResult ValidateContextFieldAccess(
    const ast::FieldAccessExpr& access,
    const TypeRef& base_type) {
  SpecDefsAuthorityModel();
  AuthorityValidationResult result{};
  result.valid = true;
  // FieldAccessExpr doesn't have span - leave result.span empty

  if (!IsContextType(base_type)) {
    return result;  // Not a Context access
  }

  // Validate the field name is a valid Context field
  auto field_type = ContextFieldType(access.name);
  if (!field_type) {
    result.valid = false;
    result.error_code = "E-TYP-1904";
    result.error_message =
        "Invalid Context field access: '" + access.name +
        "' is not a valid Context field";
  }

  return result;
}

bool IsCapabilityFieldAccess(
    const ast::FieldAccessExpr& access,
    const TypeRef& base_type) {
  SpecDefsAuthorityModel();

  if (!IsContextType(base_type)) {
    return false;
  }

  return IdEq(access.name, "io") || IdEq(access.name, "heap") ||
         IdEq(access.name, "sys") || IdEq(access.name, "reactor");
}

// =============================================================================
// Whole-program authority validation
// =============================================================================

ModuleAuthorityResult ValidateModuleAuthority(const ast::ASTModule& module,
                                              const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  ModuleAuthorityResult result;
  result.valid = true;

  for (const auto& item : module.items) {
    // ASTItem is a variant - always valid, no null check needed
    std::visit(
        [&result, expr_types](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          // Check procedures for ambient authority
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            auto proc_result = CheckAmbientAuthority(node, expr_types);
            if (!proc_result.valid) {
              result.valid = false;
              result.errors.push_back(proc_result);
            }
          }
          // Check extern blocks for capability isolation
          else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            auto extern_result = CheckExternBlockIsolation(node);
            if (!extern_result.valid) {
              result.valid = false;
              result.errors.push_back(extern_result);
            }
          }
        },
        item);  // ASTItem IS the variant directly
  }

  return result;
}

ModuleAuthorityResult ValidateModuleAuthority(
    const std::vector<const ast::ASTModule*>& modules,
    const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  ModuleAuthorityResult result;
  result.valid = true;

  for (const ast::ASTModule* module : modules) {
    if (!module) continue;

    auto module_result = ValidateModuleAuthority(*module, expr_types);
    if (!module_result.valid) {
      result.valid = false;
      result.errors.insert(result.errors.end(),
                           module_result.errors.begin(),
                           module_result.errors.end());
    }
  }

  return result;
}

ModuleAuthorityResult ValidateModuleAuthority(const ScopeContext& ctx,
                                              const ast::ASTModule& module,
                                              const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  ModuleAuthorityResult result;
  result.valid = true;

  for (const auto& item : module.items) {
    std::visit(
        [&result, &ctx, &module, expr_types](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            auto proc_result =
                CheckAmbientAuthority(ctx, module.path, node, expr_types);
            if (!proc_result.valid) {
              result.valid = false;
              result.errors.push_back(proc_result);
            }
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            auto extern_result =
                CheckExternBlockIsolation(ctx, module.path, node);
            if (!extern_result.valid) {
              result.valid = false;
              result.errors.push_back(extern_result);
            }
          }
        },
        item);
  }

  return result;
}

ModuleAuthorityResult ValidateModuleAuthority(
    const ScopeContext& ctx,
    const std::vector<const ast::ASTModule*>& modules,
    const ExprTypeMap* expr_types) {
  SpecDefsAuthorityModel();
  ModuleAuthorityResult result;
  result.valid = true;

  for (const ast::ASTModule* module : modules) {
    if (!module) {
      continue;
    }

    auto module_result = ValidateModuleAuthority(ctx, *module, expr_types);
    if (!module_result.valid) {
      result.valid = false;
      result.errors.insert(result.errors.end(),
                           module_result.errors.begin(),
                           module_result.errors.end());
    }
  }

  return result;
}

}  // namespace ultraviolet::analysis

