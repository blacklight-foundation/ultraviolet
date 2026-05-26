#include "01_project/link.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "00_core/host/services.h"
#include "00_core/path.h"
#include "00_core/crash_debug.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/host_primitives.h"
#include "00_core/process_config.h"
#include "00_core/runtime_abi.h"
#include "01_project/assemblies.h"
#include "01_project/compiler_support_paths.h"
#include "01_project/language_profile.h"
#include "01_project/outputs.h"
#include "01_project/project.h"
#include "01_project/target_platform.h"
#include "01_project/target_profile.h"
#include "01_project/tool_resolution.h"

namespace ultraviolet::project
{

  namespace
  {

    std::string PathArgString(const std::filesystem::path &path)
    {
      const auto utf8 = path.generic_u8string();
      std::string out;
      out.reserve(utf8.size());
      for (const auto ch : utf8)
      {
        out.push_back(static_cast<char>(ch));
      }
      return out;
    }

    std::string LowerAsciiForDiagnosticMatch(std::string_view text)
    {
      std::string out;
      out.reserve(text.size());
      for (unsigned char ch : text)
      {
        out.push_back(static_cast<char>(std::tolower(ch)));
      }
      return out;
    }

    std::string QuoteCommandArg(std::string_view arg)
    {
      if (arg.empty())
      {
        return "\"\"";
      }

      bool needs_quotes = false;
      for (const char ch : arg)
      {
        if (ch == ' ' || ch == '\t' || ch == '"')
        {
          needs_quotes = true;
          break;
        }
      }
      if (!needs_quotes)
      {
        return std::string(arg);
      }

      std::string out;
      out.push_back('"');
      std::size_t backslashes = 0;
      for (const char ch : arg)
      {
        if (ch == '\\')
        {
          ++backslashes;
          continue;
        }
        if (ch == '"')
        {
          out.append(backslashes * 2 + 1, '\\');
          out.push_back('"');
          backslashes = 0;
          continue;
        }
        if (backslashes > 0)
        {
          out.append(backslashes, '\\');
          backslashes = 0;
        }
        out.push_back(ch);
      }
      if (backslashes > 0)
      {
        out.append(backslashes * 2, '\\');
      }
      out.push_back('"');
      return out;
    }

    std::string CommandLineForDebug(const std::vector<std::string> &argv)
    {
      std::ostringstream oss;
      for (std::size_t i = 0; i < argv.size(); ++i)
      {
        if (i != 0)
        {
          oss << ' ';
        }
        oss << QuoteCommandArg(argv[i]);
      }
      return oss.str();
    }

    bool LinkDebugEnabled()
    {
      return core::LinkDebugOverride().value_or(core::IsDebugEnabled("link"));
    }

    void EmitExternal(core::DiagnosticStream &diags, std::string_view code)
    {
      core::EmitExternalDiagnostic(diags, code);
    }

    bool IsSharedLibraryProject(const Project &project)
    {
      return IsSharedLibrary(project);
    }

    std::optional<std::string> ReadFileBytes(const std::filesystem::path &path)
    {
      std::ifstream in(TargetHostFilesystemPath(path), std::ios::binary);
      if (!in)
      {
        return std::nullopt;
      }
      std::ostringstream oss;
      oss << in.rdbuf();
      return oss.str();
    }

    bool CanReadFile(const std::filesystem::path &path)
    {
      std::ifstream in(TargetHostFilesystemPath(path), std::ios::binary);
      return static_cast<bool>(in);
    }

    bool IsLinkerResolvedLibraryName(const std::filesystem::path &path)
    {
      return !path.empty() && !path.has_parent_path() && !path.is_absolute();
    }

    uint16_t ReadU16(const unsigned char *data)
    {
      return static_cast<uint16_t>(data[0] | (data[1] << 8));
    }

    uint32_t ReadU32(const unsigned char *data)
    {
      return static_cast<uint32_t>(data[0] | (data[1] << 8) | (data[2] << 16) |
                                   (data[3] << 24));
    }

    uint64_t ReadU64(const unsigned char *data)
    {
      return static_cast<uint64_t>(data[0]) |
             (static_cast<uint64_t>(data[1]) << 8) |
             (static_cast<uint64_t>(data[2]) << 16) |
             (static_cast<uint64_t>(data[3]) << 24) |
             (static_cast<uint64_t>(data[4]) << 32) |
             (static_cast<uint64_t>(data[5]) << 40) |
             (static_cast<uint64_t>(data[6]) << 48) |
             (static_cast<uint64_t>(data[7]) << 56);
    }

    bool ParseDecimal(std::string_view field, std::size_t *value)
    {
      std::size_t acc = 0;
      bool any = false;
      for (char c : field)
      {
        if (c >= '0' && c <= '9')
        {
          any = true;
          acc = acc * 10 + static_cast<std::size_t>(c - '0');
        }
        else if (c == ' ' || c == '\0')
        {
          if (any)
          {
            break;
          }
        }
        else
        {
          if (any)
          {
            break;
          }
          return false;
        }
      }
      if (!any)
      {
        return false;
      }
      *value = acc;
      return true;
    }

    std::string TrimArchiveName(std::string_view name_field)
    {
      std::string name(name_field);
      while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
      {
        name.pop_back();
      }
      if (name.size() > 1 && name.back() == '/')
      {
        name.pop_back();
      }
      return name;
    }

    bool IsSpecialArchiveMember(std::string_view name)
    {
      return name == "/" || name == "//" || name == "__.SYMDEF" ||
             name == "__.SYMDEF SORTED" || name == "__.SYMDEF_64" ||
             name == "__.SYMDEF_64 SORTED";
    }

    struct ArchiveMemberView
    {
      std::string name;
      std::string_view payload;
    };

    std::optional<ArchiveMemberView> ArchiveMemberViewOf(
        std::string_view header_name,
        std::string_view member)
    {
      constexpr std::string_view extended_name_prefix = "#1/";
      if (header_name.rfind(extended_name_prefix, 0) != 0)
      {
        return ArchiveMemberView{std::string(header_name), member};
      }

      std::size_t name_size = 0;
      if (!ParseDecimal(header_name.substr(extended_name_prefix.size()),
                        &name_size) ||
          name_size > member.size())
      {
        return std::nullopt;
      }
      return ArchiveMemberView{TrimArchiveName(member.substr(0, name_size)),
                               member.substr(name_size)};
    }

    std::optional<std::string> CoffSymbolName(std::string_view bytes,
                                              std::size_t entry_offset,
                                              std::size_t string_table_offset,
                                              std::size_t string_table_size)
    {
      if (entry_offset + 8 > bytes.size())
      {
        return std::nullopt;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data() + entry_offset);
      bool long_name = data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0;
      if (!long_name)
      {
        std::string name;
        name.reserve(8);
        for (std::size_t i = 0; i < 8; ++i)
        {
          char c = static_cast<char>(data[i]);
          if (c == '\0')
          {
            break;
          }
          name.push_back(c);
        }
        if (name.empty())
        {
          return std::nullopt;
        }
        return name;
      }
      if (entry_offset + 8 > bytes.size())
      {
        return std::nullopt;
      }
      const uint32_t offset = ReadU32(data + 4);
      if (offset < 4)
      {
        return std::nullopt;
      }
      const std::size_t start = string_table_offset + offset;
      const std::size_t end = string_table_offset + string_table_size;
      if (start >= bytes.size())
      {
        return std::nullopt;
      }
      std::size_t limit = bytes.size();
      if (end > start && end <= bytes.size())
      {
        limit = end;
      }
      std::string name;
      for (std::size_t i = start; i < limit; ++i)
      {
        char c = bytes[i];
        if (c == '\0')
        {
          break;
        }
        name.push_back(c);
      }
      if (name.empty())
      {
        return std::nullopt;
      }
      return name;
    }

    constexpr uint8_t kCoffStorageClassExternal = 2;

    bool IsCoffBackendHelperSymbolForDuplicateScan(std::string_view sym)
    {
      return sym.rfind("__real@", 0) == 0 ||
             sym.rfind("__xmm@", 0) == 0 ||
             sym.rfind("__ymm@", 0) == 0 ||
             sym.rfind("__zmm@", 0) == 0;
    }

    bool IsArchiveBytes(std::string_view bytes)
    {
      return bytes.size() >= 8 && bytes.substr(0, 8) == "!<arch>\n";
    }

    bool IsElfBytes(std::string_view bytes)
    {
      return bytes.size() >= 4 &&
             static_cast<unsigned char>(bytes[0]) == 0x7F &&
             bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
    }

    bool IsMachO64LittleArm64Bytes(std::string_view bytes)
    {
      if (bytes.size() < 32)
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr uint32_t kMachO64Magic = 0xfeedfacfu;
      constexpr uint32_t kMachOArm64CpuType = 0x0100000cu;
      return ReadU32(data) == kMachO64Magic &&
             ReadU32(data + 4) == kMachOArm64CpuType;
    }

    bool IsMachO64LittleArm64ObjectBytes(std::string_view bytes)
    {
      if (!IsMachO64LittleArm64Bytes(bytes))
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr uint32_t kMachOObjectFileType = 1u;
      return ReadU32(data + 12) == kMachOObjectFileType;
    }

    bool IsMachO64LittleArm64DylibBytes(std::string_view bytes)
    {
      if (!IsMachO64LittleArm64Bytes(bytes))
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr uint32_t kMachODylibFileType = 6u;
      return ReadU32(data + 12) == kMachODylibFileType;
    }

    bool IsMachO64LittleArm64SymbolTableBytes(std::string_view bytes)
    {
      return IsMachO64LittleArm64ObjectBytes(bytes) ||
             IsMachO64LittleArm64DylibBytes(bytes);
    }

    std::string NormalizeMachOSymbolName(std::string name)
    {
      if (!name.empty() && name.front() == '_')
      {
        name.erase(name.begin());
      }
      return name;
    }

    std::optional<std::string> MachOSymbolName(std::string_view bytes,
                                               std::size_t string_table_offset,
                                               std::size_t string_table_size,
                                               uint32_t name_offset)
    {
      if (name_offset == 0 || name_offset >= string_table_size ||
          string_table_offset > bytes.size() ||
          string_table_size > bytes.size() - string_table_offset)
      {
        return std::nullopt;
      }

      const char *string_table = bytes.data() + string_table_offset;
      std::string name;
      for (std::size_t pos = name_offset; pos < string_table_size; ++pos)
      {
        const char c = string_table[pos];
        if (c == '\0')
        {
          break;
        }
        name.push_back(c);
      }
      if (name.empty())
      {
        return std::nullopt;
      }
      return NormalizeMachOSymbolName(std::move(name));
    }

    bool ParseCoffSymbols(std::string_view bytes,
                          std::vector<std::string> &symbols,
                          bool defined_external_only,
                          bool ignore_comdat_externals = false)
    {
      if (bytes.size() < 20)
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      const uint16_t sig1 = ReadU16(data);
      const uint16_t sig2 = ReadU16(data + 2);
      if (sig1 == 0 && sig2 == 0xFFFFu)
      {
        return true;
      }
      const uint32_t sym_table = ReadU32(data + 8);
      const uint32_t sym_count = ReadU32(data + 12);
      const uint16_t section_count = ReadU16(data + 2);
      const uint16_t optional_header_size = ReadU16(data + 16);
      if (sym_table == 0 || sym_count == 0)
      {
        return true;
      }
      constexpr uint32_t kImageScnLnkComdat = 0x00001000u;
      std::vector<bool> comdat_sections(section_count + 1, false);
      const std::size_t section_table_offset =
          20 + static_cast<std::size_t>(optional_header_size);
      const std::size_t section_header_size = 40;
      if (section_table_offset +
              static_cast<std::size_t>(section_count) * section_header_size >
          bytes.size())
      {
        return false;
      }
      for (std::size_t i = 0; i < section_count; ++i)
      {
        const std::size_t header_offset =
            section_table_offset + i * section_header_size;
        const uint32_t characteristics = ReadU32(
            reinterpret_cast<const unsigned char *>(bytes.data() + header_offset +
                                                    36));
        comdat_sections[i + 1] = (characteristics & kImageScnLnkComdat) != 0;
      }
      const std::size_t sym_table_end =
          sym_table + static_cast<std::size_t>(sym_count) * 18;
      if (sym_table_end > bytes.size())
      {
        return false;
      }
      std::size_t string_table_offset = sym_table_end;
      std::size_t string_table_size = 0;
      if (string_table_offset + 4 <= bytes.size())
      {
        string_table_size = ReadU32(reinterpret_cast<const unsigned char *>(
            bytes.data() + string_table_offset));
      }

      uint32_t i = 0;
      while (i < sym_count)
      {
        const std::size_t entry_offset = sym_table + i * 18;
        if (entry_offset + 18 > bytes.size())
        {
          return false;
        }
        const unsigned char *entry =
            reinterpret_cast<const unsigned char *>(bytes.data() + entry_offset);
        const int16_t section = static_cast<int16_t>(ReadU16(entry + 12));
        const uint8_t storage_class = entry[16];
        const uint8_t aux = entry[17];
        const bool is_defined = section > 0 || section == static_cast<int16_t>(-1);
        const bool include_symbol =
            !defined_external_only || storage_class == kCoffStorageClassExternal;
        const bool is_comdat_external =
            ignore_comdat_externals &&
            storage_class == kCoffStorageClassExternal &&
            section > 0 &&
            static_cast<std::size_t>(section) < comdat_sections.size() &&
            comdat_sections[static_cast<std::size_t>(section)];
        if (is_defined && include_symbol && !is_comdat_external)
        {
          const auto name = CoffSymbolName(bytes, entry_offset, string_table_offset,
                                           string_table_size);
          if (name.has_value())
          {
            symbols.push_back(*name);
          }
        }
        i += 1 + static_cast<uint32_t>(aux);
      }
      return true;
    }

    bool ParseElfSymbols(std::string_view bytes,
                         std::vector<std::string> &symbols,
                         bool defined_external_only,
                         bool include_weak_externals = true)
    {
      if (!IsElfBytes(bytes) || bytes.size() < 64)
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr std::size_t kEIClass = 4;
      constexpr std::size_t kEIData = 5;
      constexpr unsigned char kElfClass64 = 2;
      constexpr unsigned char kElfDataLittle = 1;
      constexpr uint32_t kSHTSymTab = 2;
      constexpr uint32_t kSHTDynSym = 11;
      constexpr uint16_t kSHNUndef = 0;
      constexpr uint8_t kSTBLocal = 0;
      constexpr uint8_t kSTBGlobal = 1;
      constexpr uint8_t kSTBWeak = 2;

      if (data[kEIClass] != kElfClass64 || data[kEIData] != kElfDataLittle)
      {
        return false;
      }

      const std::size_t shoff = static_cast<std::size_t>(ReadU64(data + 40));
      const std::size_t shentsize = ReadU16(data + 58);
      const std::size_t shnum = ReadU16(data + 60);
      if (shoff > bytes.size() ||
          shentsize == 0 ||
          shoff + shentsize * shnum > bytes.size())
      {
        return false;
      }

      for (std::size_t i = 0; i < shnum; ++i)
      {
        const std::size_t sh_offset = shoff + i * shentsize;
        const unsigned char *sh = data + sh_offset;
        const uint32_t type = ReadU32(sh + 4);
        if (type != kSHTSymTab && type != kSHTDynSym)
        {
          continue;
        }

        const std::size_t section_offset = static_cast<std::size_t>(ReadU64(sh + 24));
        const std::size_t section_size = static_cast<std::size_t>(ReadU64(sh + 32));
        const uint32_t link = ReadU32(sh + 40);
        const std::size_t entsize = static_cast<std::size_t>(ReadU64(sh + 56));
        if (section_offset > bytes.size() ||
            section_size > bytes.size() - section_offset ||
            entsize == 0 ||
            section_size % entsize != 0 ||
            link >= shnum)
        {
          return false;
        }

        const unsigned char *linked_section =
            data + shoff + static_cast<std::size_t>(link) * shentsize;
        const std::size_t str_offset =
            static_cast<std::size_t>(ReadU64(linked_section + 24));
        const std::size_t str_size =
            static_cast<std::size_t>(ReadU64(linked_section + 32));
        if (str_offset > bytes.size() || str_size > bytes.size() - str_offset)
        {
          return false;
        }
        const char *strtab = bytes.data() + str_offset;

        const std::size_t count = section_size / entsize;
        for (std::size_t sym_index = 0; sym_index < count; ++sym_index)
        {
          const unsigned char *sym =
              data + section_offset + sym_index * entsize;
          const uint32_t name_offset = ReadU32(sym);
          const uint8_t info = sym[4];
          const uint16_t shndx = ReadU16(sym + 6);
          if (name_offset >= str_size || shndx == kSHNUndef)
          {
            continue;
          }
          const uint8_t binding = static_cast<uint8_t>(info >> 4);
          if (defined_external_only && binding == kSTBWeak &&
              !include_weak_externals)
          {
            continue;
          }
          if (defined_external_only && binding != kSTBGlobal &&
              binding != kSTBWeak)
          {
            continue;
          }

          std::string name;
          for (std::size_t pos = name_offset; pos < str_size; ++pos)
          {
            const char c = strtab[pos];
            if (c == '\0')
            {
              break;
            }
            name.push_back(c);
          }
          if (!name.empty())
          {
            symbols.push_back(std::move(name));
          }
        }
      }
      return true;
    }

    bool ParseElfUndefinedExternalSymbols(std::string_view bytes,
                                          std::vector<std::string> &symbols)
    {
      if (!IsElfBytes(bytes) || bytes.size() < 64)
      {
        return false;
      }
      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr std::size_t kEIClass = 4;
      constexpr std::size_t kEIData = 5;
      constexpr unsigned char kElfClass64 = 2;
      constexpr unsigned char kElfDataLittle = 1;
      constexpr uint16_t kETRel = 1;
      constexpr uint32_t kSHTSymTab = 2;
      constexpr uint32_t kSHTDynSym = 11;
      constexpr uint16_t kSHNUndef = 0;
      constexpr uint8_t kSTBGlobal = 1;
      constexpr uint8_t kSTBWeak = 2;

      if (data[kEIClass] != kElfClass64 || data[kEIData] != kElfDataLittle)
      {
        return false;
      }
      if (ReadU16(data + 16) != kETRel)
      {
        return true;
      }

      const std::size_t shoff = static_cast<std::size_t>(ReadU64(data + 40));
      const std::size_t shentsize = ReadU16(data + 58);
      const std::size_t shnum = ReadU16(data + 60);
      if (shoff > bytes.size() ||
          shentsize == 0 ||
          shoff + shentsize * shnum > bytes.size())
      {
        return false;
      }

      for (std::size_t i = 0; i < shnum; ++i)
      {
        const std::size_t sh_offset = shoff + i * shentsize;
        const unsigned char *sh = data + sh_offset;
        const uint32_t type = ReadU32(sh + 4);
        if (type != kSHTSymTab && type != kSHTDynSym)
        {
          continue;
        }

        const std::size_t section_offset = static_cast<std::size_t>(ReadU64(sh + 24));
        const std::size_t section_size = static_cast<std::size_t>(ReadU64(sh + 32));
        const uint32_t link = ReadU32(sh + 40);
        const std::size_t entsize = static_cast<std::size_t>(ReadU64(sh + 56));
        if (section_offset > bytes.size() ||
            section_size > bytes.size() - section_offset ||
            entsize == 0 ||
            section_size % entsize != 0 ||
            link >= shnum)
        {
          return false;
        }

        const unsigned char *linked_section =
            data + shoff + static_cast<std::size_t>(link) * shentsize;
        const std::size_t str_offset =
            static_cast<std::size_t>(ReadU64(linked_section + 24));
        const std::size_t str_size =
            static_cast<std::size_t>(ReadU64(linked_section + 32));
        if (str_offset > bytes.size() || str_size > bytes.size() - str_offset)
        {
          return false;
        }
        const char *strtab = bytes.data() + str_offset;

        const std::size_t count = section_size / entsize;
        for (std::size_t sym_index = 0; sym_index < count; ++sym_index)
        {
          const unsigned char *sym =
              data + section_offset + sym_index * entsize;
          const uint32_t name_offset = ReadU32(sym);
          const uint8_t info = sym[4];
          const uint16_t shndx = ReadU16(sym + 6);
          const uint8_t binding = static_cast<uint8_t>(info >> 4);
          if (name_offset >= str_size || shndx != kSHNUndef ||
              (binding != kSTBGlobal && binding != kSTBWeak))
          {
            continue;
          }

          std::string name;
          for (std::size_t pos = name_offset; pos < str_size; ++pos)
          {
            const char c = strtab[pos];
            if (c == '\0')
            {
              break;
            }
            name.push_back(c);
          }
          if (!name.empty())
          {
            symbols.push_back(std::move(name));
          }
        }
      }
      return true;
    }

    bool ParseMachOSymbols(std::string_view bytes,
                           std::vector<std::string> &symbols,
                           bool defined_external_only)
    {
      if (!IsMachO64LittleArm64SymbolTableBytes(bytes))
      {
        return false;
      }

      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr uint32_t kLcSymtab = 0x2u;
      constexpr uint8_t kNExt = 0x01u;
      constexpr uint8_t kNoSect = 0u;
      constexpr std::size_t kMachHeader64Size = 32;
      constexpr std::size_t kNlist64Size = 16;

      const uint32_t command_count = ReadU32(data + 16);
      const uint32_t command_bytes = ReadU32(data + 20);
      if (kMachHeader64Size + static_cast<std::size_t>(command_bytes) >
          bytes.size())
      {
        return false;
      }

      std::size_t command_offset = kMachHeader64Size;
      for (uint32_t command_index = 0; command_index < command_count;
           ++command_index)
      {
        if (command_offset + 8 > bytes.size())
        {
          return false;
        }
        const unsigned char *command = data + command_offset;
        const uint32_t command_kind = ReadU32(command);
        const uint32_t command_size = ReadU32(command + 4);
        if (command_size < 8 || command_offset + command_size > bytes.size())
        {
          return false;
        }

        if (command_kind == kLcSymtab)
        {
          if (command_size < 24)
          {
            return false;
          }
          const std::size_t symbol_offset =
              static_cast<std::size_t>(ReadU32(command + 8));
          const std::size_t symbol_count =
              static_cast<std::size_t>(ReadU32(command + 12));
          const std::size_t string_table_offset =
              static_cast<std::size_t>(ReadU32(command + 16));
          const std::size_t string_table_size =
              static_cast<std::size_t>(ReadU32(command + 20));

          if (symbol_offset > bytes.size() ||
              symbol_count > (bytes.size() - symbol_offset) / kNlist64Size ||
              string_table_offset > bytes.size() ||
              string_table_size > bytes.size() - string_table_offset)
          {
            return false;
          }

          for (std::size_t i = 0; i < symbol_count; ++i)
          {
            const std::size_t entry_offset =
                symbol_offset + i * kNlist64Size;
            const unsigned char *entry = data + entry_offset;
            const uint32_t name_offset = ReadU32(entry);
            const uint8_t type = entry[4];
            const uint8_t section = entry[5];
            const bool is_external = (type & kNExt) != 0;
            const bool is_defined = section != kNoSect;

            if (!is_defined)
            {
              continue;
            }
            if (defined_external_only && !is_external)
            {
              continue;
            }

            const auto name = MachOSymbolName(bytes,
                                              string_table_offset,
                                              string_table_size,
                                              name_offset);
            if (name.has_value())
            {
              symbols.push_back(*name);
            }
          }
        }

        command_offset += command_size;
      }
      return true;
    }

    bool ParseMachOUndefinedExternalSymbols(std::string_view bytes,
                                            std::vector<std::string> &symbols)
    {
      if (!IsMachO64LittleArm64SymbolTableBytes(bytes))
      {
        return false;
      }

      const unsigned char *data =
          reinterpret_cast<const unsigned char *>(bytes.data());
      constexpr uint32_t kLcSymtab = 0x2u;
      constexpr uint8_t kNExt = 0x01u;
      constexpr uint8_t kNoSect = 0u;
      constexpr std::size_t kMachHeader64Size = 32;
      constexpr std::size_t kNlist64Size = 16;

      const uint32_t command_count = ReadU32(data + 16);
      const uint32_t command_bytes = ReadU32(data + 20);
      if (kMachHeader64Size + static_cast<std::size_t>(command_bytes) >
          bytes.size())
      {
        return false;
      }

      std::size_t command_offset = kMachHeader64Size;
      for (uint32_t command_index = 0; command_index < command_count;
           ++command_index)
      {
        if (command_offset + 8 > bytes.size())
        {
          return false;
        }
        const unsigned char *command = data + command_offset;
        const uint32_t command_kind = ReadU32(command);
        const uint32_t command_size = ReadU32(command + 4);
        if (command_size < 8 || command_offset + command_size > bytes.size())
        {
          return false;
        }

        if (command_kind == kLcSymtab)
        {
          if (command_size < 24)
          {
            return false;
          }
          const std::size_t symbol_offset =
              static_cast<std::size_t>(ReadU32(command + 8));
          const std::size_t symbol_count =
              static_cast<std::size_t>(ReadU32(command + 12));
          const std::size_t string_table_offset =
              static_cast<std::size_t>(ReadU32(command + 16));
          const std::size_t string_table_size =
              static_cast<std::size_t>(ReadU32(command + 20));

          if (symbol_offset > bytes.size() ||
              symbol_count > (bytes.size() - symbol_offset) / kNlist64Size ||
              string_table_offset > bytes.size() ||
              string_table_size > bytes.size() - string_table_offset)
          {
            return false;
          }

          for (std::size_t i = 0; i < symbol_count; ++i)
          {
            const std::size_t entry_offset =
                symbol_offset + i * kNlist64Size;
            const unsigned char *entry = data + entry_offset;
            const uint32_t name_offset = ReadU32(entry);
            const uint8_t type = entry[4];
            const uint8_t section = entry[5];
            const bool is_external = (type & kNExt) != 0;
            const bool is_undefined = section == kNoSect;

            if (!is_external || !is_undefined)
            {
              continue;
            }

            const auto name = MachOSymbolName(bytes,
                                              string_table_offset,
                                              string_table_size,
                                              name_offset);
            if (name.has_value())
            {
              symbols.push_back(*name);
            }
          }
        }

        command_offset += command_size;
      }
      return true;
    }

    bool ParseObjectSymbols(std::string_view bytes,
                            std::vector<std::string> &symbols,
                            bool defined_external_only,
                            bool ignore_comdat_externals = false)
    {
      if (IsElfBytes(bytes))
      {
        return ParseElfSymbols(
            bytes,
            symbols,
            defined_external_only,
            !ignore_comdat_externals);
      }
      if (IsMachO64LittleArm64SymbolTableBytes(bytes))
      {
        return ParseMachOSymbols(bytes, symbols, defined_external_only);
      }
      return ParseCoffSymbols(bytes,
                              symbols,
                              defined_external_only,
                              ignore_comdat_externals);
    }

    bool ParseArchiveSymbols(std::string_view bytes,
                             std::vector<std::string> &symbols,
                             bool defined_external_only)
    {
      if (!IsArchiveBytes(bytes))
      {
        return false;
      }
      std::size_t offset = 8;
      while (offset + 60 <= bytes.size())
      {
        const std::string_view header(bytes.data() + offset, 60);
        const std::string name = TrimArchiveName(header.substr(0, 16));
        std::size_t size = 0;
        if (!ParseDecimal(header.substr(48, 10), &size))
        {
          return false;
        }
        const std::size_t data_offset = offset + 60;
        if (data_offset + size > bytes.size())
        {
          return false;
        }
        const std::string_view member(bytes.data() + data_offset, size);
        const auto member_view = ArchiveMemberViewOf(name, member);
        if (!member_view.has_value())
        {
          return false;
        }
        if (!IsSpecialArchiveMember(member_view->name))
        {
          if (!ParseObjectSymbols(member_view->payload,
                                  symbols,
                                  defined_external_only))
          {
            return false;
          }
        }
        offset = data_offset + size;
        if (offset % 2 == 1)
        {
          ++offset;
        }
      }
      return true;
    }

    bool ParseArchiveUndefinedExternalSymbols(std::string_view bytes,
                                              std::vector<std::string> &symbols)
    {
      if (!IsArchiveBytes(bytes))
      {
        return false;
      }
      std::size_t offset = 8;
      while (offset + 60 <= bytes.size())
      {
        const std::string_view header(bytes.data() + offset, 60);
        const std::string name = TrimArchiveName(header.substr(0, 16));
        std::size_t size = 0;
        if (!ParseDecimal(header.substr(48, 10), &size))
        {
          return false;
        }
        const std::size_t data_offset = offset + 60;
        if (data_offset + size > bytes.size())
        {
          return false;
        }
        const std::string_view member(bytes.data() + data_offset, size);
        const auto member_view = ArchiveMemberViewOf(name, member);
        if (!member_view.has_value())
        {
          return false;
        }
        if (!IsSpecialArchiveMember(member_view->name))
        {
          if (IsElfBytes(member_view->payload) &&
              !ParseElfUndefinedExternalSymbols(member_view->payload, symbols))
          {
            return false;
          }
          if (IsMachO64LittleArm64ObjectBytes(member_view->payload) &&
              !ParseMachOUndefinedExternalSymbols(member_view->payload, symbols))
          {
            return false;
          }
        }
        offset = data_offset + size;
        if (offset % 2 == 1)
        {
          ++offset;
        }
      }
      return true;
    }

    bool RelocatableInputsReferenceUndefinedSymbol(
        const std::vector<std::filesystem::path> &inputs,
        std::string_view symbol)
    {
      for (const auto &input : inputs)
      {
        const auto bytes = ReadFileBytes(input);
        if (!bytes.has_value())
        {
          continue;
        }

        std::vector<std::string> undefined;
        if (IsArchiveBytes(*bytes))
        {
          if (!ParseArchiveUndefinedExternalSymbols(*bytes, undefined))
          {
            continue;
          }
        }
        else if (IsElfBytes(*bytes))
        {
          if (!ParseElfUndefinedExternalSymbols(*bytes, undefined))
          {
            continue;
          }
        }
        else if (IsMachO64LittleArm64ObjectBytes(*bytes))
        {
          if (!ParseMachOUndefinedExternalSymbols(*bytes, undefined))
          {
            continue;
          }
        }
        else
        {
          continue;
        }

        if (std::find(undefined.begin(), undefined.end(), symbol) !=
            undefined.end())
        {
          return true;
        }
      }
      return false;
    }

    std::optional<ObjectFormat> DetectObjectFormatForBytes(std::string_view bytes)
    {
      if (IsElfBytes(bytes))
      {
        return ObjectFormat::Elf;
      }
      if (IsMachO64LittleArm64ObjectBytes(bytes))
      {
        return ObjectFormat::MachO;
      }

      std::vector<std::string> ignored;
      if (ParseCoffSymbols(bytes, ignored, false))
      {
        return ObjectFormat::Coff;
      }
      return std::nullopt;
    }

    bool ArchiveMembersMatchObjectFormat(std::string_view bytes,
                                         ObjectFormat expected_format)
    {
      if (!IsArchiveBytes(bytes))
      {
        return false;
      }

      bool saw_member = false;
      std::size_t offset = 8;
      while (offset + 60 <= bytes.size())
      {
        const std::string_view header(bytes.data() + offset, 60);
        const std::string name = TrimArchiveName(header.substr(0, 16));
        std::size_t size = 0;
        if (!ParseDecimal(header.substr(48, 10), &size))
        {
          return false;
        }
        const std::size_t data_offset = offset + 60;
        if (data_offset + size > bytes.size())
        {
          return false;
        }
        const std::string_view member(bytes.data() + data_offset, size);
        const auto member_view = ArchiveMemberViewOf(name, member);
        if (!member_view.has_value())
        {
          return false;
        }
        if (!IsSpecialArchiveMember(member_view->name))
        {
          const auto member_format =
              DetectObjectFormatForBytes(member_view->payload);
          if (!member_format.has_value() || *member_format != expected_format)
          {
            return false;
          }
          saw_member = true;
        }
        offset = data_offset + size;
        if (offset % 2 == 1)
        {
          ++offset;
        }
      }
      return saw_member;
    }

    bool LinkInputMatchesObjectFormat(const std::filesystem::path &input,
                                      ObjectFormat expected_format)
    {
      const auto bytes = ReadFileBytes(input);
      if (!bytes.has_value())
      {
        return false;
      }
      if (IsArchiveBytes(*bytes))
      {
        return ArchiveMembersMatchObjectFormat(*bytes, expected_format);
      }
      const auto actual_format = DetectObjectFormatForBytes(*bytes);
      return actual_format.has_value() && *actual_format == expected_format;
    }

    std::optional<std::vector<std::string>> LinkerSymsForInputs(
        const std::vector<std::filesystem::path> &inputs)
    {
      std::unordered_set<std::string> seen;
      for (const auto &input : inputs)
      {
        const auto bytes = ReadFileBytes(input);
        if (!bytes.has_value())
        {
          if (IsLinkerResolvedLibraryName(input))
          {
            if (LinkDebugEnabled())
            {
              std::fprintf(stderr,
                           "[link-debug] symbol-scan-skip-linker-name path=%s\n",
                           input.string().c_str());
            }
            continue;
          }
          if (LinkDebugEnabled())
          {
            std::fprintf(stderr,
                         "[link-debug] symbol-scan-read-fail path=%s\n",
                         input.string().c_str());
          }
          return std::nullopt;
        }
        std::vector<std::string> symbols;
        if (IsArchiveBytes(*bytes))
        {
          if (!ParseArchiveSymbols(*bytes, symbols, true))
          {
            if (LinkDebugEnabled())
            {
              std::fprintf(stderr,
                           "[link-debug] symbol-scan-archive-parse-fail path=%s\n",
                           input.string().c_str());
            }
            return std::nullopt;
          }
        }
        else
        {
          if (!ParseObjectSymbols(*bytes, symbols, true))
          {
            if (LinkDebugEnabled())
            {
              std::fprintf(stderr,
                           "[link-debug] symbol-scan-object-parse-fail path=%s\n",
                           input.string().c_str());
            }
            return std::nullopt;
          }
        }
        for (const auto &sym : symbols)
        {
          seen.insert(sym);
        }
      }
      std::vector<std::string> out;
      out.reserve(seen.size());
      for (const auto &sym : seen)
      {
        out.push_back(sym);
      }
      std::sort(out.begin(), out.end());
      return out;
    }

    std::optional<std::vector<std::string>> DuplicateDefinedExternalSymbolsForObjectInputs(
        const std::vector<std::filesystem::path> &inputs)
    {
      std::unordered_set<std::string> seen;
      std::unordered_set<std::string> duplicate_set;
      for (const auto &input : inputs)
      {
        const auto bytes = ReadFileBytes(input);
        if (!bytes.has_value())
        {
          return std::nullopt;
        }
        if (IsArchiveBytes(*bytes))
        {
          continue;
        }
        if (IsMachO64LittleArm64DylibBytes(*bytes))
        {
          continue;
        }

        std::vector<std::string> symbols;
        if (!ParseObjectSymbols(*bytes, symbols, true, true))
        {
          return std::nullopt;
        }
        for (const auto &sym : symbols)
        {
          if (IsCoffBackendHelperSymbolForDuplicateScan(sym))
          {
            continue;
          }
          if (!seen.insert(sym).second)
          {
            duplicate_set.insert(sym);
          }
        }
      }

      std::vector<std::string> duplicates;
      duplicates.reserve(duplicate_set.size());
      for (const auto &symbol : duplicate_set)
      {
        duplicates.push_back(symbol);
      }
      std::sort(duplicates.begin(), duplicates.end());
      return duplicates;
    }

    std::optional<std::vector<std::string>> DefinedExternalSymbolsForObjectInputs(
        const std::vector<std::filesystem::path> &inputs)
    {
      std::unordered_set<std::string> seen;
      for (const auto &input : inputs)
      {
        const auto bytes = ReadFileBytes(input);
        if (!bytes.has_value())
        {
          return std::nullopt;
        }
        if (IsArchiveBytes(*bytes))
        {
          continue;
        }
        if (IsMachO64LittleArm64DylibBytes(*bytes))
        {
          continue;
        }

        std::vector<std::string> symbols;
        if (!ParseObjectSymbols(*bytes, symbols, true))
        {
          return std::nullopt;
        }
        for (const auto &sym : symbols)
        {
          seen.insert(sym);
        }
      }

      std::vector<std::string> defined;
      defined.reserve(seen.size());
      for (const auto &symbol : seen)
      {
        defined.push_back(symbol);
      }
      std::sort(defined.begin(), defined.end());
      return defined;
    }

    std::optional<std::string> FirstMissingRuntimeSym(
        const std::vector<std::string> &syms)
    {
      const auto required = RuntimeRequiredSyms();
      for (const auto &req : required)
      {
        if (!std::binary_search(syms.begin(), syms.end(), req))
        {
          return req;
        }
      }
      return std::nullopt;
    }

    bool IsMissingExplicitLibraryInput(const std::filesystem::path &input)
    {
      if (input.empty())
      {
        return false;
      }
      if (!input.has_parent_path() && !input.is_absolute())
      {
        return false;
      }
      std::error_code ec;
      return !std::filesystem::exists(input, ec);
    }

    bool OutputSuggestsMissingLibrary(std::string_view output)
    {
      static constexpr std::string_view kNeedles[] = {
          "unable to find library",
          "could not open",
          "cannot open",
          "can't open",
          "no such file",
          "file not found",
          "cannot find",
          "could not find",
      };
      for (const auto needle : kNeedles)
      {
        if (output.find(needle) != std::string_view::npos)
        {
          return true;
        }
      }
      return false;
    }

    bool OutputMentionsLibrary(std::string_view output,
                               const std::filesystem::path &input)
    {
      const std::string generic =
          LowerAsciiForDiagnosticMatch(input.generic_string());
      const std::string filename =
          LowerAsciiForDiagnosticMatch(input.filename().generic_string());
      const std::string stem =
          LowerAsciiForDiagnosticMatch(input.stem().generic_string());
      return (!generic.empty() && output.find(generic) != std::string_view::npos) ||
             (!filename.empty() &&
              output.find(filename) != std::string_view::npos) ||
             (!stem.empty() && output.find(stem) != std::string_view::npos);
    }

    bool IsMissingNamedLibraryFailure(
        const std::vector<std::filesystem::path> &inputs,
        std::string_view output)
    {
      if (output.empty())
      {
        return false;
      }
      const std::string lowered = LowerAsciiForDiagnosticMatch(output);
      if (!OutputSuggestsMissingLibrary(lowered))
      {
        return false;
      }
      for (const auto &input : inputs)
      {
        if (input.empty() || IsMissingExplicitLibraryInput(input))
        {
          continue;
        }
        if (OutputMentionsLibrary(lowered, input))
        {
          return true;
        }
      }
      return false;
    }

    std::filesystem::path LinkTranscriptPath(
        const std::filesystem::path &output_path)
    {
      auto transcript_path = output_path;
      if (transcript_path.has_extension())
      {
        transcript_path.replace_extension(".linker.log");
      }
      else
      {
        transcript_path += ".linker.log";
      }
      return transcript_path;
    }

    std::string LinkerTranscriptText(const LinkInvocationResult &result)
    {
      std::ostringstream oss;
      oss << "tool: " << result.tool_path.generic_string() << "\n";
      oss << "cwd: " << result.working_directory.generic_string() << "\n";
      oss << "cmd: " << CommandLineForDebug(result.argv) << "\n";
      oss << "argv_count: " << result.argv.size() << "\n";
      for (std::size_t i = 0; i < result.argv.size(); ++i)
      {
        oss << "argv[" << i << "]: " << result.argv[i] << "\n";
      }
      oss << "launched: " << (result.launched ? "true" : "false") << "\n";
      oss << "exit_code: " << result.exit_code << "\n";
      if (!result.launch_error.empty())
      {
        oss << "launch_error: " << result.launch_error << "\n";
      }
      oss << "crashed: " << (result.crashed ? "true" : "false") << "\n";
      if (!result.crash_kind.empty())
      {
        oss << "crash_kind: " << result.crash_kind << "\n";
      }
      if (!result.crash_report_json_path.empty())
      {
        oss << "crash_report_json: "
            << result.crash_report_json_path.generic_string() << "\n";
      }
      oss << "\n[stdout]\n";
      oss << result.stdout_text;
      if (!result.stdout_text.empty() && result.stdout_text.back() != '\n')
      {
        oss << "\n";
      }
      oss << "\n[stderr]\n";
      oss << result.stderr_text;
      if (!result.stderr_text.empty() && result.stderr_text.back() != '\n')
      {
        oss << "\n";
      }
      return oss.str();
    }

    std::optional<std::filesystem::path> WriteLinkerTranscript(
        const std::filesystem::path &output_path,
        const LinkInvocationResult &result)
    {
      const auto transcript_path = LinkTranscriptPath(output_path);
      std::error_code ec;
      std::filesystem::create_directories(transcript_path.parent_path(), ec);

      std::ofstream out(transcript_path, std::ios::binary | std::ios::trunc);
      if (!out)
      {
        return std::nullopt;
      }
      const std::string transcript = LinkerTranscriptText(result);
      out.write(transcript.data(), static_cast<std::streamsize>(transcript.size()));
      out.close();
      if (!out)
      {
        return std::nullopt;
      }
      return transcript_path;
    }

    void EmitLinkDebugBlock(std::string_view label, std::string_view text)
    {
      std::fprintf(stderr, "[link-debug] %.*s-begin\n", static_cast<int>(label.size()),
                   label.data());
      if (!text.empty())
      {
        std::fwrite(text.data(), 1, text.size(), stderr);
        if (text.back() != '\n')
        {
          std::fwrite("\n", 1, 1, stderr);
        }
      }
      std::fprintf(stderr, "[link-debug] %.*s-end\n", static_cast<int>(label.size()),
                   label.data());
    }

    void EmitLinkDebugRecord(const LinkInvocationResult &result)
    {
      std::fprintf(stderr, "[link-debug] tool=%s\n",
                   result.tool_path.generic_string().c_str());
      std::fprintf(stderr, "[link-debug] cwd=%s\n",
                   result.working_directory.generic_string().c_str());
      std::fprintf(stderr, "[link-debug] cmd=%s\n",
                   CommandLineForDebug(result.argv).c_str());
      std::fprintf(stderr, "[link-debug] argv-count=%zu\n", result.argv.size());
      for (std::size_t i = 0; i < result.argv.size(); ++i)
      {
        std::fprintf(stderr, "[link-debug] argv[%zu]=%s\n", i,
                     result.argv[i].c_str());
      }
      std::fprintf(stderr, "[link-debug] launched=%s\n",
                   result.launched ? "true" : "false");
      std::fprintf(stderr, "[link-debug] exit=%d\n", result.exit_code);
      if (!result.launch_error.empty())
      {
        std::fprintf(stderr, "[link-debug] launch-error=%s\n",
                     result.launch_error.c_str());
      }
      if (!result.transcript_path.empty())
      {
        std::fprintf(stderr, "[link-debug] transcript=%s\n",
                     result.transcript_path.generic_string().c_str());
      }
      if (!result.crash_report_json_path.empty())
      {
        std::fprintf(stderr, "[link-debug] crash-report=%s\n",
                     result.crash_report_json_path.generic_string().c_str());
      }
      if (!result.crash_kind.empty())
      {
        std::fprintf(stderr, "[link-debug] crash-kind=%s\n",
                     result.crash_kind.c_str());
      }
      EmitLinkDebugBlock("stdout", result.stdout_text);
      EmitLinkDebugBlock("stderr", result.stderr_text);
    }

    std::string SummarizeToolOutput(std::string_view output)
    {
      std::string summary;
      bool saw_non_space = false;
      for (const char ch : output)
      {
        if (ch == '\r' || ch == '\n')
        {
          if (saw_non_space)
          {
            break;
          }
          continue;
        }
        if (summary.empty() && (ch == ' ' || ch == '\t'))
        {
          continue;
        }
        saw_non_space = true;
        summary.push_back(ch);
        if (summary.size() >= 240)
        {
          summary += "...";
          break;
        }
      }
      return summary;
    }

    std::filesystem::path ResolveManifestToolchainPath(
        const Project &project,
        std::string_view raw_path)
    {
      const std::filesystem::path path(raw_path);
      if (path.is_absolute() || !core::IsRelative(raw_path))
      {
        return path;
      }
      return (project.root / path).lexically_normal();
    }

    std::filesystem::path DefaultRuntimeLibPath(const Project &project,
                                                TargetProfile target_profile)
    {
      return CompilerRuntimeLibPath(project, target_profile);
    }

  } // namespace

  std::filesystem::path RuntimeLibPath(const Project &project,
                                       TargetProfile target_profile)
  {
    // Spec rule:
    // 1. CLI/manifest override
    // 2. Compiler-provided runtime
    if (const auto override_lib = core::RuntimeLibOverride();
        override_lib.has_value() && !override_lib->empty())
    {
      return std::filesystem::path(*override_lib);
    }

    if (const auto manifest_lib = core::ManifestRuntimeLib();
        manifest_lib.has_value() && !manifest_lib->empty())
    {
      return ResolveManifestToolchainPath(project, *manifest_lib);
    }

    return DefaultRuntimeLibPath(project, target_profile);
  }

  std::vector<std::string> RuntimeRequiredSyms()
  {
    return core::RuntimeLinkRequiredSyms(ActiveLanguageProfile().runtime_root);
  }

  bool IsHiddenSharedLibraryExportSymbol(std::string_view symbol)
  {
    return TargetIsHiddenSharedLibraryExportSymbol(symbol);
  }

  std::optional<std::filesystem::path> ResolveRuntimeLib(
      const Project &project,
      TargetProfile target_profile)
  {
    const auto path = RuntimeLibPath(project, target_profile);
    if (!CanReadFile(path))
    {
      SPEC_RULE("ResolveRuntimeLib-Err");
      core::HostPrimFail(core::HostPrim::ResolveRuntimeLib, true);
      return std::nullopt;
    }
    SPEC_RULE("ResolveRuntimeLib-Ok");
    return path;
  }

  std::optional<std::vector<std::string>> LinkerSyms(
      const std::filesystem::path &,
      const std::vector<std::filesystem::path> &inputs,
      const std::filesystem::path &)
  {
    return LinkerSymsForInputs(inputs);
  }

  std::optional<std::vector<std::filesystem::path>> ArchiveMembers(
      const std::filesystem::path &archive)
  {
    if (!CanReadFile(archive))
    {
      return std::nullopt;
    }
    return std::vector<std::filesystem::path>{archive};
  }

  LinkInvocationResult InvokeLinker(
      const std::filesystem::path &tool,
      const std::vector<std::filesystem::path> &inputs,
      const std::filesystem::path &output,
      const std::optional<std::filesystem::path> &import_lib,
      const LinkPlan &plan)
  {
    LinkInvocationResult result;
    const std::optional<bool> debug_override = core::LinkDebugOverride();
    const bool debug_link =
        debug_override.has_value() ? *debug_override
                                   : core::IsDebugEnabled("link");
    TargetLinkArgOptions arg_options;
    if (ObjectFormatOf(plan.target_profile) == ObjectFormat::Elf)
    {
      arg_options.inputs_reference_gxx_personality =
          RelocatableInputsReferenceUndefinedSymbol(inputs,
                                                    "__gxx_personality_v0");
      arg_options.inputs_reference_gcc_personality =
          RelocatableInputsReferenceUndefinedSymbol(inputs,
                                                    "__gcc_personality_v0");
    }
    std::vector<std::string> args =
        BuildTargetLinkArgs(tool, inputs, output, import_lib, plan, arg_options);
    result.tool_path = tool;
    result.working_directory = output.parent_path();
    result.argv = args;

    core::DebugRunOptions run_options;
    run_options.enabled =
        core::CrashReportingEnabled() && core::CrashCaptureSupported();
    run_options.program = tool;
    run_options.working_directory = result.working_directory;
    run_options.report_root = core::DefaultTargetCrashReportRoot(output);
    run_options.tool_name = "uv-link";
    for (std::size_t i = 1; i < args.size(); ++i)
    {
      run_options.arguments.push_back(args[i]);
    }

    const auto debug_result = core::DebugRunProcess(run_options);
    result.launched = debug_result.launched;
    result.ok = debug_result.launched && debug_result.exit_code == 0;
    result.crashed = debug_result.crashed;
    result.exit_code = debug_result.exit_code;
    result.launch_error = debug_result.launch_error;
    result.stdout_text = debug_result.stdout_text;
    result.stderr_text = debug_result.stderr_text;
    result.output = result.stdout_text;
    result.output += result.stderr_text;
    if (!result.launched && result.output.empty() && !result.launch_error.empty())
    {
      result.output = result.launch_error;
    }
    if (debug_result.crash_report.has_value())
    {
      result.crash_report_json_path = debug_result.crash_report->artifacts.json_path;
      result.crash_kind = debug_result.crash_report->kind;
    }
    if (debug_link || !result.ok)
    {
      if (const auto transcript_path = WriteLinkerTranscript(output, result);
          transcript_path.has_value())
      {
        result.transcript_path = *transcript_path;
      }
    }
    if (debug_link)
    {
      EmitLinkDebugRecord(result);
    }
    return result;
  }

  bool InvokeArchiver(const std::filesystem::path &tool,
                      const std::vector<std::filesystem::path> &inputs,
                      const std::filesystem::path &output)
  {
    const std::vector<std::string> args =
        BuildTargetArchiverArgs(tool, inputs, output);
    if (core::CrashReportingEnabled() && core::CrashCaptureSupported())
    {
      core::DebugRunOptions run_options;
      run_options.program = tool;
      run_options.working_directory = output.parent_path();
      run_options.report_root = core::DefaultTargetCrashReportRoot(output);
      run_options.tool_name = "uv-archiver";
      for (std::size_t i = 1; i < args.size(); ++i)
      {
        run_options.arguments.push_back(args[i]);
      }
      const auto debug_result = core::DebugRunProcess(run_options);
      return debug_result.launched && debug_result.exit_code == 0;
    }

    const bool debug_link =
        core::LinkDebugOverride().value_or(core::IsDebugEnabled("link"));
    if (debug_link)
    {
      std::fprintf(stderr, "[link-debug] archiver=%s\n", tool.string().c_str());
      std::fprintf(stderr, "[link-debug] archive-out=%s\n", output.string().c_str());
      std::fprintf(stderr, "[link-debug] archive-input-count=%zu\n", inputs.size());
      for (const auto &arg : args)
      {
        std::fprintf(stderr, "[link-debug] archive-arg=%s\n", arg.c_str());
      }
    }

    core::HostProcessSpec spec;
    spec.program = tool;
    spec.working_directory = output.parent_path();
    spec.output_mode = core::HostProcessOutputMode::CaptureMerged;
    spec.hide_window = true;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
      spec.arguments.push_back(args[i]);
    }

    const auto host_result = core::RunHostProcess(spec);
    if (debug_link)
    {
      if (!host_result.error_message.empty())
      {
        std::fprintf(stderr, "[link-debug] archive-launch-failed=%s\n",
                     host_result.error_message.c_str());
      }
      else
      {
        std::fprintf(stderr, "[link-debug] archive-exit=%d\n",
                     host_result.exit_code);
        if (!host_result.output.empty())
        {
          std::fprintf(stderr, "[link-debug] archive-output=%s\n",
                       host_result.output.c_str());
        }
      }
    }
    return host_result.launched && host_result.exit_code == 0;
  }

  std::vector<std::filesystem::path> MaterializeLinkInputsForTool(
      const Project &project,
      TargetProfile target_profile,
      const std::vector<std::filesystem::path> &inputs)
  {
    return MaterializeTargetLinkInputsForTool(project, target_profile, inputs);
  }

  std::vector<std::filesystem::path> LinkInputs(
      const std::vector<std::filesystem::path> &objs,
      const std::vector<std::filesystem::path> &library_artifact_inputs,
      const std::filesystem::path &runtime_lib)
  {
    std::vector<std::filesystem::path> inputs = objs;
    inputs.reserve(objs.size() + library_artifact_inputs.size() + 1);
    inputs.insert(inputs.end(), library_artifact_inputs.begin(),
                  library_artifact_inputs.end());
    inputs.push_back(runtime_lib);
    return inputs;
  }

  LinkResult Link(const std::vector<std::filesystem::path> &objs,
                  const std::vector<std::filesystem::path> &extra_inputs,
                  const Project &project,
                  const LinkPlan &plan,
                  const LinkDeps &deps)
  {
    LinkResult result;
    const auto output_path = LinkOutputPath(project, plan.target_profile);
    const auto import_lib = ImportLibPath(project, plan.target_profile);
    if (!output_path.has_value())
    {
      result.status = LinkStatus::Fail;
      return result;
    }

    for (const auto &input : extra_inputs)
    {
      if (IsMissingExplicitLibraryInput(input))
      {
        SPEC_RULE("Link-Library-NotFound");
        EmitExternal(result.diags, "E-SYS-3347");
        result.status = LinkStatus::Fail;
        return result;
      }
    }

    const std::string_view linker_name =
        TargetLinkerToolName(plan.target_profile);
    const auto tool = deps.resolve_tool(project, plan.target_profile, linker_name);
    if (!tool.has_value())
    {
      SPEC_RULE("Link-NotFound");
      if (auto diag = core::MakeExternalDiagnostic("E-OUT-0405"))
      {
        core::SubDiagnostic guidance_note;
        guidance_note.kind = core::SubDiagnosticKind::Note;
        guidance_note.message =
            "set llvm_bin in [toolchain] in " +
            std::string(ActiveLanguageProfile().manifest_name);
        diag->children.push_back(std::move(guidance_note));

        core::SubDiagnostic search_note;
        search_note.kind = core::SubDiagnosticKind::Note;
        search_note.message = FormatSearchedPaths(project, plan.target_profile, linker_name);
        diag->children.push_back(std::move(search_note));
        core::Emit(result.diags, *diag);
      }
      result.status = LinkStatus::NotFound;
      return result;
    }
    const std::filesystem::path tool_path = *tool;

    const auto materialized_extra_inputs =
        MaterializeLinkInputsForTool(project, plan.target_profile, extra_inputs);

    const auto runtime_lib =
        deps.resolve_runtime_lib(project, plan.target_profile);
    if (!runtime_lib.has_value())
    {
      SPEC_RULE("Link-Runtime-Missing");
      if (auto diag = core::MakeExternalDiagnostic("E-OUT-0407"))
      {
        core::SubDiagnostic guidance_note;
        guidance_note.kind = core::SubDiagnosticKind::Note;
        guidance_note.message =
            "set --runtime-lib <path> or add runtime_lib to [toolchain] in " +
            std::string(ActiveLanguageProfile().manifest_name);
        diag->children.push_back(std::move(guidance_note));

        // Collect runtime lib search locations
        const auto rt_path = RuntimeLibPath(project, plan.target_profile);
        std::string search_info = "searched for runtime library at: " +
                                  rt_path.string();
        core::SubDiagnostic search_note;
        search_note.kind = core::SubDiagnosticKind::Note;
        search_note.message = std::move(search_info);
        diag->children.push_back(std::move(search_note));
        core::Emit(result.diags, *diag);
      }
      result.status = LinkStatus::RuntimeMissing;
      return result;
    }

    if (!LinkInputMatchesObjectFormat(
            *runtime_lib, ObjectFormatOf(plan.target_profile)))
    {
      if (LinkDebugEnabled())
      {
        std::fprintf(stderr,
                     "[link-debug] runtime-format-mismatch path=%s target=%s\n",
                     runtime_lib->string().c_str(),
                     std::string(TargetProfileName(plan.target_profile)).c_str());
      }
      if (auto diag = core::MakeExternalDiagnostic("E-OUT-0408"))
      {
        core::SubDiagnostic note;
        note.kind = core::SubDiagnosticKind::Note;
        note.message = "runtime library `" + runtime_lib->string() +
                       "` does not match target object format for `" +
                       std::string(TargetProfileName(plan.target_profile)) +
                       "`";
        diag->children.push_back(std::move(note));
        core::Emit(result.diags, *diag);
      }
      else
      {
        EmitExternal(result.diags, "E-OUT-0408");
      }
      result.status = LinkStatus::RuntimeIncompatible;
      return result;
    }

    const auto logical_inputs =
        LinkInputs(objs, materialized_extra_inputs, *runtime_lib);
    const auto runtime_sidecars =
        TargetRuntimeSidecars(plan.target_profile, *runtime_lib);
    if (runtime_sidecars.size() !=
        TargetRequiredRuntimeSidecarCount(plan.target_profile))
    {
      if (auto diag = core::MakeExternalDiagnostic("E-OUT-0407"))
      {
        core::SubDiagnostic note;
        note.kind = core::SubDiagnosticKind::Note;
        note.message = TargetRuntimeSidecarMissingMessage(plan.target_profile);
        diag->children.push_back(std::move(note));
        core::Emit(result.diags, *diag);
      }
      else
      {
        EmitExternal(result.diags, "E-OUT-0407");
      }
      result.status = LinkStatus::RuntimeMissing;
      return result;
    }
    std::optional<std::filesystem::path> startup_object;
    if (TargetExecutableRequiresStartupObject(plan))
    {
      startup_object = TargetRuntimeStartupObjectPath(project, plan.target_profile,
                                                      *runtime_lib);
      if (!startup_object.has_value())
      {
        if (auto diag = core::MakeExternalDiagnostic("E-OUT-0407"))
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message =
              TargetRuntimeStartupObjectMissingMessage(plan.target_profile);
          diag->children.push_back(std::move(note));
          core::Emit(result.diags, *diag);
        }
        else
        {
          EmitExternal(result.diags, "E-OUT-0407");
        }
        result.status = LinkStatus::RuntimeMissing;
        return result;
      }
      if (!LinkInputMatchesObjectFormat(*startup_object,
                                        ObjectFormatOf(plan.target_profile)))
      {
        if (auto diag = core::MakeExternalDiagnostic("E-OUT-0408"))
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message = "runtime startup object `" +
                         startup_object->string() +
                         "` does not match target object format for `" +
                         std::string(TargetProfileName(plan.target_profile)) +
                         "`";
          diag->children.push_back(std::move(note));
          core::Emit(result.diags, *diag);
        }
        else
        {
          EmitExternal(result.diags, "E-OUT-0408");
        }
        result.status = LinkStatus::RuntimeIncompatible;
        return result;
      }
    }

    std::vector<std::filesystem::path> inputs = objs;
    inputs.reserve(objs.size() + materialized_extra_inputs.size() +
                   (startup_object.has_value() ? 2u : 1u) +
                   runtime_sidecars.size() + 4u);
    for (const auto &input : materialized_extra_inputs)
    {
      inputs.push_back(input);
    }
    if (startup_object.has_value())
    {
      inputs.push_back(*startup_object);
    }
    inputs.push_back(*runtime_lib);
    if (ObjectFormatOf(plan.target_profile) == ObjectFormat::Elf)
    {
      std::vector<std::filesystem::path> linkable_runtime_sidecars;
      linkable_runtime_sidecars.reserve(runtime_sidecars.size());
      for (const auto &sidecar : runtime_sidecars)
      {
        if (TargetRuntimeSidecarIsLinkable(sidecar))
        {
          linkable_runtime_sidecars.push_back(sidecar);
        }
      }
      if (!linkable_runtime_sidecars.empty())
      {
        inputs.push_back("--no-as-needed");
        inputs.insert(inputs.end(), linkable_runtime_sidecars.begin(),
                      linkable_runtime_sidecars.end());
        inputs.push_back("--as-needed");
      }
    }
    else
    {
      inputs.insert(inputs.end(), runtime_sidecars.begin(),
                    runtime_sidecars.end());
    }

    std::vector<std::filesystem::path> duplicate_symbol_inputs = objs;
    duplicate_symbol_inputs.reserve(objs.size() + materialized_extra_inputs.size() +
                                    (startup_object.has_value() ? 1u : 0u));
    for (const auto &input : materialized_extra_inputs)
    {
      duplicate_symbol_inputs.push_back(input);
    }
    if (startup_object.has_value())
    {
      duplicate_symbol_inputs.push_back(*startup_object);
    }
    const auto duplicate_symbols =
        DuplicateDefinedExternalSymbolsForObjectInputs(duplicate_symbol_inputs);
    if (duplicate_symbols.has_value() && !duplicate_symbols->empty())
    {
      if (auto diag = core::MakeExternalDiagnostic("E-SYS-3342"))
      {
        core::SubDiagnostic note;
        note.kind = core::SubDiagnosticKind::Note;
        note.message = "duplicate link symbol: " + duplicate_symbols->front();
        diag->children.push_back(std::move(note));
        core::Emit(result.diags, *diag);
      }
      else
      {
        EmitExternal(result.diags, "E-SYS-3342");
      }
      result.status = LinkStatus::Fail;
      return result;
    }

    const auto syms = deps.linker_syms(tool_path, logical_inputs, *output_path);
    const auto missing_runtime_sym =
        syms.has_value() ? FirstMissingRuntimeSym(*syms) : std::nullopt;
    if (!syms.has_value() || missing_runtime_sym.has_value())
    {
      if (LinkDebugEnabled())
      {
        if (!syms.has_value())
        {
          std::fprintf(stderr,
                       "[link-debug] runtime-symbol-scan-failed input_count=%zu\n",
                       logical_inputs.size());
        }
        else
        {
          std::fprintf(stderr,
                       "[link-debug] runtime-symbol-missing symbol=%s\n",
                       missing_runtime_sym->c_str());
        }
      }
      SPEC_RULE("Link-Runtime-Incompatible");
      EmitExternal(result.diags, "E-OUT-0408");
      result.status = LinkStatus::RuntimeIncompatible;
      return result;
    }

    const auto link_result =
        deps.invoke_linker(tool_path, inputs, *output_path, import_lib, plan);
    if (!link_result.ok)
    {
      core::HostPrimFail(core::HostPrim::InvokeLinker, true);
      const bool missing_library =
          IsMissingNamedLibraryFailure(extra_inputs, link_result.output);
      if (auto diag = core::MakeExternalDiagnostic(missing_library
                                                       ? "E-SYS-3347"
                                                       : "E-OUT-0404"))
      {
        if (!link_result.transcript_path.empty())
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message =
              "linker transcript: " +
              link_result.transcript_path.generic_string();
          diag->children.push_back(std::move(note));
        }
        if (link_result.exit_code >= 0)
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message =
              "linker exit code: " + std::to_string(link_result.exit_code);
          diag->children.push_back(std::move(note));
        }
        if (!link_result.launch_error.empty())
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message = "linker launch error: " + link_result.launch_error;
          diag->children.push_back(std::move(note));
        }
        if (!link_result.crash_report_json_path.empty())
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message =
              "linker crash report: " +
              link_result.crash_report_json_path.generic_string();
          diag->children.push_back(std::move(note));
        }
        const std::string linker_output = SummarizeToolOutput(link_result.output);
        if (!linker_output.empty())
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message = "linker summary: " + linker_output;
          diag->children.push_back(std::move(note));
        }
        if (link_result.crashed && !link_result.crash_kind.empty())
        {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message = "linker crash kind: " + link_result.crash_kind;
          diag->children.push_back(std::move(note));
        }
        core::Emit(result.diags, *diag);
      }
      else if (missing_library)
      {
        EmitExternal(result.diags, "E-SYS-3347");
      }
      else
      {
        EmitExternal(result.diags, "E-OUT-0404");
      }
      if (missing_library)
      {
        SPEC_RULE("Link-Library-NotFound");
      }
      else
      {
        SPEC_RULE("Link-Fail");
      }
      result.status = LinkStatus::Fail;
      return result;
    }

    SPEC_RULE("Link-Ok");
    result.status = LinkStatus::Ok;
    return result;
  }

  LinkResult Archive(const std::vector<std::filesystem::path> &objs,
                     const Project &project,
                     TargetProfile target_profile,
                     const LinkDeps &deps)
  {
    LinkResult result;
    const auto output_path = PrimaryArtifactPath(project, target_profile);
    if (!output_path.has_value())
    {
      result.status = LinkStatus::Fail;
      return result;
    }

    const std::string_view archiver_name =
        TargetArchiverToolName(target_profile);
    const auto tool =
        deps.resolve_tool(project, target_profile, archiver_name);
    if (!tool.has_value())
    {
      SPEC_RULE("Archive-NotFound");
      if (auto diag = core::MakeExternalDiagnostic("E-OUT-0405"))
      {
        core::SubDiagnostic guidance_note;
        guidance_note.kind = core::SubDiagnosticKind::Note;
        guidance_note.message =
            "set llvm_bin in [toolchain] in " +
            std::string(ActiveLanguageProfile().manifest_name);
        diag->children.push_back(std::move(guidance_note));

        core::SubDiagnostic search_note;
        search_note.kind = core::SubDiagnosticKind::Note;
        search_note.message = FormatSearchedPaths(project, target_profile, archiver_name);
        diag->children.push_back(std::move(search_note));
        core::Emit(result.diags, *diag);
      }
      result.status = LinkStatus::NotFound;
      return result;
    }
    const std::filesystem::path tool_path = *tool;

    std::vector<std::filesystem::path> inputs = objs;

    if (!deps.invoke_archiver(tool_path, inputs, *output_path))
    {
      core::HostPrimFail(core::HostPrim::InvokeArchiver, true);
      SPEC_RULE("Archive-Fail");
      EmitExternal(result.diags, "E-OUT-0404");
      result.status = LinkStatus::Fail;
      return result;
    }

    SPEC_RULE("Archive-Ok");
    result.status = LinkStatus::Ok;
    return result;
  }
} // namespace ultraviolet::project
