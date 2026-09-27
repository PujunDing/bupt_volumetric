#ifndef BUPT_VOLUMETRIC_SIGNALING_SIGNALING_CLIENT_H_
#define BUPT_VOLUMETRIC_SIGNALING_SIGNALING_CLIENT_H_

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "bupt_volumetric/signaling/protocol.h"

namespace bupt::volumetric {

enum class SignalingClientState : std::uint8_t {
  kDisconnected = 0,
  kConnected,
  kFailed,
};

// Pure connection state machine + message producer/consumer. It owns no socket
// and no media policy; the caller injects a transport (to deliver serialized
// frames) and a message sink (to receive parsed server-routed messages).
class SignalingClient final {
 public:
  using SendFn = std::function<bool(std::span<const std::uint8_t> frame)>;
  using MessageFn = std::function<void(const SignalingMessage& message)>;

  void SetTransport(SendFn send);
  void SetOnMessage(MessageFn on_message);

  [[nodiscard]] SignalingClientState state() const noexcept;
  [[nodiscard]] const std::string& peer_id() const;
  [[nodiscard]] std::uint64_t malformed_frame_count() const noexcept;

  // Connects and registers the peer. Returns false on transport failure or an
  // empty peer id; on success the state becomes kConnected.
  bool Connect(std::string peer_id);

  bool SendOffer(std::string to, std::string sdp);
  bool SendAnswer(std::string to, std::string sdp);
  bool SendCandidate(std::string to, std::string candidate);
  bool SendBye();

  // Handles one serialized frame from the server. A parse failure marks the
  // client kFailed and increments the malformed counter.
  void OnFrame(std::span<const std::uint8_t> frame);

  // Transitions back to kDisconnected and clears the peer id.
  void Disconnect();

 private:
  bool Send(SignalingMessage message);

  SendFn send_;
  MessageFn on_message_;
  SignalingClientState state_ = SignalingClientState::kDisconnected;
  std::string peer_id_;
  std::uint64_t malformed_frame_count_ = 0;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_SIGNALING_SIGNALING_CLIENT_H_
