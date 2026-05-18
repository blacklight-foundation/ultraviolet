#include "06_driver/comptime_options.h"

namespace ultraviolet::driver {

frontend::ComptimePassOptions BuildComptimeOptions(
    const project::Project& project) {
  frontend::ComptimePassOptions options;
  options.project_root = project.root;
  options.fallback_source_root = project.source_root;
  options.source_roots_by_assembly.reserve(project.assemblies.size());
  for (const auto& assembly : project.assemblies) {
    options.source_roots_by_assembly[assembly.name] = assembly.source_root;
  }
  return options;
}

}  // namespace ultraviolet::driver
