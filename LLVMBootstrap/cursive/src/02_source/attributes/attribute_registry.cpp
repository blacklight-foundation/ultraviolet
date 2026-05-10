// ===========================================================================
// attribute_registry.cpp - Attribute registry implementation
// ===========================================================================
//
// SPEC REFERENCE:
//   - CursiveSpecification.md, Section 5.13 "Attributes" (line 13848)
//   - CursiveSpecification.md, Section 5.13.1 "Attribute Syntax" (lines 13860-13920)
//   - CursiveSpecification.md, Section 5.13.2 "Built-in Attributes" (lines 13930-14100)
//
// SOURCE FILE:
//   - cursive-bootstrap/src/03_analysis/attributes/attribute_registry.cpp
//
// ===========================================================================

#include "02_source/attributes/attribute_registry.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "00_core/assert_spec.h"
#include "01_project/language_profile.h"

namespace cursive::analysis {

namespace {

std::string ReservedAttributePrefix() {
  return std::string(project::ActiveLanguageProfile().runtime_root) + "::";
}

static inline void SpecDefsAttributes() {
  SPEC_DEF("Attribute", "C0X.6.A");
  SPEC_DEF("AttrTarget", "C0X.6.A");
  SPEC_DEF("AttrValidation", "C0X.6.A");
  SPEC_DEF("AttrRegistry", "C0X.6.A");
}

static std::string NormalizeAttrLiteral(std::string value);

static bool IsStringLiteralToken(const ast::Token& tok) {
  return tok.kind == lexer::TokenKind::StringLiteral;
}

static bool IsNonEmptyStringLiteralToken(const ast::Token& tok) {
  return IsStringLiteralToken(tok) && !NormalizeAttrLiteral(tok.lexeme).empty();
}

static bool IsIdentifierToken(const ast::Token& tok) {
  return tok.kind == lexer::TokenKind::Identifier;
}

static bool IsKeywordToken(const ast::Token& tok) {
  return tok.kind == lexer::TokenKind::Keyword;
}

static const ast::Token* GetTokenArg(const ast::AttributeArg& arg) {
  return std::get_if<ast::Token>(&arg.value);
}

static bool HasOnlyPositionalTokenArg(const ast::AttributeItem& attr) {
  return attr.args.size() == 1 && !attr.args.front().key.has_value() &&
         GetTokenArg(attr.args.front()) != nullptr;
}

static bool ValidateDeriveAttributeArgs(const ast::AttributeItem& attr,
                                        AttributeValidationResult& result) {
  if (attr.name != attrs::kDerive) {
    return true;
  }

  if (attr.args.empty()) {
    result.ok = false;
    result.diag_id = "E-MOD-2450";
    result.span = attr.span;
    result.message =
        "[[derive(... )]] requires one or more identifier arguments";
    return false;
  }

  std::vector<std::string_view> seen_targets;
  seen_targets.reserve(attr.args.size());
  for (const auto& arg : attr.args) {
    const auto* token = GetTokenArg(arg);
    if (arg.key.has_value() || token == nullptr ||
        token->kind != lexer::TokenKind::Identifier) {
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      result.span = attr.span;
      result.message =
          "[[derive(... )]] requires one or more identifier arguments";
      return false;
    }

    if (std::find(seen_targets.begin(), seen_targets.end(), token->lexeme) !=
        seen_targets.end()) {
      result.ok = false;
      result.diag_id = "E-CTE-0312";
      result.span = attr.span;
      result.message = "Duplicate derive target in one derive attribute";
      return false;
    }
    seen_targets.push_back(token->lexeme);
  }

  return true;
}

static bool LooksLikeCoverageReference(std::string_view value) {
  const std::size_t marker = value.rfind("@L");
  if (marker == std::string_view::npos || marker == 0 ||
      marker + 2 >= value.size()) {
    return false;
  }
  for (std::size_t i = marker + 2; i < value.size(); ++i) {
    if (value[i] < '0' || value[i] > '9') {
      return false;
    }
  }
  return true;
}

static bool ValidateTestAttributeArgs(const ast::AttributeItem& attr,
                                      AttributeValidationResult& result) {
  if (attr.name != attrs::kTest) {
    return true;
  }

  bool saw_name = false;
  for (const auto& arg : attr.args) {
    if (arg.key.has_value() && *arg.key == "name") {
      if (saw_name) {
        result.ok = false;
        result.diag_id = "E-TST-0102";
        result.span = attr.span;
        result.message = "Duplicate [[test]] name argument";
        return false;
      }

      const auto* token = GetTokenArg(arg);
      if (!token || !IsNonEmptyStringLiteralToken(*token)) {
        result.ok = false;
        result.diag_id = "E-TST-0101";
        result.span = attr.span;
        result.message = "Malformed [[test]] argument";
        return false;
      }
      saw_name = true;
      continue;
    }

    if (arg.key.has_value() && *arg.key == "covers") {
      const auto* nested =
          std::get_if<std::vector<ast::AttributeArg>>(&arg.value);
      if (!nested || nested->size() != 1 || (*nested)[0].key.has_value()) {
        result.ok = false;
        result.diag_id = "E-TST-0103";
        result.span = attr.span;
        result.message = "Malformed covers(...) argument";
        return false;
      }

      const auto* token = GetTokenArg((*nested)[0]);
      if (!token || !IsNonEmptyStringLiteralToken(*token) ||
          !LooksLikeCoverageReference(NormalizeAttrLiteral(token->lexeme))) {
        result.ok = false;
        result.diag_id = "E-TST-0103";
        result.span = attr.span;
        result.message = "Malformed covers(...) argument";
        return false;
      }
      continue;
    }

    result.ok = false;
    result.diag_id = "E-TST-0101";
    result.span = attr.span;
    result.message = "Malformed [[test]] argument";
    return false;
  }

  return true;
}

static bool RejectArgumentBearingAttribute(const ast::AttributeItem& attr,
                                           AttributeValidationResult& result) {
  if (attr.args.empty()) {
    return true;
  }
  result.ok = false;
  result.diag_id = "E-MOD-2450";
  result.span = attr.span;
  result.message = "Malformed [[" + attr.name + "]] syntax";
  return false;
}

static std::string NormalizeAttrLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

static bool ParseU64Literal(const ast::Token& tok, std::uint64_t& out) {
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

static bool IsPowerOfTwo(std::uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

static bool IsKnownLibraryKind(std::string_view kind) {
  return kind == "dylib" || kind == "static" || kind == "framework" ||
         kind == "raw-dylib";
}

static bool IsIntLayoutKind(std::string_view value) {
  return value == "i8" || value == "i16" || value == "i32" || value == "i64" ||
         value == "u8" || value == "u16" || value == "u32" || value == "u64";
}

// Class/record methods are validated against procedure-target attributes for
// the portion that is semantically meaningful on methods.
static bool IsMethodProcedureEquivalentAttr(std::string_view name) {
  return name == attrs::kInline || name == attrs::kCold ||
         name == attrs::kDeprecated || name == attrs::kDynamic;
}

static AttributeTarget NormalizeAttrTargetForLookup(std::string_view name,
                                                    AttributeTarget target) {
  if (target == AttributeTarget::Method &&
      IsMethodProcedureEquivalentAttr(name)) {
    return AttributeTarget::Procedure;
  }
  return target;
}

// Initialize the global registry with built-in attributes
AttributeRegistry InitializeRegistry() {
  SpecDefsAttributes();
  SPEC_RULE("AttrRegistry-Init");

  AttributeRegistry registry;

  // [[dynamic]] - Dynamic verification scope
  {
    AttributeSpec spec;
    spec.name = attrs::kDynamic;
    spec.valid_targets = {AttributeTarget::Procedure, AttributeTarget::Record,
                          AttributeTarget::Enum, AttributeTarget::Modal,
                          AttributeTarget::Expression};
    registry.Register(spec);
  }

  // [[layout(...)]] - Memory representation
  {
    AttributeSpec spec;
    spec.name = attrs::kLayout;
    spec.valid_targets = {AttributeTarget::Record, AttributeTarget::Enum};
    spec.args.push_back({"kind", true, false, std::nullopt});  // C, packed, transparent
    registry.Register(spec);
  }

  // Memory order attributes
  {
    AttributeSpec spec;
    spec.name = attrs::kRelaxed;
    spec.valid_targets = {AttributeTarget::Expression, AttributeTarget::KeyBlock};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kAcquire;
    spec.valid_targets = {AttributeTarget::Expression, AttributeTarget::KeyBlock};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kRelease;
    spec.valid_targets = {AttributeTarget::Expression, AttributeTarget::KeyBlock};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kAcqRel;
    spec.valid_targets = {AttributeTarget::Expression, AttributeTarget::KeyBlock};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kSeqCst;
    spec.valid_targets = {AttributeTarget::Expression, AttributeTarget::KeyBlock};
    registry.Register(spec);
  }

  // Compile-time attribute surface.
  {
    AttributeSpec spec;
    spec.name = attrs::kReflect;
    spec.valid_targets = {AttributeTarget::Record, AttributeTarget::Enum,
                          AttributeTarget::Modal};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kDerive;
    spec.valid_targets = {AttributeTarget::Record, AttributeTarget::Enum,
                          AttributeTarget::Modal};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kEmit;
    spec.valid_targets = {AttributeTarget::Statement, AttributeTarget::Expression};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kFiles;
    spec.valid_targets = {AttributeTarget::Statement, AttributeTarget::Expression};
    registry.Register(spec);
  }
  {
    AttributeSpec spec;
    spec.name = attrs::kTest;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[deprecated]] - Deprecated declaration
  {
    AttributeSpec spec;
    spec.name = attrs::kDeprecated;
    spec.valid_targets = {AttributeTarget::Procedure, AttributeTarget::Record,
                          AttributeTarget::Enum, AttributeTarget::Modal,
                          AttributeTarget::Field, AttributeTarget::Binding,
                          AttributeTarget::TypeAlias};
    registry.Register(spec);
  }

  // [[stale_ok]] - Suppress shared staleness warnings
  {
    AttributeSpec spec;
    spec.name = attrs::kStaleOk;
    spec.valid_targets = {AttributeTarget::Binding};
    registry.Register(spec);
  }

  // [[inline]] / [[inline(mode)]] - Inline request
  {
    AttributeSpec spec;
    spec.name = attrs::kInline;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[cold]] - Cold function (unlikely to be called)
  {
    AttributeSpec spec;
    spec.name = attrs::kCold;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[mangle(mode)]] - FFI link-name control
  {
    AttributeSpec spec;
    spec.name = attrs::kMangle;
    spec.valid_targets = {AttributeTarget::Procedure};
    spec.args.push_back({"mode", true, false, std::nullopt});
    registry.Register(spec);
  }

  // [[export]] - Export symbol
  {
    AttributeSpec spec;
    spec.name = attrs::kExport;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[host_export]] - Hosted library export
  {
    AttributeSpec spec;
    spec.name = attrs::kHostExport;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[library(name: "...", kind: "...")]] - Link library (extern block)
  {
    AttributeSpec spec;
    spec.name = attrs::kLibrary;
    spec.valid_targets = {AttributeTarget::ExternBlock};
    spec.args.push_back({"name", true, false, std::nullopt});
    spec.args.push_back({"kind", false, false, "dylib"});
    registry.Register(spec);
  }

  // [[unwind("mode")]] - FFI unwind mode
  {
    AttributeSpec spec;
    spec.name = attrs::kUnwind;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }

  // [[ffi_pass_by_value]] - Force by-value FFI passing
  {
    AttributeSpec spec;
    spec.name = attrs::kFfiPassByValue;
    spec.valid_targets = {AttributeTarget::Record, AttributeTarget::Enum};
    registry.Register(spec);
  }

  // Verification-mode attributes
  {
    AttributeSpec spec;
    spec.name = attrs::kStatic;
    spec.valid_targets = {AttributeTarget::Procedure};
    registry.Register(spec);
  }
  return registry;
}

}  // namespace

AttributeRegistry::AttributeRegistry() = default;

void AttributeRegistry::Register(const AttributeSpec& spec) {
  specs_[std::string(spec.name)] = spec;
}

const AttributeSpec* AttributeRegistry::Lookup(std::string_view name) const {
  auto it = specs_.find(std::string(name));
  if (it == specs_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool AttributeRegistry::IsValidForTarget(std::string_view name,
                                         AttributeTarget target) const {
  const auto* spec = Lookup(name);
  if (!spec) {
    return false;  // Unknown attribute
  }
  return spec->valid_targets.count(target) > 0;
}

const AttributeRegistry& GetAttributeRegistry() {
  static const AttributeRegistry registry = InitializeRegistry();
  return registry;
}

AttributeValidationResult ValidateAttributes(
    const ast::AttributeList& attrs,
    AttributeTarget target) {
  SpecDefsAttributes();
  // AttrListJudg is the aggregate attribute-list judgment, and AttrListWf is
  // the concrete well-formedness check this entry point enforces.
  SPEC_RULE("AttrListJudg");
  SPEC_RULE("AttrListWf");
  SPEC_RULE("AttrValidation");

  AttributeValidationResult result;
  const auto& registry = GetAttributeRegistry();

  for (const auto& attr : attrs) {
    const auto* spec = registry.Lookup(attr.name);

    // Unknown attributes use the generic §9.1.7 diagnostic unless the
    // miss falls into the active language's reserved vendor namespace,
    // which §9.2.7 assigns to E-CNF-0402 instead.
    if (!spec) {
      result.ok = false;
      if (attr.name.rfind(ReservedAttributePrefix(), 0) == 0) {
        result.diag_id = "E-CNF-0402";
      } else {
        result.diag_id = "E-MOD-2451";  // Unknown attribute name
      }
      result.span = attr.span;
      result.message = "Unknown attribute: " + attr.name;
      return result;
    }

    // Deprecated attribute
    if (spec->deprecated) {
      // Emit warning (non-fatal)
    }

    const AttributeTarget effective_target =
        NormalizeAttrTargetForLookup(attr.name, target);

    // Invalid target
    if (!registry.IsValidForTarget(attr.name, effective_target)) {
      result.ok = false;
      if (attr.name == ::cursive::analysis::attrs::kDynamic &&
          effective_target == AttributeTarget::TypeAlias) {
        result.diag_id = "E-CON-0411";
      } else if (attr.name == ::cursive::analysis::attrs::kDynamic &&
                 effective_target == AttributeTarget::Field) {
        result.diag_id = "E-CON-0412";
      } else if (attr.name == ::cursive::analysis::attrs::kLibrary) {
        result.diag_id = "E-SYS-3345";
      } else {
        result.diag_id = "E-MOD-2452";  // Attribute not valid on target declaration kind
      }
      result.span = attr.span;
      result.message = "Attribute '" + attr.name + "' cannot be applied here";
      return result;
    }

    if (!ValidateDeriveAttributeArgs(attr, result)) {
      return result;
    }

    if (!ValidateTestAttributeArgs(attr, result)) {
      return result;
    }

    if (attr.name == ::cursive::analysis::attrs::kCold ||
        attr.name == ::cursive::analysis::attrs::kReflect ||
        attr.name == ::cursive::analysis::attrs::kDynamic ||
        attr.name == ::cursive::analysis::attrs::kStaleOk ||
        attr.name == ::cursive::analysis::attrs::kEmit ||
        attr.name == ::cursive::analysis::attrs::kFiles ||
        attr.name == ::cursive::analysis::attrs::kFfiPassByValue ||
        attr.name == ::cursive::analysis::attrs::kStatic ||
        attr.name == ::cursive::analysis::attrs::kRelaxed ||
        attr.name == ::cursive::analysis::attrs::kAcquire ||
        attr.name == ::cursive::analysis::attrs::kRelease ||
        attr.name == ::cursive::analysis::attrs::kAcqRel ||
        attr.name == ::cursive::analysis::attrs::kSeqCst) {
      if (!RejectArgumentBearingAttribute(attr, result)) {
        return result;
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kLayout) {
      bool saw_c = false;
      bool saw_packed = false;
      bool saw_align = false;
      bool saw_discriminant = false;

      for (const auto& arg : attr.args) {
        if (arg.key.has_value()) {
          if (*arg.key != "align") {
            result.ok = false;
            result.diag_id = "E-MOD-2450";
            result.span = attr.span;
            result.message = "Malformed layout argument";
            return result;
          }

          const auto* nested =
              std::get_if<std::vector<ast::AttributeArg>>(&arg.value);
          if (!nested || nested->size() != 1 || (*nested)[0].key.has_value()) {
            result.ok = false;
            result.diag_id = "E-MOD-2450";
            result.span = attr.span;
            result.message = "Malformed layout align argument";
            return result;
          }
          const auto* token = std::get_if<ast::Token>(&(*nested)[0].value);
          std::uint64_t alignment = 0;
          if (!token || token->kind != lexer::TokenKind::IntLiteral ||
              !ParseU64Literal(*token, alignment) ||
              !IsPowerOfTwo(alignment)) {
            result.ok = false;
            result.diag_id = "E-MOD-2453";
            result.span = attr.span;
            result.message = "Invalid layout align argument";
            return result;
          }
          if (saw_align) {
            result.ok = false;
            result.diag_id = "E-MOD-2455";
            result.span = attr.span;
            result.message = "Conflicting layout arguments";
            return result;
          }
          saw_align = true;
          continue;
        }

        const auto* token = std::get_if<ast::Token>(&arg.value);
        if (!token) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed layout argument";
          return result;
        }

        const auto value = NormalizeAttrLiteral(token->lexeme);
        if (value == "C") {
          if (saw_c) {
            result.ok = false;
            result.diag_id = "E-MOD-2455";
            result.span = attr.span;
            result.message = "Conflicting layout arguments";
            return result;
          }
          saw_c = true;
          continue;
        }
        if (value == "packed") {
          if (saw_packed) {
            result.ok = false;
            result.diag_id = "E-MOD-2455";
            result.span = attr.span;
            result.message = "Conflicting layout arguments";
            return result;
          }
          saw_packed = true;
          continue;
        }
        if (IsIntLayoutKind(value)) {
          if (saw_discriminant) {
            result.ok = false;
            result.diag_id = "E-MOD-2455";
            result.span = attr.span;
            result.message = "Conflicting layout arguments";
            return result;
          }
          saw_discriminant = true;
          continue;
        }

        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = "Malformed layout argument";
        return result;
      }

      if (!saw_c && !saw_packed && !saw_align && !saw_discriminant) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message =
            "Malformed [[layout]] syntax: missing required layout kind";
        return result;
      }
      if (saw_packed && target != AttributeTarget::Record) {
        result.ok = false;
        result.diag_id = "E-MOD-2454";
        result.span = attr.span;
        result.message = "layout(packed) is valid only on records";
        return result;
      }
      if (saw_discriminant && target != AttributeTarget::Enum) {
        result.ok = false;
        result.diag_id = "E-MOD-2455";
        result.span = attr.span;
        result.message = "Conflicting layout arguments";
        return result;
      }
      if (saw_packed && saw_align) {
        result.ok = false;
        result.diag_id = "E-MOD-2455";
        result.span = attr.span;
        result.message = "Conflicting layout arguments";
        return result;
      }
      if (saw_discriminant && (saw_c || saw_packed || saw_align)) {
        result.ok = false;
        result.diag_id = "E-MOD-2455";
        result.span = attr.span;
        result.message = "Conflicting layout arguments";
        return result;
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kMangle) {
      if (attr.args.size() != 1) {
        result.ok = false;
        result.diag_id = "E-SYS-3341";
        result.span = attr.span;
        result.message = "Invalid [[mangle(mode)]] argument";
        return result;
      }
      const auto& arg = attr.args.front();
      if (arg.key.has_value() && *arg.key != "mode") {
        result.ok = false;
        result.diag_id = "E-SYS-3341";
        result.span = attr.span;
        result.message = "Invalid [[mangle(mode)]] argument";
        return result;
      }
      const auto* token = std::get_if<ast::Token>(&arg.value);
      if (!token) {
        result.ok = false;
        result.diag_id = "E-SYS-3341";
        result.span = attr.span;
        result.message = "Invalid [[mangle(mode)]] argument";
        return result;
      }
      const auto mode = NormalizeAttrLiteral(token->lexeme);
      if (mode.empty()) {
        result.ok = false;
        result.diag_id = "E-SYS-3341";
        result.span = attr.span;
        result.message = "Invalid [[mangle(mode)]] argument";
        return result;
      }
      if (!(mode == "none" && IsIdentifierToken(*token)) &&
          !IsStringLiteralToken(*token)) {
        result.ok = false;
        result.diag_id = "E-SYS-3341";
        result.span = attr.span;
        result.message = "Invalid [[mangle(mode)]] argument";
        return result;
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kInline) {
      if (attr.args.size() > 1) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = "Malformed [[inline]] syntax";
        return result;
      }

      if (!attr.args.empty()) {
        const auto& arg = attr.args.front();
        if (arg.key.has_value()) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[inline]] syntax";
          return result;
        }

        const auto* token = std::get_if<ast::Token>(&arg.value);
        if (!token || (!IsIdentifierToken(*token) && !IsKeywordToken(*token))) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[inline]] syntax";
          return result;
        }

        const auto mode = NormalizeAttrLiteral(token->lexeme);
        if (mode != "always" && mode != "never" && mode != "default") {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[inline]] syntax";
          return result;
        }
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kDeprecated) {
      if (attr.args.size() > 1) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = "Malformed [[deprecated]] syntax";
        return result;
      }
      if (!attr.args.empty()) {
        const auto& arg = attr.args.front();
        const auto* token = GetTokenArg(arg);
        if (arg.key.has_value() || !token || !IsStringLiteralToken(*token)) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[deprecated]] syntax";
          return result;
        }
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kExport ||
        attr.name == ::cursive::analysis::attrs::kHostExport) {
      if (attr.args.size() != 1 || attr.args.front().key.has_value()) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = attr.name == ::cursive::analysis::attrs::kExport
                             ? "Malformed [[export]] syntax"
                             : "Malformed [[host_export]] syntax";
        return result;
      }
      const auto* token = std::get_if<ast::Token>(&attr.args.front().value);
      if (!token || !IsStringLiteralToken(*token) ||
          NormalizeAttrLiteral(token->lexeme).empty()) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = attr.name == ::cursive::analysis::attrs::kExport
                             ? "Malformed [[export]] syntax"
                             : "Malformed [[host_export]] syntax";
        return result;
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kLibrary &&
        target == AttributeTarget::ExternBlock) {
      bool saw_name = false;
      bool saw_kind = false;
      std::size_t arg_index = 0;
      for (const auto& arg : attr.args) {
        if (!arg.key.has_value() || (*arg.key != "name" && *arg.key != "kind")) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[library]] syntax";
          return result;
        }
        const auto* token = std::get_if<ast::Token>(&arg.value);
        if (!token || !IsStringLiteralToken(*token)) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";
          result.span = attr.span;
          result.message = "Malformed [[library]] syntax";
          return result;
        }
        const auto normalized = NormalizeAttrLiteral(token->lexeme);
        if (*arg.key == "name") {
          if (saw_name || normalized.empty() || arg_index != 0) {
            result.ok = false;
            result.diag_id = "E-MOD-2450";
            result.span = attr.span;
            result.message = "Malformed [[library]] syntax";
            return result;
          }
          saw_name = true;
        } else {
          if (saw_kind || !saw_name || arg_index != 1) {
            result.ok = false;
            result.diag_id = "E-MOD-2450";
            result.span = attr.span;
            result.message = "Malformed [[library]] syntax";
            return result;
          }
          saw_kind = true;
          if (!IsKnownLibraryKind(normalized)) {
            result.ok = false;
            result.diag_id = "E-SYS-3346";
            result.span = attr.span;
            result.message = "Unknown or unsupported library kind";
            return result;
          }
        }
        ++arg_index;
      }
      if (!saw_name) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message =
            "Malformed [[library]] syntax: missing required `name` argument";
        return result;
      }
    }

    if (attr.name == ::cursive::analysis::attrs::kUnwind) {
      if (!HasOnlyPositionalTokenArg(attr)) {
        result.ok = false;
        result.diag_id = "E-MOD-2450";
        result.span = attr.span;
        result.message = "Malformed [[unwind]] syntax";
        return result;
      }
    }

    for (const auto& arg_spec : spec->args) {
      if (arg_spec.required) {
        bool found = false;
        for (const auto& arg : attr.args) {
          if (arg.key.has_value() && *arg.key == arg_spec.name) {
            found = true;
            break;
          }
          if (attr.name == ::cursive::analysis::attrs::kLayout &&
              arg_spec.name == "kind" &&
              arg.key.has_value() &&
              *arg.key == "align") {
            found = true;
            break;
          }
          // Positional arg for first required arg
          if (!arg.key.has_value() && &arg_spec == &spec->args[0]) {
            found = true;
            break;
          }
        }
        if (!found) {
          result.ok = false;
          result.diag_id = "E-MOD-2450";  // Malformed attribute syntax
          result.span = attr.span;
          result.message = "Malformed [[" + attr.name +
                           "]] syntax: missing required argument `" +
                           arg_spec.name + "`";
          return result;
        }
      }
    }
  }

  return result;
}

AttributeValidationResult ValidateUnsupportedAttributeTarget(
    const ast::AttributeList& attrs,
    std::string_view target_name) {
  AttributeValidationResult result;
  const auto& registry = GetAttributeRegistry();

  for (const auto& attr : attrs) {
    const auto* spec = registry.Lookup(attr.name);
    if (!spec) {
      result.ok = false;
      if (attr.name.rfind(ReservedAttributePrefix(), 0) == 0) {
        result.diag_id = "E-CNF-0402";
      } else {
        result.diag_id = "E-MOD-2451";
      }
      result.span = attr.span;
      result.message = "Unknown attribute: " + attr.name;
      return result;
    }

    result.ok = false;
    result.diag_id =
        attr.name == ::cursive::analysis::attrs::kLibrary ? "E-SYS-3345"
                                                          : "E-MOD-2452";
    result.span = attr.span;
    result.message = "Attribute '" + attr.name + "' cannot be applied to " +
                     std::string(target_name);
    return result;
  }

  return result;
}

bool HasAttribute(const ast::AttributeList& attrs, std::string_view name) {
  for (const auto& attr : attrs) {
    if (attr.name == name) {
      return true;
    }
  }
  return false;
}

std::optional<VerificationModeAttribute> ResolveVerificationModeAttribute(
    const ast::AttributeList& attrs) {
  if (HasAttribute(attrs, attrs::kStatic)) {
    return VerificationModeAttribute::Static;
  }
  if (HasAttribute(attrs, attrs::kDynamic)) {
    return VerificationModeAttribute::Dynamic;
  }
  return std::nullopt;
}

std::optional<std::string> GetAttributeValue(
    const ast::AttributeList& attrs,
    std::string_view name,
    std::string_view arg_name) {
  for (const auto& attr : attrs) {
    if (attr.name != name) {
      continue;
    }

    for (const auto& arg : attr.args) {
      // Named argument
      if (!arg_name.empty() && arg.key.has_value() && *arg.key == arg_name) {
        if (const auto* token = std::get_if<ast::Token>(&arg.value)) {
          return std::string(token->lexeme);
        }
      }
      // First positional argument
      if (arg_name.empty() && !arg.key.has_value()) {
        if (const auto* token = std::get_if<ast::Token>(&arg.value)) {
          return std::string(token->lexeme);
        }
      }
    }
  }

  return std::nullopt;
}

}  // namespace cursive::analysis
