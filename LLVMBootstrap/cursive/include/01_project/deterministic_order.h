#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"

namespace cursive::project {

struct OrderKey {
  std::string folded;
  std::string raw;
};

std::string FoldPath(std::string_view path);
std::string Fold(std::string_view module_path);

bool Utf8LexLess(std::string_view a, std::string_view b);
bool KeyLess(const OrderKey& a, const OrderKey& b);

OrderKey FileKey(const std::filesystem::path& file,
                 const std::filesystem::path& dir,
                 core::DiagnosticStream& diags);

OrderKey DirKey(const std::filesystem::path& dir,
                const std::filesystem::path& base,
                core::DiagnosticStream& diags);

struct DirSeqResult {
  std::vector<std::filesystem::path> dirs;
  core::DiagnosticStream diags;
};

DirSeqResult DirSeq(const std::filesystem::path& root);
DirSeqResult DirSeqFrom(const std::filesystem::path& root,
                        const std::vector<std::filesystem::path>& dirs);

}  // namespace cursive::project
