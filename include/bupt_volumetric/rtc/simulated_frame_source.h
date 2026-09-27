#ifndef BUPT_VOLUMETRIC_RTC_SIMULATED_FRAME_SOURCE_H_
#define BUPT_VOLUMETRIC_RTC_SIMULATED_FRAME_SOURCE_H_

#include <memory>

#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "media/base/video_broadcaster.h"
#include "pc/video_track_source.h"

namespace bupt::volumetric {

/** Push-only 2x2 video source used solely to schedule simulated access units. */
class SimulatedFrameSource : public webrtc::VideoTrackSource {
 public:
  explicit SimulatedFrameSource(
      std::shared_ptr<SimulatedAccessUnitQueue> access_units);

  /** Queues semantic bytes before broadcasting their matching trigger frame. */
  void Push(SimulatedAccessUnit unit);

 private:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return &broadcaster_;
  }

  const std::shared_ptr<SimulatedAccessUnitQueue> access_units_;
  webrtc::VideoBroadcaster broadcaster_;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_SIMULATED_FRAME_SOURCE_H_
