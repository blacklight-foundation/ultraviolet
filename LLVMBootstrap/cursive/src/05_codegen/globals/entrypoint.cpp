// =============================================================================
// MIGRATION MAPPING: entrypoint.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.0 CG-Item-Procedure-Main rule (lines 14262-14265)
//   - MainSigOk predicate (line 14263)
//   - EmitInitPlan(P) for program initialization (line 14263)
//   - EntrySym linkage (lines 15660-15663)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/entrypoint.cpp
//   - Entry point wrapper generation
//   - Context initialization
//   - Main procedure invocation
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/globals/entrypoint.h
//   - cursive/include/05_codegen/ir/ir_model.h (ProcIR)
//   - cursive/include/05_codegen/symbols/linkage.h (LinkageOfEntrySym)
//
// REFACTORING NOTES:
//   1. Entry point wraps user's main procedure
//   2. MainSigOk: main(move ctx: Context) -> i32
//   3. Entry performs:
//      - Runtime initialization
//      - Context creation
//      - Module init calls (EmitInitPlan)
//      - main() invocation
//      - Module deinit calls
//      - Exit code return
//   4. EntrySym has External linkage
//   5. Panic handling at entry level
//
// ENTRY POINT STRUCTURE:
//   proc main() -> i32 {  // C-compatible entry
//     // Runtime init
//     let ctx = Context::new(...)
//     // Module init plan
//     EmitInitPlan(...)
//     // Call user main
//     let result = user_main(move ctx)
//     // Module deinit
//     // Return exit code
//     return result
//   }
//
// MAIN SIGNATURE CHECK:
//   - Name must be "main"
//   - Single parameter: move ctx: Context
//   - Return type: i32
//   - Must be in executable project
// =============================================================================

#include "05_codegen/globals/entrypoint.h"

#include <string_view>

#include "05_codegen/globals/globals.h"
#include "05_codegen/globals/init.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"
#include "00_core/assert_spec.h"
#include "00_core/symbols.h"

namespace cursive::codegen {

namespace {

IRValue StringImmediate(std::string_view text) {
  IRValue value;
  value.kind = IRValue::Kind::Immediate;
  value.name = "\"" + std::string(text) + "\"";
  value.bytes.assign(text.begin(), text.end());
  return value;
}

}  // namespace

// ============================================================================
// Section 6.12.17 Panic Record Type
// ============================================================================

// Note: PanicRecordType() and PanicOutType() are already defined in abi_panic.cpp
// and declared in abi.h. We just use them here - no need to redefine.

// ============================================================================
// Section 6.12.17 User Main Symbol
// ============================================================================

std::optional<std::string> MainProcSym(const LowerCtx& ctx) {
  if (ctx.main_symbol.has_value()) {
    return ctx.main_symbol;
  }
  return std::nullopt;
}

std::string MainProcSymForPath(const ast::ModulePath& module_path) {
  // Construct the path for the main procedure
  std::vector<std::string> path = module_path;
  path.push_back("main");
  return core::Mangle(core::StringOfPath(path));
}

// ============================================================================
// Section 6.12.17 Entry Sequence IR Generation
// ============================================================================

IRPtr EmitEntrySequenceIR(const LowerCtx& ctx) {
  SPEC_RULE("EntrySequenceIR");

  std::vector<IRPtr> parts;
  const auto main_sym = MainProcSym(ctx);
  std::vector<ast::ModulePath> init_order = ctx.init_order;
  if (init_order.empty() && ctx.sigma) {
    init_order = ComputeInitOrderFromSigma(*ctx.sigma);
  }

  // 1. Create context value temporary
  IRValue ctx_value;
  ctx_value.kind = IRValue::Kind::Opaque;
  ctx_value.name = "__entry_ctx";

  // 2. Call context init
  IRCall init_call;
  init_call.callee.kind = IRValue::Kind::Symbol;
  init_call.callee.name = ContextInitSym();
  init_call.result = ctx_value;
  parts.push_back(MakeIR(std::move(init_call)));

  // 3. Clear panic record
  parts.push_back(MakeIR(IRClearPanic{}));

  // 4. Emit init plan (mutable copy needed)
  LowerCtx init_ctx = ctx;  // Copy for modification
  parts.push_back(EmitInitPlan(init_order, init_ctx));

  // 5. Call user main with context and panic out
  if (main_sym.has_value()) {
    const LowerCtx::ProcSigInfo* main_sig = ctx.LookupProcSig(*main_sym);
    IRValue main_arg = ctx_value;
    if (main_sig && !main_sig->params.empty() && main_sig->params[0].type) {
      IRContextBundleBuild build_ctx;
      build_ctx.target_type = main_sig->params[0].type;
      build_ctx.root_ctx = ctx_value;
      build_ctx.result.kind = IRValue::Kind::Opaque;
      build_ctx.result.name = "__entry_main_arg";
      main_arg = build_ctx.result;
      parts.push_back(MakeIR(std::move(build_ctx)));
    }

    IRValue panic_arg;
    panic_arg.kind = IRValue::Kind::Local;
    panic_arg.name = std::string(kPanicOutName);

    IRValue ret_value;
    ret_value.kind = IRValue::Kind::Opaque;
    ret_value.name = "__entry_ret";

    IRCall main_call;
    main_call.callee.kind = IRValue::Kind::Symbol;
    main_call.callee.name = *main_sym;
    main_call.args = {main_arg, panic_arg};
    main_call.result = ret_value;
    parts.push_back(MakeIR(std::move(main_call)));

    // 6. Panic check
    parts.push_back(MakeIR(IRPanicCheck{}));

    // 7. Emit deinit plan
    parts.push_back(EmitDeinitPlan(init_order, init_ctx));

    // 7.5 Deinit may have restored a panic record; entry semantics must still
    // route that through the runtime panic path instead of returning success.
    parts.push_back(MakeIR(IRPanicCheck{}));

    // 8. Return result
    IRReturn ret;
    ret.value = ret_value;
    parts.push_back(MakeIR(std::move(ret)));
  }

  return SeqIR(std::move(parts));
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorEntrypointRules() {
  // Section 6.12.17 Entrypoint
  SPEC_RULE("EntryJudg");
  SPEC_RULE("EntrySym-Decl");
  SPEC_RULE("EntrySym-Err");
  SPEC_RULE("ContextInitSym-Decl");
  SPEC_RULE("EntryStub-Decl");
  SPEC_RULE("EntryStub-Err");
  SPEC_RULE("RuntimePanicSym");
  SPEC_RULE("EntrySequenceIR");

  // Section 6.0 CG-Item-Procedure-Main
  SPEC_RULE("MainSigOk");
  SPEC_RULE("CG-Item-Procedure-Main");
}

}  // namespace cursive::codegen
