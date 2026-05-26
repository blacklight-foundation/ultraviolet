#pragma once

// ============================================================================
// §6.12.17 Entrypoint and Context Construction
// ============================================================================
// This header declares the entrypoint generation functionality for Ultraviolet
// executables. The entrypoint is the C `main` function that:
// 1. Initializes the Ultraviolet runtime context
// 2. Clears the panic record
// 3. Calls the user's main procedure
// 4. Runs deinitializers or handles panics
//
// EntrypointJudg = {EntrySym, ContextInitSym, EntryStub, EmitEntryPoint}
// ============================================================================

#include <optional>
#include <string>
#include <vector>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/abi/abi.h"  // For kPanicOutName
#include "05_codegen/intrinsics/builtins.h"  // For ContextInitSym, RuntimePanicSym

namespace ultraviolet::codegen {

// Forward declarations
class LLVMEmitter;

// ============================================================================
// §6.12.17 Entrypoint Symbol Generation
// ============================================================================

// (EntrySym-Decl): Get the C entry point symbol name
// Returns "main" for the C runtime entry
constexpr const char* EntrySym() { return "main"; }

// Note: ContextInitSym() is declared in builtins.h

// ============================================================================
// §6.12.17 Panic Record Type
// ============================================================================

// (PanicRecordType): Get the type of the panic record
// This is a record { flag: bool, code: u32 } used to track panic state
analysis::TypeRef PanicRecordType();

// (PanicOutType): Get the type of the panic out-parameter
// This is a pointer to PanicRecordType
analysis::TypeRef PanicOutType();

// Note: kPanicOutName is defined in abi.h as "__panic"

// ============================================================================
// §6.12.17 User Main Symbol
// ============================================================================

// (MainProcSym): Get the mangled symbol for the user's main procedure
// The user main has signature: public procedure main(move ctx: Context) -> i32
std::optional<std::string> MainProcSym(const LowerCtx& ctx);

// (MainProcSym): Compute the mangled symbol for a main procedure at path
std::string MainProcSymForPath(const ast::ModulePath& module_path);

// ============================================================================
// §6.12.17 Entrypoint Generation (LLVM-specific)
// ============================================================================

// (EmitEntryPoint): Generate the C main function in LLVM IR
// This is called by LLVMEmitter after all other declarations are emitted.
// The entry point:
//   1. Allocates a panic record on the stack
//   2. Calls ContextInitSym() to construct a Context
//   3. Clears the panic record
//   4. Calls the user's main procedure with the context
//   5. Checks the panic record:
//      - If panic: calls RuntimePanicSym() and terminates
//      - If ok: calls EmitDeinitPlan() and returns the user's return code
//
// This function is implemented as LLVMEmitter::EmitEntryPoint() and is
// declared here for documentation purposes.
// Note: Actual emission is done via LLVMEmitter::EmitEntryPoint()

// ============================================================================
// §6.12.17 Runtime Panic Handling
// ============================================================================

// Note: RuntimePanicSym() is declared in builtins.h

// (IRClearPanic): Create IR to clear the panic record
// Emits zeroing of the panic record before calling user code
inline IRPtr EmitClearPanicIR() { return MakeIR(IRClearPanic{}); }

// (IRPanicCheck): Create IR to check and handle panic
// Returns IR that checks the panic record and returns panic control if set.
inline IRPtr EmitPanicCheckIR() { return MakeIR(IRPanicCheck{}); }

inline IRPtr EmitCleanupPanicCheckIR(IRPtr cleanup_ir) {
  IRCleanupPanicCheck check;
  check.cleanup_ir = cleanup_ir;
  return MakeIR(std::move(check));
}

// ============================================================================
// §6.12.17 Entry Sequence IR Generation
// ============================================================================

// (EntrySequenceIR): Generate the complete entry sequence IR
// This creates the IR for:
//   1. Context initialization call
//   2. Clear panic record
//   3. User main call with context and panic out
//   4. Panic check and handling
//   5. Deinit plan execution
IRPtr EmitEntrySequenceIR(const LowerCtx& ctx);

// (EntryStub-Decl): Materialize the spec-level entry stub declaration.
// Returns nullopt when the current lowering context does not define a user
// main procedure for the active executable project.
inline std::optional<ProcIR> EntryStubDecl(const LowerCtx& ctx) {
  SPEC_RULE("EntryJudg");

  if (!ctx.executable_project) {
    SPEC_RULE("EntryStub-Err");
    return std::nullopt;
  }

  const auto main_sym = MainProcSym(ctx);
  if (!main_sym.has_value()) {
    SPEC_RULE("EntryStub-Err");
    return std::nullopt;
  }

  ProcIR proc;
  proc.symbol = EntrySym();
  proc.ret = analysis::MakeTypePrim("i32");
  proc.body = EmitEntrySequenceIR(ctx);
  SPEC_RULE("EntryStub-Decl");
  return proc;
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

// Emits SPEC_RULE anchors for §6.12.17 entrypoint rules
void AnchorEntrypointRules();

}  // namespace ultraviolet::codegen
