// =============================================================================
// Key Block Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 17 (Key Blocks and Key Semantics)
//
// This lowering emits explicit runtime key operations so dynamic behavior
// matches key block scope rules and integrates with cleanup for all control
// flow exits.
//
// =============================================================================

#include "05_codegen/lower/stmt/key_block_stmt.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {
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

const char* BoolText(bool value) {
  return value ? "true" : "false";
}

const char* KeyModeName(ast::KeyMode mode) {
  return mode == ast::KeyMode::Write ? "write" : "read";
}

std::string_view AccessOrderingName(AccessOrdering order) {
  switch (order) {
    case AccessOrdering::Relaxed:
      return "relaxed";
    case AccessOrdering::Acquire:
      return "acquire";
    case AccessOrdering::Release:
      return "release";
    case AccessOrdering::AcqRel:
      return "acqrel";
    case AccessOrdering::SeqCst:
      return "seqcst";
  }
  return "seqcst";
}

bool KeyPathHasDynamicRuntimeIndex(const ast::KeyPathExpr& path) {
  for (const auto& seg : path.segs) {
    const auto* index = std::get_if<ast::KeySegIndex>(&seg);
    if (index && !RuntimeKeyIndexIsStatic(index->expr)) {
      return true;
    }
  }
  return false;
}

bool KeyPathHasMarkedBoundary(const ast::KeyPathExpr& path) {
  for (const auto& seg : path.segs) {
    bool marked = false;
    std::visit([&](const auto& node) { marked = node.marked; }, seg);
    if (marked) {
      return true;
    }
  }
  return false;
}

bool AnyDynamicRuntimeKeyPath(const std::vector<ast::KeyPathExpr>& paths) {
  return std::any_of(paths.begin(), paths.end(), KeyPathHasDynamicRuntimeIndex);
}

using LoweredKeyPath = std::pair<analysis::KeyPath, std::string>;

bool LoweredKeyPathLess(const LoweredKeyPath& lhs,
                        const LoweredKeyPath& rhs) {
  return analysis::KeyPathLess(lhs.first, rhs.first);
}

bool LoweredKeyPathEquivalent(const LoweredKeyPath& lhs,
                              const LoweredKeyPath& rhs) {
  return !analysis::KeyPathLess(lhs.first, rhs.first) &&
         !analysis::KeyPathLess(rhs.first, lhs.first);
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
  std::vector<LoweredKeyPath> lowered_paths;
  lowered_paths.reserve(stmt.paths.size());
  for (const auto& path : stmt.paths) {
    lowered_paths.emplace_back(analysis::ParseKeyPathSpec(path),
                               EncodeKeyPath(path));
  }
  std::sort(lowered_paths.begin(), lowered_paths.end(), LoweredKeyPathLess);
  lowered_paths.erase(
      std::unique(lowered_paths.begin(),
                  lowered_paths.end(),
                  LoweredKeyPathEquivalent),
      lowered_paths.end());

  std::vector<std::string> paths;
  paths.reserve(lowered_paths.size());
  for (auto& [_, encoded_path] : lowered_paths) {
    paths.push_back(std::move(encoded_path));
  }
  return paths;
}

static std::vector<std::string> LowerKeyPaths(
    const std::vector<ast::KeyPathExpr>& paths) {
  std::vector<std::string> lowered;
  lowered.reserve(paths.size());
  core::Conformance::Record(
      "def.19.KeyLoweringForms",
      std::nullopt,
      "source=LowerKeyPaths;forms=LowerKeyPath,LowerKeyAccess;"
      "key_ir=AcquireKey,ReleaseKey,CheckConflict,FenceIR");
  std::function<void(std::size_t)> lower_from = [&](std::size_t index) {
    if (index == paths.size()) {
      core::Conformance::Record(
          "def.19.LowerKeyPathsEmpty",
          std::nullopt,
          "source=LowerKeyPaths;branch=empty;remaining=0");
      return;
    }

    std::string encoded = EncodeKeyPath(paths[index]);
    std::string payload =
        "source=LowerKeyPaths;branch=cons;index=" +
        std::to_string(index) +
        ";path_count=" +
        std::to_string(paths.size()) +
        ";dynamic_index=" +
        BoolText(KeyPathHasDynamicRuntimeIndex(paths[index])) +
        ";marked_boundary=" +
        BoolText(KeyPathHasMarkedBoundary(paths[index])) +
        ";encoded_path=" +
        encoded;
    core::Conformance::Record(
        "def.19.LowerKeyPathsCons",
        paths[index].span,
        payload);
    core::Conformance::Record(
        "rule.19.Lower-KeyPath",
        paths[index].span,
        "source=LowerKeyPaths;branch=cons;encoded_path=" + encoded);
    if (KeyPathHasMarkedBoundary(paths[index])) {
      core::Conformance::Record(
          "requirement.19.KeyCoarseningInlineMarker",
          paths[index].span,
          "source=LowerKeyPaths;marked_boundary=true;"
          "acquisition_granularity=marked_position;"
          "covers_subsequent_segments=true;encoded_path=" +
              encoded);
    }
    lowered.push_back(std::move(encoded));
    lower_from(index + 1u);
  };
  lower_from(0u);
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

IRPtr MakeConflictCheck(std::string_view path,
                        ast::KeyMode mode,
                        LowerCtx& ctx) {
  const std::uint8_t key_mode = mode == ast::KeyMode::Write ? 1u : 0u;
  IRCall check;
  check.callee.kind = IRValue::Kind::Symbol;
  check.callee.name = ConcurrencySymKeyCheckConflict();
  check.args.push_back(StringImmediate(path));
  check.args.push_back(U8Immediate(key_mode));
  check.result = ctx.FreshTempValue("key_conflict_check");
  ctx.RegisterValueType(check.result, analysis::MakeTypePrim("()"));
  return MakeIR(std::move(check));
}

void RecordLowerConflictChecksConformance(
    const std::vector<std::string>& lowered_paths,
    ast::KeyMode mode,
    const std::optional<core::Span>& span) {
  std::string payload =
      "source=LowerConflictChecks;lower_key_paths=true;canonical_sort=true;"
      "operation=CheckConflict;mode=";
  payload += KeyModeName(mode);
  payload += ";path_count=";
  payload += std::to_string(lowered_paths.size());
  core::Conformance::Record(
      "requirement.19.KeyConflictRuntimeCompatibility",
      span,
      "source=LowerConflictChecks;runtime_relation=KeyModeCompatible,"
      "KeysOverlap,KeyConflict;operation=CheckConflict;mode=" +
          std::string(KeyModeName(mode)) +
          ";path_count=" +
          std::to_string(lowered_paths.size()));
  core::Conformance::Record("def.19.LowerConflictChecks", span, payload);
  core::Conformance::Record("rule.19.Lower-Key-ConflictChecks", span, payload);
}

[[maybe_unused]] static IRPtr LowerConflictChecks(
    const std::vector<std::string>& lowered_paths,
    ast::KeyMode mode,
    LowerCtx& ctx) {
  auto sorted = lowered_paths;
  std::sort(sorted.begin(), sorted.end());
  RecordLowerConflictChecksConformance(
      lowered_paths,
      mode,
      std::nullopt);
  std::vector<IRPtr> parts;
  parts.reserve(sorted.size());
  for (const auto& path : sorted) {
    parts.push_back(MakeConflictCheck(path, mode, ctx));
  }
  return SeqIR(std::move(parts));
}

struct SpeculativeRollbackIR {
  IRPtr setup_ir;
  IRPtr restore_ir;
};

ast::ExprPtr MakeRootPlaceExpr(const ast::KeyPathExpr& path) {
  ast::IdentifierExpr ident;
  ident.name = path.root;
  return std::make_shared<ast::Expr>(
      ast::Expr{path.span, ast::ExprNode{std::move(ident)}});
}

analysis::TypeRef LookupRootStaticType(std::string_view root, LowerCtx& ctx) {
  if (!ctx.sigma) {
    return nullptr;
  }

  std::vector<std::string> full;
  std::string resolved_name(root);
  if (ctx.resolve_name) {
    auto resolved = ctx.resolve_name(std::string(root));
    if (resolved.has_value() && !resolved->empty()) {
      full = *resolved;
      resolved_name = full.back();
      full.pop_back();
    }
  }
  if (full.empty()) {
    full = ctx.module_path;
  }

  const std::string sym = StaticSymPath(*ctx.sigma, full, resolved_name);
  if (analysis::TypeRef static_type = ctx.LookupStaticType(sym)) {
    return static_type;
  }
  if (auto bind_info = StaticBindInfo(*ctx.sigma, full, resolved_name)) {
    return bind_info->type;
  }
  return nullptr;
}

analysis::TypeRef LookupRollbackRootType(const ast::KeyPathExpr& path,
                                         const IRValue& read_value,
                                         LowerCtx& ctx) {
  if (analysis::TypeRef value_type = ctx.LookupValueType(read_value)) {
    return value_type;
  }
  if (const BindingState* binding = ctx.GetBindingState(path.root)) {
    return binding->type;
  }
  if (const CaptureAccess* capture = ctx.LookupCapture(path.root)) {
    return capture->value_type;
  }
  return LookupRootStaticType(path.root, ctx);
}

IRPtr LowerSpeculativeRollbackCaptureStore(const std::string& name,
                                           const IRValue& snapshot_value,
                                           LowerCtx& ctx) {
  const CaptureAccess* capture = ctx.LookupCapture(name);
  if (!capture) {
    ctx.ReportCodegenFailure();
    return EmptyIR();
  }

  IRValue field_ptr = ctx.CaptureFieldPtr(*capture);
  if (capture->by_ref) {
    IRValue captured_ptr = ctx.FreshTempValue("spec_rollback_capture_ptr");
    IRReadPtr load_ptr;
    load_ptr.ptr = field_ptr;
    load_ptr.result = captured_ptr;
    ctx.RegisterValueType(
        captured_ptr,
        analysis::MakeTypePtr(capture->value_type, analysis::PtrState::Valid));

    IRWritePtr write;
    write.ptr = captured_ptr;
    write.value = snapshot_value;
    return SeqIR({MakeIR(std::move(load_ptr)), MakeIR(std::move(write))});
  }

  IRWritePtr write;
  write.ptr = field_ptr;
  write.value = snapshot_value;
  return MakeIR(std::move(write));
}

IRPtr LowerSpeculativeRollbackStaticStore(
    const std::vector<std::string>& static_path,
    const std::string& static_name,
    const IRValue& snapshot_value,
    LowerCtx& ctx) {
  if (static_path.empty() || static_name.empty()) {
    ctx.ReportCodegenFailure();
    return EmptyIR();
  }

  IRStoreGlobal store;
  store.symbol = ctx.sigma ? StaticSymPath(*ctx.sigma, static_path, static_name)
                           : StaticSymPath(static_path, static_name);
  store.value = snapshot_value;
  return MakeIR(std::move(store));
}

IRPtr LowerSpeculativeRollbackStaticStore(const ast::KeyPathExpr& path,
                                          const IRValue& snapshot_value,
                                          LowerCtx& ctx) {
  std::vector<std::string> full;
  std::string resolved_name(path.root);
  if (ctx.resolve_name) {
    auto resolved = ctx.resolve_name(path.root);
    if (resolved.has_value() && !resolved->empty()) {
      full = *resolved;
      resolved_name = full.back();
      full.pop_back();
    }
  }
  if (full.empty()) {
    full = ctx.module_path;
  }

  return LowerSpeculativeRollbackStaticStore(
      full,
      resolved_name,
      snapshot_value,
      ctx);
}

IRPtr LowerSpeculativeRollbackRootStore(const ast::KeyPathExpr& path,
                                        const IRValue& snapshot_value,
                                        LowerCtx& ctx) {
  if (auto alias = ctx.LookupLocalAddrAlias(path.root)) {
    switch (alias->kind) {
      case LocalAddrAlias::Kind::Binding: {
        const BindingState* state =
            ctx.GetBindingStateById(alias->binding_name, alias->binding_id);
        if (!state) {
          ctx.ReportCodegenFailure();
          return EmptyIR();
        }
        IRStoreVarNoDrop store;
        store.name = state->stable_name.empty() ? alias->binding_name
                                                : state->stable_name;
        store.value = snapshot_value;
        return MakeIR(std::move(store));
      }
      case LocalAddrAlias::Kind::Capture:
        return LowerSpeculativeRollbackCaptureStore(
            alias->capture_name,
            snapshot_value,
            ctx);
      case LocalAddrAlias::Kind::Static:
        return LowerSpeculativeRollbackStaticStore(
            alias->static_path,
            alias->static_name,
            snapshot_value,
            ctx);
    }
  }

  if (const BindingState* state = ctx.GetBindingState(path.root)) {
    IRStoreVarNoDrop store;
    store.name = state->stable_name.empty() ? path.root : state->stable_name;
    store.value = snapshot_value;
    return MakeIR(std::move(store));
  }

  if (ctx.LookupCapture(path.root)) {
    return LowerSpeculativeRollbackCaptureStore(path.root, snapshot_value, ctx);
  }

  return LowerSpeculativeRollbackStaticStore(path, snapshot_value, ctx);
}

std::vector<const ast::KeyPathExpr*> UniqueRollbackRoots(
    const std::vector<ast::KeyPathExpr>& paths) {
  std::vector<const ast::KeyPathExpr*> roots;
  std::unordered_set<std::string> seen;
  for (const auto& path : paths) {
    if (!seen.insert(path.root).second) {
      continue;
    }
    roots.push_back(&path);
  }
  std::sort(roots.begin(),
            roots.end(),
            [](const ast::KeyPathExpr* lhs, const ast::KeyPathExpr* rhs) {
              return lhs->root < rhs->root;
            });
  return roots;
}

SpeculativeRollbackIR LowerSpeculativeRollbackIR(
    const std::vector<ast::KeyPathExpr>& paths,
    LowerCtx& ctx) {
  std::vector<IRPtr> setup_parts;
  std::vector<IRPtr> restore_parts;

  for (const ast::KeyPathExpr* path : UniqueRollbackRoots(paths)) {
    if (!path) {
      continue;
    }

    ast::ExprPtr root_place = MakeRootPlaceExpr(*path);
    LowerResult read_result = LowerReadPlace(*root_place, ctx);
    analysis::TypeRef root_type =
        LookupRollbackRootType(*path, read_result.value, ctx);
    if (!root_type) {
      ctx.ReportCodegenFailure();
      continue;
    }

    const std::string snapshot_name =
        ctx.FreshTempValue("__uv_spec_snapshot").name;
    ctx.RegisterVar(snapshot_name,
                    root_type,
                    /*has_responsibility=*/false,
                    /*is_immovable=*/true,
                    analysis::ProvenanceKind::Bottom);

    IRBindVar bind_snapshot;
    bind_snapshot.name = snapshot_name;
    bind_snapshot.stable_name = ctx.StableBindingName(snapshot_name);
    bind_snapshot.value = read_result.value;
    bind_snapshot.type = root_type;

    IRValue snapshot_value;
    snapshot_value.kind = IRValue::Kind::Local;
    snapshot_value.name = bind_snapshot.stable_name.empty()
                              ? snapshot_name
                              : bind_snapshot.stable_name;
    ctx.RegisterValueType(snapshot_value, root_type);

    setup_parts.push_back(read_result.ir);
    setup_parts.push_back(MakeIR(std::move(bind_snapshot)));
    restore_parts.push_back(
        LowerSpeculativeRollbackRootStore(*path, snapshot_value, ctx));
  }

  return SpeculativeRollbackIR{
      SeqIR(std::move(setup_parts)),
      SeqIR(std::move(restore_parts)),
  };
}

}  // namespace

IRPtr LowerKeyBlockStmtImpl(
    const ast::KeyBlockStmt& stmt,
    LowerCtx& ctx,
    const std::vector<ast::KeyPathExpr>* speculative_rollback_paths) {
  const bool has_speculative_mod =
      stmt.kind == ast::KeyBlockKind::SpeculativeWrite;
  const bool has_release_mod = stmt.kind == ast::KeyBlockKind::Release;
  SPEC_RULE(has_speculative_mod
                ? "Lower-Stmt-KeyBlock-Speculative"
                : (has_release_mod ? "Lower-Stmt-KeyBlock-Release"
                                   : "Lower-Stmt-KeyBlock"));
  SPEC_RULE(has_speculative_mod
                ? "rule.19.Lower-Stmt-KeyBlock-Speculative"
                : (has_release_mod ? "rule.19.Lower-Stmt-KeyBlock-Release"
                                   : "rule.19.Lower-Stmt-KeyBlock"));

  if (!stmt.body) {
    return EmptyIR();
  }

  const AccessOrdering effective_default_order =
      MemoryOrderFromAttrs(stmt.attrs).value_or(AccessOrdering::SeqCst);
  core::Conformance::Record(
      "requirement.19.MemoryOrderingDefaultsAndKeySemantics",
      stmt.span,
      "source=LowerKeyBlockStmt;default_access_order=" +
          std::string(AccessOrderingName(effective_default_order)) +
          ";key_acquire_order=acquire;key_release_order=release;"
          "key_semantics_unchanged=true");
  if (MemoryOrderFromAttrs(stmt.attrs).has_value()) {
    core::Conformance::Record(
        "requirement.19.MemoryOrderAttributeAttachment",
        stmt.span,
        "source=LowerKeyBlockStmt;attachment=key_block;default_order=" +
            std::string(AccessOrderingName(effective_default_order)));
  }

  if (has_speculative_mod) {
    const auto encoded_paths = LowerKeyPaths(stmt.paths);
    std::vector<LoweredKeyPath> sorted_paths;
    sorted_paths.reserve(stmt.paths.size());
    for (std::size_t i = 0; i < stmt.paths.size(); ++i) {
      sorted_paths.emplace_back(analysis::ParseKeyPathSpec(stmt.paths[i]),
                                encoded_paths[i]);
    }
    std::sort(sorted_paths.begin(), sorted_paths.end(), LoweredKeyPathLess);
    core::Conformance::Record(
        "def.19.SpeculativeIR",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=speculative;has_snapshot=true;"
        "has_validate=true;has_commit=true;has_retry=true;has_fallback=true;"
        "fallback_conflict_acquire_release=true;path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "rule.19.Lower-Stmt-KeyBlock-Speculative",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=SpeculativeWrite;lower_key_paths=true;"
        "canonical_sort=true;spec_loop=true;fallback_write_key_block=true;"
        "path_count=" +
            std::to_string(sorted_paths.size()));

    ActiveKeyScopeInfo speculative_scope;
    speculative_scope.scope_runtime_id = ctx.CurrentRuntimeScopeId().value_or(0);
    speculative_scope.scope_name = "__uv_speculative";
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
    fallback_stmt.kind = ast::KeyBlockKind::Write;
    fallback_stmt.mode = ast::KeyMode::Write;
    IRPtr fallback_ir = LowerKeyBlockStmtImpl(fallback_stmt, ctx, &stmt.paths);
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
  const bool has_dynamic_key_path = AnyDynamicRuntimeKeyPath(stmt.paths);
  const auto outer_keys =
      has_release_mod ? EnclosingHeldKeysForPaths(encoded_paths, ctx)
                      : std::vector<HeldOuterKey>{};
  if (has_release_mod) {
    core::Conformance::Record(
        "requirement.19.NestedReleaseExecutionSequence",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=Release;release_outer=true;"
        "acquire_inner=true;release_inner=true;reacquire_outer=true;"
        "outer_key_count=" +
            std::to_string(outer_keys.size()));
    core::Conformance::Record(
        "requirement.19.NestedReleaseInterleavingWindow",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=Release;"
        "outer_release_before_inner_acquire=true;"
        "inner_release_before_outer_reacquire=true");
    core::Conformance::Record(
        "rule.19.Lower-Stmt-KeyBlock-Release",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=Release;lower_key_paths=true;"
        "canonical_sort=true;release_outer=true;acquire_inner=true;"
        "release_inner=true;reacquire_outer=true");
  }

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
          ctx.FreshTempValue("__uv_released_key").name;
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
      ctx.FreshTempValue("__uv_key_scope").name;
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

  const ast::KeyMode mode = stmt.mode;
  const std::uint8_t key_mode = mode == ast::KeyMode::Write ? 1u : 0u;
  std::vector<LoweredKeyPath> sorted_paths;
  sorted_paths.reserve(stmt.paths.size());
  for (std::size_t i = 0; i < stmt.paths.size(); ++i) {
    sorted_paths.emplace_back(analysis::ParseKeyPathSpec(stmt.paths[i]),
                              encoded_paths[i]);
  }
  std::sort(sorted_paths.begin(), sorted_paths.end(), LoweredKeyPathLess);
  RecordLowerConflictChecksConformance(
      encoded_paths,
      mode,
      stmt.span);
  if (!has_release_mod) {
    core::Conformance::Record(
        "rule.19.Lower-Stmt-KeyBlock",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=ordinary;lower_key_paths=true;"
        "canonical_sort=true;check_conflict_before_acquire=true;"
        "acquire_order=canonical;scope_exit_registered=true;path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "rule.19.K-Block-Acquire",
        stmt.span,
        "source=LowerKeyBlockStmt;kind=ordinary;new_scope=true;"
        "canonical_sort=true;acquire_mode=" +
            std::string(KeyModeName(mode)) +
            ";path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "def.19.KeyBlockRuntimeJudgments",
        stmt.span,
        "source=LowerKeyBlockStmt;judgments=AcquireKeysSigma,ReleaseKeysSigma;"
        "kind=ordinary;path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "def.19.AcquireKeysSigma",
        stmt.span,
        "source=LowerKeyBlockStmt;keys=CanonicalOrder(KeyPath(paths));"
        "acquire_lock=true;held_keys_update=true;mode=" +
            std::string(KeyModeName(mode)) +
            ";path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "requirement.19.KeyBlockCanonicalOrderReferences",
        stmt.span,
        "source=LowerKeyBlockStmt;canonical_order=true;"
        "canonical_sort=true;conflict_relation=KeyConflict;"
        "path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "requirement.19.ScopeExitKeyRelease",
        stmt.span,
        "source=LowerKeyBlockStmt;scope_exit_registered=true;"
        "release_before_scope_drop=true;exit_modes=normal,return,break,"
        "continue,panic,cancel");
    core::Conformance::Record(
        "requirement.19.KeyScopeBound",
        stmt.span,
        "source=LowerKeyBlockStmt;scope_exit_registered=true;"
        "key_dependent_access_bound_to_scope=true");
    core::Conformance::Record(
        "requirement.19.KeyAcquisitionEvaluationOrder",
        stmt.span,
        "source=LowerKeyBlockStmt;lowered_paths=true;canonical_sort=true;"
        "check_conflict_before_acquire=true;acquire_order=canonical;"
        "path_count=" +
            std::to_string(sorted_paths.size()));
  }
  if (has_dynamic_key_path) {
    core::Conformance::Record(
        "requirement.19.CanonicalOrderDynamicUse",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "coarsened_runtime_path=true;canonical_sort=true;path_count=" +
            std::to_string(sorted_paths.size()));
    core::Conformance::Record(
        "requirement.19.RuntimeSynchronizationRequirements",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "runtime_sync_emitted=true;mutual_exclusion=KeyConflict,"
        "KeyModeCompatible;blocks_until_release=true;"
        "scope_exit_release=true;eventual_progress_under_eventual_release=true");
    core::Conformance::Record(
        "requirement.19.DynamicIndexRuntimeOrdering",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "ordering_relation=canonical_coarsened_path;total=true;"
        "antisymmetric=true;transitive=true;cross_task_consistent=true;"
        "value_deterministic=true");
    core::Conformance::Record(
        "requirement.19.DynamicIndexedPathCoarsening",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "coarsened_runtime_path=true;sound_static_prefix=true;"
        "mutual_exclusion_preserved=true;observational_equivalence=true");
    core::Conformance::Record(
        "requirement.19.CanonicalOrderDeadlockFreedom",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "canonical_order=true;no_circular_wait=true;"
        "eventual_acquisition_under_eventual_release=true");
    core::Conformance::Record(
        "requirement.19.StaticAndRuntimeKeySafetyEquivalence",
        stmt.span,
        "source=LowerKeyBlockStmt;dynamic_key_path=true;"
        "statically_proven_safety_equivalent_to_runtime_sync=true;"
        "observable_behavior_preserved=true");
  }
  for (const auto& [analysis_path, encoded_path] : sorted_paths) {
    setup_parts.push_back(MakeConflictCheck(encoded_path, mode, ctx));
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

  if (speculative_rollback_paths) {
    SpeculativeRollbackIR rollback =
        LowerSpeculativeRollbackIR(*speculative_rollback_paths, ctx);
    setup_parts.push_back(rollback.setup_ir);
    ctx.RegisterSpeculativeWriteRollback(rollback.restore_ir);
  }

  const auto prev_order = ctx.current_access_order;
  if (const auto order = MemoryOrderFromAttrs(stmt.attrs)) {
    ctx.current_access_order = *order;
  }
  auto body_result = LowerBlock(*stmt.body, ctx);
  ctx.current_access_order = prev_order;
  ctx.active_key_scopes.pop_back();

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

IRPtr LowerKeyBlockStmt(const ast::KeyBlockStmt& stmt, LowerCtx& ctx) {
  return LowerKeyBlockStmtImpl(stmt, ctx, nullptr);
}

}  // namespace ultraviolet::codegen
