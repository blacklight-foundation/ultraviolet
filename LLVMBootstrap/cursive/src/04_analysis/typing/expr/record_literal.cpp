// =================================================================
// File: 04_analysis/typing/expr/record_literal.cpp
// Construct: Record Literal Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Record-Literal, T-Modal-State-Intro, Record-FieldInit-Dup,
//             Record-Field-Unknown, Record-Field-NotVisible,
//             Record-FieldInit-Missing, Record-Field-NonBitcopy-Move
// =================================================================
#include "04_analysis/typing/expr/record_literal.h"

#include <unordered_map>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/deprecation_warnings.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/type_infer.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsRecordLiteral() {
  SPEC_DEF("T-Record-Literal", "5.2.12");
  SPEC_DEF("T-Modal-State-Intro", "5.4");
  SPEC_DEF("WF-ModalState", "5.4");
  SPEC_DEF("WF-ModalState-ArgCount-Err", "13.1.4");
  SPEC_DEF("ModalRefSubst", "13.1.3");
  SPEC_DEF("ModalPayload", "13.1.3");
  SPEC_DEF("ModalPayloadMap", "13.1.3");
  SPEC_DEF("State-Specific-WF", "5.4");
  SPEC_DEF("Record-FieldInit-Dup", "5.2.12");
  SPEC_DEF("Record-Field-Unknown", "5.2.12");
  SPEC_DEF("Record-Field-NotVisible", "5.2.12");
  SPEC_DEF("Record-FieldInit-Missing", "5.2.12");
  SPEC_DEF("Record-Field-NonBitcopy-Move", "5.2.12");
  SPEC_DEF("Record-FileDir-Err", "5.2.12");
}

static const ast::ModalDecl* LookupModalDeclForLiteral(
    const ScopeContext& ctx,
    const TypePath& path,
    TypePath& resolved_path) {
  const TypeDecl* decl = LookupTypeDecl(ctx, path, &resolved_path);
  if (!decl) {
    return nullptr;
  }
  return std::get_if<ast::ModalDecl>(decl);
}

}  // namespace

// §5.2.12 Record Literal Expression Typing
//
// Typing rule:
// RecordDecl(path) = R
// For each (f, v) in initializers:
//   FieldType(R, f) = T_f
//   FieldVisible(context, R, f)
//   Gamma |- v : T_v
//   T_v <: T_f
// All required fields covered
// TypeInvariant(R) holds
// --------------------------------------------------
// Gamma |- RecordName{ fields } : RecordType
//
ExprTypeResult TypeRecordExprImpl(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::RecordExpr& expr,
                                  const TypeEnv& env) {
  ExprTypeResult result;

  // Handle modal state construction: Modal@State{ fields }
  if (const auto* modal = std::get_if<ast::ModalStateRef>(&expr.target)) {
    // Built-in runtime-backed modal states that cannot be constructed directly.
    if (IsBuiltinModalRecordLiteralForbidden(modal->path)) {
      SPEC_RULE("Record-FileDir-Err");
      result.diag_id = "E-TYP-2073";
      return result;
    }

    TypePath resolved_modal_path;
    const auto* decl =
        LookupModalDeclForLiteral(ctx, modal->path, resolved_modal_path);
    if (!decl) {
      return result;
    }
    const std::optional<core::Span> ref_span =
        !expr.fields.empty() && expr.fields.front().value
            ? std::optional<core::Span>(expr.fields.front().value->span)
            : std::nullopt;
    EmitDeprecatedReferenceWarningFromAttrs(
        decl->attrs, type_ctx, ref_span);

    // Find the state in the modal
    const auto* state = LookupModalState(*decl, modal->state);
    if (!state) {
      return result;
    }

    SPEC_RULE("WF-ModalState");
    SPEC_RULE("State-Specific-WF");

    // Lower modal generic arguments and build substitution for payload types.
    std::vector<TypeRef> lowered_args;
    lowered_args.reserve(modal->generic_args.size());
    for (const auto& arg : modal->generic_args) {
      const auto lowered = LowerType(ctx, arg);
      if (!lowered.ok) {
        result.diag_id = lowered.diag_id;
        return result;
      }
      lowered_args.push_back(lowered.type);
    }
    TypeSubst modal_subst;
    if (decl->generic_params.has_value()) {
      const auto provided = lowered_args.size();
      const auto required = RequiredParamCount(decl->generic_params);
      const auto total = TotalParamCount(decl->generic_params);
      if (provided < required || provided > total) {
        SPEC_RULE("WF-ModalState-ArgCount-Err");
        result.diag_id = "E-TYP-2303";
        return result;
      }
      modal_subst = BuildModalRefSubstitution(
          decl->generic_params->params, lowered_args);
    } else if (!lowered_args.empty()) {
      SPEC_RULE("WF-ModalState-ArgCount-Err");
      result.diag_id = "E-TYP-2303";
      return result;
    }

    // Check for duplicate field initializers
    std::unordered_set<IdKey> seen;
    for (const auto& field_init : expr.fields) {
      const auto key = IdKeyOf(field_init.name);
      if (!seen.insert(key).second) {
        SPEC_RULE("Record-FieldInit-Dup");
        result.diag_id = "E-TYP-1903";
        return result;
      }
    }

    // Build a map of payload fields from state
    SPEC_RULE("ModalPayloadMap");
    std::unordered_map<IdKey, const ast::StateFieldDecl*> payload_fields;
    for (const auto& member : state->members) {
      if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
        payload_fields.emplace(IdKeyOf(field->name), field);
      }
    }

    // Check that all provided fields exist
    for (const auto& field_init : expr.fields) {
      if (payload_fields.find(IdKeyOf(field_init.name)) == payload_fields.end()) {
        SPEC_RULE("Record-Field-Unknown");
        result.diag_id = "E-TYP-1904";
        return result;
      }
    }

    // Check that all required fields are provided
    for (const auto& member : state->members) {
      if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
        const auto key = IdKeyOf(field->name);
        if (seen.find(key) == seen.end()) {
          SPEC_RULE("Record-FieldInit-Missing");
          result.diag_id = "E-TYP-1902";
          return result;
        }
      }
    }

    for (const auto& field_init : expr.fields) {
      const auto it = payload_fields.find(IdKeyOf(field_init.name));
      if (it == payload_fields.end() || !it->second) {
        return result;
      }
      const auto lowered = LowerType(ctx, it->second->type);
      if (!lowered.ok) {
        result.diag_id = lowered.diag_id;
        return result;
      }
      TypeRef field_type = lowered.type;
      SPEC_RULE("ModalPayload");
      field_type = InstantiateType(field_type, modal_subst);
      const auto check =
          CheckExprAgainst(ctx, type_ctx, field_init.value, field_type, env);
      if (!check.ok) {
        result.diag_id = check.diag_id;
        result.diag_detail = check.diag_detail;
        result.diag_span = check.diag_span;
        return result;
      }
    }

    SPEC_RULE("T-Modal-State-Intro");

    result.ok = true;
    result.type = MakeTypeModalState(
        std::move(resolved_modal_path), modal->state, std::move(lowered_args));
    return result;
  }

  const auto* path = std::get_if<ast::TypePath>(&expr.target);
  if (!path) {
    return result;
  }
  const TypePath& type_path = *path;

  // Lookup the record declaration
  const auto* record = LookupRecordDecl(ctx, type_path);
  if (!record) {
    return result;
  }
  const std::optional<core::Span> ref_span =
      !expr.fields.empty() && expr.fields.front().value
          ? std::optional<core::Span>(expr.fields.front().value->span)
          : std::nullopt;
  EmitDeprecatedReferenceWarningFromAttrs(
      record->attrs, type_ctx, ref_span);

  // Check for duplicate field initializers
  std::unordered_set<IdKey> seen;
  for (const auto& field_init : expr.fields) {
    const auto key = IdKeyOf(field_init.name);
    if (!seen.insert(key).second) {
      SPEC_RULE("Record-FieldInit-Dup");
      result.diag_id = "E-TYP-1903";
      return result;
    }
  }

  // Check that all provided fields exist and are visible
  for (const auto& field_init : expr.fields) {
    if (!FieldExists(*record, field_init.name)) {
      SPEC_RULE("Record-Field-Unknown");
      result.diag_id = "E-TYP-1904";
      return result;
    }
    if (!FieldVisible(ctx, *record, field_init.name, type_path)) {
      SPEC_RULE("Record-Field-NotVisible");
      result.diag_id = "E-TYP-1905";
      return result;
    }
  }

  // Check that all required fields are provided
  std::unordered_set<IdKey> provided;
  for (const auto& field_init : expr.fields) {
    provided.insert(IdKeyOf(field_init.name));
  }
  for (const auto* field : RecordFields(*record)) {
    if (!field) {
      continue;
    }
    if (provided.find(IdKeyOf(field->name)) == provided.end()) {
      SPEC_RULE("Record-FieldInit-Missing");
      result.diag_id = "E-TYP-1902";
      return result;
    }
  }

  // Type-check each field initializer
  for (const auto& field_init : expr.fields) {
    auto field_type_opt = FieldType(*record, field_init.name, ctx);
    if (!field_type_opt.has_value()) {
      return result;
    }

    // Check that non-Bitcopy fields use move
    if (!BitcopyType(ctx, *field_type_opt) && IsPlaceExpr(field_init.value) &&
        (!field_init.value ||
         !std::holds_alternative<ast::MoveExpr>(field_init.value->node))) {
      SPEC_RULE("Record-Field-NonBitcopy-Move");
      result.diag_id = "E-TYP-1907";
      return result;
    }

    const auto check = CheckExprAgainst(ctx, type_ctx, field_init.value,
                                        *field_type_opt, env);
    if (!check.ok) {
      result.diag_id = check.diag_id;
      result.diag_detail = check.diag_detail;
      result.diag_span = check.diag_span;
      return result;
    }
  }

  SPEC_RULE("T-Record-Literal");

  result.ok = true;
  result.type = MakeTypePath(type_path);
  return result;
}

}  // namespace cursive::analysis::expr
