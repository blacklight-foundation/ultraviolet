// =============================================================================
// File: 04_analysis/typing/expr/method_call.cpp
// Method Call Expression Typing
// Spec Section: 5.3.1 + 5.3.2
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.3.1: Class method resolution and typing
//   Section 5.3.2: Record method call typing
//   - receiver~>method(args) syntax
//   - Receiver permission must satisfy method receiver requirement
//
// =============================================================================

#include "04_analysis/typing/expr/method_call.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/span.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_filesystem.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_network.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/memory/string_bytes.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/deprecation_warnings.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/outcome.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

namespace {

static bool IsProjectFilesTypePath(const TypePath& path) {
  return path.size() == 1 && IdEq(path[0], "ProjectFiles");
}

static bool IsIntrospectTypePath(const TypePath& path) {
  return path.size() == 1 && IdEq(path[0], "Introspect");
}

static bool IsComptimeDiagnosticsTypePath(const TypePath& path) {
  return path.size() == 1 && IdEq(path[0], "ComptimeDiagnostics");
}

static bool IsTypeEmitterTypePath(const TypePath& path) {
  return path.size() == 1 && IdEq(path[0], "TypeEmitter");
}

static std::optional<FileSystemMethodSig> LookupProjectFilesMethodSig(std::string_view name) {
  FileSystemMethodSig sig{};
  sig.recv_perm = Permission::Const;

  ast::Param path_param{};
  path_param.name = "path";
  path_param.type = std::make_shared<ast::Type>();
  path_param.type->node = ast::TypeString{ast::StringState::View};

  if (IdEq(name, "read")) {
    sig.params = {path_param};
    sig.ret = MakeOutcomeType(
        MakeTypePerm(Permission::Unique, MakeTypeString(StringState::Managed)),
        MakeTypePath({"IoError"}));
    return sig;
  }
  if (IdEq(name, "read_bytes")) {
    sig.params = {path_param};
    sig.ret = MakeOutcomeType(
        MakeTypePerm(Permission::Unique, MakeTypeBytes(BytesState::Managed)),
        MakeTypePath({"IoError"}));
    return sig;
  }
  if (IdEq(name, "exists")) {
    sig.params = {path_param};
    sig.ret = MakeOutcomeType(MakeTypePrim("bool"), MakeTypePath({"IoError"}));
    return sig;
  }
  if (IdEq(name, "list_dir")) {
    sig.params = {path_param};
    sig.ret = MakeOutcomeType(
        MakeTypeSlice(MakeTypeString(StringState::Managed)),
        MakeTypePath({"IoError"}));
    return sig;
  }
  if (IdEq(name, "project_root")) {
    sig.params = {};
    sig.ret = MakeTypeString(StringState::Managed);
    return sig;
  }
  return std::nullopt;
}

static std::optional<FileSystemMethodSig> LookupComptimeDiagnosticsMethodSig(
    std::string_view name) {
  FileSystemMethodSig sig{};
  sig.recv_perm = Permission::Const;

  ast::Param msg_param{};
  msg_param.name = "message";
  msg_param.type = std::make_shared<ast::Type>();
  msg_param.type->node = ast::TypeString{ast::StringState::View};

  if (IdEq(name, "error")) {
    sig.params = {msg_param};
    sig.ret = MakeTypePrim("!");
    return sig;
  }
  if (IdEq(name, "warning") || IdEq(name, "note")) {
    sig.params = {msg_param};
    sig.ret = MakeTypePrim("()");
    return sig;
  }
  if (IdEq(name, "current_span")) {
    sig.params = {};
    sig.ret = MakeTypePath({"SourceSpan"});
    return sig;
  }
  if (IdEq(name, "current_module")) {
    sig.params = {};
    sig.ret = MakeTypeString(StringState::Managed);
    return sig;
  }
  return std::nullopt;
}

static std::optional<FileSystemMethodSig> LookupIntrospectMethodSig(
    std::string_view name) {
  FileSystemMethodSig sig{};
  sig.recv_perm = Permission::Const;

  ast::Param type_param{};
  type_param.name = "ty";
  type_param.type = std::make_shared<ast::Type>();
  type_param.type->node = ast::TypePathType{{"Type"}, {}};

  ast::Param form_param{};
  form_param.name = "form";
  form_param.type = std::make_shared<ast::Type>();
  form_param.type->node = ast::TypePathType{{"Type"}, {}};

  if (IdEq(name, "category")) {
    sig.params = {type_param};
    sig.ret = MakeTypePath({"TypeCategory"});
    return sig;
  }
  if (IdEq(name, "fields")) {
    sig.params = {type_param};
    sig.ret = MakeTypeSlice(MakeTypePath({"FieldInfo"}));
    return sig;
  }
  if (IdEq(name, "variants")) {
    sig.params = {type_param};
    sig.ret = MakeTypeSlice(MakeTypePath({"VariantInfo"}));
    return sig;
  }
  if (IdEq(name, "states")) {
    sig.params = {type_param};
    sig.ret = MakeTypeSlice(MakeTypePath({"StateInfo"}));
    return sig;
  }
  if (IdEq(name, "implements_form")) {
    sig.params = {type_param, form_param};
    sig.ret = MakeTypePrim("bool");
    return sig;
  }
  if (IdEq(name, "type_name") || IdEq(name, "module_path")) {
    sig.params = {type_param};
    sig.ret = MakeTypeString(StringState::Managed);
    return sig;
  }
  return std::nullopt;
}

static inline void SpecDefsMethodCall() {
  SPEC_DEF("T-MethodCall", "5.3.1");
  SPEC_DEF("T-Record-MethodCall", "5.3.2");
  SPEC_DEF("T-Modal-Transition", "5.6");
  SPEC_DEF("T-Modal-Method", "5.6");
  SPEC_DEF("MethodCall-RecvPerm-Err", "5.3.1");
  SPEC_DEF("LookupMethod-NotFound", "5.3.2");
  SPEC_DEF("Transition-Source-Err", "5.6");
  SPEC_DEF("Transition-NotVisible", "5.6");
  SPEC_DEF("Modal-Method-NotFound", "5.6");
  SPEC_DEF("Modal-Method-NotVisible", "5.6");
  SPEC_DEF("Region-Unchecked-Unsafe-Err", "5.4.1");
  SPEC_DEF("AllocRaw-Unsafe-Err", "5.9.3");
  SPEC_DEF("DeallocRaw-Unsafe-Err", "5.9.3");
  SPEC_DEF("UntilMethod", "19.3.6");
  SPEC_DEF("Drop-Call-Err", "10.2");
  SPEC_DEF("Drop-Call-Err-Dyn", "10.2");
  SPEC_DEF("ArgsOk", "5.2.4");
}

// Strip all permission and refinement qualifiers (recursive)
static TypeRef StripPermDeep(const TypeRef& type) {
  if (!type) {
    return type;
  }
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

// NOTE: PermOfType is now declared in type_expr.h

static bool ReceiverPermissionAdmits(Permission caller, Permission required) {
  return PermissionAdmits(caller, required);
}

static bool KeyModeSufficient(ast::KeyMode held, ast::KeyMode required) {
  return held == ast::KeyMode::Write || held == required;
}

static std::optional<ast::KeyMode> CoveringKeyMode(
    const StmtTypeContext& type_ctx,
    const KeyPath& path) {
  std::optional<ast::KeyMode> best;
  for (const auto& held : type_ctx.held_key_paths) {
    if (!KeyPathContains(held.path, path)) {
      continue;
    }
    if (!best.has_value() || held.mode == ast::KeyMode::Write) {
      best = held.mode;
    }
  }
  return best;
}

static bool CheckBuiltinMethodArgs(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const std::vector<TypeFuncParam>& params,
                                   const std::vector<ast::Arg>& args,
                                   const TypeEnv& env,
                                   ExprTypeResult& result) {
  if (args.size() != params.size()) {
    result.diag_id = "E-SEM-2532";
    return false;
  }
  const auto arg_ctx_for = [&](const TypeRef& expected) {
    if (!expected) {
      return type_ctx;
    }
    const auto perm = PermOfType(expected);
    if (perm == Permission::Unique) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Write);
    }
    if (perm == Permission::Shared || perm == Permission::Const) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
    }
    return type_ctx;
  };
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& expected = params[i];
    if (args[i].moved && !expected.mode.has_value()) {
      result.diag_id = "E-SEM-2535";
      return false;
    }
    if (!args[i].moved && expected.mode.has_value() &&
        *expected.mode == ParamMode::Move) {
      result.diag_id = "E-SEM-2534";
      return false;
    }
    const auto checked = CheckExprAgainst(
        ctx, arg_ctx_for(expected.type), args[i].value, expected.type, env);
    if (!checked.ok) {
      result.diag_id = checked.diag_id;
      return false;
    }
  }
  return true;
}

struct ModalMemberLookupResult {
  const ast::StateMethodDecl* method = nullptr;
  const ast::TransitionDecl* transition = nullptr;
  bool method_in_other_state = false;
  bool transition_in_other_state = false;
};

static ModalMemberLookupResult LookupModalMember(
    const ScopeContext& ctx,
    const TypeModalState& modal,
    std::string_view member_name) {
  ModalMemberLookupResult result;

  PathKey key;
  for (const auto& seg : modal.path) {
    key.push_back(seg);
  }
  const auto it = ctx.sigma.types.find(key);
  if (it == ctx.sigma.types.end()) {
    return result;
  }

  const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second);
  if (!modal_decl) {
    return result;
  }

  const std::string name_text(member_name);
  for (const auto& state : modal_decl->states) {
    const bool in_current_state = IdEq(state.name, modal.state);
    for (const auto& member : state.members) {
      if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
        if (!IdEq(method->name, name_text)) {
          continue;
        }
        if (in_current_state) {
          result.method = method;
          return result;
        }
        result.method_in_other_state = true;
        continue;
      }
      if (const auto* transition = std::get_if<ast::TransitionDecl>(&member)) {
        if (!IdEq(transition->name, name_text)) {
          continue;
        }
        if (in_current_state) {
          result.transition = transition;
          return result;
        }
        result.transition_in_other_state = true;
      }
    }
  }

  return result;
}

static bool IsTypeParamName(const std::vector<ast::TypeParam>& params,
                            std::string_view name) {
  for (const auto& param : params) {
    if (param.name == name) {
      return true;
    }
  }
  return false;
}

static bool TypePathEqLocal(const TypePath& lhs, const TypePath& rhs) {
  return lhs == rhs;
}

static bool BindTypeParams(const ScopeContext& ctx,
                           const std::vector<ast::TypeParam>& params,
                           const TypeRef& expected,
                           const TypeRef& actual,
                           std::map<std::string, TypeRef>& bindings) {
  if (!expected || !actual) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&expected->node)) {
    if (path->path.size() == 1 && IsTypeParamName(params, path->path[0])) {
      const std::string name = path->path[0];
      const auto it = bindings.find(name);
      if (it == bindings.end()) {
        bindings.emplace(name, actual);
        return true;
      }
      const auto equiv = TypeEquiv(it->second, actual);
      return equiv.ok && equiv.equiv;
    }
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          const auto* other = std::get_if<TypePerm>(&actual->node);
          return other && node.perm == other->perm &&
                 BindTypeParams(ctx, params, node.base, other->base, bindings);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          const auto* other = std::get_if<TypeTuple>(&actual->node);
          if (!other || node.elements.size() != other->elements.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (!BindTypeParams(ctx, params, node.elements[i], other->elements[i],
                                bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          const auto* other = std::get_if<TypeArray>(&actual->node);
          return other && node.length == other->length &&
                 BindTypeParams(ctx, params, node.element, other->element,
                                bindings);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          const auto* other = std::get_if<TypeSlice>(&actual->node);
          return other && BindTypeParams(ctx, params, node.element, other->element,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          const auto* other = std::get_if<TypePtr>(&actual->node);
          return other && node.state == other->state &&
                 BindTypeParams(ctx, params, node.element, other->element,
                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto* other = std::get_if<TypeRawPtr>(&actual->node);
          return other && node.qual == other->qual &&
                 BindTypeParams(ctx, params, node.element, other->element,
                                bindings);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          const auto* other = std::get_if<TypeUnion>(&actual->node);
          if (!other || node.members.size() != other->members.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.members.size(); ++i) {
            if (!BindTypeParams(ctx, params, node.members[i], other->members[i],
                                bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          const auto* other = std::get_if<TypeFunc>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].mode != other->params[i].mode) {
              return false;
            }
            if (!BindTypeParams(ctx, params, node.params[i].type,
                                other->params[i].type, bindings)) {
              return false;
            }
          }
          return BindTypeParams(ctx, params, node.ret, other->ret, bindings);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          const auto* other = std::get_if<TypePathType>(&actual->node);
          if (!other || !TypePathEqLocal(node.path, other->path) ||
              node.generic_args.size() != other->generic_args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
            if (!BindTypeParams(ctx, params, node.generic_args[i],
                                other->generic_args[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          const auto* other = std::get_if<TypeModalState>(&actual->node);
          if (!other || !TypePathEqLocal(node.path, other->path) ||
              node.state != other->state ||
              node.generic_args.size() != other->generic_args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
            if (!BindTypeParams(ctx, params, node.generic_args[i],
                                other->generic_args[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          const auto* other = std::get_if<TypeDynamic>(&actual->node);
          return other && TypePathEqLocal(node.path, other->path);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          const auto* other = std::get_if<TypeRefine>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          const auto* other = std::get_if<TypeClosure>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].first != other->params[i].first) {
              return false;
            }
            if (!BindTypeParams(ctx, params, node.params[i].second,
                                other->params[i].second, bindings)) {
              return false;
            }
          }
          return BindTypeParams(ctx, params, node.ret, other->ret, bindings);
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          const auto* other = std::get_if<TypeOpaque>(&actual->node);
          return other && TypePathEqLocal(node.class_path, other->class_path);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          const auto* other = std::get_if<TypeString>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          const auto* other = std::get_if<TypeBytes>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypePrim>) {
          const auto* other = std::get_if<TypePrim>(&actual->node);
          return other && node.name == other->name;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          const auto* other = std::get_if<TypeRange>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          const auto* other = std::get_if<TypeRangeInclusive>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          const auto* other = std::get_if<TypeRangeFrom>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          const auto* other = std::get_if<TypeRangeTo>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          const auto* other = std::get_if<TypeRangeToInclusive>(&actual->node);
          return other && BindTypeParams(ctx, params, node.base, other->base,
                                         bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return std::holds_alternative<TypeRangeFull>(actual->node);
        } else {
          const auto equiv = TypeEquiv(expected, actual);
          return equiv.ok && equiv.equiv;
        }
      },
      expected->node);
}

static std::optional<std::string_view> CollectArgTypes(
    const std::vector<ast::Param>& params,
    const std::vector<ast::Arg>& args,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place,
    const LowerTypeFn& lower_type,
    std::vector<TypeRef>& out_types) {
  if (params.size() != args.size()) {
    return "E-SEM-2532";
  }

  std::vector<TypeFuncParam> lowered_params;
  lowered_params.reserve(params.size());
  for (const auto& param : params) {
    const auto lowered = lower_type(param.type);
    if (!lowered.ok) {
      return lowered.diag_id;
    }
    lowered_params.push_back(
        TypeFuncParam{LowerParamMode(param.mode), lowered.type});
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (MissingRequiredMoveForConsuming(lowered_params[i].mode, args[i])) {
      return "E-SEM-2534";
    }
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!lowered_params[i].mode.has_value() && args[i].moved) {
      return "E-SEM-2535";
    }
  }

  out_types.clear();
  out_types.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
        if (!lowered_params[i].mode.has_value()) {
      const bool has_source_prov = HasSourceProvenance(arg.value);
      if (has_source_prov && !IsPlaceExprForCall(arg.value)) {
        return "E-TYP-1603";
      }
      if (has_source_prov && type_place) {
        const auto place_type = (*type_place)(arg.value);
        if (!place_type.ok) {
          return place_type.diag_id;
        }
        out_types.push_back(place_type.type);
      } else {
        const auto arg_type = type_expr(arg.value);
        if (!arg_type.ok) {
          return arg_type.diag_id;
        }
        out_types.push_back(arg_type.type);
      }
      continue;
    }
    const auto arg_expr = MovedArgExpr(arg);
    const auto arg_type = type_expr(arg_expr);
    if (!arg_type.ok) {
      return arg_type.diag_id;
    }
    out_types.push_back(arg_type.type);
  }
  return std::nullopt;
}

struct InferMethodSubstResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeSubst subst;
};

static InferMethodSubstResult InferMethodSubst(
    const ScopeContext& ctx,
    const std::vector<ast::TypeParam>& params,
    const std::vector<ast::Param>& method_params,
    const std::vector<ast::Arg>& args,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place,
    const LowerTypeFn& lower_type) {
  InferMethodSubstResult result;

  std::vector<TypeRef> expected_param_types;
  expected_param_types.reserve(method_params.size());
  for (const auto& param : method_params) {
    const auto lowered = lower_type(param.type);
    if (!lowered.ok) {
      result.diag_id = lowered.diag_id;
      return result;
    }
    expected_param_types.push_back(lowered.type);
  }

  std::vector<TypeRef> actual_arg_types;
  if (const auto diag = CollectArgTypes(method_params, args, type_expr, type_place,
                                        lower_type, actual_arg_types)) {
    result.diag_id = *diag;
    return result;
  }

  std::map<std::string, TypeRef> bindings;
  for (std::size_t i = 0; i < expected_param_types.size() &&
                          i < actual_arg_types.size(); ++i) {
    if (!BindTypeParams(ctx, params, expected_param_types[i],
                        actual_arg_types[i], bindings)) {
      result.diag_id = "E-SEM-2533";
      return result;
    }
  }

  std::vector<TypeRef> inferred_args;
  inferred_args.reserve(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto& param = params[i];
    const auto it = bindings.find(param.name);
    if (it != bindings.end()) {
      inferred_args.push_back(it->second);
      continue;
    }

    if (!param.default_type) {
      result.diag_id = "E-TYP-2301";
      return result;
    }

    const auto lowered_default = lower_type(param.default_type);
    if (!lowered_default.ok) {
      result.diag_id = lowered_default.diag_id;
      return result;
    }

    TypeRef value = lowered_default.type;
    if (i > 0) {
      std::vector<ast::TypeParam> prefix_params(
          params.begin(),
          params.begin() + static_cast<long>(i));
      std::vector<TypeRef> prefix_args(
          inferred_args.begin(),
          inferred_args.begin() + static_cast<long>(i));
      const auto prefix_subst = BuildSubstitution(prefix_params, prefix_args);
      value = InstantiateType(value, prefix_subst);
    }
    inferred_args.push_back(value);
  }

  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto& param = params[i];
    const auto& arg = inferred_args[i];
    for (const auto& bound : param.bounds) {
      if (ctx.sigma.classes.find(PathKeyOf(bound.class_path)) ==
          ctx.sigma.classes.end()) {
        result.diag_id = "E-TYP-2305";
        return result;
      }
      if (!TypeImplementsClass(ctx, arg, bound.class_path)) {
        result.diag_id = "E-TYP-2302";
        return result;
      }
    }
  }

  result.subst = BuildSubstitution(params, inferred_args);
  result.ok = true;
  return result;
}

struct CallableSigShape {
  std::vector<TypeFuncParam> params;
  TypeRef ret;
};

static std::optional<CallableSigShape> CallableSigOf(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  const auto stripped = StripPermDeep(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* fn = std::get_if<TypeFunc>(&stripped->node)) {
    return CallableSigShape{fn->params, fn->ret};
  }
  if (const auto* closure = std::get_if<TypeClosure>(&stripped->node)) {
    CallableSigShape sig;
    sig.params.reserve(closure->params.size());
    for (const auto& [is_move, param_ty] : closure->params) {
      sig.params.push_back(
          TypeFuncParam{is_move ? std::optional<ParamMode>(ParamMode::Move)
                                : std::nullopt,
                        param_ty});
    }
    sig.ret = closure->ret;
    return sig;
  }
  return std::nullopt;
}

}  // namespace

// NOTE: TypeExpr and TypePlace are now available via type_expr.h include

// Main implementation matching header declaration
ExprTypeResult TypeMethodCallExprImpl(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const ast::MethodCallExpr& expr,
                                      const TypeEnv& env,
                                      const core::Span& span) {
  SpecDefsMethodCall();
  ExprTypeResult result;

  if (!expr.receiver) {
    return result;
  }

  // User-authored direct drop invocation is forbidden.
  if (IdEq(expr.name, "drop")) {
    result.diag_id = type_ctx.contract_dynamic ? "Drop-Call-Err-Dyn"
                                                : "Drop-Call-Err";
    return result;
  }

  // Create type_expr and type_place functions
  auto type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };
  const auto arg_ctx_for = [&](const TypeRef& expected) {
    if (!expected) {
      return type_ctx;
    }
    const auto perm = PermOfType(expected);
    if (perm == Permission::Unique) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Write);
    }
    if (perm == Permission::Shared || perm == Permission::Const) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
    }
    return type_ctx;
  };
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  ArgCheckFn check_expr = [&](const ast::ExprPtr& inner,
                              const TypeRef& expected) -> ArgCheckResult {
    const auto checked =
        CheckExprAgainst(ctx, arg_ctx_for(expected), inner, expected, env);
    return ArgCheckResult{checked.ok, checked.diag_id};
  };
  auto lower_type = [&](const std::shared_ptr<ast::Type>& type) -> LowerTypeResult {
    const auto lowered = LowerType(ctx, type);
    if (!lowered.ok) {
      return {false, lowered.diag_id, {}};
    }
    return {true, std::nullopt, lowered.type};
  };
  auto emit_deprecated_warning =
      [&](const ast::AttributeList& attrs_list) {
        EmitDeprecatedReferenceWarningFromAttrs(
            attrs_list, type_ctx,
            expr.receiver ? std::optional<core::Span>(expr.receiver->span)
                          : std::optional<core::Span>(span));
      };
  TypeRef lookup_base;
  Permission caller_perm = Permission::Const;
  const auto place = TypePlace(ctx, type_ctx, expr.receiver, env);
  if (place.ok) {
    lookup_base = StripPermDeep(place.type);
    caller_perm = PermOfType(place.type);
  } else {
    const auto base_expr = TypeExpr(ctx, type_ctx, expr.receiver, env);
    if (!base_expr.ok) {
      result.diag_id = base_expr.diag_id;
      return result;
    }
    lookup_base = StripPermDeep(base_expr.type);
    caller_perm = PermOfType(base_expr.type);
  }

  auto check_shared_receiver_access = [&](Permission receiver_requirement) -> bool {
    if (caller_perm != Permission::Shared) {
      return true;
    }
    const auto built = BuildKeyPath(expr.receiver);
    if (!built.success) {
      result.diag_id = "E-CON-0034";
      return false;
    }
    const auto covering_mode = CoveringKeyMode(type_ctx, built.path);
    if (!covering_mode.has_value()) {
      return true;
    }
    const ast::KeyMode required =
        receiver_requirement == Permission::Const ? ast::KeyMode::Read
                                                  : ast::KeyMode::Write;
    if (!KeyModeSufficient(*covering_mode, required)) {
      result.diag_id = "E-CON-0005";
      return false;
    }
    return true;
  };

  if (!lookup_base) {
    return result;
  }

  if (IdEq(expr.name, "until")) {
    SPEC_RULE("UntilMethod");

    if (!ReceiverPermissionAdmits(caller_perm, Permission::Shared)) {
      SPEC_RULE("MethodCall-RecvPerm-Err");
      result.diag_id = "E-TYP-1605";
      return result;
    }

    if (expr.args.size() != 2) {
      result.diag_id = "E-SEM-2532";
      return result;
    }
    if (expr.args[0].moved || expr.args[1].moved) {
      result.diag_id = "E-SEM-2535";
      return result;
    }

    const auto pred_typed = type_expr(expr.args[0].value);
    if (!pred_typed.ok) {
      result.diag_id = pred_typed.diag_id;
      return result;
    }
    const auto action_typed = type_expr(expr.args[1].value);
    if (!action_typed.ok) {
      result.diag_id = action_typed.diag_id;
      return result;
    }

    const auto pred_sig = CallableSigOf(pred_typed.type);
    const auto action_sig = CallableSigOf(action_typed.type);
    if (!pred_sig.has_value() || !action_sig.has_value()) {
      result.diag_id = "E-SEM-2533";
      return result;
    }
    if (pred_sig->params.size() != 1 || action_sig->params.size() != 1) {
      result.diag_id = "E-SEM-2533";
      return result;
    }
    if (pred_sig->params[0].mode.has_value() || action_sig->params[0].mode.has_value()) {
      result.diag_id = "E-SEM-2533";
      return result;
    }

    const auto expected_pred_param =
        MakeTypePerm(Permission::Const, lookup_base);
    const auto expected_action_param =
        MakeTypePerm(Permission::Unique, lookup_base);

    const auto pred_param_eq =
        TypeEquiv(pred_sig->params[0].type, expected_pred_param);
    if (!pred_param_eq.ok) {
      result.diag_id = pred_param_eq.diag_id;
      return result;
    }
    if (!pred_param_eq.equiv) {
      result.diag_id = "E-SEM-2533";
      return result;
    }

    const auto action_param_eq =
        TypeEquiv(action_sig->params[0].type, expected_action_param);
    if (!action_param_eq.ok) {
      result.diag_id = action_param_eq.diag_id;
      return result;
    }
    if (!action_param_eq.equiv) {
      result.diag_id = "E-SEM-2533";
      return result;
    }

    const auto pred_ret_eq = TypeEquiv(pred_sig->ret, MakeTypePrim("bool"));
    if (!pred_ret_eq.ok) {
      result.diag_id = pred_ret_eq.diag_id;
      return result;
    }
    if (!pred_ret_eq.equiv) {
      result.diag_id = "E-SEM-2533";
      return result;
    }

    result.ok = true;
    result.type = MakeTypePath({"Async"},
                               {MakeTypePrim("()"),
                                MakeTypePrim("()"),
                                action_sig->ret,
                                MakeTypePrim("!")});
    return result;
  }

  {
    const auto async_sig = AsyncSigOf(ctx, lookup_base);
    if (async_sig.has_value()) {
      // Route Async combinator typing through built-in modal member lookup.
      const TypePath async_modal_path = {"Async"};
      const auto async_combinator =
          IsBuiltinModalGeneralMember(async_modal_path, expr.name)
              ? LookupBuiltinAsyncCombinator(expr.name)
              : std::nullopt;
      if (async_combinator.has_value()) {
        if (!ReceiverPermissionAdmits(caller_perm, Permission::Const)) {
          SPEC_RULE("MethodCall-RecvPerm-Err");
          result.diag_id = "E-TYP-1605";
          return result;
        }

        const auto unit_type = MakeTypePrim("()");
        const auto bool_type = MakeTypePrim("bool");

        if (*async_combinator == BuiltinAsyncCombinatorKind::Map) {
        if (expr.args.size() != 1 || !expr.args[0].value) {
          result.diag_id = "E-SEM-2532";
          return result;
        }
        if (expr.args[0].moved) {
          result.diag_id = "E-SEM-2535";
          return result;
        }

        const auto fn_typed = type_expr(expr.args[0].value);
        if (!fn_typed.ok) {
          result.diag_id = fn_typed.diag_id;
          return result;
        }
        const auto fn_sig = CallableSigOf(fn_typed.type);
        if (!fn_sig.has_value() ||
            fn_sig->params.size() != 1 ||
            fn_sig->params[0].mode.has_value()) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto param_sub = Subtyping(ctx, async_sig->out, fn_sig->params[0].type);
        if (!param_sub.ok) {
          result.diag_id = param_sub.diag_id;
          return result;
        }
        if (!param_sub.subtype) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        result.ok = true;
        result.type = MakeTypePath({"Async"},
                                   {fn_sig->ret,
                                    async_sig->in,
                                    async_sig->result,
                                    async_sig->err});
        return result;
      }

        if (*async_combinator == BuiltinAsyncCombinatorKind::Filter) {
        if (expr.args.size() != 1 || !expr.args[0].value) {
          result.diag_id = "E-SEM-2532";
          return result;
        }
        if (expr.args[0].moved) {
          result.diag_id = "E-SEM-2535";
          return result;
        }

        const auto in_is_unit = TypeEquiv(async_sig->in, unit_type);
        if (!in_is_unit.ok) {
          result.diag_id = in_is_unit.diag_id;
          return result;
        }
        const auto result_is_unit = TypeEquiv(async_sig->result, unit_type);
        if (!result_is_unit.ok) {
          result.diag_id = result_is_unit.diag_id;
          return result;
        }
        if (!in_is_unit.equiv || !result_is_unit.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto pred_typed = type_expr(expr.args[0].value);
        if (!pred_typed.ok) {
          result.diag_id = pred_typed.diag_id;
          return result;
        }
        const auto pred_sig = CallableSigOf(pred_typed.type);
        if (!pred_sig.has_value() ||
            pred_sig->params.size() != 1 ||
            pred_sig->params[0].mode.has_value()) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto expected_param =
            MakeTypePerm(Permission::Const, async_sig->out);
        const auto pred_param_eq = TypeEquiv(pred_sig->params[0].type, expected_param);
        if (!pred_param_eq.ok) {
          result.diag_id = pred_param_eq.diag_id;
          return result;
        }
        if (!pred_param_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto pred_ret_eq = TypeEquiv(pred_sig->ret, bool_type);
        if (!pred_ret_eq.ok) {
          result.diag_id = pred_ret_eq.diag_id;
          return result;
        }
        if (!pred_ret_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        result.ok = true;
        result.type = lookup_base;
        return result;
      }

        if (*async_combinator == BuiltinAsyncCombinatorKind::Take) {
        if (expr.args.size() != 1 || !expr.args[0].value) {
          result.diag_id = "E-SEM-2532";
          return result;
        }
        if (expr.args[0].moved) {
          result.diag_id = "E-SEM-2535";
          return result;
        }

        const auto in_is_unit = TypeEquiv(async_sig->in, unit_type);
        if (!in_is_unit.ok) {
          result.diag_id = in_is_unit.diag_id;
          return result;
        }
        const auto result_is_unit = TypeEquiv(async_sig->result, unit_type);
        if (!result_is_unit.ok) {
          result.diag_id = result_is_unit.diag_id;
          return result;
        }
        if (!in_is_unit.equiv || !result_is_unit.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto n_typed = type_expr(expr.args[0].value);
        if (!n_typed.ok) {
          result.diag_id = n_typed.diag_id;
          return result;
        }
        const auto usize_type = MakeTypePrim("usize");
        const auto n_sub = Subtyping(ctx, n_typed.type, usize_type);
        if (!n_sub.ok) {
          result.diag_id = n_sub.diag_id;
          return result;
        }
        if (!n_sub.subtype) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        result.ok = true;
        result.type = lookup_base;
        return result;
      }

        if (*async_combinator == BuiltinAsyncCombinatorKind::Fold) {
        if (expr.args.size() != 2 || !expr.args[0].value || !expr.args[1].value) {
          result.diag_id = "E-SEM-2532";
          return result;
        }
        if (expr.args[0].moved || expr.args[1].moved) {
          result.diag_id = "E-SEM-2535";
          return result;
        }

        const auto in_is_unit = TypeEquiv(async_sig->in, unit_type);
        if (!in_is_unit.ok) {
          result.diag_id = in_is_unit.diag_id;
          return result;
        }
        const auto result_is_unit = TypeEquiv(async_sig->result, unit_type);
        if (!result_is_unit.ok) {
          result.diag_id = result_is_unit.diag_id;
          return result;
        }
        if (!in_is_unit.equiv || !result_is_unit.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto init_typed = type_expr(expr.args[0].value);
        if (!init_typed.ok) {
          result.diag_id = init_typed.diag_id;
          return result;
        }
        const auto fn_typed = type_expr(expr.args[1].value);
        if (!fn_typed.ok) {
          result.diag_id = fn_typed.diag_id;
          return result;
        }
        const auto fn_sig = CallableSigOf(fn_typed.type);
        if (!fn_sig.has_value() ||
            fn_sig->params.size() != 2 ||
            fn_sig->params[0].mode.has_value() ||
            fn_sig->params[1].mode.has_value()) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto p0_eq = TypeEquiv(fn_sig->params[0].type, init_typed.type);
        if (!p0_eq.ok) {
          result.diag_id = p0_eq.diag_id;
          return result;
        }
        if (!p0_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto p1_eq = TypeEquiv(fn_sig->params[1].type, async_sig->out);
        if (!p1_eq.ok) {
          result.diag_id = p1_eq.diag_id;
          return result;
        }
        if (!p1_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto ret_eq = TypeEquiv(fn_sig->ret, init_typed.type);
        if (!ret_eq.ok) {
          result.diag_id = ret_eq.diag_id;
          return result;
        }
        if (!ret_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        result.ok = true;
        result.type = MakeTypePath({"Async"},
                                   {unit_type,
                                    unit_type,
                                    init_typed.type,
                                    async_sig->err});
        return result;
      }

        if (*async_combinator == BuiltinAsyncCombinatorKind::Chain) {
        if (expr.args.size() != 1 || !expr.args[0].value) {
          result.diag_id = "E-SEM-2532";
          return result;
        }
        if (expr.args[0].moved) {
          result.diag_id = "E-SEM-2535";
          return result;
        }

        const auto out_is_unit = TypeEquiv(async_sig->out, unit_type);
        if (!out_is_unit.ok) {
          result.diag_id = out_is_unit.diag_id;
          return result;
        }
        const auto in_is_unit = TypeEquiv(async_sig->in, unit_type);
        if (!in_is_unit.ok) {
          result.diag_id = in_is_unit.diag_id;
          return result;
        }
        if (!out_is_unit.equiv || !in_is_unit.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto fn_typed = type_expr(expr.args[0].value);
        if (!fn_typed.ok) {
          result.diag_id = fn_typed.diag_id;
          return result;
        }
        const auto fn_sig = CallableSigOf(fn_typed.type);
        if (!fn_sig.has_value() ||
            fn_sig->params.size() != 1 ||
            fn_sig->params[0].mode.has_value()) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto input_sub = Subtyping(ctx, async_sig->result, fn_sig->params[0].type);
        if (!input_sub.ok) {
          result.diag_id = input_sub.diag_id;
          return result;
        }
        if (!input_sub.subtype) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto chained_sig = GetAsyncSig(fn_sig->ret);
        if (!chained_sig.has_value()) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto chained_out_is_unit = TypeEquiv(chained_sig->out, unit_type);
        if (!chained_out_is_unit.ok) {
          result.diag_id = chained_out_is_unit.diag_id;
          return result;
        }
        const auto chained_in_is_unit = TypeEquiv(chained_sig->in, unit_type);
        if (!chained_in_is_unit.ok) {
          result.diag_id = chained_in_is_unit.diag_id;
          return result;
        }
        const auto err_eq = TypeEquiv(chained_sig->err, async_sig->err);
        if (!err_eq.ok) {
          result.diag_id = err_eq.diag_id;
          return result;
        }
        if (!chained_out_is_unit.equiv || !chained_in_is_unit.equiv || !err_eq.equiv) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        result.ok = true;
        result.type = fn_sig->ret;
        return result;
        }
      }
  }
  }

  if (const auto builtin_sig =
          LookupStringBytesBuiltinMethodSig(lookup_base, expr.name)) {
    if (!ReceiverPermissionAdmits(caller_perm, builtin_sig->recv_perm)) {
      SPEC_RULE("MethodCall-RecvPerm-Err");
      result.diag_id = "E-TYP-1605";
      return result;
    }
    if (!check_shared_receiver_access(builtin_sig->recv_perm)) {
      return result;
    }
    const auto recv_sub = Subtyping(ctx, lookup_base, builtin_sig->recv_type);
    if (!recv_sub.ok) {
      result.diag_id = recv_sub.diag_id;
      return result;
    }
    if (!recv_sub.subtype) {
      SPEC_RULE("LookupMethod-NotFound");
      result.diag_id = "LookupMethod-NotFound";
      result.diag_detail = "method '" + std::string(expr.name) +
                           "' on type '" + TypeToString(lookup_base) + "'";
      return result;
    }
    if (!CheckBuiltinMethodArgs(ctx, type_ctx, builtin_sig->params, expr.args,
                                env, result)) {
      return result;
    }
    SPEC_RULE("T-MethodCall");
    result.ok = true;
    result.type = builtin_sig->ret;
    return result;
  }

  if (const auto builtin_sig =
          LookupFoundationalBuiltinMethodSig(lookup_base, expr.name)) {
    if (!ReceiverPermissionAdmits(caller_perm, builtin_sig->recv_perm)) {
      SPEC_RULE("MethodCall-RecvPerm-Err");
      result.diag_id = "E-TYP-1605";
      return result;
    }
    if (!check_shared_receiver_access(builtin_sig->recv_perm)) {
      return result;
    }
    const auto recv_sub = Subtyping(ctx, lookup_base, builtin_sig->recv_type);
    if (!recv_sub.ok) {
      result.diag_id = recv_sub.diag_id;
      return result;
    }
    if (!recv_sub.subtype) {
      SPEC_RULE("LookupMethod-NotFound");
      result.diag_id = "LookupMethod-NotFound";
      result.diag_detail = "method '" + std::string(expr.name) +
                           "' on type '" + TypeToString(lookup_base) + "'";
      return result;
    }
    if (!CheckBuiltinMethodArgs(ctx, type_ctx, builtin_sig->params, expr.args,
                                env, result)) {
      return result;
    }
    SPEC_RULE("T-MethodCall");
    result.ok = true;
    result.type = builtin_sig->ret;
    return result;
  }

  // Get the type path for method lookup
  TypePath type_path;
  if (const auto* applied_path = AppliedTypePath(*lookup_base)) {
    type_path = *applied_path;
  } else if (const auto* modal = std::get_if<TypeModalState>(&lookup_base->node)) {
    type_path = modal->path;
  } else if (const auto* dynamic = std::get_if<TypeDynamic>(&lookup_base->node)) {
    type_path = dynamic->path;
  } else if (const auto* opaque = std::get_if<TypeOpaque>(&lookup_base->node)) {
    // Opaque receivers are valid method-call bases (T-Opaque-Project).
    type_path = opaque->class_path;
  } else {
    // Not a named type - cannot have methods
    SPEC_RULE("LookupMethod-NotFound");
    result.diag_id = "LookupMethod-NotFound";
    result.diag_detail = "method '" + std::string(expr.name) +
                         "' on type '" + TypeToString(lookup_base) + "'";
    return result;
  }

  auto handle_cap_method = [&](Permission recv_perm,
                               const std::vector<ast::Param>& params,
                               const TypeRef& ret) -> bool {
    if (!ReceiverPermissionAdmits(caller_perm, recv_perm)) {
      SPEC_RULE("MethodCall-RecvPerm-Err");
      result.diag_id = "E-TYP-1605";
      return true;
    }
    if (!check_shared_receiver_access(recv_perm)) {
      return true;
    }
    const auto args_ok =
        ArgsOk(ctx, params, expr.args, type_expr, &type_place, lower_type,
               &check_expr);
    if (!args_ok.ok) {
      result.diag_id = args_ok.diag_id;
      return true;
    }
    SPEC_RULE("T-MethodCall");
    result.ok = true;
    result.type = ret;
    return true;
  };

  if (const auto* modal = std::get_if<TypeModalState>(&lookup_base->node)) {
    if (const auto builtin_sig =
            LookupBuiltinModalMemberSig(modal->path, modal->state, expr.name)) {
      if (!ReceiverPermissionAdmits(caller_perm, builtin_sig->recv_perm)) {
        SPEC_RULE("MethodCall-RecvPerm-Err");
        result.diag_id = "E-TYP-1605";
        return result;
      }
      if (!check_shared_receiver_access(builtin_sig->recv_perm)) {
        return result;
      }
      if (builtin_sig->requires_unsafe && !IsInUnsafeSpan(ctx, span)) {
        SPEC_RULE("Region-Unchecked-Unsafe-Err");
        result.diag_id = builtin_sig->unsafe_diag;
        return result;
      }
      if (expr.args.size() != builtin_sig->params.size()) {
        SPEC_RULE("Call-ArgCount-Err");
        result.diag_id = "E-SEM-2532";
        return result;
      }

      for (std::size_t i = 0; i < expr.args.size(); ++i) {
        if (MissingRequiredMoveForConsuming(
                builtin_sig->params[i].mode, expr.args[i])) {
          SPEC_RULE("Call-Move-Missing");
          result.diag_id = "E-SEM-2534";
          return result;
        }
      }
      for (std::size_t i = 0; i < expr.args.size(); ++i) {
        if (!builtin_sig->params[i].mode.has_value() && expr.args[i].moved) {
          SPEC_RULE("Call-Move-Unexpected");
          result.diag_id = "E-SEM-2535";
          return result;
        }
      }

      std::vector<TypeRef> arg_types;
      arg_types.reserve(expr.args.size());
      for (std::size_t i = 0; i < expr.args.size(); ++i) {
        const auto& arg = expr.args[i];
        if (!arg.value) {
          result.diag_id = "E-SEM-2533";
          return result;
        }

        const auto& param = builtin_sig->params[i];
        if (!param.mode.has_value()) {
          const bool has_source_prov = HasSourceProvenance(arg.value);
          if (has_source_prov && !IsPlaceExprForCall(arg.value)) {
            SPEC_RULE("Call-Arg-NotPlace");
            result.diag_id = "E-TYP-1603";
            return result;
          }
          if (param.type && !has_source_prov) {
            const auto checked = check_expr(arg.value, param.type);
            if (checked.ok) {
              arg_types.push_back(param.type);
              continue;
            }
            if (checked.diag_id.has_value()) {
              result.diag_id = checked.diag_id;
              return result;
            }
          }
          if (has_source_prov && type_place) {
            const auto place_type = type_place(arg.value);
            if (!place_type.ok) {
              result.diag_id = place_type.diag_id;
              return result;
            }
            arg_types.push_back(place_type.type);
          } else {
            const auto arg_type = type_expr(arg.value);
            if (!arg_type.ok) {
              result.diag_id = arg_type.diag_id;
              return result;
            }
            arg_types.push_back(arg_type.type);
          }
        } else {
          const bool uses_call_temp =
              UsesCallTempForConsuming(param.mode, arg);
          const auto moved = MovedArgExpr(arg);
          if (param.type && uses_call_temp) {
            const auto checked = check_expr(moved, param.type);
            if (checked.ok) {
              arg_types.push_back(param.type);
              continue;
            }
            if (checked.diag_id.has_value()) {
              result.diag_id = checked.diag_id;
              return result;
            }
          }
          const auto arg_type = type_expr(moved);
          if (!arg_type.ok) {
            result.diag_id = arg_type.diag_id;
            return result;
          }
          arg_types.push_back(arg_type.type);
        }

        if (param.type) {
          const auto sub =
              ArgumentTypeCompatible(ctx, arg_types.back(), param.type,
                                     param.mode);
          if (!sub.ok) {
            result.diag_id = sub.diag_id;
            return result;
          }
          if (!sub.subtype) {
            SPEC_RULE("Call-ArgType-Err");
            result.diag_id = "E-SEM-2533";
            return result;
          }
        }
      }

      TypeRef ret_type = builtin_sig->ret;
      if (builtin_sig->ret_from_first_arg) {
        if (arg_types.empty()) {
          SPEC_RULE("Call-ArgCount-Err");
          result.diag_id = "E-SEM-2532";
          return result;
        }
        ret_type = arg_types.front();
      }

      SPEC_RULE("T-MethodCall");
      result.ok = true;
      result.type = ret_type;
      return result;
    }

    std::optional<TypeSubst> modal_subst;
    {
      PathKey modal_key;
      for (const auto& seg : modal->path) {
        modal_key.push_back(seg);
      }
      const auto modal_it = ctx.sigma.types.find(modal_key);
      if (modal_it != ctx.sigma.types.end()) {
        if (const auto* modal_decl = std::get_if<ast::ModalDecl>(&modal_it->second);
            modal_decl && modal_decl->generic_params.has_value() &&
            modal_decl->generic_params->params.size() == modal->generic_args.size()) {
          modal_subst = BuildSubstitution(modal_decl->generic_params->params,
                                          modal->generic_args);
        }
      }
    }
    auto modal_lower_type =
        [&](const std::shared_ptr<ast::Type>& type) -> LowerTypeResult {
      const auto lowered = lower_type(type);
      if (!lowered.ok) {
        return lowered;
      }
      if (modal_subst.has_value()) {
        return {true, std::nullopt, InstantiateType(lowered.type, *modal_subst)};
      }
      return lowered;
    };

    const auto modal_member = LookupModalMember(ctx, *modal, expr.name);
    if (modal_member.transition) {
      if (!ReceiverPermissionAdmits(caller_perm, Permission::Unique)) {
        SPEC_RULE("Transition-Source-Err");
        result.diag_id = "E-TYP-2056";
        return result;
      }
      if (!check_shared_receiver_access(Permission::Unique)) {
        return result;
      }
      if (!StateMemberVisible(ctx, modal->path, modal_member.transition->vis)) {
        SPEC_RULE("Transition-NotVisible");
        result.diag_id = "E-TYP-2064";
        return result;
      }
      const auto args_ok = ArgsOk(ctx, modal_member.transition->params, expr.args,
                                  type_expr, &type_place, modal_lower_type,
                                  &check_expr);
      if (!args_ok.ok) {
        result.diag_id = args_ok.diag_id;
        return result;
      }
      SPEC_RULE("T-Modal-Transition");
      result.ok = true;
      result.type = MakeTypeModalState(
          modal->path, modal_member.transition->target_state, modal->generic_args);
      return result;
    }
    if (modal_member.method) {
      if (!StateMemberVisible(ctx, modal->path, modal_member.method->vis)) {
        SPEC_RULE("Modal-Method-NotVisible");
        result.diag_id = "E-TYP-2064";
        return result;
      }

      const auto recv_type = RecvTypeForReceiver(
          ctx, lookup_base, modal_member.method->receiver, modal_lower_type);
      if (!recv_type.ok) {
        result.diag_id = recv_type.diag_id;
        return result;
      }
      const auto method_perm = PermOfType(recv_type.type);
      if (!ReceiverPermissionAdmits(caller_perm, method_perm)) {
        SPEC_RULE("MethodCall-RecvPerm-Err");
        result.diag_id = "E-TYP-1605";
        return result;
      }
      if (!check_shared_receiver_access(method_perm)) {
        return result;
      }
      const auto args_ok = ArgsOk(ctx, modal_member.method->params, expr.args,
                                  type_expr, &type_place, modal_lower_type,
                                  &check_expr);
      if (!args_ok.ok) {
        result.diag_id = args_ok.diag_id;
        return result;
      }
      LowerTypeResult ret_type;
      if (!modal_member.method->return_type_opt) {
        ret_type = {true, std::nullopt, MakeTypePrim("()")};
      } else {
        ret_type = modal_lower_type(modal_member.method->return_type_opt);
      }
      if (!ret_type.ok) {
        result.diag_id = ret_type.diag_id;
        return result;
      }
      SPEC_RULE("T-Modal-Method");
      emit_deprecated_warning(modal_member.method->attrs);
      result.ok = true;
      result.type = ret_type.type;
      return result;
    }
    if (modal_member.transition_in_other_state) {
      SPEC_RULE("Transition-Source-Err");
      result.diag_id = "E-TYP-2056";
      return result;
    }
    if (modal_member.method_in_other_state) {
      SPEC_RULE("Modal-Method-NotFound");
      result.diag_id = "E-TYP-2053";
      return result;
    }

    SPEC_RULE("Modal-Method-NotFound");
    result.diag_id = "E-TYP-2053";
    return result;
  }

  if (const auto* dyn = std::get_if<TypeDynamic>(&lookup_base->node)) {
    if (IsFileSystemClassPath(dyn->path)) {
      const auto sig = LookupFileSystemMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsHeapAllocatorClassPath(dyn->path)) {
      const auto sig = LookupHeapAllocatorMethodSig(expr.name);
      if (sig.has_value()) {
        const bool raw_heap_method =
            sig->kind == HeapAllocatorMethodKind::AllocRaw ||
            sig->kind == HeapAllocatorMethodKind::DeallocRaw;
        if (raw_heap_method && !IsInUnsafeSpan(ctx, span)) {
          if (sig->kind == HeapAllocatorMethodKind::AllocRaw) {
            SPEC_RULE("AllocRaw-Unsafe-Err");
            result.diag_id = "E-MEM-3030";
          } else {
            SPEC_RULE("DeallocRaw-Unsafe-Err");
            result.diag_id = "E-MEM-3030";
          }
          return result;
        }
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsNetworkClassPath(dyn->path)) {
      const auto sig = LookupNetworkMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsExecutionDomainTypePath(dyn->path)) {
      const auto sig = LookupExecutionDomainMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsReactorClassPath(dyn->path)) {
      const auto* method = LookupClassMethod(ctx, dyn->path, expr.name);
      if (method) {
        if (!VTableEligible(*method)) {
          SPEC_RULE("MethodCall-StaticDispatchOnly-OnDynamic");
          result.diag_id = "E-TYP-2540";
          return result;
        }
        const auto recv_type = RecvTypeForReceiver(ctx, lookup_base, method->receiver, lower_type);
        if (!recv_type.ok) {
          result.diag_id = recv_type.diag_id;
          return result;
        }
        const auto method_perm = PermOfType(recv_type.type);
        if (!ReceiverPermissionAdmits(caller_perm, method_perm)) {
          SPEC_RULE("MethodCall-RecvPerm-Err");
          result.diag_id = "E-TYP-1605";
          return result;
        }
        if (!check_shared_receiver_access(method_perm)) {
          return result;
        }
        std::optional<TypeSubst> subst;
        if (method->generic_params && !method->generic_params->params.empty()) {
          const auto inferred = InferMethodSubst(
              ctx, method->generic_params->params, method->params, expr.args,
              type_expr, &type_place, lower_type);
          if (!inferred.ok) {
            result.diag_id = inferred.diag_id;
            return result;
          }
          subst = inferred.subst;
          const auto args_ok =
              ArgsOkWithSubst(ctx, method->params, expr.args, type_expr,
                              &type_place, lower_type, *subst, &check_expr);
          if (!args_ok.ok) {
            result.diag_id = args_ok.diag_id;
            return result;
          }
        } else {
          const auto args_ok =
              ArgsOk(ctx, method->params, expr.args, type_expr, &type_place,
                     lower_type, &check_expr);
          if (!args_ok.ok) {
            result.diag_id = args_ok.diag_id;
            return result;
          }
        }
        LowerTypeResult ret_type;
        if (!method->return_type_opt) {
          ret_type = {true, std::nullopt, MakeTypePrim("()")};
        } else {
          ret_type = LowerType(ctx, method->return_type_opt);
        }
        if (!ret_type.ok) {
          result.diag_id = ret_type.diag_id;
          return result;
        }
        if (subst.has_value()) {
          ret_type.type = InstantiateType(ret_type.type, *subst);
        }
        SPEC_RULE("T-MethodCall");
        emit_deprecated_warning(method->attrs);
        result.ok = true;
        result.type = ret_type.type;
        return result;
      }
    }
    // General $ClassName dynamic dispatch for user-defined classes (§11.3)
    {
      const auto* method = LookupClassMethod(ctx, dyn->path, expr.name);
      if (!method && !dyn->path.empty()) {
        ast::ClassPath short_path{dyn->path.back()};
        method = LookupClassMethod(ctx, short_path, expr.name);
        if (!method) {
          ast::ClassPath module_path = ctx.current_module;
          module_path.push_back(dyn->path.back());
          method = LookupClassMethod(ctx, module_path, expr.name);
        }
      }
      if (method) {
        if (!VTableEligible(*method)) {
          SPEC_RULE("MethodCall-StaticDispatchOnly-OnDynamic");
          result.diag_id = "E-TYP-2540";
          return result;
        }
        const auto recv_type = RecvTypeForReceiver(ctx, lookup_base, method->receiver, lower_type);
        if (!recv_type.ok) {
          result.diag_id = recv_type.diag_id;
          return result;
        }
        const auto method_perm = PermOfType(recv_type.type);
        if (!ReceiverPermissionAdmits(caller_perm, method_perm)) {
          SPEC_RULE("MethodCall-RecvPerm-Err");
          result.diag_id = "E-TYP-1605";
          return result;
        }
        if (!check_shared_receiver_access(method_perm)) {
          return result;
        }
        std::optional<TypeSubst> subst;
        if (method->generic_params && !method->generic_params->params.empty()) {
          const auto inferred = InferMethodSubst(
              ctx, method->generic_params->params, method->params, expr.args,
              type_expr, &type_place, lower_type);
          if (!inferred.ok) {
            result.diag_id = inferred.diag_id;
            return result;
          }
          subst = inferred.subst;
          const auto args_ok =
              ArgsOkWithSubst(ctx, method->params, expr.args, type_expr,
                              &type_place, lower_type, *subst, &check_expr);
          if (!args_ok.ok) {
            result.diag_id = args_ok.diag_id;
            return result;
          }
        } else {
          const auto args_ok =
              ArgsOk(ctx, method->params, expr.args, type_expr, &type_place,
                     lower_type, &check_expr);
          if (!args_ok.ok) {
            result.diag_id = args_ok.diag_id;
            return result;
          }
        }
        LowerTypeResult ret_type;
        if (!method->return_type_opt) {
          ret_type = {true, std::nullopt, MakeTypePrim("()")};
        } else {
          ret_type = LowerType(ctx, method->return_type_opt);
        }
        if (!ret_type.ok) {
          result.diag_id = ret_type.diag_id;
          return result;
        }
        if (subst.has_value()) {
          ret_type.type = InstantiateType(ret_type.type, *subst);
        }
        SPEC_RULE("T-MethodCall");
        emit_deprecated_warning(method->attrs);
        result.ok = true;
        result.type = ret_type.type;
        return result;
      }
    }
  }

  if (const auto* opaque = std::get_if<TypeOpaque>(&lookup_base->node)) {
    const ast::ClassMethodDecl* method =
        LookupClassMethod(ctx, opaque->class_path, expr.name);
    if (!method && !opaque->class_path.empty()) {
      ast::ClassPath short_path{opaque->class_path.back()};
      method = LookupClassMethod(ctx, short_path, expr.name);
      if (!method) {
        ast::ClassPath module_path = ctx.current_module;
        module_path.push_back(opaque->class_path.back());
        method = LookupClassMethod(ctx, module_path, expr.name);
      }
    }
    if (!method) {
      if (core::IsDebugEnabled("method")) {
        std::fprintf(stderr,
                     "debug opaque method not found: name=%s path=",
                     std::string(expr.name).c_str());
        for (std::size_t i = 0; i < opaque->class_path.size(); ++i) {
          if (i > 0) {
            std::fprintf(stderr, "::");
          }
          std::fprintf(stderr, "%s", opaque->class_path[i].c_str());
        }
        std::fprintf(stderr, " origin_file=%s origin_span=%zu:%zu-%zu:%zu\n",
                     opaque->origin_span.file.c_str(),
                     opaque->origin_span.start_line,
                     opaque->origin_span.start_col,
                     opaque->origin_span.end_line,
                     opaque->origin_span.end_col);
      }
      SPEC_RULE("LookupMethod-NotFound");
      result.diag_id = "E-TYP-2510";
      result.diag_detail = "method '" + std::string(expr.name) +
                           "' on type '" + TypeToString(lookup_base) + "'";
      return result;
    }

    const auto recv_type =
        RecvTypeForReceiver(ctx, lookup_base, method->receiver, lower_type);
    if (!recv_type.ok) {
      result.diag_id = recv_type.diag_id;
      return result;
    }
    const auto method_perm = PermOfType(recv_type.type);
    if (!ReceiverPermissionAdmits(caller_perm, method_perm)) {
      SPEC_RULE("MethodCall-RecvPerm-Err");
      result.diag_id = "E-TYP-1605";
      return result;
    }
    if (!check_shared_receiver_access(method_perm)) {
      return result;
    }

    std::optional<TypeSubst> subst;
    if (method->generic_params && !method->generic_params->params.empty()) {
      const auto inferred = InferMethodSubst(
          ctx, method->generic_params->params, method->params, expr.args,
          type_expr, &type_place, lower_type);
      if (!inferred.ok) {
        result.diag_id = inferred.diag_id;
        return result;
      }
      subst = inferred.subst;
      const auto args_ok =
          ArgsOkWithSubst(ctx, method->params, expr.args, type_expr,
                          &type_place, lower_type, *subst, &check_expr);
      if (!args_ok.ok) {
        result.diag_id = args_ok.diag_id;
        return result;
      }
    } else {
      const auto args_ok =
          ArgsOk(ctx, method->params, expr.args, type_expr, &type_place,
                 lower_type, &check_expr);
      if (!args_ok.ok) {
        result.diag_id = args_ok.diag_id;
        return result;
      }
    }

    LowerTypeResult ret_type;
    if (!method->return_type_opt) {
      ret_type = {true, std::nullopt, MakeTypePrim("()")};
    } else {
      ret_type = LowerType(ctx, method->return_type_opt);
    }
    if (!ret_type.ok) {
      result.diag_id = ret_type.diag_id;
      return result;
    }
    if (subst.has_value()) {
      ret_type.type = InstantiateType(ret_type.type, *subst);
    }
    SPEC_RULE("T-MethodCall");
    emit_deprecated_warning(method->attrs);
    result.ok = true;
    result.type = ret_type.type;
    return result;
  }

  if (const auto* path_type = std::get_if<TypePathType>(&lookup_base->node)) {
    if (IsProjectFilesTypePath(path_type->path)) {
      const auto sig = LookupProjectFilesMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsComptimeDiagnosticsTypePath(path_type->path)) {
      const auto sig = LookupComptimeDiagnosticsMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsIntrospectTypePath(path_type->path)) {
      const auto sig = LookupIntrospectMethodSig(expr.name);
      if (sig.has_value() &&
          handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
        return result;
      }
    }
    if (IsTypeEmitterTypePath(path_type->path) && IdEq(expr.name, "emit")) {
      std::vector<ast::Param> params;
      ast::Param param{};
      param.name = "node";
      auto ast_type = std::make_shared<ast::Type>();
      ast_type->span = span;
      ast_type->node = ast::TypePathType{{"Ast"}, {}};
      auto ast_item_type = std::make_shared<ast::Type>();
      ast_item_type->span = span;
      ast_item_type->node = ast::TypePathType{{"Ast", "Item"}, {}};
      auto ty = std::make_shared<ast::Type>();
      ty->span = span;
      ty->node = ast::TypeUnion{{ast_type, ast_item_type}};
      param.type = ty;
      params.push_back(std::move(param));
      if (handle_cap_method(Permission::Const, params, MakeTypePrim("()"))) {
        return result;
      }
    }
    if (IsContextTypePath(path_type->path)) {
      const auto sig = LookupContextMethodSig(expr.name, expr.args.size());
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
    if (IsSystemTypePath(path_type->path)) {
      const auto sig = LookupSystemMethodSig(expr.name);
      if (sig.has_value()) {
        if (handle_cap_method(sig->recv_perm, sig->params, sig->ret)) {
          return result;
        }
      }
    }
  }

  // Look up method using record_methods lookup
  const auto lookup = LookupMethodStatic(ctx, lookup_base, expr.name);
  if (!lookup.ok) {
    if (const auto* dyn = std::get_if<TypeDynamic>(&lookup_base->node)) {
      const ast::ClassMethodDecl* dyn_method =
          LookupClassMethod(ctx, dyn->path, expr.name);
      if (!dyn_method && !dyn->path.empty()) {
        ast::ClassPath short_path{dyn->path.back()};
        dyn_method = LookupClassMethod(ctx, short_path, expr.name);
        if (!dyn_method) {
          ast::ClassPath module_path = ctx.current_module;
          module_path.push_back(dyn->path.back());
          dyn_method = LookupClassMethod(ctx, module_path, expr.name);
        }
      }
      if (dyn_method && !VTableEligible(*dyn_method)) {
        SPEC_RULE("MethodCall-StaticDispatchOnly-OnDynamic");
        result.diag_id = "E-TYP-2540";
        return result;
      }
    }
    if (core::IsDebugEnabled("method")) {
      const char* kind = "unknown";
      if (!lookup_base) {
        kind = "null";
      } else if (std::holds_alternative<TypeDynamic>(lookup_base->node)) {
        kind = "dynamic";
      } else if (std::holds_alternative<TypeOpaque>(lookup_base->node)) {
        kind = "opaque";
      } else if (std::holds_alternative<TypePathType>(lookup_base->node)) {
        kind = "path";
      } else if (std::holds_alternative<TypeModalState>(lookup_base->node)) {
        kind = "modal";
      }
      std::fprintf(stderr,
                   "debug lookup failed: name=%s base_kind=%s diag=%s\n",
                   std::string(expr.name).c_str(),
                   kind,
                   lookup.diag_id.has_value()
                       ? std::string(*lookup.diag_id).c_str()
                       : "<none>");
    }
    result.diag_id = lookup.diag_id;
    return result;
  }

  const auto* record_method = lookup.record_method;
  const auto* class_method = lookup.class_method;
  if (!record_method && !class_method) {
    if (core::IsDebugEnabled("method")) {
      const char* kind = "unknown";
      if (!lookup_base) {
        kind = "null";
      } else if (std::holds_alternative<TypeDynamic>(lookup_base->node)) {
        kind = "dynamic";
      } else if (std::holds_alternative<TypeOpaque>(lookup_base->node)) {
        kind = "opaque";
      } else if (std::holds_alternative<TypePathType>(lookup_base->node)) {
        kind = "path";
      } else if (std::holds_alternative<TypeModalState>(lookup_base->node)) {
        kind = "modal";
      }
      std::fprintf(stderr,
                   "debug method not found: name=%s base_kind=%s\n",
                   std::string(expr.name).c_str(),
                   kind);
    }
    SPEC_RULE("LookupMethod-NotFound");
    result.diag_id = "LookupMethod-NotFound";
    result.diag_detail = "method '" + std::string(expr.name) +
                         "' on type '" + TypeToString(lookup_base) + "'";
    return result;
  }

  if (std::holds_alternative<TypeDynamic>(lookup_base->node) &&
      class_method && !VTableEligible(*class_method)) {
    SPEC_RULE("MethodCall-StaticDispatchOnly-OnDynamic");
    result.diag_id = "E-TYP-2540";
    return result;
  }

  LowerTypeFn method_lower_type = lower_type;
  if (record_method && lookup.record_decl) {
    ScopeContext record_method_ctx = ctx;
    record_method_ctx.scopes =
        cursive::analysis::BindTypeParams(ctx, lookup.record_decl->generic_params);

    std::optional<TypeSubst> record_subst;
    if (lookup.record_decl->generic_params.has_value()) {
      const auto& record_params = lookup.record_decl->generic_params->params;
      if (lookup.record_generic_args.size() > record_params.size()) {
        SPEC_RULE("LookupMethod-NotFound");
        result.diag_id = "LookupMethod-NotFound";
        return result;
      }
      record_subst = BuildSubstitution(record_params, lookup.record_generic_args);
    } else if (!lookup.record_generic_args.empty()) {
      SPEC_RULE("LookupMethod-NotFound");
      result.diag_id = "LookupMethod-NotFound";
      return result;
    }

    method_lower_type =
        [record_method_ctx, record_subst](
            const std::shared_ptr<ast::Type>& type) -> LowerTypeResult {
      const auto lowered = LowerType(record_method_ctx, type);
      if (!lowered.ok) {
        return {false, lowered.diag_id, {}};
      }
      if (!record_subst.has_value()) {
        return {true, std::nullopt, lowered.type};
      }
      return {true, std::nullopt, InstantiateType(lowered.type, *record_subst)};
    };
  }

  // Get method signature
  const ast::Receiver& receiver =
      record_method ? record_method->receiver : class_method->receiver;
  const auto& params = record_method ? record_method->params
                                     : class_method->params;

  // Check receiver permission
  const auto recv_type =
      RecvTypeForReceiver(ctx, lookup_base, receiver, method_lower_type);
  if (!recv_type.ok) {
    result.diag_id = recv_type.diag_id;
    return result;
  }
  const auto method_perm = PermOfType(recv_type.type);
  if (!ReceiverPermissionAdmits(caller_perm, method_perm)) {
    SPEC_RULE("MethodCall-RecvPerm-Err");
    result.diag_id = "E-TYP-1605";
    return result;
  }
  if (!check_shared_receiver_access(method_perm)) {
    return result;
  }

  std::optional<TypeSubst> subst;
  const auto* generic_params =
      (record_method && record_method->generic_params)
          ? &record_method->generic_params->params
          : ((class_method && class_method->generic_params)
                 ? &class_method->generic_params->params
                 : nullptr);
  if (generic_params && !generic_params->empty()) {
    const auto inferred = InferMethodSubst(
        ctx, *generic_params, params, expr.args, type_expr, &type_place,
        method_lower_type);
    if (!inferred.ok) {
      result.diag_id = inferred.diag_id;
      return result;
    }
    subst = inferred.subst;
    const auto args_ok =
        ArgsOkWithSubst(ctx, params, expr.args, type_expr, &type_place,
                        method_lower_type, *subst, &check_expr);
    if (!args_ok.ok) {
      result.diag_id = args_ok.diag_id;
      return result;
    }
  } else {
    const auto args_ok =
        ArgsOk(ctx, params, expr.args, type_expr, &type_place, method_lower_type,
               &check_expr);
    if (!args_ok.ok) {
      result.diag_id = args_ok.diag_id;
      return result;
    }
  }

  // Get return type
  LowerTypeResult ret_type;
  const auto ret_opt = record_method ? record_method->return_type_opt
                                     : class_method->return_type_opt;
  if (!ret_opt) {
    ret_type = {true, std::nullopt, MakeTypePrim("()")};
  } else {
    ret_type = method_lower_type(ret_opt);
  }
  if (!ret_type.ok) {
    result.diag_id = ret_type.diag_id;
    return result;
  }
  if (subst.has_value()) {
    ret_type.type = InstantiateType(ret_type.type, *subst);
  }

  if (record_method) {
    SPEC_RULE("T-Record-MethodCall");
    emit_deprecated_warning(record_method->attrs);
  } else {
    SPEC_RULE("T-MethodCall");
    emit_deprecated_warning(class_method->attrs);
  }
  result.ok = true;
  result.type = ret_type.type;
  return result;
}

}  // namespace cursive::analysis::expr
