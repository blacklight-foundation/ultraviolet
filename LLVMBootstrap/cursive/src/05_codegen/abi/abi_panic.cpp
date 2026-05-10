// =============================================================================
// Panic Out-Parameter Support (§6.2.3)
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.3 Panic Out-Parameter (lines 15287-15301)
//   - PanicRecordFields = [<panic, bool>, <code, u32>]
//   - PanicOutType = rawptr[mut, PanicRecord]
//   - NeedsPanicOut predicate
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"

#include <string>
#include <unordered_set>

namespace cursive::codegen {
namespace {

// Known runtime symbols that do NOT need a panic out-parameter.
// This includes the panic handler itself, context init, and other runtime-defined symbols.
const std::unordered_set<std::string>& RuntimeSymbols() {
  struct Cache {
    project::SourceLanguage language = project::SourceLanguage::Cursive;
    std::unordered_set<std::string> syms;
    bool initialized = false;
  };

  static Cache cache;
  const auto active_language = project::ActiveLanguageProfile().language;
  if (!cache.initialized || cache.language != active_language) {
    cache.language = active_language;
    cache.syms.clear();
    auto add = [](std::unordered_set<std::string>& out, std::string sym) {
      if (!sym.empty()) {
        out.insert(std::move(sym));
      }
    };

    for (const auto& sym : RuntimeBuiltinNoPanicOutSyms()) {
      add(cache.syms, sym);
    }

    // Structured concurrency runtime symbols.
    add(cache.syms, ConcurrencySymParallelBegin());
    add(cache.syms, ConcurrencySymParallelJoin());
    add(cache.syms, ConcurrencySymSpawnCreate());
    add(cache.syms, ConcurrencySymSpawnWait());
    add(cache.syms, ConcurrencySymDispatchRun());
    add(cache.syms, ConcurrencySymCancelTokenNew());
    add(cache.syms, ConcurrencySymCancelTokenCancel());
    add(cache.syms, ConcurrencySymCancelTokenIsCancelled());
    add(cache.syms, ConcurrencySymParallelWorkPanic());
    add(cache.syms, std::string(project::ActiveLanguageProfile().lower_name) + "_panic");
    cache.initialized = true;
  }
  return cache.syms;
}

// EntrySym is the program entry point.
constexpr std::string_view kEntrySym = "main";

// Check if a symbol names one of the built-in record constructors that lower
// directly as record constructors rather than ordinary procedures.
bool IsRecordCtorSymbol(std::string_view sym) {
  if (sym.empty()) {
    return false;
  }

  if (const auto builtin_path = analysis::LookupBuiltinRecordCtorPath(sym);
      builtin_path.has_value()) {
    return true;
  }

  for (const auto ident : {std::string_view("RegionOptions")}) {
    if (const auto builtin_path = analysis::LookupBuiltinRecordCtorPath(ident);
        builtin_path.has_value()) {
      if (core::Mangle(core::StringOfPath(*builtin_path)) == sym) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace

std::vector<std::pair<std::string, analysis::TypeRef>> PanicRecordFields() {
  return {
      {"panic", analysis::MakeTypePrim("bool")},
      {"code", analysis::MakeTypePrim("u32")},
  };
}

std::vector<analysis::TypeRef> PanicRecordFieldTypes() {
  std::vector<analysis::TypeRef> fields;
  for (const auto& [name, type] : PanicRecordFields()) {
    (void)name;
    fields.push_back(type);
  }
  return fields;
}

// PanicRecord type: { panic: bool, code: u32 }
analysis::TypeRef PanicRecordType() {
  return analysis::MakeTypePath({"PanicRecord"});
}

std::optional<::cursive::analysis::layout::RecordLayout> PanicRecordLayout(
    const analysis::ScopeContext& ctx) {
  return ::cursive::analysis::layout::RecordLayoutOf(ctx, PanicRecordFieldTypes());
}

// PanicOutType = rawptr[mut, PanicRecord]
analysis::TypeRef PanicOutType() {
  return analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, PanicRecordType());
}

IRParam PanicOutParam() {
  IRParam panic_param;
  panic_param.mode = analysis::ParamMode::Move;
  panic_param.name = std::string(kPanicOutName);
  panic_param.type = PanicOutType();
  return panic_param;
}

// HostedEnvParamType = rawptr[mut, u8]
analysis::TypeRef HostedEnvParamType() {
  return analysis::MakeTypeRawPtr(
      analysis::RawPtrQual::Mut,
      analysis::MakeTypePrim("u8"));
}

IRParam HostedEnvParam() {
  IRParam param;
  param.mode = analysis::ParamMode::Move;
  param.name = std::string(kHostedEnvParamName);
  param.type = HostedEnvParamType();
  return param;
}

// NeedsPanicOut(callee) iff callee != RecordCtor(_) and callee != EntrySym
// and RuntimeSig(callee) undefined
bool NeedsPanicOut(std::string_view callee_sym) {
  if (callee_sym == kEntrySym) {
    return false;
  }

  if (IsRuntimeFunction(std::string(callee_sym))) {
    return false;
  }

  const auto& runtime_syms = RuntimeSymbols();
  if (runtime_syms.find(std::string(callee_sym)) != runtime_syms.end()) {
    return false;
  }

  if (IsRecordCtorSymbol(callee_sym)) {
    return false;
  }

  return true;
}

// PanicOutParams(params, callee) - appends panic out-param if needed
std::vector<std::tuple<std::optional<analysis::ParamMode>, std::string, analysis::TypeRef>>
PanicOutParams(
    const std::vector<std::tuple<std::optional<analysis::ParamMode>, std::string, analysis::TypeRef>>& params,
    std::string_view callee_sym) {
  auto result = params;
  if (NeedsPanicOut(callee_sym)) {
    IRParam panic_param = PanicOutParam();
    result.push_back({panic_param.mode, panic_param.name, panic_param.type});
  }
  return result;
}

}  // namespace cursive::codegen
