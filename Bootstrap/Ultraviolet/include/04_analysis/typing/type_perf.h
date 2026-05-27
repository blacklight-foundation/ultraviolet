#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/process_config.h"

namespace ultraviolet::analysis {

struct TypeBodyPerfBucket {
  std::uint64_t calls = 0;
  std::uint64_t total_us = 0;
  std::uint64_t max_us = 0;

  void Record(std::uint64_t elapsed_us) {
    ++calls;
    total_us += elapsed_us;
    max_us = std::max(max_us, elapsed_us);
  }
};

enum class TypeBodyPerfPhase : std::size_t {
  PushScope,
  PopScope,
  ProjectEnv,
  BindOf,
  TypeStmtSeq,
  TypeStmt,
  TypeBlockInfo,
  TypeBlock,
  WarnResultUnreachable,
  CollectBreakFlow,
  ResType,
  TailExpr,
  ExprDispatch,
  ExprStoreType,
  PlaceDispatch,
  PlaceStoreType,
  CheckExprAgainst,
  CheckPlaceAgainst,
  PathRuntimeComptimeRef,
  PathFindModuleByPath,
  PathModuleHasComptimeProcedure,
  PathResolveValueName,
  PathValuePathType,
  ValuePathModuleNames,
  ValuePathSigmaFingerprint,
  ValuePathNameMaps,
  ValuePathDirectLookup,
  ValuePathStaticLookup,
  ValuePathFindProcedure,
  ValuePathProcType,
  ValuePathResolveQualified,
  CallOverloadResolve,
  CallOverloadCandidate,
  CallOverloadCandidateProcType,
  CallOverloadCandidateCheckExpr,
  CallOverloadCandidateTypeExpr,
  CallOverloadCandidateTypeEquiv,
  CallTypeCall,
  CallPostChecks,
  BinaryLoadOperand,
  BinaryTryCheckOperand,
  BinaryAliasEquiv,
  IfCondition,
  IfProofFacts,
  IfProofPurity,
  IfThenEnvRefine,
  IfElseCondition,
  IfElseEnvRefine,
  IfThenProofExtend,
  IfElseProofExtend,
  IfBranchType,
  IfBranchCheck,
  IfUnifyBranchTypes,
  ReturnCheckExpr,
  ReturnValidation,
  PurityCheck,
  PurityBlock,
  PurityStmt,
  PurityCallLookup,
  PurityProcedure,
  PurityRecordMethod,
  PurityClassMethod,
  PurityStateMethod,
  PurityReceiverType,
  PurityMethodLookup,
  FieldAccessBaseExpr,
  FieldAccessNormalizeBase,
  FieldAccessClassSelfLookup,
  FieldAccessRecordDeclLookup,
  FieldAccessFieldDeclLookup,
  FieldAccessFieldType,
  FieldAccessVisibility,
  FieldAccessModalLookup,
  FieldAccessPlaceAsExpr,
  FieldTypeLookupField,
  FieldTypeLower,
  FieldTypeSubstitute,
  Count
};

enum class TypeBodyPerfKind {
  Statement,
  Expression,
  PlaceExpression
};

inline constexpr std::size_t kTypeBodyPerfPhaseCount =
    static_cast<std::size_t>(TypeBodyPerfPhase::Count);

inline std::string_view TypeBodyPerfPhaseName(TypeBodyPerfPhase phase) {
  switch (phase) {
    case TypeBodyPerfPhase::PushScope:
      return "PushScope";
    case TypeBodyPerfPhase::PopScope:
      return "PopScope";
    case TypeBodyPerfPhase::ProjectEnv:
      return "ProjectEnv";
    case TypeBodyPerfPhase::BindOf:
      return "BindOf";
    case TypeBodyPerfPhase::TypeStmtSeq:
      return "TypeStmtSeq";
    case TypeBodyPerfPhase::TypeStmt:
      return "TypeStmt";
    case TypeBodyPerfPhase::TypeBlockInfo:
      return "TypeBlockInfo";
    case TypeBodyPerfPhase::TypeBlock:
      return "TypeBlock";
    case TypeBodyPerfPhase::WarnResultUnreachable:
      return "WarnResultUnreachable";
    case TypeBodyPerfPhase::CollectBreakFlow:
      return "CollectBreakFlow";
    case TypeBodyPerfPhase::ResType:
      return "ResType";
    case TypeBodyPerfPhase::TailExpr:
      return "TailExpr";
    case TypeBodyPerfPhase::ExprDispatch:
      return "ExprDispatch";
    case TypeBodyPerfPhase::ExprStoreType:
      return "ExprStoreType";
    case TypeBodyPerfPhase::PlaceDispatch:
      return "PlaceDispatch";
    case TypeBodyPerfPhase::PlaceStoreType:
      return "PlaceStoreType";
    case TypeBodyPerfPhase::CheckExprAgainst:
      return "CheckExprAgainst";
    case TypeBodyPerfPhase::CheckPlaceAgainst:
      return "CheckPlaceAgainst";
    case TypeBodyPerfPhase::PathRuntimeComptimeRef:
      return "PathRuntimeComptimeRef";
    case TypeBodyPerfPhase::PathFindModuleByPath:
      return "PathFindModuleByPath";
    case TypeBodyPerfPhase::PathModuleHasComptimeProcedure:
      return "PathModuleHasComptimeProcedure";
    case TypeBodyPerfPhase::PathResolveValueName:
      return "PathResolveValueName";
    case TypeBodyPerfPhase::PathValuePathType:
      return "PathValuePathType";
    case TypeBodyPerfPhase::ValuePathModuleNames:
      return "ValuePathModuleNames";
    case TypeBodyPerfPhase::ValuePathSigmaFingerprint:
      return "ValuePathSigmaFingerprint";
    case TypeBodyPerfPhase::ValuePathNameMaps:
      return "ValuePathNameMaps";
    case TypeBodyPerfPhase::ValuePathDirectLookup:
      return "ValuePathDirectLookup";
    case TypeBodyPerfPhase::ValuePathStaticLookup:
      return "ValuePathStaticLookup";
    case TypeBodyPerfPhase::ValuePathFindProcedure:
      return "ValuePathFindProcedure";
    case TypeBodyPerfPhase::ValuePathProcType:
      return "ValuePathProcType";
    case TypeBodyPerfPhase::ValuePathResolveQualified:
      return "ValuePathResolveQualified";
    case TypeBodyPerfPhase::CallOverloadResolve:
      return "CallOverloadResolve";
    case TypeBodyPerfPhase::CallOverloadCandidate:
      return "CallOverloadCandidate";
    case TypeBodyPerfPhase::CallOverloadCandidateProcType:
      return "CallOverloadCandidateProcType";
    case TypeBodyPerfPhase::CallOverloadCandidateCheckExpr:
      return "CallOverloadCandidateCheckExpr";
    case TypeBodyPerfPhase::CallOverloadCandidateTypeExpr:
      return "CallOverloadCandidateTypeExpr";
    case TypeBodyPerfPhase::CallOverloadCandidateTypeEquiv:
      return "CallOverloadCandidateTypeEquiv";
    case TypeBodyPerfPhase::CallTypeCall:
      return "CallTypeCall";
    case TypeBodyPerfPhase::CallPostChecks:
      return "CallPostChecks";
    case TypeBodyPerfPhase::BinaryLoadOperand:
      return "BinaryLoadOperand";
    case TypeBodyPerfPhase::BinaryTryCheckOperand:
      return "BinaryTryCheckOperand";
    case TypeBodyPerfPhase::BinaryAliasEquiv:
      return "BinaryAliasEquiv";
    case TypeBodyPerfPhase::IfCondition:
      return "IfCondition";
    case TypeBodyPerfPhase::IfProofFacts:
      return "IfProofFacts";
    case TypeBodyPerfPhase::IfProofPurity:
      return "IfProofPurity";
    case TypeBodyPerfPhase::IfThenEnvRefine:
      return "IfThenEnvRefine";
    case TypeBodyPerfPhase::IfElseCondition:
      return "IfElseCondition";
    case TypeBodyPerfPhase::IfElseEnvRefine:
      return "IfElseEnvRefine";
    case TypeBodyPerfPhase::IfThenProofExtend:
      return "IfThenProofExtend";
    case TypeBodyPerfPhase::IfElseProofExtend:
      return "IfElseProofExtend";
    case TypeBodyPerfPhase::IfBranchType:
      return "IfBranchType";
    case TypeBodyPerfPhase::IfBranchCheck:
      return "IfBranchCheck";
    case TypeBodyPerfPhase::IfUnifyBranchTypes:
      return "IfUnifyBranchTypes";
    case TypeBodyPerfPhase::ReturnCheckExpr:
      return "ReturnCheckExpr";
    case TypeBodyPerfPhase::ReturnValidation:
      return "ReturnValidation";
    case TypeBodyPerfPhase::PurityCheck:
      return "PurityCheck";
    case TypeBodyPerfPhase::PurityBlock:
      return "PurityBlock";
    case TypeBodyPerfPhase::PurityStmt:
      return "PurityStmt";
    case TypeBodyPerfPhase::PurityCallLookup:
      return "PurityCallLookup";
    case TypeBodyPerfPhase::PurityProcedure:
      return "PurityProcedure";
    case TypeBodyPerfPhase::PurityRecordMethod:
      return "PurityRecordMethod";
    case TypeBodyPerfPhase::PurityClassMethod:
      return "PurityClassMethod";
    case TypeBodyPerfPhase::PurityStateMethod:
      return "PurityStateMethod";
    case TypeBodyPerfPhase::PurityReceiverType:
      return "PurityReceiverType";
    case TypeBodyPerfPhase::PurityMethodLookup:
      return "PurityMethodLookup";
    case TypeBodyPerfPhase::FieldAccessBaseExpr:
      return "FieldAccessBaseExpr";
    case TypeBodyPerfPhase::FieldAccessNormalizeBase:
      return "FieldAccessNormalizeBase";
    case TypeBodyPerfPhase::FieldAccessClassSelfLookup:
      return "FieldAccessClassSelfLookup";
    case TypeBodyPerfPhase::FieldAccessRecordDeclLookup:
      return "FieldAccessRecordDeclLookup";
    case TypeBodyPerfPhase::FieldAccessFieldDeclLookup:
      return "FieldAccessFieldDeclLookup";
    case TypeBodyPerfPhase::FieldAccessFieldType:
      return "FieldAccessFieldType";
    case TypeBodyPerfPhase::FieldAccessVisibility:
      return "FieldAccessVisibility";
    case TypeBodyPerfPhase::FieldAccessModalLookup:
      return "FieldAccessModalLookup";
    case TypeBodyPerfPhase::FieldAccessPlaceAsExpr:
      return "FieldAccessPlaceAsExpr";
    case TypeBodyPerfPhase::FieldTypeLookupField:
      return "FieldTypeLookupField";
    case TypeBodyPerfPhase::FieldTypeLower:
      return "FieldTypeLower";
    case TypeBodyPerfPhase::FieldTypeSubstitute:
      return "FieldTypeSubstitute";
    case TypeBodyPerfPhase::Count:
      break;
  }
  return "Unknown";
}

struct TypeBodyPerfStats {
  std::array<TypeBodyPerfBucket, kTypeBodyPerfPhaseCount> phases{};
  std::unordered_map<std::string_view, TypeBodyPerfBucket> statement_kinds;
  std::unordered_map<std::string_view, TypeBodyPerfBucket> expression_kinds;
  std::unordered_map<std::string_view, TypeBodyPerfBucket> place_expression_kinds;
};

inline TypeBodyPerfStats g_type_body_perf_stats;

inline bool TypeBodyPerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

inline bool TypeBodyPerfActive() {
  static const bool active = TypeBodyPerfEnabled();
  return active;
}

inline std::uint64_t TypeBodyPerfElapsedUs(
    std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

class ScopedTypeBodyPerfPhase {
 public:
  explicit ScopedTypeBodyPerfPhase(TypeBodyPerfPhase phase) {
    if (!TypeBodyPerfActive()) {
      return;
    }
    slot_ = &g_type_body_perf_stats.phases[static_cast<std::size_t>(phase)];
    start_ = std::chrono::steady_clock::now();
  }

  ~ScopedTypeBodyPerfPhase() {
    if (slot_) {
      slot_->Record(TypeBodyPerfElapsedUs(start_));
    }
  }

 private:
  TypeBodyPerfBucket* slot_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
};

class ScopedTypeBodyPerfKind {
 public:
  ScopedTypeBodyPerfKind(TypeBodyPerfKind kind, std::string_view name) {
    if (!TypeBodyPerfActive()) {
      return;
    }
    slot_ = &BucketMap(kind)[name];
    start_ = std::chrono::steady_clock::now();
  }

  ~ScopedTypeBodyPerfKind() {
    if (slot_) {
      slot_->Record(TypeBodyPerfElapsedUs(start_));
    }
  }

 private:
  static std::unordered_map<std::string_view, TypeBodyPerfBucket>& BucketMap(
      TypeBodyPerfKind kind) {
    switch (kind) {
      case TypeBodyPerfKind::Statement:
        return g_type_body_perf_stats.statement_kinds;
      case TypeBodyPerfKind::Expression:
        return g_type_body_perf_stats.expression_kinds;
      case TypeBodyPerfKind::PlaceExpression:
        return g_type_body_perf_stats.place_expression_kinds;
    }
    return g_type_body_perf_stats.expression_kinds;
  }

  TypeBodyPerfBucket* slot_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
};

inline void PrintTypeBodyPerfKindRows(
    std::string_view label,
    const std::unordered_map<std::string_view, TypeBodyPerfBucket>& buckets) {
  std::vector<std::pair<std::string_view, TypeBodyPerfBucket>> rows;
  rows.reserve(buckets.size());
  for (const auto& [name, bucket] : buckets) {
    if (bucket.calls > 0) {
      rows.emplace_back(name, bucket);
    }
  }
  std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second.total_us > rhs.second.total_us;
  });

  const std::size_t limit = std::min<std::size_t>(rows.size(), 30);
  for (std::size_t i = 0; i < limit; ++i) {
    const auto& [name, bucket] = rows[i];
    std::fprintf(stderr,
                 "[uv] sema perf=%.*s rank=%zu name=%.*s calls=%llu "
                 "total_us=%llu max_us=%llu\n",
                 static_cast<int>(label.size()), label.data(), i + 1,
                 static_cast<int>(name.size()), name.data(),
                 static_cast<unsigned long long>(bucket.calls),
                 static_cast<unsigned long long>(bucket.total_us),
                 static_cast<unsigned long long>(bucket.max_us));
  }
}

inline void LogTypeBodyPerfSummary() {
  if (!TypeBodyPerfActive()) {
    return;
  }

  for (std::size_t i = 0; i < kTypeBodyPerfPhaseCount; ++i) {
    const auto& bucket = g_type_body_perf_stats.phases[i];
    if (bucket.calls == 0) {
      continue;
    }
    const auto phase = static_cast<TypeBodyPerfPhase>(i);
    const auto name = TypeBodyPerfPhaseName(phase);
    std::fprintf(stderr,
                 "[uv] sema perf=body-phase name=%.*s calls=%llu "
                 "total_us=%llu max_us=%llu\n",
                 static_cast<int>(name.size()), name.data(),
                 static_cast<unsigned long long>(bucket.calls),
                 static_cast<unsigned long long>(bucket.total_us),
                 static_cast<unsigned long long>(bucket.max_us));
  }

  PrintTypeBodyPerfKindRows("body-stmt",
                            g_type_body_perf_stats.statement_kinds);
  PrintTypeBodyPerfKindRows("body-expr",
                            g_type_body_perf_stats.expression_kinds);
  PrintTypeBodyPerfKindRows("body-place",
                            g_type_body_perf_stats.place_expression_kinds);
  std::fflush(stderr);
}

}  // namespace ultraviolet::analysis
