#include "00_core/runtime_abi.h"

#include <algorithm>

#include "00_core/symbols.h"

namespace cursive::core {

namespace {

void AppendRuntimeSymbol(std::vector<std::string>& out, std::string symbol) {
  if (!symbol.empty()) {
    out.push_back(std::move(symbol));
  }
}

void SortUniqueSymbols(std::vector<std::string>& syms) {
  std::sort(syms.begin(), syms.end());
  syms.erase(std::unique(syms.begin(), syms.end()), syms.end());
}

std::string RuntimeSym(std::initializer_list<std::string_view> path) {
  return PathSig(path);
}

}  // namespace

std::vector<std::string> RuntimeLinkRequiredSyms(std::string_view runtime_root) {
  std::vector<std::string> syms;

  AppendRuntimeSymbol(syms, RuntimeSym({runtime_root, "runtime", "panic"}));
  AppendRuntimeSymbol(syms,
                      RuntimeSym({runtime_root, "runtime", "string", "drop_managed"}));
  AppendRuntimeSymbol(syms,
                      RuntimeSym({runtime_root, "runtime", "bytes", "drop_managed"}));
  AppendRuntimeSymbol(syms,
                      RuntimeSym({runtime_root, "runtime", "context_init"}));

  for (const auto* name : {
           "emit",
           "emit_int",
           "emit_bool",
           "emit_float",
           "emit_ptr",
           "emit_string",
           "emit_string_managed",
           "emit_bytes",
           "emit_bytes_managed",
           "set_sink",
           "set_root",
           "set_log_filter",
           "set_min_level",
       }) {
    AppendRuntimeSymbol(
        syms, RuntimeSym({runtime_root, "runtime", "conformance", name}));
  }

  for (const auto* name : {
           "new_scoped",
           "alloc",
           "mark",
           "reset_to",
           "reset_unchecked",
           "freeze",
           "thaw",
           "free_unchecked",
           "addr_is_active",
           "addr_tag_from",
           "scope_enter",
           "scope_exit",
           "addr_tag_scope",
       }) {
    AppendRuntimeSymbol(syms,
                        RuntimeSym({runtime_root, "runtime", "region", name}));
  }

  for (const auto* name : {
           "from",
           "as_view",
           "slice",
           "to_managed",
           "clone_with",
           "append",
           "length",
           "is_empty",
       }) {
    AppendRuntimeSymbol(syms,
                        RuntimeSym({runtime_root, "runtime", "string", name}));
  }

  for (const auto* name : {
           "with_capacity",
           "from_slice",
           "as_view",
           "to_managed",
           "view",
           "view_string",
           "as_slice",
           "append",
           "length",
           "is_empty",
       }) {
    AppendRuntimeSymbol(syms,
                        RuntimeSym({runtime_root, "runtime", "bytes", name}));
  }

  for (const auto* name : {
           "open_read",
           "open_write",
           "open_append",
           "create_write",
           "read_file",
           "read_bytes",
           "write_file",
           "write_stdout",
           "write_stderr",
           "exists",
           "remove",
           "open_dir",
           "create_dir",
           "ensure_dir",
           "kind",
           "restrict",
       }) {
    AppendRuntimeSymbol(syms, RuntimeSym({runtime_root, "runtime", "fs", name}));
  }

  AppendRuntimeSymbol(
      syms, RuntimeSym({runtime_root, "runtime", "net", "restrict_to_host"}));

  for (const auto* name : {
           "with_quota",
           "alloc_raw",
           "dealloc_raw",
       }) {
    AppendRuntimeSymbol(syms,
                        RuntimeSym({runtime_root, "runtime", "heap", name}));
  }

  for (const auto* name : {
           "exit",
           "get_env",
           "executable_path",
           "argument_count",
           "argument",
           "current_directory",
           "run",
       }) {
    AppendRuntimeSymbol(syms,
                        RuntimeSym({runtime_root, "runtime", "system", name}));
  }

  SortUniqueSymbols(syms);
  return syms;
}

std::vector<std::string> RuntimeLinkRequiredSyms() {
  return RuntimeLinkRequiredSyms("cursive");
}

}  // namespace cursive::core
