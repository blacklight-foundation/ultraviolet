// =============================================================================
// MIGRATION MAPPING: path.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 2.1 "Project Root and Manifest" - Path Resolution (lines 956-999)
//     - WinSep = {"\\", "/"}
//     - AsciiLetter(c): c in A-Z or a-z
//     - DriveRooted(p): |p| >= 3 && AsciiLetter(p[0]) && p[1] = ":" && p[2] in WinSep
//     - UNC(p): StartsWith(p, "//") || StartsWith(p, "\\\\")
//     - RootRelative(p): starts with / or \ but not UNC or DriveRooted
//     - RootTag(p): extracts root component
//     - Tail(p): extracts path after root
//     - Segs(p): splits path into segments by separators
//     - PathComps(p): RootTag + Segs(Tail)
//     - JoinComp(comps): joins components with /
//     - Join(a, b): if b absolute return b, else join components
//     - AbsPath(p): DriveRooted || UNC || RootRelative
//     - is_relative(p): !AbsPath(p)
//     - Normalize(p): removes "." components
//     - Canon(p): Normalize, fails if ".." present
//     - prefix(p, q): PathPrefix check
//     - relative(p, base): computes relative path
//     - Basename(p): last component
//     - FileExt(p): extension including dot (lines 1001-1006)
//   - Resolve rules (lines 1008-1026)
//     - Resolve-Canonical: joins and canonicalizes
//     - Resolve-Canonical-Err: fails if Canon fails
//     - WF-RelPath: validates relative path stays under root
//
// SOURCE FILE: cursive-bootstrap/src/00_core/path.cpp
//   - Lines 1-219 (entire file)
//
// CONTENT TO MIGRATE:
//   - IsWinSep(c) -> bool (lines 9-11)
//   - IsAsciiLetter(c) -> bool (lines 13-15)
//   - StartsWith(s, prefix) -> bool (lines 17-19) [static]
//   - DriveRooted(p) -> bool (lines 21-23)
//   - UNC(p) -> bool (lines 25-27)
//   - RootRelative(p) -> bool (lines 29-31)
//   - AbsPath(p) -> bool (lines 33-35)
//   - IsRelative(p) -> bool (lines 37-39)
//   - RootTag(p) -> string (lines 41-52)
//   - Tail(p) -> string (lines 54-65)
//   - Segs(p) -> vector<string> (lines 67-83)
//   - PathComps(p) -> vector<string> (lines 85-96)
//   - JoinCompRec helper (lines 98-112) [static]
//   - JoinComp(comps) -> string (lines 114-116)
//   - Join(a, b) -> string (lines 118-126)
//   - Normalize(p) -> string (lines 128-139)
//   - Canon(p) -> optional<string> (lines 141-150)
//   - PathPrefix(path, pref) -> bool (lines 152-163)
//   - Prefix(p, q) -> bool (lines 165-167)
//   - Resolve(root, p) -> optional<ResolveResult> (lines 169-179)
//   - Relative(p, base) -> optional<string> (lines 181-197)
//   - Basename(p) -> string (lines 199-205)
//   - FileExt(p) -> string (lines 207-217)
//
// DEPENDENCIES:
//   - cursive/include/00_core/path.h (header)
//     - ResolveResult struct (root, path)
//   - cursive/include/00_core/assert_spec.h
//     - SPEC_RULE macro
//   - <string>, <string_view>, <vector>, <optional>
//
// REFACTORING NOTES:
//   1. Windows-specific path handling (drive letters, UNC paths)
//   2. SPEC_RULE traces:
//      - "Resolve-Canonical" and "Resolve-Canonical-Err" in Resolve()
//   3. RootTag returns drive letter without trailing separator (e.g., "C:")
//   4. Canon fails (returns nullopt) if any ".." component exists
//   5. FileExt returns empty string if no extension or dot at position 0
//   6. All path operations are pure functions (no filesystem access)
//   7. Consider string_view parameters where lifetimes permit
//
// =============================================================================

#include "00_core/path.h"

#include <cstddef>

#include "00_core/assert_spec.h"

namespace cursive::core {

bool IsWinSep(char c) {
  return c == '/' || c == '\\';
}

bool IsAsciiLetter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool DriveRooted(std::string_view p) {
  return p.size() >= 3 && IsAsciiLetter(p[0]) && p[1] == ':' && IsWinSep(p[2]);
}

bool UNC(std::string_view p) {
  return StartsWith(p, "//") || StartsWith(p, "\\\\");
}

bool RootRelative(std::string_view p) {
  return !p.empty() && IsWinSep(p[0]) && !UNC(p) && !DriveRooted(p);
}

bool AbsPath(std::string_view p) {
  return DriveRooted(p) || UNC(p) || RootRelative(p);
}

bool IsRelative(std::string_view p) {
  return !AbsPath(p);
}

std::string RootTag(std::string_view p) {
  if (DriveRooted(p)) {
    return std::string(p.substr(0, 2));
  }
  if (UNC(p)) {
    return "//";
  }
  if (RootRelative(p)) {
    return "/";
  }
  return "";
}

std::string Tail(std::string_view p) {
  if (DriveRooted(p)) {
    return std::string(p.substr(3));
  }
  if (UNC(p)) {
    return std::string(p.substr(2));
  }
  if (RootRelative(p)) {
    return std::string(p.substr(1));
  }
  return std::string(p);
}

std::vector<std::string> Segs(std::string_view p) {
  std::vector<std::string> segs;
  std::size_t i = 0;
  while (i < p.size()) {
    while (i < p.size() && IsWinSep(p[i])) {
      ++i;
    }
    const std::size_t start = i;
    while (i < p.size() && !IsWinSep(p[i])) {
      ++i;
    }
    if (i > start) {
      segs.emplace_back(p.substr(start, i - start));
    }
  }
  return segs;
}

std::vector<std::string> PathComps(std::string_view p) {
  const std::string root = RootTag(p);
  if (root.empty()) {
    return Segs(p);
  }
  std::vector<std::string> comps;
  comps.push_back(root);
  const auto tail = Tail(p);
  auto segs = Segs(tail);
  comps.insert(comps.end(), segs.begin(), segs.end());
  return comps;
}

std::vector<std::string> DropComps(std::size_t n,
                                   const std::vector<std::string>& comps) {
  if (n == 0) {
    return comps;
  }
  if (n >= comps.size()) {
    return {};
  }
  return std::vector<std::string>(comps.begin() + static_cast<std::ptrdiff_t>(n),
                                  comps.end());
}

std::optional<std::string> LastComp(const std::vector<std::string>& comps) {
  if (comps.empty()) {
    return std::nullopt;
  }
  return comps.back();
}

static std::string JoinCompRec(const std::vector<std::string>& comps,
                               std::size_t index) {
  if (index >= comps.size()) {
    return "";
  }
  if (index + 1 == comps.size()) {
    return comps[index];
  }
  const std::string& c = comps[index];
  const std::string rest = JoinCompRec(comps, index + 1);
  if (c == "/" || c == "//") {
    return c + rest;
  }
  return c + "/" + rest;
}

std::string JoinComp(const std::vector<std::string>& comps) {
  return JoinCompRec(comps, 0);
}

std::string Join(std::string_view a, std::string_view b) {
  if (AbsPath(b)) {
    return std::string(b);
  }
  auto comps = PathComps(a);
  auto tail = PathComps(b);
  comps.insert(comps.end(), tail.begin(), tail.end());
  return JoinComp(comps);
}

std::string Normalize(std::string_view p) {
  auto comps = PathComps(p);
  std::vector<std::string> filtered;
  filtered.reserve(comps.size());
  for (const auto& c : comps) {
    if (c == ".") {
      continue;
    }
    filtered.push_back(c);
  }
  return JoinComp(filtered);
}

std::optional<std::string> Canon(std::string_view p) {
  const std::string norm = Normalize(p);
  const auto comps = PathComps(norm);
  for (const auto& c : comps) {
    if (c == "..") {
      return std::nullopt;
    }
  }
  return norm;
}

bool PathPrefix(const std::vector<std::string>& path,
                const std::vector<std::string>& pref) {
  if (pref.size() > path.size()) {
    return false;
  }
  for (std::size_t i = 0; i < pref.size(); ++i) {
    if (path[i] != pref[i]) {
      return false;
    }
  }
  return true;
}

bool Prefix(std::string_view p, std::string_view q) {
  return PathPrefix(PathComps(q), PathComps(p));
}

std::optional<ResolveResult> Resolve(std::string_view root, std::string_view p) {
  const std::string joined = Normalize(Join(root, p));
  const auto canon_root = Canon(root);
  const auto canon_path = Canon(joined);
  if (!canon_root.has_value() || !canon_path.has_value()) {
    SPEC_RULE("Resolve-Canonical-Err");
    return std::nullopt;
  }
  SPEC_RULE("Resolve-Canonical");
  return ResolveResult{*canon_root, *canon_path};
}

std::optional<std::string> Relative(std::string_view p, std::string_view base) {
  const auto canon_path = Canon(p);
  const auto canon_base = Canon(base);
  if (!canon_path.has_value() || !canon_base.has_value()) {
    return std::nullopt;
  }
  const auto path_comps = PathComps(*canon_path);
  const auto base_comps = PathComps(*canon_base);
  if (!PathPrefix(path_comps, base_comps)) {
    return std::nullopt;
  }
  const auto rel_comps = DropComps(base_comps.size(), path_comps);
  return JoinComp(rel_comps);
}

std::string Basename(std::string_view p) {
  const auto last = LastComp(PathComps(p));
  if (!last.has_value()) {
    return "";
  }
  return *last;
}

std::string FileExt(std::string_view p) {
  const std::string base = Basename(p);
  const auto pos = base.rfind('.');
  if (pos == std::string::npos) {
    return "";
  }
  if (pos == 0) {
    return "";
  }
  return base.substr(pos);
}

}  // namespace cursive::core
