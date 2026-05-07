#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/keys/key_context.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

enum class ProvenanceKind;

enum class BindingProvenanceSeedKind {
  Global,
  Stack,
  Heap,
  Region,
  Bottom,
  Param,
};

enum class ParallelContextKind {
  Cpu,
  Gpu,
  Inline,
};

struct TypeBinding {
  struct ClosureCaptureInfo {
    bool captures_any = false;
    bool captures_shared = false;
    bool has_shared_deps = false;
    bool contains_spawn = false;
  };

  ast::Mutability mut = ast::Mutability::Let;
  TypeRef type;
  // Stable declared/storage type used for writes. `type` may be flow-refined
  // for reads/proofs inside a control-flow branch; `storage_type` preserves
  // the assignment contract defined by the binding itself.
  TypeRef storage_type;
  std::optional<ClosureCaptureInfo> closure_capture_info;
  bool deprecated = false;
  std::optional<std::string> deprecated_message;
  bool derived_from_shared = false;
  bool stale_ok = false;
  bool stale_after_release = false;
  std::optional<ParallelContextKind> parallel_context_kind;
  BindingProvenanceSeedKind provenance_kind =
      BindingProvenanceSeedKind::Stack;
  std::optional<IdKey> provenance_region;
};

BindingProvenanceSeedKind NormalizeBindingProvenanceSeed(
    ProvenanceKind kind);
void ApplyBindingProvenanceSeed(
    TypeBinding& binding,
    ProvenanceKind kind,
    const std::optional<std::string>& region = std::nullopt);

using TypeScope = std::unordered_map<IdKey, TypeBinding>;

struct TypeEnv {
  std::vector<TypeScope> scopes;
  std::optional<ParallelContextKind> parallel_context;
};

struct GpuCaptureCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<std::string_view> supplemental_diag_id;
};

struct Dim3ConstValue {
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::uint64_t z = 0;
};

struct ClosureCaptureSets {
  std::unordered_set<IdKey> captures;
  std::unordered_set<IdKey> const_captures;
  std::unordered_set<IdKey> shared_captures;
  std::unordered_set<IdKey> unique_captures;
};

struct StmtTypeContext;
struct StaticProofContext;

struct ParallelCaptureScopeView {
  const std::unordered_set<IdKey>* bindings = nullptr;
  std::unordered_set<IdKey>* first_child_moves = nullptr;
};

TypeEnv PushScope(const TypeEnv& env);
TypeEnv PopScope(const TypeEnv& env);
TypeEnv ProjectTypeEnvToDepth(const TypeEnv& env, std::size_t depth);
std::optional<TypeBinding> BindOf(const TypeEnv& env, std::string_view name);
TypeRef StableBindingType(const TypeBinding& binding);
std::optional<ast::Mutability> MutOf(const TypeEnv& env,
                                        std::string_view name);
std::optional<ParallelContextKind> ParallelContext(const TypeEnv& env);
bool GpuContext(const TypeEnv& env);
bool HasHeapProvenance(const TypeEnv& env, std::string_view name);
bool IsOuterParallelBinding(const StmtTypeContext& type_ctx, const IdKey& name);
bool ClaimFirstChildMove(const StmtTypeContext& type_ctx, const IdKey& name);
std::optional<Dim3ConstValue> ExtractDim3Const(const ScopeContext& ctx,
                                               const ast::ExprPtr& expr,
                                               const ExprTypeFn& type_expr);
bool ExceedsMaxWorkgroupSize(const Dim3ConstValue& dims);
GpuCaptureCheckResult CheckGpuCapture(const ScopeContext& ctx,
                                      const TypeEnv& env,
                                      std::string_view name,
                                      bool explicit_move);
std::optional<ClosureCaptureSets> AnalyzeClosureCaptureSets(
    const ast::ExprPtr& expr,
    const TypeEnv& env);
std::optional<ClosureCaptureSets> AnalyzeBlockCaptureSets(
    const ast::Block& block,
    const TypeEnv& env);
std::optional<TypeBinding::ClosureCaptureInfo> AnalyzeClosureCaptureInfo(
    const ast::ExprPtr& expr,
    const TypeEnv& env,
    const TypeRef& closure_type_hint);
bool ClosureTypeHasSharedDeps(const TypeRef& type);
std::optional<std::string_view> CheckEscapingClosureSpawn(
    const ast::ExprPtr& expr,
    const TypeEnv& env,
    const TypeRef& expected_closure_type);
bool ExprNeedsKeyAccess(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env);

struct FlowInfo {
  std::vector<TypeRef> results;
  std::vector<TypeRef> breaks;
  bool break_void = false;
};

struct StmtTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeEnv env;
  FlowInfo flow;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

struct StmtSeqResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeEnv env;
  FlowInfo flow;
  std::shared_ptr<StaticProofContext> proof_ctx;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

struct BlockInfoResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  std::vector<TypeRef> breaks;
  bool break_void = false;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

struct PatternTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<std::pair<std::string, TypeRef>> bindings;
};

// Returns Async signature for Async/alias types, or std::nullopt if not async.
std::optional<AsyncSig> AsyncSigOf(const ScopeContext& ctx, const TypeRef& type);

enum class LoopFlag {
  None,
  Loop,
};

enum class ContractPhase {
  None,
  Precondition,
  Postcondition,
};

struct HeldKeyTypingInfo {
  KeyPath path;
  ast::KeyMode mode = ast::KeyMode::Read;
};

struct StmtTypeContext {
  TypeRef return_type;
  LoopFlag loop_flag = LoopFlag::None;
  bool in_unsafe = false;
  bool ffi_export_boundary = false;
  core::DiagnosticStream* diags = nullptr;
  TypeEnv* env_ref = nullptr;
  struct OpaqueReturnState* opaque_return = nullptr;
  // C0X Extension: Structured Concurrency (§10.1)
  bool in_parallel = false;          // true when inside a parallel block
  TypeRef parallel_domain;           // domain type when in_parallel is true
  // Names introduced within the current parallel block body.
  std::unordered_set<IdKey>* parallel_bindings = nullptr;
  const std::unordered_map<IdKey, ast::ExprPtr>* loop_iteration_ranges = nullptr;
  // Union of bindings introduced by enclosing parallel block bodies.
  const std::unordered_set<IdKey>* parallel_ancestor_bindings = nullptr;
  // Ordered innermost-last stack of enclosing parallel binding scopes.
  const std::vector<ParallelCaptureScopeView>* parallel_capture_scopes = nullptr;
  // Outer unique bindings already selected by the enclosing parallel analysis
  // for one child task via explicit move capture.
  std::unordered_set<IdKey>* parallel_first_child_moves = nullptr;
  bool keys_held = false;            // true when keys are held (for wait restriction)
  std::optional<ast::KeyMode> key_mode;  // key block mode when keys_held is true
  std::vector<HeldKeyTypingInfo> held_key_paths;  // canonical held key paths for nested key checks
  std::optional<ast::KeyMode> shared_access_mode;  // RequiredMode context for shared-place access
  bool suppress_shared_access_check = false;  // true while typing a larger shared-place path
  bool in_speculative = false;       // true when inside a speculative key block
  std::unordered_set<IdKey>* unique_captures = nullptr;  // track unique captures
  // C0X Extension: Contract predicates / invariants
  ContractPhase contract_phase = ContractPhase::None;
  bool require_pure = false;
  const ast::ContractClause* contract = nullptr;
  bool contract_dynamic = false;
  bool test_postcondition_runtime = false;
  std::shared_ptr<StaticProofContext> proof_ctx;
};

inline StmtTypeContext WithSharedAccessMode(StmtTypeContext ctx,
                                            ast::KeyMode mode) {
  if (!ctx.shared_access_mode.has_value() ||
      (mode == ast::KeyMode::Write &&
       *ctx.shared_access_mode != ast::KeyMode::Write)) {
    ctx.shared_access_mode = mode;
  }
  return ctx;
}

inline StmtTypeContext SuppressSharedAccessCheck(StmtTypeContext ctx) {
  ctx.suppress_shared_access_check = true;
  return ctx;
}

StmtTypeResult TypeScopedStmtBody(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::Block& body,
                                  const TypeEnv& scoped_env,
                                  std::size_t outer_scope_depth,
                                  const ExprTypeFn& type_expr,
                                  const IdentTypeFn& type_ident,
                                  const PlaceTypeFn& type_place);

struct OpaqueReturnState {
  const ast::Type* origin = nullptr;
  TypePath class_path;
  TypeRef underlying;
};

PatternTypeResult TypePattern(const ScopeContext& ctx,
                              const ast::PatternPtr& pattern,
                              const TypeRef& expected);

// Pattern name collection utilities
void CollectPatNames(const ast::Pattern& pat, std::vector<IdKey>& out);
bool DistinctNames(const std::vector<IdKey>& names);

// Binding introduction result
struct IntroResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeEnv env;
};

// Introduce multiple bindings into the type environment
IntroResult IntroAll(const TypeEnv& env,
                     const std::vector<std::pair<std::string, TypeRef>>& binds,
                     ast::Mutability mut,
                     bool shadow);

StmtTypeResult TypeStmt(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::Stmt& stmt,
                        const TypeEnv& env,
                        const ExprTypeFn& type_expr,
                        const IdentTypeFn& type_ident,
                        const PlaceTypeFn& type_place,
                        TypeEnv* env_ref = nullptr);

StmtSeqResult TypeStmtSeq(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const std::vector<ast::Stmt>& stmts,
                          const TypeEnv& env,
                          const ExprTypeFn& type_expr,
                          const IdentTypeFn& type_ident,
                          const PlaceTypeFn& type_place,
                          TypeEnv* env_ref = nullptr);

BlockInfoResult TypeBlockInfo(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::Block& block,
                              const TypeEnv& env,
                              const ExprTypeFn& type_expr,
                              const IdentTypeFn& type_ident,
                              const PlaceTypeFn& type_place,
                              TypeEnv* env_ref = nullptr);

ExprTypeResult TypeBlock(const ScopeContext& ctx,
                         const StmtTypeContext& type_ctx,
                         const ast::Block& block,
                         const TypeEnv& env,
                         const ExprTypeFn& type_expr,
                         const IdentTypeFn& type_ident,
                         const PlaceTypeFn& type_place,
                         TypeEnv* env_ref = nullptr);

CheckResult CheckBlock(const ScopeContext& ctx,
                       const StmtTypeContext& type_ctx,
                       const ast::Block& block,
                       const TypeEnv& env,
                       const TypeRef& expected,
                       const ExprTypeFn& type_expr,
                       const IdentTypeFn& type_ident,
                       const PlaceTypeFn& type_place,
                       TypeEnv* env_ref = nullptr);

ExprTypeResult TypeBlockExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::BlockExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);

CheckResult CheckBlockExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::BlockExpr& expr,
                           const TypeEnv& env,
                           const TypeRef& expected,
                           const ExprTypeFn& type_expr,
                           const IdentTypeFn& type_ident,
                           const PlaceTypeFn& type_place);

ExprTypeResult TypeUnsafeBlockExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::UnsafeBlockExpr& expr,
                                   const TypeEnv& env,
                                   const ExprTypeFn& type_expr,
                                   const IdentTypeFn& type_ident,
                                   const PlaceTypeFn& type_place);

CheckResult CheckUnsafeBlockExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::UnsafeBlockExpr& expr,
                                 const TypeEnv& env,
                                 const TypeRef& expected,
                                 const ExprTypeFn& type_expr,
                                 const IdentTypeFn& type_ident,
                                 const PlaceTypeFn& type_place);

ExprTypeResult TypeLoopInfiniteExpr(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::LoopInfiniteExpr& expr,
                                    const TypeEnv& env,
                                    const ExprTypeFn& type_expr,
                                    const IdentTypeFn& type_ident,
                                    const PlaceTypeFn& type_place);

ExprTypeResult TypeLoopConditionalExpr(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::LoopConditionalExpr& expr,
                                       const TypeEnv& env,
                                       const ExprTypeFn& type_expr,
                                       const IdentTypeFn& type_ident,
                                       const PlaceTypeFn& type_place);

ExprTypeResult TypeLoopIterExpr(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::LoopIterExpr& expr,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place);

ExprTypeResult TypeCtLoopIterExpr(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::CtLoopIterExpr& expr,
                                  const TypeEnv& env,
                                  const ExprTypeFn& type_expr,
                                  const IdentTypeFn& type_ident,
                                  const PlaceTypeFn& type_place);

// C0X Extension: Structured Concurrency (§10, §18)

// §18.1.1 Parallel block expression typing (T-Parallel)
ExprTypeResult TypeParallelExpr(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ParallelExpr& expr,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place);

// §18.4.1 Spawn expression typing (T-Spawn)
ExprTypeResult TypeSpawnExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::SpawnExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place);

// §10.3 Wait expression typing (T-Wait)
// Note: wait implicitly consumes the handle, so accepts type_place for non-Bitcopy handles
ExprTypeResult TypeWaitExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::WaitExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const PlaceTypeFn& type_place);

// §18.5.1 Dispatch expression typing (T-Dispatch, T-Dispatch-Reduce)
ExprTypeResult TypeDispatchExpr(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::DispatchExpr& expr,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place);

// C0X Extension: Async expressions (§19)
ExprTypeResult TypeYieldExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::YieldExpr& expr,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr);

ExprTypeResult TypeYieldFromExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::YieldFromExpr& expr,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr);

ExprTypeResult TypeSyncExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::SyncExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr);

ExprTypeResult TypeRaceExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::RaceExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& type_ident,
                            const PlaceTypeFn& type_place);

ExprTypeResult TypeAllExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::AllExpr& expr,
                           const TypeEnv& env,
                           const ExprTypeFn& type_expr);

}  // namespace cursive::analysis
