#ifndef BUPT_VOLUMETRIC_RTC_CONTROL_CHANNEL_H_
#define BUPT_VOLUMETRIC_RTC_CONTROL_CHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bupt::volumetric {

// Control messages are small; anything larger is a media payload and is
// rejected. Main point-cloud bytes must never travel on the control channel.
inline constexpr std::size_t kMaxControlPayloadBytes = 16 * 1024;

// Reliable, ordered run-control message types. Only these may travel on the
// control DataChannel; there is no media type.
enum class ControlMessageType : std::uint8_t {
  kCapabilities = 1,
  kStart = 2,
  kStop = 3,
  kClockProbe = 4,
  kClockProbeAck = 5,
  kSummary = 6,
  kAck = 7,
};

// One validated control message. `payload` carries a bounded JSON/UTF-8 body
// (run config, summary, or an empty ping/pong).
struct ControlMessage final {
  ControlMessageType type = ControlMessageType::kCapabilities;
  std::string payload;
};

// Serializes a control message into the bounded binary envelope. Returns
// std::nullopt when the type is unknown or the payload exceeds the cap.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> SerializeControlMessage(
    const ControlMessage& message) noexcept;

// Parses and validates one complete control frame, fail-closed. Oversized or
// malformed frames are rejected before the payload is copied out.
[[nodiscard]] std::optional<ControlMessage> ParseControlMessage(
    std::span<const std::uint8_t> frame) noexcept;

// Reliable, ordered run-control channel. It owns no DataChannel; the caller
// binds a transport (typically a webrtc::DataChannelInterface::Send adapter)
// and a message sink. Media payloads are rejected by the size cap in the
// serialization step.
class ControlChannel final {
 public:
  using SendFn = std::function<bool(std::span<const std::uint8_t> frame)>;
  using MessageFn = std::function<void(const ControlMessage& message)>;

  void SetTransport(SendFn send);
  void SetOnMessage(MessageFn on_message);

  // Validates, serializes, and sends one control message. Returns false when
  // the payload exceeds the cap, the type is unknown, or the transport rejects.
  bool Send(ControlMessageType type, std::string payload);

  // Handles one serialized control frame; malformed frames are counted and not
  // dispatched.
  void OnFrame(std::span<const std::uint8_t> frame);

  [[nodiscard]] std::uint64_t malformed_frame_count() const noexcept;

 private:
  SendFn send_;
  MessageFn on_message_;
  std::uint64_t malformed_frame_count_ = 0;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_CONTROL_CHANNEL_H_
