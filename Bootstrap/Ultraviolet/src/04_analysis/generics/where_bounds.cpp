#include "04_analysis/generics/where_bounds.h"

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

bool CheckClassBound(
    const ScopeContext& ctx,
    const TypeRef& type,
    const TypePath& class_path) {
  SPEC_RULE("Check-ClassBound");

  if (!type || class_path.empty()) {
    return false;
  }

  ast::ClassPath syntax_class_path;
  syntax_class_path.reserve(class_path.size());
  for (const auto& seg : class_path) {
    syntax_class_path.push_back(seg);
  }

  return TypeImplementsClass(ctx, type, syntax_class_path);
}

}  // namespace ultraviolet::analysis
