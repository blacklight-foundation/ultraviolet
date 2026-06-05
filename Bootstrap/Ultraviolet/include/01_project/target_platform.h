#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"

namespace ultraviolet::project {

struct LinkPlan;
struct Project;

struct TargetLinkArgOptions {
  bool inputs_reference_gxx_personality = false;
  bool inputs_reference_gcc_personality = false;
};

enum class TargetAggregateCarrierKind {
  None,
  Integer,
  IntegerPair,
  IntegerArray,
  Indirect,
};

struct TargetAggregateCarrier {
  TargetAggregateCarrierKind kind = TargetAggregateCarrierKind::None;
  std::uint64_t primary_bits = 0;
  std::uint64_t secondary_bits = 0;
  std::uint64_t element_bits = 0;
  std::uint64_t element_count = 0;
  bool packed = false;
};

std::string_view TargetRuntimeLibName(TargetProfile profile);
std::string_view TargetLinkerToolName(TargetProfile profile);
std::string_view TargetArchiverToolName(TargetProfile profile);
std::string_view TargetRepoLLVMSubdir(TargetProfile profile);
std::string_view TargetPackagedSupportPlatformDir(TargetProfile profile);

bool TargetCAggregateReturnUsesIndirect(TargetProfile profile,
                                        std::uint64_t size);
TargetAggregateCarrier TargetCAggregateDirectReturnCarrier(
    TargetProfile profile,
    std::uint64_t size,
    std::uint64_t align,
    bool contains_floating);
TargetAggregateCarrier TargetCAggregateByValueParamCarrier(
    TargetProfile profile,
    std::uint64_t size,
    std::uint64_t align,
    bool contains_floating);
bool TargetForeignByValueAggregateIndirectParamUsesByVal(TargetProfile profile);

std::filesystem::path TargetHostFilesystemPath(const std::filesystem::path& path);
std::string TargetToolPathArgString(const std::filesystem::path& tool,
                                    const std::filesystem::path& path);

bool TargetIsHiddenSharedLibraryExportSymbol(std::string_view symbol);
bool TargetIsCompilerSupportSidecarFile(TargetProfile profile,
                                        const std::filesystem::path& path);
std::optional<std::filesystem::path> TargetLibraryLinkInput(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile);

std::vector<std::filesystem::path> MaterializeTargetLinkInputsForTool(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<std::filesystem::path>& inputs);

std::vector<std::filesystem::path> TargetRuntimeSidecars(
    TargetProfile target_profile,
    const std::filesystem::path& runtime_lib);
std::size_t TargetRequiredRuntimeSidecarCount(TargetProfile target_profile);
std::string TargetRuntimeSidecarMissingMessage(TargetProfile target_profile);
bool TargetRuntimeSidecarIsLinkable(const std::filesystem::path& sidecar);

bool TargetExecutableRequiresStartupObject(const LinkPlan& plan);
std::optional<std::filesystem::path> TargetRuntimeStartupObjectPath(
    const Project& project,
    TargetProfile target_profile,
    const std::filesystem::path& runtime_lib);
std::string TargetRuntimeStartupObjectName(TargetProfile target_profile);
std::string TargetRuntimeStartupObjectMissingMessage(TargetProfile target_profile);

std::vector<std::string> BuildTargetLinkArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan,
    const TargetLinkArgOptions& options);

std::vector<std::string> BuildTargetArchiverArgs(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output);

}  // namespace ultraviolet::project
