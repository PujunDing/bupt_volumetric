#include "bupt_volumetric/rtc/simulated_frame_source.h"

#include <memory>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "rtc_base/time_utils.h"

namespace bupt::volumetric {

SimulatedFrameSource::SimulatedFrameSource(
    std::shared_ptr<SimulatedAccessUnitQueue> access_units)
    : webrtc::VideoTrackSource(/*remote=*/false),
      access_units_(std::move(access_units)) {}

void SimulatedFrameSource::Push(SimulatedAccessUnit unit) {
  const std::uint32_t rtp_timestamp = unit.rtp_timestamp;
  access_units_->Push(std::move(unit));
  const webrtc::VideoFrame trigger =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(webrtc::I420Buffer::Create(2, 2))
          .set_timestamp_rtp(rtp_timestamp)
          .set_timestamp_us(webrtc::TimeMicros())
          .build();
  broadcaster_.OnFrame(trigger);
}

}  // namespace bupt::volumetric
