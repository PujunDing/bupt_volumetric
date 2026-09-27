#include "bupt_volumetric/signaling/protocol.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bupt::volumetric {
namespace {

constexpr std::uint8_t kSignalingMagic = 0x53;  // 'S'
constexpr std::uint8_t kSignalingVersion = 1;
constexpr std::size_t kHeaderBytes = 12;

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

std::optional<std::uint16_t> ReadU16(std::span<const std::uint8_t> frame,
                                     std::size_t offset) {
  if (offset + 2 > frame.size()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(frame[offset]) << 8) |
      static_cast<std::uint16_t>(frame[offset + 1]));
}

std::optional<std::uint32_t> ReadU32(std::span<const std::uint8_t> frame,
                                     std::size_t offset) {
  if (offset + 4 > frame.size()) {
    return std::nullopt;
  }
  return (static_cast<std::uint32_t>(frame[offset]) << 24) |
         (static_cast<std::uint32_t>(frame[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(frame[offset + 2]) << 8) |
         static_cast<std::uint32_t>(frame[offset + 3]);
}

bool IsKnownType(std::uint8_t type) {
  return type >= static_cast<std::uint8_t>(SignalingMessageType::kRegister) &&
         type <= static_cast<std::uint8_t>(SignalingMessageType::kBye);
}

// Peer-to-peer messages (offer/answer/candidate) must name a target peer;
// register/error/bye are addressed to the signaling server and may omit it.
bool RequiresPeerTarget(SignalingMessageType type) {
  return type == SignalingMessageType::kOffer ||
         type == SignalingMessageType::kAnswer ||
         type == SignalingMessageType::kIceCandidate;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> SerializeSignalingMessage(
    const SignalingMessage& message) noexcept {
  if (message.from_peer_id.empty() || !IsKnownType(
          static_cast<std::uint8_t>(message.type))) {
    return std::nullopt;
  }
  if (RequiresPeerTarget(message.type) && message.to_peer_id.empty()) {
    return std::nullopt;
  }
  if (message.from_peer_id.size() > kMaxPeerIdBytes ||
      message.to_peer_id.size() > kMaxPeerIdBytes ||
      message.payload.size() > kMaxSignalingPayloadBytes) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> frame;
  frame.reserve(kHeaderBytes + message.from_peer_id.size() +
                message.to_peer_id.size() + message.payload.size());
  frame.push_back(kSignalingMagic);
  frame.push_back(kSignalingVersion);
  frame.push_back(static_cast<std::uint8_t>(message.type));
  frame.push_back(0);  // reserved flags
  PutU16(frame, static_cast<std::uint16_t>(message.from_peer_id.size()));
  PutU16(frame, static_cast<std::uint16_t>(message.to_peer_id.size()));
  PutU32(frame, static_cast<std::uint32_t>(message.payload.size()));
  frame.insert(frame.end(), message.from_peer_id.begin(),
               message.from_peer_id.end());
  frame.insert(frame.end(), message.to_peer_id.begin(),
               message.to_peer_id.end());
  frame.insert(frame.end(), message.payload.begin(), message.payload.end());
  return frame;
}

std::optional<SignalingMessage> ParseSignalingMessage(
    std::span<const std::uint8_t> frame) noexcept {
  if (frame.size() < kHeaderBytes) {
    return std::nullopt;
  }
  if (frame[0] != kSignalingMagic) {
    return std::nullopt;
  }
  if (frame[1] != kSignalingVersion) {
    return std::nullopt;
  }
  if (!IsKnownType(frame[2])) {
    return std::nullopt;
  }
  const auto from_len = ReadU16(frame, 4);
  const auto to_len = ReadU16(frame, 6);
  const auto payload_len = ReadU32(frame, 8);
  if (!from_len || !to_len || !payload_len) {
    return std::nullopt;
  }
  if (*from_len > kMaxPeerIdBytes || *to_len > kMaxPeerIdBytes ||
      *payload_len > kMaxSignalingPayloadBytes) {
    return std::nullopt;
  }
  const std::size_t body = static_cast<std::size_t>(*from_len) +
                           static_cast<std::size_t>(*to_len) +
                           static_cast<std::size_t>(*payload_len);
  if (kHeaderBytes + body != frame.size()) {
    return std::nullopt;
  }

  std::size_t cursor = kHeaderBytes;
  SignalingMessage message;
  message.type = static_cast<SignalingMessageType>(frame[2]);
  message.from_peer_id.assign(frame.begin() + cursor,
                              frame.begin() + cursor + *from_len);
  cursor += *from_len;
  message.to_peer_id.assign(frame.begin() + cursor,
                            frame.begin() + cursor + *to_len);
  cursor += *to_len;
  message.payload.assign(frame.begin() + cursor,
                         frame.begin() + cursor + *payload_len);

  if (message.from_peer_id.empty()) {
    return std::nullopt;
  }
  return message;
}

}  // namespace bupt::volumetric
