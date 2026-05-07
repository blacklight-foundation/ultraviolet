#pragma once

#include "02_source/ast/ast.h"

namespace cursive::codegen {

bool IsExternalVisibility(ast::Visibility vis);

bool IsExternalVisibilityWithProtected(ast::Visibility vis);

}  // namespace cursive::codegen
