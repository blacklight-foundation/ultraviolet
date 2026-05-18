// =============================================================================
// MIGRATION MAPPING: records.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Section 5.2.8 "Default Record Construction" (lines 9072-9089)
//   - Fields (line 9074)
//   - InitOk (line 9076)
//   - WF-Record (lines 9078-9081)
//   - WF-Record-DupField (lines 9083-9086)
//   - DefaultConstructible (line 9088)
//   - BuiltinRecord (line 9089)
// - Section 5.3.2 "Record Methods" (lines 12156-12259)
//   - Fields(R) definition (line 12160)
//   - Methods(R) definition (line 12161)
//   - Self_R (line 12162)
//   - SelfType (line 12163)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/records.cpp
// - Lines 1-417 (entire file)
//
// Key source functions to migrate:
// - CheckRecordWf (lines 337-384): Record well-formedness check
// - TypeRecordDefaultCall (lines 386-414): Default record constructor call typing
//
// Supporting helpers:
// - RecordFields (lines 247-256): Extract fields from record declaration
// - DefaultConstructible (lines 258-266): Check if all fields have initializers
// - FullPath (lines 268-273): Build full path from module path and name
// - LookupRecordDecl (lines 275-282): Look up record declaration by path
// - RecordCalleeResult struct (lines 284-287): Callee resolution result
// - ResolveRecordCallee (lines 289-333): Resolve call callee as record type
//
// DEPENDENCIES:
// - ultraviolet/src/04_analysis/resolve/scopes.h (ScopeContext)
// - ultraviolet/src/04_analysis/resolve/scopes_lookup.h (ResolveTypeName)
// - ultraviolet/src/04_analysis/typing/subtyping.h (Subtyping)
// - ultraviolet/src/04_analysis/typing/type_equiv.h (TypeEquiv)
// - ultraviolet/src/04_analysis/typing/type_lower.h (LowerType)
// - ultraviolet/src/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// REFACTORING NOTES:
// 1. LowerType is now consolidated in type_lower.h/cpp
// 2. BuiltinRecord set includes: RegionOptions, DirEntry, Context, System
// 3. DefaultConstructible requires all fields to have initializers
// 4. Record well-formedness checks for duplicate field names
// 5. Type lowering handles generic type arguments per WF-Apply (5.2.3)
// =============================================================================

#include "04_analysis/composite/records.h"

#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsRecords() {
  SPEC_DEF("Fields", "5.3.2");
  SPEC_DEF("InitOk", "5.2.8");
  SPEC_DEF("DefaultConstructible", "5.2.8");
  SPEC_DEF("RecordPath", "5.2.8");
  SPEC_DEF("RecordCallee", "5.2.8");
}

static bool DefaultConstructible(
    const std::vector<const ast::FieldDecl*>& fields) {
  for (const auto* field : fields) {
    if (!field || !field->init_opt) {
      return false;
    }
  }
  return true;
}

static ast::Path FullPath(const ast::ModulePath& path,
                          std::string_view name) {
  ast::Path out = path;
  out.emplace_back(name);
  return out;
}

struct RecordCalleeResult {
  const ast::RecordDecl* record = nullptr;
  std::optional<ast::Path> path;
};

static RecordCalleeResult ResolveRecordCallee(const ScopeContext& ctx,
                                              const ast::ExprPtr& callee,
                                              const std::vector<ast::Arg>& args) {
  if (!callee || !args.empty()) {
    return {};
  }
  return std::visit(
      [&](const auto& node) -> RecordCalleeResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          if (const auto builtin_path =
                  LookupBuiltinRecordCtorPath(node.name);
              builtin_path.has_value()) {
            ast::Path full = *builtin_path;
            const auto* record = LookupRecordDecl(ctx, full);
            if (record) {
              return {record, std::move(full)};
            }
          }
          const auto ent = ResolveTypeName(ctx, node.name);
          if (!ent.has_value()) {
            return {};
          }
          const auto name = ent->target_opt.value_or(node.name);
          ast::ModulePath module;
          if (ent->origin_opt.has_value()) {
            module = *ent->origin_opt;
          }
          ast::Path full = FullPath(module, name);
          const auto* record = LookupRecordDecl(ctx, full);
          if (!record) {
            return {};
          }
          return {record, std::move(full)};
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          ast::Path full = FullPath(node.path, node.name);
          const auto* record = LookupRecordDecl(ctx, full);
          if (!record) {
            return {};
          }
          return {record, std::move(full)};
        }
        return {};
      },
      callee->node);
}

}  // namespace

RecordWfResult CheckRecordWf(const ScopeContext& ctx,
                             const ast::RecordDecl& record,
                             const ExprTypeFn& type_expr) {
  SpecDefsRecords();
  RecordWfResult result;
  const auto fields = RecordFields(record);
  std::unordered_set<IdKey> seen;
  seen.reserve(fields.size());
  for (const auto* field : fields) {
    if (!field) {
      continue;
    }
    const auto key = IdKeyOf(field->name);
    if (!seen.insert(key).second) {
      SPEC_RULE("WF-Record-DupField");
      result.diag_id = "E-TYP-1901";
      return result;
    }
  }

  for (const auto* field : fields) {
    if (!field || !field->init_opt) {
      continue;
    }
    const auto init_type = type_expr(field->init_opt);
    if (!init_type.ok) {
      result.diag_id = init_type.diag_id;
      return result;
    }
    const auto field_type = LowerType(ctx, field->type);
    if (!field_type.ok) {
      result.diag_id = field_type.diag_id;
      return result;
    }
    const auto sub = Subtyping(ctx, init_type.type, field_type.type);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (!sub.subtype) {
      return result;
    }
  }

  SPEC_RULE("WF-Record");
  result.ok = true;
  return result;
}

ExprTypeResult TypeRecordDefaultCall(const ScopeContext& ctx,
                                     const ast::ExprPtr& callee,
                                     const std::vector<ast::Arg>& args,
                                     const ExprTypeFn& type_expr) {
  SpecDefsRecords();
  ExprTypeResult result;
  const auto callee_result = ResolveRecordCallee(ctx, callee, args);
  if (!callee_result.record || !callee_result.path.has_value()) {
    return result;
  }

  const auto wf = CheckRecordWf(ctx, *callee_result.record, type_expr);
  if (!wf.ok) {
    result.diag_id = wf.diag_id;
    return result;
  }

  const auto fields = RecordFields(*callee_result.record);
  if (!DefaultConstructible(fields)) {
    SPEC_RULE("Record-Default-Init-Err");
    result.diag_id = "Record-Default-Init-Err";
    return result;
  }

  SPEC_RULE("T-Record-Default");
  result.ok = true;
  result.type = MakeTypePath(*callee_result.path);
  return result;
}

}  // namespace ultraviolet::analysis
