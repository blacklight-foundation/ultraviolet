// =============================================================================
// MIGRATION: item/attribute_list.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section: Attributes
//   - Attribute syntax: [[attr]], [[attr(args)]]
//   - Complete attribute list in spec
//   - Attribute validation
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/attributes/attribute_registry.cpp
//
// =============================================================================

#include "02_source/attributes/attribute_registry.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

using ast::Token;

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsAttributeList() {
  SPEC_DEF("Attr-Syntax", "5.3.16");
  SPEC_DEF("Attr-Known", "5.3.16");
  SPEC_DEF("Attr-Target", "5.3.16");
  SPEC_DEF("Attr-Args", "5.3.16");
  SPEC_DEF("Attr-Dup", "5.3.16");
  SPEC_DEF("Attr-Conflict", "5.3.16");
}

// =============================================================================
// ATTRIBUTE NAMES
// =============================================================================

// Known attribute names
namespace attr_names {
  const std::string kInline = "inline";
  const std::string kCold = "cold";
  const std::string kExport = "export";
  const std::string kHostExport = "host_export";
  const std::string kMangle = "mangle";
  const std::string kLibrary = "library";
  const std::string kUnwind = "unwind";
  const std::string kLayout = "layout";
  const std::string kFfiPassByValue = "ffi_pass_by_value";
  const std::string kDynamic = "dynamic";
  const std::string kStatic = "static";
}

// =============================================================================
// ATTRIBUTE TARGET KINDS
// =============================================================================

enum class AttrTarget {
  Procedure,
  ExternBlock,
  Record,
  Enum,
  Modal,
  Field,
  Method,
  TypeAlias,
  Binding,
  Statement,
  Expression,
  KeyBlock,
};

// =============================================================================
// ATTRIBUTE VALIDATION HELPERS
// =============================================================================

static bool IsKnownAttribute(std::string_view name) {
  static const std::unordered_set<std::string_view> kKnownAttrs = {
    attr_names::kInline,
    attr_names::kCold,
    attr_names::kExport,
    attr_names::kHostExport,
    attr_names::kMangle,
    attr_names::kLibrary,
    attr_names::kUnwind,
    attr_names::kLayout,
    attr_names::kFfiPassByValue,
    attr_names::kDynamic,
    attr_names::kStatic,
  };
  return kKnownAttrs.find(name) != kKnownAttrs.end();
}

static bool AttrAppliesTo(std::string_view name, AttrTarget target) {
  // inline applies to procedures and methods
  if (name == attr_names::kInline) {
    return target == AttrTarget::Procedure || target == AttrTarget::Method;
  }
  // cold applies to procedures and methods
  if (name == attr_names::kCold) {
    return target == AttrTarget::Procedure || target == AttrTarget::Method;
  }
  // export applies to procedures
  if (name == attr_names::kExport) {
    return target == AttrTarget::Procedure;
  }
  if (name == attr_names::kHostExport) {
    return target == AttrTarget::Procedure;
  }
  // mangle applies to procedures
  if (name == attr_names::kMangle) {
    return target == AttrTarget::Procedure;
  }
  // library applies to extern blocks
  if (name == attr_names::kLibrary) {
    return target == AttrTarget::ExternBlock;
  }
  // unwind applies to procedures
  if (name == attr_names::kUnwind) {
    return target == AttrTarget::Procedure;
  }
  // layout applies to records and enums
  if (name == attr_names::kLayout) {
    return target == AttrTarget::Record || target == AttrTarget::Enum;
  }
  // ffi_pass_by_value applies to records and enums
  if (name == attr_names::kFfiPassByValue) {
    return target == AttrTarget::Record || target == AttrTarget::Enum;
  }
  // dynamic applies to procedures and records
  if (name == attr_names::kDynamic) {
    return target == AttrTarget::Procedure ||
           target == AttrTarget::Method ||
           target == AttrTarget::Record;
  }
  // static verification mode applies to procedures
  if (name == attr_names::kStatic) {
    return target == AttrTarget::Procedure;
  }
  return false;
}

static bool ValidateInlineArg(const std::optional<std::string>& arg) {
  if (!arg.has_value()) {
    return true;  // No arg is valid - just [[inline]]
  }
  const auto& value = *arg;
  return value == "always" || value == "never" || value == "default";
}

static bool ValidateLayoutArg(const std::optional<std::string>& arg) {
  if (!arg.has_value()) {
    return false;  // Layout requires an argument
  }
  const auto& value = *arg;
  return value == "C";
}

static bool ValidateMangleArg(const std::optional<std::string>& arg) {
  if (!arg.has_value() || arg->empty()) {
    return false;
  }
  if (*arg == "none") {
    return true;
  }
  return arg->size() >= 2 &&
         ((arg->front() == '"' && arg->back() == '"') ||
          (arg->front() == '\'' && arg->back() == '\''));
}

// Helper to extract string value from AttributeArg
static std::optional<std::string> GetArgValue(const ast::AttributeArg& arg) {
  const auto* token = std::get_if<Token>(&arg.value);
  if (!token) {
    return std::nullopt;  // Expression argument, not a simple token
  }
  return token->lexeme;
}

static bool ValidateAttributeArgs(std::string_view name,
                                   const ast::AttributeItem& attr) {
  if (name == attr_names::kInline) {
    std::optional<std::string> arg;
    if (!attr.args.empty()) {
      arg = GetArgValue(attr.args[0]);
    }
    return ValidateInlineArg(arg);
  }
  if (name == attr_names::kLayout) {
    std::optional<std::string> arg;
    if (!attr.args.empty()) {
      arg = GetArgValue(attr.args[0]);
    }
    return ValidateLayoutArg(arg);
  }
  if (name == attr_names::kMangle) {
    std::optional<std::string> arg;
    if (!attr.args.empty()) {
      arg = GetArgValue(attr.args[0]);
    }
    return ValidateMangleArg(arg);
  }
  if (name == attr_names::kHostExport) {
    std::optional<std::string> arg;
    if (!attr.args.empty()) {
      arg = GetArgValue(attr.args[0]);
    }
    return ValidateMangleArg(arg);
  }
  // Other attributes don't take arguments
  return attr.args.empty();
}

}  // namespace

// HasAttribute is defined in attribute_registry.cpp and declared in attribute_registry.h
// No duplicate definition here.

// =============================================================================
// EXPORTED: GetAttribute
// =============================================================================

const ast::AttributeItem* GetAttribute(const std::vector<ast::AttributeItem>& attrs,
                                   std::string_view name) {
  SpecDefsAttributeList();
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return &attr;
    }
  }
  return nullptr;
}

// =============================================================================
// EXPORTED: GetAttributeArg
// =============================================================================

std::optional<std::string> GetAttributeArg(
    const std::vector<ast::AttributeItem>& attrs,
    std::string_view name,
    std::size_t index) {
  SpecDefsAttributeList();
  const auto* attr = GetAttribute(attrs, name);
  if (!attr || index >= attr->args.size()) {
    return std::nullopt;
  }
  // Extract string value from AttributeArg token
  const auto* token = std::get_if<Token>(&attr->args[index].value);
  if (!token) {
    return std::nullopt;  // Expression argument, not a simple token
  }
  return token->lexeme;
}

// =============================================================================
// EXPORTED: ValidateAttributeList
// =============================================================================

AttributeValidationResult ValidateAttributeList(
    const std::vector<ast::AttributeItem>& attrs,
    AttributeTarget target) {
  SpecDefsAttributeList();
  AttributeValidationResult result;
  result.ok = true;

  std::unordered_set<std::string> seen;

  AttrTarget internal_target;
  switch (target) {
    case AttributeTarget::Procedure:
      internal_target = AttrTarget::Procedure;
      break;
    case AttributeTarget::ExternBlock:
      internal_target = AttrTarget::ExternBlock;
      break;
    case AttributeTarget::Record:
      internal_target = AttrTarget::Record;
      break;
    case AttributeTarget::Enum:
      internal_target = AttrTarget::Enum;
      break;
    case AttributeTarget::Modal:
      internal_target = AttrTarget::Modal;
      break;
    case AttributeTarget::Field:
      internal_target = AttrTarget::Field;
      break;
    case AttributeTarget::Method:
      internal_target = AttrTarget::Method;
      break;
    case AttributeTarget::TypeAlias:
      internal_target = AttrTarget::TypeAlias;
      break;
    case AttributeTarget::Binding:
      internal_target = AttrTarget::Binding;
      break;
    case AttributeTarget::Statement:
      internal_target = AttrTarget::Statement;
      break;
    case AttributeTarget::Expression:
      internal_target = AttrTarget::Expression;
      break;
    case AttributeTarget::KeyBlock:
      internal_target = AttrTarget::KeyBlock;
      break;
  }

  for (const auto& attr : attrs) {
    // Check if attribute is known
    if (!IsKnownAttribute(attr.name)) {
      SPEC_RULE("Attr-Unknown");
      result.ok = false;
      result.diag_id = "E-MOD-2451";
      return result;
    }

    // Check for duplicates
    if (!seen.insert(attr.name).second) {
      SPEC_RULE("Attr-Dup");
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      return result;
    }

    // Check if attribute applies to this target
    if (!AttrAppliesTo(attr.name, internal_target)) {
      SPEC_RULE("Attr-Target-Err");
      result.ok = false;
      result.diag_id = "E-MOD-2452";
      return result;
    }

    // Validate arguments
    if (!ValidateAttributeArgs(attr.name, attr)) {
      SPEC_RULE("Attr-Args-Err");
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      return result;
    }
  }

  // Check for conflicting attributes
  const bool has_inline_always =
      GetAttributeArg(attrs, attr_names::kInline, 0) == "always";
  const bool has_inline_never =
      GetAttributeArg(attrs, attr_names::kInline, 0) == "never";

  if (has_inline_always && HasAttribute(attrs, attr_names::kCold)) {
    SPEC_RULE("Attr-Conflict-InlineCold");
    result.ok = false;
    result.diag_id = "E-MOD-2450";
    return result;
  }

  if (has_inline_always && has_inline_never) {
    SPEC_RULE("Attr-Conflict-InlineInline");
    result.ok = false;
    result.diag_id = "E-MOD-2450";
    return result;
  }

  SPEC_RULE("Attr-Valid");
  return result;
}

// =============================================================================
// EXPORTED: IsLayoutC
// =============================================================================

bool IsLayoutC(const std::vector<ast::AttributeItem>& attrs) {
  SpecDefsAttributeList();
  const auto* layout = GetAttribute(attrs, attr_names::kLayout);
  if (!layout || layout->args.empty()) {
    return false;
  }
  // Extract string value from the token
  const auto* token = std::get_if<Token>(&layout->args[0].value);
  return token && token->lexeme == "C";
}

// =============================================================================
// EXPORTED: IsFfiPassByValue
// =============================================================================

bool IsFfiPassByValue(const std::vector<ast::AttributeItem>& attrs) {
  SpecDefsAttributeList();
  return HasAttribute(attrs, attr_names::kFfiPassByValue);
}

// =============================================================================
// EXPORTED: IsDynamic
// =============================================================================

bool IsDynamic(const std::vector<ast::AttributeItem>& attrs) {
  SpecDefsAttributeList();
  return HasAttribute(attrs, attr_names::kDynamic);
}

}  // namespace ultraviolet::analysis
