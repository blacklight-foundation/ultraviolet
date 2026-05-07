#pragma once

#include "02_source/ast/ast.h"

namespace cursive::analysis {

ast::RecordDecl BuildSourceSpanRecordDecl();
ast::EnumDecl BuildTypeCategoryEnumDecl();
ast::RecordDecl BuildFieldInfoRecordDecl();
ast::RecordDecl BuildVariantInfoRecordDecl();
ast::RecordDecl BuildStateInfoRecordDecl();

}  // namespace cursive::analysis
