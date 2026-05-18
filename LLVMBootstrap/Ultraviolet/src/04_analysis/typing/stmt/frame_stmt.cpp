// =============================================================================
// frame_stmt.cpp - Frame statement typing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.5: Memory and Pointers - Frame statements
//   - T-FrameStmt-Implicit (line 9557): Implicit frame
//   - T-FrameStmt-Explicit (line 9562): Explicit named frame
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/resolve/scopes.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsFrameStmt() {
  SPEC_DEF("T-FrameStmt-Implicit", "5.2.17");
  SPEC_DEF("T-FrameStmt-Explicit", "5.2.17");
  SPEC_DEF("Frame-NoActiveRegion-Err", "5.2.17");
  SPEC_DEF("Frame-Target-NotActive-Err", "5.2.17");
  SPEC_DEF("FrameBind", "5.2.17");
}

static IntroResult FrameBind(const TypeEnv& env,
                             const std::optional<std::string>& target_opt,
                             std::optional<std::string_view>& diag_id) {
  std::string target;
  if (!target_opt.has_value()) {
    const auto inner = InnermostActiveRegion(env);
    if (!inner.has_value()) {
      diag_id = "Frame-NoActiveRegion-Err";
      SPEC_RULE("Frame-NoActiveRegion-Err");
      return {false, diag_id, env};
    }
    target = *inner;
  } else {
    target = *target_opt;
    const auto binding = BindOf(env, target);
    if (!binding.has_value()) {
      diag_id = "ResolveExpr-Ident-Err";
      return {false, diag_id, env};
    }
    if (!RegionActiveType(binding->type)) {
      diag_id = "Frame-Target-NotActive-Err";
      SPEC_RULE("Frame-Target-NotActive-Err");
      return {false, diag_id, env};
    }
  }

  (void)target;
  const std::string fresh = FreshRegionName(env);
  std::vector<std::pair<std::string, TypeRef>> binds;
  binds.emplace_back(fresh, RegionActiveTypeRef());
  return IntroAll(env, binds, ast::Mutability::Let, false);
}

}  // namespace

StmtTypeResult TypeFrameStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::FrameStmt& node,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place) {
  SpecDefsFrameStmt();

  TypeEnv frame_env = PushScope(env);

  // Introduce a frame binding (requires enclosing region)
  std::optional<std::string_view> diag_id;
  const auto intro = FrameBind(frame_env, node.target_opt, diag_id);
  if (!intro.ok) {
    return {false, diag_id, {}, {}};
  }

  if (!node.body) {
    return {false, std::nullopt, {}, {}};
  }

  const auto typed = TypeScopedStmtBody(ctx, type_ctx, *node.body, intro.env,
                                        env.scopes.size(), type_expr,
                                        type_ident, type_place);
  if (!typed.ok) {
    return {false, typed.diag_id, {}, {}, typed.diag_detail, typed.diag_span};
  }

  if (node.target_opt.has_value()) {
    SPEC_RULE("T-FrameStmt-Explicit");
  } else {
    SPEC_RULE("T-FrameStmt-Implicit");
  }
  return typed;
}

}  // namespace ultraviolet::analysis
