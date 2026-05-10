#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// C0X Extension: Key System - Key Context Management
// Implements the key context Γ_keys for tracking held keys

// Key mode for concurrent access
enum class KeyAccessMode {
  Read,
  Write,
};

// Key scope identifier
using KeyScopeId = std::size_t;

// Key path segment
struct KeyPathSeg {
  bool boundary = false;  // # marker
  std::string name;       // field name or index expression string
  bool is_index = false;  // true if index segment
};

// Key path representation
struct KeyPath {
  std::string root;
  std::vector<KeyPathSeg> segs;
  
  // Lexicographic string representation for ordering
  std::string ToString() const;
};

// A held key (Path, Mode, Scope)
struct HeldKey {
  KeyPath path;
  KeyAccessMode mode;
  KeyScopeId scope;
};

class KeyContext;

using ProgramPoint = std::size_t;
using KeySet = std::vector<HeldKey>;
using KeyStateByProgramPoint = std::unordered_map<ProgramPoint, KeySet>;

bool Held(const KeyPath& path,
          KeyAccessMode mode,
          KeyScopeId scope,
          const KeyStateByProgramPoint& key_state,
          ProgramPoint point);

std::vector<HeldKey> AcquireKeysSigma(const std::vector<KeyPath>& paths,
                                      KeyAccessMode mode,
                                      KeyContext& ctx);
void ReleaseKeysSigma(const std::vector<HeldKey>& keys,
                      KeyContext& ctx);

// Key context - tracks held keys
class KeyContext {
 public:
  KeyContext() = default;
  
  // Push/pop key scopes
  void PushScope();
  void PopScope();
  
  // Acquire a key
  // Returns true if key was newly acquired, false if already covered
  bool Acquire(const KeyPath& path, KeyAccessMode mode);
  
  // Release keys at current scope
  void ReleaseScope();

  // Release a specific held key path.
  void Release(const KeyPath& path);

  // Transition an existing held key to a new mode.
  bool ModeTransition(const KeyPath& path, KeyAccessMode new_mode);

  // Release keys at or nested beneath the given scope (panic path).
  void PanicRelease(KeyScopeId scope);
  
  // Check if a path is covered by held keys
  bool Covers(const KeyPath& path, KeyAccessMode mode) const;
  
  // Get all held keys
  const std::vector<HeldKey>& HeldKeys() const { return held_keys_; }
  
  // Current scope ID
  KeyScopeId CurrentScope() const { return current_scope_; }
  
 private:
  std::vector<HeldKey> held_keys_;
  KeyScopeId current_scope_ = 0;
  std::vector<KeyScopeId> scope_stack_;
};

// Convert AST key path to analysis key path
KeyPath LowerKeyPath(const ast::KeyPathExpr& ast_path);

// Check if path1 is a prefix of path2
bool IsPrefix(const KeyPath& prefix, const KeyPath& path);

// Check if two paths overlap (one is prefix of other)
bool PathsOverlap(const KeyPath& p1, const KeyPath& p2);

// Canonical ordering for key paths (lexicographic)
bool KeyPathLess(const KeyPath& lhs, const KeyPath& rhs);

}  // namespace cursive::analysis
