// =============================================================================
// MIGRATION: item/record_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.3.2: Record Declarations
//   Section 5.2.8: Default Record Construction
//   - WF-Record (lines 9078-9081): Record well-formedness
//   - WF-Record-DupField (lines 9083-9086): Duplicate field error
//   - Fields(R), DefaultConstructible(R) predicates
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <array>
#include <cstdint>
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
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_layout.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/records.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/resolve/scopes.h"
#include "02_source/ast/ast.h"
#include "../typecheck_diag_lookup.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsRecordDecl() {
  SPEC_DEF("WF-Record", "5.2.14");
  SPEC_DEF("WF-Record-DupField", "5.2.14");
  SPEC_DEF("InitOk", "5.2.14");
  SPEC_DEF("T-Record-Default", "5.2.8");
  SPEC_DEF("FieldVisOk", "5.2.14");
  SPEC_DEF("TypeInvariant", "5.2.14");
  SPEC_DEF("Impl-Ok", "5.2.14");
  SPEC_DEF("WF-Record-Method", "5.3.2");
  SPEC_DEF("T-Record-Method-Body", "5.3.2");
  SPEC_DEF("WF-Record-Methods", "5.3.2");
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

// Check if field names are distinct
static bool DistinctFieldNames(const std::vector<ast::FieldDecl>& fields) {
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

static bool DistinctMethodNames(
    const std::vector<const ast::MethodDecl*>& methods) {
  if (methods.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto* method : methods) {
    if (!method) {
      continue;
    }
    if (!names.insert(method->name).second) {
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

static bool HasExplicitReturn(const ast::Block& block) {
  auto stmtHasExplicitReturn = [&](const auto& self, const ast::Stmt& stmt) -> bool {
    return std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return true;
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            return node.body && HasExplicitReturn(*node.body);
          }
          return false;
        },
        stmt);
  };
  if (block.tail_opt) {
    return false;
  }
  return !block.stmts.empty() && stmtHasExplicitReturn(stmtHasExplicitReturn, block.stmts.back());
}

// Get fields from record members
static std::vector<ast::FieldDecl> GetFields(const ast::RecordDecl& record) {
  std::vector<ast::FieldDecl> fields;
  for (const auto& member : record.members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      fields.push_back(*field);
    }
  }
  return fields;
}

// Get methods from record members
static std::vector<const ast::MethodDecl*> GetMethods(const ast::RecordDecl& record) {
  std::vector<const ast::MethodDecl*> methods;
  for (const auto& member : record.members) {
    if (const auto* method = std::get_if<ast::MethodDecl>(&member)) {
      methods.push_back(method);
    }
  }
  return methods;
}

static const ast::AssociatedTypeDecl* FindRecordAssociatedTypeDecl(
    const ast::RecordDecl& record,
    std::string_view name) {
  for (const auto& member : record.members) {
    if (const auto* assoc = std::get_if<ast::AssociatedTypeDecl>(&member)) {
      if (IdEq(assoc->name, name)) {
        return assoc;
      }
    }
  }
  return nullptr;
}

static bool CollectRecordAssociatedTypeBindings(
    const ScopeContext& ctx,
    const ast::RecordDecl& record,
    const TypeRef& self_type,
    TypeSubst& subst,
    std::optional<std::string_view>& diag_id) {
  for (const auto& member : record.members) {
    const auto* assoc = std::get_if<ast::AssociatedTypeDecl>(&member);
    if (!assoc || !assoc->default_type) {
      continue;
    }
    const auto lowered = LowerTypeWithWF(ctx, assoc->default_type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return false;
    }
    subst[assoc->name] = SubstSelfType(self_type, lowered.type, &subst);
  }
  return true;
}

static bool BuildClassAssociatedTypeBindings(
    const ScopeContext& ctx,
    const ast::RecordDecl& record,
    const ast::ClassDecl& class_decl,
    const TypeRef& self_type,
    const TypeSubst& record_assoc_subst,
    TypeSubst& out_subst,
    std::optional<std::string_view>& diag_id) {
  out_subst = record_assoc_subst;
  for (const auto& item : class_decl.items) {
    const auto* assoc = std::get_if<ast::AssociatedTypeDecl>(&item);
    if (!assoc) {
      continue;
    }
    if (out_subst.find(assoc->name) != out_subst.end()) {
      continue;
    }

    const auto* record_assoc = FindRecordAssociatedTypeDecl(record, assoc->name);
    const std::shared_ptr<ast::Type>* binding_type = nullptr;
    if (record_assoc && record_assoc->default_type) {
      binding_type = &record_assoc->default_type;
    } else if (assoc->default_type) {
      binding_type = &assoc->default_type;
    } else {
      SPEC_RULE("Impl-AssocType-Missing");
      diag_id = "E-TYP-2503";
      return false;
    }

    const auto lowered = LowerTypeWithWF(ctx, *binding_type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return false;
    }
    out_subst[assoc->name] = SubstSelfType(self_type, lowered.type, &out_subst);
  }
  return true;
}

static bool HasDropMethod(const std::vector<const ast::MethodDecl*>& methods) {
  for (const auto* method : methods) {
    if (method && method->name == "drop") {
      return true;
    }
  }
  return false;
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

// Check field visibility doesn't exceed record visibility
static bool FieldVisOk(const ast::RecordDecl& record) {
  for (const auto& member : record.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field) {
      continue;
    }
    if (VisRank(field->vis) > VisRank(record.vis)) {
      return false;
    }
  }
  return true;
}

// Check if record is default constructible (all fields have defaults)
static bool IsDefaultConstructible(const std::vector<ast::FieldDecl>& fields) {
  for (const auto& field : fields) {
    if (!field.init_opt) {
      return false;
    }
  }
  return true;
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

// Check for conflicting implementations (Bitcopy + Drop)
static bool CheckImplConflicts(const std::vector<ast::ClassPath>& impls,
                               std::optional<std::string_view>& diag_id) {
  auto has_impl = [&](std::string_view name) {
    for (const auto& impl : impls) {
      if (impl.size() == 1 && impl[0] == name) {
        return true;
      }
    }
    return false;
  };

  const bool has_bitcopy = has_impl("Bitcopy");
  const bool has_drop = has_impl("Drop");
  const bool has_clone = has_impl("Clone");

  if (has_bitcopy && has_drop) {
    diag_id = "E-TYP-2621";
    return false;
  }

  if (has_bitcopy && !has_clone) {
    diag_id = "E-TYP-2503";
    return false;
  }

  return true;
}

static void EmitTypecheckDiag(core::DiagnosticStream& diags,
                              std::string_view diag_id,
                              const std::optional<core::Span>& span,
                              const std::string& detail = {}) {
  EmitResolvedTypecheckDiagnostic(diags, diag_id, span, detail);
}

static bool ParseU64Literal(const ast::Token& tok, std::uint64_t& out) {
  if (tok.kind != lexer::TokenKind::IntLiteral) {
    return false;
  }
  std::string text = tok.lexeme;
  text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
  if (text.empty()) {
    return false;
  }
  std::size_t consumed = 0;
  try {
    const auto parsed = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

static std::optional<std::pair<std::uint64_t, core::Span>> RecordRequestedAlign(
    const ast::AttributeList& attrs_list) {
  for (const auto& attr : attrs_list) {
    if (attr.name == attrs::kLayout) {
      for (const auto& arg : attr.args) {
        if (!arg.key.has_value() || *arg.key != "align") {
          continue;
        }
        const auto* nested = std::get_if<std::vector<ast::AttributeArg>>(&arg.value);
        if (!nested || nested->size() != 1) {
          continue;
        }
        const auto* token = std::get_if<ast::Token>(&(*nested)[0].value);
        std::uint64_t parsed = 0;
        if (!token || !ParseU64Literal(*token, parsed)) {
          continue;
        }
        return std::make_pair(parsed, attr.span);
      }
    }
    if (attr.name == attrs::kAlign && !attr.args.empty()) {
      const auto* token = std::get_if<ast::Token>(&attr.args[0].value);
      std::uint64_t parsed = 0;
      if (!token || !ParseU64Literal(*token, parsed)) {
        continue;
      }
      return std::make_pair(parsed, attr.span);
    }
  }
  return std::nullopt;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeRecordDecl
// =============================================================================

RecordDeclResult TypeRecordDecl(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsRecordDecl();
  RecordDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Record);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Build type path for this record
  TypePath type_path;
  for (const auto& seg : module_path) {
    type_path.push_back(seg);
  }
  type_path.push_back(decl.name);
  result.self_type = MakeTypePath(type_path);

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }

  // Process where clauses
  std::vector<std::string> type_param_names;
  for (const auto& gp : gen_params.params) {
    type_param_names.push_back(gp.name);
  }
  if (decl.predicate_clause_opt.has_value()) {
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
    result.diag_id = "Impl-Dup";
    return result;
  }

  // Check for impl conflicts (Bitcopy + Drop)
  std::optional<std::string_view> impl_diag;
  if (!CheckImplConflicts(decl.implements, impl_diag)) {
    SPEC_RULE("BitcopyDrop-Conflict");
    result.ok = false;
    result.diag_id = impl_diag;
    return result;
  }

  // Check field visibility
  if (!FieldVisOk(decl)) {
    SPEC_RULE("FieldVisOk-Err");
    result.ok = false;
    result.diag_id = "FieldVisOk-Err";
    return result;
  }

  // Get and check fields
  const auto fields = GetFields(decl);
  const auto methods = GetMethods(decl);

  // Check field names are distinct
  if (!DistinctFieldNames(fields)) {
    SPEC_RULE("WF-Record-DupField");
    result.ok = false;
    result.diag_id = "WF-Record-DupField";
    return result;
  }

  if (!DistinctMethodNames(methods)) {
    SPEC_RULE("Record-Method-Dup");
    result.ok = false;
    result.diag_id = "Record-Method-Dup";
    return result;
  }

  TypeSubst record_assoc_subst;
  if (!CollectRecordAssociatedTypeBindings(ctx, decl, result.self_type,
                                           record_assoc_subst,
                                           result.diag_id)) {
    result.ok = false;
    return result;
  }

  // Process each field
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

    // Substitute Self type
    const auto field_type =
        SubstSelfType(result.self_type, lowered.type, &record_assoc_subst);
    result.fields.push_back({field.name, field_type});

    // Check field initializer if present
    if (field.init_opt) {
      TypeEnv env;
      env.scopes.emplace_back();
      StmtTypeContext type_ctx;
      type_ctx.return_type = MakeTypePrim("()");
      const auto init_result = TypeExpr(ctx, type_ctx, field.init_opt, env);
      if (!init_result.ok) {
        result.ok = false;
        result.diag_id = init_result.diag_id;
        return result;
      }

      // Check initializer type matches field type
      const auto sub = Subtyping(ctx, init_result.type, field_type);
      if (!sub.ok) {
        result.ok = false;
        result.diag_id = sub.diag_id;
        return result;
      }
      if (!sub.subtype) {
        SPEC_RULE("InitOk-Err");
        result.ok = false;
        result.diag_id = "E-MOD-2402";
        return result;
      }
    }
  }

  if (const auto requested_align = RecordRequestedAlign(decl.attrs);
      requested_align.has_value()) {
    std::uint64_t natural_align = 1;
    bool natural_align_known = true;
    for (const auto& field_info : result.fields) {
      const auto field_align = AlignOf(ctx, field_info.type);
      if (!field_align.has_value()) {
        natural_align_known = false;
        break;
      }
      natural_align = std::max(natural_align, *field_align);
    }
    if (natural_align_known && requested_align->first < natural_align) {
      EmitTypecheckDiag(diags, "W-MOD-2451",
                        std::optional<core::Span>(requested_align->second));
    }
  }

  // Check if Bitcopy but fields are not Bitcopy
  auto has_impl = [&](std::string_view name) {
    for (const auto& impl : decl.implements) {
      if (impl.size() == 1 && impl[0] == name) {
        return true;
      }
    }
    return false;
  };

  if (has_impl("Bitcopy")) {
    for (const auto& field_info : result.fields) {
      if (!BitcopyType(ctx, field_info.type)) {
        SPEC_RULE("Bitcopy-Field-NonBitcopy");
        result.ok = false;
        result.diag_id = "Bitcopy-Field-NonBitcopy";
        return result;
      }
    }
  }

  if (HasDropMethod(methods)) {
    bool bitcopy_fields = true;
    for (const auto& field_info : result.fields) {
      if (!BitcopyType(ctx, field_info.type)) {
        bitcopy_fields = false;
        break;
      }
    }
    if (bitcopy_fields) {
      SPEC_RULE("BitcopyDrop-Conflict");
      result.ok = false;
      result.diag_id = "E-TYP-2621";
      return result;
    }
  }

  // Process type invariant if present
  if (decl.invariant_opt.has_value()) {
    // Type invariant cannot have public mutable fields
    // Note: In Cursive, fields don't have is_const - all record fields are mutable by default
    // unless the containing binding is immutable
    for (const auto& field : fields) {
      if (field.vis == ast::Visibility::Public) {
        SPEC_RULE("TypeInvariant-PublicMut-Err");
        result.ok = false;
        result.diag_id = "E-SEM-2824";
        return result;
      }
    }

    // Build contract context for type invariant check
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
  std::unordered_set<IdKey> concrete_class_methods;
  std::unordered_set<IdKey> inherited_dynamic_methods;
  for (const auto& impl_path : decl.implements) {
    const auto class_key = PathKeyOf(impl_path);
    const auto class_it = ctx.sigma.classes.find(class_key);
    if (class_it == ctx.sigma.classes.end()) {
      SPEC_RULE("Superclass-Undefined");
      result.ok = false;
      result.diag_id = "Superclass-Undefined";
      return result;
    }
    if (IsModalClass(class_it->second)) {
      SPEC_RULE("T-Modal-Class");
      result.ok = false;
      result.diag_id = "E-TYP-2401";
      return result;
    }
    const auto& class_decl = class_it->second;
    TypeSubst class_assoc_subst;
    if (!BuildClassAssociatedTypeBindings(ctx, decl, class_decl, result.self_type,
                                          record_assoc_subst, class_assoc_subst,
                                          result.diag_id)) {
      result.ok = false;
      return result;
    }

    // Verify all required methods are implemented
    const auto method_table = ClassMethodTable(ctx, impl_path);
    if (!method_table.ok) {
      result.ok = false;
      result.diag_id = method_table.diag_id;
      return result;
    }

    const auto field_table = ClassFieldTable(ctx, impl_path);
    if (!field_table.ok) {
      result.ok = false;
      result.diag_id = field_table.diag_id;
      return result;
    }

    auto find_record_method = [&](std::string_view name) -> const ast::MethodDecl* {
      for (const auto* method : methods) {
        if (method && IdEq(method->name, name)) {
          return method;
        }
      }
      return nullptr;
    };

    auto find_record_field = [&](std::string_view name) -> const ast::FieldDecl* {
      for (const auto& field : fields) {
        if (IdEq(field.name, name)) {
          return &field;
        }
      }
      return nullptr;
    };

    // Class-implementation field checks: every effective class field must be
    // present on the implementing record with an equivalent type.
    for (const auto* class_field : field_table.fields) {
      if (!class_field) {
        continue;
      }
      const auto* impl_field = find_record_field(class_field->name);
      if (!impl_field) {
        SPEC_RULE("Impl-Field-Missing");
        result.ok = false;
        result.diag_id = "Impl-Field-Missing";
        return result;
      }

      const auto class_field_type = LowerTypeWithWF(ctx, class_field->type);
      if (!class_field_type.ok) {
        result.ok = false;
        result.diag_id = class_field_type.diag_id;
        return result;
      }
      const auto impl_field_type = FieldType(decl, impl_field->name, ctx);
      if (!impl_field_type.has_value()) {
        result.ok = false;
        result.diag_id = "Impl-Field-Type-Err";
        return result;
      }
      const auto class_field_subst =
          SubstSelfType(result.self_type, class_field_type.type,
                        &class_assoc_subst);
      const auto impl_field_subst =
          SubstSelfType(result.self_type, *impl_field_type,
                        &class_assoc_subst);
      const auto field_equiv =
          TypeEquiv(class_field_subst, impl_field_subst);
      if (!field_equiv.ok || !field_equiv.equiv) {
        SPEC_RULE("Impl-Field-Type-Err");
        result.ok = false;
        result.diag_id = "Impl-Field-Type-Err";
        return result;
      }
    }

    for (const auto& entry : method_table.methods) {
      if (!entry.method) {
        continue;
      }
      if (entry.method->body_opt) {
        concrete_class_methods.insert(IdKeyOf(entry.method->name));
      }
      const auto* impl_method = find_record_method(entry.method->name);

      // Abstract class method: implementation is required, but `override` is invalid.
      if (!entry.method->body_opt) {
        if (!impl_method) {
          SPEC_RULE("Impl-Missing-Method");
          result.ok = false;
          result.diag_id = "E-TYP-2503";
          return result;
        }
        if (impl_method->override_flag) {
          SPEC_RULE("Override-Abstract-Err");
          result.ok = false;
          result.diag_id = "Override-Abstract-Err";
          return result;
        }
      } else {
        // Concrete class method: replacement is optional; if present it must
        // be marked `override`.
        if (impl_method && !impl_method->override_flag) {
          SPEC_RULE("Override-Missing-Err");
          result.ok = false;
          result.diag_id = "Override-Missing-Err";
          return result;
        }
      }

      if (impl_method) {
        if (ResolveVerificationModeAttribute(entry.method->attrs) ==
            VerificationModeAttribute::Dynamic) {
          inherited_dynamic_methods.insert(IdKeyOf(impl_method->name));
        }
        const auto class_sig = BuildMethodSignature(
            ctx, result.self_type, entry.method->receiver,
            entry.method->params, entry.method->return_type_opt,
            &class_assoc_subst);
        const auto impl_sig = BuildMethodSignature(
            ctx, result.self_type, impl_method->receiver,
            impl_method->params, impl_method->return_type_opt,
            &class_assoc_subst);
        if (!class_sig.ok || !impl_sig.ok) {
          result.ok = false;
          result.diag_id = class_sig.ok ? impl_sig.diag_id : class_sig.diag_id;
          return result;
        }
        const auto equiv = TypeEquiv(class_sig.func_type, impl_sig.func_type);
        if (!equiv.ok || !equiv.equiv) {
          SPEC_RULE("Impl-Sig-Err");
          result.ok = false;
          result.diag_id = "E-TYP-2503";
          return result;
        }

        ast::ContractClause empty_class_contract;
        empty_class_contract.span = entry.method->span;
        const ast::ContractClause& class_contract =
            entry.method->contract.has_value()
                ? *entry.method->contract
                : empty_class_contract;

        ast::ContractClause empty_impl_contract;
        empty_impl_contract.span = impl_method->span;
        const ast::ContractClause& impl_contract =
            impl_method->contract.has_value()
                ? *impl_method->contract
                : empty_impl_contract;

        const auto behavioral =
            CheckBehavioralSubtyping(class_contract, impl_contract);
        if (!behavioral.ok) {
          SPEC_RULE("LSP");
          result.ok = false;
          result.diag_id = behavioral.diag_id;
          return result;
        }
      }
    }
  }

  // `override` must target an existing concrete method in at least one
  // implemented class (across the full implements set, not per class).
  for (const auto* method : methods) {
    if (!method || !method->override_flag) {
      continue;
    }
    if (concrete_class_methods.find(IdKeyOf(method->name)) ==
        concrete_class_methods.end()) {
      SPEC_RULE("Override-NoConcrete");
      result.ok = false;
      result.diag_id = "Override-NoConcrete";
      return result;
    }
  }

  // Process methods
  for (const auto* method : methods) {
    SPEC_RULE("WF-Record-Method");
    const auto method_attr_validation =
        ValidateAttributes(method->attrs, AttributeTarget::Method);
    if (!method_attr_validation.ok) {
      result.ok = false;
      result.diag_id = method_attr_validation.diag_id;
      return result;
    }

    if (!DistinctParamNames(method->params)) {
      SPEC_RULE("ParamBinds-Duplicate-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2713";
      return result;
    }
    if (HasAttribute(method->attrs, attrs::kStatic)) {
      result.ok = false;
      result.diag_id = "E-MOD-2452";
      return result;
    }
    if (HasReservedSelfParam(method->params)) {
      SPEC_RULE("Method-Context-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3011";
      return result;
    }

    // Build self parameter for borrow checking
    // Note: In Cursive, Receiver = variant<ReceiverShorthand, ReceiverExplicit>
    // §4.2: Pass receiver permission so borrow checker can set mutability
    // (~! = Var, ~/%/default = Let)
    std::optional<Permission> recv_perm;
    bool const_receiver = false;
    if (const auto* shorthand = std::get_if<ast::ReceiverShorthand>(&method->receiver)) {
      if (shorthand->perm == ast::ReceiverPerm::Unique) {
        recv_perm = Permission::Unique;
      } else if (shorthand->perm == ast::ReceiverPerm::Shared) {
        recv_perm = Permission::Shared;
      } else {
        recv_perm = Permission::Const;
        const_receiver = true;
      }
    }
    std::optional<BindSelfParam> self_param = BindSelfParam{result.self_type, std::nullopt, recv_perm};

    // Build method signature
    const auto sig = BuildMethodSignature(
        ctx, result.self_type, method->receiver,
        method->params, method->return_type_opt, &record_assoc_subst);
    if (!sig.ok) {
      result.ok = false;
      result.diag_id = sig.diag_id;
      return result;
    }

    if (method->contract.has_value()) {
      ContractContext contract_ctx;
      contract_ctx.scope_ctx = &ctx;
      contract_ctx.receiver_type = result.self_type;
      contract_ctx.return_type = sig.return_type;
      for (const auto& binding : sig.bindings) {
        if (binding.first == "self") {
          continue;
        }
        contract_ctx.params[binding.first] = binding.second;
      }
      const auto contract_check =
          CheckContractWellFormed(contract_ctx, *method->contract);
      if (!contract_check.ok) {
        result.ok = false;
        result.diag_id = contract_check.diag_id;
        return result;
      }
    }

    // Type method body if present
    if (method->body) {
      const bool is_unit = TypeEquiv(sig.return_type, MakeTypePrim("()")).equiv;
      if (!is_unit && !HasExplicitReturn(*method->body)) {
        SPEC_RULE("T-Record-Method-Body");
        SPEC_RULE("WF-ProcBody-ExplicitReturn-Err");
        result.ok = false;
        result.diag_id = "E-TYP-1507";
        return result;
      }

      TypeEnv env;
      env.scopes.emplace_back();
      for (const auto& binding : sig.bindings) {
        ast::Mutability binding_mut = ast::Mutability::Let;
        if (binding.first == "self") {
          if (const auto* sh = std::get_if<ast::ReceiverShorthand>(&method->receiver)) {
            if (sh->perm == ast::ReceiverPerm::Unique) {
              binding_mut = ast::Mutability::Var;
            }
          }
        }
        env.scopes.back()[IdKeyOf(binding.first)] = {
            binding_mut, binding.second
        };
      }

      // Build StmtTypeContext for body typing
      StmtTypeContext type_ctx;
      type_ctx.return_type = sig.return_type;
      type_ctx.diags = &diags;
      type_ctx.env_ref = &env;
      const bool inherited_dynamic =
          inherited_dynamic_methods.contains(IdKeyOf(method->name));
      const std::array<DynamicScopeAncestor, 2> ancestors{
          MakeDynamicScopeAncestor(decl.attrs, decl.span),
          MakeDynamicScopeAncestor(method->attrs, method->span)};
      type_ctx.contract_dynamic =
          inherited_dynamic ||
          ComputeDynamicContext(method->body->span, ancestors);
      if (method->contract.has_value()) {
        type_ctx.contract = &*method->contract;
      }
      if (decl.invariant_opt.has_value() && const_receiver) {
        type_ctx.proof_ctx =
            ExtendProofContextWithPredicateAt(type_ctx.proof_ctx,
                                              decl.invariant_opt->predicate,
                                              method->body->span);
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
          ctx, type_ctx, *method->body, env, type_expr, type_ident, type_place, &env);
      if (!body_result.ok) {
        result.ok = false;
        result.diag_id = body_result.diag_id;
        return result;
      }

      // Borrow check
      const auto bind_result = BindCheckBody(
          ctx, module_path, method->params, method->body, self_param);
      if (!bind_result.ok) {
        result.ok = false;
        result.diag_id = bind_result.diag_id;
        return result;
      }

      SPEC_RULE("T-Record-Method-Body");
    }

    result.methods.push_back({method->name, sig.func_type});
  }

  SPEC_RULE("WF-Record-Methods");

  result.default_constructible = IsDefaultConstructible(fields);

  SPEC_RULE("WF-Record-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeRecordDeclSignature (first pass)
// =============================================================================

RecordDeclResult TypeRecordDeclSignature(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsRecordDecl();
  RecordDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Record);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Build type path
  TypePath type_path;
  for (const auto& seg : module_path) {
    type_path.push_back(seg);
  }
  type_path.push_back(decl.name);
  result.self_type = MakeTypePath(type_path);

  // Process generic parameters
  const auto gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }

  // Check field names distinct
  const auto fields = GetFields(decl);
  const auto methods = GetMethods(decl);
  if (!DistinctFieldNames(fields)) {
    SPEC_RULE("WF-Record-DupField");
    result.ok = false;
    result.diag_id = "WF-Record-DupField";
    return result;
  }

  if (!DistinctMethodNames(methods)) {
    SPEC_RULE("Record-Method-Dup");
    result.ok = false;
    result.diag_id = "Record-Method-Dup";
    return result;
  }

  TypeSubst record_assoc_subst;
  if (!CollectRecordAssociatedTypeBindings(ctx, decl, result.self_type,
                                           record_assoc_subst,
                                           result.diag_id)) {
    result.ok = false;
    return result;
  }

  // Process fields
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
    const auto field_type =
        SubstSelfType(result.self_type, lowered.type, &record_assoc_subst);
    result.fields.push_back({field.name, field_type});
  }

  // Process method signatures
  for (const auto* method : methods) {
    SPEC_RULE("WF-Record-Method");
    const auto method_attr_validation =
        ValidateAttributes(method->attrs, AttributeTarget::Method);
    if (!method_attr_validation.ok) {
      result.ok = false;
      result.diag_id = method_attr_validation.diag_id;
      return result;
    }

    if (!DistinctParamNames(method->params)) {
      SPEC_RULE("ParamBinds-Duplicate-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2713";
      return result;
    }
    if (HasReservedSelfParam(method->params)) {
      SPEC_RULE("Method-Context-Err");
      result.ok = false;
      result.diag_id = "E-SEM-3011";
      return result;
    }

    const auto sig = BuildMethodSignature(
        ctx, result.self_type, method->receiver,
        method->params, method->return_type_opt, &record_assoc_subst);
    if (!sig.ok) {
      result.ok = false;
      result.diag_id = sig.diag_id;
      return result;
    }
    result.methods.push_back({method->name, sig.func_type});
  }

  SPEC_RULE("WF-Record-Methods");

  result.default_constructible = IsDefaultConstructible(fields);

  SPEC_RULE("WF-Record-Sig-Ok");
  return result;
}

}  // namespace cursive::analysis
