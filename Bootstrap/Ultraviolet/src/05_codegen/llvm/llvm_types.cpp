// =============================================================================
// MIGRATION MAPPING: llvm_types.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.12.3 LLVMTy Judgment (lines 17416-17560)
//   - LLVMTy-Prim rule (lines 17456-17459)
//   - LLVMTy-Perm rule (lines 17461-17464)
//   - LLVMTy-Ptr rule (lines 17466-17469)
//   - LLVMTy-RawPtr rule (lines 17471-17474)
//   - LLVMTy-Func rule (lines 17476-17479)
//   - LLVMTy-Record rule (lines 17493-17496)
//   - LLVMTy-Tuple rule (lines 17498-17501)
//   - LLVMTy-Array rule (lines 17503-17506)
//   - LLVMTy-Slice, LLVMTy-Union, LLVMTy-Modal, etc.
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/llvm/llvm_types.cpp
//   - Lines 1-100: Type mapping helpers
//   - Lines 19-39: BuildScope, IsUnitType helpers
//   - Lines 41-50: AlignUp helper
//   - Lines 52-98: AppendPad, AppendStructElems
//   - Lines 100+: GetLLVMType implementation
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_types.h
//   - ultraviolet/include/05_codegen/llvm/llvm_emit.h (LLVMEmitter)
//   - ultraviolet/include/04_analysis/layout/layout.h (SizeOf, AlignOf, RecordLayoutOf)
//   - llvm/IR/DerivedTypes.h
//   - llvm/IR/Type.h
// =============================================================================

#include "05_codegen/llvm/llvm_types.h"

#include "00_core/spec_trace.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/emit/internal_helpers.h"
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

#include <algorithm>
#include <cstdint>

namespace ultraviolet::codegen {

namespace {

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align) {
  if (align == 0) {
    return value;
  }
  const std::uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

bool TypePathEq(const analysis::TypePath& lhs, const analysis::TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!analysis::IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

bool TypeArgsEq(const std::vector<analysis::TypeRef>& lhs,
                const std::vector<analysis::TypeRef>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto equiv = analysis::TypeEquiv(lhs[i], rhs[i]);
    if (!equiv.ok || !equiv.equiv) {
      return false;
    }
  }
  return true;
}

bool SameNominalType(const analysis::TypeRef& lhs,
                     const analysis::TypeRef& rhs) {
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs.get() == rhs.get()) {
    return true;
  }
  if (const auto* lhs_state = std::get_if<analysis::TypeModalState>(&lhs->node)) {
    const auto* rhs_state = std::get_if<analysis::TypeModalState>(&rhs->node);
    return rhs_state &&
           TypePathEq(lhs_state->path, rhs_state->path) &&
           analysis::IdEq(lhs_state->state, rhs_state->state) &&
           TypeArgsEq(lhs_state->generic_args, rhs_state->generic_args);
  }

  const auto* lhs_path = analysis::AppliedTypePath(*lhs);
  const auto* rhs_path = analysis::AppliedTypePath(*rhs);
  if (!lhs_path || !rhs_path || !TypePathEq(*lhs_path, *rhs_path)) {
    return false;
  }
  static const std::vector<analysis::TypeRef> empty_args;
  const auto* lhs_args = analysis::AppliedTypeArgs(*lhs);
  const auto* rhs_args = analysis::AppliedTypeArgs(*rhs);
  return TypeArgsEq(lhs_args ? *lhs_args : empty_args,
                    rhs_args ? *rhs_args : empty_args);
}

std::optional<analysis::TypeSubst> BuildTypeSubstitution(
    const std::optional<ast::GenericParams>& generic_params,
    const std::vector<analysis::TypeRef>& generic_args) {
  if (!generic_params.has_value() || generic_params->params.empty()) {
    return analysis::TypeSubst{};
  }
  if (generic_args.size() > generic_params->params.size()) {
    return std::nullopt;
  }
  return analysis::BuildSubstitution(generic_params->params, generic_args);
}

std::optional<analysis::TypeRef> LowerTypeWithSubst(
    const analysis::ScopeContext& scope,
    const std::shared_ptr<ast::Type>& type,
    const analysis::TypeSubst& subst) {
  const auto lowered = analysis::layout::LowerTypeForLayout(scope, type);
  if (!lowered.has_value()) {
    return std::nullopt;
  }
  analysis::TypeRef out = *lowered;
  if (!subst.empty()) {
    out = analysis::InstantiateType(out, subst);
  }
  return out;
}

const std::vector<analysis::TypeRef>& AppliedArgsOrEmpty(
    const analysis::TypeRef& type) {
  static const std::vector<analysis::TypeRef> empty_args;
  if (!type) {
    return empty_args;
  }
  if (const auto* args = analysis::AppliedTypeArgs(*type)) {
    return *args;
  }
  return empty_args;
}

bool TypeContainsByValueImpl(const analysis::ScopeContext& scope,
                             const analysis::TypeRef& candidate,
                             const analysis::TypeRef& needle,
                             std::vector<std::string>& active) {
  if (!candidate || !needle) {
    return false;
  }
  if (SameNominalType(candidate, needle)) {
    return true;
  }

  const std::string key = analysis::TypeToString(candidate);
  if (std::find(active.begin(), active.end(), key) != active.end()) {
    return false;
  }
  active.push_back(key);
  struct PopGuard {
    std::vector<std::string>& active;
    ~PopGuard() { active.pop_back(); }
  } guard{active};

  auto contains = [&](const analysis::TypeRef& type) {
    return TypeContainsByValueImpl(scope, type, needle, active);
  };

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, analysis::TypePerm>) {
          return contains(node.base);
        } else if constexpr (std::is_same_v<T, analysis::TypeRefine>) {
          return contains(node.base);
        } else if constexpr (std::is_same_v<T, analysis::TypeUnion>) {
          for (const auto& member : node.members) {
            if (contains(member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, analysis::TypeTuple>) {
          for (const auto& element : node.elements) {
            if (contains(element)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, analysis::TypeArray>) {
          return contains(node.element);
        } else if constexpr (std::is_same_v<T, analysis::TypeModalState>) {
          const ast::ModalDecl* modal_decl =
              analysis::LookupModalDecl(scope, node.path);
          if (!modal_decl) {
            return false;
          }
          const auto subst =
              BuildTypeSubstitution(modal_decl->generic_params, node.generic_args);
          if (!subst.has_value()) {
            return false;
          }
          for (const auto& state : modal_decl->states) {
            if (!analysis::IdEq(state.name, node.state)) {
              continue;
            }
            for (const auto& member : state.members) {
              const auto* field = std::get_if<ast::StateFieldDecl>(&member);
              if (!field) {
                continue;
              }
              const auto lowered = LowerTypeWithSubst(scope, field->type, *subst);
              if (lowered.has_value() && contains(*lowered)) {
                return true;
              }
            }
            return false;
          }
          return false;
        } else if constexpr (std::is_same_v<T, analysis::TypePathType> ||
                             std::is_same_v<T, analysis::TypeApply>) {
          const analysis::TypeDecl* decl =
              analysis::LookupTypeDecl(scope, node.path);
          if (!decl) {
            return false;
          }
          const auto& args = AppliedArgsOrEmpty(candidate);
          if (const auto* alias = std::get_if<ast::TypeAliasDecl>(decl)) {
            const auto subst = BuildTypeSubstitution(alias->generic_params, args);
            if (!subst.has_value()) {
              return false;
            }
            const auto lowered = LowerTypeWithSubst(scope, alias->type, *subst);
            return lowered.has_value() && contains(*lowered);
          }
          if (const auto* record = std::get_if<ast::RecordDecl>(decl)) {
            const auto subst = BuildTypeSubstitution(record->generic_params, args);
            if (!subst.has_value()) {
              return false;
            }
            for (const auto& member : record->members) {
              const auto* field = std::get_if<ast::FieldDecl>(&member);
              if (!field) {
                continue;
              }
              const auto lowered = LowerTypeWithSubst(scope, field->type, *subst);
              if (lowered.has_value() && contains(*lowered)) {
                return true;
              }
            }
            return false;
          }
          if (const auto* modal = std::get_if<ast::ModalDecl>(decl)) {
            const auto subst = BuildTypeSubstitution(modal->generic_params, args);
            if (!subst.has_value()) {
              return false;
            }
            for (const auto& state : modal->states) {
              for (const auto& member : state.members) {
                const auto* field = std::get_if<ast::StateFieldDecl>(&member);
                if (!field) {
                  continue;
                }
                const auto lowered = LowerTypeWithSubst(scope, field->type, *subst);
                if (lowered.has_value() && contains(*lowered)) {
                  return true;
                }
              }
            }
          }
          return false;
        } else {
          return false;
        }
      },
      candidate->node);
}

bool TypeContainsByValue(const analysis::ScopeContext& scope,
                         const analysis::TypeRef& candidate,
                         const analysis::TypeRef& needle) {
  std::vector<std::string> active;
  return TypeContainsByValueImpl(scope, candidate, needle, active);
}

void AppendPad(std::vector<llvm::Type*>& elems,
               llvm::LLVMContext& ctx,
               std::uint64_t pad) {
  if (pad == 0) {
    return;
  }
  elems.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx), pad));
}

std::vector<std::string>& ActiveLLVMTypeQueries() {
  static thread_local std::vector<std::string> queries;
  return queries;
}

class ScopedLLVMTypeQuery {
 public:
  explicit ScopedLLVMTypeQuery(const analysis::TypeRef& type)
      : key_(analysis::TypeToString(type)) {
    auto& queries = ActiveLLVMTypeQueries();
    recursive_ = std::find(queries.begin(), queries.end(), key_) != queries.end();
    if (!recursive_) {
      queries.push_back(key_);
    }
  }

  ScopedLLVMTypeQuery(const ScopedLLVMTypeQuery&) = delete;
  ScopedLLVMTypeQuery& operator=(const ScopedLLVMTypeQuery&) = delete;

  ~ScopedLLVMTypeQuery() {
    if (!recursive_) {
      auto& queries = ActiveLLVMTypeQueries();
      if (!queries.empty() && queries.back() == key_) {
        queries.pop_back();
      }
    }
  }

  bool recursive() const { return recursive_; }

 private:
  std::string key_;
  bool recursive_ = false;
};

}  // namespace

// =============================================================================
// §6.12.2 Opaque Pointer Model (LLVM 21)
// =============================================================================

llvm::PointerType* GetOpaquePointerType(llvm::LLVMContext& context,
                                        unsigned address_space) {
  SPEC_DEF("OpaquePointerModel", "§6.12.2");
  return llvm::PointerType::get(context, address_space);
}

// =============================================================================
// §6.12.8 LLVM Type Mapping
// =============================================================================

llvm::StructType* GetZSTType(llvm::LLVMContext& context) {
  return llvm::StructType::get(context, {});
}

// -----------------------------------------------------------------------------
// Primitive Type Mapping
// -----------------------------------------------------------------------------

llvm::Type* GetPrimType(llvm::LLVMContext& context, std::string_view name) {
  SPEC_RULE("LLVMTy-Prim");

  if (name == "bool") {
    return llvm::Type::getInt8Ty(context);
  }
  if (name == "char") {
    return llvm::Type::getInt32Ty(context);
  }
  if (name == "i8" || name == "u8") {
    return llvm::Type::getInt8Ty(context);
  }
  if (name == "i16" || name == "u16") {
    return llvm::Type::getInt16Ty(context);
  }
  if (name == "i32" || name == "u32") {
    return llvm::Type::getInt32Ty(context);
  }
  if (name == "i64" || name == "u64" || name == "usize" || name == "isize") {
    return llvm::Type::getInt64Ty(context);
  }
  if (name == "i128" || name == "u128") {
    return llvm::Type::getInt128Ty(context);
  }
  if (name == "f16") {
    return llvm::Type::getHalfTy(context);
  }
  if (name == "f32") {
    return llvm::Type::getFloatTy(context);
  }
  if (name == "f64") {
    return llvm::Type::getDoubleTy(context);
  }
  if (name == "unit" || name == "()" || name == "never" || name == "!") {
    return GetZSTType(context);  // Zero-sized type
  }

  // Default fallback
  return llvm::Type::getInt8Ty(context);
}

bool IsValidPrimName(std::string_view name) {
  return name == "bool" || name == "char" ||
         name == "i8" || name == "u8" ||
         name == "i16" || name == "u16" ||
         name == "i32" || name == "u32" ||
         name == "i64" || name == "u64" ||
         name == "i128" || name == "u128" ||
         name == "isize" || name == "usize" ||
         name == "f16" || name == "f32" || name == "f64" ||
         name == "()" || name == "unit" ||
         name == "!" || name == "never";
}

// -----------------------------------------------------------------------------
// Aggregate Type Construction
// -----------------------------------------------------------------------------

llvm::StructType* CreateStructType(llvm::LLVMContext& context,
                                   const std::vector<llvm::Type*>& elements,
                                   bool is_packed) {
  return llvm::StructType::get(context, elements, is_packed);
}

llvm::ArrayType* CreateArrayType(llvm::Type* element_type, std::uint64_t count) {
  return llvm::ArrayType::get(element_type, count);
}

llvm::Type* CreatePaddingType(llvm::LLVMContext& context, std::uint64_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
  return llvm::ArrayType::get(llvm::Type::getInt8Ty(context), bytes);
}

// -----------------------------------------------------------------------------
// ::ultraviolet::analysis::layout::Layout-Aware Struct Construction
// -----------------------------------------------------------------------------

std::vector<llvm::Type*> ComputeStructElements(
    LLVMEmitter& emitter,
    const std::vector<analysis::TypeRef>& fields,
    const std::vector<std::uint64_t>& offsets,
    std::uint64_t total_size,
    std::uint64_t required_align) {
  std::vector<llvm::Type*> elems;
  llvm::LLVMContext& ctx = emitter.GetContext();

  if (fields.size() != offsets.size()) {
    return elems;
  }

  std::uint64_t prev_end = 0;
  std::uint64_t natural_align = 1;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const std::uint64_t offset = offsets[i];
    if (offset > prev_end) {
      AppendPad(elems, ctx, offset - prev_end);
    }

    elems.push_back(emitter.GetLLVMType(fields[i]));

    // Get the scope context from emitter
    if (emitter.GetCurrentCtx() && emitter.GetCurrentCtx()->sigma) {
      analysis::ScopeContext scope;
      scope.sigma = *emitter.GetCurrentCtx()->sigma;
      scope.current_module = emitter.GetCurrentCtx()->module_path;
      const auto field_size = ::ultraviolet::analysis::layout::SizeOf(scope, fields[i]);
      const auto field_align = ::ultraviolet::analysis::layout::AlignOf(scope, fields[i]);
      if (field_size.has_value()) {
        prev_end = offset + *field_size;
      } else {
        prev_end = offset;
      }
      if (field_align.has_value()) {
        natural_align = std::max(natural_align, *field_align);
      }
    } else {
      prev_end = offset;
    }
  }

  if (total_size > prev_end) {
    AppendPad(elems, ctx, total_size - prev_end);
  }

  if (required_align > natural_align) {
    if (llvm::Type* marker = GetAlignmentMarkerType(ctx, required_align)) {
      elems.push_back(llvm::ArrayType::get(marker, 0));
    }
  }

  return elems;
}

std::optional<LayoutLLVMRecord> ComputeLayoutLLVMRecord(
    LLVMEmitter& emitter,
    const analysis::ScopeContext& scope,
    const analysis::TypeRef& aggregate_type,
    const std::vector<analysis::TypeRef>& fields,
    const analysis::layout::RecordLayoutOptions& options) {
  LayoutLLVMRecord out;
  out.fields.reserve(fields.size());

  std::uint64_t offset = 0;
  std::uint64_t max_align = 1;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const bool recursive_indirect =
        aggregate_type && TypeContainsByValue(scope, fields[i], aggregate_type);
    std::optional<analysis::layout::Layout> field_layout;
    llvm::Type* field_ty = nullptr;
    if (recursive_indirect) {
      field_layout = analysis::layout::Layout{
          analysis::layout::PtrSize(scope),
          analysis::layout::PtrAlign(scope)};
      field_ty = emitter.GetOpaquePtr();
    } else {
      field_layout = analysis::layout::LayoutOf(scope, fields[i]);
      field_ty = emitter.GetLLVMType(fields[i]);
    }
    if (!field_layout.has_value() || !field_ty) {
      return std::nullopt;
    }

    const std::uint64_t field_align = options.packed ? 1 : field_layout->align;
    if (i != 0) {
      offset = AlignUp(offset, field_align);
    }

    LayoutLLVMField field;
    field.source_type = fields[i];
    field.llvm_type = field_ty;
    field.offset = offset;
    field.size = field_layout->size;
    field.align = field_align;
    field.recursive_indirect = recursive_indirect;
    out.fields.push_back(field);

    max_align = std::max(max_align, field_align);
    offset += field_layout->size;
  }

  if (options.packed) {
    max_align = 1;
  } else if (options.min_align.has_value()) {
    max_align = std::max(max_align, *options.min_align);
  }
  const std::uint64_t computed_size = AlignUp(offset, max_align);
  out.size = computed_size;
  out.align = max_align;

  if (aggregate_type) {
    if (const auto aggregate_layout = analysis::layout::LayoutOf(scope, aggregate_type);
        aggregate_layout.has_value() &&
        aggregate_layout->size >= out.size) {
      out.size = aggregate_layout->size;
      out.align = std::max(out.align, aggregate_layout->align);
    }
  }
  return out;
}

std::vector<llvm::Type*> ComputeStructElements(
    LLVMEmitter& emitter,
    const LayoutLLVMRecord& layout) {
  std::vector<llvm::Type*> elems;
  llvm::LLVMContext& ctx = emitter.GetContext();
  std::uint64_t prev_end = 0;
  for (const auto& field : layout.fields) {
    if (field.offset > prev_end) {
      AppendPad(elems, ctx, field.offset - prev_end);
    }
    elems.push_back(field.llvm_type);
    prev_end = field.offset + field.size;
  }
  if (layout.size > prev_end) {
    AppendPad(elems, ctx, layout.size - prev_end);
  }
  if (layout.align > 1) {
    if (llvm::Type* marker = GetAlignmentMarkerType(ctx, layout.align)) {
      elems.push_back(llvm::ArrayType::get(marker, 0));
    }
  }
  return elems;
}

std::vector<llvm::Type*> ComputeTaggedElements(
    LLVMEmitter& emitter,
    const analysis::TypeRef& disc_type,
    std::uint64_t payload_size,
    std::uint64_t payload_align,
    std::uint64_t total_size) {
  llvm::LLVMContext& ctx = emitter.GetContext();
  std::vector<llvm::Type*> elems;

  // Add discriminant type
  elems.push_back(emitter.GetLLVMType(disc_type));

  // Compute discriminant size
  std::uint64_t disc_size = 1;
  if (emitter.GetCurrentCtx() && emitter.GetCurrentCtx()->sigma) {
    analysis::ScopeContext scope;
    scope.sigma = *emitter.GetCurrentCtx()->sigma;
    scope.current_module = emitter.GetCurrentCtx()->module_path;
    const auto size_opt = ::ultraviolet::analysis::layout::SizeOf(scope, disc_type);
    if (size_opt.has_value()) {
      disc_size = *size_opt;
    }
  }

  // Add padding between discriminant and payload
  const std::uint64_t payload_off = AlignUp(disc_size, payload_align);
  const std::uint64_t pad_mid = payload_off - disc_size;
  AppendPad(elems, ctx, pad_mid);

  // Add payload blob
  llvm::Type* byte = llvm::Type::getInt8Ty(ctx);
  elems.push_back(llvm::ArrayType::get(byte, payload_size));

  // Add tail padding
  const std::uint64_t payload_end = payload_off + payload_size;
  if (total_size > payload_end) {
    AppendPad(elems, ctx, total_size - payload_end);
  }

  if (payload_align > 1) {
    if (llvm::Type* marker = GetAlignmentMarkerType(ctx, payload_align)) {
      elems.push_back(llvm::ArrayType::get(marker, 0));
    }
  }

  return elems;
}

// -----------------------------------------------------------------------------
// Composite Type Helpers
// -----------------------------------------------------------------------------

llvm::StructType* GetSliceType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-Slice");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  llvm::Type* len_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {ptr_ty, len_ty});
}

llvm::StructType* GetStringViewType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-StringView");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  llvm::Type* len_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {ptr_ty, len_ty});
}

llvm::StructType* GetBytesViewType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-BytesView");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  llvm::Type* len_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {ptr_ty, len_ty});
}

llvm::StructType* GetStringManagedType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-StringManaged");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  llvm::Type* len_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {ptr_ty, len_ty, len_ty});
}

llvm::StructType* GetBytesManagedType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-BytesManaged");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  llvm::Type* len_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {ptr_ty, len_ty, len_ty});
}

llvm::StructType* GetRangeType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-Range");
  llvm::Type* kind_ty = llvm::Type::getInt8Ty(context);
  llvm::Type* bound_ty = llvm::Type::getInt64Ty(context);
  return llvm::StructType::get(context, {kind_ty, bound_ty, bound_ty});
}

llvm::StructType* GetDynamicType(llvm::LLVMContext& context) {
  SPEC_RULE("LLVMTy-Dynamic");
  llvm::Type* ptr_ty = GetOpaquePointerType(context);
  return llvm::StructType::get(context, {ptr_ty, ptr_ty});
}

// -----------------------------------------------------------------------------
// Type Size and Alignment
// -----------------------------------------------------------------------------

std::uint64_t GetTypeSize(llvm::Type* type) {
  // Note: This is a simplified implementation
  // For accurate sizes, use DataLayout::getTypeAllocSize
  if (!type) {
    return 0;
  }
  if (type->isIntegerTy()) {
    return (type->getIntegerBitWidth() + 7) / 8;
  }
  if (type->isHalfTy()) {
    return 2;
  }
  if (type->isFloatTy()) {
    return 4;
  }
  if (type->isDoubleTy()) {
    return 8;
  }
  if (type->isPointerTy()) {
    return 8;  // x86_64
  }
  // For complex types, would need DataLayout
  return 0;
}

std::uint64_t GetTypeAlignment(llvm::Type* type) {
  // Note: This is a simplified implementation
  // For accurate alignment, use DataLayout::getABITypeAlign
  if (!type) {
    return 1;
  }
  if (type->isIntegerTy()) {
    std::uint64_t size = (type->getIntegerBitWidth() + 7) / 8;
    return std::min(size, static_cast<std::uint64_t>(8));
  }
  if (type->isHalfTy()) {
    return 2;
  }
  if (type->isFloatTy()) {
    return 4;
  }
  if (type->isDoubleTy()) {
    return 8;
  }
  if (type->isPointerTy()) {
    return 8;  // x86_64
  }
  return 1;
}

llvm::Type* GetIntTypeForSize(llvm::LLVMContext& context, std::uint64_t size) {
  switch (size) {
    case 1:
      return llvm::Type::getInt8Ty(context);
    case 2:
      return llvm::Type::getInt16Ty(context);
    case 4:
      return llvm::Type::getInt32Ty(context);
    case 8:
      return llvm::Type::getInt64Ty(context);
    default:
      return nullptr;
  }
}

llvm::Type* GetAlignmentMarkerType(llvm::LLVMContext& context, std::uint64_t align) {
  switch (align) {
    case 1:
      return llvm::Type::getInt8Ty(context);
    case 2:
      return llvm::Type::getInt16Ty(context);
    case 4:
      return llvm::Type::getInt32Ty(context);
    case 8:
      return llvm::Type::getInt64Ty(context);
    case 16:
      return llvm::Type::getInt128Ty(context);
    default:
      return nullptr;
  }
}

// -----------------------------------------------------------------------------
// Tagged Type Helpers
// -----------------------------------------------------------------------------

llvm::StructType* CreateTaggedBlobType(llvm::LLVMContext& context,
                                       std::uint64_t size,
                                       std::uint64_t align) {
  if (size == 0) {
    return llvm::StructType::get(context, {});
  }

  llvm::Type* byte = llvm::Type::getInt8Ty(context);
  llvm::Type* bytes = llvm::ArrayType::get(byte, size);
  std::vector<llvm::Type*> fields;
  fields.push_back(bytes);

  if (align > 1) {
    if (llvm::Type* marker = GetAlignmentMarkerType(context, align)) {
      fields.push_back(llvm::ArrayType::get(marker, 0));
    }
  }

  return llvm::StructType::get(context, fields, /*isPacked=*/false);
}

llvm::Type* CreateTaggedABIType(llvm::LLVMContext& context,
                                std::uint64_t size,
                                std::uint64_t align) {
  // Use integer type if size matches standard int sizes for better ABI
  if (llvm::Type* int_ty = GetIntTypeForSize(context, size)) {
    return int_ty;
  }
  return CreateTaggedBlobType(context, size, align);
}

llvm::StructType* CreateTaggedStructType(LLVMEmitter& emitter,
                                         const analysis::TypeRef& disc_type,
                                         std::uint64_t payload_size,
                                         std::uint64_t payload_align,
                                         std::uint64_t total_size) {
  std::vector<llvm::Type*> elems = ComputeTaggedElements(
      emitter, disc_type, payload_size, payload_align, total_size);
  return llvm::StructType::get(emitter.GetContext(), elems, /*isPacked=*/false);
}

// -----------------------------------------------------------------------------
// Async Type ::ultraviolet::analysis::layout::Layout (§5.4.5)
// -----------------------------------------------------------------------------

llvm::Type* BuildAsyncLLVMType(LLVMEmitter& emitter,
                               const std::vector<analysis::TypeRef>& generic_args) {
  SPEC_RULE("LLVMTy-Async");

  llvm::LLVMContext& ctx = emitter.GetContext();

  // If no context, return minimal struct
  if (!emitter.GetCurrentCtx() || !emitter.GetCurrentCtx()->sigma) {
    return llvm::StructType::get(ctx, {});
  }

  analysis::ScopeContext scope;
  scope.sigma = *emitter.GetCurrentCtx()->sigma;
  scope.current_module = emitter.GetCurrentCtx()->module_path;
  scope.target_profile = emitter.GetTargetProfile();

  analysis::AsyncSig async_sig{};
  async_sig.out =
      !generic_args.empty() ? generic_args[0] : analysis::MakeTypePrim("()");
  async_sig.in =
      generic_args.size() > 1 ? generic_args[1] : analysis::MakeTypePrim("()");
  async_sig.result =
      generic_args.size() > 2 ? generic_args[2] : analysis::MakeTypePrim("()");
  async_sig.err =
      generic_args.size() > 3 ? generic_args[3] : analysis::MakeTypePrim("!");
  const auto lowered_async = ::ultraviolet::analysis::layout::LowerAsyncType(async_sig);
  const bool has_failed_state =
      lowered_async.has_value() &&
      std::find(lowered_async->states.begin(),
                lowered_async->states.end(),
                "Failed") != lowered_async->states.end();

  // Compute async layout similar to modal layout
  std::uint64_t max_payload_size = 0;
  std::uint64_t max_payload_align = 1;

  auto add_payload_layout = [&](const std::optional<::ultraviolet::analysis::layout::Layout>& layout_opt) {
    if (!layout_opt.has_value() || layout_opt->size == 0) {
      return;
    }
    max_payload_size = std::max(max_payload_size, layout_opt->size);
    max_payload_align = std::max(max_payload_align, layout_opt->align);
  };
  auto add_payload_type = [&](const analysis::TypeRef& type) {
    if (!type) {
      return;
    }
    add_payload_layout(::ultraviolet::analysis::layout::LayoutOf(scope, type));
  };
  auto is_unit_type = [](const analysis::TypeRef& type) {
    if (!type) {
      return false;
    }
    if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
      return prim->name == "()";
    }
    if (const auto* tuple = std::get_if<analysis::TypeTuple>(&type->node)) {
      return tuple->elements.empty();
    }
    return false;
  };
  auto is_never_type = [](const analysis::TypeRef& type) {
    if (!type) {
      return false;
    }
    if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
      return prim->name == "!";
    }
    return false;
  };

  // Suspended payload: { output: Out, frame: Ptr<u8> }.
  const analysis::TypeRef out_type = async_sig.out;
  const analysis::TypeRef frame_ptr = analysis::MakeTypePtr(
      analysis::MakeTypePrim("u8"),
      analysis::PtrState::Valid);
  const auto suspended_layout = ::ultraviolet::analysis::layout::RecordLayoutOf(scope, {out_type, frame_ptr});
  if (!suspended_layout.has_value()) {
    return llvm::StructType::get(ctx, {});
  }
  add_payload_layout(suspended_layout->layout);

  // Completed payload: Result (if inhabited and non-empty).
  if (async_sig.result &&
      !is_never_type(async_sig.result) &&
      !is_unit_type(async_sig.result)) {
    add_payload_type(async_sig.result);
  }

  // Failed payload: E (if inhabited and non-empty).
  if (has_failed_state &&
      async_sig.err &&
      !is_never_type(async_sig.err) &&
      !is_unit_type(async_sig.err)) {
    add_payload_type(async_sig.err);
  }

  // Runtime async frame extraction assumes suspended payload stores a hidden
  // frame pointer at byte offset 8. Keep LLVM Async payload large/aligned
  // enough for that contract, including Out = ().
  constexpr std::uint64_t kAsyncFramePtrPayloadOffset = 8;
  const auto env =
      ::ultraviolet::analysis::layout::LayoutEnvOf(emitter.GetTargetProfile());
  const std::uint64_t min_suspended_payload =
      kAsyncFramePtrPayloadOffset + ::ultraviolet::analysis::layout::PtrSize(env);
  max_payload_size = std::max(max_payload_size, min_suspended_payload);
  max_payload_align =
      std::max(max_payload_align, ::ultraviolet::analysis::layout::PtrAlign(env));

  // Build tagged struct
  const std::uint64_t disc_size = 1;
  const std::uint64_t disc_align = 1;
  const std::uint64_t align = std::max(disc_align, max_payload_align);
  const std::uint64_t size = AlignUp(disc_size + max_payload_size, align);

  auto disc_type = analysis::MakeTypePrim("u8");
  return CreateTaggedStructType(emitter, disc_type, max_payload_size, max_payload_align, size);
}

using namespace emit_detail;

  // T-LLVM-002: Opaque Pointer Model
  llvm::Type *LLVMEmitter::GetOpaquePtr()
  {
    SPEC_DEF("OpaquePointerModel", "§6.12.2");
    return llvm::PointerType::get(context_, 0);
  }

  // T-LLVM-007: Type Mapping
  llvm::Type *LLVMEmitter::GetLLVMType(analysis::TypeRef type)
  {
    if (!type)
    {
      SPEC_RULE("LLVMTy-Err");
      return llvm::Type::getVoidTy(context_);
    }

    if (type_cache_.count(type))
    {
      return type_cache_[type];
    }

    const ScopedLLVMTypeQuery type_query(type);
    if (type_query.recursive())
    {
      return GetOpaquePtr();
    }

    llvm::Type *ll_ty = nullptr;

    if (current_ctx_ && current_ctx_->sigma)
    {
      const analysis::ScopeContext &scope = BuildScope(current_ctx_);
      if (const auto async_sig = analysis::AsyncSigOf(scope, type))
      {
        SPEC_RULE("LLVMTy-Async");
        std::vector<analysis::TypeRef> async_args;
        async_args.reserve(4);
        async_args.push_back(async_sig->out);
        async_args.push_back(async_sig->in);
        async_args.push_back(async_sig->result);
        async_args.push_back(async_sig->err);
        ll_ty = BuildAsyncLLVMType(*this, async_args);
        type_cache_[type] = ll_ty;
        return ll_ty;
      }
    }

    if (const auto *prim = std::get_if<analysis::TypePrim>(&type->node))
    {
      SPEC_RULE("LLVMTy-Prim");
      ll_ty = GetPrimType(context_, prim->name);
    }
    else if (const auto *perm = std::get_if<analysis::TypePerm>(&type->node))
    {
      SPEC_RULE("LLVMTy-Perm");
      ll_ty = GetLLVMType(perm->base);
    }
    else if (const auto *refine = std::get_if<analysis::TypeRefine>(&type->node))
    {
      SPEC_RULE("LLVMTy-Refine");
      // Refinement types are representationally identical to their base type.
      ll_ty = GetLLVMType(refine->base);
    }
    else if (const auto *opaque = std::get_if<analysis::TypeOpaque>(&type->node))
    {
      SPEC_RULE("LLVMTy-Opaque");
      if (current_ctx_ && current_ctx_->sigma)
      {
        const analysis::ScopeContext &scope = BuildScope(current_ctx_);
        const auto it = scope.sigma.opaque_underlying_by_class_path.find(
            analysis::PathKeyOf(opaque->class_path));
        if (it != scope.sigma.opaque_underlying_by_class_path.end() &&
            it->second && it->second.get() != type.get()) {
          ll_ty = GetLLVMType(it->second);
        }
      }
      if (!ll_ty)
      {
        SPEC_RULE("LLVMTy-Err");
        if (current_ctx_)
        {
          current_ctx_->ReportCodegenFailure();
        }
      }
    }
    else if (std::holds_alternative<analysis::TypePtr>(type->node))
    {
      SPEC_RULE("LLVMTy-Ptr");
      ll_ty = GetOpaquePtr();
    }
    else if (std::holds_alternative<analysis::TypeRawPtr>(type->node))
    {
      SPEC_RULE("LLVMTy-RawPtr");
      ll_ty = GetOpaquePtr();
    }
    else if (std::holds_alternative<analysis::TypeFunc>(type->node))
    {
      SPEC_RULE("LLVMTy-Func");
      ll_ty = GetOpaquePtr();
    }
    else if (const auto *closure = std::get_if<analysis::TypeClosure>(&type->node))
    {
      (void)closure;
      // Runtime closure values are lowered as a pair (env_ptr, code_ptr).
      // Both components are represented as opaque pointers at LLVM level.
      SPEC_RULE("LLVMTy-Tuple");
      llvm::Type *ptr_ty = GetOpaquePtr();
      ll_ty = llvm::StructType::get(context_, {ptr_ty, ptr_ty});
    }
    else if (const auto *tuple = std::get_if<analysis::TypeTuple>(&type->node))
    {
      SPEC_RULE("LLVMTy-Tuple");
      if (!current_ctx_ || !current_ctx_->sigma)
      {
        ll_ty = llvm::StructType::get(context_, {});
      }
      else
      {
        const analysis::ScopeContext &scope = BuildScope(current_ctx_);
        const auto layout = ::ultraviolet::analysis::layout::RecordLayoutOf(scope, tuple->elements);
        std::vector<llvm::Type *> elems;
        if (layout.has_value())
        {
          elems = ComputeStructElements(*this, tuple->elements, layout->offsets, layout->layout.size);
        }
        ll_ty = llvm::StructType::get(context_, elems);
      }
    }
    else if (const auto *uni = std::get_if<analysis::TypeUnion>(&type->node))
    {
      SPEC_RULE("LLVMTy-Union");
      if (!current_ctx_ || !current_ctx_->sigma)
      {
        ll_ty = llvm::Type::getInt8Ty(context_);
      }
      else
      {
        const analysis::ScopeContext &scope = BuildScope(current_ctx_);
        const auto layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *uni);
        if (UnionDebugEnabled())
        {
          std::cerr << "[union-debug-llvmtype] union=" << analysis::TypeToString(type)
                    << " layout=" << (layout.has_value() ? "ok" : "missing");
          if (layout.has_value())
          {
            std::cerr << " niche=" << (layout->niche ? 1 : 0)
                      << " payload_size=" << layout->payload_size;
            if (layout->disc_type.has_value())
            {
              std::cerr << " disc=" << *layout->disc_type;
            }
          }
          std::cerr << "\n";
        }
        if (layout.has_value())
        {
          auto is_unit_type = [](const analysis::TypeRef &member) -> bool
          {
            if (!member)
            {
              return false;
            }
            analysis::TypeRef stripped = analysis::StripPerm(member);
            if (!stripped)
            {
              stripped = member;
            }
            const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
            return prim && prim->name == "()";
          };

          if (layout->niche)
          {
            analysis::TypeRef payload_member = nullptr;
            for (const auto &member : layout->member_list)
            {
              if (!is_unit_type(member))
              {
                payload_member = member;
                break;
              }
            }
            if (payload_member)
            {
              ll_ty = GetLLVMType(payload_member);
            }
            else
            {
              ll_ty = llvm::Type::getInt8Ty(context_);
            }
          }
          else
          {
            analysis::TypeRef disc_type = analysis::MakeTypePrim("u8");
            if (layout->disc_type.has_value())
            {
              disc_type = analysis::MakeTypePrim(*layout->disc_type);
            }
            ll_ty = CreateTaggedStructType(*this,
                                           disc_type,
                                           layout->payload_size,
                                           layout->payload_align,
                                           layout->layout.size);
          }
        }
      }
    }
    else if (const auto *nominal_path = analysis::AppliedTypePath(*type))
    {
      const auto *nominal_args = analysis::AppliedTypeArgs(*type);
      const std::vector<analysis::TypeRef> empty_args;
      const auto &generic_args = nominal_args ? *nominal_args : empty_args;

      if (IsRuntimeHandleModalPath(*nominal_path))
      {
        ll_ty = GetOpaquePtr();
      }
      else if (current_ctx_ && current_ctx_->sigma)
      {
        const analysis::ScopeContext &scope = BuildScope(current_ctx_);
        if (analysis::IsAsyncType(type))
        {
          if (const auto async_sig = analysis::GetAsyncSig(type))
          {
            std::vector<analysis::TypeRef> async_args;
            async_args.reserve(4);
            async_args.push_back(async_sig->out);
            async_args.push_back(async_sig->in);
            async_args.push_back(async_sig->result);
            async_args.push_back(async_sig->err);
            ll_ty = BuildAsyncLLVMType(*this, async_args);
          }
        }
        if (const ast::RecordDecl *record =
                analysis::LookupRecordDecl(scope, *nominal_path))
        {
          SPEC_RULE("LLVMTy-Tuple");
          analysis::TypeSubst record_subst;
          if (record->generic_params && !record->generic_params->params.empty())
          {
            if (generic_args.size() > record->generic_params->params.size())
            {
              return nullptr;
            }
            record_subst = analysis::BuildSubstitution(
                record->generic_params->params,
                generic_args);
          }
          std::vector<analysis::TypeRef> fields;
          for (const auto &member : record->members)
          {
            const auto *field = std::get_if<ast::FieldDecl>(&member);
            if (!field)
            {
              continue;
            }
            auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, field->type);
            if (lowered.has_value())
            {
              analysis::TypeRef field_type = *lowered;
              if (!record_subst.empty())
              {
                field_type = analysis::InstantiateType(field_type, record_subst);
              }
              fields.push_back(field_type);
            }
            else
            {
              fields.push_back(analysis::MakeTypePrim("u8"));
            }
          }
          const auto record_layout_options = ::ultraviolet::analysis::layout::ResolveRecordLayoutOptions(record->attrs);
          if (const auto layout = ComputeLayoutLLVMRecord(
                  *this,
                  scope,
                  type,
                  fields,
                  record_layout_options))
          {
            std::vector<llvm::Type *> elems = ComputeStructElements(*this, *layout);
            ll_ty = llvm::StructType::get(context_, elems, record_layout_options.packed);
          }
          else
          {
            ll_ty = llvm::StructType::get(context_, {});
          }
        }
        if (!ll_ty)
        {
          if (const ast::EnumDecl *enum_decl =
                  analysis::LookupEnumDecl(scope, *nominal_path))
          {
            SPEC_RULE("LLVMTy-Enum");
            if (const auto layout = ::ultraviolet::analysis::layout::EnumLayoutOf(
                    scope,
                    *enum_decl,
                    generic_args,
                    ::ultraviolet::analysis::layout::ResolveEnumLayoutOptions(enum_decl->attrs)))
            {
              analysis::TypeRef disc_type = analysis::MakeTypePrim(layout->disc_type);
              if (layout->payload_size == 0) {
                ll_ty = GetLLVMType(disc_type);
              } else {
                ll_ty = CreateTaggedStructType(*this,
                                               disc_type,
                                               layout->payload_size,
                                               layout->payload_align,
                                               layout->layout.size);
              }
            }
          }
        }
        if (!ll_ty)
        {
          if (const auto *decl = analysis::LookupTypeDecl(scope, *nominal_path))
          {
            if (const auto *alias = std::get_if<ast::TypeAliasDecl>(decl))
            {
              if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, alias->type))
              {
                analysis::TypeRef inst = *lowered;
                if (alias->generic_params &&
                    !alias->generic_params->params.empty())
                {
                  if (generic_args.size() > alias->generic_params->params.size())
                  {
                    return nullptr;
                  }
                  analysis::TypeSubst subst =
                      analysis::BuildSubstitution(alias->generic_params->params,
                                                  generic_args);
                  inst = analysis::InstantiateType(inst, subst);
                }
                ll_ty = GetLLVMType(inst);
              }
            }
          }
        }
        if (!ll_ty)
        {
          if (const auto builtin_layout =
                  analysis::LookupBuiltinModalLayout(*nominal_path))
          {
            ll_ty = CreateTaggedStructType(
                *this,
                analysis::MakeTypePrim(builtin_layout->disc_prim),
                builtin_layout->payload_size,
                builtin_layout->payload_align,
                builtin_layout->size);
          }
        }
        if (!ll_ty)
        {
          if (const auto *decl = analysis::LookupTypeDecl(scope, *nominal_path))
          {
            if (const auto *modal = std::get_if<ast::ModalDecl>(decl))
            {
              SPEC_RULE("LLVMTy-Tuple");
              if (const auto layout = ::ultraviolet::analysis::layout::ModalLayoutOf(
                      scope, *modal, generic_args))
              {
                if (layout->disc_type.has_value())
                {
                  analysis::TypeRef disc_type = analysis::MakeTypePrim(*layout->disc_type);
                  ll_ty = CreateTaggedStructType(*this,
                                                 disc_type,
                                                 layout->payload_size,
                                                 layout->payload_align,
                                                 layout->layout.size);
                }
                else
                {
                  ll_ty = llvm::ArrayType::get(
                      llvm::Type::getInt8Ty(context_),
                      static_cast<std::uint64_t>(layout->layout.size));
                }
              }
            }
          }
        }
      }
    }
    else if (const auto *modal_state = std::get_if<analysis::TypeModalState>(&type->node))
    {
      if (IsRuntimeHandleModalPath(modal_state->path))
      {
        ll_ty = GetOpaquePtr();
      }
      else
      {
        if (analysis::IsAsyncType(type))
        {
          if (const auto async_sig = analysis::GetAsyncSig(type))
          {
            std::vector<analysis::TypeRef> async_args;
            async_args.reserve(4);
            async_args.push_back(async_sig->out);
            async_args.push_back(async_sig->in);
            async_args.push_back(async_sig->result);
            async_args.push_back(async_sig->err);
            ll_ty = BuildAsyncLLVMType(*this, async_args);
          }
        }
        if (!ll_ty)
        {
          if (const auto builtin_layout =
                  analysis::LookupBuiltinModalLayout(modal_state->path))
          {
            ll_ty = CreateTaggedStructType(
                *this,
                analysis::MakeTypePrim(builtin_layout->disc_prim),
                builtin_layout->payload_size,
                builtin_layout->payload_align,
                builtin_layout->size);
          }
        }
        if (!ll_ty && current_ctx_ && current_ctx_->sigma)
        {
          const analysis::ScopeContext &scope = BuildScope(current_ctx_);
          const ast::ModalDecl *modal_decl =
              analysis::LookupModalDecl(scope, modal_state->path);
          analysis::TypeSubst modal_subst;
          if (modal_decl)
          {
            if (modal_decl->generic_params &&
                !modal_decl->generic_params->params.empty())
            {
              if (modal_state->generic_args.size() >
                  modal_decl->generic_params->params.size())
              {
                return nullptr;
              }
              modal_subst = analysis::BuildSubstitution(
                  modal_decl->generic_params->params,
                  modal_state->generic_args);
            }
            const ast::StateBlock *state_block = nullptr;
            for (const auto &state : modal_decl->states)
            {
              if (analysis::IdEq(state.name, modal_state->state))
              {
                state_block = &state;
                break;
              }
            }

            if (state_block)
            {
              SPEC_RULE("LLVMTy-Tuple");
              std::vector<analysis::TypeRef> fields;
              for (const auto &member : state_block->members)
              {
                const auto *field = std::get_if<ast::StateFieldDecl>(&member);
                if (!field)
                {
                  continue;
                }
                auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, field->type);
                if (lowered.has_value())
                {
                  analysis::TypeRef field_type = *lowered;
                  if (!modal_subst.empty())
                  {
                    field_type = analysis::InstantiateType(field_type, modal_subst);
                  }
                  fields.push_back(field_type);
                }
                else
                {
                  fields.push_back(analysis::MakeTypePrim("u8"));
                }
              }
              if (const auto layout = ComputeLayoutLLVMRecord(
                      *this,
                      scope,
                      type,
                      fields))
              {
                std::vector<llvm::Type *> elems = ComputeStructElements(*this, *layout);
                ll_ty = llvm::StructType::get(context_, elems);
              }
              else
              {
                ll_ty = llvm::StructType::get(context_, {});
              }
            }
          }

          if (!ll_ty && modal_decl)
          {
            // Fallback to general modal layout if state layout synthesis fails.
            if (const auto layout = ::ultraviolet::analysis::layout::ModalLayoutOf(scope, *modal_decl, modal_state->generic_args))
            {
              if (layout->disc_type.has_value())
              {
                analysis::TypeRef disc_type = analysis::MakeTypePrim(*layout->disc_type);
                ll_ty = CreateTaggedStructType(*this,
                                               disc_type,
                                               layout->payload_size,
                                               layout->payload_align,
                                               layout->layout.size);
              }
              else
              {
                ll_ty = llvm::ArrayType::get(
                    llvm::Type::getInt8Ty(context_),
                    static_cast<std::uint64_t>(layout->layout.size));
              }
            }
          }
        }
      }
    }
    else if (const auto *arr = std::get_if<analysis::TypeArray>(&type->node))
    {
      SPEC_RULE("LLVMTy-Array");
      llvm::Type *elem_ty = GetLLVMType(arr->element);
      ll_ty = llvm::ArrayType::get(elem_ty, arr->length);
    }
    else if (std::holds_alternative<analysis::TypeSlice>(type->node))
    {
      SPEC_RULE("LLVMTy-Slice");
      ll_ty = GetSliceType(context_);
    }
    else if (const auto *str = std::get_if<analysis::TypeString>(&type->node))
    {
      if (str->state.has_value() && *str->state == analysis::StringState::View)
      {
        SPEC_RULE("LLVMTy-StringView");
        ll_ty = GetStringViewType(context_);
      }
      else if (str->state.has_value() && *str->state == analysis::StringState::Managed)
      {
        SPEC_RULE("LLVMTy-StringManaged");
        ll_ty = GetStringManagedType(context_);
      }
      else
      {
        SPEC_RULE("LLVMTy-Modal-StringBytes");
        const std::uint64_t payload_size = 3 * ::ultraviolet::analysis::layout::kPtrSize;
        const std::uint64_t payload_align = ::ultraviolet::analysis::layout::kPtrAlign;
        const std::uint64_t payload_off =
            ((1 + payload_align - 1) / payload_align) * payload_align;
        const std::uint64_t total_size_raw = payload_off + payload_size;
        const std::uint64_t total_size =
            ((total_size_raw + payload_align - 1) / payload_align) * payload_align;
        ll_ty = CreateTaggedStructType(
            *this,
            analysis::MakeTypePrim("u8"),
            payload_size,
            payload_align,
            total_size);
      }
    }
    else if (const auto *bytes = std::get_if<analysis::TypeBytes>(&type->node))
    {
      if (bytes->state.has_value() && *bytes->state == analysis::BytesState::View)
      {
        SPEC_RULE("LLVMTy-BytesView");
        ll_ty = GetBytesViewType(context_);
      }
      else if (bytes->state.has_value() && *bytes->state == analysis::BytesState::Managed)
      {
        SPEC_RULE("LLVMTy-BytesManaged");
        ll_ty = GetBytesManagedType(context_);
      }
      else
      {
        SPEC_RULE("LLVMTy-Modal-StringBytes");
        const std::uint64_t payload_size = 3 * ::ultraviolet::analysis::layout::kPtrSize;
        const std::uint64_t payload_align = ::ultraviolet::analysis::layout::kPtrAlign;
        const std::uint64_t payload_off =
            ((1 + payload_align - 1) / payload_align) * payload_align;
        const std::uint64_t total_size_raw = payload_off + payload_size;
        const std::uint64_t total_size =
            ((total_size_raw + payload_align - 1) / payload_align) * payload_align;
        ll_ty = CreateTaggedStructType(
            *this,
            analysis::MakeTypePrim("u8"),
            payload_size,
            payload_align,
            total_size);
      }
    }
    else if (std::holds_alternative<analysis::TypeDynamic>(type->node))
    {
      SPEC_RULE("LLVMTy-Dynamic");
      ll_ty = GetDynamicType(context_);
    }
    else if (analysis::IsRangeType(type))
    {
      analysis::TypeRef stripped = type;
      while (stripped)
      {
        if (const auto *perm = std::get_if<analysis::TypePerm>(&stripped->node))
        {
          stripped = perm->base;
          continue;
        }
        if (const auto *refine = std::get_if<analysis::TypeRefine>(&stripped->node))
        {
          stripped = refine->base;
          continue;
        }
        break;
      }

      std::vector<analysis::TypeRef> fields;
      if (const auto *range = stripped ? std::get_if<analysis::TypeRange>(&stripped->node)
                                       : nullptr)
      {
        SPEC_RULE("LLVMTy-Range");
        fields.push_back(range->base);
        fields.push_back(range->base);
      }
      else if (const auto *range = stripped ? std::get_if<analysis::TypeRangeInclusive>(&stripped->node)
                                            : nullptr)
      {
        SPEC_RULE("LLVMTy-RangeInclusive");
        fields.push_back(range->base);
        fields.push_back(range->base);
      }
      else if (const auto *range = stripped ? std::get_if<analysis::TypeRangeFrom>(&stripped->node)
                                            : nullptr)
      {
        SPEC_RULE("LLVMTy-RangeFrom");
        fields.push_back(range->base);
      }
      else if (const auto *range = stripped ? std::get_if<analysis::TypeRangeTo>(&stripped->node)
                                            : nullptr)
      {
        SPEC_RULE("LLVMTy-RangeTo");
        fields.push_back(range->base);
      }
      else if (const auto *range =
                   stripped ? std::get_if<analysis::TypeRangeToInclusive>(&stripped->node)
                            : nullptr)
      {
        SPEC_RULE("LLVMTy-RangeToInclusive");
        fields.push_back(range->base);
      }
      else if (stripped &&
               std::holds_alternative<analysis::TypeRangeFull>(stripped->node))
      {
        SPEC_RULE("LLVMTy-RangeFull");
      }

      if (fields.empty())
      {
        ll_ty = llvm::StructType::get(context_, {});
      }
      else
      {
        const analysis::ScopeContext scope = BuildScope(current_ctx_);
        if (const auto layout = ::ultraviolet::analysis::layout::RecordLayoutOf(scope, fields))
        {
          std::vector<llvm::Type *> elems = ComputeStructElements(
              *this, fields, layout->offsets, layout->layout.size);
          ll_ty = llvm::StructType::get(context_, elems);
        }
        else
        {
          std::vector<llvm::Type *> elems;
          elems.reserve(fields.size());
          for (const auto &field : fields)
          {
            elems.push_back(GetLLVMType(field));
          }
          ll_ty = llvm::StructType::get(context_, elems);
        }
      }
    }
    else
    {
      SPEC_RULE("LLVMTy-Err");
      ll_ty = llvm::Type::getInt8Ty(context_);
    }

    if (!ll_ty)
    {
      ll_ty = llvm::Type::getInt8Ty(context_);
    }

    type_cache_[type] = ll_ty;
    return ll_ty;
  }

}  // namespace ultraviolet::codegen
