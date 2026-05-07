// ===========================================================================
// attribute_targets.cpp - Attribute target determination and validation
// ===========================================================================
//
// SPEC REFERENCE:
//   - CursiveSpecification.md, Section 5.13 "Attributes" (line 13859)
//   - CursiveSpecification.md, AttrTargets definitions (lines 13870-13894)
//
// SOURCE FILE:
//   - cursive-bootstrap/src/03_analysis/attributes/attribute_registry.cpp
//
// ===========================================================================

#include "02_source/attributes/attribute_registry.h"

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsAttributeTargets() {
  SPEC_DEF("AttrTargets", "C0.5.13");
  SPEC_DEF("AttrTargetValidation", "C0.5.13.3");
}

std::optional<AttributeTarget> GetItemTarget(const ast::ASTItem& item) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Item");

  return std::visit(
      [](const auto& node) -> std::optional<AttributeTarget> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
          return AttributeTarget::Procedure;
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          return AttributeTarget::Record;
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          return AttributeTarget::Enum;
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          return AttributeTarget::Modal;
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          return AttributeTarget::TypeAlias;
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          return AttributeTarget::ExternBlock;
        } else {
          return std::nullopt;
        }
      },
      item);
}

std::string_view GetUnsupportedItemTargetName(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::string_view {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          return "import declarations";
        } else if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          return "using declarations";
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          return "class declarations";
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          return "static declarations";
        } else {
          return "this declaration";
        }
      },
      item);
}

}  // namespace

// Get the attribute target kind for a field declaration
AttributeTarget GetFieldTarget() {
  return AttributeTarget::Field;
}

// Get the attribute target kind for a method declaration
AttributeTarget GetMethodTarget() {
  return AttributeTarget::Method;
}

// Get the attribute target kind for a binding (let/var statement)
AttributeTarget GetBindingTarget() {
  return AttributeTarget::Binding;
}

// Get the attribute target kind for a statement
AttributeTarget GetStatementTarget() {
  return AttributeTarget::Statement;
}

// Get the attribute target kind for an expression
AttributeTarget GetExpressionTarget() {
  return AttributeTarget::Expression;
}

// Get the attribute target kind for a key block
AttributeTarget GetKeyBlockTarget() {
  return AttributeTarget::KeyBlock;
}

// Convert AttributeTarget to human-readable string
std::string_view GetTargetKindName(AttributeTarget target) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Name");

  switch (target) {
    case AttributeTarget::Procedure:
      return "procedure";
    case AttributeTarget::ExternBlock:
      return "extern block";
    case AttributeTarget::Record:
      return "record";
    case AttributeTarget::Enum:
      return "enum";
    case AttributeTarget::Modal:
      return "modal";
    case AttributeTarget::Field:
      return "field";
    case AttributeTarget::Method:
      return "method";
    case AttributeTarget::TypeAlias:
      return "type alias";
    case AttributeTarget::Binding:
      return "binding";
    case AttributeTarget::Statement:
      return "statement";
    case AttributeTarget::Expression:
      return "expression";
    case AttributeTarget::KeyBlock:
      return "key block";
  }
  return "unknown";
}

bool IsOnlyTargetAttribute(std::string_view name, AttributeTarget target) {
  const auto* spec = GetAttributeRegistry().Lookup(name);
  if (!spec) {
    return false;
  }
  if (spec->valid_targets.size() != 1) {
    return false;
  }
  return *spec->valid_targets.begin() == target;
}

// Check if the attribute is a procedure-only attribute.
bool IsProcedureOnlyAttribute(std::string_view name) {
  return IsOnlyTargetAttribute(name, AttributeTarget::Procedure);
}

// Check if the attribute is a type-only attribute (record/enum).
bool IsTypeOnlyAttribute(std::string_view name) {
  const auto* spec = GetAttributeRegistry().Lookup(name);
  if (!spec) {
    return false;
  }
  if (spec->valid_targets.empty() || spec->valid_targets.size() > 2) {
    return false;
  }
  bool saw_record = false;
  bool saw_enum = false;
  for (const auto target : spec->valid_targets) {
    if (target == AttributeTarget::Record) {
      saw_record = true;
      continue;
    }
    if (target == AttributeTarget::Enum) {
      saw_enum = true;
      continue;
    }
    return false;
  }
  return saw_record || saw_enum;
}

// Check if the attribute is a key-block-only attribute.
bool IsKeyBlockOnlyAttribute(std::string_view name) {
  return IsOnlyTargetAttribute(name, AttributeTarget::KeyBlock);
}

// Check if the attribute is an expression-only attribute.
bool IsExpressionOnlyAttribute(std::string_view name) {
  return IsOnlyTargetAttribute(name, AttributeTarget::Expression);
}

// Validate that an attribute is valid on a procedure
AttributeValidationResult ValidateProcedureAttribute(
    const ast::AttributeItem& attr,
    const ast::ProcedureDecl& proc) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Procedure");

  AttributeValidationResult result;

  // Check if attribute is valid on procedures
  if (!GetAttributeRegistry().IsValidForTarget(attr.name,
                                               AttributeTarget::Procedure)) {
    result.ok = false;
    result.diag_id = "E-MOD-2452";  // Attr-Target-Err
    result.span = attr.span;
    result.message = "Attribute '" + attr.name + "' is not valid on procedures";
    return result;
  }

  return result;
}

// Validate that an attribute is valid on a record or enum
AttributeValidationResult ValidateTypeAttribute(const ast::AttributeItem& attr,
                                                AttributeTarget target) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Type");

  AttributeValidationResult result;

  if (!GetAttributeRegistry().IsValidForTarget(attr.name, target)) {
    result.ok = false;
    result.diag_id = "E-MOD-2452";  // Attr-Target-Err
    result.span = attr.span;
    result.message =
        "Attribute '" + attr.name + "' is not valid on " +
        std::string(GetTargetKindName(target));
    return result;
  }

  return result;
}

// Validate that an attribute is valid on a field
AttributeValidationResult ValidateFieldAttribute(const ast::AttributeItem& attr) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Field");

  AttributeValidationResult result;

  if (!GetAttributeRegistry().IsValidForTarget(attr.name,
                                               AttributeTarget::Field)) {
    result.ok = false;
    result.diag_id = "E-MOD-2452";  // Attr-Target-Err
    result.span = attr.span;
    result.message = "Attribute '" + attr.name + "' is not valid on fields";
    return result;
  }

  return result;
}

// Validate that an attribute is valid on a method
AttributeValidationResult ValidateMethodAttribute(const ast::AttributeItem& attr) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Method");

  AttributeValidationResult result;

  if (!GetAttributeRegistry().IsValidForTarget(attr.name,
                                               AttributeTarget::Method)) {
    result.ok = false;
    result.diag_id = "E-MOD-2452";  // Attr-Target-Err
    result.span = attr.span;
    result.message = "Attribute '" + attr.name + "' is not valid on methods";
    return result;
  }

  return result;
}

// Validate all attributes on an item for target compatibility
AttributeValidationResult ValidateItemAttributeTargets(
    const ast::AttributeList& attrs,
    const ast::ASTItem& item) {
  SpecDefsAttributeTargets();
  SPEC_RULE("AttrTargets-Item-Validate");

  const auto target = GetItemTarget(item);
  if (!target.has_value()) {
    return ValidateUnsupportedAttributeTarget(
        attrs, GetUnsupportedItemTargetName(item));
  }

  for (const auto& attr : attrs) {
    if (!GetAttributeRegistry().IsValidForTarget(attr.name, *target)) {
      AttributeValidationResult result;
      result.ok = false;
      result.diag_id = "E-MOD-2452";  // Attr-Target-Err
      result.span = attr.span;
      result.message =
          "Attribute '" + attr.name + "' is not valid on " +
          std::string(GetTargetKindName(*target));
      return result;
    }
  }

  AttributeValidationResult result;
  return result;
}

}  // namespace cursive::analysis
