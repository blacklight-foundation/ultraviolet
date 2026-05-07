// =================================================================
// File: 03_analysis/types/type_wf.h
// Construct: Type Well-Formedness Checking
// Spec Section: 5.2.12
// Spec Rules: WF-Prim, WF-Perm, WF-Tuple, WF-Array, WF-Slice,
//             WF-Union, WF-Func, WF-Path, WF-Dynamic, WF-Opaque,
//             WF-Refine-Type, WF-String, WF-Bytes, WF-Ptr, WF-RawPtr,
//             WF-ModalState
// =================================================================
#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

// Result of type well-formedness check
struct TypeWfResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

// Check if a type is well-formed
TypeWfResult TypeWF(const ScopeContext& ctx, const TypeRef& type);

}  // namespace cursive::analysis
