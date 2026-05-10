// =============================================================================
// Key Block Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 17 (Key Blocks and Key Semantics)
//
// This lowering emits explicit runtime key operations so dynamic behavior
// matches key block scope rules and integrates with cleanup for all control
// flow exits.
//
// =============================================================================

#include "05_codegen/lower/stmt/key_block_stmt.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {
namespace {

IRValue StringImmediate(std::string_view text) {
  IRValue value;
  value.kind = IRValue::Kind::Immediate;
  value.name = "\"" + std::string(text) + "\"";
  value.bytes.assign(text.begin(), text.end());
  return value;
}

IRValue U8Immediate(std::uint8_t value) {
  IRValue out;
  out.kind = IRValue::Kind::Immediate;
  out.name = std::to_string(value);
  out.bytes = {value};
  return out;
}

std::string EncodeIndexSegment(const ast::ExprPtr& expr) {
  if (!expr) {
    return "?";
  }
  return FormatIndexExpr(*expr);
}

bool RuntimeKeyIndexIsStatic(const ast::ExprPtr& expr) {
  const auto constant = analysis::EvaluateConstant(expr);
  return constant.known;
}

std::string EncodeKeyPath(const ast::KeyPathExpr& path) {
  std::string encoded = path.root;
  for (const auto& seg : path.segs) {
    encoded += ".";
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      encoded += "f:";
      encoded += field->name;
      if (field->marked) {
        break;
      }
      continue;
    }
    if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      if (!RuntimeKeyIndexIsStatic(index->expr)) {
        break;
      }
      encoded += "i:";
      encoded += EncodeIndexSegment(index->expr);
      if (index->marked) {
        break;
      }
      continue;
    }
  }
  return encoded;
}

std::vector<std::string> CanonicalizeKeyPaths(const ast::KeyBlockStmt& stmt) {
  std::vector<std::string> paths;
  paths.reserve(stmt.paths.size());
  for (const auto& path : stmt.paths) {
    paths.push_back(EncodeKeyPath(path));
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

static ast::KeyMode ModeOf(const std::optional<ast::KeyMode>& mode_opt) {
  return mode_opt.value_or(ast::KeyMode::Read);
}

static std::vector<std::string> LowerKeyPaths(
    const std::vector<ast::KeyPathExpr>& paths) {
  std::vector<std::string> lowered;
  lowered.reserve(paths.size());
  for (const auto& path : paths) {
    lowered.push_back(EncodeKeyPath(path));
  }
  return lowered;
}

struct HeldOuterKey {
  std::string scope_name;
  std::string path;
  std::uint8_t mode = 0;
};

std::vector<HeldOuterKey> EnclosingHeldKeysForPaths(
    const std::vector<std::string>& encoded_paths,
    const LowerCtx& ctx) {
  std::vector<HeldOuterKey> held;
  for (const auto& encoded_path : encoded_paths) {
    for (auto scope_it = ctx.active_key_scopes.rbegin();
         scope_it != ctx.active_key_scopes.rend(); ++scope_it) {
      const auto path_it = std::find_if(
          scope_it->acquired_paths.begin(),
          scope_it->acquired_paths.end(),
          [&](const auto& candidate) {
            return candidate.encoded_path == encoded_path;
          });
      if (path_it == scope_it->acquired_paths.end()) {
        continue;
      }
      held.push_back(
          HeldOuterKey{scope_it->scope_name, path_it->encoded_path, path_it->mode});
      break;
    }
  }
  return held;
}

static IRPtr LowerConflictChecks(const std::vector<std::string>& lowered_paths,
                                 ast::KeyMode mode,
                                 LowerCtx& ctx) {
  std::vector<IRPtr> parts;
  const std::uint8_t key_mode = mode == ast::KeyMode::Write ? 1u : 0u;
  auto sorted = lowered_paths;
  std::sort(sorted.begin(), sorted.end());
  parts.reserve(sorted.size());
  for (const auto& path : sorted) {
    IRCall check;
    check.callee.kind = IRValue::Kind::Symbol;
    check.callee.name = ConcurrencySymKeyCheckConflict();
    check.args.push_back(StringImmediate(path));
    check.args.push_back(U8Immediate(key_mode));
    check.result = ctx.FreshTempValue("key_conflict_check");
    ctx.RegisterValueType(check.result, analysis::MakeTypePrim("()"));
    parts.push_back(MakeIR(std::move(check)));
  }
  return SeqIR(std::move(parts));
}

}  // namespace

IRPtr LowerKeyBlockStmt(const ast::KeyBlockStmt& stmt, LowerCtx& ctx) {
  const bool has_speculative_mod =
      std::find(stmt.mods.begin(), stmt.mods.end(),
                ast::KeyBlockMod::Speculative) != stmt.mods.end();
  const bool has_release_mod =
      std::find(stmt.mods.begin(), stmt.mods.end(), ast::KeyBlockMod::Release) !=
      stmt.mods.end();
  SPEC_RULE(has_speculative_mod
                ? "Lower-Stmt-KeyBlock-Speculative"
                : (has_release_mod ? "Lower-Stmt-KeyBlock-Release"
                                   : "Lower-Stmt-KeyBlock"));

  if (!stmt.body) {
    return EmptyIR();
  }

  if (has_speculative_mod) {
    const auto encoded_paths = LowerKeyPaths(stmt.paths);
    std::vector<std::pair<analysis::KeyPath, std::string>> sorted_paths;
    sorted_paths.reserve(stmt.paths.size());
    for (std::size_t i = 0; i < stmt.paths.size(); ++i) {
      sorted_paths.emplace_back(analysis::ParseKeyPathSpec(stmt.paths[i]),
                                encoded_paths[i]);
    }
    std::sort(sorted_paths.begin(), sorted_paths.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.second < rhs.second;
              });

    ActiveKeyScopeInfo speculative_scope;
    speculative_scope.scope_runtime_id = ctx.CurrentRuntimeScopeId().value_or(0);
    speculative_scope.scope_name = "__c0_speculative";
    speculative_scope.implicit = true;
    for (const auto& [analysis_path, encoded_path] : sorted_paths) {
      speculative_scope.acquired_paths.push_back(
          ActiveKeyPathInfo{analysis_path, encoded_path, 1u});
    }
    ctx.active_key_scopes.push_back(speculative_scope);

    const auto prev_order = ctx.current_access_order;
    if (const auto order = MemoryOrderFromAttrs(stmt.attrs)) {
      ctx.current_access_order = *order;
    }
    auto body_result = LowerBlock(*stmt.body, ctx);
    ctx.current_access_order = prev_order;
    ctx.active_key_scopes.pop_back();

    IRSpecSnapshot snapshot;
    for (const auto& [_, encoded_path] : sorted_paths) {
      snapshot.paths.push_back(encoded_path);
    }
    snapshot.result = ctx.FreshTempValue("spec_snapshot");
    ctx.RegisterValueType(snapshot.result, analysis::MakeTypePrim("()"));

    IRSpecValidate validate;
    validate.paths = snapshot.paths;
    validate.result = ctx.FreshTempValue("spec_validate");
    ctx.RegisterValueType(validate.result, analysis::MakeTypePrim("bool"));

    IRSpecCommit commit;
    commit.paths = snapshot.paths;
    commit.value = body_result.value;
    commit.result = ctx.FreshTempValue("spec_commit");
    if (ctx.expr_type && stmt.body->tail_opt) {
      ctx.RegisterValueType(commit.result, ctx.expr_type(*stmt.body->tail_opt));
    } else {
      ctx.RegisterValueType(commit.result, analysis::MakeTypePrim("()"));
    }

    IRSpecRetry retry;
    retry.result = ctx.FreshTempValue("spec_retry");
    ctx.RegisterValueType(retry.result, analysis::MakeTypePrim("()"));

    ast::KeyBlockStmt fallback_stmt = stmt;
    fallback_stmt.mods.erase(
        std::remove(fallback_stmt.mods.begin(), fallback_stmt.mods.end(),
                    ast::KeyBlockMod::Speculative),
        fallback_stmt.mods.end());
    fallback_stmt.mode = ast::KeyMode::Write;
    IRPtr fallback_ir = LowerKeyBlockStmt(fallback_stmt, ctx);
    IRValue fallback_value = body_result.value;
    if (fallback_ir) {
      if (const auto* fallback_block = std::get_if<IRBlock>(&fallback_ir->node)) {
        fallback_value = fallback_block->value;
      }
    }

    IRSpecFallback fallback;
    fallback.body = fallback_ir;
    fallback.result = fallback_value;

    IRSpecLoop loop;
    loop.snapshot_ir = MakeIR(std::move(snapshot));
    loop.body_ir = body_result.ir;
    loop.validate_ir = MakeIR(std::move(validate));
    loop.commit_ir = MakeIR(std::move(commit));
    loop.retry_ir = MakeIR(std::move(retry));
    loop.fallback_ir = MakeIR(std::move(fallback));
    loop.result = fallback_value;
    return MakeIR(std::move(loop));
  }

  // Track key-scope lifetime with the standard cleanup stack so key release
  // happens on normal and non-local exits (return/break/continue/panic).
  ctx.PushScope(false, false);

  const analysis::TypeRef key_scope_type = analysis::MakeTypeRawPtr(
      analysis::RawPtrQual::Mut,
      analysis::MakeTypePrim("u8"));
  const analysis::TypeRef unit_type = analysis::MakeTypePrim("()");
  const auto encoded_paths = LowerKeyPaths(stmt.paths);
  const auto outer_keys =
      has_release_mod ? EnclosingHeldKeysForPaths(encoded_paths, ctx)
                      : std::vector<HeldOuterKey>{};

  std::vector<IRPtr> setup_parts;

  if (has_release_mod) {
    for (auto it = outer_keys.rbegin(); it != outer_keys.rend(); ++it) {
      IRValue outer_scope_local;
      outer_scope_local.kind = IRValue::Kind::Local;
      outer_scope_local.name = it->scope_name;
      ctx.RegisterValueType(outer_scope_local, key_scope_type);

      IRCall release;
      release.callee.kind = IRValue::Kind::Symbol;
      release.callee.name = ConcurrencySymKeyReleaseOne();
      release.args.push_back(outer_scope_local);
      release.args.push_back(StringImmediate(it->path));
      release.result = ctx.FreshTempValue("key_release_one");
      ctx.RegisterValueType(release.result, key_scope_type);
      IRValue released_handle = release.result;
      setup_parts.push_back(MakeIR(std::move(release)));

      const std::string handle_local_name =
          ctx.FreshTempValue("__c0_released_key").name;
      IRBindVar bind_handle;
      bind_handle.name = handle_local_name;
      bind_handle.value = released_handle;
      bind_handle.type = key_scope_type;
      setup_parts.push_back(MakeIR(std::move(bind_handle)));

      ctx.RegisterVar(handle_local_name,
                      key_scope_type,
                      /*has_responsibility=*/false,
                      /*is_immovable=*/true,
                      analysis::ProvenanceKind::Bottom);
      ctx.RegisterReleasedKeyReacquire(handle_local_name);
    }
  }

  IRCall key_scope_enter;
  key_scope_enter.callee.kind = IRValue::Kind::Symbol;
  key_scope_enter.callee.name = ConcurrencySymKeyScopeEnter();
  key_scope_enter.result = ctx.FreshTempValue("key_scope_enter");
  ctx.RegisterValueType(key_scope_enter.result, key_scope_type);
  IRValue key_scope_value = key_scope_enter.result;
  setup_parts.push_back(MakeIR(std::move(key_scope_enter)));

  const std::string scope_local_name =
      ctx.FreshTempValue("__c0_key_scope").name;
  IRBindVar scope_bind;
  scope_bind.name = scope_local_name;
  scope_bind.value = key_scope_value;
  scope_bind.type = key_scope_type;
  setup_parts.push_back(MakeIR(std::move(scope_bind)));

  ctx.RegisterVar(scope_local_name,
                  key_scope_type,
                  /*has_responsibility=*/false,
                  /*is_immovable=*/true,
                  analysis::ProvenanceKind::Bottom);

  IRValue scope_local;
  scope_local.kind = IRValue::Kind::Local;
  scope_local.name = scope_local_name;
  ctx.RegisterValueType(scope_local, key_scope_type);

  ctx.RegisterKeyScopeExit(scope_local_name);
  ActiveKeyScopeInfo active_scope;
  active_scope.scope_runtime_id = ctx.CurrentRuntimeScopeId().value_or(0);
  active_scope.scope_name = scope_local_name;

  const ast::KeyMode mode = ModeOf(stmt.mode);
  const std::uint8_t key_mode = mode == ast::KeyMode::Write ? 1u : 0u;
  setup_parts.push_back(LowerConflictChecks(encoded_paths, mode, ctx));
  std::vector<std::pair<analysis::KeyPath, std::string>> sorted_paths;
  sorted_paths.reserve(stmt.paths.size());
  for (std::size_t i = 0; i < stmt.paths.size(); ++i) {
    sorted_paths.emplace_back(analysis::ParseKeyPathSpec(stmt.paths[i]),
                              encoded_paths[i]);
  }
  std::sort(sorted_paths.begin(), sorted_paths.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.second < rhs.second;
            });
  for (const auto& [analysis_path, encoded_path] : sorted_paths) {
    IRCall acquire;
    acquire.callee.kind = IRValue::Kind::Symbol;
    acquire.callee.name = ConcurrencySymKeyAcquire();
    acquire.args.push_back(scope_local);
    acquire.args.push_back(StringImmediate(encoded_path));
    acquire.args.push_back(U8Immediate(key_mode));
    acquire.result = ctx.FreshTempValue("key_acquire");
    ctx.RegisterValueType(acquire.result, unit_type);
    setup_parts.push_back(MakeIR(std::move(acquire)));
    active_scope.acquired_paths.push_back(
        ActiveKeyPathInfo{analysis_path, encoded_path, key_mode});
  }

  ctx.active_key_scopes.push_back(std::move(active_scope));

  const auto prev_order = ctx.current_access_order;
  if (const auto order = MemoryOrderFromAttrs(stmt.attrs)) {
    ctx.current_access_order = *order;
  }
  auto body_result = LowerBlock(*stmt.body, ctx);
  ctx.current_access_order = prev_order;

  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  CleanupPlan remainder =
      ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, ctx);
  IRPtr cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, ctx);
  ctx.PopScope();

  if (ctx.temp_sink) {
    analysis::TypeRef result_type;
    if (stmt.body->tail_opt && ctx.expr_type) {
      result_type = ctx.expr_type(*stmt.body->tail_opt);
    } else if (!stmt.body->tail_opt) {
      result_type = analysis::MakeTypePrim("()");
    }
    ctx.RegisterTempValue(body_result.value, result_type);
  }

  IRBlock block_ir;
  block_ir.setup = SeqIR(std::move(setup_parts));
  block_ir.body = SeqIR({body_result.ir, cleanup_ir});
  block_ir.value = body_result.value;
  return MakeIR(std::move(block_ir));
}

}  // namespace cursive::codegen
