#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// =============================================================================
// §6.2.1 Default Calling Convention
// =============================================================================

// CallConvDefault = UltravioletABI (Win64 x86_64-pc-windows-msvc)

// CallingConvention = {Ultraviolet, C, CUnwind, System, Stdcall, Fastcall, Vectorcall}
enum class CallingConvention {
  Ultraviolet,    // Default Ultraviolet ABI
  C,           // Standard C ABI
  CUnwind,     // C with unwinding support
  System,      // Platform default
  Stdcall,     // Windows stdcall
  Fastcall,    // Fastcall convention
  Vectorcall,  // Vectorcall convention
};

// Parse an ABI string to CallingConvention.
std::optional<CallingConvention> ParseCallingConvention(std::string_view abi_string);

// Get the name of a calling convention.
std::string_view CallingConventionName(CallingConvention cc);

// Check if a calling convention allows unwinding.
bool IsUnwindingConvention(CallingConvention cc);

// Check if a calling convention is C-compatible.
bool IsCCompatible(CallingConvention cc);

// =============================================================================
// §6.2.2 ABI Type Lowering
// =============================================================================

// ABIType = { <size, align> | size ∈ ℕ ∧ align ∈ ℕ }
// Reuses existing ::ultraviolet::analysis::layout::Layout struct which has the same structure.
using ABIType = ::ultraviolet::analysis::layout::Layout;

// ABITy(T) ⇓ <size, align>
// Maps a semantic type to its ABI representation (size and alignment).
std::optional<ABIType> ABITy(const analysis::ScopeContext& ctx,
                             const analysis::TypeRef& type);

// =============================================================================
// §6.2.3 ABI Parameter and Return Passing
// =============================================================================

// PassKind = {ByValue, ByRef, SRet}
enum class PassKind {
  ByValue,
  ByRef,
  SRet,
};

// Parameter-classification policy.
// - ModeAware: source-level parameters are represented by reference; mode
//   controls cleanup responsibility.
// - ForeignBoundary: pass-kind selection is mode-independent and derived from
//   type/layout only.
enum class ABIParamPolicy {
  ModeAware,
  ForeignBoundary,
};

// ByValMax = 16 bytes
constexpr std::uint64_t kByValMax = 16;

// ByValOk(T) ⟺ sizeof(T) ≤ ByValMax ∧ alignof(T) ≤ ByValAlign
bool ByValOk(const analysis::ScopeContext& ctx, const analysis::TypeRef& type);

// ABIParam(mode, T) ⇓ PassKind
// Determines how a parameter of type T with given mode should be passed.
std::optional<PassKind> ABIParam(const analysis::ScopeContext& ctx,
                                 std::optional<analysis::ParamMode> mode,
                                 const analysis::TypeRef& type,
                                 ABIParamPolicy policy = ABIParamPolicy::ModeAware);

// ABIRet(T) ⇓ PassKind
// Determines how a return value of type T should be passed.
std::optional<PassKind> ABIRet(const analysis::ScopeContext& ctx,
                               const analysis::TypeRef& type);

// Result of ABICall judgment
struct ABICallInfo {
  std::vector<PassKind> param_kinds;
  PassKind ret_kind = PassKind::ByValue;
  bool has_sret = false;
};

// ABICall([⟨m₁,T₁⟩,...,⟨mₙ,Tₙ⟩], R) ⇓ ⟨[k₁,...,kₙ], kᵣ, (kᵣ = SRet)⟩
std::optional<ABICallInfo> ABICall(
    const analysis::ScopeContext& ctx,
    const std::vector<std::pair<std::optional<analysis::ParamMode>, analysis::TypeRef>>& params,
    const analysis::TypeRef& ret,
    ABIParamPolicy policy = ABIParamPolicy::ModeAware);

// -----------------------------------------------------------------------------
// Panic Out-Parameter (Ultraviolet)
// -----------------------------------------------------------------------------

// PanicRecordFields = [⟨panic, bool⟩, ⟨code, u32⟩]
// Returns the TypeRef for the PanicRecord struct type.
std::vector<std::pair<std::string, analysis::TypeRef>> PanicRecordFields();
std::vector<analysis::TypeRef> PanicRecordFieldTypes();
analysis::TypeRef PanicRecordType();
std::optional<::ultraviolet::analysis::layout::RecordLayout> PanicRecordLayout(
    const analysis::ScopeContext& ctx);

// PanicOutType = rawptr[mut, PanicRecord]
analysis::TypeRef PanicOutType();
IRParam PanicOutParam();

// PanicOutName = "__panic"
constexpr std::string_view kPanicOutName = "__panic";

// HostedEnvParamName = "__uv_host_env"
constexpr std::string_view kHostedEnvParamName = "__uv_host_env";

// AsyncOutParamName = "__uv_async_out"
constexpr std::string_view kAsyncOutParamName = "__uv_async_out";

// HostedEnvParamType = rawptr[mut, u8]
analysis::TypeRef HostedEnvParamType();

// HostedEnvParam = <move, HostedEnvParamName, HostedEnvParamType>
IRParam HostedEnvParam();

// NeedsPanicOut(callee) ⟺ callee ≠ RecordCtor(_) ∧ callee ≠ EntrySym ∧ RuntimeSig(callee) undefined
bool NeedsPanicOut(std::string_view callee_sym);

// PanicOutParams(params, callee) - appends panic out-param if needed
std::vector<std::tuple<std::optional<analysis::ParamMode>, std::string, analysis::TypeRef>>
PanicOutParams(
    const std::vector<std::tuple<std::optional<analysis::ParamMode>, std::string, analysis::TypeRef>>& params,
    std::string_view callee_sym);

// =============================================================================
// §6.2.4 Call Lowering for Procedures and Methods
// =============================================================================

// Result of LowerArgs judgment
struct LowerArgsResult {
  IRPtr ir;
  std::vector<IRValue> values;
};

// Result of LowerRecvArg / LowerMethodCall judgments
struct LowerCallResult {
  IRPtr ir;
  IRValue value;
};

// -----------------------------------------------------------------------------
// Symbol Resolution
// -----------------------------------------------------------------------------

// MethodSymbol(T, name) ⇓ sym
// Resolves the mangled symbol for a method call on type T.
std::optional<std::string> MethodSymbol(const analysis::ScopeContext& ctx,
                                        const analysis::TypeRef& type,
                                        std::string_view name);

// BuiltinMethodSym(CapClass, name) ⇓ sym
// Resolves the symbol for a builtin capability method.
std::optional<std::string> BuiltinMethodSym(const analysis::TypePath& cap_path,
                                            std::string_view name);
std::optional<std::string> BuiltinMethodSym(std::string_view cap_class,
                                            std::string_view name);

// BuiltinCapClass = {IO, HeapAllocator}
bool IsBuiltinCapClass(const analysis::TypePath& class_path);
bool IsBuiltinCapClass(std::string_view class_name);

// -----------------------------------------------------------------------------
// Argument Lowering
// -----------------------------------------------------------------------------

// LowerArgs(params, args) ⇓ ⟨IR, [v₁,...,vₙ]⟩
// Lowers argument expressions to IR values.
// Note: This is a simplified interface; full implementation requires
// LowerExpr/LowerAddrOf functions from expression lowering (T-LOWER-001).
std::optional<LowerArgsResult> LowerArgs(
    const analysis::ScopeContext& ctx,
    const std::vector<ast::Param>& params,
    const std::vector<ast::Arg>& args);

// LowerRecvArg(base) ⇓ ⟨IR, v_self⟩
// Lowers the receiver expression for a method call.
std::optional<LowerCallResult> LowerRecvArg(
    const analysis::ScopeContext& ctx,
    const ast::ExprPtr& base,
    bool is_move);

// -----------------------------------------------------------------------------
// Method Call Lowering
// -----------------------------------------------------------------------------

// LowerMethodCall(MethodCall(base, name, args)) ⇓ ⟨IR, v_call⟩
// Generates IR for a complete method call.
std::optional<LowerCallResult> LowerMethodCall(
    const analysis::ScopeContext& ctx,
    const ast::ExprPtr& base,
    std::string_view name,
    const std::vector<ast::Arg>& args);

// SeqIR helper - REMOVED (moved to ir_model.h)

// =============================================================================
// SPEC_RULE anchors for coverage tracking
// =============================================================================

// Emits SPEC_RULE anchors for all §6.2.2, §6.2.3, §6.2.4 rules.
void AnchorABIRules();

}  // namespace ultraviolet::codegen
