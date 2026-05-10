#include "06_driver/incremental.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "00_core/path.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "01_project/language_profile.h"
#include "01_project/link.h"
#include "01_project/outputs.h"
#include "06_driver/fingerprints.h"
#include "06_driver/version.h"

namespace cursive::driver {

namespace {

std::vector<std::string> SplitByChar(std::string_view text, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t pos = text.find(sep, start);
    if (pos == std::string_view::npos) {
      out.emplace_back(text.substr(start));
      break;
    }
    out.emplace_back(text.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

std::string JoinByChar(const std::vector<std::string>& values, char sep) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      oss << sep;
    }
    oss << values[i];
  }
  return oss.str();
}

}  // namespace

bool IncrementalEnabled() {
  const std::optional<bool> override = cursive::core::IncrementalOverride();
  if (override.has_value()) {
    return *override;
  }
  const std::optional<bool> manifest = cursive::core::ManifestIncremental();
  if (manifest.has_value()) {
    return *manifest;
  }
  return true;
}

std::filesystem::path IncrementalDirPath(const project::Project& project) {
  return project.outputs.root /
         std::string(project::LanguageProfileFor(project.language).incremental_dir_name);
}

std::filesystem::path IncrementalManifestPath(
    const project::Project& project) {
  return IncrementalDirPath(project) / (project.assembly.name + ".manifest");
}

std::optional<IncrementalManifestState> LoadIncrementalManifest(
    const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  IncrementalManifestState state;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = SplitByChar(line, '\t');
    if (fields.empty()) {
      continue;
    }
    if (fields[0] == "H" && fields.size() >= 3) {
      if (fields[1] == "format") {
        state.format = fields[2];
      } else if (fields[1] == "assembly") {
        state.assembly = fields[2];
      } else if (fields[1] == "build_key") {
        state.build_key = fields[2];
      } else if (fields[1] == "emit_ir") {
        state.emit_ir = fields[2];
      } else if (fields[1] == "kind") {
        state.kind = fields[2];
      } else if (fields[1] == "link_kind") {
        state.link_kind = fields[2];
      } else if (fields[1] == "link") {
        state.link_fingerprint = fields[2];
      }
      continue;
    }
    if (fields[0] != "M" || fields.size() < 8) {
      continue;
    }

    IncrementalManifestModuleState module_state;
    module_state.info.source_hash = fields[2];
    module_state.info.public_hash = fields[3];
    module_state.info.full_hash = fields[4];
    module_state.obj_hash = fields[5];
    module_state.ir_hash = fields[6];
    if (!fields[7].empty()) {
      module_state.info.dependencies = SplitByChar(fields[7], ',');
    }
    state.modules[fields[1]] = std::move(module_state);
  }

  if (!in && !in.eof()) {
    return std::nullopt;
  }
  return state;
}

bool SaveIncrementalManifest(
    const project::Project& project,
    const std::function<bool(const std::filesystem::path& path)>& ensure_dir,
    const std::function<bool(const std::filesystem::path& path,
                             std::string_view bytes)>& write_file,
    const IncrementalManifestState& state) {
  const auto dir = IncrementalDirPath(project);
  if (!ensure_dir(dir)) {
    return false;
  }

  std::ostringstream out;
  out << "H\tformat\t" << state.format << "\n";
  out << "H\tassembly\t" << state.assembly << "\n";
  out << "H\tbuild_key\t" << state.build_key << "\n";
  out << "H\temit_ir\t" << state.emit_ir << "\n";
  out << "H\tkind\t" << state.kind << "\n";
  out << "H\tlink_kind\t" << state.link_kind << "\n";
  out << "H\tlink\t" << state.link_fingerprint << "\n";

  for (const auto& module : project.modules) {
    const auto it = state.modules.find(module.path);
    if (it == state.modules.end()) {
      continue;
    }
    const auto& mod = it->second;
    out << "M\t" << module.path << "\t" << mod.info.source_hash << "\t"
        << mod.info.public_hash << "\t" << mod.info.full_hash << "\t"
        << mod.obj_hash << "\t" << mod.ir_hash << "\t"
        << JoinByChar(mod.info.dependencies, ',') << "\n";
  }

  return write_file(IncrementalManifestPath(project), out.str());
}

namespace {

std::string CompilerFingerprintField(
    const std::filesystem::path& compiler_executable_path) {
  if (compiler_executable_path.empty()) {
    return "compiler=<unknown>";
  }
  std::error_code ec;
  std::filesystem::path normalized =
      std::filesystem::weakly_canonical(compiler_executable_path, ec);
  if (ec) {
    ec.clear();
    normalized = std::filesystem::absolute(compiler_executable_path, ec);
    if (ec) {
      normalized = compiler_executable_path;
    }
  }
  return "compiler=" + LinkInputFingerprintField(normalized);
}

}  // namespace

std::string BuildIncrementalBuildKey(
    const project::Project& project,
    project::TargetProfile target_profile,
    const CliOptions& opts,
    const std::filesystem::path& compiler_executable_path,
    std::string_view runtime_log_file_path) {
  std::vector<std::string> fields;
  fields.reserve(18);
  fields.push_back("v3");
  fields.push_back(GetVersionString());
  fields.push_back(CompilerFingerprintField(compiler_executable_path));
  fields.push_back(project.assembly.name);
  fields.push_back(project.assembly.kind);
  fields.push_back(project.assembly.link_kind.value_or("none"));
  fields.push_back(std::string(project::TargetProfileName(target_profile)));
  fields.push_back(project.root.generic_string());
  fields.push_back(project.source_root.generic_string());
  fields.push_back(project.outputs.root.generic_string());
  fields.push_back(project.assembly.emit_ir.value_or("none"));
  fields.push_back(std::string("log_enabled=") + (opts.log_enabled ? "1" : "0"));
  fields.push_back(std::string("log_to_console=") +
                   (opts.log_to_console ? "1" : "0"));
  fields.push_back(std::string("log_to_file=") +
                   (opts.log_to_file ? "1" : "0"));
  fields.push_back(std::string("trace=") + (opts.trace ? "1" : "0"));
  fields.push_back("trace_filter_mask=" +
                   std::to_string(opts.trace_filter_mask.value_or(0u)));
  fields.push_back("trace_min_level=" +
                   std::to_string(opts.trace_min_level.value_or(0u)));
  fields.push_back("log_file=" + std::string(runtime_log_file_path));
  if (const auto runtime_lib =
          project::ResolveRuntimeLib(project, target_profile);
      runtime_lib.has_value()) {
    fields.push_back("runtime_lib=" + LinkInputFingerprintField(*runtime_lib));
  } else {
    fields.push_back("runtime_lib=<missing>");
  }
  return HashFields(fields);
}

IncrementalBuildDataResult BuildIncrementalBuildData(
    const project::Project& project,
    const std::vector<ast::ASTModule>& resolved_modules,
    const std::string& build_key,
    const ModuleInterfaceHashFn& module_interface_hash,
    core::DiagnosticStream& diags) {
  IncrementalBuildDataResult result;
  result.build_key = build_key;

  std::unordered_set<std::string> module_set;
  module_set.reserve(std::max(project.modules.size(), resolved_modules.size()));
  for (const auto& module : resolved_modules) {
    module_set.insert(core::StringOfPath(module.path));
  }
  for (const auto& module : project.modules) {
    module_set.insert(module.path);
  }

  const auto deps_by_module = BuildModuleDeps(resolved_modules, module_set);

  std::unordered_map<std::string, std::string> source_hashes;
  source_hashes.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    const auto source_hash = ComputeModuleSourceHash(module, diags);
    if (!source_hash.has_value()) {
      return result;
    }
    source_hashes[module.path] = *source_hash;
  }

  std::unordered_map<std::string, const ast::ASTModule*> ast_by_module;
  ast_by_module.reserve(resolved_modules.size());
  for (const auto& module : resolved_modules) {
    ast_by_module[core::StringOfPath(module.path)] = &module;
  }

  std::unordered_map<std::string, std::string> own_public_hashes;
  own_public_hashes.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    const auto ast_it = ast_by_module.find(module.path);
    if (ast_it == ast_by_module.end() || ast_it->second == nullptr) {
      own_public_hashes[module.path] = source_hashes[module.path];
      continue;
    }
    own_public_hashes[module.path] = module_interface_hash(*ast_it->second);
  }

  result.modules.reserve(project.modules.size());

  std::unordered_map<std::string, std::string> public_hash_cache;
  public_hash_cache.reserve(project.modules.size());
  std::unordered_set<std::string> public_hash_visiting;

  std::function<std::string(const std::string&)> compute_public_hash =
      [&](const std::string& module_path) -> std::string {
    const auto cached_it = public_hash_cache.find(module_path);
    if (cached_it != public_hash_cache.end()) {
      return cached_it->second;
    }

    const auto own_it = own_public_hashes.find(module_path);
    const std::string own_hash =
        own_it != own_public_hashes.end() ? own_it->second : "missing";

    if (public_hash_visiting.find(module_path) != public_hash_visiting.end()) {
      std::vector<std::string> cycle_fields;
      cycle_fields.reserve(6);
      cycle_fields.push_back("public-v1");
      cycle_fields.push_back(result.build_key);
      cycle_fields.push_back(module_path);
      cycle_fields.push_back("own=" + own_hash);
      cycle_fields.push_back("cycle=1");
      const std::string cycle_hash = HashFields(cycle_fields);
      public_hash_cache[module_path] = cycle_hash;
      return cycle_hash;
    }

    public_hash_visiting.insert(module_path);

    std::vector<std::string> fields;
    const auto dep_it = deps_by_module.find(module_path);
    const std::size_t dep_count =
        dep_it != deps_by_module.end() ? dep_it->second.size() : 0;
    fields.reserve(5 + dep_count);
    fields.push_back("public-v1");
    fields.push_back(result.build_key);
    fields.push_back(module_path);
    fields.push_back("own=" + own_hash);

    if (dep_it != deps_by_module.end()) {
      for (const auto& dep : dep_it->second) {
        const auto dep_own_it = own_public_hashes.find(dep);
        if (dep_own_it == own_public_hashes.end()) {
          fields.push_back("dep=" + dep + ":missing");
          continue;
        }
        fields.push_back("dep=" + dep + ":" + compute_public_hash(dep));
      }
    }

    const std::string out = HashFields(fields);
    public_hash_visiting.erase(module_path);
    public_hash_cache[module_path] = out;
    return out;
  };

  std::unordered_map<std::string, std::string> full_hash_cache;
  full_hash_cache.reserve(project.modules.size());
  std::unordered_set<std::string> full_hash_visiting;

  std::function<std::string(const std::string&)> compute_full_hash =
      [&](const std::string& module_path) -> std::string {
    const auto cached_it = full_hash_cache.find(module_path);
    if (cached_it != full_hash_cache.end()) {
      return cached_it->second;
    }

    const auto source_it = source_hashes.find(module_path);
    const std::string source_hash =
        source_it != source_hashes.end() ? source_it->second : "missing";

    if (full_hash_visiting.find(module_path) != full_hash_visiting.end()) {
      std::vector<std::string> cycle_fields;
      cycle_fields.reserve(5);
      cycle_fields.push_back("v3");
      cycle_fields.push_back(result.build_key);
      cycle_fields.push_back(module_path);
      cycle_fields.push_back("source=" + source_hash);
      cycle_fields.push_back("public=" + compute_public_hash(module_path));
      cycle_fields.push_back("cycle=1");
      const std::string cycle_hash = HashFields(cycle_fields);
      full_hash_cache[module_path] = cycle_hash;
      return cycle_hash;
    }

    full_hash_visiting.insert(module_path);

    std::vector<std::string> fields;
    const auto dep_it = deps_by_module.find(module_path);
    const std::size_t dep_count =
        dep_it != deps_by_module.end() ? dep_it->second.size() : 0;
    fields.reserve(6 + dep_count);
    fields.push_back("v3");
    fields.push_back(result.build_key);
    fields.push_back(module_path);
    fields.push_back("source=" + source_hash);
    fields.push_back("public=" + compute_public_hash(module_path));

    if (dep_it != deps_by_module.end()) {
      for (const auto& dep : dep_it->second) {
        const auto dep_public_it = own_public_hashes.find(dep);
        if (dep_public_it == own_public_hashes.end()) {
          fields.push_back("dep=" + dep + ":missing");
          continue;
        }
        fields.push_back("dep=" + dep + ":" + compute_public_hash(dep));
      }
    }

    const std::string out = HashFields(fields);
    full_hash_visiting.erase(module_path);
    full_hash_cache[module_path] = out;
    return out;
  };

  for (const auto& module : project.modules) {
    IncrementalModuleInfo info;
    info.source_hash = source_hashes[module.path];
    info.public_hash = compute_public_hash(module.path);

    const auto dep_it = deps_by_module.find(module.path);
    if (dep_it != deps_by_module.end()) {
      info.dependencies = dep_it->second;
    }

    info.full_hash = compute_full_hash(module.path);

    result.modules[module.path] = std::move(info);
  }

  result.ok = true;
  return result;
}

std::string ComputeLinkFingerprint(
    const project::Project& proj,
    project::TargetProfile target_profile,
    const std::string& build_key,
    const std::unordered_map<std::string, IncrementalManifestModuleState>& modules,
    const std::vector<std::filesystem::path>& link_inputs,
    const std::optional<std::filesystem::path>& runtime_lib,
    const project::LinkPlan& plan,
    std::string_view emit_ir) {
  std::vector<std::string> fields;
  fields.reserve(10 + proj.modules.size() + link_inputs.size() +
                 plan.export_symbols.size() + plan.data_export_symbols.size());
  fields.push_back("v6");
  fields.push_back(build_key);
  fields.push_back("target=" +
                   std::string(project::TargetProfileName(plan.target_profile)));
  fields.push_back(proj.assembly.name);
  fields.push_back(proj.assembly.kind);
  fields.push_back(proj.assembly.link_kind.value_or("none"));
  fields.push_back(std::string(emit_ir));
  if (project::IsStaticLibrary(proj)) {
    fields.push_back("output=archive");
  } else {
    fields.push_back(plan.output_kind == project::LinkOutputKind::SharedLibrary
                         ? "output=shared"
                         : "output=exe");
  }
  switch (plan.shared_library_lifecycle_mode) {
    case project::SharedLibraryLifecycleMode::None:
      fields.push_back("lifecycle=none");
      break;
    case project::SharedLibraryLifecycleMode::WindowsEntry:
      fields.push_back("lifecycle=windows-entry");
      break;
    case project::SharedLibraryLifecycleMode::PosixCtorDtor:
      fields.push_back("lifecycle=posix-ctor-dtor");
      break;
  }
  fields.push_back("entry=" + plan.entry_symbol.value_or(""));

  std::vector<std::string> exports = plan.export_symbols;
  std::sort(exports.begin(), exports.end());
  exports.erase(std::unique(exports.begin(), exports.end()), exports.end());
  for (const auto& symbol : exports) {
    fields.push_back("export=" + symbol);
  }

  std::vector<std::string> data_exports = plan.data_export_symbols;
  std::sort(data_exports.begin(), data_exports.end());
  data_exports.erase(std::unique(data_exports.begin(), data_exports.end()),
                     data_exports.end());
  for (const auto& symbol : data_exports) {
    fields.push_back("export-data=" + symbol);
  }

  if (runtime_lib.has_value()) {
    fields.push_back("runtime=" + LinkInputFingerprintField(*runtime_lib));
  }
  if (const auto map_path = project::MapPath(proj, target_profile);
      map_path.has_value()) {
    fields.push_back("map=windows-sidecar");
    fields.push_back("map_path=" +
                     core::Normalize(map_path->generic_string()));
  }
  const auto materialized_link_inputs =
      project::MaterializeLinkInputsForTool(proj, target_profile, link_inputs);
  for (const auto& input : materialized_link_inputs) {
    fields.push_back("input=" + LinkInputFingerprintField(input));
  }

  for (const auto& module : proj.modules) {
    const auto it = modules.find(module.path);
    if (it == modules.end()) {
      fields.push_back(module.path + ":missing");
      continue;
    }
    const auto& mod = it->second;
    fields.push_back(module.path + ":" + mod.info.full_hash + ":" +
                     mod.obj_hash + ":" + mod.ir_hash);
  }

  return HashFields(fields);
}

}  // namespace cursive::driver
