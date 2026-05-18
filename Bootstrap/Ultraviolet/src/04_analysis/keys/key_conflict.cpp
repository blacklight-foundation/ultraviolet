// ===========================================================================
// key_conflict.cpp - Key Conflict Detection
// ===========================================================================
//
// SPEC REFERENCE:
//   - Docs/SPECIFICATION.md, Section 17 "Key System" (line 23759)
//   - Docs/SPECIFICATION.md, Section 17.3 "Key Conflicts" (lines 23900-24000)
//   - Docs/SPECIFICATION.md, Section 17.4 "Conflict Detection" (lines 24010-24100)
//   - Docs/SPECIFICATION.md, Section 8.13 "E-KEY Errors" (lines 22200-22300)
//
// CONFLICT RULES:
//   | Held Key | New Key | Conflict? |
//   |----------|---------|-----------|
//   | read     | read    | No        |
//   | read     | write   | Yes       |
//   | write    | read    | Yes       |
//   | write    | write   | Yes       |
//
// PATH OVERLAP:
//   - a.b overlaps a.b.c (prefix)
//   - a.b overlaps a.b (same)
//   - a.b does not overlap a.c (different field)
//   - #boundary stops overlap propagation
//
// ===========================================================================

#include "04_analysis/keys/key_conflict.h"

#include <algorithm>
#include <charconv>
#include <cstdint>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsKeyConflict() {
  SPEC_DEF("KeyConflict", "UVX.5.X");
  SPEC_DEF("Overlap", "UVX.5.X");
  SPEC_DEF("ConflictRule", "UVX.5.X");
  SPEC_DEF("CanonicalOrder", "UVX.5.X");
}

}  // namespace

bool SegmentLess(const KeyPathSeg& lhs, const KeyPathSeg& rhs) {
  if (lhs.is_index != rhs.is_index) {
    return !lhs.is_index && rhs.is_index;
  }

  if (!lhs.is_index) {
    return lhs.name < rhs.name;
  }

  std::int64_t lhs_value = 0;
  std::int64_t rhs_value = 0;
  const auto parse_int = [](std::string_view text, std::int64_t& value) {
    auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && ptr == text.data() + text.size();
  };

  if (parse_int(lhs.name, lhs_value) && parse_int(rhs.name, rhs_value)) {
    return lhs_value < rhs_value;
  }

  return lhs.name < rhs.name;
}

bool LexLess(const std::vector<KeyPathSeg>& lhs,
             const std::vector<KeyPathSeg>& rhs) {
  const std::size_t min_len = std::min(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < min_len; ++i) {
    if (SegmentLess(lhs[i], rhs[i])) {
      return true;
    }
    if (SegmentLess(rhs[i], lhs[i])) {
      return false;
    }
  }
  return lhs.size() < rhs.size();
}

bool KeyModeCompatible(KeyAccessMode lhs, KeyAccessMode rhs) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-KeyModeCompatible");
  return lhs == KeyAccessMode::Read && rhs == KeyAccessMode::Read;
}

bool PathsDisjoint(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-Disjoint");
  return !IsPrefix(p1, p2) && !IsPrefix(p2, p1);
}

bool KeysCompatible(const HeldKey& lhs, const HeldKey& rhs) {
  return PathsDisjoint(lhs.path, rhs.path) ||
         KeyModeCompatible(lhs.mode, rhs.mode);
}

bool KeysOverlap(const KeyPath& p1, const KeyPath& p2) {
  return IsPrefix(p1, p2) || IsPrefix(p2, p1);
}

bool KeysConflict(const KeyPath& p1, KeyAccessMode m1,
                  const KeyPath& p2, KeyAccessMode m2) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-Conflict");

  if (!KeysOverlap(p1, p2)) {
    return false;
  }

  return !KeyModeCompatible(m1, m2);
}

ConflictResult CheckAcquisitionConflict(const KeyContext& ctx,
                                        const KeyPath& path,
                                        KeyAccessMode mode,
                                        const core::Span& span) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-CheckAcquisition");

  ConflictResult result;

  for (const auto& held : ctx.HeldKeys()) {
    if (KeysConflict(held.path, held.mode, path, mode)) {
      result.conflict = true;
      result.diag_id = "E-CON-0005";
      result.span = span;
      result.path1 = held.path;
      result.path2 = path;
      return result;
    }
  }

  return result;
}

ConflictResult CheckBlockConflict(
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys1,
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys2,
    const core::Span& span) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-CheckBlockConflict");

  ConflictResult result;

  for (const auto& [p1, m1] : keys1) {
    for (const auto& [p2, m2] : keys2) {
      if (KeysConflict(p1, m1, p2, m2)) {
        result.conflict = true;
        result.diag_id = "E-CON-0005";
        result.span = span;
        result.path1 = p1;
        result.path2 = p2;
        return result;
      }
    }
  }

  return result;
}

OrderValidation ValidateAcquisitionOrder(
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-ValidateOrder");

  OrderValidation result;

  // Check if keys are in canonical (lexicographic) order
  for (std::size_t i = 1; i < keys.size(); ++i) {
    if (KeyPathLess(keys[i].first, keys[i - 1].first)) {
      result.ok = false;
      result.diag_id = "W-CON-0001";  // Non-canonical order warning
      break;
    }
  }

  // Compute suggested order
  result.suggested_order = {};
  auto sorted = keys;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto& a, const auto& b) {
                     return KeyPathLess(a.first, b.first);
                   });
  for (const auto& [path, _] : sorted) {
    result.suggested_order.push_back(path);
  }

  return result;
}

std::vector<KeyPath> CanonicalSort(const std::vector<KeyPath>& paths) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-CanonicalSort");

  auto sorted = paths;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const KeyPath& lhs, const KeyPath& rhs) {
                     return KeyPathLess(lhs, rhs);
                   });
  sorted.erase(std::unique(sorted.begin(), sorted.end(),
                           [](const KeyPath& lhs, const KeyPath& rhs) {
                             return !KeyPathLess(lhs, rhs) &&
                                    !KeyPathLess(rhs, lhs);
                           }),
               sorted.end());
  return sorted;
}

std::vector<KeyPath> CanonicalOrder(const std::vector<KeyPath>& paths) {
  return CanonicalSort(paths);
}

std::vector<std::pair<KeyPath, KeyAccessMode>> CanonicalOrder(
    const std::vector<std::pair<KeyPath, KeyAccessMode>>& keys) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-CanonicalOrder");

  auto sorted = keys;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto& a, const auto& b) {
                     return KeyPathLess(a.first, b.first);
                   });
  return sorted;
}

bool StaticallyDisjoint(const ast::ExprPtr& idx1, const ast::ExprPtr& idx2) {
  SpecDefsKeyConflict();
  SPEC_RULE("K-StaticallyDisjoint");

  if (!idx1 || !idx2) {
    return false;
  }

  // Simple case: both are literal integers with different values
  const auto* lit1 = std::get_if<ast::LiteralExpr>(&idx1->node);
  const auto* lit2 = std::get_if<ast::LiteralExpr>(&idx2->node);

  if (lit1 && lit2) {
    if (lit1->literal.kind == ast::TokenKind::IntLiteral &&
        lit2->literal.kind == ast::TokenKind::IntLiteral) {
      return lit1->literal.lexeme != lit2->literal.lexeme;
    }
  }

  // More sophisticated analysis would involve:
  // - Constant propagation
  // - Linear integer reasoning (i ≠ j if i - j ≠ 0)
  // - Type-derived bounds

  return false;
}

}  // namespace ultraviolet::analysis
