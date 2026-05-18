#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

struct TimeMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

bool IsTimeBuiltinTypePath(const ast::TypePath& path);
bool IsTimeClassPath(const ast::ClassPath& path);
bool IsMonotonicTimeClassPath(const ast::ClassPath& path);
bool IsWallTimeClassPath(const ast::ClassPath& path);

std::optional<TimeMethodSig> LookupTimeMethodSig(std::string_view name);
std::optional<TimeMethodSig> LookupMonotonicTimeMethodSig(std::string_view name);
std::optional<TimeMethodSig> LookupWallTimeMethodSig(std::string_view name);

ast::EnumDecl BuildTimeErrorEnumDecl();
ast::RecordDecl BuildDurationRecordDecl();
ast::RecordDecl BuildMonotonicInstantRecordDecl();
ast::RecordDecl BuildUtcInstantRecordDecl();

}  // namespace ultraviolet::analysis
