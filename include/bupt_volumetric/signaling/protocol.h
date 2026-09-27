#ifndef BUPT_VOLUMETRIC_SIGNALING_PROTOCOL_H_
#define BUPT_VOLUMETRIC_SIGNALING_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bupt::volumetric {

inline constexpr std::size_t kMaxPeerIdBytes = 64;
inline constexpr std::size_t kMaxSignalingPayloadBytes = 64 * 1024;

// Versioned signaling message types. Only session registration, SDP offer/
// answer, ICE candidates, and errors may travel on this channel.
enum class SignalingMessageType : std::uint8_t {
  kRegister = 1,
  kOffer = 2,
  kAnswer = 3,
  kIceCandidate = 4,
  kError = 5,
  kBye = 6,
};

// One validated signaling message. `to_peer_id` is empty only for messages
// addressed to the signaling server itself (register/bye).
struct SignalingMessage final {
  SignalingMessageType type = SignalingMessageType::kRegister;
  std::string from_peer_id;
  std::string to_peer_id;
  std::string payload;
};

// Stable reasons a signaling frame was rejected.
enum class SignalingDecodeError : std::uint8_t {
  kNone = 0,
  kTruncatedHeader,
  kBadMagic,
  kUnsupportedVersion,
  kUnknownMessageType,
  kPeerIdTooLong,
  kPayloadTooLong,
  kEmptyPeerId,
  kPayloadLengthMismatch,
};

// Serializes one message into the bounded version-1 binary envelope (network
// byte order). Returns std::nullopt on any protocol violation; media payloads
// must never be serialized here.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> SerializeSignalingMessage(
    const SignalingMessage& message) noexcept;

// Parses and validates one complete signaling frame. Fail-closed: every
// malformed input is rejected without allocation beyond the frame's length.
[[nodiscard]] std::optional<SignalingMessage> ParseSignalingMessage(
    std::span<const std::uint8_t> frame) noexcept;

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_SIGNALING_PROTOCOL_H_
