#include "03_comptime/comptime_internal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/path.h"
#include "00_core/source_text.h"

namespace cursive::frontend::comptime_internal {

namespace {

ast::TypePtr MakeTypeNode(ast::TypeNode node) {
  auto type = std::make_shared<ast::Type>();
  type->span = core::Span{};
  type->node = std::move(node);
  return type;
}

ast::TypePtr MakeTypePathAst(ast::TypePath path) {
  ast::TypePathType node;
  node.path = std::move(path);
  return MakeTypeNode(std::move(node));
}

ast::TypePtr MakeTypePrimAst(std::string_view name) {
  ast::TypePrim node;
  node.name = std::string(name);
  return MakeTypeNode(std::move(node));
}

ast::TypePtr MakeTypeStringAst(ast::StringState state) {
  ast::TypeString node;
  node.state = state;
  return MakeTypeNode(std::move(node));
}

ast::TypePtr MakeTypeBytesAst(ast::BytesState state) {
  ast::TypeBytes node;
  node.state = state;
  return MakeTypeNode(std::move(node));
}

ast::TypePtr MakeTypePermAst(ast::TypePerm perm, ast::TypePtr base) {
  ast::TypePermType node;
  node.perm = perm;
  node.base = std::move(base);
  return MakeTypeNode(std::move(node));
}

ast::TypePtr MakeTypeSliceAst(ast::TypePtr element) {
  ast::TypeSlice node;
  node.element = std::move(element);
  return MakeTypeNode(std::move(node));
}

ast::ModalStateRef MakeOutcomeStateRef(ast::TypePtr value_type,
                                       std::string_view state) {
  ast::ModalStateRef ref;
  ref.path = {"Outcome"};
  ref.generic_args = {std::move(value_type), MakeTypePathAst({"IoError"})};
  ref.state = std::string(state);
  ast::SyncModalStateRefFromFields(ref);
  return ref;
}

CtValue MakeOutcomeValue(CtValue value, ast::TypePtr value_type) {
  auto outcome = std::make_shared<CtModalState>();
  outcome->target = MakeOutcomeStateRef(std::move(value_type), "Value");
  outcome->fields = {{"value", std::move(value)}};
  return outcome;
}

CtValue MakeOutcomeError(CtValue error, ast::TypePtr value_type) {
  auto outcome = std::make_shared<CtModalState>();
  outcome->target = MakeOutcomeStateRef(std::move(value_type), "Error");
  outcome->fields = {{"error", std::move(error)}};
  return outcome;
}

bool IsIoErrorValue(const CtValue& value) {
  const auto* enum_value = std::get_if<std::shared_ptr<CtEnum>>(&value);
  return enum_value && *enum_value && (*enum_value)->path == Path{"IoError"};
}

CtValue MakeProjectFileOutcome(CtValue value, ast::TypePtr value_type) {
  if (IsIoErrorValue(value)) {
    return MakeOutcomeError(std::move(value), std::move(value_type));
  }
  return MakeOutcomeValue(std::move(value), std::move(value_type));
}

std::string MapIoErrorVariant(const std::error_code& ec) {
  if (ec == std::errc::permission_denied) {
    return "PermissionDenied";
  }
  return "IoFailure";
}

std::string SnapshotRootText(const std::filesystem::path& project_root) {
  const std::string root_text = project_root.generic_string();
  if (const auto canon = core::Canon(root_text)) {
    return *canon;
  }
  return core::Normalize(root_text);
}

std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path,
                                       bool& ok) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    ok = false;
    return {};
  }

  std::vector<std::uint8_t> bytes;
  input.seekg(0, std::ios::end);
  const std::streampos size = input.tellg();
  if (size < 0) {
    ok = false;
    return {};
  }
  input.seekg(0, std::ios::beg);

  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!input.good() && !input.eof()) {
    ok = false;
    return {};
  }

  ok = true;
  return bytes;
}

void PopulateFileEntry(ProjectFileSnapshotEntry& entry,
                       ProjectFileSnapshot& snapshot,
                       const std::filesystem::path& host_path) {
  entry.kind = ProjectFileSnapshotKind::File;
  snapshot.captured_file_count += 1;

  bool ok = false;
  entry.bytes = ReadAllBytes(host_path, ok);
  if (!ok) {
    entry.read_bytes_error = "IoFailure";
    entry.read_error = "IoFailure";
    entry.bytes.clear();
    return;
  }
  snapshot.captured_byte_count +=
      static_cast<std::uint64_t>(entry.bytes.size());

  const core::DecodeResult decoded = core::Decode(entry.bytes);
  if (!decoded.ok) {
    entry.read_error = "IoFailure";
  }
}

void CapturePathRecursive(ProjectFileSnapshot& snapshot,
                          const std::filesystem::path& host_path,
                          const std::string& canonical_text);

void PopulateDirectoryEntry(ProjectFileSnapshot& snapshot,
                            ProjectFileSnapshotEntry& entry,
                            const std::filesystem::path& host_path,
                            const std::string& canonical_text) {
  entry.kind = ProjectFileSnapshotKind::Directory;
  snapshot.captured_directory_count += 1;

  std::error_code ec;
  std::filesystem::directory_iterator iter(host_path, ec);
  if (ec) {
    entry.list_dir_error = MapIoErrorVariant(ec);
    return;
  }

  std::vector<std::pair<std::string, std::filesystem::path>> children;
  for (std::filesystem::directory_iterator end; iter != end; iter.increment(ec)) {
    if (ec) {
      entry.list_dir_error = MapIoErrorVariant(ec);
      entry.dir_entries.clear();
      return;
    }
    children.emplace_back(iter->path().filename().generic_string(), iter->path());
  }
  if (ec) {
    entry.list_dir_error = MapIoErrorVariant(ec);
    entry.dir_entries.clear();
    return;
  }

  std::stable_sort(children.begin(), children.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.first < rhs.first;
                   });

  entry.dir_entries.reserve(children.size());
  for (const auto& child : children) {
    entry.dir_entries.push_back(child.first);
  }

  for (const auto& child : children) {
    const std::string child_path = core::Join(canonical_text, child.first);
    CapturePathRecursive(snapshot, child.second, child_path);
  }
}

void CapturePathRecursive(ProjectFileSnapshot& snapshot,
                          const std::filesystem::path& host_path,
                          const std::string& canonical_text) {
  ProjectFileSnapshotEntry entry;

  std::error_code ec;
  const auto status = std::filesystem::status(host_path, ec);
  if (ec) {
    const std::string error = MapIoErrorVariant(ec);
    entry.exists_error = error;
    entry.read_error = error;
    entry.read_bytes_error = error;
    entry.list_dir_error = error;
    snapshot.entries[canonical_text] = std::move(entry);
    return;
  }

  if (!std::filesystem::exists(status)) {
    return;
  }

  if (std::filesystem::is_directory(status)) {
    PopulateDirectoryEntry(snapshot, entry, host_path, canonical_text);
    snapshot.entries[canonical_text] = std::move(entry);
    return;
  }

  if (std::filesystem::is_regular_file(status)) {
    PopulateFileEntry(entry, snapshot, host_path);
    snapshot.entries[canonical_text] = std::move(entry);
    return;
  }

  entry.kind = ProjectFileSnapshotKind::Other;
  entry.read_error = "IoFailure";
  entry.read_bytes_error = "IoFailure";
  entry.list_dir_error = "IoFailure";
  snapshot.entries[canonical_text] = std::move(entry);
}

std::optional<std::string> RestrictProjectPath(
    const ProjectFileSnapshot& snapshot,
    std::string_view raw_path) {
  SPEC_RULE("CtProjectPath");
  const auto resolved = core::Resolve(snapshot.root_text, raw_path);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  return resolved->path;
}

std::optional<std::string> FindAncestorAccessError(
    const ProjectFileSnapshot& snapshot,
    const std::string& canonical_text) {
  std::vector<std::string> comps = core::PathComps(canonical_text);
  while (!comps.empty()) {
    const std::string current = core::JoinComp(comps);
    const auto entry_it = snapshot.entries.find(current);
    if (entry_it != snapshot.entries.end()) {
      if (entry_it->second.list_dir_error.has_value()) {
        return entry_it->second.list_dir_error;
      }
      if (entry_it->second.exists_error.has_value()) {
        return entry_it->second.exists_error;
      }
    }
    comps.pop_back();
  }
  return std::nullopt;
}

CtValue SnapshotReadTextResult(const ProjectFileSnapshot& snapshot,
                               const std::string& canonical_text) {
  const auto entry_it = snapshot.entries.find(canonical_text);
  if (entry_it == snapshot.entries.end()) {
    if (const auto ancestor_error =
            FindAncestorAccessError(snapshot, canonical_text)) {
      return MakeIoErrorValue(*ancestor_error);
    }
    return MakeIoErrorValue("NotFound");
  }

  const ProjectFileSnapshotEntry& entry = entry_it->second;
  if (entry.exists_error.has_value()) {
    return MakeIoErrorValue(*entry.exists_error);
  }
  if (entry.kind != ProjectFileSnapshotKind::File) {
    return MakeIoErrorValue("IoFailure");
  }
  if (entry.read_error.has_value()) {
    return MakeIoErrorValue(*entry.read_error);
  }
  return CtString{std::string(entry.bytes.begin(), entry.bytes.end())};
}

CtValue SnapshotReadBytesResult(const ProjectFileSnapshot& snapshot,
                                const std::string& canonical_text) {
  const auto entry_it = snapshot.entries.find(canonical_text);
  if (entry_it == snapshot.entries.end()) {
    if (const auto ancestor_error =
            FindAncestorAccessError(snapshot, canonical_text)) {
      return MakeIoErrorValue(*ancestor_error);
    }
    return MakeIoErrorValue("NotFound");
  }

  const ProjectFileSnapshotEntry& entry = entry_it->second;
  if (entry.exists_error.has_value()) {
    return MakeIoErrorValue(*entry.exists_error);
  }
  if (entry.kind != ProjectFileSnapshotKind::File) {
    return MakeIoErrorValue("IoFailure");
  }
  if (entry.read_bytes_error.has_value()) {
    return MakeIoErrorValue(*entry.read_bytes_error);
  }
  return CtBytes{std::string(entry.bytes.begin(), entry.bytes.end())};
}

CtValue SnapshotExistsResult(const ProjectFileSnapshot& snapshot,
                             const std::string& canonical_text) {
  SPEC_RULE("CtExistsResult(fs, q)");
  const auto entry_it = snapshot.entries.find(canonical_text);
  if (entry_it == snapshot.entries.end()) {
    if (const auto ancestor_error =
            FindAncestorAccessError(snapshot, canonical_text)) {
      return MakeIoErrorValue(*ancestor_error);
    }
    return MakeCtBool(false);
  }

  const ProjectFileSnapshotEntry& entry = entry_it->second;
  if (entry.exists_error.has_value()) {
    return MakeIoErrorValue(*entry.exists_error);
  }
  return MakeCtBool(true);
}

CtValue SnapshotListDirResult(const ProjectFileSnapshot& snapshot,
                              const std::string& canonical_text) {
  SPEC_RULE("CtListDirResult(fs, q)");
  const auto entry_it = snapshot.entries.find(canonical_text);
  if (entry_it == snapshot.entries.end()) {
    if (const auto ancestor_error =
            FindAncestorAccessError(snapshot, canonical_text)) {
      return MakeIoErrorValue(*ancestor_error);
    }
    return MakeIoErrorValue("NotFound");
  }

  const ProjectFileSnapshotEntry& entry = entry_it->second;
  if (entry.exists_error.has_value()) {
    return MakeIoErrorValue(*entry.exists_error);
  }
  if (entry.kind != ProjectFileSnapshotKind::Directory) {
    return MakeIoErrorValue("IoFailure");
  }
  if (entry.list_dir_error.has_value()) {
    return MakeIoErrorValue(*entry.list_dir_error);
  }

  auto slice = std::make_shared<CtSlice>();
  slice->elements.reserve(entry.dir_entries.size());
  for (const auto& name : entry.dir_entries) {
    slice->elements.emplace_back(CtString{name});
  }
  return slice;
}

}  // namespace

CtValue MakeIoErrorValue(std::string_view variant) {
  auto error = std::make_shared<CtEnum>();
  error->path = {"IoError"};
  error->variant = std::string(variant);
  error->payload = std::monostate{};
  return error;
}

std::shared_ptr<ProjectFileSnapshot> CaptureProjectFileSnapshot(
    const std::filesystem::path& project_root) {
  auto snapshot = std::make_shared<ProjectFileSnapshot>();
  snapshot->host_root = project_root;
  snapshot->root_text = SnapshotRootText(project_root);
  CapturePathRecursive(*snapshot, project_root, snapshot->root_text);
  return snapshot;
}

std::optional<EvalResult> EvalProjectFilesMethod(const ast::MethodCallExpr& call,
                                                 CtEnv& env) {
  if (!call.receiver ||
      !std::holds_alternative<ast::IdentifierExpr>(call.receiver->node)) {
    return std::nullopt;
  }
  const auto& recv = std::get<ast::IdentifierExpr>(call.receiver->node).name;
  if (recv != "files") {
    return std::nullopt;
  }

  if (call.name == "project_root" && call.args.empty()) {
    SPEC_RULE("CtBuiltin-ProjectRoot");
    EvalResult result;
    result.ok = true;
    result.value = CtString{CtProjectRoot(env).generic_string()};
    return result;
  }

  const auto snapshot = CtFiles(env);
  if (call.args.size() != 1 || !snapshot) {
    return EvalResult{};
  }

  const auto value = EvalExpr(call.args[0].value, env);
  if (!value.ok) {
    return value;
  }

  const auto* path_arg = std::get_if<CtString>(&value.value);
  if (!path_arg) {
    return EvalResult{};
  }

  EvalResult result;
  result.ok = true;

  const auto restricted = RestrictProjectPath(*snapshot, path_arg->value);
  if (!restricted.has_value()) {
    if (call.name == "read") {
      SPEC_RULE("CtBuiltin-Read-InvalidPath");
    } else if (call.name == "read_bytes") {
      SPEC_RULE("CtBuiltin-ReadBytes-InvalidPath");
    } else if (call.name == "exists") {
      SPEC_RULE("CtBuiltin-Exists-InvalidPath");
    } else if (call.name == "list_dir") {
      SPEC_RULE("CtBuiltin-ListDir-InvalidPath");
    } else {
      return std::nullopt;
    }
    ast::TypePtr value_type;
    if (call.name == "read") {
      value_type = MakeTypePermAst(
          ast::TypePerm::Unique,
          MakeTypeStringAst(ast::StringState::Managed));
    } else if (call.name == "read_bytes") {
      value_type = MakeTypePermAst(
          ast::TypePerm::Unique,
          MakeTypeBytesAst(ast::BytesState::Managed));
    } else if (call.name == "exists") {
      value_type = MakeTypePrimAst("bool");
    } else {
      value_type = MakeTypeSliceAst(MakeTypeStringAst(ast::StringState::Managed));
    }
    result.value =
        MakeOutcomeError(MakeIoErrorValue("InvalidPath"), std::move(value_type));
    return result;
  }

  if (call.name == "read") {
    SPEC_RULE("CtBuiltin-Read");
    result.value = MakeProjectFileOutcome(
        SnapshotReadTextResult(*snapshot, *restricted),
        MakeTypePermAst(
            ast::TypePerm::Unique,
            MakeTypeStringAst(ast::StringState::Managed)));
    return result;
  }
  if (call.name == "read_bytes") {
    SPEC_RULE("CtBuiltin-ReadBytes");
    result.value = MakeProjectFileOutcome(
        SnapshotReadBytesResult(*snapshot, *restricted),
        MakeTypePermAst(
            ast::TypePerm::Unique,
            MakeTypeBytesAst(ast::BytesState::Managed)));
    return result;
  }
  if (call.name == "exists") {
    SPEC_RULE("CtBuiltin-Exists");
    result.value = MakeProjectFileOutcome(
        SnapshotExistsResult(*snapshot, *restricted),
        MakeTypePrimAst("bool"));
    return result;
  }
  if (call.name == "list_dir") {
    SPEC_RULE("CtBuiltin-ListDir");
    result.value = MakeProjectFileOutcome(
        SnapshotListDirResult(*snapshot, *restricted),
        MakeTypeSliceAst(MakeTypeStringAst(ast::StringState::Managed)));
    return result;
  }

  return std::nullopt;
}

}  // namespace cursive::frontend::comptime_internal
