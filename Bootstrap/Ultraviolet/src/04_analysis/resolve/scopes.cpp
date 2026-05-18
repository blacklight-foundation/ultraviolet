// =============================================================================
// MIGRATION MAPPING: scopes.cpp
// =============================================================================
//
// SPEC REFERENCE:
//   SPECIFICATION.md §5.1.1 "Scope Context and Identifiers" (Lines 6659-6717)
//   SPECIFICATION.md §3.1.6 "IdKey, IdEq, PathKey, PathEq"
//
// SOURCE FILE:
//   ultraviolet-bootstrap/src/03_analysis/resolve/scopes.cpp (Lines 1-213)
//
// DEPENDENCIES:
//   - ultraviolet/include/04_analysis/resolve/scopes.h (header)
//   - ultraviolet/src/00_core/unicode.h (NFC normalization)
//   - ultraviolet/src/02_source/keyword_policy.h (IsKeyword)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// 1. IdKey/IdEq Functions (Source Lines 74-97)
//    - IdKeyOf(string_view) -> IdKey: NFC-normalize identifier
//    - IdEq(s1, s2) -> bool: Compare via IdKey
//    - PathKeyOf(Path) -> PathKey: Map each component through IdKey
//    - PathEq(p1, p2) -> bool: Compare via PathKey
//
// 2. ScopeKey Validation (Source Lines 99-108)
//    - ScopeKey(Scope) -> bool: Verify all keys are NFC-normalized
//
// 3. Reserved Identifier Functions (Source Lines 110-146)
//    - BytePrefix(p, s) -> bool: Check string prefix (byte-level)
//    - Prefix(s, p) -> bool: Wrapper for BytePrefix
//    - ReservedGen(x) -> bool: Check if starts with "gen_"
//    - ReservedModulePath(path) -> bool: Check module path for reserved
//
// 4. Type Name Constants (Source Lines 148-191)
//    - PrimTypeNames(): i8-i128, u8-u128, f16-f64, bool, char, usize, isize
//    - SpecialTypeNames(): Self, Drop, Bitcopy, Clone, Eq, Hash, etc.
//    - AsyncTypeNames(): Async, Future, Sequence, Stream, Pipe, Exchange, Tracked, Spawned
//    - PrimTypeKeys(), SpecialTypeKeys(), AsyncTypeKeys(): Cached key vectors
//
// 5. KeywordKey Function (Source Lines 193-196)
//    - KeywordKey(idkey) -> bool: Check if key is a keyword
//
// 6. UniverseBindings (Source Lines 198-210)
//    - UniverseBindings() -> Scope: Built-in universe scope with:
//      - All protected type names mapped to Type entities
//      - "ultraviolet" mapped to ModuleAlias entity
//
// =============================================================================
// SPEC DEFINITIONS TO IMPLEMENT:
// =============================================================================
//
// From §5.1.1:
//   IdKeyRef = {"3.1.6"}
//   ScopeKey(S) ⇔ dom(S) ⊆ {IdKey(x) | x ∈ Identifier}
//
//   EntityKind = {Value, Type, Class, ModuleAlias}
//   EntitySource = {Decl, Using, RegionAlias}
//   Entity = ⟨kind, origin_opt, target_opt, source⟩
//
//   S : IdKey ⇀ Entity
//   Scopes(Γ) = [S_1, …, S_k, S_proc, S_module, S_universe]
//
//   UniverseProtectedRef = {"3.2.3"}
//   UniverseBindings = { IdKey(x) ↦ ⟨Type, ⊥, ⊥, Decl⟩ | x ∈ UniverseProtected }
//                    ∪ { IdKey(`ultraviolet`) ↦ ⟨ModuleAlias, `ultraviolet`, ⊥, Decl⟩ }
//
//   BytePrefix(p, s) ⇔ ∃ r. s = p ++ r
//   Prefix(s, p) ⇔ BytePrefix(p, s)
//   ReservedGen(x) ⇔ Prefix(IdKey(x), IdKey(`gen_`))
//   ReservedModulePath(path) ⇔ (|path| ≥ 1 ∧ IdEq(path[0], `ultraviolet`))
//                            ∨ (∃ i. ReservedGen(path[i]))
//
//   PrimTypeNames = {`i8`, `i16`, ..., `isize`}
//   SpecialTypeNames = {`Self`, `Drop`, `Bitcopy`, ..., `Reactor`}
//   AsyncTypeNames = {`Async`, `Future`, `Sequence`, ..., `Tracked`, `Spawned`}
//
//   KeywordKey(n) ⇔ ∃ s. n = IdKey(s) ∧ Keyword(s)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// 1. Use std::unordered_map instead of map for Scope if performance critical
// 2. Consider constexpr for name arrays if C++20 available
// 3. MakeKeys helper can be inline or removed if using initializer lists
// 4. Ensure Entity struct matches spec tuple definition exactly
// 5. UniverseProtectedNames should match SpecialTypeNames union
//
// =============================================================================

#include "04_analysis/resolve/scopes.h"

#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/unicode.h"
#include "01_project/language_profile.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsIdKeys() {
  SPEC_DEF("IdKey", "3.1.6");
  SPEC_DEF("IdEq", "3.1.6");
  SPEC_DEF("PathKey", "3.1.6");
  SPEC_DEF("PathEq", "3.1.6");
}

static inline void SpecDefsScopeKeys() {
  SPEC_DEF("IdKeyRef", "5.1.1");
  SPEC_DEF("ScopeKey", "5.1.1");
}

static inline void SpecDefsNames() {
  SPEC_DEF("PrimTypeNames", "5.1.1");
  SPEC_DEF("SpecialTypeNames", "5.1.1");
  SPEC_DEF("AsyncTypeNames", "5.1.1");
  SPEC_DEF("PrimTypeKeys", "5.1.1");
  SPEC_DEF("SpecialTypeKeys", "5.1.1");
  SPEC_DEF("AsyncTypeKeys", "5.1.1");
}

static inline void SpecDefsReserved() {
  SPEC_DEF("UniverseProtectedRef", "5.1.1");
  SPEC_DEF("UniverseBindings", "5.1.1");
  SPEC_DEF("BytePrefix", "5.1.1");
  SPEC_DEF("Prefix", "5.1.1");
  SPEC_DEF("ReservedGen", "5.1.1");
  SPEC_DEF("ReservedModulePath", "5.1.1");
  SPEC_DEF("KeywordKey", "5.1.1");
}

const std::vector<std::string_view>& UniverseProtectedNames() {
  SpecDefsReserved();
  static const std::vector<std::string_view> names = {
      "i8",     "i16",    "i32",    "i64",    "i128",   "u8",
      "u16",    "u32",    "u64",    "u128",   "f16",    "f32",
      "f64",    "bool",   "char",   "usize",  "isize",  "Self",
      "Drop", "Bitcopy", "Clone", "Eq", "Hash", "Hasher", "Iterator", "Step",
      "FfiSafe", "string", "bytes",  "Modal",  "Region", "RegionOptions",
      "CancelToken", "Context", "TestAuthority", "System", "IO",
      "HeapAllocator", "ExecutionDomain", "Reactor", "Network", "Time",
      "MonotonicTime", "WallTime", "Duration", "MonotonicInstant",
      "UtcInstant", "TimeError", "CpuSet", "Priority", "Async", "Future",
      "Sequence", "Stream", "Pipe", "Exchange", "Tracked", "Spawned"};
  return names;
}

std::vector<IdKey> MakeKeys(const std::vector<std::string_view>& names) {
  SpecDefsNames();
  std::vector<IdKey> keys;
  keys.reserve(names.size());
  for (const auto name : names) {
    keys.push_back(IdKeyOf(name));
  }
  return keys;
}

}  // namespace

IdKey IdKeyOf(std::string_view s) {
  SpecDefsIdKeys();
  return core::NFC(s);
}

bool IdEq(std::string_view s1, std::string_view s2) {
  SpecDefsIdKeys();
  return IdKeyOf(s1) == IdKeyOf(s2);
}

PathKey PathKeyOf(const ast::Path& path) {
  SpecDefsIdKeys();
  PathKey out;
  out.reserve(path.size());
  for (const auto& comp : path) {
    out.push_back(IdKeyOf(comp));
  }
  return out;
}

bool PathEq(const ast::Path& p, const ast::Path& q) {
  SpecDefsIdKeys();
  return PathKeyOf(p) == PathKeyOf(q);
}

bool ScopeKey(const Scope& scope) {
  SpecDefsScopeKeys();
  for (const auto& [key, ent] : scope) {
    (void)ent;
    if (core::NFC(key) != key) {
      return false;
    }
  }
  return true;
}

bool BytePrefix(std::string_view p, std::string_view s) {
  SpecDefsReserved();
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool Prefix(std::string_view s, std::string_view p) {
  SpecDefsReserved();
  return BytePrefix(p, s);
}

bool ReservedGen(std::string_view x) {
  SpecDefsReserved();
  return Prefix(IdKeyOf(x), IdKeyOf("gen_"));
}

bool ReservedModulePath(const ast::ModulePath& path) {
  SpecDefsReserved();
  if (!path.empty() &&
      IdEq(path[0], project::ActiveLanguageProfile().runtime_root)) {
    return true;
  }
  for (const auto& comp : path) {
    if (ReservedGen(comp)) {
      return true;
    }
  }
  return false;
}

const std::vector<std::string_view>& PrimTypeNames() {
  SpecDefsNames();
  static const std::vector<std::string_view> names = {
      "i8",   "i16",  "i32",  "i64",  "i128", "u8",  "u16", "u32",
      "u64",  "u128", "f16",  "f32",  "f64",  "bool", "char", "usize",
      "isize"};
  return names;
}

const std::vector<std::string_view>& SpecialTypeNames() {
  SpecDefsNames();
  static const std::vector<std::string_view> names = {
      "Self", "Drop", "Bitcopy", "Clone", "Eq", "Hash", "Hasher", "Iterator",
      "Step", "FfiSafe", "string", "bytes", "Modal", "Region",
      "RegionOptions", "CancelToken", "Context", "TestAuthority", "System",
      "IO", "HeapAllocator", "ExecutionDomain", "CpuSet", "Priority",
      "Reactor", "Network", "Time", "MonotonicTime", "WallTime",
      "Duration", "MonotonicInstant", "UtcInstant", "TimeError"};
  return names;
}

const std::vector<std::string_view>& AsyncTypeNames() {
  SpecDefsNames();
  static const std::vector<std::string_view> names = {
      "Async", "Future", "Sequence", "Stream", "Pipe", "Exchange",
      "Tracked", "Spawned"};
  return names;
}

const std::vector<IdKey>& PrimTypeKeys() {
  SpecDefsNames();
  static const std::vector<IdKey> keys = MakeKeys(PrimTypeNames());
  return keys;
}

const std::vector<IdKey>& SpecialTypeKeys() {
  SpecDefsNames();
  static const std::vector<IdKey> keys = MakeKeys(SpecialTypeNames());
  return keys;
}

const std::vector<IdKey>& AsyncTypeKeys() {
  SpecDefsNames();
  static const std::vector<IdKey> keys = MakeKeys(AsyncTypeNames());
  return keys;
}

bool KeywordKey(std::string_view idkey) {
  SpecDefsReserved();
  return lexer::IsKeyword(idkey);
}

Scope UniverseBindings() {
  SpecDefsReserved();
  Scope scope;
  const std::string language_root(project::ActiveLanguageProfile().runtime_root);
  const ast::ModulePath language_module_path{language_root};
  for (const auto name : UniverseProtectedNames()) {
    scope.emplace(IdKeyOf(name),
                  Entity{EntityKind::Type, std::nullopt, std::nullopt,
                         EntitySource::Decl});
  }
  scope.emplace(IdKey{language_root},
	                Entity{EntityKind::ModuleAlias,
	                       language_module_path, std::nullopt,
	                       EntitySource::Decl});
  return scope;
}

}  // namespace ultraviolet::analysis
