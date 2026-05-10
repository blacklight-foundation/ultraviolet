#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct FileSystemMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

bool IsFileSystemBuiltinTypePath(const ast::TypePath& path);
bool IsFileSystemClassPath(const ast::ClassPath& path);

std::optional<FileSystemMethodSig> LookupFileSystemMethodSig(
    std::string_view name);

ast::ModalDecl BuildFileModalDecl();
ast::ModalDecl BuildDirIterModalDecl();
ast::RecordDecl BuildDirEntryRecordDecl();
ast::EnumDecl BuildFileKindEnumDecl();
ast::EnumDecl BuildIoErrorEnumDecl();

}  // namespace cursive::analysis
