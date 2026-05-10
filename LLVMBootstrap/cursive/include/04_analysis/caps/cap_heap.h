#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

enum class HeapAllocatorMethodKind {
  Normal,
  AllocRaw,
  DeallocRaw,
};

struct HeapAllocatorMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
  HeapAllocatorMethodKind kind = HeapAllocatorMethodKind::Normal;
};

bool IsHeapAllocatorBuiltinTypePath(const ast::TypePath& path);
bool IsHeapAllocatorClassPath(const ast::ClassPath& path);

std::optional<HeapAllocatorMethodSig> LookupHeapAllocatorMethodSig(
    std::string_view name);

ast::EnumDecl BuildAllocationErrorEnumDecl();

}  // namespace cursive::analysis
