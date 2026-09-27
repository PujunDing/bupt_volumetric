#ifndef BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_FACTORY_H_
#define BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_FACTORY_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/environment/environment.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder_factory.h"

namespace bupt::volumetric {

class RateUpdateSink;
class SimulatedAccessUnitQueue;

// Encoder factory that advertises exactly one codec — the negotiated
// OCTREE-SIM/90000 format — and drives it through WebRTC's kVideoCodecGeneric
// path (the fallback chosen by PayloadStringToCodecType for an unknown SDP
// name). It performs no real compression; it only creates passthrough encoders.
class OctreeSimEncoderFactory final : public webrtc::VideoEncoderFactory {
 public:
  OctreeSimEncoderFactory() = default;
  explicit OctreeSimEncoderFactory(std::shared_ptr<RateUpdateSink> rate_sink)
      : rate_sink_(std::move(rate_sink)) {}
  OctreeSimEncoderFactory(
      std::shared_ptr<SimulatedAccessUnitQueue> access_units,
      std::shared_ptr<RateUpdateSink> rate_sink)
      : access_units_(std::move(access_units)),
        rate_sink_(std::move(rate_sink)) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

  // Rejects anything other than OCTREE-SIM; the prototype never silently
  // accepts an unknown codec.
  webrtc::VideoEncoderFactory::CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat& format,
      std::optional<std::string> scalability_mode,
      std::optional<webrtc::Resolution> resolution) const override;

 private:
  const std::shared_ptr<SimulatedAccessUnitQueue> access_units_;
  const std::shared_ptr<RateUpdateSink> rate_sink_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_FACTORY_H_
