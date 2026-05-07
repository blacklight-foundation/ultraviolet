#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "04_analysis/generics/monomorphize.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/symbols/linkage.h"
#include "04_analysis/keys/key_context.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// Forward declarations
struct LowerCtx;

// §6.4 LowerResult - result of lowering an expression
// Contains the IR to execute and the resulting value
struct LowerResult {
  IRPtr ir;
  IRValue value;
};

// ScopeInfo tracks variables declared in a scope for cleanup
struct CleanupItem {
  enum class Kind {
    DropBinding,
    DeferBlock,
    ReleaseRegion,
    ReleaseKeyScope,
    ReacquireReleasedKey,
    ParallelJoin,
    RuntimeScopeExit,
  };

  Kind kind = Kind::DropBinding;
  std::string name;
  IRPtr defer_ir;
  std::uint64_t scope_runtime_id = 0;
};

struct TempValue {
  IRValue value;
  analysis::TypeRef type;
};

struct ParallelCollectItem {
  IRValue value;
  bool needs_wait = false;
  analysis::TypeRef value_type;
};

struct ActiveKeyPathInfo {
  analysis::KeyPath path;
  std::string encoded_path;
  std::uint8_t mode = 0;
};

struct ActiveKeyScopeInfo {
  std::uint64_t scope_runtime_id = 0;
  std::string scope_name;
  bool implicit = false;
  std::vector<ActiveKeyPathInfo> acquired_paths;
};

enum class AccessOrdering {
  Relaxed,
  Acquire,
  Release,
  AcqRel,
  SeqCst,
};

// CaptureAccess tracks access to a captured binding inside spawn/dispatch bodies.
struct CaptureAccess {
  std::size_t index = 0;
  std::optional<std::uint64_t> byte_offset;
  analysis::TypeRef value_type;
  analysis::TypeRef field_type;
  bool by_ref = false;
};

struct CaptureEnvInfo {
  IRValue env_param;
  analysis::TypeRef env_type;
  std::unordered_map<std::string, CaptureAccess> captures;
};

struct ParallelCaptureBinding {
  std::string name;
  analysis::TypeRef type;
  bool explicit_move = false;
};

struct LoweredCaptureEnv {
  CaptureEnvInfo env_info;
  std::vector<IRPtr> ir_parts;
};

struct ScopeInfo {
  std::vector<std::string> variables;     // Variables in declaration order
  std::vector<std::string> aliases;       // Scoped local aliases
  std::vector<CleanupItem> cleanup_items; // Cleanup items in append order
  std::vector<std::string> region_tags;   // Synthetic region tags for naming
  bool is_loop = false;                   // True if this is a loop scope
  bool is_region = false;                 // True if this is a region scope
  std::uint64_t runtime_scope_id = 0;     // Runtime scope tag id (ScopeTag)
};

// BindingState tracks the state of a binding for cleanup purposes
struct BindingState {
  analysis::TypeRef type;                     // Type of the binding
  std::uint64_t binding_id = 0;
  std::string stable_name;
  bool has_responsibility = true;         // Does this binding own cleanupSigma
  bool is_immovable = false;             // Immovable binding (:=)
  bool is_moved = false;                  // Has this binding been movedSigma
  std::vector<std::string> moved_fields;  // Fields that have been moved (for partial moves)
  analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom;
  std::optional<std::string> prov_region;
  std::optional<std::string> prov_region_tag;
  std::uint64_t scope_runtime_id = 0;
  bool preserve_addr_provenance = false;
};

struct DerivedArraySegment {
  enum class Kind {
    Element,
    Repeat,
  };

  Kind kind = Kind::Element;
  IRValue value;
  std::optional<IRValue> count;
};


// DerivedValueInfo tracks how to materialize an IRValue produced without explicit IR
struct DerivedValueInfo {
  enum class Kind {
    Field,
    Tuple,
    Index,
    Slice,
    EnumPayloadIndex,
    EnumPayloadField,
    ModalField,
    UnionPayload,
    TupleLit,
    ArrayLit,
    ArraySegments,
    // Repeat arrays normalize onto ArraySegments so aggregate materialization
    // uses one implementation path.
    ArrayRepeat,
    RecordLit,
    DynLit,
    DynData,
    DynVTableDrop,
    EnumLit,
    RangeLit,
    AddrLocal,
    AddrStatic,
    AddrField,
    AddrTuple,
    AddrIndex,
    AddrDeref,
    LoadFromAddr,
  };

  Kind kind = Kind::Field;
  IRValue base;
  std::string field;
  std::size_t tuple_index = 0;
  std::size_t union_index = 0;
  std::optional<std::uint64_t> byte_offset;
  IRValue index;
  IRRange range;
  std::optional<IRValue> range_value;
  std::vector<IRValue> elements;
  std::vector<DerivedArraySegment> array_segments;
  std::vector<std::pair<std::string, IRValue>> fields;
  std::string variant;
  std::string modal_state;
  std::vector<IRValue> payload_elems;
  std::vector<std::pair<std::string, IRValue>> payload_fields;
  std::vector<std::string> static_path;
  std::string name;
  std::string vtable_sym;
  analysis::TypeRef dyn_impl_type;
  analysis::TypePath dyn_class_path;
  IRValue repeat_value;
  IRValue repeat_count;
};

struct LocalAddrAlias {
  enum class Kind {
    Binding,
    Capture,
    Static,
  };

  Kind kind = Kind::Binding;
  std::string binding_name;
  std::uint64_t binding_id = 0;
  std::string stable_name;
  std::string capture_name;
  std::vector<std::string> static_path;
  std::string static_name;
};

struct LowerValueState {
  const LowerCtx* parent = nullptr;
  std::unordered_map<std::string, analysis::TypeRef> value_types;
  std::vector<std::string>* value_type_insert_sink = nullptr;
  std::unordered_map<std::string, analysis::TypeRef> static_types;
  std::unordered_map<std::string, analysis::TypeRef> drop_glue_types;
  std::unordered_map<std::string, DerivedValueInfo> derived_values;

  struct RequiredVTableInfo {
    analysis::TypeRef type;
    analysis::TypePath class_path;
  };
  std::unordered_map<std::string, RequiredVTableInfo> required_vtables;
};

// LowerCtx - context for lowering operations
// Contains type information and scope state needed during lowering
struct LowerCtx {
  // Type environment - maps expressions to their types
  // In full implementation, this would be populated during type checking
  
  // Sigma for type declaration lookups (record fields, etc.)
  const analysis::Sigma* sigma = nullptr;
  
  // Current procedure return type (for propagate expressions)
  analysis::TypeRef proc_ret_type;
  
  // Module path for symbol resolution
  std::vector<std::string> module_path;

  // Selected target profile for target-sensitive semantic layout queries.
  std::optional<project::TargetProfile> target_profile;

  // Project output kind context for entrypoint-sensitive lowering.
  bool executable_project = false;
  bool shared_library_project = false;
  bool hosted_library = false;
  std::optional<std::string> project_entry_module;
  std::unordered_set<std::string> dependency_assembly_names;
  std::unordered_set<std::string> library_assembly_names;
  
  // Expression type lookup (populated by type checking phase)
  std::function<analysis::TypeRef(const ast::Expr&)> expr_type;
  analysis::ExprTypeMap* expr_types = nullptr;
  analysis::DynamicRefineExprMap* dynamic_refine_checks = nullptr;
  analysis::GenericCallSubstMap* generic_call_substs = nullptr;

  // Name resolution lookup
  std::function<std::optional<std::vector<std::string>>(const std::string&)> resolve_name;
  // Type name resolution lookup
  std::function<std::optional<std::vector<std::string>>(const std::string&)> resolve_type_name;

  // Track missing resolution or lowering failures.
  bool resolve_failed = false;
  bool codegen_failed = false;
  std::vector<std::string> resolve_failures;

  // Expression provenance map computed by analysis (per procedure). Shared
  // immutable storage keeps branch-local LowerCtx copies cheap.
  std::shared_ptr<const std::unordered_map<const ast::Expr*, analysis::ProvenanceKind>>
      expr_prov;
  std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>>
      expr_region;
  std::shared_ptr<const std::unordered_map<const ast::Expr*, std::string>>
      expr_region_tags;

  // [[dynamic]] verification scope for runtime checks (arrays, contracts, etc.)
  bool dynamic_checks = false;
  std::optional<AccessOrdering> current_access_order;

  bool log_enabled = false;
  bool log_to_console = false;
  bool log_to_file = false;
  bool trace = false;
  std::optional<std::uint8_t> trace_filter_mask;
  std::optional<std::uint8_t> trace_min_level;
  std::string trace_root;
  std::string log_file_path;

  // Active dynamic postcondition context for return-path checks.
  const ast::Expr* active_contract_postcondition = nullptr;
  std::optional<IRValue> contract_result_value;
  std::unordered_map<const ast::EntryExpr*, IRValue> contract_entry_values;
  std::unordered_map<std::string, IRValue> contract_param_entry_values;
  bool lowering_contract_postcondition = false;

  // IR value and symbol type tracking.
  // Branch-lowered contexts may chain to a parent to avoid copying large
  // transient maps for every branch clone.
  LowerValueState values;
  std::unordered_map<std::string, std::vector<std::string>> static_modules;
  std::unordered_map<std::string, std::vector<std::string>> record_ctor_paths;

  enum class FfiImportUnwindMode {
    Abort,
    Catch,
  };
  struct ProcSigInfo {
    std::vector<IRParam> params;
    analysis::TypeRef ret;
    std::optional<std::string> abi;
    bool ffi_import = false;
    FfiImportUnwindMode ffi_import_unwind_mode =
        FfiImportUnwindMode::Abort;
  };
  struct ForeignContractInfo {
    bool dynamic = false;
    std::vector<ast::ExprPtr> assumes;
    std::vector<ast::ExprPtr> ensures;
    std::vector<ast::ExprPtr> ensures_error;
    std::vector<ast::ExprPtr> ensures_null_result;
  };
  struct LocalContractInfo {
    ast::ExprPtr precondition;
    std::vector<std::string> param_names;
  };
  enum class ExportUnwindMode {
    Abort,
    Catch,
  };
  struct HostedExportInfo {
    std::string internal_symbol;
    std::string thunk_symbol;
    std::vector<IRParam> visible_params;
    analysis::TypeRef context_type;
    analysis::TypeRef ret;
    std::optional<std::string> abi;
    ExportUnwindMode unwind_mode = ExportUnwindMode::Abort;
  };
  struct HostedStateTemplate {
    std::string symbol;
    std::vector<std::uint8_t> bytes;
    std::uint64_t size = 0;
    std::uint64_t align = 1;
    bool zero_init = false;
  };
  std::unordered_map<std::string, ProcSigInfo> proc_sigs;
  std::unordered_map<std::string, LinkageKind> proc_linkages;
  std::unordered_map<std::string, ast::Visibility> proc_visibilities;
  std::unordered_map<std::string, std::vector<std::string>> proc_modules;
  std::unordered_map<std::string, ExportUnwindMode> export_unwind_modes;
  std::unordered_map<std::string, ForeignContractInfo> foreign_contracts;
  std::unordered_map<std::string, LocalContractInfo> local_contracts;
  std::optional<std::string> main_symbol;
  std::vector<HostedExportInfo> hosted_exports;
  std::unordered_map<std::string, HostedStateTemplate> hosted_state_templates;
  std::vector<std::string> hosted_project_modules;
  std::vector<std::string> shared_library_export_symbols;
  struct StaticInitCleanup {
    ast::ModulePath module_path;
    std::string name;
    analysis::TypeRef type;
  };
  std::vector<StaticInitCleanup> active_static_init_cleanup;
  std::unordered_set<std::string> hosted_explicit_env_procs;
  std::vector<ast::ModulePath> init_order;
  std::vector<ast::ModulePath> init_modules;
  std::vector<std::pair<std::size_t, std::size_t>> init_eager_edges;

  // Async procedure lowering metadata
  struct AsyncFrameSlot {
    analysis::TypeRef type;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t align = 1;
  };
  struct AsyncProcInfo {
    bool is_wrapper = false;
    bool is_resume = false;
    bool resume_needs_panic_out = false;
    std::string resume_symbol;
    analysis::TypeRef async_type;
    analysis::TypeRef out_type;
    analysis::TypeRef in_type;
    analysis::TypeRef result_type;
    analysis::TypeRef err_type;
    std::uint64_t frame_size = 0;
    std::uint64_t frame_align = 1;
    std::unordered_map<std::string, AsyncFrameSlot> slots;
    std::vector<std::string> slot_order;
    std::vector<std::string> param_names;
  };
  std::unordered_map<std::string, AsyncProcInfo> async_procs;

  // Shared immutable baseline tables used to avoid repeatedly deep-copying
  // large signature/registry maps when branch-local LowerCtx copies are made.
  struct LookupTables {
    std::unordered_map<std::string, analysis::TypeRef> static_types;
    std::unordered_map<std::string, std::vector<std::string>> static_modules;
    std::unordered_map<std::string, std::vector<std::string>> record_ctor_paths;
    std::unordered_map<std::string, ProcSigInfo> proc_sigs;
    std::unordered_map<std::string, LinkageKind> proc_linkages;
    std::unordered_map<std::string, ast::Visibility> proc_visibilities;
    std::unordered_map<std::string, std::vector<std::string>> proc_modules;
    std::unordered_map<std::string, ExportUnwindMode> export_unwind_modes;
    std::unordered_map<std::string, ForeignContractInfo> foreign_contracts;
    std::unordered_map<std::string, LocalContractInfo> local_contracts;
    std::unordered_map<std::string, AsyncProcInfo> async_procs;
  };
  std::shared_ptr<const LookupTables> baseline_tables;

  // Active region alias stack for implicit frame lowering.
  std::vector<std::string> active_region_aliases;
  std::uint64_t region_alias_counter = 0;

  void ReportResolveFailure(const std::string& name);
  void ReportCodegenFailure(
      std::source_location loc = std::source_location::current());

  void RegisterValueType(
      const IRValue& value,
      analysis::TypeRef type,
      std::source_location loc = std::source_location::current());
  analysis::TypeRef LookupValueType(const IRValue& value) const;
  void RegisterStaticType(const std::string& sym, analysis::TypeRef type);
  analysis::TypeRef LookupStaticType(const std::string& sym) const;
  void RegisterStaticModule(const std::string& sym, const ast::ModulePath& module_path);
  const std::vector<std::string>* LookupStaticModule(const std::string& sym) const;
  void RegisterDropGlueType(const std::string& sym, analysis::TypeRef type);
  analysis::TypeRef LookupDropGlueType(const std::string& sym) const;
  void RegisterRequiredVTable(const std::string& sym,
                              analysis::TypeRef type,
                              const analysis::TypePath& class_path);
  const LowerValueState::RequiredVTableInfo* LookupRequiredVTable(
      const std::string& sym) const;
  void RegisterRecordCtor(const std::string& sym, const std::vector<std::string>& path);
  const std::vector<std::string>* LookupRecordCtor(const std::string& sym) const;
  void RegisterProcSig(const ProcIR& proc);
  const ProcSigInfo* LookupProcSig(const std::string& sym) const;
  void RegisterProcLinkage(const std::string& sym, LinkageKind linkage);
  std::optional<LinkageKind> LookupProcLinkage(const std::string& sym) const;
  const std::unordered_map<std::string, LinkageKind>& AllProcLinkages() const;
  void RegisterProcVisibility(const std::string& sym, ast::Visibility visibility);
  std::optional<ast::Visibility> LookupProcVisibility(const std::string& sym) const;
  void RegisterProcFfiImport(const std::string& sym,
                             FfiImportUnwindMode mode);
  bool NeedsPanicOutForSymbol(const std::string& sym) const;
  void RegisterProcModule(const std::string& sym, const ast::ModulePath& module_path);
  const std::vector<std::string>* LookupProcModule(const std::string& sym) const;
  void RegisterExportUnwindMode(const std::string& sym, ExportUnwindMode mode);
  std::optional<ExportUnwindMode> LookupExportUnwindMode(const std::string& sym) const;
  void RegisterForeignContractInfo(const std::string& sym, ForeignContractInfo info);
  const ForeignContractInfo* LookupForeignContractInfo(const std::string& sym) const;
  void RegisterLocalContractInfo(const std::string& sym, LocalContractInfo info);
  const LocalContractInfo* LookupLocalContractInfo(const std::string& sym) const;
  const AsyncProcInfo* LookupAsyncProc(const std::string& sym) const;
  void FreezeLookupTables();
  void QueueExtraProc(ProcIR proc,
                      std::optional<LinkageKind> linkage = std::nullopt,
                      const ast::ModulePath* module_path = nullptr);
  
  // =========================================================================
  // §6.8 Scope tracking for cleanup
  // =========================================================================
  
  // Stack of scope information for cleanup
  std::vector<ScopeInfo> scope_stack;
  std::uint64_t next_runtime_scope_id = 1;
  
  // Map from binding name to its state
  std::unordered_map<std::string, std::vector<BindingState>> binding_states;
  std::unordered_map<std::string, std::vector<LocalAddrAlias>> local_addr_aliases;
  std::uint64_t next_binding_id = 1;

  // Current temp sink for statement-scoped temporaries
  std::vector<TempValue>* temp_sink = nullptr;
  int temp_depth = 0;
  std::optional<int> suppress_temp_at_depth;
  std::shared_ptr<std::uint64_t> temp_counter = std::make_shared<std::uint64_t>(0);

  // Structured concurrency implicit result collection for parallel blocks
  std::vector<ParallelCollectItem>* parallel_collect = nullptr;
  int parallel_collect_depth = 0;

  // Structured concurrency capture environment for spawn/dispatch bodies
  std::optional<CaptureEnvInfo> capture_env;

  // Active key scopes for nested-release lowering.
  std::vector<ActiveKeyScopeInfo> active_key_scopes;
  std::unordered_map<std::uint64_t, std::string> implicit_key_scope_names;

  // Synthetic procedures generated during lowering (spawn/dispatch wrappers)
  std::vector<ProcIR> extra_procs;
  std::uint64_t synth_proc_counter = 0;
  std::optional<std::string> current_proc_symbol;
  std::uint64_t current_closure_counter = 0;

  // Generic instantiation recursion guard (Section 9.3.4 / E-TYP-2307).
  std::vector<std::string> generic_instantiation_stack;
  std::unordered_set<std::string> generic_instantiation_in_progress;

  // Active source-level generic substitution while lowering a monomorphized
  // procedure body. Analysis maps remain keyed to the generic source body, so
  // lowering must substitute queried expression types before using them for
  // nested generic calls and ABI-sensitive argument lowering.
  std::optional<analysis::TypeSubst> active_generic_type_subst;
  
  // Push a new scope
  void PushScope(bool is_loop = false, bool is_region = false);
  
  // Pop the current scope
  void PopScope();
  
  // Get variables in current scope
  std::vector<std::string> CurrentScopeVars() const;
  
  // Get all variables from current scope up to (but not including) loop scope
  // Used for break/continue cleanup
  std::vector<std::string> VarsToLoopScope() const;
  
  // Get all variables from current scope to function root
  // Used for return cleanup
  std::vector<std::string> VarsToFunctionRoot() const;
  
  // Register a variable in the current scope
  void RegisterVar(const std::string& name,
                   analysis::TypeRef type,
                   bool has_responsibility = true,
                   bool is_immovable = false,
                   analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom,
                   std::optional<std::string> prov_region = std::nullopt,
                   bool preserve_addr_provenance = false,
                   std::optional<std::string> prov_region_tag = std::nullopt);

  // Register runtime scope exit cleanup for the current scope.
  void RegisterRuntimeScopeExit();

  // Runtime scope id for the current scope (if any).
  std::optional<std::uint64_t> CurrentRuntimeScopeId() const;
  
  // Mark a variable as moved
  void MarkMoved(const std::string& name);

  // Mark a sequence of bindings as moved in source order.
  void MarkMoved(const std::vector<std::string>& names);
  
  // Mark a field of a variable as moved
  void MarkFieldMoved(const std::string& name, const std::string& field);
  
  // Get binding state
  const BindingState* GetBindingState(const std::string& name) const;
  const BindingState* GetBindingStateById(const std::string& name,
                                          std::uint64_t binding_id) const;
  std::string StableBindingName(const std::string& name) const;
  void RegisterLocalAddrAlias(const std::string& alias,
                              const std::string& source_name);
  std::optional<LocalAddrAlias> LookupLocalAddrAlias(
      const std::string& alias) const;

  std::optional<analysis::ProvenanceKind> LookupExprProv(const ast::Expr& expr) const;
  std::optional<std::string> LookupExprRegion(const ast::Expr& expr) const;
  std::optional<std::string> LookupExprRegionTag(const ast::Expr& expr) const;
  const std::vector<analysis::TypeRef>* LookupDynamicRefinementTypes(
      const ast::Expr& expr) const;
  
  // Register a defer block in the current scope
  void RegisterDefer(const IRPtr& defer_ir);
  void RegisterRegionRelease(const std::string& name);
  void RegisterKeyScopeExit(const std::string& scope_name);
  void RegisterReleasedKeyReacquire(const std::string& handle_name);
  void RegisterParallelJoin(const IRValue& parallel_ctx);

  // Register a temporary value for cleanup
  void RegisterTempValue(const IRValue& value, const analysis::TypeRef& type);

  void RegisterDerivedValue(const IRValue& value, const DerivedValueInfo& info);
  const DerivedValueInfo* LookupDerivedValue(const IRValue& value) const;

  // Capture lookup helpers (spawn/dispatch lowering)
  CaptureEnvInfo LoadEnv(
      const IRValue& env_param,
      analysis::TypeRef env_type,
      const std::unordered_map<std::string, CaptureAccess>& captures) const;
  void BindAll(CaptureEnvInfo env);
  void BindEnv(CaptureEnvInfo env);
  LoweredCaptureEnv LowerParallelCaptureEnv(
      const std::vector<ParallelCaptureBinding>& captures,
      std::string_view env_prefix);
  const CaptureAccess* LookupCapture(const std::string& name) const;
  IRValue CaptureFieldPtr(const CaptureAccess& access);

  // Generate a unique temporary value name.
  IRValue FreshTempValue(std::string_view prefix);

  // Generate a unique internal alias for an implicit region.
  std::string FreshRegionAlias();
  void ReserveRegionTag(const std::string& name);
  
};

// Helper to create IR nodes
template <typename T>
IRPtr MakeIR(T&& node) {
  return std::make_shared<IR>(IR{std::forward<T>(node)});
}

// Helper to create a sequence of IR nodes (moved to ir_model.h)
// IRPtr SeqIR(std::vector<IRPtr> items);

// Helper to create an empty IR (no-op)
inline IRPtr EmptyIR() {
  return MakeIR(IROpaque{});
}

// Create a usize immediate IR value.
IRValue USizeConstValue(std::uint64_t value);

// Build a scope context for analysis/type-lowering helpers.
const analysis::ScopeContext& ScopeForLowering(const LowerCtx& ctx);
const analysis::ScopeContext& ScopeForLowering(const LowerCtx* ctx);

// Emit runtime scope activation for a scope id.
IRPtr EmitRuntimeScopeEnter(std::uint64_t scope_id, LowerCtx& ctx);

// Emit recorded dynamic refinement checks for an already-lowered expression
// value. Returns EmptyIR() when no runtime refinement check is required.
IRPtr EmitDynamicRefinementChecksForExpr(const ast::Expr& expr,
                                        const IRValue& value,
                                        analysis::TypeRef value_type,
                                        LowerCtx& ctx);

// ============================================================================
// §6.4 Expression Lowering Judgments
// ============================================================================

// §6.4 LowerExpr - main expression lowering entry point
// Lowers any expression form to IR + value
LowerResult LowerExpr(const ast::Expr& expr, LowerCtx& ctx);

// §6.4 LowerList - lower a list of expressions (LTR order)
// Returns IR sequence and list of values
std::pair<IRPtr, std::vector<IRValue>> LowerList(
    const std::vector<ast::ExprPtr>& exprs, LowerCtx& ctx);

// §6.4 LowerList - lower segmented array elements (LTR order)
std::pair<IRPtr, std::vector<DerivedArraySegment>> LowerList(
    const std::vector<ast::ArraySegment>& segments, LowerCtx& ctx);

// §6.4 LowerFieldInits - lower field initializers (LTR order)
std::pair<IRPtr, std::vector<std::pair<std::string, IRValue>>> LowerFieldInits(
    const std::vector<ast::FieldInit>& fields, LowerCtx& ctx, bool suppress_temps);

// Register a lowered record aggregate value backed by field initializer values.
IRValue RegisterLoweredRecordValue(
    std::vector<std::pair<std::string, IRValue>> field_values,
    std::optional<analysis::TypeRef> record_type,
    std::string_view temp_prefix,
    LowerCtx& ctx);

// §6.4 LowerOpt - lower an optional expression
std::pair<IRPtr, std::optional<IRValue>> LowerOpt(
    const ast::ExprPtr& expr_opt, LowerCtx& ctx);

// ============================================================================
// §6.4 Place Lowering Judgments  
// ============================================================================

// §6.4 LowerReadPlace - lower a place expression for reading
LowerResult LowerReadPlace(const ast::Expr& place, LowerCtx& ctx);

// §6.4 LowerWritePlace - lower a place expression for writing
IRPtr LowerWritePlace(const ast::Expr& place, const IRValue& value, LowerCtx& ctx);

// §6.4 LowerWritePlaceSub - lower subplace write (no drop)
IRPtr LowerWritePlaceSub(const ast::Expr& place, const IRValue& value, LowerCtx& ctx);

// §6.4 LowerAddrOf - lower address-of expression
LowerResult LowerAddrOf(const ast::Expr& place, LowerCtx& ctx);

// §6.4 LowerMovePlace - lower move expression
LowerResult LowerMovePlace(const ast::Expr& place, LowerCtx& ctx);

// §6.4 LowerPlace - lower a place to a place representation
IRPlace LowerPlace(const ast::Expr& place, LowerCtx& ctx);

// ============================================================================
// §6.4 Operator Lowering
// ============================================================================

// §6.4 LowerUnOp - lower unary operator
LowerResult LowerUnOp(const std::string& op, const ast::Expr& operand, LowerCtx& ctx);

// §6.4 LowerBinOp - lower binary operator (non-short-circuit)
LowerResult LowerBinOp(const std::string& op,
                       const ast::Expr& lhs,
                       const ast::Expr& rhs,
                       LowerCtx& ctx);

// §6.4 LowerCast - lower cast expression
LowerResult LowerCast(const ast::Expr& expr, analysis::TypeRef target_type, LowerCtx& ctx);

// ============================================================================
// §6.4 Call Lowering
// ============================================================================

using ParamModeList = std::vector<std::optional<analysis::ParamMode>>;
using ParamTypeList = std::vector<analysis::TypeRef>;

// §6.4 LowerArgs - lower call arguments
std::pair<IRPtr, std::vector<IRValue>> LowerArgs(
    const ParamModeList& params,
    const std::vector<ast::Arg>& args,
    LowerCtx& ctx,
    const ParamTypeList* param_types = nullptr);

// §6.4 LowerMethodCall - lower method call expression
LowerResult LowerMethodCall(const ast::Expr& expr_wrapper,
                            const ast::MethodCallExpr& expr,
                            LowerCtx& ctx);

// ============================================================================
// §6.4 Control Flow Expression Lowering
// ============================================================================

// §6.4 LowerBlock - lower a block expression
LowerResult LowerBlock(const ast::Block& block, LowerCtx& ctx);

// §6.4 LowerIfCases - lower if-is case analysis
LowerResult LowerIfCases(const ast::Expr& scrutinee,
                         const std::vector<ast::IfCaseClause>& arms,
                         const ast::ExprPtr& else_expr,
                         bool single_form,
                         LowerCtx& ctx);

// §6.4 LowerLoop - lower loop expression
LowerResult LowerLoop(const ast::Expr& loop, LowerCtx& ctx);

// ============================================================================
// §6.4 Evaluation Order
// ============================================================================

// §6.4 Children_LTR - get child expressions in evaluation order
std::vector<ast::ExprPtr> ChildrenLTR(const ast::Expr& expr);

// Emit SPEC_RULE anchors for all §6.4 rules
void AnchorExprLoweringRules();

}  // namespace cursive::codegen
