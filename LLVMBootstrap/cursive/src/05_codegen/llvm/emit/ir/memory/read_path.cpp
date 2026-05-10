// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/read_path.cpp
// Canonical owner for LLVM IR path read instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRReadPath &read) const
{
  std::vector<std::string> full = read.path;
  full.push_back(read.name);
  const std::string qualified = core::StringOfPath(full);
  const std::string mangled = core::Mangle(qualified);
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  const bool known_proc = ctx && (ctx->LookupProcSig(mangled) != nullptr);
  const bool known_static = ctx && static_cast<bool>(ctx->LookupStaticType(mangled));
  const bool known_drop_glue = ctx && static_cast<bool>(ctx->LookupDropGlueType(mangled));
  const auto *record_ctor_path = ctx ? ctx->LookupRecordCtor(mangled) : nullptr;
  const bool known_record_ctor = record_ctor_path != nullptr;
  const bool known_runtime = IsRuntimeFunction(mangled);
  auto emit_poison_if_user_module = [&](const std::vector<std::string>* module_path) {
    if (!module_path || module_path->empty()) {
      return;
    }
    if (ctx && !ctx->module_path.empty() && !module_path->empty()) {
      const std::string& current_root = ctx->module_path.front();
      const std::string& target_root = module_path->front();
      const bool cross_shared_library_boundary =
          current_root != target_root &&
          ctx->shared_library_assembly_names.contains(target_root);
      if (cross_shared_library_boundary) {
        return;
      }
    }
    emitter.EmitPoisonCheck(core::StringOfPath(*module_path));
  };

  if (known_record_ctor)
  {
    std::vector<std::string> owner_module = *record_ctor_path;
    if (!owner_module.empty())
    {
      owner_module.pop_back();
    }
    emit_poison_if_user_module(&owner_module);
    emitter.SetSymbolAlias(read.name, mangled);
    emitter.SetSymbolAlias(qualified, mangled);
    return;
  }

  if (emitter.GetFunction(mangled) || emitter.GetGlobal(mangled) ||
      known_proc || known_static || known_drop_glue || known_runtime)
  {
    if (known_proc)
    {
      emit_poison_if_user_module(ctx->LookupProcModule(mangled));
    }
    else if (known_static)
    {
      emit_poison_if_user_module(ctx->LookupStaticModule(mangled));
    }
    emitter.SetSymbolAlias(read.name, mangled);
    emitter.SetSymbolAlias(qualified, mangled);
    return;
  }
  if (emitter.GetFunction(read.name) || emitter.GetGlobal(read.name))
  {
    emitter.SetSymbolAlias(read.name, read.name);
    emitter.SetSymbolAlias(qualified, read.name);
  }
}

} // namespace cursive::codegen::emit_detail
