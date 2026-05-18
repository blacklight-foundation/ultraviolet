// ===========================================================================
// attribute_validate.cpp - Specialized attribute validation
// ===========================================================================
//
// SPEC REFERENCE:
//   - SPECIFICATION.md, Section 5.13 "Attributes" (line 13859)
//   - SPECIFICATION.md, Section 5.13.3 "Built-in Attributes" (lines 13941-13980)
//   - SPECIFICATION.md, Section 5.13.6 "Export and FFI" (line 13922)
//
// SOURCE FILE:
//   - ultraviolet-bootstrap/src/03_analysis/attributes/attribute_registry.cpp
//
// ===========================================================================

#include "02_source/attributes/attribute_registry.h"

#include <algorithm>
#include <set>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis
{

  namespace
  {

    static inline void SpecDefsAttributeValidate()
    {
      SPEC_DEF("AttrValidate", "C0.5.13.5");
      SPEC_DEF("AttrEffects", "C0.5.13.6");
    }

    // Valid inline argument values
    const std::set<std::string> kValidInlineArgs = {"always", "never", "default"};

    // Valid layout argument values
    const std::set<std::string> kValidLayoutArgs = {
        "C", "packed", "transparent",
        // Integer discriminants for enums
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"};

  } // namespace

  // Validate [[inline]] attribute variants
  // Per spec: Argument must be: (none), always, never, default
  AttributeValidationResult ValidateInlineAttribute(const ast::AttributeItem &attr)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Inline");

    AttributeValidationResult result;

    if (attr.name != attrs::kInline)
    {
      return result; // Not an inline attribute
    }

    // If no arguments, that's valid (bare [[inline]])
    if (attr.args.empty())
    {
      return result;
    }

    // Check first positional argument
    for (const auto &arg : attr.args)
    {
      if (!arg.key.has_value())
      {
        if (const auto *token = std::get_if<ast::Token>(&arg.value))
        {
          std::string value(token->lexeme);
          if (kValidInlineArgs.find(value) == kValidInlineArgs.end())
          {
            result.ok = false;
            result.diag_id = "E-MOD-2450";
            result.span = attr.span;
            result.message = "Invalid [[inline]] argument '" + value +
                             "'; expected 'always', 'never', or 'default'";
            return result;
          }
        }
      }
    }

    return result;
  }

  // Validate [[layout(...)]] attribute
  // Per spec §5.13.3:
  //   - [[layout(C)]]: C-compatible layout for records/enums
  //   - [[layout(packed)]]: No inter-field padding
  //   - [[layout(IntType)]]: Explicit discriminant type for enums
  AttributeValidationResult ValidateLayoutAttribute(
      const ast::AttributeItem &attr,
      AttributeTarget target)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Layout");

    AttributeValidationResult result;

    if (attr.name != attrs::kLayout)
    {
      return result; // Not a layout attribute
    }

    // Layout requires an argument
    if (attr.args.empty())
    {
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      result.span = attr.span;
      result.message = "[[layout]] requires an argument (C, packed, or int type)";
      return result;
    }

    // Check the argument value
    for (const auto &arg : attr.args)
    {
      if (!arg.key.has_value() || *arg.key == "kind")
      {
        if (const auto *token = std::get_if<ast::Token>(&arg.value))
        {
          std::string value(token->lexeme);

          // Check for valid layout values
          if (kValidLayoutArgs.find(value) == kValidLayoutArgs.end())
          {
            result.ok = false;
            result.diag_id = "E-MOD-2455"; // Attr-Layout-Conflict
            result.span = attr.span;
            result.message = "Invalid [[layout]] argument '" + value + "'";
            return result;
          }

          // Integer discriminants only valid on enums
          bool is_int_type = value == "i8" || value == "i16" || value == "i32" ||
                             value == "i64" || value == "u8" || value == "u16" ||
                             value == "u32" || value == "u64";
          if (is_int_type && target != AttributeTarget::Enum)
          {
            result.ok = false;
            result.diag_id = "E-MOD-2452";
            result.span = attr.span;
            result.message =
                "[[layout(" + value + ")]] is only valid on enum declarations";
            return result;
          }

          // [[packed]] only valid on records
          if (value == "packed" && target != AttributeTarget::Record)
          {
            result.ok = false;
            result.diag_id = "E-MOD-2454"; // Attr-Packed-NonRecord
            result.span = attr.span;
            result.message = "[[layout(packed)]] is only valid on record types";
            return result;
          }
        }
      }
    }

    return result;
  }

  // Validate [[export]] / [[host_export]] attribute on procedure
  // Per spec §5.13.6:
  //   - Procedure must have FfiSafe signature
  //   - Cannot take capability parameters
  AttributeValidationResult ValidateExportAttribute(
      const ast::AttributeItem &attr,
      const ast::ProcedureDecl &proc)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Export");

    AttributeValidationResult result;

    if (attr.name != attrs::kExport && attr.name != attrs::kHostExport)
    {
      return result; // Not an export attribute
    }

    // The actual FFI safety check is done during type checking
    // Here we just validate the attribute syntax

    return result;
  }

  // Validate [[dynamic]] attribute
  // Per spec: Procedure must have |: contract to use [[dynamic]]
  AttributeValidationResult ValidateDynamicAttribute(
      const ast::AttributeItem &attr,
      bool has_contract)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Dynamic");

    AttributeValidationResult result;

    if (attr.name != attrs::kDynamic)
    {
      return result; // Not a dynamic attribute
    }

    if (!has_contract)
    {
      result.ok = false;
      result.diag_id = "E-CON-0410";
      result.span = attr.span;
      result.message = "[[dynamic]] requires procedure to have a contract (|:)";
      return result;
    }

    return result;
  }

  // Validate [[ffi_pass_by_value]] attribute
  // Per spec: Required for types with Drop that are passed by value in FFI
  AttributeValidationResult ValidateFfiPassByValueAttribute(
      const ast::AttributeItem &attr,
      AttributeTarget target)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-FfiPassByValue");

    AttributeValidationResult result;

    if (attr.name != attrs::kFfiPassByValue)
    {
      return result;
    }

    // Must be on record or enum
    if (target != AttributeTarget::Record && target != AttributeTarget::Enum)
    {
      result.ok = false;
      result.diag_id = "E-MOD-2452";
      result.span = attr.span;
      result.message =
          "[[ffi_pass_by_value]] is only valid on record or enum declarations";
      return result;
    }

    return result;
  }

  // Validate [[align(N)]] attribute
  // Per spec: N must be a power of two
  AttributeValidationResult ValidateAlignAttribute(const ast::AttributeItem &attr)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Align");

    AttributeValidationResult result;

    if (attr.name != attrs::kAlign)
    {
      return result;
    }

    // Must have an argument
    if (attr.args.empty())
    {
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      result.span = attr.span;
      result.message = "[[align]] requires an alignment value";
      return result;
    }

    // Check alignment is power of two (done during constant evaluation)
    // Here we just validate syntax

    return result;
  }

  // Check for conflicting attributes on the same target
  AttributeValidationResult CheckAttributeConflicts(
      const ast::AttributeList &attrs)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Conflicts");

    AttributeValidationResult result;

    bool has_inline_always = false;
    bool has_inline_never = false;
    bool has_cold = false;
    bool has_hot = false;

    for (const auto &attr : attrs)
    {
      if (attr.name == attrs::kInline)
      {
        for (const auto &arg : attr.args)
        {
          if (!arg.key.has_value())
          {
            if (const auto *token = std::get_if<ast::Token>(&arg.value))
            {
              if (token->lexeme == "always")
              {
                has_inline_always = true;
              }
              else if (token->lexeme == "never")
              {
                has_inline_never = true;
              }
            }
          }
        }
      }
      else if (attr.name == attrs::kCold)
      {
        has_cold = true;
      }
      else if (attr.name == attrs::kHot)
      {
        has_hot = true;
      }
    }

    // [[inline(always)]] and [[inline(never)]] conflict
    if (has_inline_always && has_inline_never)
    {
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      result.message =
          "Conflicting attributes: [[inline(always)]] and [[inline(never)]]";
      return result;
    }

    // [[cold]] and [[hot]] conflict
    if (has_cold && has_hot)
    {
      result.ok = false;
      result.diag_id = "E-MOD-2450";
      result.message = "Conflicting attributes: [[cold]] and [[hot]]";
      return result;
    }

    // [[inline(always)]] and [[cold]] are suspicious but allowed
    // [[inline(never)]] and [[hot]] are suspicious but allowed

    return result;
  }

  // Semantic attribute effects structure
  struct AttributeEffects
  {
    bool is_inline = false;
    bool is_inline_always = false;
    bool is_inline_never = false;
    bool is_cold = false;
    bool is_hot = false;
    bool is_export = false;
    bool has_mangle = false;
    bool is_deprecated = false;
    bool is_dynamic = false;
    bool has_layout_c = false;
    bool is_packed = false;
    std::optional<std::uint64_t> align;
    std::optional<std::string> mangle_mode;
    std::optional<std::string> deprecated_message;
  };

  // Extract semantic effects from attribute list
  AttributeEffects ExtractAttributeEffects(const ast::AttributeList &attrs)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrEffects-Extract");

    AttributeEffects effects;

    for (const auto &attr : attrs)
    {
      if (attr.name == attrs::kInline)
      {
        effects.is_inline = true;
        for (const auto &arg : attr.args)
        {
          if (!arg.key.has_value())
          {
            if (const auto *token = std::get_if<ast::Token>(&arg.value))
            {
              if (token->lexeme == "always")
              {
                effects.is_inline_always = true;
              }
              else if (token->lexeme == "never")
              {
                effects.is_inline_never = true;
              }
            }
          }
        }
      }
      else if (attr.name == attrs::kCold)
      {
        effects.is_cold = true;
      }
      else if (attr.name == attrs::kHot)
      {
        effects.is_hot = true;
      }
      else if (attr.name == attrs::kExport || attr.name == attrs::kHostExport)
      {
        effects.is_export = true;
      }
      else if (attr.name == attrs::kMangle)
      {
        effects.has_mangle = true;
        auto mode = GetAttributeValue(attrs, attrs::kMangle, "mode");
        if (!mode)
        {
          mode = GetAttributeValue(attrs, attrs::kMangle, "");
        }
        if (mode)
        {
          effects.mangle_mode = *mode;
        }
      }
      else if (attr.name == attrs::kDeprecated)
      {
        effects.is_deprecated = true;
        auto msg = GetAttributeValue(attrs, attrs::kDeprecated);
        if (msg)
        {
          effects.deprecated_message = *msg;
        }
      }
      else if (attr.name == attrs::kDynamic)
      {
        effects.is_dynamic = true;
      }
      else if (attr.name == attrs::kLayout)
      {
        auto kind = GetAttributeValue(attrs, attrs::kLayout, "kind");
        if (!kind)
        {
          kind = GetAttributeValue(attrs, attrs::kLayout, "");
        }
        if (kind && *kind == "C")
        {
          effects.has_layout_c = true;
        }
        else if (kind && *kind == "packed")
        {
          effects.is_packed = true;
        }
      }
    }

    return effects;
  }

  // Full attribute validation for a declaration
  AttributeValidationResult ValidateDeclarationAttributes(
      const ast::AttributeList &attrs,
      AttributeTarget target,
      bool has_contract)
  {
    SpecDefsAttributeValidate();
    SPEC_RULE("AttrValidate-Full");

    // First, check basic validity against registry
    auto result = ValidateAttributes(attrs, target);
    if (!result.ok)
    {
      return result;
    }

    // Check for conflicts
    result = CheckAttributeConflicts(attrs);
    if (!result.ok)
    {
      return result;
    }

    // Validate specific attributes
    for (const auto &attr : attrs)
    {
      if (attr.name == attrs::kInline)
      {
        result = ValidateInlineAttribute(attr);
      }
      else if (attr.name == attrs::kLayout)
      {
        result = ValidateLayoutAttribute(attr, target);
      }
      else if (attr.name == attrs::kDynamic)
      {
        result = ValidateDynamicAttribute(attr, has_contract);
      }
      else if (attr.name == attrs::kFfiPassByValue)
      {
        result = ValidateFfiPassByValueAttribute(attr, target);
      }
      else if (attr.name == attrs::kAlign)
      {
        result = ValidateAlignAttribute(attr);
      }

      if (!result.ok)
      {
        return result;
      }
    }

    return result;
  }

} // namespace ultraviolet::analysis
