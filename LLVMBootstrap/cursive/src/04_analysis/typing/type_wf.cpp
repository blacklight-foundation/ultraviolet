// =============================================================================
// MIGRATION MAPPING: type_wf.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2: Static Semantics
//   - Type well-formedness checks
//   - E-CON-0201 (line 21635): Async type parameter not well-formed
//   - Generic type parameter validation
//   - Type bounds checking
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_wf.cpp
//   Type well-formedness portions
//   Key functions:
//   - TypeWellFormed(Env, Type) -> bool
//   - CheckTypeBounds(Env, TypeParam, Type) -> bool
//   - ValidateGenericArgs(Env, GenericDecl, Args) -> bool
//
// KEY CONTENT TO MIGRATE:
//   TYPE WELL-FORMEDNESS (Gamma |- T wf):
//   1. Primitive types: always well-formed
//   2. Named types (TypePath): declaration must exist
//   3. Generic types: arguments must satisfy bounds
//   4. Tuple types: all elements well-formed
//   5. Array types: element well-formed, length constant
//   6. Slice types: element well-formed
//   7. Union types: all members well-formed, normalized
//   8. Function types: all params and return well-formed
//   9. Permission types: inner type well-formed
//   10. Pointer types: pointee well-formed
//   11. Modal state types: modal declaration exists, state valid
//   12. Dynamic types: class declaration exists
//   13. Async types: Out, In, Result, E all well-formed
//
//   GENERIC ARGUMENT VALIDATION:
//   - Count matches parameter count
//   - Each argument satisfies corresponding bound
//   - Where clause predicates satisfied
//
//   TYPE BOUNDS:
//   - Class bounds: T <: ClassName
//   - Predicate bounds: Bitcopy(T), Clone(T), Drop(T), FfiSafe(T)
//   - Combined bounds: all must be satisfied
//
//   RECURSIVE TYPES:
//   - Must be guarded by indirection (Ptr, function type)
//   - Unguarded recursion is ill-formed
//
// DEPENDENCIES:
//   - Environment for declaration lookups
//   - LowerType() for processing type expressions
//   - CheckClassImpl() for class bound verification
//   - CheckPredicate() for predicate bounds
//   - ConstLen() for array length validation
//
// SPEC RULES:
//   - WF-Type-* rules (implicit in spec)
//   - E-CON-0201: Async type parameter not well-formed
//   - Generic bound checking at instantiation
//
// RESULT:
//   bool indicating well-formedness
//   Diagnostics for ill-formed types
//
// NOTES:
//   - Well-formedness checked at type construction
//   - Generic instantiation checked at use sites
//   - Async types have specific well-formedness requirements
//
// =============================================================================
// =================================================================
// File: 04_analysis/typing/type_wf.cpp
// Construct: Type Well-Formedness Checking
// Spec Section: 5.2.12
// Spec Rules: WF-Prim, WF-Perm, WF-Tuple, WF-Array, WF-Slice,
//             WF-Union, WF-Func, WF-Path, WF-Dynamic, WF-Opaque,
//             WF-Refine-Type, WF-String, WF-Bytes, WF-Ptr, WF-RawPtr,
//             WF-ModalState
// =================================================================
#include "04_analysis/typing/type_wf.h"

#include "00_core/assert_spec.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_stmt.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsTypeWF() {
  SPEC_DEF("WF-Prim", "5.2.12");
  SPEC_DEF("WF-Perm", "5.2.12");
  SPEC_DEF("WF-Tuple", "5.2.12");
  SPEC_DEF("WF-Array", "5.2.12");
  SPEC_DEF("WF-Slice", "5.2.12");
  SPEC_DEF("WF-Union", "5.2.7");
  SPEC_DEF("WF-Union-TooFew", "5.2.7");
  SPEC_DEF("WF-Func", "5.2.12");
  SPEC_DEF("WF-Path", "5.2.12");
  SPEC_DEF("WF-Dynamic", "5.2.12");
  SPEC_DEF("WF-Dynamic-Err", "5.2.12");
  SPEC_DEF("WF-Opaque", "5.2.12");
  SPEC_DEF("WF-Opaque-Err", "5.2.12");
  SPEC_DEF("WF-Refine-Type", "5.2.12");
  SPEC_DEF("WF-String", "5.2.12");
  SPEC_DEF("WF-Bytes", "5.2.12");
  SPEC_DEF("WF-Ptr", "5.2.12");
  SPEC_DEF("WF-RawPtr", "5.2.12");
  SPEC_DEF("WF-ModalState", "5.4");
  SPEC_DEF("WF-ModalState-ArgCount-Err", "5.4");
  SPEC_DEF("WF-Async-Arg-WF-Err", "5.2.12");
  SPEC_DEF("WF-Async-ArgCount-Err", "5.2.12");
  SPEC_DEF("WF-Async-Path-Err", "5.2.12");
  SPEC_DEF("K-Witness-Shared-WF", "19.1.4");
}

static bool IsSelfAssociatedTypePath(const TypePath& path) {
  return path.size() == 2 && IdEq(path[0], "Self");
}

static bool IsBuiltinCapClassPath(const TypePath& path) {
  ast::ClassPath class_path;
  class_path.reserve(path.size());
  for (const auto& seg : path) {
    class_path.push_back(seg);
  }
  return IsCapabilityClassPath(class_path);
}

static bool IsAsyncPathType(const TypePath& path) {
  return IsAsyncModalPath(path);
}

static bool IsKnownTypePath(const ScopeContext& ctx, const TypePath& path) {
  ast::TypePath ast_path;
  ast_path.reserve(path.size());
  for (const auto& comp : path) {
    ast_path.push_back(comp);
  }
  if (ctx.sigma.types.find(PathKeyOf(ast_path)) != ctx.sigma.types.end()) {
    return true;
  }
  if (path.size() != 1) {
    return false;
  }
  const auto ent = ResolveTypeName(ctx, path.front());
  if (!ent.has_value()) {
    return false;
  }
  if (!ent->origin_opt.has_value()) {
    return ent->target_opt.has_value() && IdEq(*ent->target_opt, path.front());
  }
  ast::TypePath resolved = *ent->origin_opt;
  resolved.push_back(ent->target_opt.value_or(path.front()));
  return ctx.sigma.types.find(PathKeyOf(resolved)) != ctx.sigma.types.end();
}

static bool ReceiverIsConst(const ScopeContext& ctx,
                            const ast::ClassMethodDecl& method) {
  if (const auto* shorthand =
          std::get_if<ast::ReceiverShorthand>(&method.receiver)) {
    return shorthand->perm == ast::ReceiverPerm::Const;
  }

  if (const auto* explicit_recv =
          std::get_if<ast::ReceiverExplicit>(&method.receiver)) {
    const auto lowered = LowerType(ctx, explicit_recv->type);
    if (!lowered.ok) {
      return true;
    }
    return PermOfType(lowered.type) == Permission::Const;
  }

  return true;
}

static bool SharedDynamicReceiversAreConst(const ScopeContext& ctx,
                                           const TypeDynamic& type) {
  ast::ClassPath class_path;
  class_path.reserve(type.path.size());
  for (const auto& comp : type.path) {
    class_path.push_back(comp);
  }

  const auto table = ClassMethodTable(ctx, class_path);
  if (!table.ok) {
    return true;
  }

  for (const auto& entry : table.methods) {
    if (entry.method && VTableEligible(*entry.method) &&
        !ReceiverIsConst(ctx, *entry.method)) {
      return false;
    }
  }
  return true;
}

// Forward declaration for primitive type name checking - defined below at line 321
// Note: Must use fully qualified call since the definition is outside the anonymous namespace
}  // namespace (anonymous) - temporarily close to declare forward ref

namespace expr {
bool IsPrimTypeName(const std::string& name);
}  // namespace expr

namespace {  // reopen anonymous namespace

static TypeWfResult TypeWFImpl(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypeWF();
  if (!type) {
    return {};
  }
  return std::visit(
      [&](const auto& node) -> TypeWfResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          if (!expr::IsPrimTypeName(node.name)) {
            return {};
          }
          if (IdEq(node.name, "Async")) {
            SPEC_RULE("WF-Async-Path-Err");
            return {false, "WF-Async-Path-Err"};
          }
          SPEC_RULE("WF-Prim");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          const auto base = TypeWFImpl(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          if (node.perm == Permission::Shared) {
            if (const auto* dynamic =
                    std::get_if<TypeDynamic>(&node.base->node)) {
              if (!SharedDynamicReceiversAreConst(ctx, *dynamic)) {
                SPEC_RULE("K-Witness-Shared-WF");
                return {false, "E-CON-0083"};
              }
            }
          }
          SPEC_RULE("WF-Perm");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            const auto wf = TypeWFImpl(ctx, elem);
            if (!wf.ok) {
              return wf;
            }
          }
          SPEC_RULE("WF-Tuple");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          const auto wf = TypeWFImpl(ctx, node.element);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-Array");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          const auto wf = TypeWFImpl(ctx, node.element);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-Slice");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          if (node.members.size() < 2) {
            SPEC_RULE("WF-Union-TooFew");
            return {false, "WF-Union-TooFew"};
          }
          std::vector<TypeRef> distinct_members;
          for (const auto& member : node.members) {
            const auto wf = TypeWFImpl(ctx, member);
            if (!wf.ok) {
              return wf;
            }
            bool duplicate = false;
            for (const auto& seen : distinct_members) {
              const auto equiv = TypeEquiv(seen, member);
              if (equiv.ok && equiv.equiv) {
                duplicate = true;
                break;
              }
            }
            if (!duplicate) {
              distinct_members.push_back(member);
            }
          }
          if (distinct_members.size() < 2) {
            SPEC_RULE("WF-Union-TooFew");
            return {false, "WF-Union-TooFew"};
          }
          SPEC_RULE("WF-Union");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            const auto wf = TypeWFImpl(ctx, param.type);
            if (!wf.ok) {
              return wf;
            }
          }
          const auto ret = TypeWFImpl(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          SPEC_RULE("WF-Func");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& param : node.params) {
            const auto wf = TypeWFImpl(ctx, param.second);
            if (!wf.ok) {
              return wf;
            }
          }
          const auto ret = TypeWFImpl(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              const auto wf = TypeWFImpl(ctx, dep.type);
              if (!wf.ok) {
                return wf;
              }
            }
          }
          SPEC_RULE("WF-Func");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          if (IsAsyncPathType(node.path)) {
            if (node.generic_args.size() != 4) {
              SPEC_RULE("WF-Async-ArgCount-Err");
              return {false, "WF-Async-ArgCount-Err"};
            }
            for (const auto& arg : node.generic_args) {
              const auto wf = TypeWFImpl(ctx, arg);
              if (!wf.ok) {
                SPEC_RULE("WF-Async-Arg-WF-Err");
                return {false, "WF-Async-Arg-WF-Err"};
              }
            }
            SPEC_RULE("WF-Path");
            return {true, std::nullopt};
          }
          if (IsSelfVarPath(node.path)) {
            SPEC_RULE("WF-Path");
            return {true, std::nullopt};
          }
          if (IsSelfAssociatedTypePath(node.path)) {
            SPEC_RULE("WF-Path");
            return {true, std::nullopt};
          }
          if (const auto* params = TypeParamsOf(ctx, node.path)) {
            if (TotalParamCount(*params) > 0) {
              return {false, "E-TYP-2303"};
            }
          }
          if (!IsKnownTypePath(ctx, node.path)) {
            return {};
          }
          SPEC_RULE("WF-Path");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          if (IsAsyncPathType(node.path)) {
            if (node.args.size() != 4) {
              SPEC_RULE("WF-Async-ArgCount-Err");
              return {false, "WF-Async-ArgCount-Err"};
            }
          } else if (const auto* params = TypeParamsOf(ctx, node.path)) {
            const auto provided = node.args.size();
            const auto required = RequiredParamCount(*params);
            const auto total = TotalParamCount(*params);
            if (provided < required || provided > total) {
              return {false, "E-TYP-2303"};
            }
          }
          for (const auto& arg : node.args) {
            const auto wf = TypeWFImpl(ctx, arg);
            if (!wf.ok) {
              if (IsAsyncPathType(node.path)) {
                SPEC_RULE("WF-Async-Arg-WF-Err");
                return {false, "WF-Async-Arg-WF-Err"};
              }
              return wf;
            }
          }
          if (!IsKnownTypePath(ctx, node.path)) {
            return {};
          }
          SPEC_RULE("WF-Apply");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          if (IsBuiltinCapClassPath(node.path)) {
            SPEC_RULE("WF-Dynamic");
            return {true, std::nullopt};
          }
          ast::TypePath ast_path;
          ast_path.reserve(node.path.size());
          for (const auto& comp : node.path) {
            ast_path.push_back(comp);
          }
          if (ctx.sigma.classes.find(PathKeyOf(ast_path)) == ctx.sigma.classes.end()) {
            SPEC_RULE("WF-Dynamic-Err");
            return {false, "Superclass-Undefined"};
          }
          SPEC_RULE("WF-Dynamic");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          ast::Path class_path;
          class_path.reserve(node.class_path.size());
          for (const auto& comp : node.class_path) {
            class_path.push_back(comp);
          }
          if (ctx.sigma.classes.find(PathKeyOf(class_path)) == ctx.sigma.classes.end()) {
            SPEC_RULE("WF-Opaque-Err");
            return {false, "Superclass-Undefined"};
          }
          SPEC_RULE("WF-Opaque");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          const auto base = TypeWFImpl(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          if (!node.predicate) {
            return {false, std::nullopt};
          }
          TypeEnv env;
          TypeScope scope;
          scope.emplace(IdKeyOf("self"),
                        TypeBinding{ast::Mutability::Let, node.base});
          env.scopes.push_back(std::move(scope));
          StmtTypeContext type_ctx;
          type_ctx.return_type = MakeTypePrim("bool");
          const auto pred_type = TypeExpr(ctx, type_ctx, node.predicate, env);
          if (!pred_type.ok) {
            return {false, pred_type.diag_id};
          }
          if (!IsPrimType(pred_type.type, "bool")) {
            return {false, "E-TYP-1955"};
          }
          const auto purity = CheckPurity(node.predicate);
          if (!purity.ok) {
            return {false, std::optional<std::string_view>{"E-TYP-1954"}};
          }
          SPEC_RULE("WF-Refine-Type");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeString>) {
          SPEC_RULE("WF-String");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          SPEC_RULE("WF-Bytes");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          const auto wf = TypeWFImpl(ctx, node.element);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-Ptr");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto wf = TypeWFImpl(ctx, node.element);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-RawPtr");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          const auto* decl = LookupModalDecl(ctx, node.modal_ref);
          if (!decl || !HasState(*decl, node.state)) {
            return {};
          }
          const auto provided = node.generic_args.size();
          const auto required = RequiredParamCount(decl->generic_params);
          const auto total = TotalParamCount(decl->generic_params);
          if (provided < required || provided > total) {
            SPEC_RULE("WF-ModalState-ArgCount-Err");
            return {false, "WF-ModalState-ArgCount-Err"};
          }
          for (const auto& arg : node.generic_args) {
            const auto wf = TypeWFImpl(ctx, arg);
            if (!wf.ok) {
              return wf;
            }
          }
          SPEC_RULE("WF-ModalState");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          const auto wf = TypeWFImpl(ctx, node.base);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-Range");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          const auto wf = TypeWFImpl(ctx, node.base);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-RangeInclusive");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          const auto wf = TypeWFImpl(ctx, node.base);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-RangeFrom");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          const auto wf = TypeWFImpl(ctx, node.base);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-RangeTo");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          const auto wf = TypeWFImpl(ctx, node.base);
          if (!wf.ok) {
            return wf;
          }
          SPEC_RULE("WF-RangeToInclusive");
          return {true, std::nullopt};
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          SPEC_RULE("WF-RangeFull");
          return {true, std::nullopt};
        } else {
          return {};
        }
      },
      type->node);
}

}  // namespace

TypeWfResult TypeWF(const ScopeContext& ctx, const TypeRef& type) {
  return TypeWFImpl(ctx, type);
}

namespace expr {

bool IsPrimTypeName(const std::string& name) {
  // Check against all primitive type names
  const auto& prims = PrimTypeNames();
  for (const auto& prim : prims) {
    if (name == prim) {
      return true;
    }
  }
  // Also check for ! (never type) and () (unit)
  if (name == "!" || name == "()") {
    return true;
  }
  return false;
}

}  // namespace expr

}  // namespace cursive::analysis
