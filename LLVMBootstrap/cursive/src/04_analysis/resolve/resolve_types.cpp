// =============================================================================
// resolve_types.cpp - Type Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.3 "Lookup" (Lines 6822-6899)
//   CursiveSpecification.md §5.1.6 "Qualified Disambiguation" (Lines 7310-7429)
//   CursiveSpecification.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//
// SOURCE FILE:
//   cursive-bootstrap/src/03_analysis/resolve/resolver_types.cpp (Lines 1-311)
//
// MIGRATED: 2026-02-01
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <utility>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scope_overrides.h"
#include "04_analysis/caps/cap_filesystem.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_network.h"
#include "04_analysis/caps/cap_time.h"
#include "04_analysis/language_service/facts.h"
#include "04_analysis/typing/outcome.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsResolverTypes() {
  SPEC_DEF("ResolveTypePath", "5.1.7");
  SPEC_DEF("ResolveClassPath", "5.1.7");
  SPEC_DEF("ResolveType", "5.1.7");
  SPEC_DEF("ResolveTypeRef", "5.1.7");
  SPEC_DEF("ResolveParams", "5.1.7");
  SPEC_DEF("ResolveParam", "5.1.7");
  SPEC_DEF("FullPath", "5.12");
}


static bool IsFoundationalClassPath(const ast::ClassPath& path) {
  if (path.size() != 1) {
    return false;
  }
  return IdEq(path[0], "Drop") || IdEq(path[0], "Bitcopy") ||
         IdEq(path[0], "Clone") || IdEq(path[0], "Eq") ||
         IdEq(path[0], "Hasher") || IdEq(path[0], "Hash") ||
         IdEq(path[0], "Iterator") || IdEq(path[0], "Step") ||
         IdEq(path[0], "FfiSafe") ||
         // C0X Extension: Structured Concurrency (§18.2.4)
         IsExecutionDomainClassPath(path) ||
         IsReactorClassPath(path);
}

static bool IsSelfAssociatedTypePath(const ast::TypePath& path) {
  return path.size() == 2 && IdEq(path[0], "Self");
}

static bool IsGpuPtrPath(const ast::TypePath& path) {
  return path.size() == 1 && IdEq(path[0], "GpuPtr");
}

static bool IsGpuPtrAddressSpaceArg(const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return false;
  }
  const auto* path = std::get_if<ast::TypePathType>(&type->node);
  if (!path || path->path.size() != 1 || !path->generic_args.empty()) {
    return false;
  }
  return IdEq(path->path[0], "Global") || IdEq(path->path[0], "Shared") ||
         IdEq(path->path[0], "Private");
}

ast::Path FullPath(const ast::Path& path, std::string_view name) {
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

}  // namespace

ResTypePathResult ResolveTypePath(ResolveContext& ctx,
                                 const ast::TypePath& path) {
  SpecDefsResolverTypes();
  ResTypePathResult result;
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names) {
    return result;
  }
  if (path.empty()) {
    return result;
  }
  if (path.size() == 1) {
    if (IsGpuPtrPath(path) ||
        IsFileSystemBuiltinTypePath(path) ||
        IsHeapAllocatorBuiltinTypePath(path) ||
        IsTimeBuiltinTypePath(path) ||
        IsOutcomeTypePath(path)) {
      SPEC_RULE("ResolveTypePath-Ident-Local");
      return {true, std::nullopt, std::nullopt, ast::TypePath{path[0]}};
    }
    const auto ent = ResolveTypeName(*ctx.ctx, path[0]);
    if (!ent.has_value()) {
      return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
    }
    const auto name = ent->target_opt.value_or(path[0]);
    if (ent->origin_opt.has_value()) {
      SPEC_RULE("ResolveTypePath-Ident");
      return {true, std::nullopt, std::nullopt,
              FullPath(*ent->origin_opt, name)};
    }
    SPEC_RULE("ResolveTypePath-Ident-Local");
    return {true, std::nullopt, std::nullopt, ast::TypePath{name}};
  }
  if (IsSelfAssociatedTypePath(path)) {
    SPEC_RULE("ResolveTypePath-Ident-Local");
    return {true, std::nullopt, std::nullopt, path};
  }

  const auto prefix = ast::ModulePath(path.begin(), path.end() - 1);
  const auto name = path.back();
  const auto qualified =
      ResolveQualified(*ctx.ctx, *ctx.name_maps, *ctx.module_names, prefix,
                       name, EntityKind::Type, ctx.can_access);
  if (!qualified.ok || !qualified.entity.has_value()) {
    return {false, qualified.diag_id, std::nullopt, {}};
  }
  const auto ent = *qualified.entity;
  if (!ent.origin_opt.has_value()) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }
  const auto resolved_name = ent.target_opt.value_or(name);
  SPEC_RULE("ResolveTypePath-Qual");
  return {true, std::nullopt, std::nullopt,
          FullPath(*ent.origin_opt, resolved_name)};
}

ResClassPathResult ResolveClassPath(ResolveContext& ctx,
                                   const ast::ClassPath& path) {
  SpecDefsResolverTypes();
  ResClassPathResult result;
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names) {
    return result;
  }
  if (path.empty()) {
    return result;
  }
  if (path.size() == 1) {
    if (IsFileSystemClassPath(path) || IsNetworkClassPath(path) ||
        IsHeapAllocatorClassPath(path) ||
        IsTimeClassPath(path) || IsMonotonicTimeClassPath(path) ||
        IsWallTimeClassPath(path) ||
        IsFoundationalClassPath(path)) {
      SPEC_RULE("ResolveClassPath-Ident");
      return {true, std::nullopt, std::nullopt, ast::ClassPath{path[0]}};
    }
    const auto ent = ResolveClassName(*ctx.ctx, path[0]);
    if (!ent.has_value() || !ent->origin_opt.has_value()) {
      // Defer unknown superclass/class-bound reporting to declaration typing
      // so class-path diagnostics use the type-system error family.
      SPEC_RULE("ResolveClassPath-Ident");
      return {true, std::nullopt, std::nullopt, ast::ClassPath{path[0]}};
    }
    const auto name = ent->target_opt.value_or(path[0]);
    SPEC_RULE("ResolveClassPath-Ident");
    return {true, std::nullopt, std::nullopt,
            FullPath(*ent->origin_opt, name)};
  }

  const auto prefix = ast::ModulePath(path.begin(), path.end() - 1);
  const auto name = path.back();
  const auto qualified =
      ResolveQualified(*ctx.ctx, *ctx.name_maps, *ctx.module_names, prefix,
                       name, EntityKind::Class, ctx.can_access);
  if (!qualified.ok || !qualified.entity.has_value()) {
    return {false, qualified.diag_id, std::nullopt, {}};
  }
  const auto ent = *qualified.entity;
  if (!ent.origin_opt.has_value()) {
    return {false, "ResolveExpr-Ident-Err", std::nullopt, {}};
  }
  const auto resolved_name = ent.target_opt.value_or(name);
  SPEC_RULE("ResolveClassPath-Qual");
  return {true, std::nullopt, std::nullopt,
          FullPath(*ent.origin_opt, resolved_name)};
}

ResTypeResult ResolveType(ResolveContext& ctx,
                          const std::shared_ptr<ast::Type>& type) {
  SpecDefsResolverTypes();
  ResTypeResult result;
  if (!type) {
    return result;
  }
  result.value = type;
  return std::visit(
      [&](const auto& node) -> ResTypeResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          const auto resolved = ResolveTypePath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          RecordLanguageServiceTypePathReference(
              ctx.language_service, *ctx.ctx, resolved.value, type->span);
          auto out = *type;
          auto& out_node = std::get<ast::TypePathType>(out.node);
          out_node.path = resolved.value;
          // Also resolve generic args
          for (auto& arg : out_node.generic_args) {
            const auto resolved_arg = ResolveType(ctx, arg);
            if (!resolved_arg.ok) {
              return {false, resolved_arg.diag_id, std::nullopt, {}};
            }
            arg = resolved_arg.value;
          }
          SPEC_RULE("ResolveType-Path");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          const auto resolved = ResolveTypePath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          RecordLanguageServiceTypePathReference(
              ctx.language_service, *ctx.ctx, resolved.value, type->span);
          std::vector<std::shared_ptr<ast::Type>> resolved_args;
          resolved_args.reserve(node.args.size());
          for (std::size_t index = 0; index < node.args.size(); ++index) {
            const auto& arg = node.args[index];
            if (IsGpuPtrPath(resolved.value) && index == 1 &&
                IsGpuPtrAddressSpaceArg(arg)) {
              resolved_args.push_back(arg);
              continue;
            }
            const auto resolved_arg = ResolveType(ctx, arg);
            if (!resolved_arg.ok) {
              return {false, resolved_arg.diag_id, std::nullopt, {}};
            }
            resolved_args.push_back(resolved_arg.value);
          }

          ast::TypePathType path_type;
          path_type.path = resolved.value;
          path_type.generic_args = std::move(resolved_args);

          auto out = *type;
          out.node = std::move(path_type);
          SPEC_RULE("ResolveType-Path");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          const auto resolved = ResolveClassPath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          if (!resolved.value.empty()) {
            Entity entity{EntityKind::Class,
                          ast::ModulePath(resolved.value.begin(),
                                          resolved.value.end() - 1),
                          resolved.value.back(), EntitySource::Decl};
            RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                           resolved.value.back(), type->span,
                                           entity);
          }
          auto out = *type;
          auto& out_node = std::get<ast::TypeDynamic>(out.node);
          out_node.path = resolved.value;
          SPEC_RULE("ResolveType-Dynamic");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          const auto resolved = ResolveTypePath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          RecordLanguageServiceTypePathReference(
              ctx.language_service, *ctx.ctx, resolved.value, type->span);
          auto out = *type;
          auto& out_node = std::get<ast::TypeModalState>(out.node);
          out_node.path = resolved.value;
          for (auto& arg : out_node.generic_args) {
            const auto resolved_arg = ResolveType(ctx, arg);
            if (!resolved_arg.ok) {
              return {false, resolved_arg.diag_id, std::nullopt, {}};
            }
            arg = resolved_arg.value;
          }
          ast::SyncTypeModalStateFromFields(out_node);
          SPEC_RULE("ResolveType-ModalState");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          const auto resolved = ResolveType(ctx, node.base);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          auto out = *type;
          auto& out_node = std::get<ast::TypePermType>(out.node);
          out_node.base = resolved.value;
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeUnion>(out.node);
          for (auto& elem : out_node.types) {
            const auto resolved = ResolveType(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            elem = resolved.value;
          }
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeFunc>(out.node);
          for (auto& param : out_node.params) {
            const auto resolved = ResolveType(ctx, param.type);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            param.type = resolved.value;
          }
          if (out_node.ret) {
            const auto resolved = ResolveType(ctx, out_node.ret);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            out_node.ret = resolved.value;
          }
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeClosure>(out.node);
          for (auto& param : out_node.params) {
            const auto resolved = ResolveType(ctx, param.type);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            param.type = resolved.value;
          }
          if (out_node.ret) {
            const auto resolved = ResolveType(ctx, out_node.ret);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            out_node.ret = resolved.value;
          }
          if (out_node.deps_opt.has_value()) {
            for (auto& dep : *out_node.deps_opt) {
              const auto resolved = ResolveType(ctx, dep.type);
              if (!resolved.ok) {
                return {false, resolved.diag_id, std::nullopt, {}};
              }
              dep.type = resolved.value;
            }
          }
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeTuple>(out.node);
          for (auto& elem : out_node.elements) {
            const auto resolved = ResolveType(ctx, elem);
            if (!resolved.ok) {
              return {false, resolved.diag_id, std::nullopt, {}};
            }
            elem = resolved.value;
          }
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeArray>(out.node);
          const auto resolved_elem = ResolveType(ctx, out_node.element);
          if (!resolved_elem.ok) {
            return {false, resolved_elem.diag_id, std::nullopt, {}};
          }
          out_node.element = resolved_elem.value;
          if (out_node.length) {
            const auto resolved_len = ResolveExpr(ctx, out_node.length);
            if (!resolved_len.ok) {
              return {false, resolved_len.diag_id, std::nullopt, {}};
            }
            out_node.length = resolved_len.value;
          }
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeSlice>(out.node);
          const auto resolved = ResolveType(ctx, out_node.element);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          out_node.element = resolved.value;
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeSafePtr>(out.node);
          const auto resolved = ResolveType(ctx, out_node.element);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          out_node.element = resolved.value;
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeRawPtr>(out.node);
          const auto resolved = ResolveType(ctx, out_node.element);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          out_node.element = resolved.value;
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          auto out = *type;
          auto& out_node = std::get<ast::TypeRefine>(out.node);
          const auto resolved_base = ResolveType(ctx, out_node.base);
          if (!resolved_base.ok) {
            return {false, resolved_base.diag_id, resolved_base.span, {}};
          }
          out_node.base = resolved_base.value;
          if (out_node.predicate) {
            // Refinement predicates resolve in a scope where `self` denotes
            // the constrained value (CursiveSpecification.md §13.7.1, §13.7.2).
            ScopedLeadingScope pred_scope(*ctx.ctx);
            const auto intro = Intro(
                *ctx.ctx, "self",
                Entity{EntityKind::Value, std::nullopt, std::nullopt,
                       EntitySource::Decl});
            if (!intro.ok) {
              return {false, intro.diag_id,
                      out_node.predicate ? out_node.predicate->span : type->span,
                      {}};
            }

            const auto resolved_pred = ResolveExpr(ctx, out_node.predicate);
            if (!resolved_pred.ok) {
              return {false, resolved_pred.diag_id, resolved_pred.span, {}};
            }
            out_node.predicate = resolved_pred.value;
          }
          SPEC_RULE("ResolveType-Refine");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          const auto resolved = ResolveClassPath(ctx, node.path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, std::nullopt, {}};
          }
          if (!resolved.value.empty()) {
            Entity entity{EntityKind::Class,
                          ast::ModulePath(resolved.value.begin(),
                                          resolved.value.end() - 1),
                          resolved.value.back(), EntitySource::Decl};
            RecordLanguageServiceReference(ctx.language_service, *ctx.ctx,
                                           resolved.value.back(), type->span,
                                           entity);
          }
          auto out = *type;
          auto& out_node = std::get<ast::TypeOpaque>(out.node);
          out_node.path = resolved.value;
          SPEC_RULE("ResolveType-Opaque");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          auto out = *type;
          auto& out_node = std::get<T>(out.node);
          const auto resolved_base = ResolveType(ctx, out_node.base);
          if (!resolved_base.ok) {
            return {false, resolved_base.diag_id, std::nullopt, {}};
          }
          out_node.base = resolved_base.value;
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt,
                  std::make_shared<ast::Type>(std::move(out))};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFull>) {
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt, type};
        } else {
          // TypePrim, TypeString, TypeBytes - no sub-resolution needed
          SPEC_RULE("ResolveType-Hom");
          return {true, std::nullopt, std::nullopt, type};
        }
      },
      type->node);
}

}  // namespace cursive::analysis
