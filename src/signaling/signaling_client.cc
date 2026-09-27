#include "bupt_volumetric/signaling/signaling_client.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bupt::volumetric {

void SignalingClient::SetTransport(SendFn send) { send_ = std::move(send); }

void SignalingClient::SetOnMessage(MessageFn on_message) {
  on_message_ = std::move(on_message);
}

SignalingClientState SignalingClient::state() const noexcept { return state_; }

const std::string& SignalingClient::peer_id() const { return peer_id_; }

std::uint64_t SignalingClient::malformed_frame_count() const noexcept {
  return malformed_frame_count_;
}

bool SignalingClient::Connect(std::string peer_id) {
  if (peer_id.empty()) {
    state_ = SignalingClientState::kFailed;
    return false;
  }
  peer_id_ = std::move(peer_id);
  state_ = SignalingClientState::kConnected;
  const SignalingMessage register_message{
      .type = SignalingMessageType::kRegister,
      .from_peer_id = peer_id_,
      .to_peer_id = "",
      .payload = "",
  };
  if (!Send(register_message)) {
    return false;
  }
  return true;
}

bool SignalingClient::SendOffer(std::string to, std::string sdp) {
  return Send(SignalingMessage{.type = SignalingMessageType::kOffer,
                               .from_peer_id = peer_id_,
                               .to_peer_id = std::move(to),
                               .payload = std::move(sdp)});
}

bool SignalingClient::SendAnswer(std::string to, std::string sdp) {
  return Send(SignalingMessage{.type = SignalingMessageType::kAnswer,
                               .from_peer_id = peer_id_,
                               .to_peer_id = std::move(to),
                               .payload = std::move(sdp)});
}

bool SignalingClient::SendCandidate(std::string to, std::string candidate) {
  return Send(SignalingMessage{.type = SignalingMessageType::kIceCandidate,
                               .from_peer_id = peer_id_,
                               .to_peer_id = std::move(to),
                               .payload = std::move(candidate)});
}

bool SignalingClient::SendBye() {
  return Send(SignalingMessage{.type = SignalingMessageType::kBye,
                               .from_peer_id = peer_id_,
                               .to_peer_id = "",
                               .payload = ""});
}

void SignalingClient::OnFrame(std::span<const std::uint8_t> frame) {
  const std::optional<SignalingMessage> message = ParseSignalingMessage(frame);
  if (!message.has_value()) {
    ++malformed_frame_count_;
    state_ = SignalingClientState::kFailed;
    return;
  }
  if (on_message_) {
    on_message_(*message);
  }
}

void SignalingClient::Disconnect() {
  state_ = SignalingClientState::kDisconnected;
  peer_id_.clear();
}

bool SignalingClient::Send(SignalingMessage message) {
  if (state_ != SignalingClientState::kConnected) {
    state_ = SignalingClientState::kFailed;
    return false;
  }
  const std::optional<std::vector<std::uint8_t>> frame =
      SerializeSignalingMessage(message);
  if (!frame.has_value()) {
    state_ = SignalingClientState::kFailed;
    return false;
  }
  if (!send_ || !send_(*frame)) {
    state_ = SignalingClientState::kFailed;
    return false;
  }
  return true;
}

}  // namespace bupt::volumetric
