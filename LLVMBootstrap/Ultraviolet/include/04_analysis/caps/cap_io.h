#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct IOMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

bool IsIOBuiltinTypePath(const ast::TypePath& path);
bool IsIOClassPath(const ast::ClassPath& path);

std::optional<IOMethodSig> LookupIOMethodSig(
    std::string_view name);

ast::ModalDecl BuildFileModalDecl();
ast::ModalDecl BuildDirIterModalDecl();
ast::RecordDecl BuildDirEntryRecordDecl();
ast::EnumDecl BuildFileKindEnumDecl();
ast::EnumDecl BuildIoErrorEnumDecl();

}  // namespace ultraviolet::analysis
