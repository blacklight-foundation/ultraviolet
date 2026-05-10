#include "01_project/target_profile.h"

#include <cstdlib>

#include "01_project/language_profile.h"

namespace cursive::project {

namespace {

[[noreturn]] void UnreachableTargetProfile() {
  std::abort();
}

}  // namespace

std::string_view TargetProfileName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
      return "x86_64-sysv";
    case TargetProfile::X86_64Win64:
      return "x86_64-win64";
    case TargetProfile::AArch64AAPCS64:
      return "aarch64-aapcs64";
  }
  UnreachableTargetProfile();
}

std::optional<TargetProfile> ParseTargetProfile(std::string_view value) {
  if (value == "x86_64-sysv") {
    return TargetProfile::X86_64SysV;
  }
  if (value == "x86_64-win64") {
    return TargetProfile::X86_64Win64;
  }
  if (value == "aarch64-aapcs64") {
    return TargetProfile::AArch64AAPCS64;
  }
  return std::nullopt;
}

TargetArch TargetArchOf(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::X86_64Win64:
      return TargetArch::X86_64;
    case TargetProfile::AArch64AAPCS64:
      return TargetArch::AArch64;
  }
  UnreachableTargetProfile();
}

Endianness EndiannessOf(TargetProfile profile) {
  (void)profile;
  return Endianness::Little;
}

std::size_t PtrSizeBytes(TargetProfile profile) {
  (void)profile;
  return 8;
}

std::string_view ObjExt(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ".o";
    case TargetProfile::X86_64Win64:
      return ".obj";
  }
  UnreachableTargetProfile();
}

std::string_view ExeSuffix(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return "";
    case TargetProfile::X86_64Win64:
      return ".exe";
  }
  UnreachableTargetProfile();
}

std::string_view LibraryPrefix(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return "lib";
    case TargetProfile::X86_64Win64:
      return "";
  }
  UnreachableTargetProfile();
}

std::string_view SharedLibSuffix(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ".so";
    case TargetProfile::X86_64Win64:
      return ".dll";
  }
  UnreachableTargetProfile();
}

std::string_view StaticLibSuffix(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ".a";
    case TargetProfile::X86_64Win64:
      return ".lib";
  }
  UnreachableTargetProfile();
}

std::string_view ImportLibSuffix(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ".so.import";
    case TargetProfile::X86_64Win64:
      return ".lib";
  }
  UnreachableTargetProfile();
}

bool EmitsImportLib(TargetProfile profile) {
  return profile == TargetProfile::X86_64Win64;
}

std::string_view RuntimeLibNameFor(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ActiveLanguageProfile().runtime_static_lib_elf;
    case TargetProfile::X86_64Win64:
      return ActiveLanguageProfile().runtime_static_lib_coff;
  }
  UnreachableTargetProfile();
}

std::string_view LinkerToolName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return "ld.lld";
    case TargetProfile::X86_64Win64:
      return "lld-link";
  }
  UnreachableTargetProfile();
}

std::string_view ArchiverToolName(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return "llvm-ar";
    case TargetProfile::X86_64Win64:
      return "llvm-lib";
  }
  UnreachableTargetProfile();
}

std::string_view LLVMTripleOf(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
      return "x86_64-unknown-linux-gnu";
    case TargetProfile::X86_64Win64:
      return "x86_64-pc-windows-msvc";
    case TargetProfile::AArch64AAPCS64:
      return "aarch64-unknown-linux-gnu";
  }
  UnreachableTargetProfile();
}

std::string_view LLVMDataLayoutOf(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
      return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i128:128-n8:16:32:64-S128";
    case TargetProfile::X86_64Win64:
      return "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
    case TargetProfile::AArch64AAPCS64:
      return "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128";
  }
  UnreachableTargetProfile();
}

std::string_view RepoLLVMSubdir(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
      return "llvm/llvm-21.1.8-x86_64-sysv/bin";
    case TargetProfile::X86_64Win64:
      return "llvm/llvm-21.1.8-x86_64-win64/bin";
    case TargetProfile::AArch64AAPCS64:
      return "llvm/llvm-21.1.8-aarch64-aapcs64/bin";
  }
  UnreachableTargetProfile();
}

ObjectFormat ObjectFormatOf(TargetProfile profile) {
  switch (profile) {
    case TargetProfile::X86_64SysV:
    case TargetProfile::AArch64AAPCS64:
      return ObjectFormat::Elf;
    case TargetProfile::X86_64Win64:
      return ObjectFormat::Coff;
  }
  UnreachableTargetProfile();
}

bool SupportsSharedLibraries(TargetProfile profile) {
  return profile == TargetProfile::X86_64Win64 ||
         profile == TargetProfile::X86_64SysV ||
         profile == TargetProfile::AArch64AAPCS64;
}

bool SupportsHostedLibraries(TargetProfile profile) {
  return profile == TargetProfile::X86_64Win64 ||
         profile == TargetProfile::X86_64SysV ||
         profile == TargetProfile::AArch64AAPCS64;
}

bool LibraryKindSupported(std::string_view kind, TargetProfile profile) {
  if (kind == "raw-dylib") {
    return profile == TargetProfile::X86_64Win64;
  }
  if (kind == "framework") {
    return false;
  }
  return kind == "dylib" || kind == "static";
}

std::optional<std::string> ResolveLibraryName(std::string_view kind,
                                              std::string_view name,
                                              TargetProfile profile) {
  if (!LibraryKindSupported(kind, profile)) {
    return std::nullopt;
  }
  if (kind == "dylib") {
    return std::string(LibraryPrefix(profile)) + std::string(name) +
           std::string(SharedLibSuffix(profile));
  }
  if (kind == "static") {
    return std::string(LibraryPrefix(profile)) + std::string(name) +
           std::string(StaticLibSuffix(profile));
  }
  if (kind == "raw-dylib" && profile == TargetProfile::X86_64Win64) {
    return std::string(name) + ".dll";
  }
  return std::nullopt;
}

}  // namespace cursive::project
