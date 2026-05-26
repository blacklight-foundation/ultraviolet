#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

enum class BuiltinModalLoweringOp {
  DirectCall,
  AllocInReceiver,
};

enum class BuiltinAsyncCombinatorKind {
  Map,
  Filter,
  Take,
  Fold,
  Chain,
};

struct BuiltinModalParam {
  std::optional<ParamMode> mode;
  TypeRef type;
};

struct BuiltinModalMemberSig {
  Permission recv_perm = Permission::Const;
  std::vector<BuiltinModalParam> params;
  TypeRef ret;
  bool ret_from_first_arg = false;
  bool requires_unsafe = false;
  std::string_view unsafe_diag = "Call-Unsafe-Required-Err";
  BuiltinModalLoweringOp lowering = BuiltinModalLoweringOp::DirectCall;
  bool allocates_in_receiver = false;
  std::string runtime_symbol;
  bool consumes_receiver = false;
};

struct BuiltinModalLayoutInfo {
  std::uint64_t size = 0;
  std::uint64_t align = 1;
  std::string disc_prim = "u8";
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
};

bool IsBuiltinModalTypePath(const TypePath& modal_path);

bool IsBuiltinModalRecordLiteralForbidden(const TypePath& modal_path);

bool IsBuiltinRuntimeHandleModalTypePath(const TypePath& modal_path);

bool IsBuiltinModalMemberName(const TypePath& modal_path,
                              std::string_view member_name);

bool IsBuiltinModalGeneralMember(const TypePath& modal_path,
                                 std::string_view member_name);

std::optional<BuiltinAsyncCombinatorKind> LookupBuiltinAsyncCombinator(
    std::string_view member_name);

std::optional<BuiltinAsyncCombinatorKind>
LookupBuiltinAsyncCombinatorByRuntimeSymbol(std::string_view symbol);

bool IsBuiltinModalStaticMemberName(const TypePath& modal_path,
                                    std::string_view member_name);

std::optional<BuiltinModalMemberSig> LookupBuiltinModalMemberSig(
    const TypePath& modal_path,
    std::string_view state,
    std::string_view member_name);

std::optional<TypeFunc> LookupBuiltinModalStaticFuncSig(
    const TypePath& modal_path,
    std::string_view member_name);

std::optional<std::string> LookupBuiltinModalRuntimeSymbol(
    const TypePath& modal_path,
    std::optional<std::string_view> state,
    std::string_view member_name);

std::optional<BuiltinModalLayoutInfo> LookupBuiltinModalLayout(
    const TypePath& modal_path);

}  // namespace ultraviolet::analysis
