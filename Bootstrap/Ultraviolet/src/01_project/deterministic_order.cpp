// =============================================================================
// MIGRATION MAPPING: deterministic_order.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 2.3 (lines 1168-1207)
//   - 2.3. Deterministic Ordering and Case Folding
//   - 2.3.1. Module File Processing Order
//   - 2.3.2. Module Path Case-Folding Algorithm
//   - 2.3.3. Directory Enumeration Order
//
// SOURCE FILE: ultraviolet-bootstrap/src/01_project/deterministic_order.cpp
//   - Lines 1-171 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. EmitExternal() helper (lines 16-22)
//    - PURPOSE: Emit diagnostic without source span
//    - REFACTORING: Consolidate with other MakeExternalDiag helpers
//
// 2. SplitModulePath() helper (lines 24-37)
//    - PURPOSE: Split module path on "::" delimiter
//    - EXAMPLE: "foo::bar::baz" -> ["foo", "bar", "baz"]
//
// 3. JoinModulePath() helper (lines 39-49)
//    - PURPOSE: Join module path components with "::"
//    - EXAMPLE: ["foo", "bar", "baz"] -> "foo::bar::baz"
//
// 4. FoldPath() (lines 53-61)
//    - PURPOSE: Case-fold filesystem path for comparison
//    - SPEC RULE: FoldPath(r) = JoinComp([CaseFold(NFC(c)) | c in PathComps(r)])
//    - SPEC REF: Line 1171
//    - DEPENDENCIES: core::PathComps, core::CaseFold, core::NFC, core::JoinComp
//
// 5. Fold() (lines 63-71)
//    - PURPOSE: Case-fold module path for comparison
//    - SPEC RULE: Fold(p) = [CaseFold(NFC(c)) | c in p]
//    - SPEC REF: Lines 1186-1187
//    - DEPENDENCIES: SplitModulePath, core::CaseFold, core::NFC
//
// 6. Utf8LexLess() (lines 73-77)
//    - PURPOSE: UTF-8 lexicographic comparison for sorting
//    - SPEC RULE: Utf8LexLess(a, b) <=> LexBytes(Utf8(a), Utf8(b))
//    - SPEC REF: Line 1374
//
// 7. KeyLess() (lines 79-87)
//    - PURPOSE: Compare OrderKey pairs for sorting
//    - SPEC RULE: f_1 <_file f_2 <=> Utf8LexLess(FileKey(f_1, d), FileKey(f_2, d))
//    - SPEC REF: Line 1177
//
// 8. FileKey() (lines 89-101)
//    - PURPOSE: Compute file ordering key
//    - SPEC RULE: FileKey(f, d) = <FoldPath(rel), rel> if relative(f, d) ok
//    - SPEC RULE: FileKey(f, d) = <bottom, Basename(f)> if relative fails
//    - SPEC REF: Lines 1173-1175
//    - DIAGNOSTIC: E-PRJ-0303 (FileOrder-Rel-Fail)
//
// 9. DirKey() (lines 103-115)
//    - PURPOSE: Compute directory ordering key
//    - SPEC RULE: DirKey(d, S) = <FoldPath(rel), rel> if relative(d, S) ok
//    - SPEC RULE: DirKey(d, S) = <bottom, Basename(d)> if relative fails
//    - SPEC REF: Lines 1191-1194
//    - DIAGNOSTIC: E-PRJ-0303 (DirSeq-Rel-Fail)
//
// 10. DirSeqFrom() (lines 117-138)
//     - PURPOSE: Sort directory list by deterministic order
//     - SPEC RULE: DirSeq(S) = sort_{<_dir}(Dirs(S))
//     - SPEC REF: Line 1197
//
// 11. DirSeq() (lines 140-168)
//     - PURPOSE: Enumerate and sort directories
//     - SPEC RULE: DirSeq-Read-Err
//     - SPEC REF: Lines 1199-1202
//     - DIAGNOSTIC: E-PRJ-0305
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "uv/01_project/deterministic_order.h"
//   - "uv/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "uv/00_core/diagnostic_messages.h" (MakeDiagnostic)
//   - "uv/00_core/path.h" (PathComps, Basename, Relative, JoinComp)
//   - "uv/00_core/unicode.h" (CaseFold, NFC)
//   - <algorithm>
//   - <optional>
//   - <system_error>
//   - <filesystem>
//   - <vector>
//   - <string>
//
// Types from header (deterministic_order.h):
//   - OrderKey { std::string folded; std::string raw; }
//   - DirSeqResult { std::vector<std::filesystem::path> dirs; DiagnosticStream diags; }
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. SplitModulePath/JoinModulePath also appear in module_discovery.cpp
//    RECOMMENDATION: Move to a shared module_path utility
//
// 2. Consider using std::ranges for sorting operations in C++20
//
// 3. The OrderKey type could benefit from comparison operators:
//    bool operator<(const OrderKey& a, const OrderKey& b) { return KeyLess(a, b); }
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Line 96: SPEC_RULE("FileOrder-Rel-Fail");
// Line 110: SPEC_RULE("DirSeq-Rel-Fail");
// Line 145: SPEC_RULE("DirSeq-Read-Err");
// Line 153: SPEC_RULE("DirSeq-Read-Err");
// Line 160: SPEC_RULE("DirSeq-Read-Err");
//
// =============================================================================

#include "01_project/deterministic_order.h"

#include <algorithm>
#include <optional>
#include <system_error>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/path.h"
#include "00_core/unicode.h"

namespace ultraviolet::project {

namespace {

constexpr std::string_view kBottomOrderKey = "<bottom>";

void EmitExternal(core::DiagnosticStream& diags, std::string_view code) {
  core::EmitExternalDiagnostic(diags, code);
}

std::vector<std::string> SplitModulePath(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t pos = path.find("::", start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(path.substr(start));
      break;
    }
    parts.emplace_back(path.substr(start, pos - start));
    start = pos + 2;
  }
  return parts;
}

std::string JoinModulePath(const std::vector<std::string>& comps) {
  if (comps.empty()) {
    return "";
  }
  std::string out = comps[0];
  for (std::size_t i = 1; i < comps.size(); ++i) {
    out.append("::");
    out.append(comps[i]);
  }
  return out;
}

}  // namespace

std::string FoldPath(std::string_view path) {
  const auto comps = core::PathComps(path);
  std::vector<std::string> folded;
  folded.reserve(comps.size());
  for (const auto& comp : comps) {
    folded.push_back(core::CaseFold(core::NFC(comp)));
  }
  return core::JoinComp(folded);
}

std::string Fold(std::string_view module_path) {
  const auto comps = SplitModulePath(module_path);
  std::vector<std::string> folded;
  folded.reserve(comps.size());
  for (const auto& comp : comps) {
    folded.push_back(core::CaseFold(core::NFC(comp)));
  }
  return JoinModulePath(folded);
}

bool Utf8LexLess(std::string_view a, std::string_view b) {
  return std::lexicographical_compare(
      a.begin(), a.end(), b.begin(), b.end(),
      [](unsigned char lhs, unsigned char rhs) { return lhs < rhs; });
}

bool KeyLess(const OrderKey& a, const OrderKey& b) {
  const bool a_is_bottom = a.folded == kBottomOrderKey;
  const bool b_is_bottom = b.folded == kBottomOrderKey;
  if (a_is_bottom != b_is_bottom) {
    return a_is_bottom;
  }
  if (Utf8LexLess(a.folded, b.folded)) {
    return true;
  }
  if (a.folded == b.folded) {
    return Utf8LexLess(a.raw, b.raw);
  }
  return false;
}

OrderKey FileKey(const std::filesystem::path& file,
                 const std::filesystem::path& dir,
                 core::DiagnosticStream& diags) {
  const std::string file_str = file.generic_string();
  const std::string dir_str = dir.generic_string();
  const std::optional<std::string> rel = core::Relative(file_str, dir_str);
  if (!rel.has_value()) {
    SPEC_RULE("FileOrder-Rel-Fail");
    EmitExternal(diags, "E-PRJ-0303");
    return OrderKey{std::string(kBottomOrderKey), core::Basename(file_str)};
  }
  return OrderKey{FoldPath(*rel), *rel};
}

OrderKey DirKey(const std::filesystem::path& dir,
                const std::filesystem::path& base,
                core::DiagnosticStream& diags) {
  const std::string dir_str = dir.generic_string();
  const std::string base_str = base.generic_string();
  const std::optional<std::string> rel = core::Relative(dir_str, base_str);
  if (!rel.has_value()) {
    SPEC_RULE("DirSeq-Rel-Fail");
    EmitExternal(diags, "E-PRJ-0303");
    return OrderKey{std::string(kBottomOrderKey), core::Basename(dir_str)};
  }
  return OrderKey{FoldPath(*rel), *rel};
}

DirSeqResult DirSeqFrom(const std::filesystem::path& root,
                        const std::vector<std::filesystem::path>& dirs) {
  DirSeqResult result;
  struct DirEntry {
    std::filesystem::path path;
    OrderKey key;
  };
  std::vector<DirEntry> entries;
  entries.reserve(dirs.size());
  for (const auto& dir : dirs) {
    entries.push_back(DirEntry{dir, DirKey(dir, root, result.diags)});
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const DirEntry& lhs, const DirEntry& rhs) {
                     return KeyLess(lhs.key, rhs.key);
                   });
  result.dirs.reserve(entries.size());
  for (const auto& entry : entries) {
    result.dirs.push_back(entry.path);
  }
  return result;
}

DirSeqResult DirSeq(const std::filesystem::path& root) {
  DirSeqResult result;
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(root, ec);
  if (ec) {
    SPEC_RULE("DirSeq-Read-Err");
    EmitExternal(result.diags, "E-PRJ-0305");
    return result;
  }
  std::vector<std::filesystem::path> dirs;
  dirs.push_back(root);
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      SPEC_RULE("DirSeq-Read-Err");
      EmitExternal(result.diags, "E-PRJ-0305");
      return result;
    }
    std::error_code type_ec;
    if (it->is_directory(type_ec)) {
      if (type_ec) {
        SPEC_RULE("DirSeq-Read-Err");
        EmitExternal(result.diags, "E-PRJ-0305");
        return result;
      }
      dirs.push_back(it->path());
    }
  }
  return DirSeqFrom(root, dirs);
}

}  // namespace ultraviolet::project
