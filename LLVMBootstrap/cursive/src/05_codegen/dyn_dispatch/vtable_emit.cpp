// =============================================================================
// VTable Emission Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.10 VTable Emission (lines 17597-17645)
//   - Mangle-VTable rule (lines 15517-15520)
//   - Linkage-VTable rule (lines 15620-15623)
//   - DynLayout rule (lines 17556-17558)
//
// =============================================================================

#include "05_codegen/dyn_dispatch/vtable_emit.h"

#include <algorithm>
#include <set>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"

namespace cursive::codegen {

namespace {

/// Prefix used for vtable symbols.
constexpr std::string_view kVTablePrefix = "_ZTV";  // C++ style vtable prefix

void AddVTableRef(std::set<std::string>& refs, const IRValue& value) {
  if (!value.vtable_sym.empty()) {
    refs.insert(value.vtable_sym);
  }
}

void AddOptVTableRef(std::set<std::string>& refs,
                     const std::optional<IRValue>& value) {
  if (value.has_value()) {
    AddVTableRef(refs, *value);
  }
}

void CollectVTableRefsFromIR(std::set<std::string>& refs, const IRPtr& ir) {
  if (!ir) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IROpaque>) {
          return;
        } else if constexpr (std::is_same_v<T, IRSeq>) {
          for (const auto& item : node.items) {
            CollectVTableRefsFromIR(refs, item);
          }
        } else if constexpr (std::is_same_v<T, IRCall>) {
          AddVTableRef(refs, node.callee);
          for (const auto& arg : node.args) {
            AddVTableRef(refs, arg);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRCallVTable>) {
          AddVTableRef(refs, node.base);
          for (const auto& arg : node.args) {
            AddVTableRef(refs, arg);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRStoreGlobal>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRBindVar>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRStoreVar>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRStoreVarNoDrop>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRReadPtr>) {
          AddVTableRef(refs, node.ptr);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRWritePtr>) {
          AddVTableRef(refs, node.ptr);
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRUnaryOp>) {
          AddVTableRef(refs, node.operand);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRBinaryOp>) {
          AddVTableRef(refs, node.lhs);
          AddVTableRef(refs, node.rhs);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRCast>) {
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRTransmute>) {
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRCheckIndex>) {
          AddVTableRef(refs, node.base);
          AddVTableRef(refs, node.index);
        } else if constexpr (std::is_same_v<T, IRCheckRange>) {
          AddVTableRef(refs, node.base);
          AddOptVTableRef(refs, node.range.lo);
          AddOptVTableRef(refs, node.range.hi);
          AddOptVTableRef(refs, node.range_value);
        } else if constexpr (std::is_same_v<T, IRCheckSliceLen>) {
          AddVTableRef(refs, node.base);
          AddOptVTableRef(refs, node.range.lo);
          AddOptVTableRef(refs, node.range.hi);
          AddOptVTableRef(refs, node.range_value);
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRCheckOp>) {
          AddVTableRef(refs, node.lhs);
          AddOptVTableRef(refs, node.rhs);
        } else if constexpr (std::is_same_v<T, IRCheckCast>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRAlloc>) {
          AddOptVTableRef(refs, node.region);
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRContextBundleBuild>) {
          AddVTableRef(refs, node.root_ctx);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRReturn>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRResult>) {
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRBreak>) {
          AddOptVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRIf>) {
          AddVTableRef(refs, node.cond);
          CollectVTableRefsFromIR(refs, node.then_ir);
          AddVTableRef(refs, node.then_value);
          CollectVTableRefsFromIR(refs, node.else_ir);
          AddVTableRef(refs, node.else_value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRBlock>) {
          CollectVTableRefsFromIR(refs, node.setup);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRLoop>) {
          CollectVTableRefsFromIR(refs, node.iter_ir);
          AddOptVTableRef(refs, node.iter_value);
          CollectVTableRefsFromIR(refs, node.cond_ir);
          AddOptVTableRef(refs, node.cond_value);
          CollectVTableRefsFromIR(refs, node.body_ir);
          AddVTableRef(refs, node.body_value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRIfCase>) {
          AddVTableRef(refs, node.scrutinee);
          for (const auto& arm : node.arms) {
            CollectVTableRefsFromIR(refs, arm.body);
            AddVTableRef(refs, arm.value);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRRegion>) {
          AddVTableRef(refs, node.owner);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRFrame>) {
          AddOptVTableRef(refs, node.region);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRBranch>) {
          AddOptVTableRef(refs, node.cond);
        } else if constexpr (std::is_same_v<T, IRPhi>) {
          for (const auto& incoming : node.incoming) {
            AddVTableRef(refs, incoming.value);
          }
          AddVTableRef(refs, node.value);
        } else if constexpr (std::is_same_v<T, IRPanicCheck>) {
          CollectVTableRefsFromIR(refs, node.cleanup_ir);
        } else if constexpr (std::is_same_v<T, IRInitPanicHandle>) {
          CollectVTableRefsFromIR(refs, node.cleanup_ir);
        } else if constexpr (std::is_same_v<T, IRLowerPanic>) {
          CollectVTableRefsFromIR(refs, node.cleanup_ir);
        } else if constexpr (std::is_same_v<T, IRParallel>) {
          AddVTableRef(refs, node.domain);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.result);
          AddOptVTableRef(refs, node.cancel_token);
        } else if constexpr (std::is_same_v<T, IRSpawn>) {
          CollectVTableRefsFromIR(refs, node.captured_env);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.body_result);
          AddVTableRef(refs, node.result);
          AddVTableRef(refs, node.env_ptr);
          AddVTableRef(refs, node.env_size);
          AddVTableRef(refs, node.body_fn);
          AddVTableRef(refs, node.result_size);
          AddOptVTableRef(refs, node.affinity_mask);
          AddOptVTableRef(refs, node.priority);
        } else if constexpr (std::is_same_v<T, IRWait>) {
          AddVTableRef(refs, node.handle);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRCancelCheck>) {
          AddVTableRef(refs, node.token);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRDispatch>) {
          AddVTableRef(refs, node.range);
          CollectVTableRefsFromIR(refs, node.body);
          AddVTableRef(refs, node.body_result);
          CollectVTableRefsFromIR(refs, node.captured_env);
          AddVTableRef(refs, node.env_ptr);
          AddVTableRef(refs, node.body_fn);
          AddVTableRef(refs, node.elem_size);
          AddVTableRef(refs, node.result_size);
          AddVTableRef(refs, node.result_ptr);
          AddOptVTableRef(refs, node.reduce_fn);
          AddVTableRef(refs, node.result);
          AddOptVTableRef(refs, node.chunk_size);
        } else if constexpr (std::is_same_v<T, IRYield>) {
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
          AddVTableRef(refs, node.keys_record);
        } else if constexpr (std::is_same_v<T, IRYieldFrom>) {
          AddVTableRef(refs, node.source);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRSync>) {
          AddVTableRef(refs, node.async_value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRRaceReturn>) {
          for (const auto& arm : node.arms) {
            CollectVTableRefsFromIR(refs, arm.async_ir);
            AddVTableRef(refs, arm.async_value);
            AddVTableRef(refs, arm.match_value);
            CollectVTableRefsFromIR(refs, arm.handler_ir);
            AddVTableRef(refs, arm.handler_result);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRRaceYield>) {
          for (const auto& arm : node.arms) {
            CollectVTableRefsFromIR(refs, arm.async_ir);
            AddVTableRef(refs, arm.async_value);
            AddVTableRef(refs, arm.match_value);
            CollectVTableRefsFromIR(refs, arm.handler_ir);
            AddVTableRef(refs, arm.handler_result);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRAll>) {
          for (const auto& async_ir : node.async_irs) {
            CollectVTableRefsFromIR(refs, async_ir);
          }
          for (const auto& async_value : node.async_values) {
            AddVTableRef(refs, async_value);
          }
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRAsyncComplete>) {
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
        } else if constexpr (std::is_same_v<T, IRAsyncFail>) {
          AddVTableRef(refs, node.value);
          AddVTableRef(refs, node.result);
        }
      },
      ir->node);
}

}  // namespace

// =============================================================================
// VTable Header ::cursive::analysis::layout::Layout Implementation
// =============================================================================

VTableHeaderLayout GetVTableHeaderLayout(
    project::TargetProfile target_profile) {
  const auto env = analysis::layout::LayoutEnvOf(target_profile);
  const std::uint64_t ptr_size = analysis::layout::PtrSize(env);
  VTableHeaderLayout layout;
  layout.size_offset = 0;
  layout.align_offset = ptr_size;
  layout.drop_offset = 2 * ptr_size;
  layout.slots_offset = 3 * ptr_size;
  layout.slot_size = ptr_size;
  return layout;
}

VTableHeaderLayout GetVTableHeaderLayout(const LowerCtx& ctx) {
  return GetVTableHeaderLayout(
      ctx.target_profile.value_or(project::TargetProfile::X86_64SysV));
}

// =============================================================================
// VTable Symbol Resolution Implementation
// =============================================================================

std::string ResolveVTableSlotSymbol(
    const analysis::TypeRef& impl_type,
    const analysis::TypePath& class_path,
    const std::string& method_name,
    [[maybe_unused]] LowerCtx& ctx) {
  SPEC_RULE("DispatchSym-Impl");

  // Build the method symbol for the implementing type
  // This will be resolved during VTable generation
  std::vector<std::string> path;

  // Get the type path
  if (impl_type) {
    auto type_path = PathOfType(impl_type);
    path.insert(path.end(), type_path.begin(), type_path.end());
  }

  path.push_back(method_name);

  return ScopedSym(path);
}

std::string GetDropGlueSymbol(const analysis::TypeRef& type) {
  SPEC_RULE("DropGlueSym");

  if (!type) {
    return "";
  }

  // Build drop glue symbol: __drop_<type_path>
  auto type_path = PathOfType(type);
  std::vector<std::string> path = {"__drop"};
  path.insert(path.end(), type_path.begin(), type_path.end());

  return ScopedSym(path);
}

// =============================================================================
// VTable IR Generation Implementation
// =============================================================================

GlobalVTable GenerateVTableIR(
    const analysis::TypeRef& type,
    const analysis::TypePath& class_path,
    const ast::ClassDecl& class_decl,
    LowerCtx& ctx) {
  SPEC_RULE("VTable-Order");

  GlobalVTable vtable;

  // Generate the vtable symbol
  vtable.symbol = MangleVTable(type, class_path);

  // Get type size and alignment
  // These would normally come from the layout module
  // Initialize with defaults; finalized values are filled during emission
  vtable.header.size = 0;   // Will be computed from type layout
  vtable.header.align = 1;  // Will be computed from type layout

  // Get drop glue symbol
  vtable.header.drop_sym = GetDropGlueSymbol(type);

  // Get vtable-eligible methods
  auto eligible_methods = VTableEligible(class_decl);

  // Generate slot symbols in order
  vtable.slots.reserve(eligible_methods.size());
  for (const auto& method_name : eligible_methods) {
    std::string slot_sym = ResolveVTableSlotSymbol(type, class_path, method_name, ctx);
    vtable.slots.push_back(slot_sym);
  }

  return vtable;
}

// =============================================================================
// VTable Validation Implementation
// =============================================================================

bool ValidateVTable(
    const GlobalVTable& vtable,
    std::vector<std::string>& error_messages) {
  SPEC_RULE("ValidateVTable");

  bool valid = true;

  // Check that the symbol is not empty
  if (vtable.symbol.empty()) {
    error_messages.push_back("VTable symbol is empty");
    valid = false;
  }

  // Check that drop symbol is present (can be empty for Bitcopy types)
  // We don't require drop_sym to be non-empty

  // Check that all slots have symbols
  for (std::size_t i = 0; i < vtable.slots.size(); ++i) {
    if (vtable.slots[i].empty()) {
      error_messages.push_back("VTable slot " + std::to_string(i) + " has empty symbol");
      valid = false;
    }
  }

  return valid;
}

bool IsValidSlotTarget(
    [[maybe_unused]] const std::string& symbol,
    [[maybe_unused]] LowerCtx& ctx) {
  // A symbol is a valid slot target if it's a non-empty string
  // More detailed validation would require looking up the symbol
  // in the symbol table and checking its signature
  return !symbol.empty();
}

// =============================================================================
// VTable References Implementation
// =============================================================================

std::vector<std::string> CollectVTableRefs(const IRDecls& decls) {
  SPEC_RULE("VTableRefs");

  std::set<std::string> refs;
  for (const auto& decl : decls) {
    if (const auto* proc = std::get_if<ProcIR>(&decl)) {
      CollectVTableRefsFromIR(refs, proc->body);
    }
  }

  return {refs.begin(), refs.end()};
}

std::vector<std::string> CollectVTableRefs(const IRDecls& decls,
                                           const LowerCtx& ctx) {
  SPEC_RULE("VTableRefs");

  std::set<std::string> refs;
  for (const auto& symbol : CollectVTableRefs(decls)) {
    refs.insert(symbol);
  }
  for (const auto& [symbol, _info] : ctx.values.required_vtables) {
    refs.insert(symbol);
  }
  return {refs.begin(), refs.end()};
}

std::vector<std::string> CollectVTableRefs(const LowerCtx& ctx) {
  SPEC_RULE("VTableRefs");

  std::vector<std::string> vtable_refs;
  vtable_refs.reserve(ctx.values.required_vtables.size());
  for (const auto& [symbol, _info] : ctx.values.required_vtables) {
    vtable_refs.push_back(symbol);
  }
  std::sort(vtable_refs.begin(), vtable_refs.end());
  return vtable_refs;
}

bool IsVTableSymbol(const std::string& symbol) {
  // Check if the symbol starts with the vtable prefix
  // or contains "vtable" in the mangled name
  if (symbol.find(kVTablePrefix) == 0) {
    return true;
  }
  if (symbol.find("vtable") != std::string::npos) {
    return true;
  }
  return false;
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorVTableEmitRules() {
  // VTable generation
  SPEC_RULE("VTable-Order");
  SPEC_RULE("VTable-Slot");

  // Symbol resolution
  SPEC_RULE("DispatchSym-Impl");
  SPEC_RULE("DispatchSym-Default-None");
  SPEC_RULE("DispatchSym-Default-Mismatch");
  SPEC_RULE("DropGlueSym");

  // LLVM emission
  SPEC_RULE("EmitVTable");
  SPEC_RULE("EmitVTable-Header");
  SPEC_RULE("EmitVTable-Slots");
  SPEC_RULE("EmitVTable-Err");
  SPEC_RULE("LowerIRDecl-VTable");

  // Validation
  SPEC_RULE("ValidateVTable");
  SPEC_RULE("VTableRefs");
}

}  // namespace cursive::codegen
