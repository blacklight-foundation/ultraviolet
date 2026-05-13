// =============================================================================
// MIGRATION: item/modal_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.4: Modal Types (Definitions)
//   - Modal-WF: Modal well-formedness
//   - WF-ModalState: State well-formedness
//   - Modal-NoStates-Err / Modal-DupState-Err / Modal-StateName-Err
//   - WF-Modal-Payload (line 12300): Payload well-formedness
//   - modal_decl grammar (line 3103)
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/dynamic_context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsModalDecl() {
  SPEC_DEF("Modal-WF", "5.4");
  SPEC_DEF("WF-ModalState", "5.4");
  SPEC_DEF("WF-Modal-Payload", "5.4");
  SPEC_DEF("Modal-NoStates-Err", "5.4");
  SPEC_DEF("Modal-DupState-Err", "5.4");
  SPEC_DEF("Modal-StateName-Err", "5.4");
  SPEC_DEF("Modal-Payload-DupField", "5.4");
  SPEC_DEF("State-Specific-WF", "5.4");
  SPEC_DEF("WF-State-Method", "5.6");
  SPEC_DEF("WF-Transition", "5.6");
  SPEC_DEF("Transition-Target-Err", "5.6");
  SPEC_DEF("T-Modal-Method-Body", "5.6");
  SPEC_DEF("T-Modal-Transition-Body", "5.6");
  SPEC_DEF("StateVisOk", "5.2.14");
}

// =============================================================================
// HELPERS
// =============================================================================

// Lower type with well-formedness check
static LowerTypeResult LowerTypeWithWF(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    return {false, wf.diag_id, {}};
  }
  return lowered;
}

// Check if state names are distinct
static bool DistinctStateNames(const std::vector<ast::StateBlock>& states) {
  if (states.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& state : states) {
    if (!names.insert(state.name).second) {
      return false;
    }
  }
  return true;
}

// Check if field names within a state are distinct
static bool DistinctStateFieldNames(const std::vector<ast::StateFieldDecl>& fields) {
  if (fields.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& field : fields) {
    if (!names.insert(field.name).second) {
      return false;
    }
  }
  return true;
}

static bool DistinctStateMethodNames(
    const std::vector<ast::StateMethodDecl>& methods) {
  if (methods.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& method : methods) {
    if (!names.insert(method.name).second) {
      return false;
    }
  }
  return true;
}

static bool DistinctTransitionNames(
    const std::vector<ast::TransitionDecl>& transitions) {
  if (transitions.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& transition : transitions) {
    if (!names.insert(transition.name).second) {
      return false;
    }
  }
  return true;
}

static bool DistinctParamNames(const std::vector<ast::Param>& params) {
  if (params.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& param : params) {
    if (!names.insert(param.name).second) {
      return false;
    }
  }
  return true;
}

static bool HasReservedSelfParam(const std::vector<ast::Param>& params) {
  for (const auto& param : params) {
    if (IdEq(param.name, "self")) {
      return true;
    }
  }
  return false;
}

static bool ReceiverIsConst(const ast::Receiver& receiver) {
  if (const auto* shorthand = std::get_if<ast::ReceiverShorthand>(&receiver)) {
    return shorthand->perm == ast::ReceiverPerm::Const;
  }
  return false;
}

static ast::ExprPtr StateInvariantPredicateFor(
    const ast::TypeInvariant& invariant,
    std::string_view state_name) {
  if (!invariant.predicate) {
    return nullptr;
  }
  const auto* if_case = std::get_if<ast::IfCaseExpr>(&invariant.predicate->node);
  if (!if_case) {
    return invariant.predicate;
  }
  const auto* ident =
      if_case->scrutinee
          ? std::get_if<ast::IdentifierExpr>(&if_case->scrutinee->node)
          : nullptr;
  if (!ident || !IdEq(ident->name, "self")) {
    return invariant.predicate;
  }
  for (const auto& arm : if_case->cases) {
    if (!arm.pattern) {
      continue;
    }
    const auto* modal_pattern =
        std::get_if<ast::ModalPattern>(&arm.pattern->node);
    if (modal_pattern && IdEq(modal_pattern->state, state_name)) {
      return arm.body;
    }
  }
  return invariant.predicate;
}

static bool StateMemberNamesDisjoint(
    const std::vector<ast::StateMethodDecl>& methods,
    const std::vector<ast::TransitionDecl>& transitions) {
  if (methods.empty() || transitions.empty()) {
    return true;
  }
  std::unordered_set<std::string> method_names;
  method_names.reserve(methods.size());
  for (const auto& method : methods) {
    method_names.insert(method.name);
  }
  for (const auto& transition : transitions) {
    if (method_names.find(transition.name) != method_names.end()) {
      return false;
    }
  }
  return true;
}

static bool StateMemberNamesDisjointAll(
    const std::vector<ast::StateFieldDecl>& fields,
    const std::vector<ast::StateMethodDecl>& methods,
    const std::vector<ast::TransitionDecl>& transitions) {
  std::unordered_set<std::string> names;
  names.reserve(fields.size() + methods.size() + transitions.size());
  for (const auto& field : fields) {
    if (!names.insert(field.name).second) {
      return false;
    }
  }
  for (const auto& method : methods) {
    if (!names.insert(method.name).second) {
      return false;
    }
  }
  for (const auto& transition : transitions) {
    if (!names.insert(transition.name).second) {
      return false;
    }
  }
  return true;
}

static std::vector<TypeRef> ModalSelfGenericArgs(
    const GenericParamsResult& gen_params) {
  std::vector<TypeRef> args;
  args.reserve(gen_params.params.size());
  for (const auto& param : gen_params.params) {
    TypePath path;
    path.push_back(param.name);
    args.push_back(MakeTypePath(std::move(path)));
  }
  return args;
}

// Visibility ranking
static int VisRank(ast::Visibility vis) {
  switch (vis) {
    case ast::Visibility::Public:
      return 4;
    case ast::Visibility::Internal:
      return 3;
    case ast::Visibility::Private:
      return 1;
  }
  return 1;
}

// Check state member visibility doesn't exceed modal visibility
static bool StateMemberVisOk(const ast::ModalDecl& modal) {
  for (const auto& state : modal.states) {
    for (const auto& member : state.members) {
      std::optional<ast::Visibility> vis;
      if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
        vis = field->vis;
      } else if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
        vis = method->vis;
      } else if (const auto* transition = std::get_if<ast::TransitionDecl>(&member)) {
        vis = transition->vis;
      }
      if (vis.has_value() && VisRank(*vis) > VisRank(modal.vis)) {
        return false;
      }
    }
  }
  return true;
}

// Collect fields from state members
static std::vector<ast::StateFieldDecl> CollectStateFields(
    const std::vector<ast::StateMember>& members) {
  std::vector<ast::StateFieldDecl> fields;
  for (const auto& member : members) {
    if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
      fields.push_back(*field);
    }
  }
  return fields;
}

// Collect methods from state members
static std::vector<ast::StateMethodDecl> CollectStateMethods(
    const std::vector<ast::StateMember>& members) {
  std::vector<ast::StateMethodDecl> methods;
  for (const auto& member : members) {
    if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
      methods.push_back(*method);
    }
  }
  return methods;
}

// Collect transitions from state members
static std::vector<ast::TransitionDecl> CollectTransitions(
    const std::vector<ast::StateMember>& members) {
  std::vector<ast::TransitionDecl> transitions;
  for (const auto& member : members) {
    if (const auto* transition = std::get_if<ast::TransitionDecl>(&member)) {
      transitions.push_back(*transition);
    }
  }
  return transitions;
}

// Check class implementations are distinct
static bool DistinctClassPaths(const std::vector<ast::ClassPath>& impls) {
  if (impls.size() < 2) {
    return true;
  }
  std::vector<PathKey> keys;
  keys.reserve(impls.size());
  for (const auto& impl : impls) {
    keys.push_back(PathKeyOf(impl));
  }
  std::sort(keys.begin(), keys.end());
  return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
}

struct RequiredStateFieldInfo {
  std::string name;
  TypeRef type;
};

struct RequiredStateInfo {
  std::string name;
  std::vector<RequiredStateFieldInfo> fields;
};

// Compare required-state shapes for compatibility when a state name is inherited
// through multiple class paths.
static bool RequiredStateShapeCompatible(const RequiredStateInfo& lhs,
                                         const RequiredStateInfo& rhs) {
  if (lhs.fields.size() != rhs.fields.size()) {
    return false;
  }

  for (const auto& lhs_field : lhs.fields) {
    const RequiredStateFieldInfo* rhs_field = nullptr;
    for (const auto& candidate : rhs.fields) {
      if (IdEq(lhs_field.name, candidate.name)) {
        rhs_field = &candidate;
        break;
      }
    }
    if (!rhs_field) {
      return false;
    }
    const auto eq = TypeEquiv(lhs_field.type, rhs_field->type);
    if (!eq.ok || !eq.equiv) {
      return false;
    }
  }
  return true;
}

static std::optional<RequiredStateInfo> LowerRequiredStateInfo(
    const ScopeContext& ctx,
    const TypeRef& impl_self_type,
    const ast::AbstractStateDecl& state,
    std::optional<std::string_view>& diag_id) {
  RequiredStateInfo info;
  info.name = state.name;

  std::unordered_set<std::string> seen_fields;
  for (const auto& field : state.fields) {
    if (!seen_fields.insert(field.name).second) {
      diag_id = "Class-AbstractField-Dup";
      return std::nullopt;
    }
    const auto lowered = LowerTypeWithWF(ctx, field.type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return std::nullopt;
    }
    info.fields.push_back({
        field.name, SubstSelfType(impl_self_type, lowered.type)});
  }
  return info;
}

static bool CollectModalRequiredStates(
    const ScopeContext& ctx,
    const TypeRef& impl_self_type,
    const ast::ClassPath& class_path,
    std::vector<RequiredStateInfo>& out_states,
    std::optional<std::string_view>& diag_id) {
  const auto linearized = LinearizeClass(ctx, class_path);
  if (!linearized.ok) {
    diag_id = linearized.diag_id;
    return false;
  }

  std::unordered_map<std::string, std::size_t> state_index;
  out_states.clear();

  for (const auto& lin_path : linearized.order) {
    const auto class_it = ctx.sigma.classes.find(PathKeyOf(lin_path));
    if (class_it == ctx.sigma.classes.end()) {
      diag_id = "Superclass-Undefined";
      return false;
    }
    for (const auto* abstract_state : ClassAbstractStates(class_it->second)) {
      if (!abstract_state) {
        continue;
      }
      auto lowered_state =
          LowerRequiredStateInfo(ctx, impl_self_type, *abstract_state, diag_id);
      if (!lowered_state.has_value()) {
        return false;
      }

      const auto idx_it = state_index.find(lowered_state->name);
      if (idx_it == state_index.end()) {
        state_index.emplace(lowered_state->name, out_states.size());
        out_states.push_back(std::move(*lowered_state));
        continue;
      }

      auto& existing = out_states[idx_it->second];
      if (!RequiredStateShapeCompatible(existing, *lowered_state)) {
        diag_id = "E-TYP-2407";
        return false;
      }
    }
  }

  return true;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeModalDecl
// =============================================================================

ModalDeclResult TypeModalDecl(
    const ScopeContext& ctx,
    const ast::ModalDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsModalDecl();
  ModalDeclResult result;
  result.ok = true;

  // Build type path for this modal
  TypePath type_path;
  for (const auto& seg : module_path) {
    type_path.push_back(seg);
  }
  type_path.push_back(decl.name);

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  const auto self_generic_args = ModalSelfGenericArgs(gen_params);
  result.self_type = MakeTypePath(type_path, self_generic_args);

  // Process where clauses
  std::vector<std::string> type_param_names;
  for (const auto& gp : gen_params.params) {
    type_param_names.push_back(gp.name);
  }
  if (decl.predicate_clause_opt.has_value() &&
      !decl.predicate_clause_opt->empty()) {
    const auto where_result = ProcessWhereClause(
        ctx, *decl.predicate_clause_opt, type_param_names);
    if (!where_result.ok) {
      result.ok = false;
      result.diag_id = where_result.diag_id;
      return result;
    }
  }

  // Check class implementations are distinct
  if (!DistinctClassPaths(decl.implements)) {
    SPEC_RULE("Impl-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2506";
    return result;
  }

  // Check state member visibility
  if (!StateMemberVisOk(decl)) {
    SPEC_RULE("StateVisOk-Err");
    result.ok = false;
    result.diag_id = "StateMemberVisOk-Err";
    return result;
  }

  // Check state names are distinct
  if (!DistinctStateNames(decl.states)) {
    SPEC_RULE("Modal-DupState-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2051";
    return result;
  }

  for (const auto& state : decl.states) {
    if (state.name == decl.name) {
      SPEC_RULE("Modal-StateName-Err");
      result.ok = false;
      result.diag_id = "E-TYP-2054";
      return result;
    }
  }

  // Must have at least one state
  if (decl.states.empty()) {
    SPEC_RULE("Modal-NoStates-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2050";
    return result;
  }

  // Collect all state names for transition validation
  std::unordered_set<std::string> state_names;
  for (const auto& state : decl.states) {
    state_names.insert(state.name);
  }

  // Process each state
  for (const auto& state : decl.states) {
    StateInfo state_info;
    state_info.name = state.name;

    // Collect and check state fields
    const auto fields = CollectStateFields(state.members);
    std::vector<ast::StateFieldDecl> state_fields;
    for (const auto& member : state.members) {
      if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
        state_fields.push_back(*field);
      }
    }

    if (!DistinctStateFieldNames(state_fields)) {
      SPEC_RULE("Modal-Payload-DupField");
      result.ok = false;
      result.diag_id = "E-TYP-2058";
      return result;
    }

    // Process state fields
    for (const auto& field : fields) {
      const auto lowered = LowerTypeWithWF(ctx, field.type);
      if (!lowered.ok) {
        result.ok = false;
        result.diag_id = lowered.diag_id;
        return result;
      }
      const auto field_type = SubstSelfType(result.self_type, lowered.type);
      state_info.fields.push_back({field.name, field_type});
    }

    const auto methods = CollectStateMethods(state.members);
    const auto transitions = CollectTransitions(state.members);

    if (!DistinctStateMethodNames(methods)) {
      SPEC_RULE("StateMethod-Dup");
      result.ok = false;
      result.diag_id = "StateMethod-Dup";
      return result;
    }
    if (!DistinctTransitionNames(transitions)) {
      SPEC_RULE("Transition-Dup");
      result.ok = false;
      result.diag_id = "Transition-Dup";
      return result;
    }
    if (!StateMemberNamesDisjoint(methods, transitions) ||
        !StateMemberNamesDisjointAll(fields, methods, transitions)) {
      SPEC_RULE("StateMember-Name-Conflict");
      result.ok = false;
      result.diag_id = "StateMember-Name-Conflict";
      return result;
    }

    // Process state methods
    for (const auto& method : methods) {
      if (!DistinctParamNames(method.params)) {
        SPEC_RULE("ParamBinds-Duplicate-Err");
        result.ok = false;
        result.diag_id = "E-SEM-2713";
        return result;
      }
      if (HasReservedSelfParam(method.params)) {
        SPEC_RULE("Method-Context-Err");
        result.ok = false;
        result.diag_id = "E-SEM-3011";
        return result;
      }

      // Build method signature with modal state as receiver type.
      const auto state_type =
          MakeTypeModalState(type_path, state.name, self_generic_args);
      const auto sig = BuildMethodSignature(
          ctx, state_type, method.receiver, method.params, method.return_type_opt);
      if (!sig.ok) {
        result.ok = false;
        result.diag_id = sig.diag_id;
        return result;
      }
      SPEC_RULE("WF-State-Method");

      if (method.contract.has_value()) {
        ContractContext contract_ctx;
        contract_ctx.scope_ctx = &ctx;
        contract_ctx.receiver_type = state_type;
        contract_ctx.return_type = sig.return_type;
        for (const auto& binding : sig.bindings) {
          if (binding.first == "self") {
            continue;
          }
          contract_ctx.params[binding.first] = binding.second;
        }
        const auto contract_check =
            CheckContractWellFormed(contract_ctx, *method.contract);
        if (!contract_check.ok) {
          result.ok = false;
          result.diag_id = contract_check.diag_id;
          return result;
        }
      }

      // Type method body if present
      if (method.body) {
        TypeEnv env;
        env.scopes.emplace_back();
        for (const auto& binding : sig.bindings) {
          ast::Mutability binding_mut = ast::Mutability::Let;
          if (binding.first == "self") {
            if (const auto* shorthand =
                    std::get_if<ast::ReceiverShorthand>(&method.receiver)) {
              if (shorthand->perm == ast::ReceiverPerm::Unique) {
                binding_mut = ast::Mutability::Var;
              }
            }
          }
          env.scopes.back()[IdKeyOf(binding.first)] = {
              binding_mut, binding.second
          };
        }

        StmtTypeContext type_ctx;
        type_ctx.return_type = sig.return_type;
        type_ctx.diags = &diags;
        type_ctx.env_ref = &env;
        const std::array<DynamicScopeAncestor, 2> ancestors{
            MakeDynamicScopeAncestor(decl.attrs, decl.span),
            MakeDynamicScopeAncestor(method.attrs, method.span)};
        type_ctx.contract_dynamic =
            ComputeDynamicContext(method.body->span, ancestors);
        if (method.contract.has_value()) {
          type_ctx.contract = &*method.contract;
        }
        if (decl.invariant_opt.has_value() && ReceiverIsConst(method.receiver)) {
          const ast::ExprPtr state_invariant =
              StateInvariantPredicateFor(*decl.invariant_opt, state.name);
          type_ctx.proof_ctx =
              ExtendProofContextWithPredicateAt(type_ctx.proof_ctx,
                                                state_invariant,
                                                method.body->span);
        }

        ExprTypeFn type_expr = [&](const ast::ExprPtr& inner) {
          return TypeExpr(ctx, type_ctx, inner, env);
        };
        IdentTypeFn type_ident = [&](std::string_view name) -> ExprTypeResult {
          return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
        };
        PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
          return TypePlace(ctx, type_ctx, inner, env);
        };

        const auto body_result = TypeBlock(
            ctx, type_ctx, *method.body, env, type_expr, type_ident, type_place, &env);
        if (!body_result.ok) {
          result.ok = false;
          result.diag_id = body_result.diag_id;
          return result;
        }

        std::optional<Permission> recv_perm;
        if (const auto* shorthand =
                std::get_if<ast::ReceiverShorthand>(&method.receiver)) {
          if (shorthand->perm == ast::ReceiverPerm::Unique) {
            recv_perm = Permission::Unique;
          } else if (shorthand->perm == ast::ReceiverPerm::Shared) {
            recv_perm = Permission::Shared;
          } else {
            recv_perm = Permission::Const;
          }
        }
        const std::optional<BindSelfParam> self_param =
            BindSelfParam{state_type, std::nullopt, recv_perm};
        const auto bind_result = BindCheckBody(
            ctx, module_path, method.params, method.body, self_param);
        if (!bind_result.ok) {
          result.ok = false;
          result.diag_id = bind_result.diag_id;
          return result;
        }
        SPEC_RULE("T-Modal-Method-Body");
      }

      state_info.methods.push_back({method.name, sig.func_type});
    }

    // Process transitions
    for (const auto& transition : transitions) {
      if (!DistinctParamNames(transition.params)) {
        SPEC_RULE("ParamBinds-Duplicate-Err");
        result.ok = false;
        result.diag_id = "E-SEM-2713";
        return result;
      }
      if (HasReservedSelfParam(transition.params)) {
        SPEC_RULE("Method-Context-Err");
        result.ok = false;
        result.diag_id = "E-SEM-3011";
        return result;
      }

      // Validate target state exists
      if (state_names.find(transition.target_state) == state_names.end()) {
        SPEC_RULE("Transition-Target-Err");
        result.ok = false;
        result.diag_id = "E-TYP-2059";
        return result;
      }

      const auto source_type =
          MakeTypeModalState(type_path, state.name, self_generic_args);
      const auto target_type = MakeTypeModalState(type_path,
                                                  transition.target_state,
                                                  self_generic_args);
      const auto sig = BuildTransitionSignature(
          ctx, source_type, target_type, transition.params);
      if (!sig.ok) {
        result.ok = false;
        result.diag_id = sig.diag_id;
        return result;
      }
      SPEC_RULE("WF-Transition");

      // Type transition body
      if (transition.body) {
        TypeEnv env;
        env.scopes.emplace_back();
        for (const auto& binding : sig.bindings) {
          env.scopes.back()[IdKeyOf(binding.first)] = {
              ast::Mutability::Let, binding.second
          };
        }

        StmtTypeContext type_ctx;
        type_ctx.return_type = target_type;
        const std::array<DynamicScopeAncestor, 2> ancestors{
            MakeDynamicScopeAncestor(decl.attrs, decl.span),
            MakeDynamicScopeAncestor(transition.attrs, transition.span)};
        type_ctx.contract_dynamic =
            ComputeDynamicContext(transition.body->span, ancestors);
        auto type_expr = [&](const ast::ExprPtr& inner) {
          return TypeExpr(ctx, type_ctx, inner, env);
        };
        auto type_ident = [&](std::string_view name) -> ExprTypeResult {
          return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
        };
        auto type_place = [&](const ast::ExprPtr& inner) {
          return TypePlace(ctx, type_ctx, inner, env);
        };

        const auto body_result = TypeBlock(
            ctx, type_ctx, *transition.body, env, type_expr, type_ident, type_place);
        if (!body_result.ok) {
          result.ok = false;
          result.diag_id = body_result.diag_id;
          result.diag_detail = body_result.diag_detail;
          return result;
        }

        const auto sub = Subtyping(ctx, body_result.type, target_type);
        if (!sub.ok || !sub.subtype) {
          SPEC_RULE("Transition-Body-Err");
          result.ok = false;
          result.diag_id = "E-TYP-2055";
          result.diag_detail =
              "transition body type is not the declared target state";
          return result;
        }
        SPEC_RULE("T-Modal-Transition-Body");

        const std::optional<BindSelfParam> self_param = BindSelfParam{
            source_type, std::optional<ParamMode>(ParamMode::Move),
            Permission::Unique};
        const auto bind_result = BindCheckBody(
            ctx, module_path, transition.params, transition.body, self_param);
        if (!bind_result.ok) {
          result.ok = false;
          result.diag_id = bind_result.diag_id;
          return result;
        }
      }

      TransitionInfo trans_info;
      trans_info.name = transition.name;
      trans_info.target_state = transition.target_state;
      trans_info.func_type = sig.func_type;
      state_info.transitions.push_back(trans_info);
    }

    result.states.push_back(state_info);
  }

  // Process type invariant if present
  if (decl.invariant_opt) {
    ContractContext contract_ctx;
    contract_ctx.scope_ctx = &ctx;
    contract_ctx.receiver_type = result.self_type;
    contract_ctx.in_type_invariant = true;
    const auto inv_result =
        CheckTypeInvariant(contract_ctx, *decl.invariant_opt);
    if (!inv_result.ok) {
      result.ok = false;
      result.diag_id = inv_result.diag_id;
      return result;
    }
  }

  // Check class implementations
  std::vector<RequiredStateInfo> aggregate_required_states;
  std::unordered_map<std::string, std::size_t> aggregate_required_state_index;

  for (const auto& impl_path : decl.implements) {
    const auto class_key = PathKeyOf(impl_path);
    const auto class_it = ctx.sigma.classes.find(class_key);
    if (class_it == ctx.sigma.classes.end()) {
      SPEC_RULE("Superclass-Undefined");
      result.ok = false;
      result.diag_id = "Superclass-Undefined";
      return result;
    }

    const auto field_table = ClassFieldTable(ctx, impl_path);
    if (!field_table.ok) {
      result.ok = false;
      result.diag_id = field_table.diag_id;
      return result;
    }
    if (!field_table.fields.empty()) {
      SPEC_RULE("Impl-Field-Missing");
      result.ok = false;
      result.diag_id = "Impl-Field-Missing";
      result.diag_detail =
          "modal type '" + decl.name +
          "' cannot satisfy required class field '" +
          field_table.fields.front()->name + "'";
      return result;
    }

    if (!IsModalClass(class_it->second)) {
      continue;
    }

    std::vector<RequiredStateInfo> required_states;
    std::optional<std::string_view> required_states_diag;
    if (!CollectModalRequiredStates(
            ctx, result.self_type, impl_path, required_states,
            required_states_diag)) {
      result.ok = false;
      result.diag_id = required_states_diag;
      return result;
    }

    for (const auto& required_state : required_states) {
      const auto idx_it =
          aggregate_required_state_index.find(required_state.name);
      if (idx_it == aggregate_required_state_index.end()) {
        aggregate_required_state_index.emplace(
            required_state.name, aggregate_required_states.size());
        aggregate_required_states.push_back(required_state);
        continue;
      }

      auto& existing = aggregate_required_states[idx_it->second];
      if (!RequiredStateShapeCompatible(existing, required_state)) {
        SPEC_RULE("EffStates-Conflict");
        result.ok = false;
        result.diag_id = "E-TYP-2407";
        result.diag_detail =
            "conflicting abstract state requirements for '" +
            required_state.name + "'";
        return result;
      }
    }
  }

  for (const auto& required_state : aggregate_required_states) {
    const ast::StateBlock* impl_state = nullptr;
    for (const auto& state : decl.states) {
      if (IdEq(state.name, required_state.name)) {
        impl_state = &state;
        break;
      }
    }
    if (!impl_state) {
      SPEC_RULE("T-Impl-Complete");
      result.ok = false;
      result.diag_id = "E-TYP-2403";
      result.diag_detail =
          "missing required state '" + required_state.name + "'";
      return result;
    }

    const auto impl_state_fields = CollectStateFields(impl_state->members);
    for (const auto& required_field : required_state.fields) {
      const ast::StateFieldDecl* impl_field = nullptr;
      for (const auto& field : impl_state_fields) {
        if (IdEq(field.name, required_field.name)) {
          impl_field = &field;
          break;
        }
      }
      if (!impl_field) {
        SPEC_RULE("T-Impl-Complete");
        result.ok = false;
        result.diag_id = "E-TYP-2405";
        result.diag_detail =
            "state '" + required_state.name + "' missing payload field '" +
            required_field.name + "'";
        return result;
      }

      const auto lowered_impl_field = LowerTypeWithWF(ctx, impl_field->type);
      if (!lowered_impl_field.ok) {
        result.ok = false;
        result.diag_id = lowered_impl_field.diag_id;
        return result;
      }
      const auto impl_field_type =
          SubstSelfType(result.self_type, lowered_impl_field.type);
      const auto field_equiv = TypeEquiv(required_field.type, impl_field_type);
      if (!field_equiv.ok || !field_equiv.equiv) {
        SPEC_RULE("T-Field-Compat");
        result.ok = false;
        result.diag_id = "Impl-Field-Type-Err";
        result.diag_detail =
            "state '" + required_state.name + "', field '" +
            required_field.name + "' has incompatible type";
        return result;
      }
    }
  }

  SPEC_RULE("Modal-WF");
  return result;
}

// =============================================================================
// EXPORTED: TypeModalDeclSignature (first pass)
// =============================================================================

ModalDeclResult TypeModalDeclSignature(
    const ScopeContext& ctx,
    const ast::ModalDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsModalDecl();
  ModalDeclResult result;
  result.ok = true;

  // Build type path
  TypePath type_path;
  for (const auto& seg : module_path) {
    type_path.push_back(seg);
  }
  type_path.push_back(decl.name);

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  const auto self_generic_args = ModalSelfGenericArgs(gen_params);
  result.self_type = MakeTypePath(type_path, self_generic_args);

  // Check state names are distinct
  if (!DistinctStateNames(decl.states)) {
    SPEC_RULE("Modal-DupState-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2051";
    return result;
  }

  for (const auto& state : decl.states) {
    if (state.name == decl.name) {
      SPEC_RULE("Modal-StateName-Err");
      result.ok = false;
      result.diag_id = "E-TYP-2054";
      return result;
    }
  }

  // Process state signatures
  std::unordered_set<std::string> state_names;
  for (const auto& state : decl.states) {
    state_names.insert(state.name);
  }
  for (const auto& state : decl.states) {
    StateInfo state_info;
    state_info.name = state.name;

    // Process field types
    const auto fields = CollectStateFields(state.members);
    for (const auto& field : fields) {
      const auto lowered = LowerTypeWithWF(ctx, field.type);
      if (!lowered.ok) {
        result.ok = false;
        result.diag_id = lowered.diag_id;
        return result;
      }
      const auto field_type = SubstSelfType(result.self_type, lowered.type);
      state_info.fields.push_back({field.name, field_type});
    }

    const auto methods = CollectStateMethods(state.members);
    const auto transitions = CollectTransitions(state.members);

    if (!DistinctStateMethodNames(methods)) {
      SPEC_RULE("StateMethod-Dup");
      result.ok = false;
      result.diag_id = "StateMethod-Dup";
      return result;
    }
    if (!DistinctTransitionNames(transitions)) {
      SPEC_RULE("Transition-Dup");
      result.ok = false;
      result.diag_id = "Transition-Dup";
      return result;
    }
    if (!StateMemberNamesDisjoint(methods, transitions) ||
        !StateMemberNamesDisjointAll(fields, methods, transitions)) {
      SPEC_RULE("StateMember-Name-Conflict");
      result.ok = false;
      result.diag_id = "StateMember-Name-Conflict";
      return result;
    }

    // Process method signatures
    for (const auto& method : methods) {
      if (!DistinctParamNames(method.params)) {
        SPEC_RULE("ParamBinds-Duplicate-Err");
        result.ok = false;
        result.diag_id = "E-SEM-2713";
        return result;
      }
      if (HasReservedSelfParam(method.params)) {
        SPEC_RULE("Method-Context-Err");
        result.ok = false;
        result.diag_id = "E-SEM-3011";
        return result;
      }

      const auto state_type =
          MakeTypeModalState(type_path, state.name, self_generic_args);
      const auto sig = BuildMethodSignature(
          ctx, state_type, method.receiver, method.params, method.return_type_opt);
      if (!sig.ok) {
        result.ok = false;
        result.diag_id = sig.diag_id;
        return result;
      }
      SPEC_RULE("WF-State-Method");
      state_info.methods.push_back({method.name, sig.func_type});
    }

    // Process transition signatures
    for (const auto& transition : transitions) {
      if (!DistinctParamNames(transition.params)) {
        SPEC_RULE("ParamBinds-Duplicate-Err");
        result.ok = false;
        result.diag_id = "E-SEM-2713";
        return result;
      }
      if (HasReservedSelfParam(transition.params)) {
        SPEC_RULE("Method-Context-Err");
        result.ok = false;
        result.diag_id = "E-SEM-3011";
        return result;
      }
      if (state_names.find(transition.target_state) == state_names.end()) {
        SPEC_RULE("Transition-Target-Err");
        result.ok = false;
        result.diag_id = "E-TYP-2059";
        return result;
      }

      const auto source_type =
          MakeTypeModalState(type_path, state.name, self_generic_args);
      const auto target_type = MakeTypeModalState(type_path,
                                                  transition.target_state,
                                                  self_generic_args);
      const auto sig = BuildTransitionSignature(
          ctx, source_type, target_type, transition.params);
      if (!sig.ok) {
        result.ok = false;
        result.diag_id = sig.diag_id;
        return result;
      }
      SPEC_RULE("WF-Transition");

      TransitionInfo trans_info;
      trans_info.name = transition.name;
      trans_info.target_state = transition.target_state;
      trans_info.func_type = sig.func_type;
      state_info.transitions.push_back(trans_info);
    }

    result.states.push_back(state_info);
  }

  SPEC_RULE("Modal-WF");
  return result;
}

}  // namespace cursive::analysis
