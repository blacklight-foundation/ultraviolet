// =============================================================================
// ABI Type: Path Types (ABI-Record, ABI-Enum, ABI-Alias, ABI-Modal for paths)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABI-Alias: resolve alias body and recurse
//   - ABI-Record: use ::ultraviolet::analysis::layout::RecordLayout
//   - ABI-Enum: use EnumLayout
//   - ABI-Modal: use ModalLayout for modal path types
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/generics/monomorphize.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {
namespace {

// Helper to resolve type alias bodies.
std::optional<analysis::TypeRef> ResolveAliasBody(
    const analysis::ScopeContext& ctx,
    const analysis::TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return std::nullopt;
  }
  const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!alias) {
    return std::nullopt;
  }
  return ::ultraviolet::analysis::layout::LowerTypeForLayout(ctx, alias->type);
}

// Check if a TypePathType resolves to a record declaration.
bool IsRecordDecl(const analysis::ScopeContext& ctx,
                  const analysis::TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  return std::holds_alternative<ast::RecordDecl>(it->second);
}

// Check if a TypePathType resolves to an enum declaration.
bool IsEnumDecl(const analysis::ScopeContext& ctx,
                const analysis::TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  return std::holds_alternative<ast::EnumDecl>(it->second);
}

// Check if a TypePathType resolves to a modal declaration.
bool IsModalDecl(const analysis::ScopeContext& ctx,
                 const analysis::TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  return std::holds_alternative<ast::ModalDecl>(it->second);
}

// Check if a TypePathType resolves to a type alias declaration.
bool IsAliasDecl(const analysis::ScopeContext& ctx,
                 const analysis::TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& comp : path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  return std::holds_alternative<ast::TypeAliasDecl>(it->second);
}

}  // namespace

// Forward declaration for recursion.
std::optional<ABIType> ABITy(const analysis::ScopeContext& ctx,
                             const analysis::TypeRef& type);

std::optional<ABIType> ABITyPathType(const analysis::ScopeContext& ctx,
                                     const analysis::TypeRef& type,
                                     const analysis::TypePathType& path_type) {
  // (ABI-Alias)
  // ABITy(TypePath(p)) => tau when AliasBody(p) = ty and ABITy(ty) => tau
  if (IsAliasDecl(ctx, path_type.path)) {
    SPEC_RULE("ABI-Alias");
    const auto resolved = ResolveAliasBody(ctx, path_type.path);
    if (!resolved.has_value()) {
      return std::nullopt;
    }
    analysis::TypeRef inst = *resolved;
    // Apply generic substitution when alias has params and path has args.
    ast::Path syntax_path;
    syntax_path.reserve(path_type.path.size());
    for (const auto& comp : path_type.path) {
      syntax_path.push_back(comp);
    }
    const auto it = ctx.sigma.types.find(analysis::PathKeyOf(syntax_path));
    if (it != ctx.sigma.types.end()) {
      if (const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second)) {
        if (alias->generic_params &&
            !alias->generic_params->params.empty() &&
            !path_type.generic_args.empty()) {
          analysis::TypeSubst subst =
              analysis::BuildSubstitution(alias->generic_params->params,
                                          path_type.generic_args);
          inst = analysis::InstantiateType(inst, subst);
        }
      }
    }
    return ABITy(ctx, inst);
  }

  // (ABI-Record)
  // ABITy(TypePath(p)) => <size, align> when RecordDecl(p) = R
  if (IsRecordDecl(ctx, path_type.path)) {
    SPEC_RULE("ABI-Record");
    const auto layout = ::ultraviolet::analysis::layout::LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return *layout;
  }

  // (ABI-Enum)
  // ABITy(TypePath(p)) => <size, align> when EnumDecl(p) = E
  if (IsEnumDecl(ctx, path_type.path)) {
    SPEC_RULE("ABI-Enum");
    const auto layout = ::ultraviolet::analysis::layout::LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return *layout;
  }

  // (ABI-Modal) - for TypePath to modal declaration
  // ABITy(TypePath(p)) => <size, align> when Sigma.Types[p] = modal M
  if (IsModalDecl(ctx, path_type.path)) {
    SPEC_RULE("ABI-Modal");
    const auto layout = ::ultraviolet::analysis::layout::LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return *layout;
  }

  return std::nullopt;
}

}  // namespace ultraviolet::codegen
