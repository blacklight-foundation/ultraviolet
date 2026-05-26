#pragma once

#include <cstdint>

namespace ultraviolet::codegen {

constexpr std::uint64_t kAsyncFrameResumeStateOffset = 0;
constexpr std::uint64_t kAsyncFrameResumeFnOffset = 8;
constexpr std::uint64_t kAsyncFrameHostedEnvOffset = 16;
constexpr std::uint64_t kAsyncFrameKeySnapshotOffset = 24;
constexpr std::uint64_t kAsyncFrameHeaderSize = 32;
constexpr std::uint64_t kAsyncFrameHeaderAlign = 8;

}  // namespace ultraviolet::codegen
