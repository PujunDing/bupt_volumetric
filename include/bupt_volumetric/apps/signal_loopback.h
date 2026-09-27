#ifndef BUPT_VOLUMETRIC_APPS_SIGNAL_LOOPBACK_H_
#define BUPT_VOLUMETRIC_APPS_SIGNAL_LOOPBACK_H_

#include <memory>
#include <string>

#include "bupt_volumetric/rtc/peer_connection_session.h"
#include "bupt_volumetric/rtc/rtc_configuration.h"
#include "bupt_volumetric/signaling/tcp_transport.h"

namespace bupt::volumetric {

// Creates a PeerConnectionSession wired to the signaling transport, starts it,
// and drives the offer/answer/ICE exchange until dtls_connected (or timeout).
// Returns the started session on success, or nullptr on failure. The caller
// owns the returned session and must call Stop() before destroying it.
std::unique_ptr<PeerConnectionSession> RunSignalLoopback(
    TcpSignalingClient& transport,
    const std::string& peer_id,
    const std::string& remote_peer_id,
    MediaDirection direction,
    bool is_offerer,
    int timeout_ms,
    std::uint64_t bootstrap_bitrate_bps = 0);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_APPS_SIGNAL_LOOPBACK_H_
