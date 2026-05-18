// =============================================================================
// MIGRATION MAPPING: llvm_attr.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.12 LLVM 21 Backend Requirements
//   - LLVM-PtrAttrs-* rules (attribute emission)
//   - Pointer attribute inference from type
//   - Function attribute mapping
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/llvm/llvm_attrs.cpp
//   - Lines 1-100: Attribute helper functions
//   - Lines 15-61: AddArgAttrs, AddPtrAttrs
//   - Lines 46-61: AddArgAttrs for alias/readonly
//   - Lines 63-98: AddPtrAttrs for nonnull/noundef/dereferenceable
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_attr.h
//   - ultraviolet/include/05_codegen/llvm/llvm_emit.h (LLVMEmitter)
//   - ultraviolet/include/04_analysis/layout/layout.h (SizeOf, AlignOf)
//   - llvm/IR/Attributes.h
//   - llvm/IR/Function.h
// =============================================================================

#include "05_codegen/llvm/llvm_attr.h"

#include "00_core/spec_trace.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/llvm/llvm_emit.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"

#include <algorithm>

namespace ultraviolet::codegen {

namespace {

bool AttrSpecEquals(const AttrSpec& lhs, const AttrSpec& rhs) {
  return lhs.kind == rhs.kind &&
         lhs.value == rhs.value &&
         lhs.type == rhs.type;
}

}  // namespace

// =============================================================================
// §6.12.3 LLVM Attribute Mapping
// =============================================================================

// -----------------------------------------------------------------------------
// Permission and Pointer State Extraction
// -----------------------------------------------------------------------------

analysis::TypeRef StripPerm(const analysis::TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    SPEC_RULE("PtrStateOf-Perm");
    return StripPerm(perm->base);
  }
  return type;
}

analysis::Permission PermOf(const analysis::TypeRef& type) {
  if (!type) {
    return analysis::Permission::Shared;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return perm->perm;
  }
  return analysis::Permission::Shared;
}

std::optional<analysis::PtrState> PtrStateOf(const analysis::TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
    return ptr->state;
  }
  return std::nullopt;
}

// -----------------------------------------------------------------------------
// Attribute Building
// -----------------------------------------------------------------------------

AttrSet ComputePtrAttrs(const analysis::TypeRef& type, const LowerCtx* ctx) {
  AttrSet attrs;

  const auto stripped = StripPerm(type);
  if (!stripped) {
    return attrs;
  }

  const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node);
  if (ptr) {
    // Check pointer state
    if (!ptr->state.has_value() || *ptr->state != analysis::PtrState::Valid) {
      SPEC_RULE("LLVM-PtrAttrs-Other");
      return attrs;  // Empty for non-Valid pointers
    }
  } else if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
    SPEC_RULE("LLVM-PtrAttrs-RawPtr");
    return attrs;  // Empty for raw pointers
  } else {
    return attrs;  // Not a pointer type
  }

  // Valid pointer - add attributes
  SPEC_RULE("LLVM-PtrAttrs-Valid");
  attrs.push_back({AttrKind::NonNull, 0});
  attrs.push_back({AttrKind::NoUndef, 0});

  // Add size and alignment attributes if context available
  if (ctx && ctx->sigma) {
    analysis::ScopeContext scope;
    scope.sigma = *ctx->sigma;
    scope.sigma_source = ctx->sigma;
    scope.current_module = ctx->module_path;

    const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, ptr->element);
    const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, ptr->element);

    if (size.has_value()) {
      attrs.push_back({AttrKind::Dereferenceable, *size});
    }
    if (align.has_value() && *align > 0) {
      attrs.push_back({AttrKind::Alignment, *align});
    }
  }

  return attrs;
}

AttrSet ComputeArgAttrs(const analysis::TypeRef& type) {
  AttrSet attrs;

  const auto stripped = StripPerm(type);
  if (!stripped) {
    return attrs;
  }

  // Check if it's a pointer or function type
  if (std::holds_alternative<analysis::TypePtr>(stripped->node) ||
      std::holds_alternative<analysis::TypeFunc>(stripped->node)) {
    SPEC_RULE("LLVM-ArgAttrs-Ptr");

    const auto perm = PermOf(type);
    if (perm == analysis::Permission::Unique) {
      attrs.push_back({AttrKind::NoAlias, 0});
    }
    if (perm == analysis::Permission::Const) {
      attrs.push_back({AttrKind::ReadOnly, 0});
    }
  } else if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
    SPEC_RULE("LLVM-ArgAttrs-RawPtr");
    // Empty for raw pointers
  } else {
    SPEC_RULE("LLVM-ArgAttrs-NonPtr");
    // Empty for non-pointer types
  }

  return attrs;
}

bool NoEscapeParam(std::string_view param_name) {
  // Chapter 24 models nocapture as an optional strengthening. Until escape
  // analysis is threaded into LLVM attribute emission, stay conservative.
  (void)param_name;
  return false;
}

AttrSet ComputeArgAttrsExt(const std::string& param_name,
                           const analysis::TypeRef& type) {
  // Start with basic arg attrs
  AttrSet attrs = ComputeArgAttrs(type);

  if (NoEscapeParam(param_name)) {
    attrs.push_back({AttrKind::NoCapture, 0});
  }

  return attrs;
}

// -----------------------------------------------------------------------------
// LLVM AttrBuilder Integration
// -----------------------------------------------------------------------------

void AddAttrSetToBuilder(llvm::AttrBuilder& builder,
                         const AttrSet& attrs) {
  for (const auto& attr : attrs) {
    switch (attr.kind) {
      case AttrKind::NonNull:
        builder.addAttribute(llvm::Attribute::NonNull);
        break;
      case AttrKind::NoUndef:
        builder.addAttribute(llvm::Attribute::NoUndef);
        break;
      case AttrKind::NoAlias:
        builder.addAttribute(llvm::Attribute::NoAlias);
        break;
      case AttrKind::ReadOnly:
        builder.addAttribute(llvm::Attribute::ReadOnly);
        break;
      case AttrKind::Dereferenceable:
        builder.addDereferenceableAttr(attr.value);
        break;
      case AttrKind::Alignment:
        if (attr.value > 0) {
          builder.addAlignmentAttr(llvm::Align(attr.value));
        }
        break;
      case AttrKind::StructRet:
        if (attr.type) {
          builder.addStructRetAttr(attr.type);
        }
        break;
      case AttrKind::NoCapture:
        builder.addCapturesAttr(llvm::CaptureInfo::none());
        break;
      case AttrKind::NoReturn:
        builder.addAttribute(llvm::Attribute::NoReturn);
        break;
      case AttrKind::NoUnwind:
        builder.addAttribute(llvm::Attribute::NoUnwind);
        break;
    }
  }
}

void AddArgAttrsToBuilder(llvm::AttrBuilder& builder,
                          const analysis::TypeRef& type) {
  AddAttrSetToBuilder(builder, ComputeArgAttrs(type));
}

void AddPtrAttrsToBuilder(llvm::AttrBuilder& builder,
                          const analysis::TypeRef& type,
                          const LowerCtx* ctx) {
  AddAttrSetToBuilder(builder, ComputePtrAttrs(type, ctx));
}

// -----------------------------------------------------------------------------
// Function Attribute Management
// -----------------------------------------------------------------------------

AttrSet DeclAttrs(std::string_view symbol) {
  AttrSet attrs;

  // All functions get nounwind (no exceptions in Ultraviolet)
  attrs.push_back({AttrKind::NoUnwind, 0});

  // Panic function gets noreturn
  if (symbol.find("panic") != std::string_view::npos ||
      symbol.find("abort") != std::string_view::npos) {
    attrs.push_back({AttrKind::NoReturn, 0});
  }

  return attrs;
}

bool DeclAttrsOk(std::string_view symbol, const AttrSet& attrs) {
  bool has_nounwind = false;
  bool has_noreturn = false;

  for (const auto& attr : attrs) {
    if (attr.kind == AttrKind::NoUnwind) {
      has_nounwind = true;
    }
    if (attr.kind == AttrKind::NoReturn) {
      has_noreturn = true;
    }
  }

  // All declarations must have nounwind
  if (!has_nounwind) {
    return false;
  }

  // Panic symbols must have noreturn
  bool is_panic = symbol.find("panic") != std::string_view::npos ||
                  symbol.find("abort") != std::string_view::npos;
  if (is_panic && !has_noreturn) {
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------------
// LLVMEmitter Attribute Wrapper
// -----------------------------------------------------------------------------

void LLVMEmitter::AddPtrAttributes(llvm::Function *func,
                                   unsigned arg_idx,
                                   analysis::TypeRef type) {
  SPEC_RULE("LLVM-PtrAttrs-Valid");
  SPEC_RULE("LLVM-ArgAttrs-Ptr");

  if (!func || !type) {
    return;
  }

  llvm::AttrBuilder b(context_);
  AttrSet attrs = ComputeArgAttrs(type);
  AttrSet ptr_attrs = ComputePtrAttrs(type, current_ctx_);
  for (const auto& attr : ptr_attrs) {
    if (std::find_if(attrs.begin(), attrs.end(),
                     [&](const AttrSpec& existing) {
                       return AttrSpecEquals(existing, attr);
                     }) == attrs.end()) {
      attrs.push_back(attr);
    }
  }
  AddAttrSetToBuilder(b, attrs);

  if (b.hasAttributes()) {
    func->addParamAttrs(arg_idx, b);
  }
}

}  // namespace ultraviolet::codegen


