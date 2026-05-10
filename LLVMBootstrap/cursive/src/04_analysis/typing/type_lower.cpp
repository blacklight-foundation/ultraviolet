// =================================================================
// File: 04_analysis/typing/type_lower.cpp
// Construct: Syntax Type to Analysis Type Lowering
// Spec Section: 5.2.3, 5.2.9
// =================================================================
//
// MIGRATED FROM: cursive-bootstrap/src/03_analysis/types/type_lower.cpp
//
// =================================================================

#include "04_analysis/typing/type_lower.h"

#include "00_core/assert_spec.h"
#include "02_source/ast/ast_dump.h"
#include "04_analysis/typing/type_equiv.h"

namespace cursive::analysis {

Permission LowerPermission(ast::TypePerm perm) {
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

std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  switch (*mode) {
    case ast::ParamMode::Move:
      SPEC_RULE("ParamMode-Move");
      return ParamMode::Move;
  }
  return std::nullopt;
}

RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual) {
  switch (qual) {
    case ast::RawPtrQual::Imm:
      return RawPtrQual::Imm;
    case ast::RawPtrQual::Mut:
      return RawPtrQual::Mut;
  }
  return RawPtrQual::Imm;
}

std::optional<StringState> LowerStringState(
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

std::optional<BytesState> LowerBytesState(
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

std::optional<PtrState> LowerPtrState(
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

LowerTypeResult LowerType(const ScopeContext& ctx,
                          const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return {false, std::nullopt, {}};
  }
  return std::visit(
      [&](const auto& node) -> LowerTypeResult {
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
            params.push_back(
                TypeFuncParam{LowerParamMode(param.mode), lowered.type});
          }
          const auto ret = LowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          return {true, std::nullopt,
                  MakeTypeFunc(std::move(params), ret.type)};
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
          std::optional<std::string> len_expr_text;
          if (node.length) {
            ast::DumpOptions dump_opts;
            dump_opts.include_spans = false;
            dump_opts.include_docs = false;
            len_expr_text = ast::to_string(*node.length, dump_opts);
          }
          return {true, std::nullopt,
                  MakeTypeArray(elem.type, *len.value, std::move(len_expr_text))};
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
          const auto& modal_path = ast::TypeModalRefPath(node.modal_ref);
          const auto& modal_args = ast::TypeModalRefArgs(node.modal_ref);
          std::vector<TypeRef> args;
          args.reserve(modal_args.size());
          for (const auto& arg : modal_args) {
            const auto lower_result = LowerType(ctx, arg);
            if (!lower_result.ok) {
              return lower_result;
            }
            args.push_back(lower_result.type);
          }
          return {true, std::nullopt,
                  MakeTypeModalState(modal_path, node.state, std::move(args))};
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          // §5.2.9, §13.1: Generic type instantiation lowering
          // Per WF-Apply (§5.2.3), type arguments MUST be preserved for ALL
          // generic types - both builtin (Ptr, Spawned, etc.) and user-defined
          // (records, enums, modals with type parameters).
          if (!node.generic_args.empty()) {
            SPEC_RULE("WF-Apply");
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
                    MakeTypeApply(node.path, std::move(lowered_args))};
          }
          return {true, std::nullopt, MakeTypePath(node.path)};
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          SPEC_RULE("WF-Apply");
          std::vector<TypeRef> lowered_args;
          lowered_args.reserve(node.args.size());
          for (const auto& arg : node.args) {
            const auto lower_result = LowerType(ctx, arg);
            if (!lower_result.ok) {
              return lower_result;
            }
            lowered_args.push_back(lower_result.type);
          }
          return {true, std::nullopt,
                  MakeTypeApply(node.path, std::move(lowered_args))};
        } else if constexpr (std::is_same_v<T, ast::TypeRange>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt, MakeTypeRange(base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeInclusive>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt, MakeTypeRangeInclusive(base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFrom>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt, MakeTypeRangeFrom(base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeTo>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt, MakeTypeRangeTo(base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeToInclusive>) {
          const auto base = LowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt, MakeTypeRangeToInclusive(base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFull>) {
          return {true, std::nullopt, MakeTypeRangeFull()};
        } else {
          return {false, std::nullopt, {}};
        }
      },
      type->node);
}

}  // namespace cursive::analysis
