/*
 * =============================================================================
 * where_bounds.cpp - Where Clause and Bound Validation Implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - Docs/SPECIFICATION.md, Section 13.3 "Type Bounds" (lines 22460-22520)
 *   - Docs/SPECIFICATION.md, Section 13.3.1 "Where Clauses" (lines 22470-22500)
 *   - Docs/SPECIFICATION.md, Section 13.3.2 "Predicate Syntax" (lines 22510-22520)
 *   - Docs/SPECIFICATION.md, Section 11 "Classes" (lines 22700-22900)
 *
 * This file implements the where clause parsing and bound validation system:
 *   - ParseWhereClause: Parse where clause into list of bounds
 *   - ValidateBound: Validate type argument satisfies a bound
 *   - CheckPredicateBound: Check predicate bounds (Bitcopy, Clone, Drop, FfiSafe, GpuSafe)
 *   - CheckClassBound: Check class implementation bounds
 *   - SubstituteWhere: Substitute types in where clause
 *   - InferBoundsFromUsage: Infer required bounds from generic body
 *
 * CRITICAL SYNTAX:
 *   - Where clause syntax is Predicate(Type), NOT Type: Predicate
 *   - Example: where Bitcopy(T)  NOT where T: Bitcopy
 *   - Available predicates: Bitcopy, Clone, Drop, FfiSafe, GpuSafe
 *   - Class bounds use <: syntax: T <: Comparable
 *
 * =============================================================================
 */

#include "04_analysis/generics/where_bounds.h"

#include <algorithm>
#include <set>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsWhereBounds() {
  SPEC_DEF("WhereClause", "UVX.13.3");
  SPEC_DEF("PredicateReq", "UVX.13.3");
  SPEC_DEF("PredOk", "UVX.13.3");
  SPEC_DEF("T-Constraint-Sat", "UVX.13.3");
  SPEC_DEF("PredicateName", "UVX.13.3");
}

core::Diagnostic MakeInternalGenericDiagnostic(
    core::Severity severity,
    const std::optional<core::Span>& span,
    const std::string& message) {
  core::Diagnostic diag;
  diag.severity = severity;
  diag.span = span;
  diag.message = message;
  return diag;
}

// Set of valid predicate names
const std::set<std::string> kPredicateNames = {
    "Bitcopy",
    "Clone",
    "Drop",
    "FfiSafe",
    "GpuSafe"
};

// Lower an AST type to a TypeRef
// This is a simplified version - in full implementation would use type_lower.cpp
TypeRef LowerAstType(const ScopeContext& ctx,
                     const std::shared_ptr<ast::Type>& ast_type) {
  if (!ast_type) {
    return nullptr;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          return MakeTypePrim(node.name);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          TypePath path;
          path.reserve(node.path.size());
          for (const auto& ident : node.path) {
            path.push_back(ident);
          }

          if (!node.generic_args.empty()) {
            std::vector<TypeRef> args;
            args.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              args.push_back(LowerAstType(ctx, arg));
            }
            return MakeTypePath(path, std::move(args));
          }

          return MakeTypePath(path);
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            elements.push_back(LowerAstType(ctx, elem));
          }
          return MakeTypeTuple(std::move(elements));
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return MakeTypeArray(LowerAstType(ctx, node.element), 0);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return MakeTypeSlice(LowerAstType(ctx, node.element));
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          Permission perm;
          switch (node.perm) {
            case ast::TypePerm::Const:
              perm = Permission::Const;
              break;
            case ast::TypePerm::Unique:
              perm = Permission::Unique;
              break;
            case ast::TypePerm::Shared:
              perm = Permission::Shared;
              break;
          }
          return MakeTypePerm(perm, LowerAstType(ctx, node.base));
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& ty : node.types) {
            members.push_back(LowerAstType(ctx, ty));
          }
          return MakeTypeUnion(std::move(members));
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            std::optional<ParamMode> mode;
            if (param.mode) {
              mode = ParamMode::Move;
            }
            params.push_back(TypeFuncParam{mode, LowerAstType(ctx, param.type)});
          }
          return MakeTypeFunc(std::move(params), LowerAstType(ctx, node.ret));
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const bool is_move = param.mode.has_value();
            params.emplace_back(is_move, LowerAstType(ctx, param.type));
          }
          TypeRef ret = LowerAstType(ctx, node.ret);
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              SharedDep lowered;
              lowered.name = dep.name;
              lowered.type = LowerAstType(ctx, dep.type);
              deps.push_back(std::move(lowered));
            }
            deps_opt = std::move(deps);
          }
          return MakeTypeClosure(std::move(params), ret, std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          std::optional<PtrState> state;
          if (node.state) {
            switch (*node.state) {
              case ast::PtrState::Valid:
                state = PtrState::Valid;
                break;
              case ast::PtrState::Null:
                state = PtrState::Null;
                break;
              case ast::PtrState::Expired:
                state = PtrState::Expired;
                break;
            }
          }
          return MakeTypePtr(LowerAstType(ctx, node.element), state);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          RawPtrQual qual =
              node.qual == ast::RawPtrQual::Imm ? RawPtrQual::Imm : RawPtrQual::Mut;
          return MakeTypeRawPtr(qual, LowerAstType(ctx, node.element));
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          std::optional<StringState> state;
          if (node.state) {
            state = *node.state == ast::StringState::Managed ? StringState::Managed
                                                              : StringState::View;
          }
          return MakeTypeString(state);
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          std::optional<BytesState> state;
          if (node.state) {
            state = *node.state == ast::BytesState::Managed ? BytesState::Managed
                                                             : BytesState::View;
          }
          return MakeTypeBytes(state);
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return MakeTypeDynamic(node.path);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            args.push_back(LowerAstType(ctx, arg));
          }
          return MakeTypeModalState(node.path, node.state, std::move(args));
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          return MakeTypeOpaque(node.path, nullptr, core::Span{});
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return MakeTypeRefine(LowerAstType(ctx, node.base), node.predicate);
        } else {
          return nullptr;
        }
      },
      ast_type->node);
}

// Extract type name from a TypeRef for error messages
std::string GetTypeName(const TypeRef& type) {
  if (!type) {
    return "<unknown>";
  }
  return TypeToString(type);
}

// Forward declaration of CheckFfiSafe
bool CheckFfiSafe(const TypeRef& type);

core::Span PredicateClauseSpan(const ast::PredicateClause& predicates) {
  core::Span span;
  bool initialized = false;
  for (const auto& pred : predicates) {
    if (!pred.type) {
      continue;
    }
    if (!initialized) {
      span = pred.type->span;
      initialized = true;
      continue;
    }
    span.end_offset = pred.type->span.end_offset;
    span.end_line = pred.type->span.end_line;
    span.end_col = pred.type->span.end_col;
  }
  return span;
}

// Check if a type is FfiSafe (C ABI compatible)
// SPEC: Docs/SPECIFICATION.md FfiSafe rules
bool CheckFfiSafe(const TypeRef& type) {
  if (!type) {
    return false;
  }

  // FfiPrimTypes are FfiSafe
  static const std::set<std::string> ffi_prims = {
      "i8", "i16", "i32", "i64", "i128",
      "u8", "u16", "u32", "u64", "u128",
      "isize", "usize",
      "f16", "f32", "f64",
      "char", "()"
  };

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          return ffi_prims.find(node.name) != ffi_prims.end();
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return true;  // Raw pointers are always FfiSafe
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return CheckFfiSafe(node.element);
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          // Function types are FfiSafe if all params and return are FfiSafe
          for (const auto& param : node.params) {
            if (!CheckFfiSafe(param.type)) {
              return false;
            }
          }
          return CheckFfiSafe(node.ret);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          return false;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return CheckFfiSafe(node.base);
        } else {
          // Tuples, unions, slices, strings, bytes, modals are not FfiSafe
          return false;
        }
      },
      type->node);
}

}  // namespace

// =============================================================================
// Predicate Parsing
// =============================================================================

std::optional<PredicateKind> ParsePredicateName(const std::string& name) {
  SpecDefsWhereBounds();
  SPEC_RULE("Parse-PredicateName");

  // SPEC: IsPredName(name) iff name in {Bitcopy, Clone, Drop, FfiSafe, GpuSafe}
  if (name == "Bitcopy") {
    return PredicateKind::Bitcopy;
  } else if (name == "Clone") {
    return PredicateKind::Clone;
  } else if (name == "Drop") {
    return PredicateKind::Drop;
  } else if (name == "FfiSafe") {
    return PredicateKind::FfiSafe;
  } else if (name == "GpuSafe") {
    return PredicateKind::GpuSafe;
  }
  return std::nullopt;
}

std::string_view PredicateKindToString(PredicateKind kind) {
  switch (kind) {
    case PredicateKind::Bitcopy:
      return "Bitcopy";
    case PredicateKind::Clone:
      return "Clone";
    case PredicateKind::Drop:
      return "Drop";
    case PredicateKind::FfiSafe:
      return "FfiSafe";
    case PredicateKind::GpuSafe:
      return "GpuSafe";
  }
  return "Unknown";
}

bool IsPredName(const std::string& name) {
  return kPredicateNames.find(name) != kPredicateNames.end();
}

// =============================================================================
// Where Clause Parsing
// =============================================================================

WhereParseResult ParseWhereClause(
    const ScopeContext& ctx,
    const std::optional<ast::PredicateClause>& where_opt) {
  SpecDefsWhereBounds();
  SPEC_RULE("Parse-WhereClause");

  WhereParseResult result;

  // No where clause is valid
  if (!where_opt || where_opt->empty()) {
    return result;
  }

  // SPEC: Docs/SPECIFICATION.md (Parse-PredicateClauseOpt-Yes) rule
  // predicate_clause = [PredicateReq]

  result.clause.span = PredicateClauseSpan(*where_opt);

  for (const auto& pred : *where_opt) {
    // SPEC: PredicateReq = <pred, type>
    auto pred_kind = ParsePredicateName(pred.pred);
    if (!pred_kind) {
      result.ok = false;
      result.diag_id = "E-TYP-2302";
      return result;
    }

    PredicateBound bound;
    bound.predicate = *pred_kind;
    bound.type = LowerAstType(ctx, pred.type);
    bound.span = pred.type ? pred.type->span : result.clause.span;
    result.clause.bounds.push_back(bound);
  }

  return result;
}

// =============================================================================
// Bound Validation
// =============================================================================

BoundCheckResult ValidateBound(
    const ScopeContext& ctx,
    const Bound& bound,
    const TypeRef& type_arg) {
  SpecDefsWhereBounds();
  SPEC_RULE("Validate-Bound");

  BoundCheckResult result;

  return std::visit(
      [&](const auto& b) -> BoundCheckResult {
        using T = std::decay_t<decltype(b)>;

        if constexpr (std::is_same_v<T, PredicateBound>) {
          // SPEC: PredOk(pred, T)
          if (!CheckPredicateBound(ctx, b.predicate, type_arg)) {
            result.ok = false;
            result.diag_id = "E-TYP-2302";
            result.type_name = GetTypeName(type_arg);
            result.bound_name = std::string(PredicateKindToString(b.predicate));
            if (auto diag = core::MakeDiagnosticById("E-TYP-2302", b.span)) {
              diag->message = "Type '" + result.type_name +
                              "' does not satisfy predicate bound '" +
                              result.bound_name + "'";
              result.diagnostics.push_back(*diag);
            } else {
              result.diagnostics.push_back(MakeInternalGenericDiagnostic(
                  core::Severity::Error, b.span,
                  "Internal error: unresolved diagnostic code 'E-TYP-2302'"));
            }
          }
        } else if constexpr (std::is_same_v<T, ClassBound>) {
          // SPEC: T <: Class check
          if (!CheckClassBound(ctx, type_arg, b.class_path)) {
            result.ok = false;
            result.diag_id = "E-TYP-2302";
            result.type_name = GetTypeName(type_arg);

            std::string class_name;
            for (size_t i = 0; i < b.class_path.size(); ++i) {
              if (i > 0) class_name += "::";
              class_name += b.class_path[i];
            }
            result.bound_name = class_name;
            if (auto diag = core::MakeDiagnosticById("E-TYP-2302", b.span)) {
              diag->message = "Type '" + result.type_name +
                              "' does not implement class '" +
                              result.bound_name + "'";
              result.diagnostics.push_back(*diag);
            } else {
              result.diagnostics.push_back(MakeInternalGenericDiagnostic(
                  core::Severity::Error, b.span,
                  "Internal error: unresolved diagnostic code 'E-TYP-2302'"));
            }
          }
        }

        return result;
      },
      bound);
}

bool CheckPredicateBound(
    const ScopeContext& ctx,
    PredicateKind predicate,
    const TypeRef& type) {
  SpecDefsWhereBounds();
  SPEC_RULE("Check-PredicateBound");

  // SPEC: Docs/SPECIFICATION.md PredOk definitions
  // PredOk(`Bitcopy`, T) iff BitcopyType(T)
  // PredOk(`Clone`, T) iff CloneType(T)
  // PredOk(`Drop`, T) iff DropType(T)
  // PredOk(`FfiSafe`, T) iff FfiSafeType(T) ⇓ ok

  if (!type) {
    return false;
  }

  switch (predicate) {
    case PredicateKind::Bitcopy:
      // SPEC: BitcopyType(T)
      return BitcopyType(ctx, type);

    case PredicateKind::Clone:
      // SPEC: CloneType(T)
      // CloneType includes BitcopyType and types with clone() method
      return CloneType(ctx, type);

    case PredicateKind::Drop:
      // SPEC: DropType(T)
      return DropType(ctx, type);

    case PredicateKind::FfiSafe:
      // SPEC: FfiSafeType(T)
      return FfiSafeType(ctx, type);
    case PredicateKind::GpuSafe:
      // SPEC: GpuSafeType(T)
      return !GpuSafeDiagForType(ctx, type).has_value();
  }

  return false;
}

bool CheckClassBound(
    const ScopeContext& ctx,
    const TypeRef& type,
    const TypePath& class_path) {
  SpecDefsWhereBounds();
  SPEC_RULE("Check-ClassBound");

  // SPEC: Implements(Gamma, T, Class)
  // Check if type implements the specified class

  if (!type || class_path.empty()) {
    return false;
  }

  ast::ClassPath syntax_class_path;
  syntax_class_path.reserve(class_path.size());
  for (const auto& seg : class_path) {
    syntax_class_path.push_back(seg);
  }
  if (TypeImplementsClass(ctx, type, syntax_class_path)) {
    return true;
  }

  // Look up the type declaration
  if (const auto* path_type = std::get_if<TypePathType>(&type->node)) {
    PathKey type_key = path_type->path;

    // Look up in sigma for the type declaration
    auto it = ctx.sigma.types.find(type_key);
    if (it == ctx.sigma.types.end()) {
      // Type not found - may be a type parameter
      // Type parameters are assumed to satisfy their declared bounds
      return true;
    }

    // Check if the type implements the class
    return std::visit(
        [&](const auto& decl) -> bool {
          using D = std::decay_t<decltype(decl)>;

          if constexpr (std::is_same_v<D, ast::RecordDecl>) {
            // Check implements list
            for (const auto& impl : decl.implements) {
              if (impl == class_path) {
                return true;
              }
            }
          } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
            for (const auto& impl : decl.implements) {
              if (impl == class_path) {
                return true;
              }
            }
          } else if constexpr (std::is_same_v<D, ast::ModalDecl>) {
            for (const auto& impl : decl.implements) {
              if (impl == class_path) {
                return true;
              }
            }
          }
          // TypeAliasDecl doesn't have implements

          return false;
        },
        it->second);
  }

  // Primitive types, tuples, etc. don't implement classes
  // (unless we add built-in class implementations)
  return false;
}

BoundCheckResult ValidateAllBounds(
    const ScopeContext& ctx,
    const ParsedWhereClause& where_clause,
    const TypeSubst& subst) {
  SpecDefsWhereBounds();
  SPEC_RULE("Validate-AllBounds");

  // SPEC: T-Constraint-Sat rule
  // forall B in Bounds, Gamma |- A <: B

  BoundCheckResult result;

  // Substitute types in bounds and validate each
  for (const auto& bound : where_clause.bounds) {
    Bound subst_bound = SubstituteBound(bound, subst);

    // Get the substituted type
    TypeRef type_arg;
    std::visit(
        [&](const auto& b) {
          using T = std::decay_t<decltype(b)>;
          if constexpr (std::is_same_v<T, PredicateBound>) {
            type_arg = b.type;
          } else if constexpr (std::is_same_v<T, ClassBound>) {
            type_arg = b.type;
          }
        },
        subst_bound);

    auto check = ValidateBound(ctx, subst_bound, type_arg);
    if (!check.ok) {
      return check;
    }
  }

  return result;
}

// =============================================================================
// Substitution Functions
// =============================================================================

ParsedWhereClause SubstituteWhere(
    const ParsedWhereClause& where_clause,
    const TypeSubst& subst) {
  SpecDefsWhereBounds();
  SPEC_RULE("Substitute-Where");

  ParsedWhereClause result;
  result.span = where_clause.span;
  result.bounds.reserve(where_clause.bounds.size());

  for (const auto& bound : where_clause.bounds) {
    result.bounds.push_back(SubstituteBound(bound, subst));
  }

  return result;
}

Bound SubstituteBound(
    const Bound& bound,
    const TypeSubst& subst) {
  SpecDefsWhereBounds();
  SPEC_RULE("Substitute-Bound");

  return std::visit(
      [&](const auto& b) -> Bound {
        using T = std::decay_t<decltype(b)>;

        if constexpr (std::is_same_v<T, PredicateBound>) {
          PredicateBound result = b;
          result.type = InstantiateType(b.type, subst);
          return result;
        } else if constexpr (std::is_same_v<T, ClassBound>) {
          ClassBound result = b;
          result.type = InstantiateType(b.type, subst);
          return result;
        }
      },
      bound);
}

// =============================================================================
// Bound Inference
// =============================================================================

InferredBounds InferBoundsFromUsage(
    const ScopeContext& ctx,
    const ast::BlockPtr& body,
    const std::vector<ast::TypeParam>& params) {
  SpecDefsWhereBounds();
  SPEC_RULE("Infer-BoundsFromUsage");

  // SPEC: Docs/SPECIFICATION.md Section 13.3.3 "Bound Inference"
  // Examine how type parameters are used and infer required bounds

  InferredBounds result;

  if (!body || params.empty()) {
    return result;
  }

  // This would require a full AST traversal to examine:
  // 1. Method calls on values of type parameter type
  //    - If clone() is called, infer Clone bound
  //    - If comparison methods called, infer relevant class bounds
  // 2. Assignment patterns
  //    - If value is copied, infer Bitcopy bound
  // 3. FFI usage
  //    - If passed to extern, infer FfiSafe bound
  // 4. Drop behavior
  //    - If moved/dropped, may need Drop bound

  // For now, return empty - full implementation would traverse the AST
  // and collect evidence of bound requirements

  return result;
}

// =============================================================================
// Helper Functions
// =============================================================================

core::Span BoundSpan(const Bound& bound) {
  return std::visit(
      [](const auto& b) -> core::Span {
        return b.span;
      },
      bound);
}

std::string BoundToString(const Bound& bound) {
  return std::visit(
      [](const auto& b) -> std::string {
        using T = std::decay_t<decltype(b)>;

        if constexpr (std::is_same_v<T, PredicateBound>) {
          std::string result = std::string(PredicateKindToString(b.predicate));
          result += "(";
          result += TypeToString(b.type);
          result += ")";
          return result;
        } else if constexpr (std::is_same_v<T, ClassBound>) {
          std::string result = TypeToString(b.type);
          result += " <: ";
          for (size_t i = 0; i < b.class_path.size(); ++i) {
            if (i > 0) result += "::";
            result += b.class_path[i];
          }
          return result;
        }
      },
      bound);
}

bool BoundsEquivalent(const Bound& a, const Bound& b) {
  // Bounds are equivalent if they have the same kind and constrain the same type
  if (a.index() != b.index()) {
    return false;
  }

  return std::visit(
      [](const auto& ba, const auto& bb) -> bool {
        using TA = std::decay_t<decltype(ba)>;
        using TB = std::decay_t<decltype(bb)>;

        if constexpr (std::is_same_v<TA, PredicateBound> &&
                      std::is_same_v<TB, PredicateBound>) {
          return ba.predicate == bb.predicate &&
                 TypeEquiv(ba.type, bb.type).equiv;
        } else if constexpr (std::is_same_v<TA, ClassBound> &&
                             std::is_same_v<TB, ClassBound>) {
          return ba.class_path == bb.class_path &&
                 TypeEquiv(ba.type, bb.type).equiv;
        } else {
          return false;
        }
      },
      a, b);
}

std::vector<Bound> MergeBounds(const std::vector<std::vector<Bound>>& bound_lists) {
  std::vector<Bound> result;

  for (const auto& list : bound_lists) {
    for (const auto& bound : list) {
      // Check if already present
      bool found = false;
      for (const auto& existing : result) {
        if (BoundsEquivalent(existing, bound)) {
          found = true;
          break;
        }
      }
      if (!found) {
        result.push_back(bound);
      }
    }
  }

  return result;
}

}  // namespace ultraviolet::analysis
