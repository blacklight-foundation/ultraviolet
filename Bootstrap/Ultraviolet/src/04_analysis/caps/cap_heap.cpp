// =============================================================================
// cap_heap.cpp - Heap Allocator Capability Checking Implementation
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 5.9.1 "Capability Classes" (CapClass, CapType, CapMethodSig, CapRecv)
//   - Section 5.9.3 "HeapAllocator Interface" (HeapAllocatorInterface, AllocationError)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/caps/cap_heap.cpp (lines 1-131)
//
// Key functions migrated:
//   - IsHeapAllocatorBuiltinTypePath: Check if path is AllocationError
//   - IsHeapAllocatorClassPath: Check if path is HeapAllocator
//   - LookupHeapAllocatorMethodSig: Get signature for HeapAllocator method
//   - BuildAllocationErrorEnumDecl: Build built-in AllocationError enum declaration
//
// REFACTORING NOTES:
//   1. Namespace changed from uv to ultraviolet
//   2. syntax:: namespace changed to ast::
//   3. Include paths updated to new structure
//   4. SPEC_DEF and SPEC_RULE annotations preserved
//
// =============================================================================

#include "04_analysis/caps/cap_heap.h"

#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsCapHeap() {
  SPEC_DEF("CapClass", "5.9.1");
  SPEC_DEF("CapType", "5.9.1");
  SPEC_DEF("CapMethodSig", "5.9.1");
  SPEC_DEF("CapRecv", "5.9.1");
  SPEC_DEF("HeapAllocatorInterface", "5.9.3");
  SPEC_DEF("AllocationError", "5.9.3");
}

static ast::TypePtr MakeTypeNode(const ast::TypeNode& node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = node;
  return ty;
}

static ast::TypePtr MakeTypePrimAst(std::string_view name) {
  return MakeTypeNode(ast::TypePrim{std::string(name)});
}

static ast::TypePtr MakeTypeRawPtrAst(
    ast::RawPtrQual qual,
    ast::TypePtr elem) {
  ast::TypeRawPtr node;
  node.qual = qual;
  node.element = std::move(elem);
  return MakeTypeNode(node);
}

static ast::Param MakeParam(std::string_view name, ast::TypePtr type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = std::string(name);
  param.type = std::move(type);
  param.span = core::Span{};
  return param;
}

static TypeRef TypeHeapAllocatorDynamic() {
  return MakeTypeDynamic({"HeapAllocator"});
}

static TypeRef TypeUnit() {
  return MakeTypePrim("()");
}

}  // namespace

bool IsHeapAllocatorBuiltinTypePath(const ast::TypePath& path) {
  SpecDefsCapHeap();
  return PathMatchesBuiltinName(path, "AllocationError");
}

bool IsHeapAllocatorClassPath(const ast::ClassPath& path) {
  SpecDefsCapHeap();
  return PathMatchesBuiltinName(path, "HeapAllocator");
}

std::optional<HeapAllocatorMethodSig> LookupHeapAllocatorMethodSig(
    std::string_view name) {
  SpecDefsCapHeap();
  HeapAllocatorMethodSig sig{};

  if (IdEq(name, "with_quota")) {
    sig.recv_perm = Permission::Const;
    sig.params = {MakeParam("size", MakeTypePrimAst("usize"))};
    sig.ret = TypeHeapAllocatorDynamic();
    return sig;
  }
  if (IdEq(name, "alloc_raw")) {
    sig.recv_perm = Permission::Const;
    sig.params = {MakeParam("count", MakeTypePrimAst("usize"))};
    sig.ret = MakeTypeRawPtr(RawPtrQual::Mut, MakeTypePrim("u8"));
    sig.kind = HeapAllocatorMethodKind::AllocRaw;
    return sig;
  }
  if (IdEq(name, "dealloc_raw")) {
    sig.recv_perm = Permission::Const;
    sig.params = {
        MakeParam("ptr",
                  MakeTypeRawPtrAst(ast::RawPtrQual::Mut,
                                    MakeTypePrimAst("u8"))),
        MakeParam("count", MakeTypePrimAst("usize")),
    };
    sig.ret = TypeUnit();
    sig.kind = HeapAllocatorMethodKind::DeallocRaw;
    return sig;
  }

  return std::nullopt;
}

ast::EnumDecl BuildAllocationErrorEnumDecl() {
  SpecDefsCapHeap();
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "AllocationError";
  decl.implements = {};
  decl.span = core::Span{};
  decl.doc = {};

  auto make_variant = [](std::string_view name) {
    ast::VariantDecl variant{};
    variant.name = std::string(name);
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;

    ast::VariantPayloadTuple tuple{};
    tuple.elements.push_back(MakeTypePrimAst("usize"));
    variant.payload_opt = ast::VariantPayload{std::move(tuple)};
    return variant;
  };

  decl.variants = {make_variant("OutOfMemory"), make_variant("QuotaExceeded")};
  return decl;
}

}  // namespace ultraviolet::analysis
