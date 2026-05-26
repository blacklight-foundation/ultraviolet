// =============================================================================
// File: 04_analysis/typing/expr/parallel_expr.cpp
// Parallel Block Expression Typing
// Spec Section: 17.2
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 17.2: Structured Parallelism
//   - T-Parallel: Parallel block typing
//   - T-Parallel-Empty: Empty parallel block
//   - T-Parallel-Single: Single spawn
//   - T-Parallel-Multi: Multiple spawns
//   - Structured concurrency invariant
//
// =============================================================================

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsParallel() {
  SPEC_DEF("T-Parallel", "17.2");
  SPEC_DEF("T-Parallel-Empty", "17.2");
  SPEC_DEF("T-Parallel-Single", "17.2");
  SPEC_DEF("T-Parallel-Multi", "17.2");
  SPEC_DEF("ParallelBlockOpts", "17.2");
  SPEC_DEF("CaptureSemantics", "17.2");
  SPEC_DEF("ForkJoin", "17.2");
  SPEC_DEF("ExecutionDomain", "17.2");
  SPEC_DEF("T-GPU-Nested-Err", "18.1.1");
  SPEC_DEF("GpuCapture-HeapProv-Err", "20.3.4");
  SPEC_DEF("Dim3Const-Err", "20.2.4");
  SPEC_DEF("WorkgroupSize-Err", "20.2.4");
}

core::Diagnostic MakeInternalTypingDiagnostic(core::Severity severity,
                                              const std::string& message) {
  core::Diagnostic diag;
  diag.severity = severity;
  diag.message = message;
  return diag;
}

// Strip permission qualifiers
static TypeRef StripPermLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

// Check if type is $ExecutionDomain
static bool IsExecutionDomainType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  // Accept ExecutionDomain and built-in domain subclasses (Cpu/Gpu/Inline).
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    if (IsExecutionDomainTypePath(dyn->path)) {
      return true;
    }
  }

  // Also allow path-based references.
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    if (IsExecutionDomainTypePath(path->path)) {
      return true;
    }
  }

  return false;
}

static bool IsNamedTypePathLocal(const TypeRef& type, std::string_view name) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }
  const auto* path = std::get_if<TypePathType>(&stripped->node);
  if (!path || path->path.empty()) {
    return false;
  }
  return path->path.back() == name;
}

static const ast::Expr* StripParallelDomainExpr(const ast::ExprPtr& expr) {
  const ast::Expr* cur = expr.get();
  while (cur) {
    if (const auto* attributed = std::get_if<ast::AttributedExpr>(&cur->node)) {
      cur = attributed->expr.get();
      continue;
    }
    return cur;
  }
  return nullptr;
}

static std::optional<ParallelContextKind> ParallelContextKindOf(
    const ast::ExprPtr& expr,
    const TypeEnv& env) {
  const ast::Expr* core = StripParallelDomainExpr(expr);
  if (!core) {
    return std::nullopt;
  }
  const auto* method = std::get_if<ast::MethodCallExpr>(&core->node);
  if (!method) {
    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&core->node)) {
      if (const auto binding = BindOf(env, ident->name)) {
        return binding->parallel_context_kind;
      }
    }
    return std::nullopt;
  }
  if (method->name == "cpu") {
    return ParallelContextKind::Cpu;
  }
  if (method->name == "gpu") {
    return ParallelContextKind::Gpu;
  }
  if (method->name == "inline") {
    return ParallelContextKind::Inline;
  }
  return std::nullopt;
}

static bool IsGpuDomain(const ast::ExprPtr& expr, const TypeEnv& env) {
  SPEC_RULE("IsGpuDomain");
  return ParallelContextKindOf(expr, env) == ParallelContextKind::Gpu;
}

static const ast::MethodCallExpr* ParallelDomainCtorCallOf(
    const ast::ExprPtr& expr) {
  const ast::Expr* core = StripParallelDomainExpr(expr);
  if (!core) {
    return nullptr;
  }
  return std::get_if<ast::MethodCallExpr>(&core->node);
}

static std::optional<std::string_view> ParallelDomainParamDiag(
    const ScopeContext& ctx,
    const ast::ExprPtr& domain_expr,
    const ExprTypeFn& type_expr) {
  const auto* call = ParallelDomainCtorCallOf(domain_expr);
  if (!call || !call->receiver) {
    return std::nullopt;
  }

  const auto receiver_type = type_expr(call->receiver);
  if (!receiver_type.ok) {
    return std::nullopt;
  }

  const auto stripped_receiver = StripPermLocal(receiver_type.type);
  const auto* receiver_path =
      stripped_receiver ? std::get_if<TypePathType>(&stripped_receiver->node)
                        : nullptr;
  if (!receiver_path || !IsContextTypePath(receiver_path->path)) {
    return std::nullopt;
  }

  const auto sig = LookupContextMethodSig(call->name, call->args.size());
  if (!sig.has_value()) {
    return "E-CON-0103";
  }

  for (std::size_t i = 0; i < call->args.size(); ++i) {
    const auto& arg = call->args[i];
    if (!arg.value) {
      return "E-CON-0103";
    }
    const auto arg_type = type_expr(arg.value);
    if (!arg_type.ok) {
      return arg_type.diag_id;
    }
    if (IdEq(call->name, "cpu")) {
      if (i == 0 && !IsNamedTypePathLocal(arg_type.type, "CpuSet")) {
        return "E-CON-0103";
      }
      if (i == 1 && !IsNamedTypePathLocal(arg_type.type, "Priority")) {
        return "E-CON-0103";
      }
    }
  }

  return std::nullopt;
}

static bool BindingHasHeapProvenance(const TypeBinding& binding) {
  return binding.provenance_kind == BindingProvenanceSeedKind::Heap;
}

// Check if type is CancelToken
static bool IsCancelTokenType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    if (IsCancelTokenTypePath(path->path)) {
      return true;
    }
  }

  if (const auto* modal = std::get_if<TypeModalState>(&stripped->node)) {
    if (IsCancelTokenTypePath(modal->path) &&
        IsValidCancelTokenState(modal->state)) {
      return true;
    }
  }

  return false;
}

// Check if type is string (for name option)
static bool IsStringType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  if (std::holds_alternative<TypeString>(stripped->node)) {
    return true;
  }

  return false;
}

static bool DispatchHasReduce(const ast::DispatchExpr& dispatch) {
  for (const auto& opt : dispatch.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

struct ParallelResultCollect {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<TypeRef> types;
};

static ast::ExprPtr StripAttributedExpr(ast::ExprPtr expr) {
  ast::ExprPtr cur = expr;
  while (cur) {
    const auto* attr = std::get_if<ast::AttributedExpr>(&cur->node);
    if (!attr) {
      break;
    }
    cur = attr->expr;
  }
  return cur;
}

static bool IsCollectableParallelExpr(const ast::ExprPtr& expr) {
  const auto stripped_expr = StripAttributedExpr(expr);
  if (!stripped_expr) {
    return false;
  }
  if (std::holds_alternative<ast::SpawnExpr>(stripped_expr->node)) {
    return true;
  }
  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&stripped_expr->node)) {
    return DispatchHasReduce(*dispatch);
  }
  return false;
}

static ParallelResultCollect CollectParallelResultExpr(
    const ExprTypeFn& type_expr,
    const ast::ExprPtr& expr,
    ParallelResultCollect in) {
  if (!in.ok || !expr) {
    return in;
  }

  const auto stripped_expr = StripAttributedExpr(expr);
  if (!stripped_expr) {
    return in;
  }

  if (const auto* spawn = std::get_if<ast::SpawnExpr>(&stripped_expr->node)) {
    (void)spawn;
    const auto typed = type_expr(stripped_expr);
    if (!typed.ok) {
      in.ok = false;
      in.diag_id = typed.diag_id;
      return in;
    }
    TypeRef collected = typed.type;
    if (const auto inner = ExtractSpawnedInner(StripPermLocal(typed.type))) {
      collected = *inner;
    }
    in.types.push_back(collected);
    return in;
  }

  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&stripped_expr->node)) {
    if (!DispatchHasReduce(*dispatch)) {
      return in;
    }
    const auto typed = type_expr(stripped_expr);
    if (!typed.ok) {
      in.ok = false;
      in.diag_id = typed.diag_id;
      return in;
    }
    in.types.push_back(typed.type);
    return in;
  }

  return in;
}

static ParallelResultCollect CollectParallelResultStmt(
    const ExprTypeFn& type_expr,
    const ast::Stmt& stmt,
    ParallelResultCollect in) {
  if (!in.ok) {
    return in;
  }
  if (const auto* expr_stmt = std::get_if<ast::ExprStmt>(&stmt)) {
    return CollectParallelResultExpr(type_expr, expr_stmt->value, std::move(in));
  }
  if (const auto* let_stmt = std::get_if<ast::LetStmt>(&stmt)) {
    return CollectParallelResultExpr(type_expr, let_stmt->binding.init, std::move(in));
  }
  if (const auto* var_stmt = std::get_if<ast::VarStmt>(&stmt)) {
    return CollectParallelResultExpr(type_expr, var_stmt->binding.init, std::move(in));
  }
  if (const auto* using_local = std::get_if<ast::UsingLocalStmt>(&stmt)) {
    // UsingLocalStmt is a compile-time alias; no runtime expression.
    (void)using_local;
    return in;
  }
  if (const auto* assign = std::get_if<ast::AssignStmt>(&stmt)) {
    return CollectParallelResultExpr(type_expr, assign->value, std::move(in));
  }
  if (const auto* compound = std::get_if<ast::CompoundAssignStmt>(&stmt)) {
    return CollectParallelResultExpr(type_expr, compound->value, std::move(in));
  }
  return in;
}

static void EmitSupplementalTypeDiag(const StmtTypeContext& type_ctx,
                                     std::string_view code) {
  if (!type_ctx.diags) {
    return;
  }
  if (auto diag = core::MakeDiagnosticById(code)) {
    core::Emit(*type_ctx.diags, *diag);
    return;
  }
  core::Emit(*type_ctx.diags, MakeInternalTypingDiagnostic(
                                  core::Severity::Error,
                                  "Internal error: unresolved diagnostic id '" +
                                      std::string(code) + "'"));
}

}  // namespace

ExprTypeResult TypeParallelExpr(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ParallelExpr& expr,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place) {
  SpecDefsParallel();
  ExprTypeResult result;

  if (!expr.domain || !expr.body) {
    return result;
  }

  // 1. Type the domain expression (must be $ExecutionDomain)
  SPEC_RULE("ExecutionDomain");
  const auto domain_type = type_expr(expr.domain);
  if (!domain_type.ok) {
    if (const auto domain_diag =
            ParallelDomainParamDiag(ctx, expr.domain, type_expr);
        domain_diag.has_value()) {
      result.diag_id = *domain_diag;
    } else {
      result.diag_id = domain_type.diag_id;
    }
    return result;
  }

  if (!IsExecutionDomainType(domain_type.type)) {
    // Domain expression must type as $ExecutionDomain.
    result.diag_id = "E-CON-0102";
    return result;
  }

  const bool gpu_domain = IsGpuDomain(expr.domain, env);

  if (GpuContext(env) && gpu_domain) {
    SPEC_RULE("T-GPU-Nested-Err");
    result.diag_id = "E-CON-0152";
    return result;
  }

  // 2. Check block options
  SPEC_RULE("ParallelBlockOpts");
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::ParallelOptionKind::Name) {
      continue;
    }

    if (!opt.value) {
      continue;
    }

    const auto opt_type = type_expr(opt.value);
    if (!opt_type.ok) {
      result.diag_id = opt_type.diag_id;
      return result;
    }

    switch (opt.kind) {
      case ast::ParallelOptionKind::Cancel:
        if (!IsCancelTokenType(opt_type.type)) {
          result.diag_id = "E-CON-0103";
          return result;
        }
        break;

      case ast::ParallelOptionKind::Workgroup:
      case ast::ParallelOptionKind::Workgroups: {
        const auto dims = ExtractDim3Const(ctx, opt.value, type_expr);
        if (!dims.has_value()) {
          SPEC_RULE("Dim3Const-Err");
          result.diag_id = "E-CON-0159";
          return result;
        }
        if (opt.kind == ast::ParallelOptionKind::Workgroup &&
            gpu_domain &&
            ExceedsMaxWorkgroupSize(*dims)) {
            SPEC_RULE("WorkgroupSize-Err");
            result.diag_id = "E-CON-0157";
            return result;
        }
        break;
      }

      case ast::ParallelOptionKind::Name:
        break;
    }
  }

  // 3. Create parallel context for body typing
  StmtTypeContext parallel_ctx = type_ctx;
  parallel_ctx.in_parallel = true;
  parallel_ctx.parallel_domain = domain_type.type;
  std::unordered_set<IdKey> parallel_ancestor_bindings;
  if (type_ctx.parallel_ancestor_bindings) {
    parallel_ancestor_bindings.insert(type_ctx.parallel_ancestor_bindings->begin(),
                                      type_ctx.parallel_ancestor_bindings->end());
  }
  if (type_ctx.parallel_bindings) {
    parallel_ancestor_bindings.insert(type_ctx.parallel_bindings->begin(),
                                      type_ctx.parallel_bindings->end());
  }
  std::unordered_set<IdKey> parallel_bindings;
  std::unordered_set<IdKey> parallel_first_child_moves;
  std::vector<ParallelCaptureScopeView> parallel_capture_scopes;
  if (type_ctx.parallel_capture_scopes) {
    parallel_capture_scopes = *type_ctx.parallel_capture_scopes;
  } else {
    if (type_ctx.parallel_ancestor_bindings &&
        type_ctx.parallel_first_child_moves) {
      parallel_capture_scopes.push_back(ParallelCaptureScopeView{
          .bindings = type_ctx.parallel_ancestor_bindings,
          .first_child_moves = type_ctx.parallel_first_child_moves,
      });
    }
    if (type_ctx.parallel_bindings &&
        type_ctx.parallel_first_child_moves &&
        type_ctx.parallel_bindings != type_ctx.parallel_ancestor_bindings) {
      parallel_capture_scopes.push_back(ParallelCaptureScopeView{
          .bindings = type_ctx.parallel_bindings,
          .first_child_moves = type_ctx.parallel_first_child_moves,
      });
    }
  }
  parallel_capture_scopes.push_back(ParallelCaptureScopeView{
      .bindings = &parallel_bindings,
      .first_child_moves = &parallel_first_child_moves,
  });
  parallel_ctx.parallel_bindings = &parallel_bindings;
  parallel_ctx.parallel_ancestor_bindings = &parallel_ancestor_bindings;
  parallel_ctx.parallel_capture_scopes = &parallel_capture_scopes;
  parallel_ctx.parallel_first_child_moves = &parallel_first_child_moves;
  TypeEnv parallel_env = env;
  parallel_env.parallel_context = ParallelContextKindOf(expr.domain, env);
  parallel_ctx.env_ref = &parallel_env;

  // Rebind recursive typing callbacks to the parallel context so nested
  // spawn/dispatch expressions are checked with in_parallel=true and the
  // evolving statement environment.
  const ExprTypeFn parallel_type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, parallel_ctx, inner, parallel_env);
  };
  const PlaceTypeFn parallel_type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, parallel_ctx, inner, parallel_env);
  };
  const IdentTypeFn parallel_type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, parallel_env);
  };

  // 4. Type the body block
  const auto body_info = TypeBlockInfo(ctx, parallel_ctx, *expr.body, parallel_env,
                                       parallel_type_expr, parallel_type_ident,
                                       parallel_type_place, &parallel_env);
  if (!body_info.ok) {
    result.diag_id = body_info.diag_id;
    result.diag_detail = body_info.diag_detail;
    result.diag_span = body_info.diag_span;
    return result;
  }

  // GPU domains have additional capture restrictions.
  if (gpu_domain) {
    const auto capture_sets = *AnalyzeBlockCaptureSets(*expr.body, env);
    for (const auto& captured_name : capture_sets.captures) {
      const auto binding = BindOf(env, captured_name);
      if (!binding.has_value()) {
        continue;
      }
      if (PermOfType(binding->type) == Permission::Shared) {
        result.diag_id = "E-CON-0151";
        return result;
      }
      if (BindingHasHeapProvenance(*binding)) {
        SPEC_RULE("GpuCapture-HeapProv-Err");
        result.diag_id = "E-CON-0150";
        return result;
      }
      if (const auto gpu_diag = GpuSafeDiagForType(ctx, binding->type);
          gpu_diag.has_value()) {
        SPEC_RULE("GpuCapture-NonGpuSafe-Err");
        EmitSupplementalTypeDiag(type_ctx, *gpu_diag);
        result.diag_id = "E-CON-0153";
        return result;
      }
    }
  }

  // A non-collectable tail expression makes the parallel expression result
  // equal to the block body type, matching T-Parallel and BlockInfo-Tail.
  const bool explicit_result =
      expr.body->tail_opt && !IsCollectableParallelExpr(expr.body->tail_opt);
  if (explicit_result) {
    SPEC_RULE("T-Parallel");
    result.ok = true;
    result.type = body_info.type;
    return result;
  }

  // 5. Determine completion result type from collected spawn/dispatch work.
  SPEC_RULE("ForkJoin");
  ParallelResultCollect collected;
  for (const auto& stmt : expr.body->stmts) {
    collected = CollectParallelResultStmt(parallel_type_expr, stmt, std::move(collected));
    if (!collected.ok) {
      result.diag_id = collected.diag_id;
      return result;
    }
  }
  if (expr.body->tail_opt) {
    collected = CollectParallelResultExpr(parallel_type_expr, expr.body->tail_opt,
                                          std::move(collected));
    if (!collected.ok) {
      result.diag_id = collected.diag_id;
      return result;
    }
  }

  if (collected.types.empty()) {
    // No collectable work result.
    SPEC_RULE("T-Parallel-Empty");
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }

  if (collected.types.size() == 1) {
    // Single collectable result.
    SPEC_RULE("T-Parallel-Single");
    result.ok = true;
    result.type = collected.types.front();
    return result;
  }

  // Multiple collectable results become a tuple in enqueue order.
  SPEC_RULE("T-Parallel-Multi");
  result.ok = true;
  result.type = MakeTypeTuple(std::move(collected.types));

  SPEC_RULE("T-Parallel");
  return result;
}

}  // namespace ultraviolet::analysis
