#include "01_project/target_platform.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "00_core/compiler_support.h"
#include "00_core/host/services.h"
#include "00_core/path.h"
#include "01_project/compiler_support_paths.h"
#include "01_project/language_profile.h"
#include "01_project/link.h"
#include "01_project/project.h"

namespace ultraviolet::project {

namespace {

constexpr std::uint64_t kWindowsExeStackReserveBytes = 1ull << 20;
constexpr std::uint64_t kWindowsExeStackCommitBytes = 64ull << 10;
constexpr std::string_view kLinuxIcuI18nSidecar = "libicui18n.so.72";
constexpr std::string_view kLinuxIcuUcSidecar = "libicuuc.so.72";
constexpr std::string_view kLinuxIcuDataSidecar = "libicudata.so.72";
constexpr std::string_view kLinuxIcuDataBlobSidecar = "icudt72l.dat";
constexpr std::string_view kMacosRuntimeSupportSidecar =
    "libUltravioletRTSupport.dylib";
constexpr std::string_view kMacosIcuI18nSidecar = "libicui18n.72.dylib";
constexpr std::string_view kMacosIcuUcSidecar = "libicuuc.72.dylib";
constexpr std::string_view kMacosIcuDataSidecar = "libicudata.72.dylib";

[[noreturn]] void UnreachableTargetPlatform() {
  std::abort();
}

std::string PathArgString(const std::filesystem::path& path) {
  const auto utf8 = path.generic_u8string();
  std::string out;
  out.reserve(utf8.size());
  for (const auto ch : utf8) {
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

std::optional<std::string> DarwinBareDylibLinkArg(
    const std::filesystem::path& input) {
  if (input.empty() || input.has_parent_path() || input.is_absolute()) {
    return std::nullopt;
  }

  const std::string filename = input.filename().generic_string();
  constexpr std::string_view prefix = "lib";
  constexpr std::string_view suffix = ".dylib";
  if (filename.size() <= prefix.size() + suffix.size() ||
      filename.rfind(prefix, 0) != 0 ||
      filename.compare(filename.size() - suffix.size(),
                       suffix.size(),
                       suffix) != 0) {
    return std::nullopt;
  }

  const std::string library_name =
      filename.substr(prefix.size(),
                      filename.size() - prefix.size() - suffix.size());
  return "-l" + library_name;
}

std::string DarwinLinkInputArg(const std::filesystem::path& input) {
  if (const auto library_arg = DarwinBareDylibLinkArg(input);
      library_arg.has_value()) {
    return *library_arg;
  }
  return PathArgString(input);
}

std::string LowerAscii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char ch : text) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

std::string TrimAsciiWhitespace(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start]))) {
    ++start;
  }

  std::size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }

  return std::string(text.substr(start, end - start));
}

std::optional<std::string> WslDrivePathArgString(
    const std::filesystem::path& path) {
  const std::string generic = PathArgString(path);
  if (generic.size() < 8 || generic[0] != '/' || generic[1] != 'm' ||
      generic[2] != 'n' || generic[3] != 't' || generic[4] != '/' ||
      generic[6] != '/') {
    return std::nullopt;
  }

  const unsigned char drive_ch = static_cast<unsigned char>(generic[5]);
  if (!std::isalpha(drive_ch)) {
    return std::nullopt;
  }

  std::string out;
  out.reserve(generic.size());
  out.push_back(static_cast<char>(std::toupper(drive_ch)));
  out.push_back(':');
  out.append(generic, 6, std::string::npos);
  return out;
}

bool ToolPathNamesWindowsExecutable(const std::filesystem::path& tool) {
  const std::string extension = LowerAscii(tool.extension().generic_string());
  if (extension == ".exe") {
    return true;
  }

  std::error_code ec;
  const auto target = std::filesystem::read_symlink(tool, ec);
  if (ec || target.empty()) {
    return false;
  }
  return LowerAscii(target.extension().generic_string()) == ".exe";
}

bool CanReadFile(const std::filesystem::path& path) {
  std::ifstream in(TargetHostFilesystemPath(path), std::ios::binary);
  return static_cast<bool>(in);
}

bool IsWin64DirectAggregateSize(std::uint64_t size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

bool IsRegisterPairDirectAggregateSize(std::uint64_t size) {
  return size > 0 && size <= 16;
}

TargetAggregateCarrier IntegerCarrier(std::uint64_t bits) {
  TargetAggregateCarrier carrier;
  carrier.kind = TargetAggregateCarrierKind::Integer;
  carrier.primary_bits = bits;
  return carrier;
}

TargetAggregateCarrier IntegerPairCarrier(std::uint64_t high_bits,
                                          bool packed) {
  TargetAggregateCarrier carrier;
  carrier.kind = TargetAggregateCarrierKind::IntegerPair;
  carrier.primary_bits = 64;
  carrier.secondary_bits = high_bits;
  carrier.packed = packed;
  return carrier;
}

TargetAggregateCarrier IntegerArrayCarrier(std::uint64_t element_bits,
                                           std::uint64_t element_count) {
  TargetAggregateCarrier carrier;
  carrier.kind = TargetAggregateCarrierKind::IntegerArray;
  carrier.element_bits = element_bits;
  carrier.element_count = element_count;
  return carrier;
}

TargetAggregateCarrier IndirectCarrier() {
  TargetAggregateCarrier carrier;
  carrier.kind = TargetAggregateCarrierKind::Indirect;
  return carrier;
}

TargetAggregateCarrier Win64DirectAggregateCarrier(std::uint64_t size) {
  if (!IsWin64DirectAggregateSize(size)) {
    return {};
  }
  return IntegerCarrier(size * 8);
}

TargetAggregateCarrier RegisterPairAggregateCarrier(
    std::uint64_t size,
    bool contains_floating) {
  if (!IsRegisterPairDirectAggregateSize(size) || contains_floating) {
    return {};
  }
  if (size <= 8) {
    return IntegerCarrier(size * 8);
  }
  return IntegerPairCarrier((size - 8) * 8, size != 16);
}

TargetAggregateCarrier AArch64AggregateCarrier(std::uint64_t size,
                                               std::uint64_t align) {
  if (!IsRegisterPairDirectAggregateSize(size)) {
    return {};
  }
  if (size <= 8) {
    return IntegerCarrier(size * 8);
  }
  if (size == 16 && align >= 16) {
    return IntegerCarrier(128);
  }
  if (size == 16) {
    return IntegerArrayCarrier(64, 2);
  }
  return IntegerPairCarrier((size - 8) * 8, true);
}

void AppendExistingUniqueDir(std::vector<std::filesystem::path>& out,
                             const std::filesystem::path& dir) {
  if (dir.empty()) {
    return;
  }
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec) || ec) {
    return;
  }
  if (std::find(out.begin(), out.end(), dir) == out.end()) {
    out.push_back(dir);
  }
}

std::vector<std::filesystem::path> WindowsImportLibSearchDirs() {
  std::vector<std::filesystem::path> out;

  const std::filesystem::path windows_kits_root(
      "C:\\Program Files (x86)\\Windows Kits\\10\\Lib");
  std::error_code ec;
  std::vector<std::filesystem::path> versions;
  if (std::filesystem::is_directory(windows_kits_root, ec) && !ec) {
    for (const auto& entry :
         std::filesystem::directory_iterator(windows_kits_root, ec)) {
      if (ec) {
        break;
      }
      if (entry.is_directory(ec) && !ec) {
        versions.push_back(entry.path());
      }
    }
  }
  std::sort(versions.begin(), versions.end());
  std::reverse(versions.begin(), versions.end());
  for (const auto& version_dir : versions) {
    AppendExistingUniqueDir(out, version_dir / "um" / "x64");
    AppendExistingUniqueDir(out, version_dir / "ucrt" / "x64");
  }

  const std::vector<std::filesystem::path> msvc_roots = {
      std::filesystem::path(
          "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC"),
      std::filesystem::path(
          "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC"),
  };
  for (const auto& msvc_root : msvc_roots) {
    std::vector<std::filesystem::path> msvc_versions;
    ec.clear();
    if (std::filesystem::is_directory(msvc_root, ec) && !ec) {
      for (const auto& entry :
           std::filesystem::directory_iterator(msvc_root, ec)) {
        if (ec) {
          break;
        }
        if (entry.is_directory(ec) && !ec) {
          msvc_versions.push_back(entry.path());
        }
      }
    }
    std::sort(msvc_versions.begin(), msvc_versions.end());
    std::reverse(msvc_versions.begin(), msvc_versions.end());
    for (const auto& version_dir : msvc_versions) {
      AppendExistingUniqueDir(out, version_dir / "lib" / "x64");
    }
  }

  const std::filesystem::path exe_path = core::CurrentExecutablePath();
  const std::filesystem::path bootstrap_root =
      exe_path.parent_path().parent_path().parent_path().parent_path();
  AppendExistingUniqueDir(out, bootstrap_root / "extern" / "icu" / "win64" / "lib64");
  AppendExistingUniqueDir(out, bootstrap_root / "extern" / "icu" / "win64" / "lib");
  AppendExistingUniqueDir(out, bootstrap_root / ".." / "extern" / "icu" / "win64" / "lib64");
  AppendExistingUniqueDir(out, bootstrap_root / ".." / "extern" / "icu" / "win64" / "lib");

  return out;
}

std::vector<std::string> CuratedSharedLibraryExportSymbols(
    const LinkPlan& plan) {
  std::vector<std::string> export_symbols = plan.export_symbols;
  std::vector<std::string> data_export_symbols = plan.data_export_symbols;
  export_symbols.erase(
      std::remove_if(export_symbols.begin(),
                     export_symbols.end(),
                     [](const std::string& symbol) {
                       return TargetIsHiddenSharedLibraryExportSymbol(symbol);
                     }),
      export_symbols.end());
  data_export_symbols.erase(
      std::remove_if(data_export_symbols.begin(),
                     data_export_symbols.end(),
                     [](const std::string& symbol) {
                       return TargetIsHiddenSharedLibraryExportSymbol(symbol);
                     }),
      data_export_symbols.end());
  std::sort(export_symbols.begin(), export_symbols.end());
  export_symbols.erase(
      std::unique(export_symbols.begin(), export_symbols.end()),
      export_symbols.end());
  std::sort(data_export_symbols.begin(), data_export_symbols.end());
  data_export_symbols.erase(
      std::unique(data_export_symbols.begin(), data_export_symbols.end()),
      data_export_symbols.end());
  export_symbols.insert(export_symbols.end(),
                        data_export_symbols.begin(),
                        data_export_symbols.end());
  std::sort(export_symbols.begin(), export_symbols.end());
  export_symbols.erase(
      std::unique(export_symbols.begin(), export_symbols.end()),
      export_symbols.end());
  return export_symbols;
}

std::optional<std::filesystem::path> WritePosixVersionScript(
    const std::filesystem::path& output,
    const std::vector<std::string>& export_symbols) {
  if (export_symbols.empty()) {
    return std::nullopt;
  }

  std::filesystem::path script_path = output;
  script_path += ".exports.map";
  std::error_code ec;
  std::filesystem::create_directories(script_path.parent_path(), ec);

  std::ofstream out(script_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return std::nullopt;
  }

  out << "{\n";
  out << "  global:\n";
  for (const auto& symbol : export_symbols) {
    out << "    " << symbol << ";\n";
  }
  out << "  local:\n";
  out << "    *;\n";
  out << "};\n";
  out.close();
  if (!out) {
    return std::nullopt;
  }
  return script_path;
}

std::filesystem::path MaterializeLinkInputForTool(
    const Project&,
    TargetProfile target_profile,
    const std::filesystem::path& input) {
  if (target_profile != TargetProfile::X86_64Win64 ||
      input.empty() ||
      (!input.has_parent_path() && !input.is_absolute()) ||
      input.extension() != SharedLibSuffix(target_profile)) {
    return input;
  }

  auto candidate = input;
  candidate.replace_extension(ImportLibSuffix(target_profile));
  if (CanReadFile(candidate)) {
    return candidate;
  }

  if (input.has_parent_path() &&
      input.parent_path().filename() == "Binary") {
    candidate =
        input.parent_path().parent_path() / "Library" / input.filename();
    candidate.replace_extension(ImportLibSuffix(target_profile));
    if (CanReadFile(candidate)) {
      return candidate;
    }
  }

  if (input.has_parent_path() && input.parent_path().filename() == "bin") {
    candidate = input.parent_path().parent_path() / "lib" / input.filename();
    candidate.replace_extension(ImportLibSuffix(target_profile));
    if (CanReadFile(candidate)) {
      return candidate;
    }
  }

  return input;
}

bool ArchiverUsesWindowsFlags(const std::filesystem::path& tool) {
  const std::string filename = LowerAscii(tool.filename().string());
  return filename == "llvm-lib" || filename == "llvm-lib.exe";
}

std::optional<std::string> RunProgramCapture(
    const std::filesystem::path& tool,
    const std::vector<std::string>& extra_args) {
  core::HostProcessSpec spec;
  spec.program = tool;
  spec.arguments = extra_args;
  spec.output_mode = core::HostProcessOutputMode::CaptureMerged;
  spec.hide_window = true;
  const auto result = core::RunHostProcess(spec);
  if (!result.launched || result.exit_code != 0) {
    return std::nullopt;
  }
  return result.output;
}

std::optional<std::filesystem::path> MacOSSDKRoot() {
  if (const char* sdkroot = std::getenv("SDKROOT");
      sdkroot != nullptr && sdkroot[0] != '\0') {
    const std::filesystem::path path(sdkroot);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec) && !ec) {
      return path;
    }
  }

  if (const auto output =
          RunProgramCapture("xcrun", {"--sdk", "macosx", "--show-sdk-path"});
      output.has_value()) {
    const std::filesystem::path path(TrimAsciiWhitespace(*output));
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_directory(path, ec) && !ec) {
      return path;
    }
  }

  for (const auto* fallback :
       {"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
        "/Applications/Xcode.app/Contents/Developer/Platforms/"
        "MacOSX.platform/Developer/SDKs/MacOSX.sdk"}) {
    const std::filesystem::path path(fallback);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec) && !ec) {
      return path;
    }
  }

  return std::nullopt;
}

void AppendSearchDirsFromDelimitedList(std::vector<std::filesystem::path>& out,
                                       std::string_view text) {
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ':') {
      const std::string_view segment = text.substr(start, i - start);
      if (!segment.empty()) {
        AppendExistingUniqueDir(out, std::filesystem::path(segment));
      }
      start = i + 1;
    }
  }
}

void AppendClangSearchDirs(std::vector<std::filesystem::path>& out,
                           const std::filesystem::path& linker_tool) {
  const std::filesystem::path tool_dir = linker_tool.parent_path();
  const std::vector<std::string> driver_names = {
      "clang++",
      "clang++-21",
      "clang++-20",
      "clang++-19",
      "clang++-18",
  };

  for (const auto& driver_name : driver_names) {
    const std::filesystem::path driver = tool_dir / driver_name;
    if (!CanReadFile(driver)) {
      continue;
    }
    const auto output = RunProgramCapture(driver, {"--print-search-dirs"});
    if (!output.has_value()) {
      continue;
    }

    std::istringstream lines(*output);
    std::string line;
    while (std::getline(lines, line)) {
      constexpr std::string_view prefix = "libraries: =";
      if (line.rfind(prefix.data(), 0) != 0) {
        continue;
      }
      AppendSearchDirsFromDelimitedList(
          out, std::string_view(line).substr(prefix.size()));
      return;
    }
  }
}

void AppendTargetLibDirs(std::vector<std::filesystem::path>& out,
                         TargetProfile target_profile) {
  switch (target_profile) {
    case TargetProfile::X86_64SysV: {
      const std::filesystem::path gcc_root("/usr/lib/gcc/x86_64-linux-gnu");
      std::vector<std::filesystem::path> gcc_versions;
      std::error_code ec;
      if (std::filesystem::is_directory(gcc_root, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(gcc_root, ec)) {
          if (ec) {
            break;
          }
          if (entry.is_directory(ec) && !ec) {
            gcc_versions.push_back(entry.path());
          }
        }
      }
      std::sort(gcc_versions.begin(), gcc_versions.end());
      for (const auto& version_dir : gcc_versions) {
        AppendExistingUniqueDir(out, version_dir);
        AppendExistingUniqueDir(out,
                                (version_dir / ".." / ".." / ".." / ".." /
                                 "lib64")
                                    .lexically_normal());
      }

      for (const auto* dir : {"/lib/x86_64-linux-gnu",
                              "/usr/lib/x86_64-linux-gnu",
                              "/lib64",
                              "/usr/lib64",
                              "/lib",
                              "/usr/lib",
                              "/usr/local/lib"}) {
        AppendExistingUniqueDir(out, dir);
      }
      break;
    }
    case TargetProfile::AArch64AAPCS64:
      for (const auto* dir : {"/lib/aarch64-linux-gnu",
                              "/usr/lib/aarch64-linux-gnu",
                              "/lib64",
                              "/usr/lib64",
                              "/lib",
                              "/usr/lib",
                              "/usr/local/lib"}) {
        AppendExistingUniqueDir(out, dir);
      }
      break;
    case TargetProfile::X86_64Win64:
      break;
    case TargetProfile::AArch64Darwin:
      break;
  }
}

std::vector<std::filesystem::path> PosixLibrarySearchDirs(
    const std::filesystem::path& linker_tool,
    TargetProfile target_profile) {
  std::vector<std::filesystem::path> out;
  AppendClangSearchDirs(out, linker_tool);
  AppendTargetLibDirs(out, target_profile);
  return out;
}

const char* ElfInterpreterPath(TargetProfile target_profile) {
  switch (target_profile) {
    case TargetProfile::X86_64SysV:
      return "/lib64/ld-linux-x86-64.so.2";
    case TargetProfile::AArch64AAPCS64:
      return "/lib/ld-linux-aarch64.so.1";
    case TargetProfile::X86_64Win64:
    case TargetProfile::AArch64Darwin:
      return nullptr;
  }
  return nullptr;
}

std::vector<std::string> BuildWindowsLinkArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan) {
  const bool shared_library = plan.output_kind == LinkOutputKind::SharedLibrary;
  std::vector<std::string> args;
  args.reserve(inputs.size() + plan.export_symbols.size() +
               plan.data_export_symbols.size() + 8);
  args.push_back(PathArgString(tool));
  args.push_back("/NOLOGO");
  args.push_back("/OUT:" + TargetToolPathArgString(tool, output));
  auto map_output = output;
  map_output.replace_extension(".map");
  if (shared_library) {
    args.push_back("/DLL");
    if (plan.shared_library_lifecycle_mode ==
        SharedLibraryLifecycleMode::WindowsEntry) {
      const std::string entry_symbol =
          plan.entry_symbol.value_or(
              std::string(ActiveLanguageProfile().library_entry_symbol));
      args.push_back("/ENTRY:" + entry_symbol);
    }
    if (import_lib.has_value()) {
      args.push_back("/IMPLIB:" + TargetToolPathArgString(tool, *import_lib));
    }
  } else {
    args.push_back("/ENTRY:main");
    args.push_back("/SUBSYSTEM:CONSOLE");
    args.push_back("/MANIFEST:EMBED");
    args.push_back("/MANIFESTUAC:level='asInvoker' uiAccess='false'");
    args.push_back("/STACK:" +
                   std::to_string(kWindowsExeStackReserveBytes) +
                   "," +
                   std::to_string(kWindowsExeStackCommitBytes));
  }
  args.push_back("/MAP:" + TargetToolPathArgString(tool, map_output));
  args.push_back("/NODEFAULTLIB");
  if (plan.target_profile == TargetProfile::X86_64Win64) {
    for (const auto& lib_dir : WindowsImportLibSearchDirs()) {
      args.push_back("/LIBPATH:" + TargetToolPathArgString(tool, lib_dir));
    }

    const auto extern_dir = tool.parent_path().parent_path().parent_path();
    const auto bundled_icu_lib_dir = extern_dir / "icu" / "win64" / "lib64";
    std::error_code icu_ec;
    if (std::filesystem::is_directory(bundled_icu_lib_dir, icu_ec) && !icu_ec) {
      args.push_back("/LIBPATH:" +
                     TargetToolPathArgString(tool, bundled_icu_lib_dir));
    }
  }
  for (const auto& input : inputs) {
    args.push_back(TargetToolPathArgString(tool, input));
  }
  if (plan.target_profile == TargetProfile::X86_64Win64) {
    args.push_back("kernel32.lib");
    args.push_back("msvcrt.lib");
    args.push_back("ucrt.lib");
    args.push_back("icuuc.lib");
    args.push_back("icuin.lib");
    args.push_back("icudt.lib");
  }

  std::vector<std::string> export_symbols = plan.export_symbols;
  std::vector<std::string> data_export_symbols = plan.data_export_symbols;
  if (shared_library) {
    export_symbols.erase(
        std::remove_if(export_symbols.begin(),
                       export_symbols.end(),
                       [](const std::string& symbol) {
                         return TargetIsHiddenSharedLibraryExportSymbol(symbol);
                       }),
        export_symbols.end());
    data_export_symbols.erase(
        std::remove_if(data_export_symbols.begin(),
                       data_export_symbols.end(),
                       [](const std::string& symbol) {
                         return TargetIsHiddenSharedLibraryExportSymbol(symbol);
                       }),
        data_export_symbols.end());
  }
  std::sort(export_symbols.begin(), export_symbols.end());
  export_symbols.erase(
      std::unique(export_symbols.begin(), export_symbols.end()),
      export_symbols.end());
  std::sort(data_export_symbols.begin(), data_export_symbols.end());
  data_export_symbols.erase(
      std::unique(data_export_symbols.begin(), data_export_symbols.end()),
      data_export_symbols.end());
  for (const auto& symbol : data_export_symbols) {
    export_symbols.erase(
        std::remove(export_symbols.begin(), export_symbols.end(), symbol),
        export_symbols.end());
  }
  for (const auto& symbol : export_symbols) {
    args.push_back("/EXPORT:" + symbol);
  }
  for (const auto& symbol : data_export_symbols) {
    args.push_back("/EXPORT:" + symbol + ",DATA");
  }
  return args;
}

std::vector<std::string> BuildElfLinkArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan,
    const TargetLinkArgOptions& options) {
  (void)import_lib;
  const bool sysv_executable =
      plan.output_kind != LinkOutputKind::SharedLibrary &&
      plan.target_profile == TargetProfile::X86_64SysV;
  const auto search_dirs = PosixLibrarySearchDirs(tool, plan.target_profile);
  std::vector<std::string> args;
  args.reserve(inputs.size() + search_dirs.size() + 7);
  args.push_back(PathArgString(tool));
  args.push_back("-o");
  args.push_back(PathArgString(output));
  if (plan.output_kind == LinkOutputKind::SharedLibrary) {
    args.push_back("--shared");
  } else if (sysv_executable) {
    args.push_back("--entry=_start");
  } else {
    args.push_back("--entry=main");
  }
  if (sysv_executable) {
    args.push_back("--undefined=_start");
  }
  args.push_back("--nostdlib");
  args.push_back("-rpath=$ORIGIN");
  if (plan.output_kind == LinkOutputKind::SharedLibrary) {
    if (const auto version_script =
            WritePosixVersionScript(output, CuratedSharedLibraryExportSymbols(plan));
        version_script.has_value()) {
      args.push_back("--version-script=" + PathArgString(*version_script));
    }
  }
  if (plan.output_kind != LinkOutputKind::SharedLibrary) {
    if (const char* interpreter = ElfInterpreterPath(plan.target_profile);
        interpreter != nullptr) {
      args.push_back(std::string("--dynamic-linker=") + interpreter);
    }
  }
  for (const auto& dir : search_dirs) {
    args.push_back("-L" + dir.string());
  }
  for (const auto& input : inputs) {
    args.push_back(PathArgString(input));
  }
  if (ObjectFormatOf(plan.target_profile) == ObjectFormat::Elf) {
    if (options.inputs_reference_gxx_personality) {
      args.push_back("-lstdc++");
    }
    if (options.inputs_reference_gcc_personality) {
      args.push_back("-lgcc_s");
    }
    if (options.inputs_reference_gxx_personality) {
      args.push_back("-lstdc++");
    }
    if (options.inputs_reference_gcc_personality) {
      args.push_back("-lgcc_s");
    }
    args.push_back("-lm");
    args.push_back("-lc");
  }
  return args;
}

std::vector<std::string> BuildDarwinLinkArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan) {
  (void)import_lib;
  const bool shared_library = plan.output_kind == LinkOutputKind::SharedLibrary;

  std::vector<std::string> args;
  args.reserve(inputs.size() + plan.export_symbols.size() +
               plan.data_export_symbols.size() + 14);
  args.push_back(PathArgString(tool));
  if (shared_library) {
    args.push_back("-dynamiclib");
  }
  args.push_back("-target");
  args.push_back("arm64-apple-macosx14.0.0");
  args.push_back("-mmacosx-version-min=14.0");
  if (const auto sdk_root = MacOSSDKRoot(); sdk_root.has_value()) {
    args.push_back("-isysroot");
    args.push_back(PathArgString(*sdk_root));
  }
  if (shared_library) {
    args.push_back("-install_name");
    args.push_back("@rpath/" + output.filename().generic_string());
  }
  args.push_back("-o");
  args.push_back(PathArgString(output));
  if (shared_library) {
    args.push_back("-Wl,-rpath,@loader_path");
  } else {
    args.push_back("-Wl,-rpath,@executable_path");
    args.push_back("-Wl,-rpath,@loader_path");
  }
  for (const auto& input : inputs) {
    args.push_back(DarwinLinkInputArg(input));
  }
  if (shared_library) {
    for (const auto& symbol : CuratedSharedLibraryExportSymbols(plan)) {
      args.push_back("-Wl,-exported_symbol,_" + symbol);
    }
  }
  return args;
}

}  // namespace

std::string_view TargetRuntimeLibName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin:
      return ActiveLanguageProfile().runtime_static_lib_elf;
    case TargetProfile::X86_64Win64:
      return ActiveLanguageProfile().runtime_static_lib_coff;
  }
  UnreachableTargetPlatform();
}

std::string_view TargetLinkerToolName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return "ld.lld";
    case TargetProfile::X86_64Win64:
      return "lld-link";
    case TargetProfile::AArch64Darwin:
      return "clang++";
  }
  UnreachableTargetPlatform();
}

std::string_view TargetArchiverToolName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin:
      return "llvm-ar";
    case TargetProfile::X86_64Win64:
      return "llvm-lib";
  }
  UnreachableTargetPlatform();
}

std::string_view TargetRepoLLVMSubdir(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
      return "llvm/llvm-21.1.8-x86_64-sysv/bin";
    case TargetProfile::X86_64Win64:
      return "llvm/llvm-21.1.8-x86_64-win64/bin";
    case TargetProfile::AArch64AAPCS64:
      return "llvm/llvm-21.1.8-aarch64-aapcs64/bin";
    case TargetProfile::AArch64Darwin:
      return "llvm/llvm-21.1.8-aarch64-darwin/bin";
  }
  UnreachableTargetPlatform();
}

std::string_view TargetPackagedSupportPlatformDir(TargetProfile profile) {
  switch (ObjectFormatOf(profile)) {
    case ObjectFormat::Coff:
      return "windows";
    case ObjectFormat::Elf:
      return "linux";
    case ObjectFormat::MachO:
      return "macos";
  }
  UnreachableTargetPlatform();
}

bool TargetCAggregateReturnUsesIndirect(TargetProfile profile,
                                        std::uint64_t size) {
  if (size == 0) {
    return false;
  }

  switch (profile) {
    case TargetProfile::X86_64Win64:
      return !IsWin64DirectAggregateSize(size);
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin:
      return !IsRegisterPairDirectAggregateSize(size);
  }

  UnreachableTargetPlatform();
}

TargetAggregateCarrier TargetCAggregateDirectReturnCarrier(
    TargetProfile profile,
    std::uint64_t size,
    std::uint64_t align,
    bool contains_floating) {
  switch (profile) {
    case TargetProfile::X86_64Win64:
      return Win64DirectAggregateCarrier(size);
    case TargetProfile::X86_64SysV:
      return RegisterPairAggregateCarrier(size, contains_floating);
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin:
      return AArch64AggregateCarrier(size, align);
  }

  UnreachableTargetPlatform();
}

TargetAggregateCarrier TargetCAggregateByValueParamCarrier(
    TargetProfile profile,
    std::uint64_t size,
    std::uint64_t align,
    bool contains_floating) {
  if (size == 0) {
    return {};
  }

  switch (profile) {
    case TargetProfile::X86_64Win64: {
      const auto carrier = Win64DirectAggregateCarrier(size);
      if (carrier.kind != TargetAggregateCarrierKind::None) {
        return carrier;
      }
      return IndirectCarrier();
    }
    case TargetProfile::X86_64SysV:
      return RegisterPairAggregateCarrier(size, contains_floating);
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin: {
      const auto carrier = AArch64AggregateCarrier(size, align);
      if (carrier.kind != TargetAggregateCarrierKind::None) {
        return carrier;
      }
      return IndirectCarrier();
    }
  }

  UnreachableTargetPlatform();
}

bool TargetForeignByValueAggregateIndirectParamUsesByVal(
    TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64Win64:
      return false;
    case TargetProfile::X86_64SysV:
      return true;
    case TargetProfile::AArch64AAPCS64:
    case TargetProfile::AArch64Darwin:
      return false;
  }

  UnreachableTargetPlatform();
}

std::filesystem::path TargetHostFilesystemPath(const std::filesystem::path& path) {
  return ultraviolet::core::HostFilesystemPath(path);
}

std::string TargetToolPathArgString(const std::filesystem::path& tool,
                                    const std::filesystem::path& path) {
  if (ToolPathNamesWindowsExecutable(tool)) {
    if (auto windows_path = WslDrivePathArgString(path);
        windows_path.has_value()) {
      return *windows_path;
    }
  }
  return PathArgString(path);
}

bool TargetIsHiddenSharedLibraryExportSymbol(std::string_view symbol) {
  const auto& language = ActiveLanguageProfile();
  return symbol == language.library_entry_symbol ||
         symbol.rfind("__cx_", 0) == 0 ||
         symbol.rfind(language.runtime_init_mangle_prefix, 0) == 0 ||
         symbol.rfind(language.runtime_deinit_mangle_prefix, 0) == 0 ||
         symbol.ends_with("$resume");
}

bool TargetIsCompilerSupportSidecarFile(TargetProfile profile,
                                        const std::filesystem::path& path) {
  const std::string name = path.filename().generic_string();
  if (ObjectFormatOf(profile) == ObjectFormat::Coff) {
    return path.extension() == ".dll";
  }
  if (ObjectFormatOf(profile) == ObjectFormat::Elf) {
    return name.find(".so") != std::string::npos ||
           name == kLinuxIcuDataBlobSidecar;
  }
  if (ObjectFormatOf(profile) == ObjectFormat::MachO) {
    return path.extension() == ".dylib";
  }
  UnreachableTargetPlatform();
}

std::optional<std::filesystem::path> TargetLibraryLinkInput(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile) {
  if (kind == "raw-dylib") {
    return std::nullopt;
  }

  const auto resolved = ResolveLibraryName(kind, name, profile);
  if (!resolved.has_value()) {
    return std::nullopt;
  }

  std::filesystem::path candidate(*resolved);
  const std::filesystem::path original_name{std::string(name)};
  if (profile == TargetProfile::X86_64Win64 &&
      !original_name.has_extension() &&
      kind == "dylib" &&
      candidate.extension() == SharedLibSuffix(profile)) {
    candidate.replace_extension(ImportLibSuffix(profile));
  }
  return candidate;
}

std::vector<std::filesystem::path> MaterializeTargetLinkInputsForTool(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<std::filesystem::path>& inputs) {
  std::vector<std::filesystem::path> materialized;
  materialized.reserve(inputs.size());
  for (const auto& input : inputs) {
    materialized.push_back(
        MaterializeLinkInputForTool(project, target_profile, input));
  }
  return materialized;
}

std::vector<std::filesystem::path> TargetRuntimeSidecars(
    TargetProfile target_profile,
    const std::filesystem::path& runtime_lib) {
  const ObjectFormat object_format = ObjectFormatOf(target_profile);
  if (object_format != ObjectFormat::Elf &&
      object_format != ObjectFormat::MachO) {
    return {};
  }

  std::vector<std::filesystem::path> roots;
  if (const auto support_lib_dir = CompilerSupportLibDir(target_profile);
      support_lib_dir.has_value()) {
    AppendExistingUniqueDir(roots, *support_lib_dir);
  }

  if (!runtime_lib.empty()) {
    const auto runtime_dir = runtime_lib.parent_path();
    if (object_format == ObjectFormat::MachO) {
      AppendExistingUniqueDir(roots, runtime_dir / "macos" / "lib");
      AppendExistingUniqueDir(roots, runtime_dir / "lib");
    } else {
      AppendExistingUniqueDir(roots, runtime_dir / "linux" / "lib");
      AppendExistingUniqueDir(roots, runtime_dir / "lib");
      if (!runtime_dir.empty()) {
        AppendExistingUniqueDir(roots, runtime_dir.parent_path() / "lib");
      }
    }
  }

  std::vector<std::filesystem::path> out;
  const auto append_required_sidecar = [&](std::string_view name) -> bool {
    for (const auto& root : roots) {
      const auto candidate = root / std::string(name);
      if (CanReadFile(candidate)) {
        out.push_back(candidate);
        return true;
      }
    }
    return false;
  };

  if (object_format == ObjectFormat::MachO) {
    for (const auto name : {kMacosRuntimeSupportSidecar,
                            kMacosIcuI18nSidecar,
                            kMacosIcuUcSidecar,
                            kMacosIcuDataSidecar}) {
      if (!append_required_sidecar(name)) {
        return {};
      }
    }
  } else {
    const std::string runtime_support_sidecar(
        ActiveLanguageProfile().linux_runtime_support_sidecar);
    for (const std::string_view name : {
             std::string_view(runtime_support_sidecar),
             kLinuxIcuI18nSidecar,
             kLinuxIcuUcSidecar,
             kLinuxIcuDataSidecar,
             kLinuxIcuDataBlobSidecar,
         }) {
      if (!append_required_sidecar(name)) {
        return {};
      }
    }
  }

  return out;
}

std::size_t TargetRequiredRuntimeSidecarCount(TargetProfile target_profile) {
  switch (ObjectFormatOf(target_profile)) {
    case ObjectFormat::Elf:
      return 5u;
    case ObjectFormat::MachO:
      return 4u;
    case ObjectFormat::Coff:
      return 0u;
  }
  UnreachableTargetPlatform();
}

std::string TargetRuntimeSidecarMissingMessage(TargetProfile target_profile) {
  if (ObjectFormatOf(target_profile) == ObjectFormat::Elf) {
    return "missing Linux runtime sidecar assets under the compiler support "
           "lib directory";
  }
  if (ObjectFormatOf(target_profile) == ObjectFormat::MachO) {
    return "missing macOS runtime sidecar assets under the compiler support "
           "lib directory";
  }
  return "missing runtime sidecar assets under the compiler support directory";
}

bool TargetRuntimeSidecarIsLinkable(const std::filesystem::path& sidecar) {
  const std::string name = sidecar.filename().generic_string();
  return name.find(".so") != std::string::npos ||
         name.find(".dylib") != std::string::npos;
}

bool TargetExecutableRequiresStartupObject(const LinkPlan& plan) {
  return plan.output_kind == LinkOutputKind::Executable &&
         plan.target_profile == TargetProfile::X86_64SysV;
}

std::optional<std::filesystem::path> TargetRuntimeStartupObjectPath(
    const Project& project,
    TargetProfile target_profile,
    const std::filesystem::path& runtime_lib) {
  if (target_profile != TargetProfile::X86_64SysV) {
    return std::nullopt;
  }

  const std::filesystem::path startup_name(TargetRuntimeStartupObjectName(target_profile));
  std::vector<std::filesystem::path> candidates;
  candidates.reserve(6);

  if (!runtime_lib.empty()) {
    const auto runtime_dir = runtime_lib.parent_path();
    if (!runtime_dir.empty()) {
      candidates.push_back(runtime_dir / startup_name);
      candidates.push_back(runtime_dir / "runtime" / startup_name);
      candidates.push_back(runtime_dir / "linux" / "runtime" / startup_name);
    }
  }

  if (const auto support_startup = core::CompilerSupportAssetPath(
          std::filesystem::path("linux") / "runtime" / startup_name,
          std::filesystem::path("runtime") / startup_name);
      support_startup.has_value()) {
    candidates.push_back(*support_startup);
  }

  std::filesystem::path build_root = project.outputs.root;
  if (build_root.empty()) {
    build_root = project.root / "Build";
  }
  candidates.push_back(build_root / "runtime" / startup_name);

  for (const auto& candidate : candidates) {
    if (CanReadFile(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::string TargetRuntimeStartupObjectName(TargetProfile target_profile) {
  if (target_profile == TargetProfile::X86_64SysV) {
    return std::string(ActiveLanguageProfile().linux_startup_object_x86_64_sysv);
  }
  return {};
}

std::string TargetRuntimeStartupObjectMissingMessage(TargetProfile target_profile) {
  const std::string startup_object_name =
      TargetRuntimeStartupObjectName(target_profile);
  if (target_profile == TargetProfile::X86_64SysV) {
    return "missing Linux runtime startup object `" + startup_object_name + "`";
  }
  return "missing runtime startup object `" + startup_object_name + "`";
}

std::vector<std::string> BuildTargetLinkArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan,
    const TargetLinkArgOptions& options) {
  switch (ObjectFormatOf(plan.target_profile)) {
    case ObjectFormat::Coff:
      return BuildWindowsLinkArgs(tool, inputs, output, import_lib, plan);
    case ObjectFormat::Elf:
      return BuildElfLinkArgs(tool, inputs, output, import_lib, plan, options);
    case ObjectFormat::MachO:
      return BuildDarwinLinkArgs(tool, inputs, output, import_lib, plan);
  }
  UnreachableTargetPlatform();
}

std::vector<std::string> BuildTargetArchiverArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output) {
  std::vector<std::string> args;
  if (ArchiverUsesWindowsFlags(tool)) {
    args.reserve(inputs.size() + 3);
    args.push_back(PathArgString(tool));
    args.push_back("/NOLOGO");
    args.push_back("/OUT:" + TargetToolPathArgString(tool, output));
    for (const auto& input : inputs) {
      args.push_back(TargetToolPathArgString(tool, input));
    }
  } else {
    args.reserve(inputs.size() + 3);
    args.push_back(PathArgString(tool));
    args.push_back("rcs");
    args.push_back(PathArgString(output));
    for (const auto& input : inputs) {
      args.push_back(PathArgString(input));
    }
  }
  return args;
}

}  // namespace ultraviolet::project
