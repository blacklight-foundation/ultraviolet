#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cursive::driver {

struct IncrementalModuleInfo {
  std::string source_hash;
  std::string public_hash;
  std::string full_hash;
  std::vector<std::string> dependencies;
};

struct IncrementalManifestModuleState {
  IncrementalModuleInfo info;
  std::string obj_hash;
  std::string ir_hash;
};

struct IncrementalManifestState {
  std::string format = "1";
  std::string assembly;
  std::string build_key;
  std::string emit_ir;
  std::string kind;
  std::string link_kind;
  std::string link_fingerprint;
  std::unordered_map<std::string, IncrementalManifestModuleState> modules;
};

}  // namespace cursive::driver
