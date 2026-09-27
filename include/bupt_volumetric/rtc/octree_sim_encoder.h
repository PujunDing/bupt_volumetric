#ifndef BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_H_
#define BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/video_encoder.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "bupt_volumetric/transport/transport_feedback.h"

namespace bupt::volumetric {

// Passthrough OCTREE-SIM encoder. It performs no real point-cloud compression
// and never reports synthetic work as codec time; the input VideoFrame only
// serves as WebRTC's scheduling trigger. SetRates is thread-safe and forwards
// every rate change to the optional RateUpdateSink.
class OctreeSimEncoder final : public webrtc::VideoEncoder {
 public:
  explicit OctreeSimEncoder(std::shared_ptr<RateUpdateSink> rate_sink = nullptr)
      : rate_sink_(std::move(rate_sink)) {}
  OctreeSimEncoder(std::shared_ptr<SimulatedAccessUnitQueue> access_units,
                   std::shared_ptr<RateUpdateSink> rate_sink)
      : access_units_(std::move(access_units)),
        rate_sink_(std::move(rate_sink)) {}

  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override;
  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override;
  int32_t Release() override;
  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types) override;
  void SetRates(
      const webrtc::VideoEncoder::RateControlParameters& parameters) override;
  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

  // Snapshot of the last SetRates values, for tests and diagnostics.
  std::uint32_t target_bitrate_bps() const;
  double framerate_fps() const;

 private:
  const std::shared_ptr<SimulatedAccessUnitQueue> access_units_;
  const std::shared_ptr<RateUpdateSink> rate_sink_;
  webrtc::EncodedImageCallback* callback_ = nullptr;

  mutable std::mutex mutex_;
  std::uint32_t target_bitrate_bps_ = 0;
  double framerate_fps_ = 0.0;
  bool initialized_ = false;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_OCTREE_SIM_ENCODER_H_
