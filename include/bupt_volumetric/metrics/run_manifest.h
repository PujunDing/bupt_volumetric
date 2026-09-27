#ifndef BUPT_VOLUMETRIC_METRICS_RUN_MANIFEST_H_
#define BUPT_VOLUMETRIC_METRICS_RUN_MANIFEST_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace bupt::volumetric {

// Pinned WebRTC commit this prototype links against (see webrtc_patches/manifest).
inline constexpr const char* kWebRtcRevision =
    "8a3d684776938a4614f892f81859136ff4855ea3";
// Prototype revision tag. The workspace is not a Git repository, so this is a
// manual tag bumped with each milestone rather than a commit hash.
inline constexpr const char* kSourceRevision = "2026-09-27-dev";

// Immutable provenance captured before a run begins. A run without a complete
// manifest is not eligible for research comparison: two runs are only
// comparable when their manifests agree on everything except run_id/seed.
struct RunManifest final {
  std::string run_id;
  std::string config_hash;  // 16 lowercase hex chars (FNV-1a 64).
  std::string source_revision;
  std::string webrtc_revision;
  std::string build_flags;
  std::string machine;   // hostname.
  std::string topology;  // e.g. "localhost-host-candidate".
  std::uint64_t seed = 0;

  bool operator==(const RunManifest&) const = default;
};

// FNV-1a 64-bit hash of the run configuration bytes, formatted as 16 lowercase
// hexadecimal characters. Deterministic across runs and platforms.
[[nodiscard]] std::string ComputeConfigHash(
    std::span<const std::uint8_t> bytes) noexcept;

// Assembles an immutable manifest from explicit inputs and fills the pinned
// source/WebRTC revisions. Returns std::nullopt when any required identity field
// (run_id, machine, topology, build_flags) is empty or config bytes are empty.
[[nodiscard]] std::optional<RunManifest> BuildRunManifest(
    std::string run_id,
    std::span<const std::uint8_t> config_bytes,
    std::uint64_t seed,
    std::string machine,
    std::string topology,
    std::string build_flags);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_METRICS_RUN_MANIFEST_H_
