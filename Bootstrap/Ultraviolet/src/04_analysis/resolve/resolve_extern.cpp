// =============================================================================
// resolve_extern.cpp - Extern Block Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   Docs/SPECIFICATION.md §12 "Foreign Function Interface" (FFI section)
//
// SOURCE FILE:
//   Migrated from ultraviolet-bootstrap/src/03_analysis/resolve/resolver_items.cpp
//   (Lines 828-854 for extern block resolution)
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scope_overrides.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsExtern() {
  SPEC_DEF("ResolveExternBlock", "5.1.7");
  SPEC_DEF("ResolveExternProcedure", "5.1.7");
  SPEC_DEF("ValidAbiString", "12");
  SPEC_DEF("KnownAbiStrings", "12");
}

// -----------------------------------------------------------------------------
// KnownAbiStrings
// -----------------------------------------------------------------------------
// Returns the set of known ABI strings.
// From §12: KnownAbiStrings = { "C", "C-unwind", "system", "stdcall",
//                               "fastcall", "vectorcall" }

const std::vector<std::string_view>& KnownAbiStrings() {
  static const std::vector<std::string_view> abis = {
      "C", "C-unwind", "system", "stdcall", "fastcall", "vectorcall"};
  return abis;
}

// -----------------------------------------------------------------------------
// ValidAbiString
// -----------------------------------------------------------------------------
// Validates that an ABI string is in the known set.

bool ValidAbiString(std::string_view abi) {
  SpecDefsExtern();
  for (const auto& known : KnownAbiStrings()) {
    if (abi == known) {
      SPEC_RULE("ValidAbiString");
      return true;
    }
  }
  return false;
}

// -----------------------------------------------------------------------------
// GetAbiString
// -----------------------------------------------------------------------------
// Extracts the ABI string from an ExternAbi variant.

std::optional<std::string_view> GetAbiString(
    const std::optional<ast::ExternAbi>& abi_opt) {
  if (!abi_opt.has_value()) {
    return "C";  // Default ABI is "C"
  }
  return std::visit(
      [](const auto& abi) -> std::optional<std::string_view> {
        using T = std::decay_t<decltype(abi)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          return abi.literal.lexeme;
        } else if constexpr (std::is_same_v<T, ast::ExternAbiIdent>) {
          return abi.name;
        }
        return std::nullopt;
      },
      *abi_opt);
}

}  // namespace

// -----------------------------------------------------------------------------
// ResolveExternProcDecl
// -----------------------------------------------------------------------------
// Resolves a single extern procedure declaration.
// Implements (Resolve-Extern-Procedure):
//   - Resolves parameter types (FfiSafe check deferred to type checking)
//   - Resolves return type (FfiSafe check deferred to type checking)
//   - Resolves foreign contracts if present

ResolveResult<ast::ExternProcDecl> ResolveExternProcDecl(
    ResolveContext& ctx,
    const ast::ExternProcDecl& proc) {
  SpecDefsExtern();
  ResolveResult<ast::ExternProcDecl> result;
  result.ok = true;
  result.value = proc;

  ResolveContext proc_ctx = ctx;
  ScopeContext proc_scope_ctx;
  std::optional<ScopedScopesOverride> scoped_proc_override;

  // Create procedure scope context with type parameters.
  if (ctx.ctx) {
    Scope proc_scope;

    // Introduce type parameters if present
    if (proc.generic_params.has_value()) {
      for (const auto& type_param : proc.generic_params->params) {
        proc_scope.emplace(IdKeyOf(type_param.name),
                           Entity{EntityKind::Type, std::nullopt, std::nullopt,
                                  EntitySource::Decl});
      }
    }

    // Introduce parameter names
    for (const auto& param : proc.params) {
      proc_scope.emplace(IdKeyOf(param.name),
                         Entity{EntityKind::Value, std::nullopt, std::nullopt,
                                EntitySource::Decl});
    }

    scoped_proc_override.emplace(
        *ctx.ctx, MakeProcLikeScopes(*ctx.ctx, std::move(proc_scope)));
  } else {
    proc_ctx.ctx = &proc_scope_ctx;
  }

  // Resolve parameter types
  std::vector<ast::Param> resolved_params;
  resolved_params.reserve(proc.params.size());
  for (const auto& param : proc.params) {
    ast::Param out_param = param;
    if (param.type) {
      const auto resolved_type = ResolveType(proc_ctx, param.type);
      if (!resolved_type.ok) {
        return {false, resolved_type.diag_id, resolved_type.span, {}};
      }
      out_param.type = resolved_type.value;
    }
    resolved_params.push_back(out_param);
    SPEC_RULE("ResolveExternProcedure-Param");
  }
  result.value.params = resolved_params;

  // Resolve return type
  if (proc.return_type_opt) {
    const auto resolved_ret = ResolveType(proc_ctx, proc.return_type_opt);
    if (!resolved_ret.ok) {
      return {false, resolved_ret.diag_id, resolved_ret.span, {}};
    }
    result.value.return_type_opt = resolved_ret.value;
    SPEC_RULE("ResolveExternProcedure-Return");
  }

  // Note: Foreign contracts resolution would go here if needed
  // Foreign contracts use @foreign_assumes and @foreign_ensures
  // which have special syntax for extern procedures

  SPEC_RULE("ResolveExternProcedure");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveExternBlock
// -----------------------------------------------------------------------------
// Resolves an extern block.
// Implements (Resolve-Extern-Block):
//   ValidAbiString(abi) /\
//   forall proc in procs. Gamma |- ResolveExternProcedure(proc) => ok
//   -> Gamma |- ResolveExternBlock(extern_block) => ok

ResolveResult<ast::ExternBlock> ResolveExternBlock(
    ResolveContext& ctx,
    const ast::ExternBlock& block) {
  SpecDefsExtern();
  ResolveResult<ast::ExternBlock> result;
  result.ok = true;
  result.value = block;
  result.value.items.clear();
  result.value.items.reserve(block.items.size());

  // Validate ABI string
  const auto abi_str = GetAbiString(block.abi_opt);
  if (abi_str.has_value() && !ValidAbiString(*abi_str)) {
    SPEC_RULE("ResolveExternBlock-InvalidAbi");
    return {false, "E-SYS-3352", block.span, {}};  // Invalid ABI string
  }

  // Resolve each extern item
  for (const auto& item : block.items) {
    ast::ExternItem resolved_item = std::visit(
        [&](const auto& ext_item) -> ast::ExternItem {
          using T = std::decay_t<decltype(ext_item)>;
          if constexpr (std::is_same_v<T, ast::ExternProcDecl>) {
            const auto resolved = ResolveExternProcDecl(ctx, ext_item);
            if (resolved.ok) {
              return resolved.value;
            }
            // On error, return original item (error already recorded)
            return ext_item;
          }
          return ext_item;
        },
        item);
    result.value.items.push_back(resolved_item);
    SPEC_RULE("ResolveExternBlock-Item");
  }

  SPEC_RULE("ResolveExternBlock");
  return result;
}

}  // namespace ultraviolet::analysis

