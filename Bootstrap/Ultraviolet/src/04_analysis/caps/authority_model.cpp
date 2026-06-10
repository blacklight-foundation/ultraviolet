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
#include <unordered_map>
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

struct AttenuationOrigin {
  std::string parent;
  core::Span span;
};

struct AttenuationBindingInfo {
  int depth = 0;
  bool parameter = false;
  std::optional<std::string> parent;
  core::Span span;
};

struct AttenuationValidationContext {
  const ExprTypeMap* expr_types = nullptr;
  std::vector<std::vector<std::string>> scopes;
  std::unordered_map<std::string, AttenuationBindingInfo> bindings;
  AuthorityValidationResult result;
};

TypeRef StripPermAndRefineForAuthority(const TypeRef& type) {
  TypeRef current = type;
  bool changed = true;
  while (current && changed) {
    changed = false;
    if (const auto* perm = std::get_if<TypePerm>(&current->node)) {
      current = perm->base;
      changed = true;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&current->node)) {
      current = refine->base;
      changed = true;
      continue;
    }
  }
  return current;
}

bool TypePathEquals(const TypePath& path, std::string_view name) {
  return path.size() == 1 && IdEq(path.front(), name);
}

bool IsDynamicTypeNamed(const TypeRef& type, std::string_view name) {
  const TypeRef stripped = StripPermAndRefineForAuthority(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return TypePathEquals(dyn->path, name);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return TypePathEquals(path->path, name);
  }
  return false;
}

bool IsCancelTokenActiveType(const TypeRef& type) {
  const TypeRef stripped = StripPermAndRefineForAuthority(type);
  if (!stripped) {
    return false;
  }
  const auto* modal = std::get_if<TypeModalState>(&stripped->node);
  return modal && TypePathEquals(modal->path, "CancelToken") &&
         IdEq(modal->state, "Active");
}

bool IsAttenuationMethod(std::string_view name, const TypeRef& receiver_type) {
  const TypeRef stripped = StripPermAndRefineForAuthority(receiver_type);
  if (!stripped) {
    return false;
  }
  if (name == "restrict") {
    return CapabilityKindFromTypeRef(stripped) == CapabilityKind::IO;
  }
  if (name == "restrict_to_host") {
    return CapabilityKindFromTypeRef(stripped) == CapabilityKind::Network;
  }
  if (name == "with_quota") {
    return CapabilityKindFromTypeRef(stripped) == CapabilityKind::HeapAllocator;
  }
  if (name == "monotonic" || name == "wall") {
    return CapabilityKindFromTypeRef(stripped) == CapabilityKind::Time;
  }
  if (name == "coarsen") {
    return IsDynamicTypeNamed(stripped, "MonotonicTime") ||
           IsDynamicTypeNamed(stripped, "WallTime");
  }
  if (name == "cpu" || name == "gpu" || name == "inline") {
    return IsContextType(stripped);
  }
  if (name == "child") {
    return IsCancelTokenActiveType(stripped);
  }
  return false;
}

const TypeRef* ExprTypeForAttenuation(const ast::Expr& expr,
                                      const ExprTypeMap* expr_types) {
  if (!expr_types) {
    return nullptr;
  }
  const auto it = expr_types->find(&expr);
  if (it == expr_types->end()) {
    return nullptr;
  }
  return &it->second;
}

std::optional<std::string> DirectIdentifierName(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return DirectIdentifierName(attributed->expr);
  }
  if (const auto* move_expr = std::get_if<ast::MoveExpr>(&expr->node)) {
    return DirectIdentifierName(move_expr->place);
  }
  return std::nullopt;
}

int CurrentAttenuationDepth(const AttenuationValidationContext& ctx) {
  return ctx.scopes.empty() ? 0 : static_cast<int>(ctx.scopes.size()) - 1;
}

bool AttenuationFailed(const AttenuationValidationContext& ctx) {
  return !ctx.result.valid;
}

void ReportAttenuationEscape(AttenuationValidationContext& ctx,
                             const core::Span& span,
                             std::string_view parent) {
  if (AttenuationFailed(ctx)) {
    return;
  }
  SPEC_RULE("req.AttenuationParentDropRejectedWithLiveChildren");
  ctx.result.valid = false;
  ctx.result.error_code = "E-MEM-3020";
  ctx.result.span = span;
  ctx.result.error_message =
      "Derived capability escapes the lifetime of parent capability '" +
      std::string(parent) +
      "'; dropping a parent capability while a derived child remains live is "
      "ill-formed";
}

bool ParentIsLocalToDepth(const AttenuationValidationContext& ctx,
                          std::string_view parent,
                          int depth) {
  const auto it = ctx.bindings.find(std::string(parent));
  return it != ctx.bindings.end() && !it->second.parameter &&
         it->second.depth >= depth;
}

void CheckOriginEscapesScope(AttenuationValidationContext& ctx,
                             const std::optional<AttenuationOrigin>& origin,
                             int scope_depth) {
  if (!origin.has_value()) {
    return;
  }
  if (ParentIsLocalToDepth(ctx, origin->parent, scope_depth)) {
    ReportAttenuationEscape(ctx, origin->span, origin->parent);
  }
}

void PushAttenuationScope(AttenuationValidationContext& ctx) {
  ctx.scopes.emplace_back();
}

void PopAttenuationScope(AttenuationValidationContext& ctx) {
  if (ctx.scopes.empty()) {
    return;
  }
  const int depth = CurrentAttenuationDepth(ctx);
  for (const auto& local : ctx.scopes.back()) {
    const auto parent_it = ctx.bindings.find(local);
    if (parent_it == ctx.bindings.end()) {
      continue;
    }
    for (const auto& [child_name, child] : ctx.bindings) {
      if (!child.parent.has_value() || *child.parent != local) {
        continue;
      }
      if (child.depth < depth) {
        ReportAttenuationEscape(ctx, child.span, local);
        break;
      }
    }
  }
  for (const auto& local : ctx.scopes.back()) {
    ctx.bindings.erase(local);
  }
  ctx.scopes.pop_back();
}

void BindAttenuationName(
    AttenuationValidationContext& ctx,
    const std::string& name,
    std::optional<AttenuationOrigin> origin,
    bool parameter,
    const core::Span& span) {
  if (ctx.scopes.empty()) {
    PushAttenuationScope(ctx);
  }
  ctx.scopes.back().push_back(name);
  AttenuationBindingInfo info;
  info.depth = CurrentAttenuationDepth(ctx);
  info.parameter = parameter;
  info.span = span;
  if (origin.has_value()) {
    info.parent = origin->parent;
  }
  ctx.bindings[name] = std::move(info);
}

void CollectPatternBindingNames(const ast::Pattern& pattern,
                                std::vector<std::string>& names);

void CollectFieldPatternBindingNames(const ast::FieldPattern& field,
                                     std::vector<std::string>& names) {
  if (field.pattern_opt) {
    CollectPatternBindingNames(*field.pattern_opt, names);
  } else if (field.name != "_") {
    names.push_back(field.name);
  }
}

void CollectPatternBindingNames(const ast::Pattern& pattern,
                                std::vector<std::string>& names) {
  std::visit(
      [&names](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          if (node.name != "_") {
            names.push_back(node.name);
          }
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name != "_") {
            names.push_back(node.name);
          }
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& element : node.elements) {
            if (element) {
              CollectPatternBindingNames(*element, names);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            CollectFieldPatternBindingNames(field, names);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt.has_value()) {
            return;
          }
          std::visit(
              [&names](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& element : payload.elements) {
                    if (element) {
                      CollectPatternBindingNames(*element, names);
                    }
                  }
                } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                  for (const auto& field : payload.fields) {
                    CollectFieldPatternBindingNames(field, names);
                  }
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt.has_value()) {
            return;
          }
          for (const auto& field : node.fields_opt->fields) {
            CollectFieldPatternBindingNames(field, names);
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          if (node.lo) {
            CollectPatternBindingNames(*node.lo, names);
          }
          if (node.hi) {
            CollectPatternBindingNames(*node.hi, names);
          }
        }
      },
      pattern.node);
}

std::optional<AttenuationOrigin> AnalyzeAttenuationExpr(
    AttenuationValidationContext& ctx,
    const ast::ExprPtr& expr);
std::optional<AttenuationOrigin> AnalyzeAttenuationBlock(
    AttenuationValidationContext& ctx,
    const ast::Block& block);
void AnalyzeAttenuationStmt(AttenuationValidationContext& ctx,
                            const ast::Stmt& stmt);

void AnalyzeAttenuationArgs(AttenuationValidationContext& ctx,
                            const std::vector<ast::Arg>& args) {
  for (const auto& arg : args) {
    (void)AnalyzeAttenuationExpr(ctx, arg.value);
  }
}

std::optional<AttenuationOrigin> FirstOrigin(
    std::optional<AttenuationOrigin> left,
    std::optional<AttenuationOrigin> right) {
  return left.has_value() ? left : right;
}

std::optional<AttenuationOrigin> AnalyzeAttenuationExpr(
    AttenuationValidationContext& ctx,
    const ast::ExprPtr& expr) {
  if (!expr || AttenuationFailed(ctx)) {
    return std::nullopt;
  }

  return std::visit(
      [&ctx, &expr](const auto& node) -> std::optional<AttenuationOrigin> {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto it = ctx.bindings.find(node.name);
          if (it != ctx.bindings.end() && it->second.parent.has_value()) {
            return AttenuationOrigin{*it->second.parent, expr->span};
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.place);
        } else if constexpr (std::is_same_v<T, ast::CopyExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.expr);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          std::optional<AttenuationOrigin> origin;
          for (const auto& element : node.elements) {
            origin = FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, element));
          }
          return origin;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          std::optional<AttenuationOrigin> origin;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& element) {
            origin = FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, element));
          });
          return origin;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.value),
                             AnalyzeAttenuationExpr(ctx, node.count));
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          std::optional<AttenuationOrigin> origin;
          for (const auto& field : node.fields) {
            origin = FirstOrigin(origin,
                                 AnalyzeAttenuationExpr(ctx, field.value));
          }
          return origin;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          std::optional<AttenuationOrigin> origin;
          if (!node.payload_opt.has_value()) {
            return origin;
          }
          std::visit(
              [&ctx, &origin](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& element : payload.elements) {
                    origin =
                        FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, element));
                  }
                } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                  for (const auto& field : payload.fields) {
                    origin = FirstOrigin(
                        origin, AnalyzeAttenuationExpr(ctx, field.value));
                  }
                }
              },
              *node.payload_opt);
          return origin;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.lhs),
                             AnalyzeAttenuationExpr(ctx, node.rhs));
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.lhs),
                             AnalyzeAttenuationExpr(ctx, node.rhs));
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.cond);
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.then_expr),
                             AnalyzeAttenuationExpr(ctx, node.else_expr));
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.scrutinee);
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.then_expr),
                             AnalyzeAttenuationExpr(ctx, node.else_expr));
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          std::optional<AttenuationOrigin> origin =
              AnalyzeAttenuationExpr(ctx, node.scrutinee);
          for (const auto& clause : node.cases) {
            origin =
                FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, clause.body));
          }
          return FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, node.else_expr));
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (!node.block) {
            return std::nullopt;
          }
          return AnalyzeAttenuationBlock(ctx, *node.block);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (!node.block) {
            return std::nullopt;
          }
          return AnalyzeAttenuationBlock(ctx, *node.block);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.cond);
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.iter);
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.body);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.lhs),
                             AnalyzeAttenuationExpr(ctx, node.rhs));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return FirstOrigin(AnalyzeAttenuationExpr(ctx, node.base),
                             AnalyzeAttenuationExpr(ctx, node.index));
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.callee);
          AnalyzeAttenuationArgs(ctx, node.args);
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::CallTypeArgsExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.callee);
          AnalyzeAttenuationArgs(ctx, node.args);
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.receiver);
          AnalyzeAttenuationArgs(ctx, node.args);
          const TypeRef* receiver_type =
              node.receiver ? ExprTypeForAttenuation(*node.receiver,
                                                     ctx.expr_types)
                            : nullptr;
          if (receiver_type &&
              IsAttenuationMethod(node.name, *receiver_type)) {
            if (const auto parent = DirectIdentifierName(node.receiver)) {
              if (ctx.bindings.find(*parent) != ctx.bindings.end()) {
                return AttenuationOrigin{*parent, expr->span};
              }
            }
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          return AnalyzeAttenuationExpr(ctx, node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          std::optional<AttenuationOrigin> origin;
          for (const auto& arm : node.arms) {
            origin = FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, arm.expr));
            origin = FirstOrigin(origin,
                                 AnalyzeAttenuationExpr(ctx, arm.handler.value));
          }
          return origin;
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          std::optional<AttenuationOrigin> origin;
          for (const auto& value : node.exprs) {
            origin = FirstOrigin(origin, AnalyzeAttenuationExpr(ctx, value));
          }
          return origin;
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.domain);
          for (const auto& opt : node.opts) {
            (void)AnalyzeAttenuationExpr(ctx, opt.value);
          }
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            (void)AnalyzeAttenuationExpr(ctx, opt.value);
          }
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          return AnalyzeAttenuationExpr(ctx, node.handle);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          (void)AnalyzeAttenuationExpr(ctx, node.range);
          for (const auto& opt : node.opts) {
            (void)AnalyzeAttenuationExpr(ctx, opt.chunk_expr);
            (void)AnalyzeAttenuationExpr(ctx, opt.workgroup_expr);
          }
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
          return std::nullopt;
        } else {
          return std::nullopt;
        }
      },
      expr->node);
}

void AnalyzeAttenuationBinding(AttenuationValidationContext& ctx,
                               const ast::Binding& binding) {
  std::optional<AttenuationOrigin> origin =
      AnalyzeAttenuationExpr(ctx, binding.init);
  std::vector<std::string> names;
  if (binding.pat) {
    CollectPatternBindingNames(*binding.pat, names);
  }
  for (const auto& name : names) {
    BindAttenuationName(ctx, name, origin, false, binding.span);
  }
}

void AnalyzeAttenuationStmt(AttenuationValidationContext& ctx,
                            const ast::Stmt& stmt) {
  if (AttenuationFailed(ctx)) {
    return;
  }
  std::visit(
      [&ctx](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          AnalyzeAttenuationBinding(ctx, node.binding);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          const auto origin = AnalyzeAttenuationExpr(ctx, node.value);
          if (!origin.has_value()) {
            return;
          }
          const auto target = DirectIdentifierName(node.place);
          if (!target.has_value()) {
            return;
          }
          const auto target_it = ctx.bindings.find(*target);
          const auto parent_it = ctx.bindings.find(origin->parent);
          if (target_it != ctx.bindings.end() &&
              parent_it != ctx.bindings.end() &&
              !parent_it->second.parameter &&
              target_it->second.depth < parent_it->second.depth) {
            ReportAttenuationEscape(ctx, origin->span, origin->parent);
          }
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          (void)AnalyzeAttenuationExpr(ctx, node.place);
          (void)AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          (void)AnalyzeAttenuationExpr(ctx, node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          (void)AnalyzeAttenuationExpr(ctx, node.opts_opt);
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          const auto origin = AnalyzeAttenuationExpr(ctx, node.value_opt);
          if (origin.has_value()) {
            const auto parent_it = ctx.bindings.find(origin->parent);
            if (parent_it != ctx.bindings.end() &&
                !parent_it->second.parameter) {
              ReportAttenuationEscape(ctx, origin->span, origin->parent);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          const auto origin = AnalyzeAttenuationExpr(ctx, node.value_opt);
          if (origin.has_value()) {
            const auto parent_it = ctx.bindings.find(origin->parent);
            if (parent_it != ctx.bindings.end() &&
                !parent_it->second.parameter) {
              ReportAttenuationEscape(ctx, origin->span, origin->parent);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            (void)AnalyzeAttenuationBlock(ctx, *node.body);
          }
        }
      },
      stmt);
}

std::optional<AttenuationOrigin> AnalyzeAttenuationBlock(
    AttenuationValidationContext& ctx,
    const ast::Block& block) {
  PushAttenuationScope(ctx);
  const int scope_depth = CurrentAttenuationDepth(ctx);
  for (const auto& stmt : block.stmts) {
    AnalyzeAttenuationStmt(ctx, stmt);
    if (AttenuationFailed(ctx)) {
      PopAttenuationScope(ctx);
      return std::nullopt;
    }
  }
  const auto origin = AnalyzeAttenuationExpr(ctx, block.tail_opt);
  CheckOriginEscapesScope(ctx, origin, scope_depth);
  PopAttenuationScope(ctx);
  if (AttenuationFailed(ctx)) {
    return std::nullopt;
  }
  return origin;
}

AuthorityValidationResult CheckAttenuationParentLiveness(
    const ast::ProcedureDecl& proc,
    const ExprTypeMap* expr_types) {
  AuthorityValidationResult result{};
  result.valid = true;
  result.span = proc.span;
  if (!proc.body) {
    return result;
  }

  AttenuationValidationContext ctx;
  ctx.expr_types = expr_types;
  ctx.result.valid = true;
  PushAttenuationScope(ctx);
  for (const auto& param : proc.params) {
    BindAttenuationName(ctx, param.name, std::nullopt, true, param.span);
  }
  for (const auto& stmt : proc.body->stmts) {
    AnalyzeAttenuationStmt(ctx, stmt);
    if (AttenuationFailed(ctx)) {
      return ctx.result;
    }
  }
  const auto tail = AnalyzeAttenuationExpr(ctx, proc.body->tail_opt);
  CheckOriginEscapesScope(ctx, tail, CurrentAttenuationDepth(ctx));
  if (AttenuationFailed(ctx)) {
    return ctx.result;
  }
  PopAttenuationScope(ctx);
  return result;
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

  return IdEq(access.name, "io") || IdEq(access.name, "net") ||
         IdEq(access.name, "heap") || IdEq(access.name, "sys") ||
         IdEq(access.name, "reactor") || IdEq(access.name, "time");
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
            auto attenuation_result =
                CheckAttenuationParentLiveness(node, expr_types);
            if (!attenuation_result.valid) {
              result.valid = false;
              result.errors.push_back(attenuation_result);
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
            auto attenuation_result =
                CheckAttenuationParentLiveness(node, expr_types);
            if (!attenuation_result.valid) {
              result.valid = false;
              result.errors.push_back(attenuation_result);
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
