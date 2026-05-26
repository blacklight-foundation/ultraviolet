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

static UnwindAttrCheck CheckUnwindAttr(const ast::AttributeList& attrs) {
  UnwindAttrCheck result;
  std::vector<const ast::AttributeItem*> unwind_attrs;
  for (const auto& attr : attrs) {
    if (IdEq(std::string_view(attr.name), ::ultraviolet::analysis::attrs::kUnwind)) {
      unwind_attrs.push_back(&attr);
    }
  }

  if (unwind_attrs.empty()) {
    return result;
  }

  result.has_attr = true;
  if (unwind_attrs.size() > 1) {
    result.duplicate = true;
    return result;
  }

  const ast::AttributeItem& attr = *unwind_attrs.front();
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
    diag_id = "E-SYS-3351";
    return false;
  }
  if (proc_mangle.invalid) {
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
    diag_id = "E-SYS-3355";
    return false;
  }
  if (proc_unwind.has_attr && proc_unwind.mode == "catch" &&
      block_abi != "C-unwind") {
    SPEC_RULE("UnwindMode-Invalid-Err");
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
    const auto lowered = LowerTypeWithWF(ctx, param.type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return false;
    }

    if (!FfiSafeType(ctx, lowered.type)) {
      SPEC_RULE("FfiSafe-Param-Err");
      diag_id = FfiSafeDiagForType(ctx, module_path, lowered.type)
                    .value_or("E-TYP-2623");
      return false;
    }
    if (!InferCapabilitiesFromType(ctx, module_path, lowered.type).IsEmpty()) {
      SPEC_RULE("Capability-Isolation-Err");
      diag_id = "E-TYP-2623";
      return false;
    }
    if (!FfiByValueOk(ctx, lowered.type)) {
      SPEC_RULE("ExternProc-ByValue-Err");
      diag_id = "E-TYP-2630";
      return false;
    }

    params.push_back({LowerParamMode(param.mode), lowered.type});
    proc_info.param_types.push_back(lowered.type);
  }

  const auto lowered_return = LowerTypeWithWF(ctx, proc.return_type_opt);
  if (!lowered_return.ok) {
    diag_id = lowered_return.diag_id;
    return false;
  }
  if (!FfiSafeType(ctx, lowered_return.type)) {
    SPEC_RULE("FfiSafe-Return-Err");
    diag_id = FfiSafeDiagForType(ctx, module_path, lowered_return.type)
                  .value_or("E-TYP-2623");
    return false;
  }
  if (!InferCapabilitiesFromType(ctx, module_path, lowered_return.type)
           .IsEmpty()) {
    SPEC_RULE("Capability-Isolation-Err");
    diag_id = "E-TYP-2623";
    return false;
  }
  if (!FfiByValueOk(ctx, lowered_return.type)) {
    SPEC_RULE("ExternProc-ByValue-Err");
    diag_id = "E-TYP-2630";
    return false;
  }

  proc_info.return_type = lowered_return.type;
  proc_info.func_type = MakeTypeFunc(params, lowered_return.type);
  proc_info.verification_mode = ResolveForeignVerificationMode(proc);

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

  const auto block_unwind = CheckUnwindAttr(ast::AttrListOf(block));
  if (block_unwind.duplicate) {
    SPEC_RULE("UnwindMode-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-FFI-0350";
    return result;
  }
  if (block_unwind.invalid) {
    SPEC_RULE("UnwindMode-Invalid-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }
  if (block_unwind.has_attr && block_unwind.mode == "catch" &&
      result.abi != "C-unwind") {
    SPEC_RULE("UnwindMode-Invalid-Err");
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

  const auto block_unwind = CheckUnwindAttr(ast::AttrListOf(block));
  if (block_unwind.duplicate) {
    SPEC_RULE("UnwindMode-Duplicate-Err");
    result.ok = false;
    result.diag_id = "E-FFI-0350";
    return result;
  }
  if (block_unwind.invalid) {
    SPEC_RULE("UnwindMode-Invalid-Err");
    result.ok = false;
    result.diag_id = "E-SYS-3355";
    return result;
  }
  if (block_unwind.has_attr && block_unwind.mode == "catch" &&
      result.abi != "C-unwind") {
    SPEC_RULE("UnwindMode-Invalid-Err");
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
