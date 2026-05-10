#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cursive::project {

enum class TargetProfile {
  X86_64SysV,
  X86_64Win64,
  AArch64AAPCS64,
};

enum class ObjectFormat {
  Coff,
  Elf,
};

enum class TargetArch {
  X86_64,
  AArch64,
};

enum class Endianness {
  Little,
};

std::string_view TargetProfileName(TargetProfile profile);
std::optional<TargetProfile> ParseTargetProfile(std::string_view value);
TargetArch TargetArchOf(TargetProfile profile);
Endianness EndiannessOf(TargetProfile profile);
std::size_t PtrSizeBytes(TargetProfile profile);

std::string_view ObjExt(TargetProfile profile);
std::string_view ExeSuffix(TargetProfile profile);
std::string_view LibraryPrefix(TargetProfile profile);
std::string_view SharedLibSuffix(TargetProfile profile);
std::string_view StaticLibSuffix(TargetProfile profile);
std::string_view ImportLibSuffix(TargetProfile profile);
bool EmitsImportLib(TargetProfile profile);

std::string_view RuntimeLibNameFor(TargetProfile profile);
std::string_view LinkerToolName(TargetProfile profile);
std::string_view ArchiverToolName(TargetProfile profile);

std::string_view LLVMTripleOf(TargetProfile profile);
std::string_view LLVMDataLayoutOf(TargetProfile profile);
std::string_view RepoLLVMSubdir(TargetProfile profile);
ObjectFormat ObjectFormatOf(TargetProfile profile);
bool SupportsSharedLibraries(TargetProfile profile);
bool SupportsHostedLibraries(TargetProfile profile);

bool LibraryKindSupported(std::string_view kind, TargetProfile profile);
std::optional<std::string> ResolveLibraryName(std::string_view kind,
                                              std::string_view name,
                                              TargetProfile profile);

}  // namespace cursive::project
