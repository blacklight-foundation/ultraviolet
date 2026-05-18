#include "06_driver/fingerprints.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <variant>

#include "00_core/hash.h"
#include "00_core/path.h"
#include "00_core/symbols.h"
#include "02_source/parser/parse_modules.h"

namespace ultraviolet::driver {

namespace {

void MixHashByte(std::uint64_t& hash, std::uint8_t byte) {
  hash ^= static_cast<std::uint64_t>(byte);
  hash *= core::kFNVPrime64;
}

void MixHashString(std::uint64_t& hash, std::string_view value) {
  for (const unsigned char ch : value) {
    MixHashByte(hash, static_cast<std::uint8_t>(ch));
  }
  MixHashByte(hash, 0xFFU);
}

std::optional<std::string> ResolveImportedModule(
    const ast::ImportDecl& import,
    const std::unordered_set<std::string>& known_modules) {
  if (import.path.empty()) {
    return std::nullopt;
  }

  std::string candidate;
  std::optional<std::string> best;
  for (std::size_t i = 0; i < import.path.size(); ++i) {
    if (i > 0) {
      candidate.append("::");
    }
    candidate.append(import.path[i]);
    if (known_modules.find(candidate) != known_modules.end()) {
      best = candidate;
    }
  }
  return best;
}

}  // namespace

std::optional<std::string> ComputeFileSourceHash(
    const std::filesystem::path& file,
    core::DiagnosticStream& diags) {
  const auto bytes = ultraviolet::frontend::ReadBytesDefault(file);
  for (const auto& diag : bytes.diags) {
    core::Emit(diags, diag);
  }
  if (!bytes.bytes.has_value()) {
    return std::nullopt;
  }

  std::uint64_t hash = core::kFNVOffset64;
  for (const auto byte : *bytes.bytes) {
    MixHashByte(hash, byte);
  }
  return core::Hex64(hash);
}

std::optional<std::string> HashFileBytes(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::string buffer((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  if (!in && !in.eof()) {
    return std::nullopt;
  }
  return HashBytes(buffer);
}

std::string HashBytes(std::string_view bytes) {
  return core::Hex64(core::FNV1a64(bytes));
}

std::string LinkInputFingerprintField(const std::filesystem::path& path) {
  const std::string normalized = core::Normalize(path.generic_string());
  const auto hash = HashFileBytes(path);
  if (!hash.has_value()) {
    return normalized + ":missing";
  }
  return normalized + ":" + *hash;
}

bool IsExternalDependencyMarker(std::string_view dep) {
  constexpr std::string_view marker = "__external__:";
  return dep.size() >= marker.size() &&
         dep.compare(0, marker.size(), marker) == 0;
}

std::string HashFields(const std::vector<std::string>& fields) {
  std::uint64_t hash = core::kFNVOffset64;
  for (const auto& field : fields) {
    MixHashString(hash, field);
  }
  return core::Hex64(hash);
}

std::optional<std::string> ComputeModuleSourceHash(
    const project::ModuleInfo& module,
    core::DiagnosticStream& diags) {
  const auto unit = project::CompilationUnit(module.dir);
  for (const auto& diag : unit.diags) {
    core::Emit(diags, diag);
  }
  if (core::HasError(unit.diags)) {
    return std::nullopt;
  }

  std::uint64_t hash = core::kFNVOffset64;
  MixHashString(hash, module.path);

  for (const auto& file : unit.files) {
    MixHashString(hash, core::Normalize(file.generic_string()));
    const auto file_hash = ComputeFileSourceHash(file, diags);
    if (!file_hash.has_value()) {
      return std::nullopt;
    }
    MixHashString(hash, *file_hash);
    MixHashByte(hash, 0x00U);
  }

  return core::Hex64(hash);
}

std::unordered_map<std::string, std::vector<std::string>> BuildModuleDeps(
    const std::vector<ast::ASTModule>& modules,
    const std::unordered_set<std::string>& known_modules) {
  std::unordered_map<std::string, std::vector<std::string>> deps_by_module;
  deps_by_module.reserve(modules.size());

  for (const auto& module : modules) {
    const std::string module_path = core::StringOfPath(module.path);
    if (known_modules.find(module_path) == known_modules.end()) {
      continue;
    }

    std::unordered_set<std::string> dep_set;
    for (const auto& item : module.items) {
      const auto* import = std::get_if<ast::ImportDecl>(&item);
      if (!import) {
        continue;
      }
      const auto target = ResolveImportedModule(*import, known_modules);
      if (!target.has_value()) {
        dep_set.insert("__external__:" + core::StringOfPath(import->path));
        continue;
      }
      if (*target == module_path) {
        continue;
      }
      dep_set.insert(*target);
    }

    std::vector<std::string> deps(dep_set.begin(), dep_set.end());
    std::sort(deps.begin(), deps.end());
    deps_by_module[module_path] = std::move(deps);
  }

  return deps_by_module;
}

}  // namespace ultraviolet::driver
