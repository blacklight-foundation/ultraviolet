// =============================================================================
// MIGRATION: item/enum_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.3.3: Enum Declarations
//   - WF-EnumDecl (line 10540): Enum well-formedness
//   - enum_decl grammar (line 3102)
//   - Explicit discriminants (line 13948)
//   - Type invariants on enums (line 23390)
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/enums.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsEnumDecl() {
  SPEC_DEF("WF-EnumDecl", "5.2.14");
  SPEC_DEF("WF-Variant", "5.2.14");
  SPEC_DEF("Discriminant", "5.2.14");
  SPEC_DEF("TypeInvariant", "5.2.14");
  SPEC_DEF("Impl-Ok", "5.2.14");
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

// Check if variant names are distinct
static bool DistinctVariantNames(const std::vector<ast::VariantDecl>& variants) {
  if (variants.size() < 2) {
    return true;
  }
  std::unordered_set<std::string> names;
  for (const auto& variant : variants) {
    if (!names.insert(variant.name).second) {
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

  if (has_bitcopy && has_drop) {
    diag_id = "E-TYP-2621";
    return false;
  }

  return true;
}

static std::string NormalizeAttrLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

static std::optional<std::string_view> ValidateEnumLayoutAttributes(
    const ast::EnumDecl& decl) {
  if (const auto layout_kind = GetAttributeValue(decl.attrs, attrs::kLayout)) {
    if (NormalizeAttrLiteral(*layout_kind) == "packed") {
      return "Attr-Packed-NonRecord";
    }
  }
  return std::nullopt;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeEnumDecl
// =============================================================================

EnumDeclResult TypeEnumDecl(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsEnumDecl();
  EnumDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Enum);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }
  if (const auto layout_diag = ValidateEnumLayoutAttributes(decl)) {
    result.ok = false;
    result.diag_id = *layout_diag;
    return result;
  }

  // Build type path for this enum
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
    result.diag_id = "E-TYP-2506";
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

  if (decl.variants.empty()) {
    SPEC_RULE("Enum-Empty-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2001";
    return result;
  }

  // Check variant names are distinct
  if (!DistinctVariantNames(decl.variants)) {
    SPEC_RULE("WF-EnumDecl-DupVariant");
    result.ok = false;
    result.diag_id = "E-TYP-2505";
    return result;
  }

  const auto enum_discriminants = EnumDiscriminants(decl);
  if (!enum_discriminants.ok) {
    result.ok = false;
    result.diag_id = enum_discriminants.diag_id;
    return result;
  }
  if (enum_discriminants.discs.size() != decl.variants.size()) {
    SPEC_RULE("Enum-Disc-Invalid");
    result.ok = false;
    result.diag_id = "E-TYP-1921";
    return result;
  }

  // Process variants
  for (std::size_t i = 0; i < decl.variants.size(); ++i) {
    const auto& variant = decl.variants[i];
    VariantInfo var_info;
    var_info.name = variant.name;
    var_info.discriminant = enum_discriminants.discs[i];

    // Process payload types
    if (variant.payload_opt.has_value()) {
      std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, ast::VariantPayloadTuple>) {
          for (const auto& payload_type : payload.elements) {
            const auto lowered = LowerTypeWithWF(ctx, payload_type);
            if (!lowered.ok) {
              result.ok = false;
              result.diag_id = lowered.diag_id;
              return;
            }
            const auto payload_subst = SubstSelfType(result.self_type, lowered.type);
            var_info.payload.push_back(payload_subst);
          }
        } else if constexpr (std::is_same_v<T, ast::VariantPayloadRecord>) {
          for (const auto& field : payload.fields) {
            if (HasAttribute(field.attrs, attrs::kDynamic)) {
              result.ok = false;
              result.diag_id = "E-CON-0412";
              return;
            }
            const auto field_attr_validation =
                ValidateAttributes(field.attrs, AttributeTarget::Field);
            if (!field_attr_validation.ok) {
              result.ok = false;
              result.diag_id = field_attr_validation.diag_id;
              return;
            }
            const auto lowered = LowerTypeWithWF(ctx, field.type);
            if (!lowered.ok) {
              result.ok = false;
              result.diag_id = lowered.diag_id;
              return;
            }
            const auto payload_subst = SubstSelfType(result.self_type, lowered.type);
            var_info.payload.push_back(payload_subst);
          }
        }
      }, *variant.payload_opt);
      if (!result.ok) {
        return result;
      }
    }

    result.variants.push_back(var_info);
  }

  // Check if Bitcopy but payloads are not Bitcopy
  auto has_impl = [&](std::string_view name) {
    for (const auto& impl : decl.implements) {
      if (impl.size() == 1 && impl[0] == name) {
        return true;
      }
    }
    return false;
  };

  if (has_impl("Bitcopy")) {
    for (const auto& var_info : result.variants) {
      for (const auto& payload : var_info.payload) {
        if (!BitcopyType(ctx, payload)) {
          SPEC_RULE("Bitcopy-Payload-NonBitcopy");
          result.ok = false;
          result.diag_id = "E-TYP-2622";
          return result;
        }
      }
    }
  }

  // Process type invariant if present
  if (decl.invariant_opt.has_value()) {
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

  // Check class implementations exist
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

    // Enums cannot declare class methods; they may only implement classes
    // whose required methods all have concrete defaults.
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
    if (!field_table.fields.empty()) {
      SPEC_RULE("Impl-Field-Missing");
      result.ok = false;
      result.diag_id = "Impl-Field-Missing";
      return result;
    }

    for (const auto& entry : method_table.methods) {
      if (!entry.method) {
        continue;
      }
      if (!entry.method->body_opt) {
        SPEC_RULE("Impl-Missing-Method");
        result.ok = false;
        result.diag_id = "E-TYP-2503";
        return result;
      }
    }
  }

  SPEC_RULE("WF-EnumDecl-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeEnumDeclSignature (first pass)
// =============================================================================

EnumDeclResult TypeEnumDeclSignature(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsEnumDecl();
  EnumDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::Enum);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }
  if (const auto layout_diag = ValidateEnumLayoutAttributes(decl)) {
    result.ok = false;
    result.diag_id = *layout_diag;
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

  if (decl.variants.empty()) {
    SPEC_RULE("Enum-Empty-Err");
    result.ok = false;
    result.diag_id = "E-TYP-2001";
    return result;
  }

  // Check variant names are distinct
  if (!DistinctVariantNames(decl.variants)) {
    SPEC_RULE("WF-EnumDecl-DupVariant");
    result.ok = false;
    result.diag_id = "E-TYP-2505";
    return result;
  }

  const auto enum_discriminants = EnumDiscriminants(decl);
  if (!enum_discriminants.ok) {
    result.ok = false;
    result.diag_id = enum_discriminants.diag_id;
    return result;
  }
  if (enum_discriminants.discs.size() != decl.variants.size()) {
    SPEC_RULE("Enum-Disc-Invalid");
    result.ok = false;
    result.diag_id = "E-TYP-1921";
    return result;
  }

  // Process variants (signature only)
  for (std::size_t i = 0; i < decl.variants.size(); ++i) {
    const auto& variant = decl.variants[i];
    VariantInfo var_info;
    var_info.name = variant.name;
    var_info.discriminant = enum_discriminants.discs[i];

    if (variant.payload_opt.has_value()) {
      std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, ast::VariantPayloadTuple>) {
          for (const auto& payload_type : payload.elements) {
            const auto lowered = LowerTypeWithWF(ctx, payload_type);
            if (!lowered.ok) {
              result.ok = false;
              result.diag_id = lowered.diag_id;
              return;
            }
            const auto payload_subst = SubstSelfType(result.self_type, lowered.type);
            var_info.payload.push_back(payload_subst);
          }
        } else if constexpr (std::is_same_v<T, ast::VariantPayloadRecord>) {
          for (const auto& field : payload.fields) {
            if (HasAttribute(field.attrs, attrs::kDynamic)) {
              result.ok = false;
              result.diag_id = "E-CON-0412";
              return;
            }
            const auto field_attr_validation =
                ValidateAttributes(field.attrs, AttributeTarget::Field);
            if (!field_attr_validation.ok) {
              result.ok = false;
              result.diag_id = field_attr_validation.diag_id;
              return;
            }
            const auto lowered = LowerTypeWithWF(ctx, field.type);
            if (!lowered.ok) {
              result.ok = false;
              result.diag_id = lowered.diag_id;
              return;
            }
            const auto payload_subst = SubstSelfType(result.self_type, lowered.type);
            var_info.payload.push_back(payload_subst);
          }
        }
      }, *variant.payload_opt);
      if (!result.ok) {
        return result;
      }
    }

    result.variants.push_back(var_info);
  }

  SPEC_RULE("WF-EnumDecl-Sig-Ok");
  return result;
}

}  // namespace cursive::analysis
