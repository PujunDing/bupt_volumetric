#ifndef BUPT_VOLUMETRIC_RTC_PEER_CONNECTION_SESSION_H_
#define BUPT_VOLUMETRIC_RTC_PEER_CONNECTION_SESSION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bupt_volumetric/model/simulated_octree_frame.h"
#include "bupt_volumetric/rtc/rtc_configuration.h"
#include "bupt_volumetric/transport/transport_feedback.h"

namespace bupt::volumetric {

// Externally observable lifecycle states of one peer connection session.
enum class PeerSessionState : std::uint8_t {
  kNew,
  kStarting,
  kConnecting,
  kConnected,
  kStopped,
  kFailed,
};

// Lightweight outcome for immediate (non-async) session operations.
struct SessionStatus {
  bool ok = true;
  std::string message;

  static SessionStatus Ok() { return SessionStatus{}; }
  static SessionStatus Error(std::string message) {
    SessionStatus status;
    status.ok = false;
    status.message = std::move(message);
    return status;
  }
};

// Delivers a locally created offer/answer SDP to signaling.
using LocalDescriptionCallback =
    std::function<void(std::string type, std::string sdp)>;
// Delivers one serialized local ICE candidate to signaling, together with the
// mid and m-line index that identify its m= section on the peer.
using LocalIceCandidateCallback =
    std::function<void(std::string mid, int mline, std::string candidate)>;
// Reports each committed lifecycle transition in order.
using SessionStateCallback = std::function<void(PeerSessionState state)>;
// Reports asynchronous WebRTC failures that cannot be returned by the method
// which initiated the operation.
using SessionErrorCallback = std::function<void(std::string message)>;

// Callbacks retained by the session for its lifetime.
struct PeerConnectionCallbacks {
  LocalDescriptionCallback on_local_description;
  LocalIceCandidateCallback on_local_ice_candidate;
  SessionStateCallback on_state;
  SessionErrorCallback on_error;
};

// High-level owner of one PeerConnection: threads, factory, offer/answer, ICE,
// and ordered teardown. Methods report immediate validation failures through
// SessionStatus; later asynchronous outcomes arrive through `on_state`.
class PeerConnectionSession {
 public:
  virtual ~PeerConnectionSession() = default;

  virtual SessionStatus Start(const RtcSessionConfig& config) = 0;
  virtual SessionStatus CreateOffer() = 0;
  virtual SessionStatus CreateAnswer() = 0;
  virtual SessionStatus ApplyRemoteDescription(std::string type,
                                               std::string sdp) = 0;
  virtual SessionStatus AddRemoteIceCandidate(std::string mid,
                                              int mline,
                                              std::string candidate) = 0;
  virtual SessionStatus SubmitSimulatedFrame(
      SimulatedOctreeFrame frame, std::uint32_t rtp_timestamp) = 0;

  // Requests one asynchronous RTCStats collection; the report is mapped and
  // merged into the transport feedback store without blocking a WebRTC thread.
  // Applications/tests choose their own sampling cadence.
  virtual SessionStatus CollectTransportStats() = 0;

  // Idempotent ordered shutdown; all WebRTC threads join before it returns.
  virtual SessionStatus Stop() noexcept = 0;

  // Latest merged rate + observation snapshot, independent of WebRTC threads.
  [[nodiscard]] virtual TransportFeedbackSnapshot transport_feedback()
      const = 0;

  [[nodiscard]] virtual PeerSessionState state() const noexcept = 0;

  // True once the standardized connection state reports kConnected, i.e. ICE
  // and the DTLS handshake have both completed.
  [[nodiscard]] virtual bool dtls_connected() const noexcept = 0;

  // Number of access units the local OCTREE-SIM decoder has completed. Zero on
  // a send-only session.
  [[nodiscard]] virtual std::uint64_t completed_frame_count() const noexcept = 0;

  // Number of semantic blocks the post-FEC observer adapter has assembled to
  // completion (byte-exact). Zero on a send-only session and before the first
  // block completes on a receive-only session.
  [[nodiscard]] virtual std::uint64_t completed_block_count() const noexcept = 0;

  // Drains and returns the queued per-block end-to-end latencies (microseconds,
  // capture -> block decoded). Empty on a send-only session. Each completed
  // block contributes exactly one sample.
  [[nodiscard]] virtual std::vector<std::uint64_t> DrainLatencyUs() = 0;
};

// Creates an unstarted session; no WebRTC thread/device/socket is created yet.
std::unique_ptr<PeerConnectionSession> CreatePeerConnectionSession(
    PeerConnectionCallbacks callbacks);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_PEER_CONNECTION_SESSION_H_
