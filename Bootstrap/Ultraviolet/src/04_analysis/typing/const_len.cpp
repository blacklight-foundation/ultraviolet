// =============================================================================
// MIGRATION MAPPING: const_len.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.2.1: Type Equivalence (lines 8490-8593)
//   - ConstLenJudg = {ConstLen}
//   - Rules: ConstLen-Lit, ConstLen-Path, ConstLen-Err
//   - ConstLen-Lit (line 8497-8500): Integer literal evaluation
//   - ConstLen-Path (line 8502-8505): Path-based constant lookup
//   - ConstLen-Err (line 8507-8510): Error case
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_equiv.cpp
//   Lines 311-384 (ConstLen function and helpers)
//
// KEY CONTENT TO MIGRATE:
//   - SpecDefsTypeEquiv() helper referencing "5.2.1" (lines 23-27)
//   - kPointerSizeBytes, kPointerSizeBits constants (lines 29-30)
//   - kIntSuffixes array for parsing (lines 32-34)
//   - EndsWith() string helper (lines 36-41)
//   - StripIntSuffix() to extract numeric core (lines 43-54)
//   - DigitValue() for hex/oct/bin/dec parsing (lines 56-85)
//   - ParseIntCore() for full integer parsing (lines 87-134)
//     * Handles 0x (hex), 0o (octal), 0b (binary), decimal
//     * Handles underscore separators
//     * Uses UInt128 for large value support
//   - IntWidthOf() for type width lookup (lines 136-156)
//   - IsUnsignedIntType(), IsSignedIntType() helpers (lines 158-166)
//   - InRangeUnsigned(), InRangeSigned() range checks (lines 168-206)
//   - ParseIntLiteralUsize() for usize-constrained parsing (lines 208-222)
//   - FindModule() for module lookup (lines 242-250)
//   - FindStaticInit() for static initializer lookup (lines 252-275)
//   - ModuleNamesForConstLen() helper (lines 277-287)
//   - ResolveValuePathForConstLen() for qualified name resolution (lines 289-307)
//   - ConstLen() main function (lines 311-384)
//     * LiteralExpr case: Parse integer, validate usize range (ConstLen-Lit)
//     * IdentifierExpr case: Resolve name, find static, recurse (ConstLen-Path)
//     * PathExpr case: Resolve qualified path, find static, recurse (ConstLen-Path)
//     * Default case: Error (ConstLen-Err)
//
// DEPENDENCIES:
//   - ScopeContext for resolution context
//   - syntax::ExprPtr, syntax::LiteralExpr, syntax::IdentifierExpr, syntax::PathExpr
//   - core::UInt128 for 128-bit arithmetic
//   - ResolveValueName() from resolve/scopes_lookup.h
//   - ResolveQualified() from resolve/scopes_lookup.h
//   - IdEq() from resolve/scopes.h
//   - syntax::ASTModule, syntax::StaticDecl for static lookup
//   - CollectNameMaps() from resolve/collect_toplevel.h
//
// REFACTORING NOTES:
//   1. Integer parsing utilities (ParseIntCore, etc.) could be shared with
//      literal typing in literals.cpp
//   2. Consider caching parsed static initializers to avoid repeated parsing
//   3. The UInt128 dependency is for handling i128/u128 constants
//   4. Module lookup could be optimized with an index
//   5. Error messages should include the expression span for diagnostics
//
// SPEC RULES IMPLEMENTED:
//   - ConstLen-Lit: Integer literal constant evaluation
//   - ConstLen-Path: Path-based constant lookup (identifier and qualified)
//   - ConstLen-Err: Error case for non-constant expressions
//
// RESULT TYPE:
//   ConstLenResult { bool ok; optional<string_view> diag_id; optional<uint64_t> value; }
//
// =============================================================================

#include "04_analysis/typing/type_equiv.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/int128.h"
#include "00_core/numeric_literals.h"
#include "00_core/symbols.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/visibility.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsTypeEquiv() {
  SPEC_DEF("TypeEqJudg", "5.2.1");
  SPEC_DEF("ConstLenJudg", "5.2.1");
  SPEC_DEF("MembersEq", "5.2.1");
}

static constexpr unsigned kPointerSizeBytes = 8;
static constexpr unsigned kPointerSizeBits = 8 * kPointerSizeBytes;

static std::optional<unsigned> IntWidthOf(std::string_view name) {
  if (name == "i8" || name == "u8") {
    return 8;
  }
  if (name == "i16" || name == "u16") {
    return 16;
  }
  if (name == "i32" || name == "u32") {
    return 32;
  }
  if (name == "i64" || name == "u64") {
    return 64;
  }
  if (name == "i128" || name == "u128") {
    return 128;
  }
  if (name == "isize" || name == "usize") {
    return kPointerSizeBits;
  }
  return std::nullopt;
}

static bool IsUnsignedIntType(std::string_view name) {
  return name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "u128" || name == "usize";
}

static bool IsSignedIntType(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "i128" || name == "isize";
}

static bool InRangeUnsigned(core::UInt128 value, unsigned width) {
  if (width >= 128) {
    return true;
  }
  const core::UInt128 one = core::UInt128FromU64(1);
  const core::UInt128 max =
      core::UInt128Sub(core::UInt128ShiftLeft(one, width), one);
  return core::UInt128LessOrEqual(value, max);
}

static bool InRangeSigned(core::UInt128 value, unsigned width) {
  if (width == 0) {
    return false;
  }
  if (width >= 128) {
    const core::UInt128 one = core::UInt128FromU64(1);
    const core::UInt128 max =
        core::UInt128Sub(core::UInt128ShiftLeft(one, 127), one);
    return core::UInt128LessOrEqual(value, max);
  }
  const core::UInt128 one = core::UInt128FromU64(1);
  const core::UInt128 max =
      core::UInt128Sub(core::UInt128ShiftLeft(one, width - 1), one);
  return core::UInt128LessOrEqual(value, max);
}

static bool InRangeInt(core::UInt128 value, std::string_view name) {
  const auto width = IntWidthOf(name);
  if (!width.has_value()) {
    return false;
  }
  if (IsUnsignedIntType(name)) {
    return InRangeUnsigned(value, *width);
  }
  if (IsSignedIntType(name)) {
    return InRangeSigned(value, *width);
  }
  return false;
}

static std::optional<std::uint64_t> ParseIntLiteralUsize(
    std::string_view lexeme) {
  const std::string_view core_text = core::StripIntSuffix(lexeme);
  const auto value = core::ParseIntCore(core_text);
  if (!value.has_value()) {
    return std::nullopt;
  }
  if (!InRangeInt(*value, "usize")) {
    return std::nullopt;
  }
  if (!core::UInt128FitsU64(*value)) {
    return std::nullopt;
  }
  return core::UInt128ToU64(*value);
}

static const ast::ASTModule* FindModule(const ScopeContext& ctx,
                                        const ast::ModulePath& path) {
  for (const auto& mod : ctx.sigma.mods) {
    if (PathEq(mod.path, path)) {
      return &mod;
    }
  }
  return nullptr;
}

static std::optional<ast::ExprPtr> FindStaticInit(
    const ast::ASTModule& module,
    std::string_view name) {
  for (const auto& item : module.items) {
    const auto* decl = std::get_if<ast::StaticDecl>(&item);
    if (!decl || !decl->binding.pat) {
      continue;
    }
    bool name_matches = false;
    for (const auto& binding_name : PatNames(decl->binding.pat)) {
      if (IdEq(binding_name, name)) {
        name_matches = true;
        break;
      }
    }
    if (!name_matches) {
      continue;
    }
    if (decl->binding.op.kind != lexer::TokenKind::Operator ||
        decl->binding.op.lexeme != "=") {
      continue;
    }
    return decl->binding.init;
  }
  return std::nullopt;
}

static std::optional<ast::ExprPtr> FindCurrentModuleStaticInit(
    const ScopeContext& ctx,
    std::string_view name) {
  if (ctx.current_module.empty()) {
    return std::nullopt;
  }
  const auto* module = FindModule(ctx, ctx.current_module);
  if (!module) {
    return std::nullopt;
  }
  return FindStaticInit(*module, name);
}

static source::ModuleNames ModuleNamesForConstLen(const ScopeContext& ctx) {
  if (ctx.project) {
    return ModuleNamesOf(*ctx.project);
  }
  source::ModuleNames names;
  names.reserve(ctx.sigma.mods.size());
  for (const auto& mod : ctx.sigma.mods) {
    names.insert(core::StringOfPath(mod.path));
  }
  return names;
}

struct ConstLenNameMapCache {
  const ScopeContext* ctx = nullptr;
  const project::Project* project = nullptr;
  std::uint64_t sigma_fingerprint = 0;
  std::size_t mods_size = 0;
  std::size_t types_size = 0;
  std::size_t classes_size = 0;
  NameMapTable name_maps;
};

static std::uint64_t MixFingerprint(std::uint64_t h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static std::uint64_t SigmaFingerprint(const ScopeContext& ctx) {
  std::uint64_t h = 0x24f27f1a18d3c1d1ull;
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.mods.size()));
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.types.size()));
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.classes.size()));
  h = MixFingerprint(
      h, static_cast<std::uint64_t>(
             reinterpret_cast<std::uintptr_t>(ctx.sigma.mods.data())));
  for (const auto& mod : ctx.sigma.mods) {
    h = MixFingerprint(h, static_cast<std::uint64_t>(mod.path.size()));
    for (const auto& seg : mod.path) {
      h = MixFingerprint(h, static_cast<std::uint64_t>(IdKeyOf(seg).size()));
    }
    h = MixFingerprint(h, static_cast<std::uint64_t>(mod.items.size()));
    h = MixFingerprint(
        h, static_cast<std::uint64_t>(
               reinterpret_cast<std::uintptr_t>(mod.items.data())));
  }
  return h;
}

static bool ConstLenNameMapCacheValid(const ScopeContext& ctx,
                                      const ConstLenNameMapCache& cache) {
  return cache.ctx == &ctx && cache.project == ctx.project &&
         cache.sigma_fingerprint == SigmaFingerprint(ctx) &&
         cache.mods_size == ctx.sigma.mods.size() &&
         cache.types_size == ctx.sigma.types.size() &&
         cache.classes_size == ctx.sigma.classes.size();
}

static const NameMapTable& CachedNameMapsForConstLen(const ScopeContext& ctx) {
  static thread_local ConstLenNameMapCache cache;
  if (ConstLenNameMapCacheValid(ctx, cache)) {
    return cache.name_maps;
  }

  auto& mutable_ctx = const_cast<ScopeContext&>(ctx);
  const ast::ModulePath saved_module = mutable_ctx.current_module;
  const auto built = CollectNameMaps(mutable_ctx);
  mutable_ctx.current_module = saved_module;

  cache.ctx = &ctx;
  cache.project = ctx.project;
  cache.sigma_fingerprint = SigmaFingerprint(ctx);
  cache.mods_size = ctx.sigma.mods.size();
  cache.types_size = ctx.sigma.types.size();
  cache.classes_size = ctx.sigma.classes.size();
  cache.name_maps = built.name_maps;
  return cache.name_maps;
}

static std::optional<std::pair<ast::ModulePath, ast::Identifier>>
ResolveValuePathForConstLen(const ScopeContext& ctx,
                            const ast::ModulePath& path,
                            std::string_view name) {
  const auto& name_maps = CachedNameMapsForConstLen(ctx);
  const auto module_names = ModuleNamesForConstLen(ctx);
  const auto value = ResolveQualified(ctx, name_maps, module_names,
                                      path, name, EntityKind::Value, CanAccess);
  if (!value.ok || !value.entity || !value.entity->origin_opt) {
    return std::nullopt;
  }
  ast::Identifier resolved_name =
      value.entity->target_opt ? *value.entity->target_opt
                               : ast::Identifier(name);
  return std::make_pair(*value.entity->origin_opt, std::move(resolved_name));
}

}  // namespace

ConstLenResult ConstLen(const ScopeContext& ctx, const ast::ExprPtr& expr) {
  SpecDefsTypeEquiv();
  if (!expr) {
    SPEC_RULE("ConstLen-Err");
    return {false, "ConstLen-Err", std::nullopt};
  }
  return std::visit(
      [&](const auto& node) -> ConstLenResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          if (node.literal.kind != lexer::TokenKind::IntLiteral) {
            SPEC_RULE("ConstLen-Err");
            return {false, "ConstLen-Err", std::nullopt};
          }
          const auto value = ParseIntLiteralUsize(node.literal.lexeme);
          if (!value.has_value()) {
            SPEC_RULE("ConstLen-Err");
            return {false, "ConstLen-Err", std::nullopt};
          }
          SPEC_RULE("ConstLen-Lit");
          return {true, std::nullopt, *value};
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto ent = ResolveValueName(ctx, node.name);
          if (!ent || !ent->origin_opt) {
            const auto init = FindCurrentModuleStaticInit(ctx, node.name);
            if (!init.has_value()) {
              SPEC_RULE("ConstLen-Err");
              return {false, "ConstLen-Err", std::nullopt};
            }
            const auto nested = ConstLen(ctx, *init);
            if (!nested.ok) {
              return nested;
            }
            SPEC_RULE("ConstLen-Path");
            return nested;
          }
          const auto* module = FindModule(ctx, *ent->origin_opt);
          if (!module) {
            const auto init = FindCurrentModuleStaticInit(ctx, node.name);
            if (!init.has_value()) {
              SPEC_RULE("ConstLen-Err");
              return {false, "ConstLen-Err", std::nullopt};
            }
            const auto nested = ConstLen(ctx, *init);
            if (!nested.ok) {
              return nested;
            }
            SPEC_RULE("ConstLen-Path");
            return nested;
          }
          const auto resolved_name = ent->target_opt.value_or(node.name);
          const auto init = FindStaticInit(*module, resolved_name);
          if (!init.has_value()) {
            const auto current_init = FindCurrentModuleStaticInit(ctx, node.name);
            if (!current_init.has_value()) {
              SPEC_RULE("ConstLen-Err");
              return {false, "ConstLen-Err", std::nullopt};
            }
            const auto nested = ConstLen(ctx, *current_init);
            if (!nested.ok) {
              return nested;
            }
            SPEC_RULE("ConstLen-Path");
            return nested;
          }
          const auto nested = ConstLen(ctx, *init);
          if (!nested.ok) {
            return nested;
          }
          SPEC_RULE("ConstLen-Path");
          return nested;
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          const auto resolved =
              ResolveValuePathForConstLen(ctx, node.path, node.name);
          if (!resolved.has_value()) {
            if (node.path.empty()) {
              const auto init = FindCurrentModuleStaticInit(ctx, node.name);
              if (init.has_value()) {
                const auto nested = ConstLen(ctx, *init);
                if (!nested.ok) {
                  return nested;
                }
                SPEC_RULE("ConstLen-Path");
                return nested;
              }
            }
            SPEC_RULE("ConstLen-Err");
            return {false, "ConstLen-Err", std::nullopt};
          }
          const auto* module = FindModule(ctx, resolved->first);
          if (!module) {
            if (node.path.empty()) {
              const auto init = FindCurrentModuleStaticInit(ctx, node.name);
              if (init.has_value()) {
                const auto nested = ConstLen(ctx, *init);
                if (!nested.ok) {
                  return nested;
                }
                SPEC_RULE("ConstLen-Path");
                return nested;
              }
            }
            SPEC_RULE("ConstLen-Err");
            return {false, "ConstLen-Err", std::nullopt};
          }
          const auto init = FindStaticInit(*module, resolved->second);
          if (!init.has_value()) {
            if (node.path.empty()) {
              const auto current_init = FindCurrentModuleStaticInit(ctx, node.name);
              if (current_init.has_value()) {
                const auto nested = ConstLen(ctx, *current_init);
                if (!nested.ok) {
                  return nested;
                }
                SPEC_RULE("ConstLen-Path");
                return nested;
              }
            }
            SPEC_RULE("ConstLen-Err");
            return {false, "ConstLen-Err", std::nullopt};
          }
          const auto nested = ConstLen(ctx, *init);
          if (!nested.ok) {
            return nested;
          }
          SPEC_RULE("ConstLen-Path");
          return nested;
        } else {
          SPEC_RULE("ConstLen-Err");
          return {false, "ConstLen-Err", std::nullopt};
        }
      },
      expr->node);
}

}  // namespace ultraviolet::analysis
