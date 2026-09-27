#include "bupt_volumetric/rtc/rtc_configuration.h"

#include <string>
#include <vector>

#include "api/peer_connection_interface.h"
#include "absl/strings/match.h"

namespace bupt::volumetric {

std::string ValidateRtcSessionConfig(const RtcSessionConfig& config) {
  if (config.max_framerate_fps < 0.0) {
    return "max_framerate_fps must be >= 0";
  }
  for (const std::string& server : config.ice_servers) {
    if (server.empty()) {
      return "ICE server URL must not be empty";
    }
  }
  return "";
}

webrtc::PeerConnectionInterface::RTCConfiguration BuildRtcConfiguration(
    const RtcSessionConfig& config) {
  webrtc::PeerConnectionInterface::RTCConfiguration rtc;
  rtc.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  rtc.bundle_policy =
      webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
  rtc.rtcp_mux_policy =
      webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;

  rtc.servers.reserve(config.ice_servers.size());
  for (const std::string& url : config.ice_servers) {
    webrtc::PeerConnectionInterface::IceServer server;
    if (absl::StartsWithIgnoreCase(url, "stun:")) {
      server.uri = url;
      server.urls.push_back(url);
    } else {
      // Non-stun (turn) URLs are passed through verbatim; the prototype does
      // not add default credentials.
      server.uri = url;
      server.urls.push_back(url);
    }
    rtc.servers.push_back(std::move(server));
  }
  return rtc;
}

}  // namespace bupt::volumetric
