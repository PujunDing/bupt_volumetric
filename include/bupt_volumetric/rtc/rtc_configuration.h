#ifndef BUPT_VOLUMETRIC_RTC_RTC_CONFIGURATION_H_
#define BUPT_VOLUMETRIC_RTC_RTC_CONFIGURATION_H_

#include <cstdint>
#include <string>
#include <vector>

#include "api/peer_connection_interface.h"

namespace bupt::volumetric {

enum class MediaDirection : std::uint8_t { kSendOnly, kReceiveOnly };

// Application-level session policy. An empty `ice_servers` list means localhost
// host candidates only (no STUN/TURN). The bitrate/framerate fields never pin
// min=max; zero leaves WebRTC's rate controller in charge.
struct RtcSessionConfig {
  MediaDirection direction = MediaDirection::kSendOnly;
  std::vector<std::string> ice_servers;
  std::uint32_t target_bitrate_bps = 0;
  double max_framerate_fps = 0.0;
  // Used only before GoogCC produces its first estimate; zero falls back to a
  // 1 Mbps internal default so the first frames can flow and kick-start BWE.
  std::uint64_t bootstrap_bitrate_bps = 0;
};

// Returns an empty string on success, otherwise a human-readable validation
// error. Rejects an empty ICE URL or a negative framerate.
std::string ValidateRtcSessionConfig(const RtcSessionConfig& config);

// Builds a WebRTC RTCConfiguration with the prototype defaults — unified-plan,
// bundle (max-bundle), and required RTCP mux — plus the configured ICE servers.
webrtc::PeerConnectionInterface::RTCConfiguration BuildRtcConfiguration(
    const RtcSessionConfig& config);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_RTC_CONFIGURATION_H_
