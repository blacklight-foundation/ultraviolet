// =============================================================================
// MIGRATION: item/static_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.3.7: Static Declarations
//   - WF-StaticDecl-Ann-Mismatch (line 21477): Type mismatch
//   - Static let/var declarations
//   - Module-level bindings
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <string>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/types.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsStaticDecl() {
  SPEC_DEF("WF-StaticDecl", "5.2.14");
  SPEC_DEF("WF-StaticDecl-Ann-Mismatch", "5.2.14");
  SPEC_DEF("StaticVisOk", "5.2.14");
  SPEC_DEF("StaticConst", "5.2.14");
}

// =============================================================================
// HELPERS
// =============================================================================

// Lower type with well-formedness check
static LowerTypeResult LowerTypeWithWF(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    return {false, wf.diag_id, {}};
  }
  return lowered;
}

// Check visibility constraints for statics
// Public mutable statics are not allowed
static bool StaticVisOk(ast::Visibility vis, ast::Mutability mut) {
  return !(vis == ast::Visibility::Public && mut == ast::Mutability::Var);
}

}  // namespace

// =============================================================================
// HELPERS: Extract name from binding pattern
// =============================================================================

static std::optional<std::string> ExtractBindingName(const ast::Binding& binding) {
  if (!binding.pat) {
    return std::nullopt;
  }
  return std::visit(
      [](const auto& pat) -> std::optional<std::string> {
        using T = std::decay_t<decltype(pat)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return pat.name;
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (pat.name == "_") {
            return std::nullopt;
          }
          return pat.name;
        }
        return std::nullopt;
      },
      binding.pat->node);
}

// =============================================================================
// EXPORTED: TypeStaticDecl
// =============================================================================

StaticDeclResult TypeStaticDecl(
    const ScopeContext& ctx,
    const ast::StaticDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsStaticDecl();
  StaticDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(ast::AttrListOf(decl.attrs_opt),
                                         "static declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Binding patterns may legally introduce zero names (for example `_`).
  // Keep an empty name for anonymous static patterns.
  if (const auto name_opt = ExtractBindingName(decl.binding); name_opt.has_value()) {
    result.name = *name_opt;
  }
  result.is_mutable = (decl.mut == ast::Mutability::Var);

  const auto ann_type = ast::BindingAnnotationTypeOpt(decl.binding);
  const bool missing_type = !ann_type;
  const bool vis_error = !StaticVisOk(decl.vis, decl.mut);

  // Lower the type annotation (required for statics)
  if (missing_type) {
    if (vis_error) {
      // Report concurrent visibility violation as an additional diagnostic so
      // both obligations remain observable from the same malformed declaration.
      if (auto diag = core::MakeDiagnosticById("E-MOD-2433", decl.span)) {
        core::Emit(diags, *diag);
      }
    }
    SPEC_RULE("WF-StaticDecl-MissingType");
    result.ok = false;
    result.diag_id = "E-TYP-1505";
    return result;
  }

  // Validate visibility only after required annotation shape checks.
  if (vis_error) {
    SPEC_RULE("StaticVisOk-Err");
    result.ok = false;
    result.diag_id = "E-MOD-2433";
    return result;
  }

  const auto lowered = LowerTypeWithWF(ctx, ann_type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }
  result.type = lowered.type;

  // Type the initializer expression
  if (decl.binding.init) {
    TypeEnv env;
    env.scopes.emplace_back();
    StmtTypeContext type_ctx;
    type_ctx.return_type = MakeTypePrim("()");

    const auto init_result =
        CheckExprAgainst(ctx, type_ctx, decl.binding.init, lowered.type, env);
    if (!init_result.ok) {
      SPEC_RULE("WF-StaticDecl-Ann-Mismatch");
      result.ok = false;
      result.diag_id =
          init_result.diag_id.has_value() ? init_result.diag_id
                                          : std::optional<std::string_view>("E-MOD-2402");
      return result;
    }
  } else {
    // Parser guarantees an initializer shape; if missing, report syntax failure.
    result.ok = false;
    result.diag_id = "Parse-Syntax-Err";
    return result;
  }

  (void)module_path;
  (void)diags;

  SPEC_RULE("WF-StaticDecl-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeStaticDeclSignature (first pass - signature only)
// =============================================================================

StaticDeclResult TypeStaticDeclSignature(
    const ScopeContext& ctx,
    const ast::StaticDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsStaticDecl();
  StaticDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(ast::AttrListOf(decl.attrs_opt),
                                         "static declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Binding patterns may legally introduce zero names (for example `_`).
  if (const auto name_opt = ExtractBindingName(decl.binding); name_opt.has_value()) {
    result.name = *name_opt;
  }
  result.is_mutable = (decl.mut == ast::Mutability::Var);

  // Lower the type (required for statics)
  const auto ann_type = ast::BindingAnnotationTypeOpt(decl.binding);
  if (!ann_type) {
    SPEC_RULE("WF-StaticDecl-MissingType");
    result.ok = false;
    result.diag_id = "E-TYP-1505";
    return result;
  }

  // Validate visibility only after required annotation shape checks.
  if (!StaticVisOk(decl.vis, decl.mut)) {
    SPEC_RULE("StaticVisOk-Err");
    result.ok = false;
    result.diag_id = "E-MOD-2433";
    return result;
  }

  const auto lowered = LowerTypeWithWF(ctx, ann_type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }
  result.type = lowered.type;

  (void)module_path;

  SPEC_RULE("WF-StaticDecl-Sig-Ok");
  return result;
}

}  // namespace ultraviolet::analysis
