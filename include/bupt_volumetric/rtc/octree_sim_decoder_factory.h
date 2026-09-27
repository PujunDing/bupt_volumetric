#ifndef BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_FACTORY_H_
#define BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_FACTORY_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "api/environment/environment.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "bupt_volumetric/latency/latency_recorder.h"

namespace bupt::volumetric {

// Decoder factory for the negotiated OCTREE-SIM/90000 format. Creates lifecycle
// adapters only; semantic assembly happens in the post-FEC observer. An
// optional shared counter aggregates every decoder's completed-frame count so a
// receive-only session can expose it.
class OctreeSimDecoderFactory final : public webrtc::VideoDecoderFactory {
 public:
  OctreeSimDecoderFactory() = default;
  OctreeSimDecoderFactory(std::shared_ptr<std::atomic<std::uint64_t>> completed_frames,
                          std::shared_ptr<LatencySampleQueue> latency_samples)
      : completed_frames_(std::move(completed_frames)),
        latency_samples_(std::move(latency_samples)) {}
  explicit OctreeSimDecoderFactory(
      std::shared_ptr<std::atomic<std::uint64_t>> completed_frames)
      : OctreeSimDecoderFactory(std::move(completed_frames), nullptr) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

 private:
  const std::shared_ptr<std::atomic<std::uint64_t>> completed_frames_;
  const std::shared_ptr<LatencySampleQueue> latency_samples_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_FACTORY_H_
