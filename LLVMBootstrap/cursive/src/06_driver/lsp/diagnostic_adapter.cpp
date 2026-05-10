#include "06_driver/lsp/diagnostic_adapter.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "00_core/source_load.h"
#include "06_driver/lsp/protocol.h"
#include "06_driver/tooling/line_index.h"
#include "06_driver/tooling/uri.h"

namespace cursive::driver::lsp {

namespace {

std::optional<std::string> TextForPath(
    const tooling::DocumentStore& documents,
    const std::filesystem::path& path) {
  if (const auto overlay = documents.FindByPath(path)) {
    std::vector<std::uint8_t> bytes(overlay->text_utf8.begin(),
                                    overlay->text_utf8.end());
    core::SourceLoadResult loaded =
        core::LoadSource(path.generic_string(), bytes);
    if (loaded.source.has_value()) {
      return loaded.source->text;
    }
    return overlay->text_utf8;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
  core::SourceLoadResult loaded = core::LoadSource(path.generic_string(), bytes);
  if (!loaded.source.has_value()) {
    return std::nullopt;
  }
  return loaded.source->text;
}

int SeverityToLsp(core::Severity severity) {
  switch (severity) {
    case core::Severity::Error:
    case core::Severity::Panic:
      return 1;
    case core::Severity::Warning:
      return 2;
    case core::Severity::Info:
      return 3;
    case core::Severity::Note:
      return 4;
  }
  return 1;
}

llvm::json::Object LocationJson(const core::Span& span,
                                const tooling::LineIndex& index) {
  llvm::json::Object location;
  location["uri"] = tooling::PathToFileUri(span.file);
  location["range"] = RangeJson(index.RangeFor(span));
  return location;
}

}  // namespace

llvm::json::Array DiagnosticsForPath(
    const tooling::AnalysisSnapshot& snapshot,
    const tooling::DocumentStore& documents,
    const std::filesystem::path& path) {
  llvm::json::Array out;
  const auto text = TextForPath(documents, path);
  tooling::LineIndex index(text.value_or(std::string()));
  const std::string key = tooling::PathKey(path);

  for (const auto& diag : snapshot.diagnostics) {
    if (diag.span.has_value() && tooling::PathKey(diag.span->file) != key) {
      continue;
    }

    llvm::json::Object item;
    item["range"] = diag.span.has_value()
                        ? RangeJson(index.RangeFor(*diag.span))
                        : RangeJson(tooling::LineRange{});
    item["severity"] = static_cast<std::int64_t>(SeverityToLsp(diag.severity));
    if (!diag.code.empty()) {
      item["code"] = diag.code;
    }
    item["source"] = "cursive";
    item["message"] = diag.message;

    llvm::json::Array related;
    llvm::json::Array fixits;
    for (const auto& child : diag.children) {
      if (!child.span.has_value()) {
        continue;
      }
      const auto child_text = TextForPath(documents, child.span->file);
      tooling::LineIndex child_index(child_text.value_or(std::string()));
      if (child.kind == core::SubDiagnosticKind::FixIt) {
        if (!child.fix_text.has_value()) {
          continue;
        }
        llvm::json::Object fixit;
        fixit["title"] =
            child.message.empty() ? "Apply fix" : child.message;
        fixit["uri"] = tooling::PathToFileUri(child.span->file);
        fixit["range"] = RangeJson(child_index.RangeFor(*child.span));
        fixit["newText"] = *child.fix_text;
        fixits.emplace_back(std::move(fixit));
        continue;
      }
      llvm::json::Object related_item;
      related_item["location"] = LocationJson(*child.span, child_index);
      related_item["message"] = child.message;
      related.emplace_back(std::move(related_item));
    }
    if (!related.empty()) {
      item["relatedInformation"] = std::move(related);
    }
    if (!fixits.empty()) {
      llvm::json::Object data;
      data["fixits"] = std::move(fixits);
      item["data"] = std::move(data);
    }

    out.emplace_back(std::move(item));
  }

  return out;
}

}  // namespace cursive::driver::lsp
