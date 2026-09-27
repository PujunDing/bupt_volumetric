#include "bupt_volumetric/rtc/control_channel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bupt::volumetric {
namespace {

constexpr std::uint8_t kControlMagic = 0x43;  // 'C'
constexpr std::uint8_t kControlVersion = 1;
constexpr std::size_t kControlHeaderBytes = 8;

bool IsKnownType(std::uint8_t type) {
  return type >= static_cast<std::uint8_t>(ControlMessageType::kCapabilities) &&
         type <= static_cast<std::uint8_t>(ControlMessageType::kAck);
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
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

}  // namespace

std::optional<std::vector<std::uint8_t>> SerializeControlMessage(
    const ControlMessage& message) noexcept {
  if (!IsKnownType(static_cast<std::uint8_t>(message.type)) ||
      message.payload.size() > kMaxControlPayloadBytes) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> frame;
  frame.reserve(kControlHeaderBytes + message.payload.size());
  frame.push_back(kControlMagic);
  frame.push_back(kControlVersion);
  frame.push_back(static_cast<std::uint8_t>(message.type));
  frame.push_back(0);  // reserved flags
  PutU32(frame, static_cast<std::uint32_t>(message.payload.size()));
  frame.insert(frame.end(), message.payload.begin(), message.payload.end());
  return frame;
}

std::optional<ControlMessage> ParseControlMessage(
    std::span<const std::uint8_t> frame) noexcept {
  if (frame.size() < kControlHeaderBytes) {
    return std::nullopt;
  }
  if (frame[0] != kControlMagic || frame[1] != kControlVersion) {
    return std::nullopt;
  }
  if (!IsKnownType(frame[2])) {
    return std::nullopt;
  }
  const auto payload_len = ReadU32(frame, 4);
  if (!payload_len || *payload_len > kMaxControlPayloadBytes) {
    return std::nullopt;
  }
  if (kControlHeaderBytes + *payload_len != frame.size()) {
    return std::nullopt;
  }

  ControlMessage message;
  message.type = static_cast<ControlMessageType>(frame[2]);
  message.payload.assign(frame.begin() + kControlHeaderBytes, frame.end());
  return message;
}

void ControlChannel::SetTransport(SendFn send) { send_ = std::move(send); }

void ControlChannel::SetOnMessage(MessageFn on_message) {
  on_message_ = std::move(on_message);
}

bool ControlChannel::Send(ControlMessageType type, std::string payload) {
  const std::optional<std::vector<std::uint8_t>> frame = SerializeControlMessage(
      ControlMessage{.type = type, .payload = std::move(payload)});
  if (!frame.has_value()) {
    return false;
  }
  return send_ && send_(*frame);
}

void ControlChannel::OnFrame(std::span<const std::uint8_t> frame) {
  const std::optional<ControlMessage> message = ParseControlMessage(frame);
  if (!message.has_value()) {
    ++malformed_frame_count_;
    return;
  }
  if (on_message_) {
    on_message_(*message);
  }
}

std::uint64_t ControlChannel::malformed_frame_count() const noexcept {
  return malformed_frame_count_;
}

}  // namespace bupt::volumetric
