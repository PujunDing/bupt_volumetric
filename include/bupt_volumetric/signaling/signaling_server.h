#ifndef BUPT_VOLUMETRIC_SIGNALING_SIGNALING_SERVER_H_
#define BUPT_VOLUMETRIC_SIGNALING_SIGNALING_SERVER_H_

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bupt_volumetric/signaling/protocol.h"

namespace bupt::volumetric {

enum class SignalingServerStatus : std::uint8_t {
  kOk = 0,
  kDuplicatePeer,
  kUnknownPeer,
  kNoTargetPeer,
  kShuttingDown,
};

struct SignalingServerResult final {
  SignalingServerStatus status = SignalingServerStatus::kOk;
  std::string message;

  bool ok() const noexcept { return status == SignalingServerStatus::kOk; }
};

// In-memory bounded router between two named peers. No socket or media data
// passes through it; it only relays registration, SDP, ICE candidates, and
// errors. State is observable so disconnect/duplicate/shutdown are testable.
class SignalingServer final {
 public:
  SignalingServer() = default;

  // Registers a peer id. Fails with kDuplicatePeer if the id is already
  // connected (callers must disconnect before re-registering).
  SignalingServerResult Register(const std::string& peer_id);

  // Routes one peer-to-peer message into the target peer's FIFO queue.
  SignalingServerResult Route(const SignalingMessage& message);

  // Drains and removes every pending message for a peer, oldest first.
  std::vector<SignalingMessage> Poll(const std::string& peer_id);

  // Removes a peer and discards its queue.
  SignalingServerResult Disconnect(const std::string& peer_id);

  // Removes every peer and queue; further Register/Route fail with kShuttingDown.
  void Shutdown();

  [[nodiscard]] std::size_t connected_peer_count() const noexcept;
  [[nodiscard]] bool IsConnected(const std::string& peer_id) const;
  [[nodiscard]] std::uint64_t routed_message_count() const noexcept;

 private:
  std::map<std::string, std::deque<SignalingMessage>> queues_;
  std::set<std::string> connected_;
  std::uint64_t routed_message_count_ = 0;
  bool shutting_down_ = false;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_SIGNALING_SIGNALING_SERVER_H_
