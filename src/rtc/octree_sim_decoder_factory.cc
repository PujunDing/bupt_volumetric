#include "bupt_volumetric/rtc/octree_sim_decoder_factory.h"

#include <memory>
#include <vector>

#include "absl/strings/match.h"
#include "api/video_codecs/sdp_video_format.h"
#include "bupt_volumetric/rtc/octree_sim_decoder.h"

namespace bupt::volumetric {
namespace {

constexpr char kOctreeSimCodecName[] = "OCTREE-SIM";

}  // namespace

std::vector<webrtc::SdpVideoFormat>
OctreeSimDecoderFactory::GetSupportedFormats() const {
  return {webrtc::SdpVideoFormat(kOctreeSimCodecName, {})};
}

std::unique_ptr<webrtc::VideoDecoder> OctreeSimDecoderFactory::Create(
    const webrtc::Environment& /*env*/,
    const webrtc::SdpVideoFormat& format) {
  if (!absl::EqualsIgnoreCase(format.name, kOctreeSimCodecName)) {
    return nullptr;
  }
  return std::make_unique<OctreeSimDecoder>(completed_frames_,
                                            latency_samples_);
}

}  // namespace bupt::volumetric
