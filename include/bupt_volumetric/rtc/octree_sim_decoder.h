#ifndef BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_H_
#define BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "api/video/encoded_image.h"
#include "api/video_codecs/video_decoder.h"
#include "bupt_volumetric/latency/latency_recorder.h"

namespace bupt::volumetric {

// Lifecycle adapter for the OCTREE-SIM codec. Semantic payload assembly happens
// in the post-FEC observer, before whole-frame decoding, so this decoder never
// fabricates renderable frames, recovery, or completeness.
class OctreeSimDecoder final : public webrtc::VideoDecoder {
 public:
  OctreeSimDecoder() = default;
  OctreeSimDecoder(std::shared_ptr<std::atomic<std::uint64_t>> completed_frames,
                   std::shared_ptr<LatencySampleQueue> latency_samples)
      : completed_frames_(std::move(completed_frames)),
        latency_samples_(std::move(latency_samples)) {}
  explicit OctreeSimDecoder(
      std::shared_ptr<std::atomic<std::uint64_t>> completed_frames)
      : OctreeSimDecoder(std::move(completed_frames), nullptr) {}

  bool Configure(const webrtc::VideoDecoder::Settings& settings) override;
  int32_t Decode(const webrtc::EncodedImage& input_image,
                 int64_t render_time_ms) override;
  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override;
  int32_t Release() override;
  webrtc::VideoDecoder::DecoderInfo GetDecoderInfo() const override;

  [[nodiscard]] std::uint64_t completed_count() const;
  [[nodiscard]] std::uint64_t validation_failure_count() const;

 private:
  mutable std::mutex mutex_;
  webrtc::DecodedImageCallback* callback_ = nullptr;
  std::uint64_t completed_count_ = 0;
  std::uint64_t validation_failure_count_ = 0;
  const std::shared_ptr<std::atomic<std::uint64_t>> completed_frames_;
  const std::shared_ptr<LatencySampleQueue> latency_samples_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_OCTREE_SIM_DECODER_H_
