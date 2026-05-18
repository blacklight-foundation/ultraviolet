// =============================================================================
// classes.cpp - Class Type Checking Implementation
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Section 5.3.1 "Classes (Ultraviolet)" (lines 11762-12155)
//   - Common Method-Signature Definitions (lines 11764-11811)
//   - Class Declarations (lines 11816-11820)
//   - WF-ClassPath (lines 11824-11832)
//   - Superclass Linearization C3 (lines 11834-11876)
//   - Effective Method Set (lines 11883-11900)
//   - Effective Field Set (lines 11902-11919)
//   - Dispatchability (lines 11921-11943)
//   - Class Method Well-Formedness (lines 11945-11988)
//   - Class Implementation (lines 11989-12091)
//   - Dynamic Class Types (lines 12092-12102)
//   - Method Lookup (lines 12104-12154)
//
// =============================================================================

#include "04_analysis/composite/classes.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsClasses() {
  SPEC_DEF("ClassItems", "5.3.1");
  SPEC_DEF("ClassMethods", "5.3.1");
  SPEC_DEF("ClassFields", "5.3.1");
  SPEC_DEF("MethodNames", "5.3.1");
  SPEC_DEF("FieldNames", "5.3.1");
  SPEC_DEF("SelfVar", "5.3.1");
  SPEC_DEF("SubstSelf", "5.3.1");
  SPEC_DEF("RecvType", "5.3.1");
  SPEC_DEF("RecvMode", "5.3.1");
  SPEC_DEF("RecvPerm", "5.3.1");
  SPEC_DEF("ParamSig_T", "5.3.1");
  SPEC_DEF("ParamBinds_T", "5.3.1");
  SPEC_DEF("ReturnType_T", "5.3.1");
  SPEC_DEF("Sig_T", "5.3.1");
  SPEC_DEF("SigSelf", "5.3.1");
  SPEC_DEF("StripPerm", "5.2.12");
  SPEC_DEF("EffMethods", "5.3.1");
  SPEC_DEF("FirstByName", "5.3.1");
  SPEC_DEF("EffFields", "5.3.1");
  SPEC_DEF("FirstFieldByName", "5.3.1");
  SPEC_DEF("FieldSig", "5.3.1");
  SPEC_DEF("SelfOccurs", "5.3.1");
  SPEC_DEF("dispatchable", "5.3.1");
  SPEC_DEF("vtable_eligible", "5.3.1");
  SPEC_DEF("ClassMethodTable", "5.3.1");
  SPEC_DEF("ClassFieldTable", "5.3.1");
  SPEC_DEF("LookupClassMethod", "5.3.1");
  SPEC_DEF("SuperclassClosure", "5.3.1");
}

struct ClassPerfStats {
  std::uint64_t class_method_table_calls = 0;
  std::uint64_t class_method_table_us = 0;
  std::uint64_t lookup_class_method_calls = 0;
};

static ClassPerfStats& ClassesPerfStats() {
  static ClassPerfStats stats;
  return stats;
}

static bool ClassesPerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

static bool ClassesPerfActive() {
  static const bool enabled = ClassesPerfEnabled();
  return enabled;
}

class ScopedClassesTimer {
 public:
  explicit ScopedClassesTimer(std::uint64_t* slot)
      : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~ScopedClassesTimer() {
    if (!slot_) {
      return;
    }
    *slot_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
  }

 private:
  std::uint64_t* slot_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
};

static const ast::ClassDecl* LookupClassDecl(const ScopeContext& ctx,
                                             const ast::ClassPath& path) {
  const auto it = ctx.sigma.classes.find(PathKeyOf(path));
  if (it != ctx.sigma.classes.end()) {
    return &it->second;
  }

  if (path.size() == 1) {
    const auto ent = ResolveClassName(ctx, path[0]);
    if (ent.has_value() && ent->origin_opt.has_value()) {
      ast::Path resolved = *ent->origin_opt;
      const std::string resolved_name =
          ent->target_opt.has_value() ? *ent->target_opt : path[0];
      resolved.push_back(resolved_name);
      const auto resolved_it = ctx.sigma.classes.find(PathKeyOf(resolved));
      if (resolved_it != ctx.sigma.classes.end()) {
        return &resolved_it->second;
      }
    }
  }
  return nullptr;
}

static std::vector<const ast::ClassMethodDecl*> ClassMethods(
    const ast::ClassDecl& decl) {
  std::vector<const ast::ClassMethodDecl*> out;
  for (const auto& item : decl.items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      out.push_back(method);
    }
  }
  return out;
}

static std::vector<const ast::ClassFieldDecl*> ClassFields(
    const ast::ClassDecl& decl) {
  std::vector<const ast::ClassFieldDecl*> out;
  for (const auto& item : decl.items) {
    if (const auto* field = std::get_if<ast::ClassFieldDecl>(&item)) {
      out.push_back(field);
    }
  }
  return out;
}

struct TypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

static Permission LowerPermission(ast::TypePerm perm) {
  switch (perm) {
    case ast::TypePerm::Const:
      return Permission::Const;
    case ast::TypePerm::Unique:
      return Permission::Unique;
    case ast::TypePerm::Shared:
      return Permission::Shared;
  }
  return Permission::Const;
}

static std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  return ParamMode::Move;
}

static RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual) {
  switch (qual) {
    case ast::RawPtrQual::Imm:
      return RawPtrQual::Imm;
    case ast::RawPtrQual::Mut:
      return RawPtrQual::Mut;
  }
  return RawPtrQual::Imm;
}

static std::optional<StringState> LowerStringState(
    const std::optional<ast::StringState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::StringState::Managed:
      return StringState::Managed;
    case ast::StringState::View:
      return StringState::View;
  }
  return std::nullopt;
}

static std::optional<BytesState> LowerBytesState(
    const std::optional<ast::BytesState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::BytesState::Managed:
      return BytesState::Managed;
    case ast::BytesState::View:
      return BytesState::View;
  }
  return std::nullopt;
}

static std::optional<PtrState> LowerPtrState(
    const std::optional<ast::PtrState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::PtrState::Valid:
      return PtrState::Valid;
    case ast::PtrState::Null:
      return PtrState::Null;
    case ast::PtrState::Expired:
      return PtrState::Expired;
  }
  return std::nullopt;
}

static TypeLowerResult LowerType(const ScopeContext& ctx,
                                 const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return {false, std::nullopt, {}};
  }
  return std::visit(
      [&](const auto& node) -> TypeLowerResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          return {true, std::nullopt, MakeTypePrim(node.name)};
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypePerm(LowerPermission(node.perm), base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& elem : node.types) {
            const auto lowered = LowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            members.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeUnion(std::move(members))};
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            params.push_back(TypeFuncParam{LowerParamMode(param.mode),
                                           lowered.type});
          }
          const auto ret = LowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            const bool is_move =
                param.mode.has_value() && *param.mode == ast::ParamMode::Move;
            params.emplace_back(is_move, lowered.type);
          }
          const auto ret = LowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto dep_type = LowerType(ctx, dep.type);
              if (!dep_type.ok) {
                return dep_type;
              }
              SharedDep lowered_dep;
              lowered_dep.name = dep.name;
              lowered_dep.type = dep_type.type;
              deps.push_back(std::move(lowered_dep));
            }
            deps_opt = std::move(deps);
          }
          return {true, std::nullopt,
                  MakeTypeClosure(std::move(params), ret.type,
                                  std::move(deps_opt))};
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto lowered = LowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            elements.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeTuple(std::move(elements))};
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          const auto elem = LowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          const auto len = ConstLen(ctx, node.length);
          if (!len.ok || !len.value.has_value()) {
            return {false, len.diag_id, {}};
          }
          return {true, std::nullopt, MakeTypeArray(elem.type, *len.value)};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          const auto elem = LowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt, MakeTypeSlice(elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          const auto elem = LowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypePtr(elem.type, LowerPtrState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          const auto elem = LowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypeRawPtr(LowerRawPtrQual(node.qual), elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          return {true, std::nullopt,
                  MakeTypeString(LowerStringState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          return {true, std::nullopt,
                  MakeTypeBytes(LowerBytesState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return {true, std::nullopt, MakeTypeDynamic(node.path)};
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          return {true, std::nullopt,
                  MakeTypeOpaque(node.path, type.get(), type->span)};
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypeRefine(base.type, node.predicate)};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LowerType(ctx, arg);
            if (!lowered.ok) {
              return lowered;
            }
            args.push_back(lowered.type);
          }
          return {true, std::nullopt,
                  MakeTypeModalState(node.path, node.state, std::move(args))};
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          // Section 5.2.9, Section 13.1: Generic type instantiation lowering
          // Per WF-Apply (Section 5.2.3), type arguments MUST be preserved
          if (!node.generic_args.empty()) {
            std::vector<TypeRef> lowered_args;
            lowered_args.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              const auto lower_result = LowerType(ctx, arg);
              if (!lower_result.ok) {
                return lower_result;
              }
              lowered_args.push_back(lower_result.type);
            }
            return {true, std::nullopt,
                    MakeTypePath(node.path, std::move(lowered_args))};
          }
          return {true, std::nullopt, MakeTypePath(node.path)};
        } else {
          return {false, std::nullopt, {}};
        }
      },
      type->node);
}

static TypeLowerResult LowerReturnType(
    const ScopeContext& ctx,
    const std::shared_ptr<ast::Type>& type_opt) {
  if (!type_opt) {
    return {true, std::nullopt, MakeTypePrim("()")};
  }
  return LowerType(ctx, type_opt);
}

static TypeRef SubstSelfType(const TypeRef& self, const TypeRef& type) {
  if (!type) {
    return type;
  }
  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePathType>) {
          if (IsSelfVarPath(node.path)) {
            return self;
          }
          if (node.generic_args.empty()) {
            return type;
          }
          TypePathType out = node;
          out.generic_args.clear();
          out.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            out.generic_args.push_back(SubstSelfType(self, arg));
          }
          return MakeType(out);
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return MakeTypePerm(node.perm, SubstSelfType(self, node.base));
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          std::vector<TypeRef> elems;
          elems.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            elems.push_back(SubstSelfType(self, elem));
          }
          return MakeTypeTuple(std::move(elems));
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return MakeTypeArray(SubstSelfType(self, node.element),
                               node.length,
                               node.length_expr_text);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return MakeTypeSlice(SubstSelfType(self, node.element));
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.members.size());
          for (const auto& member : node.members) {
            members.push_back(SubstSelfType(self, member));
          }
          return MakeTypeUnion(std::move(members));
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            params.push_back(TypeFuncParam{param.mode,
                                           SubstSelfType(self, param.type)});
          }
          return MakeTypeFunc(std::move(params), SubstSelfType(self, node.ret));
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            params.emplace_back(param.first, SubstSelfType(self, param.second));
          }
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              SharedDep lowered;
              lowered.name = dep.name;
              lowered.type = SubstSelfType(self, dep.type);
              deps.push_back(std::move(lowered));
            }
            deps_opt = std::move(deps);
          }
          return MakeTypeClosure(std::move(params),
                                 SubstSelfType(self, node.ret),
                                 std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return MakeTypePtr(SubstSelfType(self, node.element), node.state);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return MakeTypeRawPtr(node.qual, SubstSelfType(self, node.element));
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return MakeTypeRefine(SubstSelfType(self, node.base), node.predicate);
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          TypeModalState out = node;
          out.generic_args.clear();
          out.generic_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            out.generic_args.push_back(SubstSelfType(self, arg));
          }
          SyncTypeModalStateFromFields(out);
          return MakeType(out);
        } else {
          return type;
        }
      },
      type->node);
}

static TypeRef StripPermLocal(const TypeRef& type) {
  SpecDefsClasses();
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return StripPermLocal(perm->base);
  }
  if (const auto* refine = std::get_if<TypeRefine>(&type->node)) {
    return StripPermLocal(refine->base);
  }
  return type;
}


static Permission LowerReceiverPerm(ast::ReceiverPerm perm) {
  switch (perm) {
    case ast::ReceiverPerm::Const:
      return Permission::Const;
    case ast::ReceiverPerm::Unique:
      return Permission::Unique;
    case ast::ReceiverPerm::Shared:
      return Permission::Shared;
  }
  return Permission::Const;
}

struct MethodSig {
  bool ok = false;
  TypeRef recv_type;
  std::vector<TypeFuncParam> params;
  TypeRef ret;
};

static MethodSig MethodSigSelf(const ScopeContext& ctx,
                               const ast::ClassMethodDecl& method) {
  MethodSig sig;
  const auto self = SelfVarType();

  if (const auto* shorthand =
          std::get_if<ast::ReceiverShorthand>(&method.receiver)) {
    sig.recv_type = MakeTypePerm(LowerReceiverPerm(shorthand->perm), self);
  } else if (const auto* explicit_recv =
                 std::get_if<ast::ReceiverExplicit>(&method.receiver)) {
    const auto lowered = LowerType(ctx, explicit_recv->type);
    if (!lowered.ok) {
      return sig;
    }
    sig.recv_type = SubstSelfType(self, lowered.type);
  }

  for (const auto& param : method.params) {
    const auto lowered = LowerType(ctx, param.type);
    if (!lowered.ok) {
      return sig;
    }
    sig.params.push_back(TypeFuncParam{LowerParamMode(param.mode),
                                       SubstSelfType(self, lowered.type)});
  }

  const auto ret = LowerReturnType(ctx, method.return_type_opt);
  if (!ret.ok) {
    return sig;
  }
  sig.ret = SubstSelfType(self, ret.type);
  sig.ok = true;
  return sig;
}

static bool SigEqual(const MethodSig& lhs, const MethodSig& rhs) {
  if (!lhs.ok || !rhs.ok) {
    return false;
  }
  const auto recv_eq = TypeEquiv(lhs.recv_type, rhs.recv_type);
  if (!recv_eq.ok || !recv_eq.equiv) {
    return false;
  }
  if (lhs.params.size() != rhs.params.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.params.size(); ++i) {
    if (lhs.params[i].mode != rhs.params[i].mode) {
      return false;
    }
    const auto param_eq = TypeEquiv(lhs.params[i].type, rhs.params[i].type);
    if (!param_eq.ok || !param_eq.equiv) {
      return false;
    }
  }
  const auto ret_eq = TypeEquiv(lhs.ret, rhs.ret);
  if (!ret_eq.ok || !ret_eq.equiv) {
    return false;
  }
  return true;
}

static bool SelfOccurs(const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          return node.path.size() == 1 && IdEq(node.path[0], "Self");
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return SelfOccurs(node.base);
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (SelfOccurs(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return SelfOccurs(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return SelfOccurs(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& elem : node.types) {
            if (SelfOccurs(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            if (SelfOccurs(param.type)) {
              return true;
            }
          }
          return SelfOccurs(node.ret);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            if (SelfOccurs(param.type)) {
              return true;
            }
          }
          if (SelfOccurs(node.ret)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (SelfOccurs(dep.type)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return SelfOccurs(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return SelfOccurs(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return SelfOccurs(node.base);
        } else {
          return false;
        }
      },
      type->node);
}

static bool SelfOccurs(const ast::ClassMethodDecl& method) {
  if (method.return_type_opt && SelfOccurs(method.return_type_opt)) {
    return true;
  }
  for (const auto& param : method.params) {
    if (SelfOccurs(param.type)) {
      return true;
    }
  }
  return false;
}

static bool VtableEligibleInternal(const ast::ClassMethodDecl& method) {
  if (method.generic_params && !method.generic_params->params.empty()) {
    return false;
  }
  return !SelfOccurs(method);
}

static std::optional<std::string_view> ClassDispatchabilityDiagnosticInOrder(
    const ScopeContext& ctx,
    const ast::ClassPath& path) {
  const auto lin = LinearizeClass(ctx, path);
  if (!lin.ok) {
    return lin.diag_id;
  }

  for (const auto& cls_path : lin.order) {
    const auto* decl = LookupClassDecl(ctx, cls_path);
    if (!decl) {
      return "Superclass-Undefined";
    }
    for (const auto* method : ClassMethods(*decl)) {
      if (!VTableEligible(*method)) {
        if (method->generic_params && !method->generic_params->params.empty()) {
          return "E-TYP-2542";
        }
        return "E-TYP-2541";
      }
    }
  }
  return std::nullopt;
}

}  // namespace

void LogClassLookupPerfSummary() {
  if (!ClassesPerfEnabled()) {
    return;
  }
  const auto& stats = ClassesPerfStats();
  if (stats.class_method_table_calls == 0 &&
      stats.lookup_class_method_calls == 0) {
    return;
  }
  std::fprintf(stderr,
               "[uv] sema perf=class-lookup "
               "class_method_table_calls=%llu class_method_table_us=%llu "
               "lookup_class_method_calls=%llu\n",
               static_cast<unsigned long long>(stats.class_method_table_calls),
               static_cast<unsigned long long>(stats.class_method_table_us),
               static_cast<unsigned long long>(stats.lookup_class_method_calls));
  std::fflush(stderr);
}

ClassMethodTableResult ClassMethodTable(const ScopeContext& ctx,
                                        const ast::ClassPath& path) {
  SpecDefsClasses();
  auto& perf = ClassesPerfStats();
  const bool perf_on = ClassesPerfActive();
  if (perf_on) {
    ++perf.class_method_table_calls;
  }
  ScopedClassesTimer timer(perf_on ? &perf.class_method_table_us : nullptr);
  ClassMethodTableResult result;
  const auto lin = LinearizeClass(ctx, path);
  if (!lin.ok) {
    result.diag_id = lin.diag_id;
    return result;
  }

  std::unordered_map<IdKey, MethodSig> seen;
  for (const auto& cls_path : lin.order) {
    const auto* decl = LookupClassDecl(ctx, cls_path);
    if (!decl) {
      result.diag_id = "Superclass-Undefined";
      return result;
    }
    const auto methods = ClassMethods(*decl);
    for (const auto* method : methods) {
      const auto key = IdKeyOf(method->name);
      const auto sig = MethodSigSelf(ctx, *method);
      if (!sig.ok) {
        continue;
      }
      const auto it = seen.find(key);
      if (it == seen.end()) {
        seen.emplace(key, sig);
        result.methods.push_back(ClassMethodTableResult::Entry{method, cls_path});
        continue;
      }
      if (SigEqual(it->second, sig)) {
        continue;
      }
      SPEC_RULE("EffMethods-Conflict");
      result.diag_id = "EffMethods-Conflict";
      return result;
    }
  }

  result.ok = true;
  return result;
}

ClassFieldTableResult ClassFieldTable(const ScopeContext& ctx,
                                      const ast::ClassPath& path) {
  SpecDefsClasses();
  ClassFieldTableResult result;
  const auto lin = LinearizeClass(ctx, path);
  if (!lin.ok) {
    result.diag_id = lin.diag_id;
    return result;
  }

  std::vector<const ast::ClassFieldDecl*> all;
  for (const auto& cls_path : lin.order) {
    const auto* decl = LookupClassDecl(ctx, cls_path);
    if (!decl) {
      result.diag_id = "Superclass-Undefined";
      return result;
    }
    const auto fields = ClassFields(*decl);
    all.insert(all.end(), fields.begin(), fields.end());
  }

  std::unordered_map<IdKey, TypeRef> seen;
  for (const auto* field : all) {
    const auto key = IdKeyOf(field->name);
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok) {
      continue;
    }
    const auto it = seen.find(key);
    if (it == seen.end()) {
      seen.emplace(key, lowered.type);
      result.fields.push_back(field);
      continue;
    }
    const auto eq = TypeEquiv(it->second, lowered.type);
    if (eq.ok && eq.equiv) {
      continue;
    }
    SPEC_RULE("EffFields-Conflict");
    result.diag_id = "EffFields-Conflict";
    return result;
  }

  result.ok = true;
  return result;
}

const ast::ClassMethodDecl* LookupClassMethod(const ScopeContext& ctx,
                                                 const ast::ClassPath& path,
                                                 std::string_view name) {
  SpecDefsClasses();
  auto& perf = ClassesPerfStats();
  if (ClassesPerfActive()) {
    ++perf.lookup_class_method_calls;
  }
  const auto table = ClassMethodTable(ctx, path);
  if (!table.ok) {
    return nullptr;
  }
  for (const auto& entry : table.methods) {
    if (entry.method && IdEq(entry.method->name, name)) {
      return entry.method;
    }
  }
  return nullptr;
}

bool ClassDispatchable(const ScopeContext& ctx, const ast::ClassPath& path) {
  return !ClassDispatchabilityDiagnostic(ctx, path).has_value();
}

std::optional<std::string_view> ClassDispatchabilityDiagnostic(
    const ScopeContext& ctx,
    const ast::ClassPath& path) {
  SpecDefsClasses();
  if (const auto diag_id = ClassDispatchabilityDiagnosticInOrder(ctx, path)) {
    return diag_id;
  }
  const auto table = ClassMethodTable(ctx, path);
  if (!table.ok) {
    return table.diag_id;
  }
  for (const auto& entry : table.methods) {
    if (!entry.method || !VtableEligibleInternal(*entry.method)) {
      if (entry.method && entry.method->generic_params &&
          !entry.method->generic_params->params.empty()) {
        return "E-TYP-2542";
      }
      return "E-TYP-2541";
    }
  }
  return std::nullopt;
}

bool ClassSubtypes(const ScopeContext& ctx,
                   const ast::ClassPath& sub,
                   const ast::ClassPath& sup) {
  SpecDefsClasses();
  if (PathEq(sub, sup)) {
    return true;
  }
  const auto lin = LinearizeClass(ctx, sub);
  if (!lin.ok) {
    return false;
  }
  for (const auto& entry : lin.order) {
    if (PathEq(entry, sup) && !PathEq(entry, sub)) {
      return true;
    }
  }
  return false;
}

bool TypeImplementsClass(const ScopeContext& ctx,
                         const TypeRef& type,
                         const ast::ClassPath& path) {
  SpecDefsClasses();
  if (!type) {
    return false;
  }
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  if (path.size() == 1) {
    const auto& name = path[0];
    if (IdEq(name, "Bitcopy")) {
      return BitcopyType(ctx, stripped);
    }
    if (IdEq(name, "Clone")) {
      return CloneType(ctx, stripped);
    }
    if (IdEq(name, "Drop")) {
      return DropType(ctx, stripped);
    }
    if (IdEq(name, "FfiSafe")) {
      return FfiSafeType(ctx, stripped);
    }
    if (IdEq(name, "Eq")) {
      return EqType(stripped);
    }
    if (IdEq(name, "Step")) {
      return BuiltinStepType(stripped);
    }
    if (IdEq(name, "Hash")) {
      // Built-in hashability follows equality support for primitive/string/ptr
      // families; user-defined aggregate hashing is checked via explicit impls.
      if (EqType(stripped)) {
        return true;
      }
    }
  }

  const auto* path_type = std::get_if<TypePathType>(&stripped->node);
  if (!path_type) {
    return false;
  }

  ast::Path syntax_path;
  syntax_path.reserve(path_type->path.size());
  for (const auto& comp : path_type->path) {
    syntax_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(PathKeyOf(syntax_path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }

  const std::vector<ast::ClassPath>* impls = nullptr;
  if (const auto* record = std::get_if<ast::RecordDecl>(&it->second)) {
    impls = &record->implements;
  } else if (const auto* enum_decl = std::get_if<ast::EnumDecl>(&it->second)) {
    impls = &enum_decl->implements;
  } else if (const auto* modal_decl =
                 std::get_if<ast::ModalDecl>(&it->second)) {
    impls = &modal_decl->implements;
  }
  if (!impls) {
    return false;
  }

  for (const auto& impl : *impls) {
    if (PathEq(impl, path)) {
      return true;
    }
    if (ClassSubtypes(ctx, impl, path)) {
      return true;
    }
  }

  return false;
}

// UVX Extension: Associated types

std::vector<const ast::AssociatedTypeDecl*> ClassAssociatedTypes(
    const ast::ClassDecl& decl) {
  std::vector<const ast::AssociatedTypeDecl*> out;
  for (const auto& item : decl.items) {
    if (const auto* assoc = std::get_if<ast::AssociatedTypeDecl>(&item)) {
      out.push_back(assoc);
    }
  }
  return out;
}

// UVX Extension: Abstract states for modal classes

std::vector<const ast::AbstractStateDecl*> ClassAbstractStates(
    const ast::ClassDecl& decl) {
  std::vector<const ast::AbstractStateDecl*> out;
  for (const auto& item : decl.items) {
    if (const auto* state = std::get_if<ast::AbstractStateDecl>(&item)) {
      out.push_back(state);
    }
  }
  return out;
}

// UVX Extension: Check if class is a modal class
bool IsModalClass(const ast::ClassDecl& decl) {
  SPEC_RULE("T-Modal-Class");
  return decl.modal || !ClassAbstractStates(decl).empty();
}

// UVX Extension: VTable eligibility check
// A method is vtable-eligible if:
// 1. Has a receiver (not static)
// 2. No generic params on the method itself
// 3. Does not use Self by value (except through pointers)
bool VTableEligible(const ast::ClassMethodDecl& method) {
  SpecDefsClasses();
  SPEC_RULE("vtable_eligible");

  // Must have receiver
  if (std::holds_alternative<ast::ReceiverShorthand>(method.receiver)) {
    // Shorthand always has receiver
  } else if (const auto* explicit_recv =
                 std::get_if<ast::ReceiverExplicit>(&method.receiver)) {
    // Explicit receiver exists
    (void)explicit_recv;
  } else {
    return false;
  }

  // Method-level generics cannot be represented in runtime vtables.
  if (method.generic_params && !method.generic_params->params.empty()) {
    return false;
  }

  // Check if Self appears by value (not through pointer)
  // For parameters
  for (const auto& param : method.params) {
    if (SelfOccurs(param.type)) {
      // Check if it's through a pointer
      if (const auto* type = param.type.get()) {
        if (std::holds_alternative<ast::TypePathType>(type->node)) {
          // Direct Self by value - not vtable eligible
          const auto* path = std::get_if<ast::TypePathType>(&type->node);
          if (path && IsSelfVarPath(path->path)) {
            return false;
          }
        }
      }
    }
  }

  return true;
}

// UVX Extension: Dispatchability check
// A class is dispatchable if all procedures are vtable-eligible.
bool Dispatchable(const ScopeContext& ctx, const ast::ClassDecl& decl) {
  SpecDefsClasses();
  SPEC_RULE("dispatchable");
  (void)ctx;

  for (const auto& item : decl.items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      if (!VTableEligible(*method)) {
        return false;
      }
    }
  }

  return true;
}

// UVX Extension: Implementation completeness check
// (CompletenessResult is declared in the header)

CompletenessResult CheckImplCompleteness(
    const ScopeContext& ctx,
    const ast::ClassPath& class_path,
    const ast::RecordDecl& impl) {
  CompletenessResult result;

  const auto* class_decl = LookupClassDecl(ctx, class_path);
  if (!class_decl) {
    result.ok = false;
    return result;
  }

  // Check methods
  for (const auto* class_method : ClassMethods(*class_decl)) {
    if (!class_method->body_opt) {
      // Abstract method - must be implemented
      bool found = false;
      for (const auto& member : impl.members) {
        if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
          if (IdEq(method->name, class_method->name)) {
            found = true;
            break;
          }
        }
      }
      if (!found) {
        result.missing_methods.push_back(class_method->name);
      }
    }
  }

  // Check associated types
  for (const auto* assoc_type : ClassAssociatedTypes(*class_decl)) {
    if (!assoc_type->default_type) {
      // Abstract associated type - must be provided
      // Implementation would provide this via a type alias member
      // (Simplified check - full impl needs type member lookup)
    }
  }

  result.ok = result.missing_methods.empty() &&
              result.missing_types.empty() &&
              result.missing_states.empty();

  if (!result.ok) {
    result.diag_id = "E-TYP-IMPL-INCOMPLETE";
  }

  return result;
}

// UVX Extension: Orphan rule check
// At least one of T or Cl must be defined in the current assembly
bool CheckOrphanRule(const ScopeContext& ctx,
                     const TypePath& type_path,
                     const ast::ClassPath& class_path,
                     const ast::ModulePath& current_module) {
  SPEC_RULE("T-Orphan-Rule");

  auto in_current_assembly = [&](const ast::Path& path) {
    return !path.empty() && !current_module.empty() &&
           IdEq(path.front(), current_module.front());
  };

  const bool type_declared =
      ctx.sigma.types.find(PathKeyOf(type_path)) != ctx.sigma.types.end();
  const bool class_declared =
      ctx.sigma.classes.find(PathKeyOf(class_path)) != ctx.sigma.classes.end();

  const bool type_local = type_declared && in_current_assembly(type_path);
  const bool class_local = class_declared && in_current_assembly(class_path);
  return type_local || class_local;
}

}  // namespace ultraviolet::analysis
