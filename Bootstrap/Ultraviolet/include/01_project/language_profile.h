#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace ultraviolet::project {

enum class SourceLanguage {
  Ultraviolet,
};

struct LanguageProfile {
  SourceLanguage language;
  std::string_view display_name;
  std::string_view manifest_name;
  std::string_view source_extension;
  std::string_view runtime_root;
  std::string_view lower_name;

  std::string_view runtime_static_lib_elf;
  std::string_view runtime_static_lib_coff;
  std::string_view linux_startup_object_x86_64_sysv;
  std::string_view linux_runtime_support_sidecar;

  std::string_view library_entry_symbol;
  std::string_view library_ctor_symbol;
  std::string_view library_dtor_symbol;
  std::string_view library_attached_symbol;
  std::string_view image_panic_record_symbol;

  std::string_view host_abi_version_symbol;
  std::string_view host_session_create_symbol;
  std::string_view host_session_destroy_symbol;
  std::string_view host_session_owner_token_symbol;
  std::string_view host_runtime_alloc_symbol;
  std::string_view host_runtime_free_symbol;
  std::string_view host_runtime_register_symbol;
  std::string_view host_runtime_try_enter_symbol;
  std::string_view host_runtime_leave_symbol;
  std::string_view host_runtime_try_retire_symbol;
  std::string_view host_runtime_abort_live_symbol;
  std::string_view host_runtime_current_env_symbol;
  std::string_view host_runtime_enter_retired_symbol;
  std::string_view host_runtime_leave_retired_symbol;
  std::string_view host_runtime_abort_retired_symbol;
  std::string_view raw_dylib_resolve_symbol;

  std::string_view runtime_init_mangle_prefix;
  std::string_view runtime_deinit_mangle_prefix;
  std::string_view runtime_symbol_prefix;
  std::string_view concurrency_symbol_prefix;

  std::string_view region_active_alias;
  std::string_view hosted_session_param_name;
};

const LanguageProfile& UltravioletLanguageProfile();
const LanguageProfile& LanguageProfileFor(SourceLanguage language);

const LanguageProfile& ActiveLanguageProfile();
void SetActiveLanguageProfile(SourceLanguage language);
std::optional<SourceLanguage> DetectSourceLanguageForInput(
    const std::filesystem::path& input_path);

std::string LanguagePathSig(std::initializer_list<std::string_view> tail);
std::string RuntimePathSig(std::initializer_list<std::string_view> tail);

}  // namespace ultraviolet::project
