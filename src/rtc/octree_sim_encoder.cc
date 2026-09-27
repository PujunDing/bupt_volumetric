#include "bupt_volumetric/rtc/octree_sim_encoder.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/video_encoder.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "bupt_volumetric/transport/transport_feedback.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

namespace bupt::volumetric {

int OctreeSimEncoder::InitEncode(
    const webrtc::VideoCodec* /*codec_settings*/,
    const webrtc::VideoEncoder::Settings& /*settings*/) {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = true;
  return 0;  // WEBRTC_VIDEO_CODEC_OK
}

int32_t OctreeSimEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = callback;
  return 0;  // WEBRTC_VIDEO_CODEC_OK
}

int32_t OctreeSimEncoder::Release() {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = nullptr;
  initialized_ = false;
  return 0;  // WEBRTC_VIDEO_CODEC_OK
}

int32_t OctreeSimEncoder::Encode(
    const webrtc::VideoFrame& frame,
    const std::vector<webrtc::VideoFrameType>* /*frame_types*/) {
  webrtc::EncodedImageCallback* callback = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || callback_ == nullptr) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    callback = callback_;
  }
  if (!access_units_) {
    return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
  }
  const std::optional<SimulatedAccessUnit> unit = access_units_->PopOldest();
  if (!unit.has_value()) {
    return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
  }
  const std::optional<std::vector<std::uint8_t>> wire =
      SerializeSimulatedAccessUnit(*unit);
  if (!wire.has_value()) {
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  webrtc::EncodedImage image;
  image.SetEncodedData(
      webrtc::EncodedImageBuffer::Create(wire->data(), wire->size()));
  image.SetRtpTimestamp(frame.rtp_timestamp());
  image._frameType = webrtc::VideoFrameType::kVideoFrameKey;
  image._encodedWidth = 2;
  image._encodedHeight = 2;
  image.SetSimulcastIndex(0);
  image.SetSpatialIndex(0);
  image.SetTemporalIndex(0);
  image.set_end_of_temporal_unit(true);

  webrtc::CodecSpecificInfo codec_specific_info;
  codec_specific_info.codecType = webrtc::kVideoCodecGeneric;
  codec_specific_info.fec_rate = unit->fec_rate;
  const webrtc::EncodedImageCallback::Result result =
      callback->OnEncodedImage(image, &codec_specific_info);
  return result.error == webrtc::EncodedImageCallback::Result::OK
             ? WEBRTC_VIDEO_CODEC_OK
             : WEBRTC_VIDEO_CODEC_ERROR;
}

void OctreeSimEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& parameters) {
  const std::uint32_t bitrate = parameters.target_bitrate.get_sum_bps();
  const double framerate = parameters.framerate_fps;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    target_bitrate_bps_ = bitrate;
    framerate_fps_ = framerate;
  }
  if (rate_sink_) {
    rate_sink_->OnRateUpdate(EncoderRateUpdate{
        .target_bitrate_bps = bitrate,
        .target_framerate_fps = framerate,
        .observed_at_ms = static_cast<std::uint64_t>(webrtc::TimeMillis()),
    });
  }
}

webrtc::VideoEncoder::EncoderInfo OctreeSimEncoder::GetEncoderInfo() const {
  webrtc::VideoEncoder::EncoderInfo info;
  info.implementation_name = "OCTREE-SIM";
  info.requested_resolution_alignment = 1;
  return info;
}

std::uint32_t OctreeSimEncoder::target_bitrate_bps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_bitrate_bps_;
}

double OctreeSimEncoder::framerate_fps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return framerate_fps_;
}

}  // namespace bupt::volumetric
