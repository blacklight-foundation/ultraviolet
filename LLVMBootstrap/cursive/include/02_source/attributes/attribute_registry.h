#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// C0X Extension: Attribute System - Registry

// Attribute target (where attribute can be applied). This enum mirrors the
// spec-defined AttrTarget set exactly.
enum class AttributeTarget {
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

// Attribute argument specification
struct AttributeArgSpec {
  std::string name;
  bool required = false;
  bool accepts_expr = false;  // Can take expression value
  std::optional<std::string> default_value;
};

// Attribute specification
struct AttributeSpec {
  std::string_view name;
  std::set<AttributeTarget> valid_targets;
  std::vector<AttributeArgSpec> args;
  bool deprecated = false;
  std::optional<std::string_view> deprecated_message;
};

// Built-in attribute names
namespace attrs {
  // Key system attributes
  constexpr std::string_view kDynamic = "dynamic";
  constexpr std::string_view kOrder = "order";
  
  // Contract attributes
  constexpr std::string_view kDebugContract = "debug_contract";
  constexpr std::string_view kReleaseContract = "release_contract";
  
  // Memory attributes
  constexpr std::string_view kLayout = "layout";
  constexpr std::string_view kAlign = "align";
  constexpr std::string_view kPacked = "packed";
  constexpr std::string_view kRelaxed = "relaxed";
  constexpr std::string_view kAcquire = "acquire";
  constexpr std::string_view kRelease = "release";
  constexpr std::string_view kAcqRel = "acqrel";
  constexpr std::string_view kSeqCst = "seqcst";
  constexpr std::string_view kReflect = "reflect";
  constexpr std::string_view kDerive = "derive";
  constexpr std::string_view kEmit = "emit";
  constexpr std::string_view kFiles = "files";
  constexpr std::string_view kTest = "test";
  
  // Diagnostic control
  constexpr std::string_view kAllow = "allow";
  constexpr std::string_view kWarn = "warn";
  constexpr std::string_view kDeny = "deny";
  constexpr std::string_view kForbid = "forbid";
  
  // Visibility/linking
  constexpr std::string_view kDeprecated = "deprecated";
  constexpr std::string_view kStaleOk = "stale_ok";
  constexpr std::string_view kInline = "inline";
  constexpr std::string_view kCold = "cold";
  constexpr std::string_view kHot = "hot";
  constexpr std::string_view kMangle = "mangle";
  constexpr std::string_view kExport = "export";
  constexpr std::string_view kHostExport = "host_export";
  constexpr std::string_view kLibrary = "library";
  constexpr std::string_view kUnwind = "unwind";
  constexpr std::string_view kWeak = "weak";
  constexpr std::string_view kFfiPassByValue = "ffi_pass_by_value";

  // Verification-mode attributes
  constexpr std::string_view kStatic = "static";
}

// Verification-mode attributes used on foreign declarations and contracts.
enum class VerificationModeAttribute {
  Static,
  Dynamic,
};

// Attribute registry
class AttributeRegistry {
 public:
  AttributeRegistry();
  
  // Register a new attribute
  void Register(const AttributeSpec& spec);
  
  // Lookup attribute specification
  const AttributeSpec* Lookup(std::string_view name) const;
  
  // Check if attribute is valid for target
  bool IsValidForTarget(std::string_view name, AttributeTarget target) const;
  
  // Get all registered attributes
  const std::map<std::string, AttributeSpec>& Specs() const { return specs_; }
  
 private:
  std::map<std::string, AttributeSpec> specs_;
};

// Global registry instance
const AttributeRegistry& GetAttributeRegistry();

// Attribute validation result
struct AttributeValidationResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::string message;
};

// Validate attribute-list well-formedness (`AttrListJudg = {AttrListWf}`).
AttributeValidationResult ValidateAttributes(
    const ast::AttributeList& attrs,
    AttributeTarget target);

// Validate attributes on a declaration form that is syntax-bearing but not a
// spec-defined AttrTarget.
AttributeValidationResult ValidateUnsupportedAttributeTarget(
    const ast::AttributeList& attrs,
    std::string_view target_name);

// Check for specific attribute
bool HasAttribute(const ast::AttributeList& attrs, std::string_view name);

// Declaration-level AttrByName relation used by declaration-scoped semantics.
template <typename Decl>
bool HasAttributeByName(const Decl& decl, std::string_view name) {
  return HasAttribute(decl.attrs, name);
}

template <typename Decl>
bool IsDynamicDecl(const Decl& decl) {
  return HasAttributeByName(decl, attrs::kDynamic);
}

// Resolve verification mode attribute from an attribute list.
// Returns nullopt when no verification-mode attribute is present.
std::optional<VerificationModeAttribute> ResolveVerificationModeAttribute(
    const ast::AttributeList& attrs);

// Get attribute value (first argument or named arg)
std::optional<std::string> GetAttributeValue(
    const ast::AttributeList& attrs,
    std::string_view name,
    std::string_view arg_name = "");

}  // namespace cursive::analysis
