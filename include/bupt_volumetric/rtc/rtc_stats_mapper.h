#ifndef BUPT_VOLUMETRIC_RTC_RTC_STATS_MAPPER_H_
#define BUPT_VOLUMETRIC_RTC_RTC_STATS_MAPPER_H_

#include "api/stats/rtc_stats_report.h"
#include "bupt_volumetric/transport/transport_feedback.h"

namespace bupt::volumetric {

/**
 * Converts WebRTC's standards-facing stats report into the nullable transport
 * observation consumed by the application control loop.
 *
 * Missing, non-finite, negative, or otherwise invalid measurements remain
 * absent. Durations exposed by WebRTC in seconds are normalized to
 * milliseconds at this boundary.
 */
[[nodiscard]] RtcTransportObservation MapRtcStats(
    const webrtc::RTCStatsReport& report) noexcept;

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_RTC_STATS_MAPPER_H_
