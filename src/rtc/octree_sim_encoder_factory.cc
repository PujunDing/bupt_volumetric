#include "bupt_volumetric/rtc/octree_sim_encoder_factory.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "api/video_codecs/sdp_video_format.h"
#include "bupt_volumetric/rtc/octree_sim_encoder.h"

namespace bupt::volumetric {
namespace {

constexpr char kOctreeSimCodecName[] = "OCTREE-SIM";

}  // namespace

std::vector<webrtc::SdpVideoFormat>
OctreeSimEncoderFactory::GetSupportedFormats() const {
  return {webrtc::SdpVideoFormat(kOctreeSimCodecName, {})};
}

std::unique_ptr<webrtc::VideoEncoder> OctreeSimEncoderFactory::Create(
    const webrtc::Environment& /*env*/,
    const webrtc::SdpVideoFormat& format) {
  if (!absl::EqualsIgnoreCase(format.name, kOctreeSimCodecName)) {
    return nullptr;
  }
  return std::make_unique<OctreeSimEncoder>(access_units_, rate_sink_);
}

webrtc::VideoEncoderFactory::CodecSupport
OctreeSimEncoderFactory::QueryCodecSupport(
    const webrtc::SdpVideoFormat& format,
    std::optional<std::string> /*scalability_mode*/,
    std::optional<webrtc::Resolution> /*resolution*/) const {
  return {.is_supported =
              absl::EqualsIgnoreCase(format.name, kOctreeSimCodecName),
          .is_power_efficient = false};
}

}  // namespace bupt::volumetric
