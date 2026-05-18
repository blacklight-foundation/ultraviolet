// =================================================================
// File: 04_analysis/typing/expr/dispatch_expr.cpp
// Construct: Dispatch Expression Type Checking (data parallelism)
// Spec Section: 17.2.3
// Spec Rules: T-Dispatch, T-Dispatch-Reduce
// =================================================================
//
// DISPATCH EXPRESSION (dispatch pat in range key? opts { body }):
//   1. Verify inside parallel block
//   2. Type range expression (must be range type)
//   3. Bind pattern to iteration index
//   4. Process key clause if present
//   5. Process options (reduce, ordered, chunk)
//   6. Type body expression
//   7. Compute result type based on options
//
// DISPATCH OPTIONS:
//   - reduce: op - reduction operation (+, *, min, max, and, or)
//   - ordered - preserve iteration order for side effects
//   - chunk: expr - set chunk size for work distribution
//
// KEY CLAUSE:
//   - key path mode - acquire keys for parallel access
//   - Enables safe parallel mutation
//   - Key pattern determines parallelism
//
// =================================================================

#include "04_analysis/typing/expr/dispatch_expr.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/typecheck.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsDispatch() {
  SPEC_DEF("T-Dispatch", "17.2.3");
  SPEC_DEF("T-Dispatch-Reduce", "17.2.3");
  SPEC_DEF("T-GPU-Dispatch", "20.5.4");
  SPEC_DEF("T-GPU-Dispatch-Reduce", "20.5.4");
  SPEC_DEF("GpuCapture-Shared-Err", "20.3.4");
  SPEC_DEF("GpuCapture-HeapProv-Err", "20.3.4");
  SPEC_DEF("GpuCapture-NonGpuSafe-Err", "20.3.4");
  SPEC_DEF("GpuContext", "20.2.3");
  SPEC_DEF("Dim3Const-Err", "20.2.4");
  SPEC_DEF("WorkgroupSize-Err", "20.2.4");
}

static TypeRef StripPermRefine(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static bool IsUsizeType(const TypeRef& type) {
  const auto stripped = StripPermRefine(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  return prim && prim->name == "usize";
}

static bool IsGpuDomainType(const TypeRef& type) {
  const auto stripped = StripPermRefine(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
}

static void EmitSupplementalTypeDiag(const StmtTypeContext& type_ctx,
                                     std::string_view code) {
  if (!type_ctx.diags) {
    return;
  }
  if (auto diag = core::MakeDiagnosticById(code)) {
    core::Emit(*type_ctx.diags, *diag);
    return;
  }
}

static std::optional<TypeRef> InferDispatchIndexType(
    const TypeRef& range_type) {
  return ::ultraviolet::analysis::RangeElementType(range_type);
}

enum class DispatchUseMode {
  Read,
  Write,
};

struct DispatchSchemaSeg {
  bool is_index = false;
  std::string key;
  ast::ExprPtr index_expr;
};

struct DispatchSchema {
  std::string root;
  std::vector<DispatchSchemaSeg> segs;
  core::Span span;
};

struct DispatchAccess {
  DispatchSchema schema;
  DispatchUseMode mode = DispatchUseMode::Read;
  core::Span span;
};

struct DispatchCandidateUse {
  ast::ExprPtr expr;
  DispatchUseMode mode = DispatchUseMode::Read;
  core::Span span;
  std::unordered_set<IdKey> local_names;
};

struct DispatchInferenceResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<DispatchAccess> spec;
};

static std::string JoinModulePath(const ast::ModulePath& path) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      oss << "::";
    }
    oss << path[i];
  }
  return oss.str();
}

static ast::ExprPtr StripDispatchAttrs(const ast::ExprPtr& expr) {
  ast::ExprPtr cur = expr;
  while (cur) {
    const auto* attributed = std::get_if<ast::AttributedExpr>(&cur->node);
    if (!attributed) {
      break;
    }
    cur = attributed->expr;
  }
  return cur;
}

static std::optional<std::string> CanonicalDispatchIndexExpr(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names);

static bool IsDispatchConstantExpr(const ast::ExprPtr& expr);
static bool IsStaticDispatchIndexExpr(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names);
static std::unordered_set<IdKey> DispatchPatternNameSet(
    const ast::PatternPtr& pattern);

static std::optional<int64_t> ParseDispatchIntLiteral(std::string_view lexeme) {
  std::string digits;
  bool saw_digit = false;
  for (const char ch : lexeme) {
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      digits.push_back(ch);
      saw_digit = true;
      continue;
    }
    if (ch == '_') {
      continue;
    }
    break;
  }
  if (!saw_digit || digits.empty()) {
    return std::nullopt;
  }
  try {
    return static_cast<int64_t>(std::stoll(digits));
  } catch (...) {
    return std::nullopt;
  }
}

struct AffineDispatchIndex {
  std::optional<IdKey> var;
  int64_t offset = 0;
};

static std::optional<AffineDispatchIndex> TryAffineDispatchIndex(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names) {
  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<AffineDispatchIndex> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          const auto parsed = ParseDispatchIntLiteral(node.literal.lexeme);
          if (!parsed.has_value()) {
            return std::nullopt;
          }
          return AffineDispatchIndex{std::nullopt, *parsed};
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto key = IdKeyOf(node.name);
          if (pattern_names.find(key) == pattern_names.end()) {
            return std::nullopt;
          }
          return AffineDispatchIndex{key, 0};
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (!node.path.empty()) {
            return std::nullopt;
          }
          const auto key = IdKeyOf(node.name);
          if (pattern_names.find(key) == pattern_names.end()) {
            return std::nullopt;
          }
          return AffineDispatchIndex{key, 0};
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto inner = TryAffineDispatchIndex(node.value, pattern_names);
          if (!inner.has_value()) {
            return std::nullopt;
          }
          if (node.op == "+") {
            return inner;
          }
          if (node.op == "-" && !inner->var.has_value()) {
            inner->offset = -inner->offset;
            return inner;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          const auto lhs = TryAffineDispatchIndex(node.lhs, pattern_names);
          const auto rhs = TryAffineDispatchIndex(node.rhs, pattern_names);
          if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
          }
          if (node.op == "+") {
            if (lhs->var.has_value() && !rhs->var.has_value()) {
              return AffineDispatchIndex{lhs->var, lhs->offset + rhs->offset};
            }
            if (!lhs->var.has_value() && rhs->var.has_value()) {
              return AffineDispatchIndex{rhs->var, lhs->offset + rhs->offset};
            }
            if (!lhs->var.has_value() && !rhs->var.has_value()) {
              return AffineDispatchIndex{std::nullopt,
                                         lhs->offset + rhs->offset};
            }
            return std::nullopt;
          }
          if (node.op == "-") {
            if (lhs->var.has_value() && !rhs->var.has_value()) {
              return AffineDispatchIndex{lhs->var, lhs->offset - rhs->offset};
            }
            if (!lhs->var.has_value() && !rhs->var.has_value()) {
              return AffineDispatchIndex{std::nullopt,
                                         lhs->offset - rhs->offset};
            }
            return std::nullopt;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return TryAffineDispatchIndex(node.value, pattern_names);
        } else {
          return std::nullopt;
        }
      },
      stripped->node);
}

static bool IsConstDispatchBinding(const TypeEnv& env, std::string_view name) {
  const auto binding = BindOf(env, name);
  return binding.has_value() && PermOfType(binding->type) == Permission::Const;
}

static bool DispatchIndexExprInvariant(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names,
    const TypeEnv& env,
    const std::unordered_set<IdKey>& local_names) {
  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto key = IdKeyOf(node.name);
          if (local_names.find(key) != local_names.end() &&
              pattern_names.find(key) == pattern_names.end()) {
            return false;
          }
          return pattern_names.find(key) != pattern_names.end() ||
                 IsConstDispatchBinding(env, node.name);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (!node.path.empty()) {
            return true;
          }
          const auto key = IdKeyOf(node.name);
          if (local_names.find(key) != local_names.end() &&
              pattern_names.find(key) == pattern_names.end()) {
            return false;
          }
          return pattern_names.find(key) != pattern_names.end() ||
                 IsConstDispatchBinding(env, node.name);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return DispatchIndexExprInvariant(node.base, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return DispatchIndexExprInvariant(node.base, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return DispatchIndexExprInvariant(node.base, pattern_names, env,
                                            local_names) &&
                 DispatchIndexExprInvariant(node.index, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return DispatchIndexExprInvariant(node.value, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return DispatchIndexExprInvariant(node.lhs, pattern_names, env,
                                            local_names) &&
                 DispatchIndexExprInvariant(node.rhs, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return DispatchIndexExprInvariant(node.value, pattern_names, env,
                                            local_names);
        } else {
          return false;
        }
      },
      stripped->node);
}

static bool DispatchInvariantKeyPathExpr(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names,
    const TypeEnv& env,
    const std::unordered_set<IdKey>& local_names) {
  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr> ||
                      std::is_same_v<T, ast::PathExpr> ||
                      std::is_same_v<T, ast::QualifiedNameExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return DispatchInvariantKeyPathExpr(node.base, pattern_names, env,
                                              local_names);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return DispatchInvariantKeyPathExpr(node.base, pattern_names, env,
                                              local_names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return DispatchInvariantKeyPathExpr(node.base, pattern_names, env,
                                              local_names) &&
                 DispatchIndexExprInvariant(node.index, pattern_names, env,
                                            local_names);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return DispatchInvariantKeyPathExpr(node.receiver, pattern_names, env,
                                              local_names);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return DispatchInvariantKeyPathExpr(node.place, pattern_names, env,
                                              local_names);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return DispatchInvariantKeyPathExpr(node.value, pattern_names, env,
                                              local_names);
        } else {
          return false;
        }
      },
      stripped->node);
}

static std::optional<std::string> CanonicalDispatchIndexExpr(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names) {
  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return std::string(node.literal.lexeme);
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto key = IdKeyOf(node.name);
          if (pattern_names.find(key) != pattern_names.end()) {
            return "$" + std::string(node.name);
          }
          return std::string(node.name);
        } else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          return JoinModulePath(node.path) + "::" + std::string(node.name);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (node.path.empty()) {
            const auto key = IdKeyOf(node.name);
            if (pattern_names.find(key) != pattern_names.end()) {
              return "$" + std::string(node.name);
            }
            return std::string(node.name);
          }
          return JoinModulePath(node.path) + "::" + std::string(node.name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          const auto base = CanonicalDispatchIndexExpr(node.base, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return *base + "." + std::string(node.name);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          const auto base = CanonicalDispatchIndexExpr(node.base, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return *base + "." + ast::FormatTupleIndex(node.index);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          const auto base = CanonicalDispatchIndexExpr(node.base, pattern_names);
          const auto index =
              CanonicalDispatchIndexExpr(node.index, pattern_names);
          if (!base.has_value() || !index.has_value()) {
            return std::nullopt;
          }
          return *base + "[" + *index + "]";
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          const auto inner =
              CanonicalDispatchIndexExpr(node.value, pattern_names);
          if (!inner.has_value()) {
            return std::nullopt;
          }
          return "(" + std::string(node.op) + *inner + ")";
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          const auto lhs = CanonicalDispatchIndexExpr(node.lhs, pattern_names);
          const auto rhs = CanonicalDispatchIndexExpr(node.rhs, pattern_names);
          if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
          }
          return "(" + *lhs + std::string(node.op) + *rhs + ")";
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          const auto inner =
              CanonicalDispatchIndexExpr(node.value, pattern_names);
          if (!inner.has_value()) {
            return std::nullopt;
          }
          return "cast(" + *inner + ")";
        } else {
          return std::nullopt;
        }
      },
      stripped->node);
}

static bool ProvablyDisjointDispatchIndexExpr(
    const ast::ExprPtr& lhs,
    const ast::ExprPtr& rhs,
    const std::unordered_set<IdKey>& pattern_names) {
  const auto lhs_key = CanonicalDispatchIndexExpr(lhs, pattern_names);
  const auto rhs_key = CanonicalDispatchIndexExpr(rhs, pattern_names);
  if (lhs_key.has_value() && rhs_key.has_value() && *lhs_key == *rhs_key) {
    return false;
  }

  const auto lhs_aff = TryAffineDispatchIndex(lhs, pattern_names);
  const auto rhs_aff = TryAffineDispatchIndex(rhs, pattern_names);
  if (!lhs_aff.has_value() || !rhs_aff.has_value()) {
    return false;
  }

  if (!lhs_aff->var.has_value() && !rhs_aff->var.has_value()) {
    return lhs_aff->offset != rhs_aff->offset;
  }

  if (lhs_aff->var.has_value() && rhs_aff->var.has_value() &&
      lhs_aff->var == rhs_aff->var) {
    return lhs_aff->offset != rhs_aff->offset;
  }

  return false;
}

static std::optional<DispatchSchema> TryBuildDispatchSchema(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names) {
  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return std::nullopt;
  }

  return std::visit(
      [&](const auto& node) -> std::optional<DispatchSchema> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return DispatchSchema{std::string(node.name), {}, stripped->span};
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (!node.path.empty()) {
            return std::nullopt;
          }
          return DispatchSchema{std::string(node.name), {}, stripped->span};
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto base = TryBuildDispatchSchema(node.base, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          base->segs.push_back(
              DispatchSchemaSeg{false, std::string(node.name), nullptr});
          base->span = stripped->span;
          return base;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto base = TryBuildDispatchSchema(node.base, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          base->segs.push_back(DispatchSchemaSeg{
              false, ast::FormatTupleIndex(node.index), nullptr});
          base->span = stripped->span;
          return base;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base = TryBuildDispatchSchema(node.base, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          const auto key =
              CanonicalDispatchIndexExpr(node.index, pattern_names);
          if (!key.has_value()) {
            return std::nullopt;
          }
          base->segs.push_back(DispatchSchemaSeg{true, *key, node.index});
          base->span = stripped->span;
          return base;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto base = TryBuildDispatchSchema(node.receiver, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          base->span = stripped->span;
          return base;
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return TryBuildDispatchSchema(node.place, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto base = TryBuildDispatchSchema(node.value, pattern_names);
          if (!base.has_value()) {
            return std::nullopt;
          }
          base->span = stripped->span;
          return base;
        } else {
          return std::nullopt;
        }
      },
      stripped->node);
}

static std::optional<std::string_view> RootBindingOfPlace(
    const ast::ExprPtr& place) {
  if (!place) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<std::string_view> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return std::string_view(node.name);
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (node.path.empty()) {
            return std::string_view(node.name);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return RootBindingOfPlace(node.base);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return RootBindingOfPlace(node.place);
        } else {
          return std::nullopt;
        }
      },
      place->node);
}

static DispatchSchema SchemaOfKeyPathExpr(
    const ast::KeyPathExpr& path,
    const std::unordered_set<IdKey>& pattern_names) {
  DispatchSchema schema;
  schema.root = path.root;
  schema.span = path.span;
  for (const auto& seg : path.segs) {
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      schema.segs.push_back(
          DispatchSchemaSeg{false, std::string(field->name), nullptr});
      continue;
    }
    const auto& index = std::get<ast::KeySegIndex>(seg);
    const auto key = CanonicalDispatchIndexExpr(index.expr, pattern_names)
                         .value_or("<?>");
    schema.segs.push_back(DispatchSchemaSeg{true, key, index.expr});
  }
  return schema;
}

static std::string DispatchSchemaKey(const DispatchSchema& schema) {
  std::ostringstream oss;
  oss << schema.root;
  for (const auto& seg : schema.segs) {
    if (seg.is_index) {
      oss << "[" << seg.key << "]";
    } else {
      oss << "." << seg.key;
    }
  }
  return oss.str();
}

static DispatchUseMode JoinDispatchMode(DispatchUseMode lhs,
                                        DispatchUseMode rhs) {
  if (lhs == DispatchUseMode::Read && rhs == DispatchUseMode::Read) {
    return DispatchUseMode::Read;
  }
  return DispatchUseMode::Write;
}

static bool KeyModeCompatible(DispatchUseMode lhs, DispatchUseMode rhs) {
  return lhs == DispatchUseMode::Read && rhs == DispatchUseMode::Read;
}

static bool SegEqForDispatch(const DispatchSchemaSeg& lhs,
                             const DispatchSchemaSeg& rhs,
                             const std::unordered_set<IdKey>& pattern_names) {
  if (lhs.is_index != rhs.is_index) {
    return false;
  }
  if (!lhs.is_index) {
    return lhs.key == rhs.key;
  }
  if (!lhs.index_expr || !rhs.index_expr) {
    return lhs.key == rhs.key;
  }
  const auto lhs_key =
      CanonicalDispatchIndexExpr(lhs.index_expr, pattern_names);
  const auto rhs_key =
      CanonicalDispatchIndexExpr(rhs.index_expr, pattern_names);
  return lhs_key.has_value() && rhs_key.has_value() && *lhs_key == *rhs_key;
}

static bool SegmentProvablyDisjoint(
    const DispatchSchemaSeg& lhs,
    const DispatchSchemaSeg& rhs,
    const std::unordered_set<IdKey>& pattern_names) {
  if (lhs.is_index != rhs.is_index) {
    return false;
  }
  if (!lhs.is_index) {
    return lhs.key != rhs.key;
  }
  if (!lhs.index_expr || !rhs.index_expr) {
    return false;
  }
  return ProvablyDisjointDispatchIndexExpr(lhs.index_expr, rhs.index_expr,
                                           pattern_names);
}

static bool ProvablyDisjointPath(const DispatchSchema& lhs,
                                 const DispatchSchema& rhs,
                                 const std::unordered_set<IdKey>& pattern_names) {
  if (lhs.root != rhs.root) {
    return true;
  }

  const std::size_t seg_count = std::min(lhs.segs.size(), rhs.segs.size());
  for (std::size_t i = 0; i < seg_count; ++i) {
    bool prefix_equal = true;
    for (std::size_t j = 0; j < i; ++j) {
      if (!SegEqForDispatch(lhs.segs[j], rhs.segs[j], pattern_names)) {
        prefix_equal = false;
        break;
      }
    }
    if (!prefix_equal) {
      return false;
    }
    if (SegmentProvablyDisjoint(lhs.segs[i], rhs.segs[i], pattern_names)) {
      return true;
    }
    if (!SegEqForDispatch(lhs.segs[i], rhs.segs[i], pattern_names)) {
      return false;
    }
  }
  return false;
}

static std::vector<DispatchAccess> MergeDispatchAccesses(
    const std::vector<DispatchAccess>& raw) {
  std::vector<DispatchAccess> merged;
  std::unordered_map<std::string, std::size_t> by_key;
  for (const auto& access : raw) {
    const std::string key = DispatchSchemaKey(access.schema);
    const auto [it, inserted] = by_key.emplace(key, merged.size());
    if (inserted) {
      merged.push_back(access);
      continue;
    }
    auto& existing = merged[it->second];
    existing.mode = JoinDispatchMode(existing.mode, access.mode);
  }
  return merged;
}

static bool DynamicKeyPattern(const std::vector<DispatchAccess>& spec,
                              const std::unordered_set<IdKey>& pattern_names) {
  for (const auto& access : spec) {
    for (const auto& seg : access.schema.segs) {
      if (!seg.is_index || !seg.index_expr) {
        continue;
      }
      if (!IsDispatchConstantExpr(seg.index_expr) &&
          !IsStaticDispatchIndexExpr(seg.index_expr, pattern_names)) {
        return true;
      }
    }
  }
  return false;
}

struct DispatchCaptureInfo {
  std::unordered_set<IdKey> captures;
  std::unordered_set<IdKey> explicit_moves;
};

class DispatchImplicitUseCollector {
 public:
  explicit DispatchImplicitUseCollector(const TypeEnv& env) : env_(env) {
    local_scopes_.emplace_back();
  }

  std::vector<DispatchCandidateUse> Collect(const ast::Block& body,
                                            const ast::PatternPtr& pattern) {
    PushScope();
    DeclarePattern(pattern);
    for (const auto& stmt : body.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(body.tail_opt, DispatchUseMode::Read);
    PopScope();
    return uses_;
  }

 private:
  void PushScope() { local_scopes_.emplace_back(); }

  void PopScope() {
    if (!local_scopes_.empty()) {
      local_scopes_.pop_back();
    }
  }

  bool IsLocal(const IdKey& name) const {
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
      if (it->find(name) != it->end()) {
        return true;
      }
    }
    return false;
  }

  void DeclareName(std::string_view name) {
    if (local_scopes_.empty()) {
      local_scopes_.emplace_back();
    }
    local_scopes_.back().insert(IdKeyOf(name));
  }

  void DeclarePattern(const ast::PatternPtr& pattern) {
    if (!pattern) {
      return;
    }
    std::vector<IdKey> names;
    CollectPatNames(*pattern, names);
    for (const auto& name : names) {
      if (local_scopes_.empty()) {
        local_scopes_.emplace_back();
      }
      local_scopes_.back().insert(name);
    }
  }

  bool RecordAccess(const ast::ExprPtr& expr, DispatchUseMode mode) {
    if (!expr) {
      return false;
    }
    const auto built = BuildKeyPath(expr);
    if (!built.success || built.path.root.empty()) {
      return false;
    }
    const auto root = IdKeyOf(built.path.root);
    if (IsLocal(root)) {
      return false;
    }
    const auto binding = BindOf(env_, built.path.root);
    if (!binding.has_value() ||
        PermOfType(binding->type) != Permission::Shared) {
      return false;
    }
    std::unordered_set<IdKey> locals;
    for (const auto& scope : local_scopes_) {
      locals.insert(scope.begin(), scope.end());
    }
    uses_.push_back(DispatchCandidateUse{expr, mode, expr->span,
                                         std::move(locals)});
    return true;
  }

  void VisitStmt(const ast::Stmt& stmt);
  void VisitExpr(const ast::ExprPtr& expr, DispatchUseMode mode);

  const TypeEnv& env_;
  std::vector<std::unordered_set<IdKey>> local_scopes_;
  std::vector<DispatchCandidateUse> uses_;
};

class DispatchCaptureCollector {
 public:
  explicit DispatchCaptureCollector(const TypeEnv& env) : env_(env) {
    local_scopes_.emplace_back();
  }

  DispatchCaptureInfo Collect(const ast::Block& body,
                              const ast::PatternPtr& iter_pattern) {
    PushScope();
    DeclarePattern(iter_pattern);
    for (const auto& stmt : body.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(body.tail_opt);
    PopScope();
    return {captures_, explicit_moves_};
  }

 private:
  void PushScope() { local_scopes_.emplace_back(); }
  void PopScope() {
    if (!local_scopes_.empty()) {
      local_scopes_.pop_back();
    }
  }

  bool IsLocal(const IdKey& name) const {
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
      if (it->find(name) != it->end()) {
        return true;
      }
    }
    return false;
  }

  void DeclareName(std::string_view name) {
    if (local_scopes_.empty()) {
      local_scopes_.emplace_back();
    }
    local_scopes_.back().insert(IdKeyOf(name));
  }

  void DeclarePattern(const ast::PatternPtr& pattern) {
    if (!pattern) {
      return;
    }
    std::vector<IdKey> names;
    CollectPatNames(*pattern, names);
    for (const auto& name : names) {
      if (local_scopes_.empty()) {
        local_scopes_.emplace_back();
      }
      local_scopes_.back().insert(name);
    }
  }

  void CaptureIfOuter(std::string_view name) {
    const auto key = IdKeyOf(name);
    if (IsLocal(key)) {
      return;
    }
    if (BindOf(env_, name).has_value()) {
      captures_.insert(key);
    }
  }

  void MarkExplicitMoveIfOuter(const ast::ExprPtr& place) {
    const auto root = RootBindingOfPlace(place);
    if (!root.has_value()) {
      return;
    }
    const auto key = IdKeyOf(*root);
    if (IsLocal(key)) {
      return;
    }
    if (!BindOf(env_, *root).has_value()) {
      return;
    }
    explicit_moves_.insert(key);
  }

  void VisitKeyPath(const ast::KeyPathExpr& path) {
    CaptureIfOuter(path.root);
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitStmt(const ast::Stmt& stmt) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::LetStmt>) {
            VisitExpr(node.binding.init);
            DeclarePattern(node.binding.pat);
          } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
            if (node.binding.init) {
              VisitExpr(node.binding.init);
            }
            DeclarePattern(node.binding.pat);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression,
            // but the alias name still enters the surrounding scope.
            DeclareName(node.alias);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                               std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt> ||
                               std::is_same_v<T, ast::RegionStmt> ||
                               std::is_same_v<T, ast::FrameStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          }
        },
        stmt);
  }

  void VisitBlock(const ast::Block& block) {
    PushScope();
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(block.tail_opt);
    PopScope();
  }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
            CaptureIfOuter(node.name);
          } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
            if (node.path.empty()) {
              CaptureIfOuter(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            if (std::holds_alternative<ast::ParenArgs>(node.args)) {
              const auto& args = std::get<ast::ParenArgs>(node.args).args;
              for (const auto& arg : args) {
                VisitExpr(arg.value);
              }
            } else {
              const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
              for (const auto& field : fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                               std::is_same_v<T, ast::CastExpr> ||
                               std::is_same_v<T, ast::DerefExpr> ||
                               std::is_same_v<T, ast::PropagateExpr> ||
                               std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::TransmuteExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            MarkExplicitMoveIfOuter(node.place);
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              VisitExpr(elem);
            }
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
              VisitExpr(elem);
            });
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            VisitExpr(node.value);
            VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (!node.payload_opt.has_value()) {
              return;
            }
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      VisitExpr(elem);
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      VisitExpr(field.value);
                    }
                  }
                },
                *node.payload_opt);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                               std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& case_clause : node.cases) {
              PushScope();
              DeclarePattern(case_clause.pattern);
              VisitExpr(case_clause.body);
              PopScope();
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            PushScope();
            DeclarePattern(node.pattern);
            VisitExpr(node.then_expr);
            PopScope();
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            PushScope();
            DeclarePattern(node.pattern);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            PushScope();
            for (const auto& param : node.params) {
              DeclareName(param.name);
            }
            VisitExpr(node.body);
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            PushScope();
            DeclarePattern(node.pattern);
            if (node.body) {
              for (const auto& stmt : node.body->stmts) {
                VisitStmt(stmt);
              }
              VisitExpr(node.body->tail_opt);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              if (arm.pattern) {
                PushScope();
                DeclarePattern(arm.pattern);
                VisitExpr(arm.handler.value);
                PopScope();
              } else {
                VisitExpr(arm.handler.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          }
        },
        expr->node);
  }

  const TypeEnv& env_;
  std::vector<std::unordered_set<IdKey>> local_scopes_;
  std::unordered_set<IdKey> captures_;
  std::unordered_set<IdKey> explicit_moves_;
};

void DispatchImplicitUseCollector::VisitStmt(const ast::Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          VisitExpr(node.binding.init, DispatchUseMode::Read);
          DeclarePattern(node.binding.pat);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          VisitExpr(node.binding.init, DispatchUseMode::Read);
          DeclarePattern(node.binding.pat);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression,
          // but the alias name still enters the surrounding scope.
          DeclareName(node.alias);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          VisitExpr(node.place, DispatchUseMode::Write);
          VisitExpr(node.value, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          VisitExpr(node.place, DispatchUseMode::Write);
          VisitExpr(node.value, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          VisitExpr(node.value, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          VisitExpr(node.value_opt, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            PushScope();
            for (const auto& nested : node.body->stmts) {
              VisitStmt(nested);
            }
            VisitExpr(node.body->tail_opt, DispatchUseMode::Read);
            PopScope();
          }
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return;
        }
      },
      stmt);
}

void DispatchImplicitUseCollector::VisitExpr(const ast::ExprPtr& expr,
                                             DispatchUseMode mode) {
  if (!expr) {
    return;
  }

  const ast::ExprPtr stripped = StripDispatchAttrs(expr);
  if (!stripped) {
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr> ||
                      std::is_same_v<T, ast::PathExpr> ||
                      std::is_same_v<T, ast::FieldAccessExpr> ||
                      std::is_same_v<T, ast::TupleAccessExpr> ||
                      std::is_same_v<T, ast::DerefExpr>) {
          RecordAccess(stripped, mode);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          RecordAccess(stripped, mode);
          VisitExpr(node.index, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          RecordAccess(stripped, mode);
          for (const auto& arg : node.args) {
            VisitExpr(arg.value, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          RecordAccess(node.place, mode);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& args = std::get<ast::ParenArgs>(node.args).args;
            for (const auto& arg : args) {
              VisitExpr(arg.value, DispatchUseMode::Read);
            }
          } else {
            const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
            for (const auto& field : fields) {
              VisitExpr(field.value, DispatchUseMode::Read);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          VisitExpr(node.lhs, DispatchUseMode::Read);
          VisitExpr(node.rhs, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          VisitExpr(node.value, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          VisitExpr(node.expr, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          VisitExpr(node.place, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          VisitExpr(node.cond, DispatchUseMode::Read);
          VisitExpr(node.then_expr, DispatchUseMode::Read);
          VisitExpr(node.else_expr, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            VisitExpr(elem, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            VisitExpr(elem, DispatchUseMode::Read);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          VisitExpr(node.value, DispatchUseMode::Read);
          VisitExpr(node.count, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            VisitExpr(field.value, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return;
          }
          std::visit(
              [&](const auto& payload) {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                  for (const auto& elem : payload.elements) {
                    VisitExpr(elem, DispatchUseMode::Read);
                  }
                } else {
                  for (const auto& field : payload.fields) {
                    VisitExpr(field.value, DispatchUseMode::Read);
                  }
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          VisitExpr(node.callee, DispatchUseMode::Read);
          for (const auto& arg : node.args) {
            VisitExpr(arg.value, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          VisitExpr(node.scrutinee, DispatchUseMode::Read);
          for (const auto& clause : node.cases) {
            PushScope();
            DeclarePattern(clause.pattern);
            VisitExpr(clause.body, DispatchUseMode::Read);
            PopScope();
          }
          VisitExpr(node.else_expr, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          VisitExpr(node.scrutinee, DispatchUseMode::Read);
          PushScope();
          DeclarePattern(node.pattern);
          VisitExpr(node.then_expr, DispatchUseMode::Read);
          PopScope();
          VisitExpr(node.else_expr, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value()) {
            VisitExpr(node.invariant_opt->predicate, DispatchUseMode::Read);
          }
          if (node.body) {
            PushScope();
            for (const auto& nested : node.body->stmts) {
              VisitStmt(nested);
            }
            VisitExpr(node.body->tail_opt, DispatchUseMode::Read);
            PopScope();
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          VisitExpr(node.cond, DispatchUseMode::Read);
          if (node.invariant_opt.has_value()) {
            VisitExpr(node.invariant_opt->predicate, DispatchUseMode::Read);
          }
          if (node.body) {
            PushScope();
            for (const auto& nested : node.body->stmts) {
              VisitStmt(nested);
            }
            VisitExpr(node.body->tail_opt, DispatchUseMode::Read);
            PopScope();
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          VisitExpr(node.iter, DispatchUseMode::Read);
          PushScope();
          DeclarePattern(node.pattern);
          if (node.invariant_opt.has_value()) {
            VisitExpr(node.invariant_opt->predicate, DispatchUseMode::Read);
          }
          if (node.body) {
            for (const auto& nested : node.body->stmts) {
              VisitStmt(nested);
            }
            VisitExpr(node.body->tail_opt, DispatchUseMode::Read);
          }
          PopScope();
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            PushScope();
            for (const auto& nested : node.block->stmts) {
              VisitStmt(nested);
            }
            VisitExpr(node.block->tail_opt, DispatchUseMode::Read);
            PopScope();
          }
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          PushScope();
          for (const auto& param : node.params) {
            DeclareName(param.name);
          }
          VisitExpr(node.body, DispatchUseMode::Read);
          PopScope();
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          VisitExpr(node.lhs, DispatchUseMode::Read);
          VisitExpr(node.rhs, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          VisitExpr(node.domain, DispatchUseMode::Read);
          for (const auto& opt : node.opts) {
            VisitExpr(opt.value, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            VisitExpr(opt.value, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          VisitExpr(node.handle, DispatchUseMode::Read);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          VisitExpr(node.range, DispatchUseMode::Read);
          for (const auto& opt : node.opts) {
            VisitExpr(opt.chunk_expr, DispatchUseMode::Read);
            VisitExpr(opt.workgroup_expr, DispatchUseMode::Read);
          }
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            VisitExpr(arm.expr, DispatchUseMode::Read);
            if (arm.pattern) {
              PushScope();
              DeclarePattern(arm.pattern);
              VisitExpr(arm.handler.value, DispatchUseMode::Read);
              PopScope();
            } else {
              VisitExpr(arm.handler.value, DispatchUseMode::Read);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& sub : node.exprs) {
            VisitExpr(sub, DispatchUseMode::Read);
          }
        }
      },
      stripped->node);
}

static DispatchInferenceResult InferDispatchAccesses(
    const ast::PatternPtr& pattern,
    const ast::Block& body,
    const TypeEnv& env) {
  DispatchInferenceResult result;
  const auto pattern_names = DispatchPatternNameSet(pattern);
  const auto candidates = DispatchImplicitUseCollector(env).Collect(body, pattern);
  std::vector<DispatchAccess> raw;
  raw.reserve(candidates.size());

  for (const auto& candidate : candidates) {
    if (!DispatchInvariantKeyPathExpr(candidate.expr, pattern_names, env,
                                      candidate.local_names)) {
      SPEC_RULE("Dispatch-Infer-Err");
      result.ok = false;
      result.diag_id = "E-CON-0141";
      return result;
    }

    const auto schema = TryBuildDispatchSchema(candidate.expr, pattern_names);
    if (!schema.has_value()) {
      SPEC_RULE("Dispatch-Infer-Err");
      result.ok = false;
      result.diag_id = "E-CON-0141";
      return result;
    }

    raw.push_back(DispatchAccess{*schema, candidate.mode, candidate.span});
  }

  result.spec = MergeDispatchAccesses(raw);
  return result;
}

static std::unordered_set<IdKey> DispatchPatternNameSet(
    const ast::PatternPtr& pattern) {
  std::vector<IdKey> names;
  if (pattern) {
    CollectPatNames(*pattern, names);
  }
  return {names.begin(), names.end()};
}

static bool IsDispatchConstantExpr(const ast::ExprPtr& expr);

static bool IsStaticDispatchIndexExpr(
    const ast::ExprPtr& expr,
    const std::unordered_set<IdKey>& pattern_names) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return pattern_names.find(IdKeyOf(node.name)) != pattern_names.end();
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return node.path.empty() &&
                 pattern_names.find(IdKeyOf(node.name)) != pattern_names.end();
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return IsStaticDispatchIndexExpr(node.base, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return IsStaticDispatchIndexExpr(node.base, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return IsStaticDispatchIndexExpr(node.base, pattern_names) &&
                 IsStaticDispatchIndexExpr(node.index, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return IsStaticDispatchIndexExpr(node.value, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return IsStaticDispatchIndexExpr(node.lhs, pattern_names) &&
                 IsStaticDispatchIndexExpr(node.rhs, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return IsStaticDispatchIndexExpr(node.value, pattern_names);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return IsStaticDispatchIndexExpr(node.expr, pattern_names);
        } else {
          return false;
        }
      },
      expr->node);
}

static bool IsDispatchConstantExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return IsStaticDispatchIndexExpr(expr, {});
}

static bool IsAssociativeReduce(const ast::DispatchOption& opt) {
  if (opt.kind != ast::DispatchOptionKind::Reduce) {
    return true;
  }
  if (opt.reduce_op == ast::ReduceOp::Custom ||
      !opt.custom_reduce_name.empty()) {
    // User-defined reducers are not statically known to be associative.
    return false;
  }
  switch (opt.reduce_op) {
    case ast::ReduceOp::Add:
    case ast::ReduceOp::Mul:
    case ast::ReduceOp::Min:
    case ast::ReduceOp::Max:
    case ast::ReduceOp::And:
    case ast::ReduceOp::Or:
      return true;
    case ast::ReduceOp::Custom:
      return false;
  }
  return false;
}

}  // namespace

ExprTypeResult TypeDispatchExprImpl(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::DispatchExpr& expr,
                                    const TypeEnv& env,
                                    const TypeExprFn& type_expr,
                                    const TypeIdentFn& type_ident,
                                    const PlaceTypeFn& type_place) {
  SpecDefsDispatch();
  SPEC_RULE("T-Dispatch");
  ExprTypeResult result;

  // Check that we're inside a parallel block (section 18.5.1)
  if (!type_ctx.in_parallel) {
    result.diag_id = "E-CON-0140";  // dispatch without enclosing parallel block
    return result;
  }

  // Type check range expression
  ExprTypeResult range_result = type_expr(expr.range);
  if (!range_result.ok) {
    result.diag_id = range_result.diag_id;
    return result;
  }

  // Check range is a range family type.
  const auto stripped_range = StripPerm(range_result.type);
  if (!::ultraviolet::analysis::IsRangeType(stripped_range)) {
    result.diag_id = "E-SEM-3133";
    return result;
  }
  if (stripped_range &&
      std::holds_alternative<TypeRangeFull>(stripped_range->node)) {
    // Unbounded dispatch ranges are not finite iteration domains.
    result.diag_id = "E-SEM-3133";
    return result;
  }

  // Infer the iteration variable type from the range element type.
  TypeRef index_type = InferDispatchIndexType(stripped_range).value_or(
      MakeTypePrim("usize"));

  // Validate key clause root binding (section 18.5.1)
  if (expr.key_clause.has_value()) {
    const auto binding = BindOf(env, expr.key_clause->key_path.root);
    if (!binding.has_value()) {
      result.diag_id = "ResolveExpr-Ident-Err";
      return result;
    }
  }

  // Add pattern binding to environment
  TypeEnv body_env = PushScope(env);
  if (expr.pattern) {
    PatternTypeResult pat_result = TypePattern(ctx, expr.pattern, index_type);
    if (!pat_result.ok) {
      result.diag_id = pat_result.diag_id;
      return result;
    }
    // Add bindings to environment
    for (const auto& [name, type] : pat_result.bindings) {
      body_env.scopes.back()[name] = TypeBinding{ast::Mutability::Let, type};
    }
  }

  // Type check body
  if (!expr.body) {
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }

  ExprTypeResult body_result = TypeBlock(ctx, type_ctx, *expr.body, body_env,
                                         type_expr, type_ident, type_place);
  if (!body_result.ok) {
    result.diag_id = body_result.diag_id;
    result.diag_detail = body_result.diag_detail;
    result.diag_span = body_result.diag_span;
    return result;
  }

  // Capture analysis (section 18.3):
  // - unique captures require explicit move (E-CON-0120)
  // - explicit move from an outer parallel scope is permitted for the first
  //   selected child task only; later selections reject with E-CON-0122
  const auto capture_info =
      DispatchCaptureCollector(env).Collect(*expr.body, expr.pattern);
  for (const auto& captured_name : capture_info.captures) {
    const auto binding = BindOf(env, captured_name);
    if (!binding.has_value()) {
      continue;
    }

    const bool is_explicit_move =
        capture_info.explicit_moves.find(captured_name) !=
        capture_info.explicit_moves.end();

    if (is_explicit_move &&
        IsOuterParallelBinding(type_ctx, captured_name) &&
        !ClaimFirstChildMove(type_ctx, captured_name)) {
      result.diag_id = "E-CON-0122";
      return result;
    }

    if (PermOfType(binding->type) == Permission::Unique && !is_explicit_move) {
      result.diag_id = "E-CON-0120";
      return result;
    }

    if (GpuContext(env)) {
      const auto gpu_capture =
          CheckGpuCapture(ctx, env, captured_name, is_explicit_move);
      if (!gpu_capture.ok) {
        if (gpu_capture.supplemental_diag_id.has_value()) {
          EmitSupplementalTypeDiag(type_ctx, *gpu_capture.supplemental_diag_id);
        }
        result.diag_id = gpu_capture.diag_id;
        return result;
      }
    }
  }

  bool has_ordered = false;
  bool has_reduce = false;
  bool non_associative_reduce = false;
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Ordered) {
      has_ordered = true;
      continue;
    }
    if (opt.kind == ast::DispatchOptionKind::Chunk) {
      const auto chunk_typed = type_expr(opt.chunk_expr);
      if (!chunk_typed.ok) {
        result.diag_id = chunk_typed.diag_id;
        return result;
      }
      if (!IsUsizeType(chunk_typed.type)) {
        result.diag_id = "E-SEM-3133";
        return result;
      }
      continue;
    }
    if (opt.kind == ast::DispatchOptionKind::Workgroup) {
      const auto workgroup_typed = type_expr(opt.workgroup_expr);
      if (!workgroup_typed.ok) {
        result.diag_id = workgroup_typed.diag_id;
        return result;
      }
      const auto dims = ExtractDim3Const(ctx, opt.workgroup_expr, type_expr);
      if (!dims.has_value()) {
        SPEC_RULE("Dim3Const-Err");
        result.diag_id = "E-CON-0159";
        return result;
      }
      if (GpuContext(env)) {
        if (ExceedsMaxWorkgroupSize(*dims)) {
          SPEC_RULE("WorkgroupSize-Err");
          result.diag_id = "E-CON-0157";
          return result;
        }
      }
      continue;
    }
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      has_reduce = true;
      if (!IsAssociativeReduce(opt)) {
        non_associative_reduce = true;
      }
    }
  }

  if (non_associative_reduce && !has_ordered) {
    result.diag_id = "E-CON-0143";
    return result;
  }

  const auto pattern_names = DispatchPatternNameSet(expr.pattern);
  std::vector<DispatchAccess> partition_spec;
  if (expr.key_clause.has_value()) {
    partition_spec.push_back(DispatchAccess{
        SchemaOfKeyPathExpr(expr.key_clause->key_path, pattern_names),
        expr.key_clause->mode == ast::KeyMode::Read ? DispatchUseMode::Read
                                                    : DispatchUseMode::Write,
        expr.key_clause->span});
  } else if (expr.body) {
    auto inferred = InferDispatchAccesses(expr.pattern, *expr.body, body_env);
    if (!inferred.ok) {
      result.diag_id = inferred.diag_id;
      return result;
    }
    partition_spec = std::move(inferred.spec);
    for (std::size_t i = 0; i < partition_spec.size(); ++i) {
      for (std::size_t j = i + 1; j < partition_spec.size(); ++j) {
        if (!ProvablyDisjointPath(partition_spec[i].schema, partition_spec[j].schema,
                                  pattern_names) &&
            !KeyModeCompatible(partition_spec[i].mode, partition_spec[j].mode)) {
          SPEC_RULE("Dispatch-Dependency-Err");
          result.diag_id = "E-CON-0142";
          return result;
        }
      }
    }
  }

  if (!partition_spec.empty() && type_ctx.diags &&
      DynamicKeyPattern(partition_spec, pattern_names)) {
    SPEC_RULE("Dispatch-DynamicKey-Warn");
    const core::Span warn_span =
        expr.key_clause.has_value()
            ? expr.key_clause->span
            : (expr.body ? expr.body->span : expr.range->span);
    if (auto diag = core::MakeDiagnosticById("W-CON-0140", warn_span)) {
      core::Emit(*type_ctx.diags, *diag);
    }
  }

  if (has_reduce) {
    if (GpuContext(env)) {
      if (const auto gpu_diag = GpuSafeDiagForType(ctx, body_result.type);
          gpu_diag.has_value()) {
        EmitSupplementalTypeDiag(type_ctx, *gpu_diag);
        result.diag_id = *gpu_diag;
        return result;
      }
      SPEC_RULE("T-GPU-Dispatch-Reduce");
    }
    SPEC_RULE("T-Dispatch-Reduce");
    result.ok = true;
    result.type = body_result.type;
    return result;
  }

  // Basic dispatch returns unit
  if (GpuContext(env)) {
    SPEC_RULE("T-GPU-Dispatch");
  }
  result.ok = true;
  result.type = MakeTypePrim("()");
  return result;
}

}  // namespace ultraviolet::analysis::expr
