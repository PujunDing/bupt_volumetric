#include "bupt_volumetric/rtc/octree_sim_decoder.h"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>

#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_decoder.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/time_utils.h"

namespace bupt::volumetric {

bool OctreeSimDecoder::Configure(
    const webrtc::VideoDecoder::Settings& /*settings*/) {
  return true;
}

int32_t OctreeSimDecoder::Decode(const webrtc::EncodedImage& input_image,
                                 int64_t /*render_time_ms*/) {
  const SimulatedAccessUnitParseResult parsed = ParseSimulatedAccessUnit(
      std::span<const std::uint8_t>(input_image.data(), input_image.size()));
  webrtc::DecodedImageCallback* callback = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parsed.ok()) {
      ++validation_failure_count_;
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (callback_ == nullptr) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    ++completed_count_;
    callback = callback_;
  }
  if (completed_frames_) {
    completed_frames_->fetch_add(1, std::memory_order_relaxed);
  }
  if (latency_samples_ && parsed.unit->capture_time_us != 0) {
    const std::uint64_t now_us = webrtc::TimeMicros();
    if (now_us >= parsed.unit->capture_time_us) {
      latency_samples_->Push(now_us - parsed.unit->capture_time_us);
    }
  }

  webrtc::VideoFrame completion =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(webrtc::I420Buffer::Create(2, 2))
          .set_timestamp_rtp(input_image.RtpTimestamp())
          .build();
  return callback->Decoded(completion);
}

int32_t OctreeSimDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t OctreeSimDecoder::Release() {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = nullptr;
  return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoDecoder::DecoderInfo OctreeSimDecoder::GetDecoderInfo() const {
  webrtc::VideoDecoder::DecoderInfo info;
  info.implementation_name = "OCTREE-SIM";
  info.is_hardware_accelerated = false;
  return info;
}

std::uint64_t OctreeSimDecoder::completed_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return completed_count_;
}

std::uint64_t OctreeSimDecoder::validation_failure_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return validation_failure_count_;
}

}  // namespace bupt::volumetric
