// =============================================================================
// Key Context Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   - SPECIFICATION.md, Section 17.1 "Key Context" (lines 23870-23890)
//   - SPECIFICATION.md, Section 17.2 "Key Acquisition" (lines 24010-24050)
//   - SPECIFICATION.md, Section 17.5 "Key Block Semantics" (lines 24100-24130)
//
// KEY CONTEXT SEMANTICS:
//   - Γ_keys is a collection of (P, M, S) triples: path, mode, scope
//   - Acquire: Γ_keys' = Γ_keys ∪ {(P, M, S)}
//   - Release: Γ_keys' = Γ_keys \ {(P, M, S) : S = current_scope}
//   - Covered: A path P with mode M is covered if ∃ held key covering P with mode ≥ M
//
// KEY BLOCK SYNTAX (from spec):
//   #path { ... }           -- write mode (default)
//   #path read { ... }      -- read mode
//   #path write { ... }     -- explicit write mode
//   #p1, p2 { ... }         -- multiple paths
//
// BOUNDARY SEMANTICS:
//   - # marker on field creates key boundary
//   - Key acquisition stops at boundary markers
//   - Fields with # prefix establish permanent key boundaries
//
// =============================================================================

#include "04_analysis/keys/key_context.h"

#include <algorithm>
#include <charconv>
#include <sstream>

#include "00_core/assert_spec.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsKeyContext() {
  SPEC_DEF("KeyContext", "UVX.5.X");
  SPEC_DEF("HeldKey", "UVX.5.X");
  SPEC_DEF("KeyPath", "UVX.5.X");
  SPEC_DEF("Covers", "UVX.5.X");
  SPEC_DEF("Acquire", "UVX.5.X");
}

static bool ParseStaticIndexValue(std::string_view text, std::int64_t& value) {
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  return ec == std::errc{} && ptr == end;
}

static bool SegmentEqual(const KeyPathSeg& lhs, const KeyPathSeg& rhs) {
  return lhs.is_index == rhs.is_index &&
         lhs.name == rhs.name &&
         lhs.boundary == rhs.boundary;
}

static bool SegmentLessLocal(const KeyPathSeg& lhs, const KeyPathSeg& rhs) {
  if (lhs.is_index != rhs.is_index) {
    return !lhs.is_index && rhs.is_index;
  }

  if (!lhs.is_index) {
    return lhs.name < rhs.name;
  }

  std::int64_t lhs_value = 0;
  std::int64_t rhs_value = 0;
  if (ParseStaticIndexValue(lhs.name, lhs_value) &&
      ParseStaticIndexValue(rhs.name, rhs_value)) {
    return lhs_value < rhs_value;
  }

  return lhs.name < rhs.name;
}

static bool LexLessLocal(const std::vector<KeyPathSeg>& lhs,
                         const std::vector<KeyPathSeg>& rhs) {
  const std::size_t min_len = std::min(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < min_len; ++i) {
    if (SegmentLessLocal(lhs[i], rhs[i])) {
      return true;
    }
    if (SegmentLessLocal(rhs[i], lhs[i])) {
      return false;
    }
  }
  return lhs.size() < rhs.size();
}

}  // namespace

bool Held(const KeyPath& path,
          KeyAccessMode mode,
          KeyScopeId scope,
          const KeyStateByProgramPoint& key_state,
          ProgramPoint point) {
  const auto it = key_state.find(point);
  if (it == key_state.end()) {
    return false;
  }
  return std::any_of(it->second.begin(), it->second.end(),
                     [&](const HeldKey& held) {
                       return held.mode == mode &&
                              held.scope == scope &&
                              !KeyPathLess(held.path, path) &&
                              !KeyPathLess(path, held.path);
                     });
}

std::vector<HeldKey> AcquireKeysSigma(const std::vector<KeyPath>& paths,
                                      KeyAccessMode mode,
                                      KeyContext& ctx) {
  auto ordered = paths;
  std::sort(ordered.begin(), ordered.end(),
            [](const KeyPath& lhs, const KeyPath& rhs) {
              return KeyPathLess(lhs, rhs);
            });

  std::vector<HeldKey> acquired;
  for (const auto& path : ordered) {
    if (!ctx.Acquire(path, mode)) {
      continue;
    }
    acquired.push_back(HeldKey{path, mode, ctx.CurrentScope()});
  }
  return acquired;
}

void ReleaseKeysSigma(const std::vector<HeldKey>& keys,
                      KeyContext& ctx) {
  for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
    ctx.Release(it->path);
  }
}

std::string KeyPath::ToString() const {
  std::ostringstream oss;
  oss << root;
  for (const auto& seg : segs) {
    if (seg.boundary) {
      oss << ".#";
    } else {
      oss << ".";
    }
    if (seg.is_index) {
      oss << "[" << seg.name << "]";
    } else {
      oss << seg.name;
    }
  }
  return oss.str();
}

void KeyContext::PushScope() {
  SpecDefsKeyContext();
  SPEC_RULE("K-Push-Scope");
  scope_stack_.push_back(current_scope_);
  current_scope_ = scope_stack_.size();
}

void KeyContext::PopScope() {
  SpecDefsKeyContext();
  SPEC_RULE("K-Pop-Scope");
  if (!scope_stack_.empty()) {
    // Release all keys at current scope
    ReleaseScope();
    current_scope_ = scope_stack_.back();
    scope_stack_.pop_back();
  }
}

bool KeyContext::Acquire(const KeyPath& path, KeyAccessMode mode) {
  SpecDefsKeyContext();

  // Check if already covered
  if (Covers(path, mode)) {
    SPEC_RULE("K-Acquire-Covered");
    return false;  // Already covered, no acquisition needed
  }

  SPEC_RULE("K-Acquire-New");
  HeldKey key;
  key.path = path;
  key.mode = mode;
  key.scope = current_scope_;
  held_keys_.push_back(std::move(key));
  return true;
}

void KeyContext::ReleaseScope() {
  SpecDefsKeyContext();
  SPEC_RULE("K-Release-Scope");

  // Remove all keys at current scope
  held_keys_.erase(
      std::remove_if(held_keys_.begin(), held_keys_.end(),
                     [this](const HeldKey& k) {
                       return k.scope == current_scope_;
                     }),
      held_keys_.end());
}

void KeyContext::Release(const KeyPath& path) {
  SpecDefsKeyContext();
  SPEC_RULE("K-Release");

  held_keys_.erase(
      std::remove_if(held_keys_.begin(), held_keys_.end(),
                     [&](const HeldKey& held) {
                       return !KeyPathLess(held.path, path) &&
                              !KeyPathLess(path, held.path);
                     }),
      held_keys_.end());
}

bool KeyContext::ModeTransition(const KeyPath& path, KeyAccessMode new_mode) {
  SpecDefsKeyContext();
  SPEC_RULE("K-ModeTransition");

  for (auto& held : held_keys_) {
    if (!KeyPathLess(held.path, path) && !KeyPathLess(path, held.path)) {
      held.mode = new_mode;
      return true;
    }
  }
  return false;
}

void KeyContext::PanicRelease(KeyScopeId scope) {
  SpecDefsKeyContext();
  SPEC_RULE("K-PanicRelease");

  held_keys_.erase(
      std::remove_if(held_keys_.begin(), held_keys_.end(),
                     [&](const HeldKey& held) {
                       return held.scope >= scope;
                     }),
      held_keys_.end());
}

bool KeyContext::Covers(const KeyPath& path, KeyAccessMode mode) const {
  SpecDefsKeyContext();
  SPEC_RULE("K-Covers");

  for (const auto& held : held_keys_) {
    // A held key covers the path if:
    // 1. The held path is a prefix of the requested path
    // 2. The held mode is >= the requested mode (Write covers both, Read covers Read)
    if (IsPrefix(held.path, path)) {
      if (held.mode == KeyAccessMode::Write) {
        return true;  // Write covers everything
      }
      if (mode == KeyAccessMode::Read) {
        return true;  // Read covers Read
      }
    }
  }
  return false;
}

KeyPath LowerKeyPath(const ast::KeyPathExpr& ast_path) {
  SpecDefsKeyContext();
  SPEC_RULE("K-Lower-Path");

  KeyPath path;
  path.root = ast_path.root;

  for (const auto& seg : ast_path.segs) {
    KeyPathSeg lowered;
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      lowered.boundary = false;
      lowered.name = field->name;
      lowered.is_index = false;
      path.segs.push_back(std::move(lowered));
      if (field->marked) {
        break;
      }
    } else if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      lowered.boundary = false;
      // For now, represent index expression as string
      // More sophisticated handling would involve constant evaluation
      lowered.name = "[index]";
      lowered.is_index = true;
      path.segs.push_back(std::move(lowered));
      if (index->marked) {
        break;
      }
    }
  }

  return path;
}

bool IsPrefix(const KeyPath& prefix, const KeyPath& path) {
  SpecDefsKeyContext();
  SPEC_RULE("K-IsPrefix");

  if (prefix.root != path.root) {
    return false;
  }

  if (prefix.segs.size() > path.segs.size()) {
    return false;
  }

  for (std::size_t i = 0; i < prefix.segs.size(); ++i) {
    if (!SegmentEqual(prefix.segs[i], path.segs[i])) {
      return false;
    }
  }

  return true;
}

bool PathsOverlap(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyContext();
  SPEC_RULE("K-PathsOverlap");

  // Two paths overlap if one is a prefix of the other
  return IsPrefix(p1, p2) || IsPrefix(p2, p1);
}

bool KeyPathLess(const KeyPath& lhs, const KeyPath& rhs) {
  SpecDefsKeyContext();
  SPEC_RULE("K-PathLess");

  // Lexicographic comparison
  if (lhs.root != rhs.root) {
    return lhs.root < rhs.root;
  }

  return LexLessLocal(lhs.segs, rhs.segs);
}

}  // namespace ultraviolet::analysis
