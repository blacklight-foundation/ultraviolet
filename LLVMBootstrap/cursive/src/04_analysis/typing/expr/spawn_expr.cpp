// =================================================================
// File: 04_analysis/typing/expr/spawn_expr.cpp
// Construct: Spawn Expression Type Checking
// Spec Section: 17.2.2 (18.4.1)
// Spec Rules: T-Spawn
// =================================================================
#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include <optional>
#include <unordered_set>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsSpawn() {
  SPEC_DEF("T-Spawn", "17.2.2");
  SPEC_DEF("GpuContext", "20.2.3");
}

bool IsStringType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<TypeString>(stripped->node);
}

bool IsNamedTypePath(const TypeRef& type, std::string_view name) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* path = std::get_if<TypePathType>(&stripped->node);
  if (!path || path->path.empty()) {
    return false;
  }
  return IdEq(path->path.back(), name);
}

bool IsGpuDomainType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
}

void EmitSupplementalTypeDiag(const StmtTypeContext& type_ctx,
                              std::string_view code) {
  if (!type_ctx.diags) {
    return;
  }
  if (auto diag = core::MakeDiagnosticById(code)) {
    core::Emit(*type_ctx.diags, *diag);
  }
}

std::optional<IdKey> RootBindingOfPlace(const ast::ExprPtr& place) {
  if (!place) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<IdKey> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return IdKeyOf(node.name);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (node.path.empty()) {
            return IdKeyOf(node.name);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return RootBindingOfPlace(node.place);
        } else {
          return std::nullopt;
        }
      },
      place->node);
}

struct SpawnCaptureInfo {
  std::unordered_set<IdKey> captures;
  std::unordered_set<IdKey> explicit_moves;
};

class SpawnCaptureCollector {
 public:
  explicit SpawnCaptureCollector(const TypeEnv& env) : env_(env) {
    local_scopes_.emplace_back();
  }

  SpawnCaptureInfo Collect(const ast::Block& block) {
    VisitBlock(block);
    SpawnCaptureInfo info;
    info.captures = captures_;
    info.explicit_moves = explicit_moves_;
    return info;
  }

 private:
  void PushScope() { local_scopes_.emplace_back(); }

  void PopScope() {
    if (!local_scopes_.empty()) {
      local_scopes_.pop_back();
    }
  }

  bool IsLocal(std::string_view name) const {
    const IdKey key = IdKeyOf(name);
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
      if (it->find(key) != it->end()) {
        return true;
      }
    }
    return false;
  }

  void DeclareName(std::string_view name) {
    if (local_scopes_.empty()) {
      local_scopes_.emplace_back();
    }
    local_scopes_.back().insert(IdKeyOf(name));
  }

  void DeclarePattern(const ast::PatternPtr& pattern) {
    if (!pattern) {
      return;
    }
    std::vector<IdKey> names;
    CollectPatNames(*pattern, names);
    for (const auto& name : names) {
      DeclareName(name);
    }
  }

  void CaptureIfOuter(std::string_view name) {
    if (IsLocal(name)) {
      return;
    }
    if (BindOf(env_, name).has_value()) {
      captures_.insert(IdKeyOf(name));
    }
  }

  void MarkExplicitMoveIfOuter(const ast::ExprPtr& place) {
    const auto root = RootBindingOfPlace(place);
    if (!root.has_value()) {
      return;
    }
    if (IsLocal(*root)) {
      return;
    }
    if (!BindOf(env_, *root).has_value()) {
      return;
    }
    explicit_moves_.insert(*root);
  }

  void VisitKeyPath(const ast::KeyPathExpr& path) {
    CaptureIfOuter(path.root);
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitBlock(const ast::Block& block) {
    PushScope();
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(block.tail_opt);
    PopScope();
  }

  void VisitStmt(const ast::Stmt& stmt) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
            DeclarePattern(node.binding.pat);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression,
            // but the alias name still enters the surrounding scope.
            DeclareName(node.alias);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            VisitExpr(node.opts_opt);
            if (node.body) {
              PushScope();
              if (node.alias_opt.has_value()) {
                DeclareName(*node.alias_opt);
              }
              for (const auto& inner : node.body->stmts) {
                VisitStmt(inner);
              }
              VisitExpr(node.body->tail_opt);
              PopScope();
            }
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            if (node.target_opt.has_value()) {
              CaptureIfOuter(*node.target_opt);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else {
            // ContinueStmt / ErrorStmt carry no expressions.
          }
        },
        stmt);
  }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
            CaptureIfOuter(node.name);
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            if (std::holds_alternative<ast::ParenArgs>(node.args)) {
              const auto& args = std::get<ast::ParenArgs>(node.args).args;
              for (const auto& arg : args) {
                VisitExpr(arg.value);
              }
            } else {
              const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
              for (const auto& field : fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            MarkExplicitMoveIfOuter(node.place);
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
            VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            VisitExpr(elem);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            VisitExpr(elem);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          VisitExpr(node.value);
          VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (!node.payload_opt.has_value()) {
              return;
            }
            if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
              const auto& payload = std::get<ast::EnumPayloadParen>(*node.payload_opt);
              for (const auto& elem : payload.elements) {
                VisitExpr(elem);
              }
            } else {
              const auto& payload = std::get<ast::EnumPayloadBrace>(*node.payload_opt);
              for (const auto& field : payload.fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& arm : node.cases) {
              PushScope();
              DeclarePattern(arm.pattern);
              VisitExpr(arm.body);
              PopScope();
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            PushScope();
            DeclarePattern(node.pattern);
            VisitExpr(node.then_expr);
            PopScope();
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            PushScope();
            DeclarePattern(node.pattern);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            PushScope();
            for (const auto& param : node.params) {
              DeclareName(param.name);
            }
            VisitExpr(node.body);
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              if (arm.pattern) {
                PushScope();
                DeclarePattern(arm.pattern);
                VisitExpr(arm.handler.value);
                PopScope();
              } else {
                VisitExpr(arm.handler.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            PushScope();
            DeclarePattern(node.pattern);
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else {
            // ErrorExpr, LiteralExpr, QualifiedNameExpr, PathExpr,
            // PtrNullExpr, SizeofExpr, AlignofExpr, ResultExpr have no children.
          }
        },
        expr->node);
  }

  const TypeEnv& env_;
  std::vector<std::unordered_set<IdKey>> local_scopes_;
  std::unordered_set<IdKey> captures_;
  std::unordered_set<IdKey> explicit_moves_;
};

// Check capture permission (unique requires explicit move)
std::optional<std::string_view> CheckCapturePermission(
    const TypeRef& type,
    bool is_explicit_move) {
  if (!type) return std::nullopt;

  const auto perm = PermOfType(type);
  if (perm == Permission::Unique && !is_explicit_move) {
    return "E-CON-0120";  // unique binding captured without explicit move
  }

  return std::nullopt;
}

}  // namespace

// §17.2.2 (§18.4.1) Spawn Expression Typing
//
// Typing rule (T-Spawn):
// Inside parallel block
// opts well-formed
// Gamma |- body : T
// --------------------------------------------------
// Gamma |- spawn opts {body} : Spawned<T>
//
// Spawned<T> is a modal type with states @Pending and @Ready
// The value can be retrieved via wait expression.
//
ExprTypeResult TypeSpawnExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::SpawnExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place) {
  SPEC_RULE("T-Spawn");
  ExprTypeResult result;

  // Check that we're inside a parallel block
  if (!type_ctx.in_parallel) {
    result.diag_id = "E-CON-0101";  // spawn without enclosing parallel block
    return result;
  }

  // Validate spawn option value types (name/affinity/priority).
  for (const auto& opt : expr.opts) {
    if (!opt.value) {
      result.diag_id = "E-CON-0130";
      return result;
    }

    const auto opt_type = type_expr(opt.value);
    if (!opt_type.ok) {
      result.diag_id = opt_type.diag_id;
      return result;
    }

    switch (opt.kind) {
      case ast::SpawnOptionKind::Name:
        if (!IsStringType(opt_type.type)) {
          result.diag_id = "E-CON-0130";
          return result;
        }
        break;
      case ast::SpawnOptionKind::Affinity:
        if (!IsNamedTypePath(opt_type.type, "CpuSet")) {
          result.diag_id = "E-CON-0130";
          return result;
        }
        break;
      case ast::SpawnOptionKind::Priority:
        if (!IsNamedTypePath(opt_type.type, "Priority")) {
          result.diag_id = "E-CON-0130";
          return result;
        }
        break;
    }
  }

  // Handle empty body case
  if (!expr.body) {
    result.ok = true;
    result.type = MakeSpawnedType(MakeTypePrim("()"));
    return result;
  }

  // Type check spawn body
  ExprTypeResult body_result = TypeBlock(ctx, type_ctx, *expr.body, env,
                                         type_expr, type_ident, type_place);
  if (!body_result.ok) {
    result.diag_id = body_result.diag_id;
    return result;
  }

  // Capture analysis for spawn body
  // Walk the spawn body and analyze outer bindings that are captured.
  const auto capture_sets = *AnalyzeBlockCaptureSets(*expr.body, env);
  const auto capture_info = SpawnCaptureCollector(env).Collect(*expr.body);
  for (const auto& captured_name : capture_sets.captures) {
    const auto binding = BindOf(env, captured_name);
    if (!binding.has_value()) {
      continue;
    }

    const bool is_explicit_move =
        capture_info.explicit_moves.find(captured_name) !=
        capture_info.explicit_moves.end();

    if (is_explicit_move &&
        IsOuterParallelBinding(type_ctx, captured_name) &&
        !ClaimFirstChildMove(type_ctx, captured_name)) {
      result.diag_id = "E-CON-0122";
      return result;
    }

    if (const auto capture_diag = CheckCapturePermission(binding->type, is_explicit_move);
        capture_diag.has_value()) {
      result.diag_id = *capture_diag;
      return result;
    }

    if (GpuContext(env)) {
      const auto gpu_capture =
          CheckGpuCapture(ctx, env, captured_name, is_explicit_move);
      if (!gpu_capture.ok) {
        if (gpu_capture.supplemental_diag_id.has_value()) {
          EmitSupplementalTypeDiag(type_ctx, *gpu_capture.supplemental_diag_id);
        }
        result.diag_id = gpu_capture.diag_id;
        return result;
      }
    }
  }

  // Return type is Spawned<T> where T is body type
  result.ok = true;
  result.type = MakeSpawnedType(body_result.type);
  return result;
}

}  // namespace cursive::analysis::expr
