#include "bupt_volumetric/metrics/run_manifest.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace bupt::volumetric {
namespace {

// FNV-1a 64-bit offset basis and prime.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

constexpr char kHexDigits[] = "0123456789abcdef";

std::string Hex(std::uint64_t value) {
  std::string out;
  out.reserve(16);
  for (int shift = 60; shift >= 0; shift -= 4) {
    out.push_back(kHexDigits[(value >> shift) & 0xF]);
  }
  return out;
}

bool IsBlank(const std::string& value) noexcept {
  return value.empty();
}

}  // namespace

std::string ComputeConfigHash(std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t hash = kFnvOffsetBasis;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return Hex(hash);
}

std::optional<RunManifest> BuildRunManifest(
    std::string run_id,
    std::span<const std::uint8_t> config_bytes,
    std::uint64_t seed,
    std::string machine,
    std::string topology,
    std::string build_flags) {
  if (IsBlank(run_id) || config_bytes.empty() || IsBlank(machine) ||
      IsBlank(topology) || IsBlank(build_flags)) {
    return std::nullopt;
  }
  RunManifest manifest;
  manifest.run_id = std::move(run_id);
  manifest.config_hash = ComputeConfigHash(config_bytes);
  manifest.source_revision = kSourceRevision;
  manifest.webrtc_revision = kWebRtcRevision;
  manifest.build_flags = std::move(build_flags);
  manifest.machine = std::move(machine);
  manifest.topology = std::move(topology);
  manifest.seed = seed;
  return manifest;
}

}  // namespace bupt::volumetric
