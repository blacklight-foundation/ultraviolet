#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "01_project/language_profile.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

// Built-in names are available as prelude identifiers and as <language-root>::<Name>.
inline bool PathMatchesBuiltinName(const std::vector<std::string>& path,
                                   std::string_view name) {
  if (path.empty()) {
    return false;
  }
  if (IdEq(path.back(), name)) {
    return true;
  }
  if (path.size() >= 2 &&
      IdEq(path[path.size() - 2], project::ActiveLanguageProfile().runtime_root) &&
      IdEq(path.back(), name)) {
    return true;
  }
  return false;
}

}  // namespace cursive::analysis
