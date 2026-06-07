// =============================================================================
// MIGRATION: item/extern_block.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 18: Foreign Function Interface
//   - extern block grammar
//   - FfiSafe types
//   - Foreign contracts (@foreign_assumes, @foreign_ensures)
//   - ABI strings ("C", "C-unwind", etc.)
//   - Capability isolation
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/ffi_by_value.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_predicates.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/attributes/ffi_library_attrs.h"
#include "04_analysis/contracts/contract_check.h"
#include "01_project/ffi_library.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsExternBlock() {
  SPEC_DEF("WF-ExternProcDecl", "5.2.14");
  SPEC_DEF("WF-ExternProcDecl-MissingReturnType", "5.2.14");
  SPEC_DEF("ExternProc-Generic-Err", "5.2.14");
  SPEC_DEF("ExternProc-ByValue-Err", "5.2.14");
  SPEC_DEF("WF-ExternBlock", "5.2.14");
  SPEC_DEF("ExternAbi-Unknown-Err", "5.2.14");
  SPEC_DEF("FfiSafe", "18.1");
  SPEC_DEF("ABI-Valid", "18.2");
  SPEC_DEF("Foreign-Contract", "18.3");
  SPEC_DEF("Capability-Isolation", "18.4");
}

// =============================================================================
// HELPERS
// =============================================================================

static void RecordExternProcedureTypeAdmissibilityObligations();

static void RecordFfiConformance(std::string_view rule_id,
                                 std::string_view payload) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  core::Conformance::Record(rule_id, std::nullopt, payload);
}

static void RecordExternAbiConformance(std::string_view abi) {
  std::string payload =
      "source=TypeExternBlock;abi_set=C,C-unwind,system,stdcall,fastcall,"
      "vectorcall;selected_abi=";
  payload += abi;
  payload += ";target_profile_checked=true;supported=true";
  RecordFfiConformance("def.23.ExternAbiStrings", payload);
}

static void RecordExternSignatureConformance(
    std::string_view source,
    std::string_view block_abi,
    const ast::ExternProcDecl& proc) {
  std::string payload;
  payload.reserve(proc.name.size() + block_abi.size() + 256);
  payload += "source=";
  payload += source;
  payload += ";boundary=extern_import;abi=";
  payload += block_abi;
  payload += ";procedure=";
  payload += proc.name;
  payload += ";param_count=";
  payload += std::to_string(proc.params.size());
  payload +=
      ";return_annotation=true;generic_params=false;ffi_safe=true;"
      "by_value=true;capability_values_exposed=false;"
      "region_local_raw_ptrs_rejected=true";

  RecordFfiConformance("def.23.ExternSignatureRequirements", payload);
  RecordFfiConformance("requirement.23.ExternFfiConstraints", payload);
  RecordFfiConformance("requirement.23.CapabilityIsolationSemantics", payload);
}

static void RecordCapabilityIsolationSurfaceConformance(
    std::string_view source,
    std::string_view boundary) {
  std::string payload;
  payload.reserve(source.size() + boundary.size() + 240);
  payload += "source=";
  payload += source;
  payload += ";boundary=";
  payload += boundary;
  payload +=
      ";syntax=none;parser=existing_ffi_declarations;"
      "dedicated_ast_nodes=false;semantic_gate=signature_admissibility;"
      "helpers=RegionLocalProv,RawPtrType,FFICall";

  RecordFfiConformance(
      "requirement.23.CapabilityIsolationSyntaxNoAdditionalForm", payload);
  RecordFfiConformance(
      "requirement.23.CapabilityIsolationParsingNoAdditionalRules", payload);
  RecordFfiConformance("ast.23.CapabilityIsolationNoDedicatedAst", payload);
  RecordFfiConformance("def.23.CapabilityIsolationHelpers", payload);
}

static LowerTypeResult LowerExternSignatureType(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (wf.ok) {
    return lowered;
  }

  const auto ffi_diag = FfiSafeDiagForType(ctx, module_path, lowered.type);
  if (ffi_diag.has_value() && *ffi_diag == "E-TYP-2629") {
    RecordExternProcedureTypeAdmissibilityObligations();
    return {false, ffi_diag, lowered.type};
  }
  return {false, wf.diag_id, {}};
}

// Valid ABI strings
static bool IsValidABI(std::string_view abi) {
  return abi == "C" ||
         abi == "C-unwind" ||
         abi == "system" ||
         abi == "stdcall" ||
         abi == "fastcall" ||
         abi == "vectorcall";
}

static bool IsSupportedABIForProfile(std::string_view abi,
                                     project::TargetProfile profile) {
  if (abi == "stdcall" || abi == "fastcall" || abi == "vectorcall") {
    return profile == project::TargetProfile::X86_64Win64;
  }
  return true;
}

static ForeignVerificationMode ToForeignVerificationMode(
    VerificationModeAttribute mode) {
  switch (mode) {
    case VerificationModeAttribute::Static:
      return ForeignVerificationMode::Static;
    case VerificationModeAttribute::Dynamic:
      return ForeignVerificationMode::Dynamic;
  }
  return ForeignVerificationMode::Static;
}

static ForeignVerificationMode ResolveForeignVerificationMode(
    const ast::ExternProcDecl& proc) {
  const auto proc_attr_mode = ResolveVerificationModeAttribute(proc.attrs);
  return proc_attr_mode.has_value() ? ToForeignVerificationMode(*proc_attr_mode)
                                    : ForeignVerificationMode::Static;
}

// Extract ABI string from ExternAbi variant
static std::string NormalizeAbiLiteral(std::string abi) {
  if (abi.size() >= 2 &&
      ((abi.front() == '"' && abi.back() == '"') ||
       (abi.front() == '\'' && abi.back() == '\''))) {
    return abi.substr(1, abi.size() - 2);
  }
  return abi;
}

struct UnwindAttrCheck {
  bool has_attr = false;
  bool duplicate = false;
  bool invalid = false;
  std::string mode;
};

struct ForeignPredicateValidation {
  bool ok = true;
  std::string_view diag_id;
};

static bool ForeignPredicateNameAllowed(
    std::string_view name,
    const std::vector<std::string_view>& allowed_names) {
  for (const auto allowed : allowed_names) {
    if (IdEq(name, allowed)) {
      return true;
    }
  }
  return false;
}

static ForeignPredicateValidation ValidateForeignPredicateExpr(
    const ast::ExprPtr& expr,
    const std::vector<std::string_view>& allowed_names,
    bool allow_result,
    std::string_view impurity_diag) {
  if (!expr) {
    return {};
  }

  return std::visit(
      [&](const auto& node) -> ForeignPredicateValidation {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                      std::is_same_v<T, ast::PtrNullExpr> ||
                      std::is_same_v<T, ast::TupleExpr>) {
          return {};
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (ForeignPredicateNameAllowed(node.name, allowed_names)) {
            return {};
          }
          return {false, "E-SEM-2852"};
        } else if constexpr (std::is_same_v<T, ast::ResultExpr>) {
          if (allow_result) {
            return {};
          }
          return {false, "E-SEM-2854"};
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          const auto lhs =
              ValidateForeignPredicateExpr(
                  node.lhs, allowed_names, allow_result, impurity_diag);
          if (!lhs.ok) {
            return lhs;
          }
          return ValidateForeignPredicateExpr(
              node.rhs, allowed_names, allow_result, impurity_diag);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ValidateForeignPredicateExpr(
              node.value, allowed_names, allow_result, impurity_diag);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ValidateForeignPredicateExpr(
              node.base, allowed_names, allow_result, impurity_diag);
        } else {
          return {false, impurity_diag};
        }
      },
      expr->node);
}

static void RecordFfiSafeFailureObligations(const ScopeContext& ctx,
                                            const ast::ModulePath& module_path,
                                            const TypeRef& type,
                                            std::string_view diag_id) {
  if (diag_id != "E-TYP-2623") {
    return;
  }
  SPEC_RULE("FfiSafe-Prohibited-Err");
  SPEC_RULE("rule.23.FfiSafe-Prohibited-Err");
  if (!InferCapabilitiesFromType(ctx, module_path, type).IsEmpty()) {
    SPEC_RULE("diagnostics.23.CapabilityIsolationDiagnosticOwnership");
  }
}

static void RecordExternProcedureTypeAdmissibilityObligations() {
  SPEC_RULE("def.23.ExternSignatureRequirements");
  SPEC_RULE("requirement.23.ExternFfiConstraints");
  SPEC_RULE("diagnostics.23.ExternProcedureDiagnosticOwnership");
}

static void RecordExternProcedureByValueFailureObligations() {
  RecordExternProcedureTypeAdmissibilityObligations();
  SPEC_RULE("def.23.FfiByValueHelpers");
  SPEC_RULE("requirement.23.FfiSafeRaiiByValueRule");
  SPEC_RULE("requirement.23.FfiPassByValueAttributeSemantics");
}

static UnwindAttrCheck CheckUnwindAttr(const ast::AttributeList& attrs) {
  SPEC_RULE("def.23.DetermineUnwindMode");
  UnwindAttrCheck result;
  std::vector<const ast::AttributeItem*> unwind_attrs;
  for (const auto& attr : attrs) {
    if (IdEq(std::string_view(attr.name), ::ultraviolet::analysis::attrs::kUnwind)) {
      unwind_attrs.push_back(&attr);
    }
  }

  RecordFfiConformance(
      "def.23.UnwindModes",
      "source=CheckUnwindAttr;modes=abort,catch;default=abort");
  if (unwind_attrs.empty()) {
    RecordFfiConformance(
        "requirement.23.UnwindDefaultMode",
        "source=CheckUnwindAttr;attr_present=false;mode=abort");
    RecordFfiConformance(
        "def.23.DetermineUnwindMode",
        "source=CheckUnwindAttr;default=abort;explicit=none");
    return result;
  }

  RecordFfiConformance(
      "requirement.23.BoundaryUnwindingSyntax",
      "source=CheckUnwindAttr;syntax=unwind_attribute;argument=string_literal;"
      "additional_forms=false");
  RecordFfiConformance(
      "requirement.23.BoundaryUnwindingParsingNoAdditionalRules",
      "source=CheckUnwindAttr;parser=attribute_list;additional_rules=false");
  RecordFfiConformance(
      "ast.23.BoundaryUnwindPolicySource",
      "source=CheckUnwindAttr;policy_source=unwind_attribute");
  RecordFfiConformance(
      "def.23.UnwindModeAstHelpers",
      "source=CheckUnwindAttr;helpers=DetermineUnwindMode,ParseUnwindArg");
  RecordFfiConformance(
      "def.23.DetermineUnwindMode",
      "source=CheckUnwindAttr;default=abort;explicit=attribute_value");

  result.has_attr = true;
  if (unwind_attrs.size() > 1) {
    result.duplicate = true;
    return result;
  }

  const ast::AttributeItem& attr = *unwind_attrs.front();
  SPEC_RULE("def.23.ParseUnwindArg");
  RecordFfiConformance(
      "def.23.ParseUnwindArg",
      "source=CheckUnwindAttr;argument=string_literal;valid_values=abort,catch");
  if (attr.args.size() != 1 || attr.args.front().key.has_value()) {
    result.invalid = true;
    return result;
  }

  const auto* token = std::get_if<ast::Token>(&attr.args.front().value);
  if (!token || token->kind != lexer::TokenKind::StringLiteral) {
    result.invalid = true;
    return result;
  }

  const std::string mode = NormalizeAbiLiteral(token->lexeme);
  if (mode != "abort" && mode != "catch") {
    result.invalid = true;
    return result;
  }

  result.mode = mode;
  RecordFfiConformance(
      "rule.23.UnwindMode-Valid",
      mode == "catch"
          ? "source=CheckUnwindAttr;mode=catch;valid=true"
          : "source=CheckUnwindAttr;mode=abort;valid=true");
  return result;
}

struct MangleAttrCheck {
  bool has_attr = false;
  bool invalid = false;
  bool conflicting = false;
  bool none_mode = false;
  std::string explicit_name;
};

static MangleAttrCheck CheckMangleAttr(const ast::AttributeList& attrs_list) {
  MangleAttrCheck check;
  bool has_none_mode = false;
  std::optional<std::string> symbol_mode_value;

  for (const auto& attr : attrs_list) {
    if (!IdEq(std::string_view(attr.name), ::ultraviolet::analysis::attrs::kMangle)) {
      continue;
    }

    check.has_attr = true;
    if (attr.args.size() != 1) {
      check.invalid = true;
      return check;
    }

    const auto& arg = attr.args.front();
    if (arg.key.has_value() && *arg.key != "mode") {
      check.invalid = true;
      return check;
    }

    const auto* token = std::get_if<ast::Token>(&arg.value);
    if (!token) {
      check.invalid = true;
      return check;
    }
    const std::string raw = NormalizeAbiLiteral(token->lexeme);
    if (raw.empty()) {
      check.invalid = true;
      return check;
    }
    if (raw == "none" && token->kind != lexer::TokenKind::StringLiteral) {
      if (symbol_mode_value.has_value()) {
        check.conflicting = true;
        return check;
      }
      has_none_mode = true;
      continue;
    }
    if (token->kind == lexer::TokenKind::StringLiteral) {
      if (has_none_mode) {
        check.conflicting = true;
        return check;
      }
      if (symbol_mode_value.has_value() && *symbol_mode_value != raw) {
        check.conflicting = true;
        return check;
      }
      symbol_mode_value = raw;
      continue;
    }
    check.invalid = true;
    return check;
  }

  if (has_none_mode) {
    check.none_mode = true;
    return check;
  }
  if (symbol_mode_value.has_value()) {
    check.explicit_name = *symbol_mode_value;
  }
  return check;
}

static std::string ExtractAbiString(const std::optional<ast::ExternAbi>& abi_opt) {
  if (!abi_opt.has_value()) {
    return "C";  // Default ABI
  }
  return std::visit(
      [](const auto& abi) -> std::string {
        using T = std::decay_t<decltype(abi)>;
        if constexpr (std::is_same_v<T, ast::ExternAbiString>) {
          return NormalizeAbiLiteral(abi.literal.lexeme);
        } else {
          return abi.name;
        }
      },
      *abi_opt);
}

constexpr std::string_view kMissingTargetProfileDiag =
    "Internal-MissingTargetProfile";

static std::optional<project::TargetProfile> RequireExternTargetProfile(
    const ScopeContext& ctx,
    std::optional<std::string_view>& diag_id) {
  const auto profile = RequireSelectedTargetProfile(ctx);
  if (!profile.has_value()) {
    diag_id = kMissingTargetProfileDiag;
  }
  return profile;
}

static bool ValidateLibraryKindsForCurrentTarget(
    const ScopeContext& ctx,
    const ast::ExternBlock& block,
    ExternBlockResult& result) {
  const auto profile = RequireExternTargetProfile(ctx, result.diag_id);
  if (!profile.has_value()) {
    result.ok = false;
    return false;
  }
  for (const auto& attr : ast::AttrListOf(block)) {
    const auto library = NormalizeLibraryAttribute(attr);
    if (!library.has_value()) {
      continue;
    }
    if (project::IsLibraryKindSupportedForCurrentTarget(library->kind,
                                                       *profile)) {
      continue;
    }
    SPEC_RULE("requirement.23.UnsupportedLibraryKindIllFormed");
    result.ok = false;
    result.diag_id = "E-SYS-3346";
    return false;
  }
  return true;
}

static bool BuildExternProcInfo(const ScopeContext& ctx,
                                const ast::ExternBlock& block,
                                const ast::ModulePath& module_path,
                                std::string_view block_abi,
                                const ast::ExternProcDecl& proc,
                                ExternProcInfo& proc_info,
                                std::optional<std::string_view>& diag_id) {
  const auto proc_attr_validation =
      ValidateAttributes(proc.attrs, AttributeTarget::Procedure);
  if (!proc_attr_validation.ok) {
    diag_id = proc_attr_validation.diag_id;
    return false;
  }

  const auto proc_mangle = CheckMangleAttr(proc.attrs);
  if (proc_mangle.conflicting) {
    SPEC_RULE("requirement.23.FfiAttributeConstraints");
    diag_id = "E-SYS-3351";
    return false;
  }
  if (proc_mangle.invalid) {
    SPEC_RULE("requirement.23.FfiAttributeConstraints");
    diag_id = "E-SYS-3341";
    return false;
  }

  const auto proc_unwind = CheckUnwindAttr(proc.attrs);
  if (proc_unwind.duplicate) {
    SPEC_RULE("UnwindMode-Duplicate-Err");
    diag_id = "E-FFI-0350";
    return false;
  }
  if (proc_unwind.invalid) {
    SPEC_RULE("UnwindMode-Invalid-Err");
    SPEC_RULE("rule.23.UnwindMode-Invalid-Err");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    diag_id = "E-SYS-3355";
    return false;
  }
  if (proc_unwind.has_attr && proc_unwind.mode == "catch" &&
      block_abi != "C-unwind") {
    SPEC_RULE("requirement.23.UnwindCatchAbiRequirement");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    diag_id = "E-SYS-3355";
    return false;
  }

  proc_info.name = proc.name;

  if (!proc.return_type_opt) {
    SPEC_RULE("WF-ExternProcDecl-MissingReturnType");
    diag_id = "WF-ExternProcDecl-MissingReturnType";
    return false;
  }

  if (!ast::TypeParamsOpt(proc.generic_params).empty()) {
    SPEC_RULE("ExternProc-Generic-Err");
    diag_id = "E-TYP-2306";
    return false;
  }

  std::vector<TypeFuncParam> params;
  params.reserve(proc.params.size());
  proc_info.param_types.reserve(proc.params.size());
  for (const auto& param : proc.params) {
    const auto lowered = LowerExternSignatureType(ctx, module_path, param.type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return false;
    }

    if (!FfiSafeType(ctx, lowered.type)) {
      SPEC_RULE("FfiSafe-Param-Err");
      RecordExternProcedureTypeAdmissibilityObligations();
      diag_id = FfiSafeDiagForType(ctx, module_path, lowered.type)
                    .value_or("E-TYP-2623");
      RecordFfiSafeFailureObligations(ctx, module_path, lowered.type, *diag_id);
      return false;
    }
    if (!InferCapabilitiesFromType(ctx, module_path, lowered.type).IsEmpty()) {
      SPEC_RULE("Capability-Isolation-Err");
      SPEC_RULE("FfiSafe-Prohibited-Err");
      SPEC_RULE("rule.23.FfiSafe-Prohibited-Err");
      SPEC_RULE("diagnostics.23.CapabilityIsolationDiagnosticOwnership");
      diag_id = "E-TYP-2623";
      return false;
    }
    if (!FfiByValueOk(ctx, lowered.type)) {
      SPEC_RULE("ExternProc-ByValue-Err");
      RecordExternProcedureByValueFailureObligations();
      diag_id = "E-TYP-2630";
      return false;
    }

    params.push_back({LowerParamMode(param.mode), lowered.type});
    proc_info.param_types.push_back(lowered.type);
  }

  const auto lowered_return =
      LowerExternSignatureType(ctx, module_path, proc.return_type_opt);
  if (!lowered_return.ok) {
    diag_id = lowered_return.diag_id;
    return false;
  }
  if (!FfiSafeType(ctx, lowered_return.type)) {
    SPEC_RULE("FfiSafe-Return-Err");
    RecordExternProcedureTypeAdmissibilityObligations();
    diag_id = FfiSafeDiagForType(ctx, module_path, lowered_return.type)
                  .value_or("E-TYP-2623");
    RecordFfiSafeFailureObligations(
        ctx, module_path, lowered_return.type, *diag_id);
    return false;
  }
  if (!InferCapabilitiesFromType(ctx, module_path, lowered_return.type)
           .IsEmpty()) {
    SPEC_RULE("Capability-Isolation-Err");
    SPEC_RULE("FfiSafe-Prohibited-Err");
    SPEC_RULE("rule.23.FfiSafe-Prohibited-Err");
    SPEC_RULE("diagnostics.23.CapabilityIsolationDiagnosticOwnership");
    diag_id = "E-TYP-2623";
    return false;
  }
  if (!FfiByValueOk(ctx, lowered_return.type)) {
    SPEC_RULE("ExternProc-ByValue-Err");
    RecordExternProcedureByValueFailureObligations();
    diag_id = "E-TYP-2630";
    return false;
  }

  proc_info.return_type = lowered_return.type;
  proc_info.func_type = MakeTypeFunc(params, lowered_return.type);
  proc_info.verification_mode = ResolveForeignVerificationMode(proc);
  RecordCapabilityIsolationSurfaceConformance(
      "BuildExternProcInfo", "extern_import");
  RecordExternSignatureConformance("BuildExternProcInfo", block_abi, proc);

  if (proc.foreign_contracts_opt.has_value()) {
    std::vector<std::string_view> foreign_predicate_params;
    foreign_predicate_params.reserve(proc.params.size());
    for (const auto& param : proc.params) {
      foreign_predicate_params.push_back(param.name);
    }

    for (const auto& clause : *proc.foreign_contracts_opt) {
      const bool is_assumes =
          clause.kind == ast::ForeignContractKind::Assumes;
      const std::string_view impurity_diag =
          is_assumes ? "E-SEM-2851" : "E-SEM-2853";
      for (const auto& predicate : clause.predicates) {
        const auto validation = ValidateForeignPredicateExpr(
            predicate,
            foreign_predicate_params,
            !is_assumes,
            impurity_diag);
        if (!validation.ok) {
          if (validation.diag_id == "E-SEM-2851" ||
              validation.diag_id == "E-SEM-2853") {
            SPEC_RULE("requirement.23.ForeignPredicateContext");
          }
          if (!is_assumes && validation.diag_id == "E-SEM-2852") {
            SPEC_RULE("requirement.23.ForeignPostconditionPredicateBindings");
          }
          diag_id = validation.diag_id;
          return false;
        }
      }

      switch (clause.kind) {
        case ast::ForeignContractKind::Assumes: {
          const auto assume_result = ResolveForeignAssumes(clause);
          if (!assume_result.ok) {
            diag_id = assume_result.diag_id;
            return false;
          }
          break;
        }
        case ast::ForeignContractKind::Ensures:
        case ast::ForeignContractKind::EnsuresError:
        case ast::ForeignContractKind::EnsuresNullResult: {
          const auto ensure_result =
              ResolveForeignEnsures(clause, lowered_return.type);
          if (!ensure_result.ok) {
            diag_id = ensure_result.diag_id;
            return false;
          }
          break;
        }
      }
    }
  }

  SPEC_RULE("WF-ExternProcDecl");
  return true;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeExternBlock
// =============================================================================

ExternBlockResult TypeExternBlock(
    const ScopeContext& ctx,
    const ast::ExternBlock& block,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsExternBlock();
  ExternBlockResult result;
  result.ok = true;
  result.abi = ExtractAbiString(block.abi_opt);

  const auto attr_validation = ValidateAttributes(
      ast::AttrListOf(block), AttributeTarget::ExternBlock);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }
  if (!ValidateLibraryKindsForCurrentTarget(ctx, block, result)) {
    return result;
  }

  // Validate ABI string
  if (!IsValidABI(result.abi)) {
    SPEC_RULE("ExternAbi-Unknown-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3352";
    return result;
  }
  const auto profile = RequireExternTargetProfile(ctx, result.diag_id);
  if (!profile.has_value()) {
    result.ok = false;
    return result;
  }
  if (!IsSupportedABIForProfile(result.abi, *profile)) {
    SPEC_RULE("ExternAbi-Unknown-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3352";
    return result;
  }
  RecordExternAbiConformance(result.abi);

  const auto block_unwind = CheckUnwindAttr(ast::AttrListOf(block));
  if (block_unwind.duplicate) {
    SPEC_RULE("UnwindMode-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-FFI-0350";
    return result;
  }
  if (block_unwind.invalid) {
    SPEC_RULE("UnwindMode-Invalid-Err");
    SPEC_RULE("rule.23.UnwindMode-Invalid-Err");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }
  if (block_unwind.has_attr && block_unwind.mode == "catch" &&
      result.abi != "C-unwind") {
    SPEC_RULE("requirement.23.UnwindCatchAbiRequirement");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }

  // Process each extern item
  for (const auto& item : block.items) {
    // Extract procedure from ExternItem variant
    const auto* proc = std::get_if<ast::ExternProcDecl>(&item);
    if (!proc) {
      continue;  // Only handle procedures for now
    }

    ExternProcInfo proc_info;
    if (!BuildExternProcInfo(
            ctx, block, module_path, result.abi, *proc, proc_info,
            result.diag_id)) {
      result.ok = false;
      return result;
    }

    result.procedures.push_back(proc_info);
  }
  (void)diags;

  SPEC_RULE("WF-ExternBlock");
  return result;
}

// =============================================================================
// EXPORTED: TypeExternBlockSignature (first pass)
// =============================================================================

ExternBlockResult TypeExternBlockSignature(
    const ScopeContext& ctx,
    const ast::ExternBlock& block,
    const ast::ModulePath& module_path) {
  SpecDefsExternBlock();
  ExternBlockResult result;
  result.ok = true;
  result.abi = ExtractAbiString(block.abi_opt);

  const auto attr_validation = ValidateAttributes(
      ast::AttrListOf(block), AttributeTarget::ExternBlock);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }
  if (!ValidateLibraryKindsForCurrentTarget(ctx, block, result)) {
    return result;
  }

  // Validate ABI
  if (!IsValidABI(result.abi)) {
    SPEC_RULE("ExternAbi-Unknown-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3352";
    return result;
  }
  const auto profile = RequireExternTargetProfile(ctx, result.diag_id);
  if (!profile.has_value()) {
    result.ok = false;
    return result;
  }
  if (!IsSupportedABIForProfile(result.abi, *profile)) {
    SPEC_RULE("ExternAbi-Unknown-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3352";
    return result;
  }
  RecordExternAbiConformance(result.abi);

  const auto block_unwind = CheckUnwindAttr(ast::AttrListOf(block));
  if (block_unwind.duplicate) {
    SPEC_RULE("UnwindMode-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-FFI-0350";
    return result;
  }
  if (block_unwind.invalid) {
    SPEC_RULE("UnwindMode-Invalid-Err");
    SPEC_RULE("rule.23.UnwindMode-Invalid-Err");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }
  if (block_unwind.has_attr && block_unwind.mode == "catch" &&
      result.abi != "C-unwind") {
    SPEC_RULE("requirement.23.UnwindCatchAbiRequirement");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingDiagnosticOwnership");
    SPEC_RULE("diagnostics.23.BoundaryUnwindingNoAdditionalDiagnostics");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }

  // Process procedure signatures
  for (const auto& item : block.items) {
    const auto* proc = std::get_if<ast::ExternProcDecl>(&item);
    if (!proc) {
      continue;
    }

    ExternProcInfo proc_info;
    if (!BuildExternProcInfo(
            ctx, block, module_path, result.abi, *proc, proc_info,
            result.diag_id)) {
      result.ok = false;
      return result;
    }
    result.procedures.push_back(proc_info);
  }

  SPEC_RULE("WF-ExternBlock");
  return result;
}

}  // namespace ultraviolet::analysis
