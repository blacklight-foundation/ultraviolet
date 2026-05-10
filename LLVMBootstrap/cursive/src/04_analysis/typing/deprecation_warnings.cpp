#include "04_analysis/typing/deprecation_warnings.h"

#include <string>

#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"

namespace cursive::analysis {

namespace {

std::string NormalizeAttrLiteralLocal(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

}  // namespace

void EmitDeprecatedReferenceWarningFromAttrs(
    const ast::AttributeList& attrs_list,
    const StmtTypeContext& type_ctx,
    const std::optional<core::Span>& span) {
  if (!type_ctx.diags) {
    return;
  }
  if (!HasAttribute(attrs_list, attrs::kDeprecated)) {
    return;
  }

  auto diag = core::MakeDiagnosticById("W-CNF-0601", span);
  if (!diag.has_value()) {
    return;
  }

  if (auto msg = GetAttributeValue(attrs_list, attrs::kDeprecated);
      msg.has_value()) {
    const auto normalized = NormalizeAttrLiteralLocal(*msg);
    if (!normalized.empty()) {
      core::SubDiagnostic note;
      note.kind = core::SubDiagnosticKind::Note;
      note.message = "deprecated message: " + normalized;
      diag->children.push_back(std::move(note));
    }
  }

  core::Emit(*type_ctx.diags, *diag);
}

}  // namespace cursive::analysis
