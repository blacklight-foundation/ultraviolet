// =============================================================================
// MIGRATION: item/class_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.3.1: Classes (Cursive0)
//   - WF-ClassDecl (line 10564): Class well-formedness
//   - WF-Class (line 11964, 22625): Class requirements
//   - WF-Class-Method (line 11949): Method well-formedness
//   - WF-Class-Self (line 22652): Self type in classes
//   - class_decl grammar (line 3104)
//   - class_item grammar (line 3130)
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
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/dynamic_context.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/composite/classes.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsClassDecl() {
  SPEC_DEF("WF-ClassDecl", "5.3.1");
  SPEC_DEF("WF-Class", "5.3.1");
  SPEC_DEF("WF-Class-Method", "5.3.1");
  SPEC_DEF("WF-Class-Self", "5.3.1");
  SPEC_DEF("WF-Class-AssocType", "5.3.1");
  SPEC_DEF("WF-Class-Super", "5.3.1");
  SPEC_DEF("Class-Name-Conflict", "5.3.1");
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

static std::vector<IdKey> MethodNames(const std::vector<ast::ClassItem>& items) {
  SPEC_RULE("MethodNames");
  std::vector<IdKey> names;
  for (const auto& item : items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      names.push_back(IdKeyOf(method->name));
    }
  }
  return names;
}

static std::vector<IdKey> FieldNames(
    const std::vector<ast::ClassFieldDecl>& fields) {
  SPEC_RULE("FieldNames");
  std::vector<IdKey> names;
  names.reserve(fields.size());
  for (const auto& field : fields) {
    names.push_back(IdKeyOf(field.name));
  }
  return names;
}

static bool DistinctClassMemberNameKeys(const std::vector<IdKey>& names) {
  if (names.size() < 2) {
    return true;
  }
  std::unordered_set<IdKey> seen;
  for (const auto& name : names) {
    if (!seen.insert(name).second) {
      return false;
    }
  }
  return true;
}

static bool DisjointClassMemberNameKeys(const std::vector<IdKey>& lhs,
                                        const std::vector<IdKey>& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return true;
  }
  std::unordered_set<IdKey> seen;
  seen.reserve(lhs.size());
  for (const auto& name : lhs) {
    seen.insert(name);
  }
  for (const auto& name : rhs) {
    if (seen.find(name) != seen.end()) {
      return false;
    }
  }
  return true;
}

// Check if associated type names are distinct
static bool DistinctAssocTypeNames(const std::vector<ast::AssociatedTypeDecl>& types) {
  if (types.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& t : types) {
    if (!names.insert(t.name).second) {
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

// Check superclass bounds are distinct
static bool DistinctSuperclasses(const std::vector<ast::ClassPath>& supers) {
  if (supers.size() < 2) {
    return true;
  }
  std::vector<PathKey> keys;
  keys.reserve(supers.size());
  for (const auto& s : supers) {
    keys.push_back(PathKeyOf(s));
  }
  std::sort(keys.begin(), keys.end());
  return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
}

// Collect methods from class items
static std::vector<ast::ClassMethodDecl> CollectMethods(
    const std::vector<ast::ClassItem>& items) {
  std::vector<ast::ClassMethodDecl> methods;
  for (const auto& item : items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      methods.push_back(*method);
    }
  }
  return methods;
}

// Collect associated types from class items
static std::vector<ast::AssociatedTypeDecl> CollectAssocTypes(
    const std::vector<ast::ClassItem>& items) {
  std::vector<ast::AssociatedTypeDecl> types;
  for (const auto& item : items) {
    if (const auto* assoc = std::get_if<ast::AssociatedTypeDecl>(&item)) {
      types.push_back(*assoc);
    }
  }
  return types;
}

// Collect abstract fields from class items
static std::vector<ast::ClassFieldDecl> CollectFields(
    const std::vector<ast::ClassItem>& items) {
  std::vector<ast::ClassFieldDecl> fields;
  for (const auto& item : items) {
    if (const auto* field = std::get_if<ast::ClassFieldDecl>(&item)) {
      fields.push_back(*field);
    }
  }
  return fields;
}

// Collect abstract states from class items
static std::vector<ast::AbstractStateDecl> CollectAbstractStates(
    const std::vector<ast::ClassItem>& items) {
  std::vector<ast::AbstractStateDecl> states;
  for (const auto& item : items) {
    if (const auto* state = std::get_if<ast::AbstractStateDecl>(&item)) {
      states.push_back(*state);
    }
  }
  return states;
}

static bool DistinctAbstractStateNames(
    const std::vector<ast::AbstractStateDecl>& states) {
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

}  // namespace

// =============================================================================
// EXPORTED: TypeClassDecl
// =============================================================================

ClassDeclResult TypeClassDecl(
    const ScopeContext& ctx,
    const ast::ClassDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsClassDecl();
  ClassDeclResult result;
  result.ok = true;
  result.name = decl.name;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(decl.attrs, "class declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Build class path
  TypePath class_path;
  for (const auto& seg : module_path) {
    class_path.push_back(seg);
  }
  class_path.push_back(decl.name);
  result.class_path = class_path;

  // Self type (abstract within class)
  result.self_type = SelfVarType();

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  for (const auto& gp : gen_params.params) {
    result.generic_params.push_back(gp.name);
  }

  // Process where clauses
  if (decl.predicate_clause_opt.has_value()) {
    std::vector<std::string> type_param_names;
    for (const auto& gp : gen_params.params) {
      type_param_names.push_back(gp.name);
    }
    const auto where_result = ProcessWhereClause(
        ctx, *decl.predicate_clause_opt, type_param_names);
    if (!where_result.ok) {
      result.ok = false;
      result.diag_id = where_result.diag_id;
      return result;
    }
  }

  // Check superclass bounds are distinct
  if (!DistinctSuperclasses(decl.supers)) {
    SPEC_RULE("WF-Class-Super-Duplicate");
    result.ok = false;
    result.diag_id = "Impl-Duplicate-Class-Err";
    return result;
  }

  // Check superclasses exist and no cycles
  for (const auto& super_path : decl.supers) {
    const auto super_key = PathKeyOf(super_path);
    if (ctx.sigma.classes.find(super_key) == ctx.sigma.classes.end()) {
      SPEC_RULE("Superclass-Undefined");
      result.ok = false;
      result.diag_id = "Superclass-Undefined";
      return result;
    }

    result.superclasses.push_back(super_path);
  }

  // Collect and check methods
  const auto methods = CollectMethods(decl.items);
  const auto method_names = MethodNames(decl.items);
  if (!DistinctClassMemberNameKeys(method_names)) {
    SPEC_RULE("WF-Class-Method-Duplicate");
    result.ok = false;
    result.diag_id = "E-TYP-2500";
    return result;
  }

  // Process methods
  for (const auto& method : methods) {
    const auto method_attr_validation =
        ValidateAttributes(method.attrs, AttributeTarget::Method);
    if (!method_attr_validation.ok) {
      result.ok = false;
      result.diag_id = method_attr_validation.diag_id;
      return result;
    }
    if (!DistinctParamNames(method.params)) {
      SPEC_RULE("WF-Class-Method");
      SPEC_RULE("ParamBinds-Duplicate-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2713";
      return result;
    }
    if (HasReservedSelfParam(method.params)) {
      SPEC_RULE("WF-Class-Method");
      SPEC_RULE("Method-Context-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3011";
      return result;
    }

    // Build method signature
    const auto sig = BuildMethodSignature(
        ctx, result.self_type, method.receiver,
        method.params, method.return_type_opt);
    if (!sig.ok) {
      result.ok = false;
      result.diag_id = sig.diag_id;
      return result;
    }

    std::optional<Permission> recv_perm;
    if (const auto* shorthand = std::get_if<ast::ReceiverShorthand>(&method.receiver)) {
      if (shorthand->perm == ast::ReceiverPerm::Unique) {
        recv_perm = Permission::Unique;
      } else if (shorthand->perm == ast::ReceiverPerm::Shared) {
        recv_perm = Permission::Shared;
      } else {
        recv_perm = Permission::Const;
      }
    }
    const std::optional<BindSelfParam> self_param =
        BindSelfParam{result.self_type, std::nullopt, recv_perm};

    ClassMethodInfo method_info;
    method_info.name = method.name;
    method_info.func_type = sig.func_type;
    method_info.has_default = method.body_opt != nullptr;

    // Type default implementation if present
    if (method.body_opt) {
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
          ComputeDynamicContext(method.body_opt->span, ancestors);
      if (method.contract.has_value()) {
        type_ctx.contract = &*method.contract;
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
          ctx, type_ctx, *method.body_opt, env, type_expr, type_ident, type_place, &env);
      if (!body_result.ok) {
        result.ok = false;
        result.diag_id = body_result.diag_id;
        return result;
      }

      const auto bind_result = BindCheckBody(
          ctx, module_path, method.params, method.body_opt, self_param);
      if (!bind_result.ok) {
        result.ok = false;
        result.diag_id = bind_result.diag_id;
        return result;
      }
    }

    result.methods.push_back(method_info);
  }

  // Collect and check associated types
  const auto assoc_types = CollectAssocTypes(decl.items);
  if (!DistinctAssocTypeNames(assoc_types)) {
    SPEC_RULE("WF-Class-AssocType-Duplicate");
    result.ok = false;
    result.diag_id = "E-TYP-2504";
    return result;
  }

  // Process associated types
  for (const auto& assoc : assoc_types) {
    const auto assoc_attr_validation =
        ValidateAttributes(assoc.attrs, AttributeTarget::TypeAlias);
    if (!assoc_attr_validation.ok) {
      result.ok = false;
      result.diag_id = assoc_attr_validation.diag_id;
      return result;
    }

    ClassAssocTypeInfo assoc_info;
    assoc_info.name = assoc.name;

    // Check default type if present
    if (assoc.default_type) {
      const auto lowered = LowerTypeWithWF(ctx, assoc.default_type);
      if (!lowered.ok) {
        result.ok = false;
        result.diag_id = lowered.diag_id;
        return result;
      }
      assoc_info.default_type = lowered.type;
    }

    result.assoc_types.push_back(assoc_info);
  }

  // Process abstract fields
  const auto fields = CollectFields(decl.items);
  const auto field_names = FieldNames(fields);
  const auto abstract_states = CollectAbstractStates(decl.items);
  if (!DistinctClassMemberNameKeys(field_names)) {
    SPEC_RULE("Class-AbstractField-Dup");
    result.ok = false;
    result.diag_id = "E-TYP-2408";
    return result;
  }
  if (!DisjointClassMemberNameKeys(method_names, field_names)) {
    SPEC_RULE("Class-Name-Conflict");
    result.ok = false;
    result.diag_id = "E-TYP-2505";
    return result;
  }
  if (!DistinctAbstractStateNames(abstract_states)) {
    SPEC_RULE("WF-Class-State-Duplicate");
    result.ok = false;
    result.diag_id = "E-TYP-2409";
    return result;
  }
  for (const auto& field : fields) {
    if (HasAttribute(field.attrs, attrs::kDynamic)) {
      result.ok = false;
      result.diag_id = "E-CON-0412";
      return result;
    }
    const auto field_attr_validation =
        ValidateAttributes(field.attrs, AttributeTarget::Field);
    if (!field_attr_validation.ok) {
      result.ok = false;
      result.diag_id = field_attr_validation.diag_id;
      return result;
    }

    const auto lowered = LowerTypeWithWF(ctx, field.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }

    ClassFieldInfo field_info;
    field_info.name = field.name;
    field_info.type = SubstSelfType(result.self_type, lowered.type);
    result.fields.push_back(field_info);
  }

  // Cross-category class member names must be globally unique:
  // method names, associated-type names, and abstract-field names.
  {
    std::unordered_set<IdKey> seen;
    for (const auto& method : methods) {
      seen.insert(IdKeyOf(method.name));
    }
    for (const auto& assoc : assoc_types) {
      const auto key = IdKeyOf(assoc.name);
      if (seen.find(key) != seen.end()) {
        SPEC_RULE("Class-Name-Conflict");
        result.ok = false;
        result.diag_id = "E-TYP-2505";
        return result;
      }
      seen.insert(key);
    }
    for (const auto& field : fields) {
      const auto key = IdKeyOf(field.name);
      if (seen.find(key) != seen.end()) {
        SPEC_RULE("Class-Name-Conflict");
        result.ok = false;
        result.diag_id = "E-TYP-2505";
        return result;
      }
      seen.insert(key);
    }
    for (const auto& state : abstract_states) {
      const auto key = IdKeyOf(state.name);
      if (seen.find(key) != seen.end()) {
        SPEC_RULE("Class-Name-Conflict");
        result.ok = false;
        result.diag_id = "E-TYP-2505";
        return result;
      }
      seen.insert(key);
    }
  }

  // Check modal class properties
  result.is_modal = decl.modal;
  if (decl.modal) {
    // Process abstract states for modal classes
    for (const auto& state : abstract_states) {
      result.abstract_states.push_back(state.name);
    }
  }

  // Effective member-set checks across the superclass linearization.
  // These emit EffMethods-Conflict / EffFields-Conflict when inherited
  // members collide with incompatible signatures.
  ast::ClassPath self_class_path;
  for (const auto& seg : module_path) {
    self_class_path.push_back(seg);
  }
  self_class_path.push_back(decl.name);

  const auto method_table = ClassMethodTable(ctx, self_class_path);
  if (!method_table.ok) {
    if (method_table.diag_id.has_value() &&
        *method_table.diag_id == "E-TYP-2508") {
      SPEC_RULE("Superclass-Cycle");
    }
    result.ok = false;
    result.diag_id = method_table.diag_id;
    return result;
  }

  const auto field_table = ClassFieldTable(ctx, self_class_path);
  if (!field_table.ok) {
    if (field_table.diag_id.has_value() &&
        *field_table.diag_id == "E-TYP-2508") {
      SPEC_RULE("Superclass-Cycle");
    }
    result.ok = false;
    result.diag_id = field_table.diag_id;
    return result;
  }

  SPEC_RULE("WF-ClassDecl-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeClassDeclSignature (first pass)
// =============================================================================

ClassDeclResult TypeClassDeclSignature(
    const ScopeContext& ctx,
    const ast::ClassDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsClassDecl();
  ClassDeclResult result;
  result.ok = true;
  result.name = decl.name;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(decl.attrs, "class declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Build class path
  TypePath class_path;
  for (const auto& seg : module_path) {
    class_path.push_back(seg);
  }
  class_path.push_back(decl.name);
  result.class_path = class_path;

  // Self type
  result.self_type = SelfVarType();

  // Process generic parameters
  const auto gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  for (const auto& gp : gen_params.params) {
    result.generic_params.push_back(gp.name);
  }

  // Superclasses
  for (const auto& super_path : decl.supers) {
    result.superclasses.push_back(super_path);
  }

  // Collect method signatures
  const auto methods = CollectMethods(decl.items);
  const auto method_names = MethodNames(decl.items);
  if (!DistinctClassMemberNameKeys(method_names)) {
    SPEC_RULE("WF-Class-Method-Duplicate");
    result.ok = false;
    result.diag_id = "E-TYP-2500";
    return result;
  }

  for (const auto& method : methods) {
    const auto method_attr_validation =
        ValidateAttributes(method.attrs, AttributeTarget::Method);
    if (!method_attr_validation.ok) {
      result.ok = false;
      result.diag_id = method_attr_validation.diag_id;
      return result;
    }
    if (!DistinctParamNames(method.params)) {
      SPEC_RULE("WF-Class-Method");
      SPEC_RULE("ParamBinds-Duplicate-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2713";
      return result;
    }
    if (HasReservedSelfParam(method.params)) {
      SPEC_RULE("WF-Class-Method");
      SPEC_RULE("Method-Context-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3011";
      return result;
    }

    const auto sig = BuildMethodSignature(
        ctx, result.self_type, method.receiver,
        method.params, method.return_type_opt);
    if (!sig.ok) {
      result.ok = false;
      result.diag_id = sig.diag_id;
      return result;
    }

    ClassMethodInfo method_info;
    method_info.name = method.name;
    method_info.func_type = sig.func_type;
    method_info.has_default = method.body_opt != nullptr;
    result.methods.push_back(method_info);
  }

  // Collect associated type signatures
  const auto assoc_types = CollectAssocTypes(decl.items);
  for (const auto& assoc : assoc_types) {
    const auto assoc_attr_validation =
        ValidateAttributes(assoc.attrs, AttributeTarget::TypeAlias);
    if (!assoc_attr_validation.ok) {
      result.ok = false;
      result.diag_id = assoc_attr_validation.diag_id;
      return result;
    }

    ClassAssocTypeInfo assoc_info;
    assoc_info.name = assoc.name;
    // Note: AssociatedTypeDecl doesn't have bounds - bounds are in type predicates
    result.assoc_types.push_back(assoc_info);
  }

  // Collect field signatures
  const auto fields = CollectFields(decl.items);
  const auto field_names = FieldNames(fields);
  const auto abstract_states = CollectAbstractStates(decl.items);
  if (!DistinctClassMemberNameKeys(field_names)) {
    SPEC_RULE("Class-AbstractField-Dup");
    result.ok = false;
    result.diag_id = "E-TYP-2408";
    return result;
  }
  if (!DisjointClassMemberNameKeys(method_names, field_names)) {
    SPEC_RULE("Class-Name-Conflict");
    result.ok = false;
    result.diag_id = "E-TYP-2505";
    return result;
  }
  if (!DistinctAbstractStateNames(abstract_states)) {
    SPEC_RULE("WF-Class-State-Duplicate");
    result.ok = false;
    result.diag_id = "E-TYP-2409";
    return result;
  }
  for (const auto& field : fields) {
    if (HasAttribute(field.attrs, attrs::kDynamic)) {
      result.ok = false;
      result.diag_id = "E-CON-0412";
      return result;
    }
    const auto field_attr_validation =
        ValidateAttributes(field.attrs, AttributeTarget::Field);
    if (!field_attr_validation.ok) {
      result.ok = false;
      result.diag_id = field_attr_validation.diag_id;
      return result;
    }

    const auto lowered = LowerTypeWithWF(ctx, field.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }

    ClassFieldInfo field_info;
    field_info.name = field.name;
    field_info.type = SubstSelfType(result.self_type, lowered.type);
    result.fields.push_back(field_info);
  }

  result.is_modal = decl.modal;

  SPEC_RULE("WF-ClassDecl-Sig-Ok");
  return result;
}

}  // namespace cursive::analysis
